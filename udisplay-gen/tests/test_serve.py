"""
Tests for `udisplay-gen serve` (issue #76): a real socket client walks the
bootstrap against udisplay_gen.serve.Server, the way udisplay-client does —
HANDSHAKE, HANDSHAKE_ACK, chunk headers + chunks, Merkle check, CLIENT_READY —
then sends events and checks they are logged raw.
"""
import hashlib
import os
import socket
import struct
import threading
import time
import zlib

import pytest
from click.testing import CliRunner

from udisplay_gen.cli import cli
from udisplay_gen.runtime import udisplay_runtime as rt
from udisplay_gen.serve import Server, build_blob, describe_message

VALID_YAML = b"""\
device:
  name: "Serve Test"
  version: "1.0"
widgets:
  btn:
    type: button
    label: "Go"
"""

INVALID_YAML = b"widgets: [unclosed\n  - \x00 not: yaml: at all"


class _Log:
    def __init__(self):
        self.lines = []
        self.lock = threading.Lock()

    def __call__(self, msg):
        with self.lock:
            self.lines.append(msg)

    def wait_for(self, needle, timeout=3.0):
        deadline = time.monotonic() + timeout
        while time.monotonic() < deadline:
            with self.lock:
                hits = [l for l in self.lines if needle in l]
            if hits:
                return hits
            time.sleep(0.01)
        raise AssertionError(f"{needle!r} not logged; got {self.lines}")


@pytest.fixture
def run_server():
    started = []

    def start(yaml_bytes, **kw):
        log = _Log()
        srv = Server(yaml_bytes, host="127.0.0.1", port=0, log=log, **kw)
        t = threading.Thread(target=srv.serve_forever, daemon=True)
        t.start()
        started.append((srv, t))
        return srv, log

    yield start
    for srv, t in started:
        srv.stop()
        t.join(timeout=3)
        assert not t.is_alive()


class _Client:
    def __init__(self, addr):
        self.sock = socket.create_connection(addr, timeout=3)
        self.rx = b""

    def send(self, msg):
        self.sock.sendall(rt.tcp_frame(msg))

    def recv(self, skip_heartbeats=True):
        while True:
            msg, used = rt.tcp_unframe(self.rx)
            if msg is not None:
                self.rx = self.rx[used:]
                if skip_heartbeats and msg == bytes([rt.MSG_HEARTBEAT]):
                    continue
                return msg
            data = self.sock.recv(4096)
            if not data:
                raise ConnectionError("server closed")
            self.rx += data

    def bootstrap(self):
        """Full client download; returns the decompressed YAML bytes."""
        hs = self.recv()
        assert hs[0] == rt.MSG_HANDSHAKE and hs[1] == rt.UDISPLAY_PROTO_VERSION
        root = hs[3:35]
        count, size = struct.unpack("<HH", hs[35:39])
        assert size == 256
        self.send(bytes([rt.MSG_HANDSHAKE_ACK, 0x00]))
        hashes, blob = [], b""
        for i in range(count):
            self.send(bytes([rt.MSG_CHUNK_HEADER_REQUEST]) + struct.pack("<H", i))
            hdr = self.recv()
            assert hdr[0] == rt.MSG_CHUNK_HEADER_RESPONSE
            hashes.append(hdr[1:33])
            self.send(bytes([rt.MSG_CHUNK_REQUEST]) + struct.pack("<H", i))
            resp = self.recv()
            assert resp[0] == rt.MSG_CHUNK_RESPONSE
            idx, n = struct.unpack("<HH", resp[1:5])
            assert idx == i
            chunk = resp[5:5 + n]
            assert hashlib.sha256(chunk.ljust(256, b"\0")).digest() == hashes[i]
            blob += chunk
        assert hashlib.sha256(b"".join(hashes)).digest() == root
        self.send(bytes([rt.MSG_CLIENT_READY]))
        return zlib.decompress(blob)

    def close(self):
        self.sock.close()


