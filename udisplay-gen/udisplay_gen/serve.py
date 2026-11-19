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
import select
import socket
import threading
import time
from typing import Callable, Optional

from .backends._shared import MAX_CHUNK_COUNT
from .merkle import CHUNK_SIZE, MAX_BLOB_SIZE, chunk_hashes, compress, root
from .runtime import udisplay_runtime as rt

DEFAULT_PORT = 5555
HEARTBEAT_S = 5.0

# Largest blob the HANDSHAKE can describe at all (chunk_count is a u16).
# Anything up to this is served even if it breaks MAX_BLOB_SIZE or
# MAX_CHUNK_COUNT, so the client's own limit checks can be exercised.
_WIRE_MAX_CHUNKS = 0xFFFF

_MSG_NAMES = {
    rt.MSG_HANDSHAKE: "HANDSHAKE",
    rt.MSG_HANDSHAKE_ACK: "HANDSHAKE_ACK",
    rt.MSG_CLIENT_READY: "CLIENT_READY",
    rt.MSG_CHUNK_HEADER_REQUEST: "CHUNK_HEADER_REQUEST",
    rt.MSG_CHUNK_HEADER_RESPONSE: "CHUNK_HEADER_RESPONSE",
    rt.MSG_CHUNK_REQUEST: "CHUNK_REQUEST",
    rt.MSG_CHUNK_RESPONSE: "CHUNK_RESPONSE",
    rt.MSG_ERR_INVALID_CHUNK: "ERR_INVALID_CHUNK",
    rt.MSG_HEARTBEAT: "HEARTBEAT",
    rt.MSG_STATE_UPDATE: "STATE_UPDATE",
    rt.MSG_EVENT: "EVENT",
    rt.MSG_SET_PROPERTY: "SET_PROPERTY",
    rt.MSG_RESET_PROPERTY: "RESET_PROPERTY",
}

_EVENT_NAMES = {
    rt.UDISPLAY_EVENT_BUTTON_CLICK: "BUTTON_CLICK",
    rt.UDISPLAY_EVENT_SLIDER_CHANGE: "SLIDER_CHANGE",
    rt.UDISPLAY_EVENT_TOGGLE_CHANGE: "TOGGLE_CHANGE",
    rt.UDISPLAY_EVENT_TEXT_SUBMIT: "TEXT_SUBMIT",
    rt.UDISPLAY_EVENT_SELECTION_CHANGE: "SELECTION_CHANGE",
    rt.UDISPLAY_EVENT_BUTTON_PRESS: "BUTTON_PRESS",
    rt.UDISPLAY_EVENT_BUTTON_RELEASE: "BUTTON_RELEASE",
}


def build_blob(yaml_bytes: bytes, *, enforce_limits: bool = True):
    """compress → chunk → hash → root, then split into the runtime's chunk
    lists. Returns (root, chunks, hashes, lens, blob_len).

    enforce_limits=True applies the same limits as `build` (64 KB blob,
    MAX_CHUNK_COUNT chunks). False only refuses what the HANDSHAKE cannot
    express, so oversized blobs reach the client unchanged."""
    blob = compress(yaml_bytes)
    n = math.ceil(len(blob) / CHUNK_SIZE)
    if enforce_limits:
        if len(blob) > MAX_BLOB_SIZE:
            raise ValueError(
                f"Compressed blob is {len(blob):,} bytes — exceeds the 64 KB limit "
                f"({MAX_BLOB_SIZE:,} bytes)."
            )
        if n > MAX_CHUNK_COUNT:
            raise ValueError(
                f"Blob requires {n} chunks but protocol maximum is {MAX_CHUNK_COUNT}."
            )
    elif n > _WIRE_MAX_CHUNKS:
        raise ValueError(f"Blob requires {n} chunks; the HANDSHAKE can describe at most {_WIRE_MAX_CHUNKS}.")
    hashes = chunk_hashes(blob)
    chunks = [blob[i * CHUNK_SIZE:(i + 1) * CHUNK_SIZE] for i in range(n)]
    return root(hashes), chunks, hashes, [len(c) for c in chunks], len(blob)


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

    def __init__(self, *args, log: Callable[[str], None], log_heartbeats: bool = False, **kwargs):
        super().__init__(*args, **kwargs)
        self._log = log
        self._log_heartbeats = log_heartbeats

    def _on_message(self, msg):
        if self._log_heartbeats or msg != bytes([rt.MSG_HEARTBEAT]):
            self._log("RX " + describe_message(msg))
        super()._on_message(msg)


class Server:
    """Listening socket + single-client loop around a LoggingDevice."""

    def __init__(self, yaml_bytes: bytes, *, host: str = "0.0.0.0", port: int = DEFAULT_PORT,
                 enforce_limits: bool = True, log: Callable[[str], None] = print,
                 verbose: bool = False, heartbeat_s: float = HEARTBEAT_S):
        self.root, chunks, hashes, lens, self.blob_len = build_blob(
            yaml_bytes, enforce_limits=enforce_limits)
        self.chunk_count = len(chunks)
        self._chunks = (chunks, hashes, lens)
        self._log = log
        self._verbose = verbose
        self._heartbeat_s = heartbeat_s
        self._stop = threading.Event()
        self._conn: Optional[socket.socket] = None
        self._device: Optional[LoggingDevice] = None
        self._drop = False

        self._listener = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        self._listener.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        self._listener.bind((host, port))
        self._listener.listen(1)

    @property
    def address(self):
        return self._listener.getsockname()

    def stop(self) -> None:
        self._stop.set()

    # ── connection handling ──────────────────────────────────────────────

    def _send(self, data: bytes) -> None:
        if self._verbose and len(data) > 2:
            self._log("TX " + _MSG_NAMES.get(data[2], f"0x{data[2]:02X}"))
        try:
            rt.tcp_send_all(self._conn, data)
        except OSError as exc:
            self._log(f"send failed: {exc}")
            self._drop = True

    def _on_comms_error(self) -> None:
        self._log("comms watchdog fired (no client progress / heartbeat echo) — dropping client")
        self._drop = True

    def _accept(self) -> None:
        conn, addr = self._listener.accept()
        if self._conn is not None:
            self._log("new connection replaces the current client")
            self._close()
        conn.setsockopt(socket.IPPROTO_TCP, socket.TCP_NODELAY, 1)
        self._conn = conn
        self._drop = False
        self._log(f"client connected: {addr[0]}:{addr[1]}")
        chunks, hashes, lens = self._chunks
        self._device = LoggingDevice(
            self.root, chunks, hashes, lens, self._send,
            on_client_ready=lambda: self._log("client ready (UI loaded)"),
            on_comms_error=self._on_comms_error,
            log=self._log, log_heartbeats=self._verbose,
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

                if time.monotonic() >= next_hb:
                    next_hb += self._heartbeat_s
                    if self._device is not None:
                        self._device.heartbeat()
                        if self._drop:
                            self._close()
        finally:
            self._close()
            self._listener.close()
