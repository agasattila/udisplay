# SPDX-License-Identifier: MPL-2.0
# Copyright (c) 2026 Attila Agas

"""
`udisplay-gen serve`: a minimal uDisplay TCP device for testing a YAML file
against udisplay-client without firmware (issue #76).

The protocol state machine is the MicroPython backend's runtime
(runtime/udisplay_runtime.py), used unchanged. This module only adds what a
desktop test server needs around it: the blob pipeline, a socket loop with a
heartbeat timer, and logging of every inbound message in raw form.

Scope: TCP only, no auth, one client at a time, no STATE_UPDATE. A new
connection replaces the current one, so a client that reconnects after a
crash doesn't have to wait for the watchdog to drop its old socket.
"""
from __future__ import annotations

import math
import os
import select
import socket
import threading
import time
from typing import Callable, Optional

from .backends._shared import validate_blob_size
from .merkle import CHUNK_SIZE, chunk_hashes, compress, compute, root
from .runtime import udisplay_runtime as rt

DEFAULT_HOST = "0.0.0.0"
DEFAULT_PORT = 5555
HEARTBEAT_S = 5.0
# A client that stops reading must not wedge the single-threaded loop:
# a send that can't complete in this time drops the client.
SEND_TIMEOUT_S = 5.0
# Feed inbound bytes to the runtime in slices no larger than one framed
# message. TcpRx holds 2 * (MAX_MSG_SIZE + 2) bytes and at most
# MAX_MSG_SIZE + 1 of them are a pending partial frame, so a slice this
# size always fits; a bigger one can overflow it, and the runtime then
# throws the whole buffer away.
_FEED_SLICE = rt.UDISPLAY_MAX_MSG_SIZE + 2

# Largest blob the HANDSHAKE can describe at all (chunk_count is a u16).
# Anything up to this is served even if it breaks MAX_BLOB_SIZE or
# MAX_CHUNK_COUNT, so the client's own limit checks can be exercised.
_WIRE_MAX_CHUNKS = 0xFFFF

_MSG_NAMES = {v: k[len("MSG_"):] for k, v in vars(rt).items() if k.startswith("MSG_")}
_EVENT_NAMES = {v: k[len("UDISPLAY_EVENT_"):] for k, v in vars(rt).items()
                if k.startswith("UDISPLAY_EVENT_")}


def build_blob(yaml_bytes: bytes, *, enforce_limits: bool = True):
    """compress → chunk → hash → root, then split into the runtime's chunk
    lists. Returns (root, chunks, hashes, lens, blob_len).

    enforce_limits=True applies the same limits as `build` (64 KB blob,
    MAX_CHUNK_COUNT chunks). False only refuses what the HANDSHAKE cannot
    express, so oversized blobs reach the client unchanged."""
    if enforce_limits:
        blob, blob_root, hashes = compute(yaml_bytes)
        validate_blob_size(blob)
    else:
        blob = compress(yaml_bytes)
        if math.ceil(len(blob) / CHUNK_SIZE) > _WIRE_MAX_CHUNKS:
            raise ValueError(f"Blob needs more than {_WIRE_MAX_CHUNKS} chunks, "
                             "which the HANDSHAKE cannot describe.")
        hashes = chunk_hashes(blob)
        blob_root = root(hashes)
    chunks = [blob[i:i + CHUNK_SIZE] for i in range(0, len(blob), CHUNK_SIZE)]
    return blob_root, chunks, hashes, [len(c) for c in chunks], len(blob)


def describe_message(msg: bytes) -> str:
    """One-line raw rendering of an inbound message: type byte, its name, and
    the payload as hex. EVENTs also get the widget ID and event type split
    out — still raw, no payload decoding."""
    if not msg:
        return "<empty message>"
    mtype = msg[0]
    name = _MSG_NAMES.get(mtype, "UNKNOWN")
    payload = msg[1:]
    text = f"0x{mtype:02X} {name} len={len(payload)}"
    if mtype == rt.MSG_EVENT and len(payload) >= 2:
        ev = payload[1]
        text += (f" widget_id={payload[0]} event=0x{ev:02X}"
                 f" {_EVENT_NAMES.get(ev, 'UNKNOWN')}"
                 f" data=[{payload[2:].hex(' ')}]")
    else:
        text += f" payload=[{payload.hex(' ')}]"
    return text


class LoggingDevice(rt.UDisplayDevice):
    """UDisplayDevice that logs each inbound message before the runtime
    parses it, so malformed and unknown messages (which proto_parse drops
    silently) and events before CLIENT_READY (which the runtime drops) show
    up too."""

    def __init__(self, *args, log: Callable[[str], None], on_desync: Callable[[], None],
                 log_heartbeats: bool = False, **kwargs):
        super().__init__(*args, **kwargs)
        self._log = log
        self._on_desync = on_desync
        self._log_heartbeats = log_heartbeats

    def feed(self, data):
        for i in range(0, len(data), _FEED_SLICE):
            overflow = self.rx.feed(data[i:i + _FEED_SLICE], self._on_message)
            # After a drain the buffer starts at a frame boundary. A declared
            # length over MAX_MSG_SIZE never completes (tcp_unframe keeps
            # returning None), and the real frame boundary can't be recovered,
            # so give up on the stream now rather than wait for the overflow
            # and then misread payload bytes as frame headers.
            rx = self.rx
            declared = rx.buf[0] | rx.buf[1] << 8 if rx.used >= 2 else 0
            if overflow or declared > rt.UDISPLAY_MAX_MSG_SIZE:
                self._log(f"RX frame length {declared} over the protocol maximum "
                          f"({rt.UDISPLAY_MAX_MSG_SIZE}) — stream desynced, dropping client")
                rx.reset()
                self._on_desync()
                return

    # Overrides the runtime's per-frame dispatch hook to log before parsing.
    def _on_message(self, msg):
        if self._log_heartbeats or msg != bytes([rt.MSG_HEARTBEAT]):
            self._log("RX " + describe_message(msg))
        super()._on_message(msg)