def test_valid_yaml_served_and_events_logged(run_server):
    srv, log = run_server(VALID_YAML)
    c = _Client(srv.address)
    assert c.bootstrap() == VALID_YAML
    log.wait_for("client ready")

    c.send(bytes([rt.MSG_EVENT, 3, rt.UDISPLAY_EVENT_BUTTON_CLICK]))
    c.send(bytes([rt.MSG_EVENT, 7, rt.UDISPLAY_EVENT_SLIDER_CHANGE]) + struct.pack("<f", 1.5))
    c.send(bytes([0x99, 0xAB]))  # unknown type: runtime drops it, log must not
    log.wait_for("widget_id=3 event=0x01 BUTTON_CLICK data=[]")
    log.wait_for("widget_id=7 event=0x02 SLIDER_CHANGE data=[00 00 c0 3f]")
    log.wait_for("0x99 UNKNOWN len=1 payload=[ab]")
    c.close()
    log.wait_for("client disconnected")


def test_invalid_yaml_served_unchanged(run_server):
    srv, _ = run_server(INVALID_YAML, enforce_limits=False)
    c = _Client(srv.address)
    assert c.bootstrap() == INVALID_YAML
    c.close()


def test_heartbeat_sent_and_echo_not_logged(run_server):
    srv, log = run_server(VALID_YAML, heartbeat_s=0.3)
    c = _Client(srv.address)
    c.recv()  # HANDSHAKE
    c.send(bytes([rt.MSG_CLIENT_READY]))
    assert c.recv(skip_heartbeats=False) == bytes([rt.MSG_HEARTBEAT])
    c.send(bytes([rt.MSG_HEARTBEAT]))
    c.send(bytes([rt.MSG_EVENT, 1, rt.UDISPLAY_EVENT_BUTTON_CLICK]))
    log.wait_for("BUTTON_CLICK")  # sent after the echo, so the echo was processed
    assert not [l for l in log.lines if "HEARTBEAT" in l]
    c.close()


def test_verbose_logs_tx_and_heartbeats(run_server):
    srv, log = run_server(VALID_YAML, verbose=True)
    c = _Client(srv.address)
    c.recv()
    log.wait_for("TX HANDSHAKE")
    c.send(bytes([rt.MSG_HEARTBEAT]))
    log.wait_for("RX 0x40 HEARTBEAT len=0")
    c.close()


def test_event_before_ready_still_logged(run_server):
    # The runtime drops EVENTs before CLIENT_READY; the log must still show them.
    srv, log = run_server(VALID_YAML)
    c = _Client(srv.address)
    c.recv()
    c.send(bytes([rt.MSG_EVENT, 5, rt.UDISPLAY_EVENT_BUTTON_CLICK]))
    log.wait_for("widget_id=5 event=0x01 BUTTON_CLICK")
    c.close()


def test_inbound_burst_larger_than_runtime_buffer(run_server):
    # The client sends every CHUNK_HEADER_REQUEST in one burst. 600 of them
    # (3000 bytes) exceed the runtime's 2052-byte reassembly buffer in one
    # recv(); none may be dropped.
    big = os.urandom(150_000)
    srv, log = run_server(big, enforce_limits=False)
    c = _Client(srv.address)
    hs = c.recv()
    count = struct.unpack("<H", hs[35:37])[0]
    assert count > 410
    c.sock.sendall(b"".join(
        rt.tcp_frame(bytes([rt.MSG_CHUNK_HEADER_REQUEST]) + struct.pack("<H", i))
        for i in range(count)))
    for _ in range(count):
        assert c.recv()[0] == rt.MSG_CHUNK_HEADER_RESPONSE
    assert not [l for l in log.lines if "overflow" in l]
    c.close()


def test_client_that_stops_reading_is_dropped(run_server):
    # Requests whose responses are never read fill the socket buffers; the
    # send timeout must drop that client, and the server must keep serving.
    big = os.urandom(150_000)
    srv, log = run_server(big, enforce_limits=False, send_timeout_s=0.3)
    stuck = socket.create_connection(srv.address, timeout=3)
    stuck.setsockopt(socket.SOL_SOCKET, socket.SO_RCVBUF, 4096)
    req = b"".join(rt.tcp_frame(bytes([rt.MSG_CHUNK_REQUEST]) + struct.pack("<H", i % 500))
                   for i in range(400))
    deadline = time.monotonic() + 10
    while not [l for l in log.lines if "send failed" in l]:
        assert time.monotonic() < deadline, log.lines
        try:
            stuck.sendall(req)
        except OSError:
            break
        time.sleep(0.05)
    log.wait_for("client disconnected")
    c = _Client(srv.address)
    assert c.recv()[0] == rt.MSG_HANDSHAKE
    c.close()
    stuck.close()


def test_stall_does_not_burst_heartbeats(run_server, monkeypatch):
    # After the loop stalls for several intervals, one heartbeat goes out,
    # not a catch-up burst that trips the 3-miss watchdog.
    import udisplay_gen.serve as serve_mod
    real = time.monotonic
    offset = [0.0]
    monkeypatch.setattr(serve_mod.time, "monotonic", lambda: real() + offset[0])
    srv, log = run_server(VALID_YAML, heartbeat_s=0.2)
    c = _Client(srv.address)
    c.recv()
    c.send(bytes([rt.MSG_CLIENT_READY]))
    log.wait_for("client ready")
    offset[0] = 10.0  # jump 50 intervals ahead
    assert c.recv(skip_heartbeats=False) == bytes([rt.MSG_HEARTBEAT])
    c.send(bytes([rt.MSG_HEARTBEAT]))
    time.sleep(0.1)
    assert not [l for l in log.lines if "watchdog" in l]
    c.close()


def test_stalled_client_dropped_by_watchdog(run_server):
    srv, log = run_server(VALID_YAML, heartbeat_s=0.02)
    c = _Client(srv.address)
    log.wait_for("comms watchdog fired")
    log.wait_for("client disconnected")
    c.close()


def test_new_connection_replaces_current(run_server):
    srv, log = run_server(VALID_YAML)
    a = _Client(srv.address)
    a.recv()
    b = _Client(srv.address)
    assert b.recv()[0] == rt.MSG_HANDSHAKE
    log.wait_for("replaces the current client")
    with pytest.raises((ConnectionError, OSError)):
        a.recv()
    a.close()
    b.close()


def test_build_blob_limits():
    big = os.urandom(70_000)  # incompressible: > 64 KB compressed
    with pytest.raises(ValueError, match="64 KB"):
        build_blob(big)
    _, chunks, _, lens, blob_len = build_blob(big, enforce_limits=False)
    assert len(chunks) > 64 and sum(lens) == blob_len


def test_describe_message_raw():
    assert describe_message(b"") == "<empty message>"
    assert describe_message(bytes([0x31, 2])) == "0x31 EVENT len=1 payload=[02]"
    assert describe_message(bytes([0x31, 2, 4, 3, 0x61, 0x62, 0x63])) == \
        "0x31 EVENT len=6 widget_id=2 event=0x04 TEXT_SUBMIT data=[03 61 62 63]"


def test_cli_rejects_invalid_yaml_without_flag(tmp_path):
    f = tmp_path / "bad.yaml"
    f.write_bytes(INVALID_YAML)
    r = CliRunner().invoke(cli, ["serve", str(f), "--port", "0"])
    assert r.exit_code == 1
    assert "--no-validate" in r.output


def test_cli_rejects_schema_invalid_yaml(tmp_path):
    f = tmp_path / "bad.yaml"
    f.write_text('device:\n  name: "T"\nwidgets:\n  x:\n    type: nope\n')
    r = CliRunner().invoke(cli, ["serve", str(f), "--port", "0"])
    assert r.exit_code == 1
    assert "validation failed" in r.output


def test_cli_no_validate_serves_unchecked(tmp_path, monkeypatch):
    import udisplay_gen.serve as serve_mod
    seen = {}

    class FakeServer:
        def __init__(self, yaml_bytes, **kw):
            seen.update(kw, yaml=yaml_bytes)
            self.address = ("0.0.0.0", 5555)
            self.blob_len, self.chunk_count, self.root = 1, 1, bytes(32)

        def serve_forever(self):
            pass

    monkeypatch.setattr(serve_mod, "Server", FakeServer)
    f = tmp_path / "bad.yaml"
    f.write_bytes(INVALID_YAML)
    r = CliRunner().invoke(cli, ["serve", str(f), "--no-validate"])
    assert r.exit_code == 0, r.output
    assert seen["yaml"] == INVALID_YAML and seen["enforce_limits"] is False
    assert "this machine's IP address" in r.output


def test_cli_port_in_use(tmp_path):
    f = tmp_path / "ok.yaml"
    f.write_bytes(VALID_YAML)
    s = socket.socket()
    s.bind(("127.0.0.1", 0))
    s.listen(1)
    try:
        r = CliRunner().invoke(cli, ["serve", str(f), "--host", "127.0.0.1",
                                     "--port", str(s.getsockname()[1])])
    finally:
        s.close()
    assert r.exit_code == 1
    assert "cannot listen" in r.output