class Server:
    """Listening socket + single-client loop around a LoggingDevice."""

    def __init__(self, yaml_bytes: bytes, *, host: str = DEFAULT_HOST, port: int = DEFAULT_PORT,
                 enforce_limits: bool = True, log: Callable[[str], None] = print,
                 verbose: bool = False, heartbeat_s: float = HEARTBEAT_S,
                 send_timeout_s: float = SEND_TIMEOUT_S):
        self.root, chunks, hashes, lens, self.blob_len = build_blob(
            yaml_bytes, enforce_limits=enforce_limits)
        self.chunk_count = len(chunks)
        self._chunks = (chunks, hashes, lens)
        self._log = log
        self._verbose = verbose
        self._heartbeat_s = heartbeat_s
        self._send_timeout_s = send_timeout_s
        self._stop = threading.Event()
        self._conn: Optional[socket.socket] = None
        self._device: Optional[LoggingDevice] = None
        self._drop = False

        self._listener = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        if os.name == "nt":
            # SO_REUSEADDR on Windows lets a second server bind the same port
            # silently; exclusive use gives the expected "address in use".
            self._listener.setsockopt(socket.SOL_SOCKET, socket.SO_EXCLUSIVEADDRUSE, 1)
        else:
            self._listener.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        try:
            self._listener.bind((host, port))
            self._listener.listen(1)
        except OSError:
            self._listener.close()
            raise
        self._listener.setblocking(False)

    @property
    def address(self):
        return self._listener.getsockname()

    def stop(self) -> None:
        self._stop.set()

    # ── connection handling ──────────────────────────────────────────────

    def _send(self, data: bytes) -> None:
        if self._drop:
            return
        if self._verbose and len(data) > 2:
            self._log("TX " + _MSG_NAMES.get(data[2], f"0x{data[2]:02X}"))
        try:
            self._send_all(data)
        except OSError as exc:  # includes socket.timeout
            self._log(f"send failed ({exc or 'timed out'}) — dropping client")
            self._drop = True

    def _send_all(self, data: bytes) -> None:
        """Like rt.tcp_send_all, but with one deadline for the whole message:
        a per-send() timeout restarts on every partial write, so a peer
        reading just fast enough could hold the loop for much longer."""
        deadline = time.monotonic() + self._send_timeout_s
        view = memoryview(data)
        while view:
            remaining = deadline - time.monotonic()
            if remaining <= 0:
                raise socket.timeout("timed out")
            self._conn.settimeout(remaining)
            n = self._conn.send(view)
            if not n:
                raise OSError("socket closed during send")
            view = view[n:]

    def _on_comms_error(self) -> None:
        self._log("comms watchdog fired (no client progress / heartbeat echo) — dropping client")
        self._drop = True

    def _on_desync(self) -> None:
        self._drop = True

    def _accept(self) -> None:
        try:
            conn, addr = self._listener.accept()
        except BlockingIOError:  # spurious readiness: nothing to accept
            return
        except OSError as exc:  # peer reset before accept, EMFILE, ...
            self._log(f"accept failed: {exc}")
            return
        try:
            conn.setblocking(True)
            conn.settimeout(self._send_timeout_s)
            conn.setsockopt(socket.IPPROTO_TCP, socket.TCP_NODELAY, 1)
        except OSError as exc:
            self._log(f"accept failed: {exc}")
            conn.close()
            return
        if self._conn is not None:
            self._log("new connection replaces the current client")
            self._close()
        self._conn = conn
        self._drop = False
        self._log(f"client connected: {addr[0]}:{addr[1]}")
        chunks, hashes, lens = self._chunks
        self._device = LoggingDevice(
            self.root, chunks, hashes, lens, self._send,
            on_client_ready=lambda: self._log("client ready (UI loaded)"),
            on_comms_error=self._on_comms_error,
            log=self._log, on_desync=self._on_desync, log_heartbeats=self._verbose,
        )
        self._device.on_connect()

    def _close(self) -> None:
        if self._device is not None:
            self._device.on_disconnect()
        if self._conn is not None:
            self._conn.close()
            self._log("client disconnected")
        self._conn = None
        self._device = None

    def serve_forever(self) -> None:
        next_hb = time.monotonic() + self._heartbeat_s
        try:
            while not self._stop.is_set():
                rlist = [self._listener] + ([self._conn] if self._conn else [])
                timeout = max(0.0, min(next_hb - time.monotonic(), 0.2))
                readable, _, _ = select.select(rlist, [], [], timeout)

                if self._conn in readable:
                    try:
                        data = self._conn.recv(4096)
                    except OSError:
                        data = b""
                    if data:
                        self._device.feed(data)
                    else:
                        self._drop = True
                if self._drop:
                    self._close()
                if self._listener in readable:
                    self._accept()

                now = time.monotonic()
                if now >= next_hb:
                    # Re-arm from now, not from the missed deadline: after a
                    # stall, catch-up heartbeats fired back to back would
                    # trip the watchdog before the client could echo.
                    next_hb = now + self._heartbeat_s
                    if self._device is not None:
                        self._device.heartbeat()
                        if self._drop:
                            self._close()
        finally:
            self._close()
            self._listener.close()
