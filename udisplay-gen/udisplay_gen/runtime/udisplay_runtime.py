# SPDX-License-Identifier: Apache-2.0
# Copyright (c) 2026 Attila Agas

"""
uDisplay device-side protocol runtime, pure Python.

Implements the wire protocol specified in docs/protocol.md and the
canonical protocol vectors (tests/protocol_vectors.json) — those are the
authoritative source of truth, not any single implementation. libudisplay
(libudisplay/src/{udisplay,protocol,chunk_server,framing}.c) is the
corresponding C implementation of the same spec, useful here as a
compatibility/cross-check reference for the pieces this module covers, but
this module is an independent implementation, not required to reproduce a
libudisplay bug — see the CLIENT_READY/HANDSHAKE_ACK ordering issue tracked
separately in issue #12.

v0 scope (see docs/designs/micropython-backend.md, Approach A):
  - TCP transport only. BLE fragmentation is not implemented here.
  - No HMAC auth. auth_algo is always "none", matching the C and C++
    backends today (_shared.py's _config_fields() hardcodes the same).
  - No cryptographic hashing at runtime. The device only ever SERVES
    chunks and their pre-computed hashes (baked in at build time by
    udisplay-gen's merkle.py); verifying those hashes against the Merkle
    root is the desktop CLIENT's job, not the device's. See the
    device_never_hashes_client_verifies learning (2026-09-11) for the
    full reasoning — this was a real error caught during outside-voice
    review of the design and is not something to "fix" here.

This file is copied verbatim into generated output by python_backend.py —
it is a hand-written library, not templated/regenerated per YAML. It has
zero dependencies beyond the stdlib subset MicroPython and CPython share
(struct only), so the identical file runs unmodified on ESP8266, ESP32,
the MicroPython Unix port, and desktop CPython.
"""
import struct

# ── Protocol constants (mirrors udisplay.h) ─────────────────────────────────

UDISPLAY_PROTO_VERSION = 0x04
UDISPLAY_MAX_MSG_SIZE = 1024
UDISPLAY_HB_MISS_MAX = 3
UDISPLAY_AUTH_NONE = 0x00

UDISPLAY_EVENT_BUTTON_CLICK = 0x01
UDISPLAY_EVENT_SLIDER_CHANGE = 0x02
UDISPLAY_EVENT_TOGGLE_CHANGE = 0x03
UDISPLAY_EVENT_TEXT_SUBMIT = 0x04
UDISPLAY_EVENT_SELECTION_CHANGE = 0x05
UDISPLAY_EVENT_BUTTON_PRESS = 0x06
UDISPLAY_EVENT_BUTTON_RELEASE = 0x07

UDISPLAY_PROP_ENABLED = 0x01
UDISPLAY_PROP_VISIBLE = 0x02
UDISPLAY_PROP_MODE = 0x03
UDISPLAY_PROP_STYLE = 0x04

UDISPLAY_TRANSPORT_NONE = 0
UDISPLAY_TRANSPORT_TCP = 1
# UDISPLAY_TRANSPORT_BLE = 2  # not implemented in v0 (deferred, see module docstring)

# ── Message type bytes (mirrors protocol.h) ─────────────────────────────────

MSG_HANDSHAKE = 0x00
MSG_HANDSHAKE_ACK = 0x01
MSG_CLIENT_READY = 0x02
MSG_CHUNK_HEADER_REQUEST = 0x10
MSG_CHUNK_HEADER_RESPONSE = 0x11
MSG_CHUNK_REQUEST = 0x20
MSG_CHUNK_RESPONSE = 0x21
MSG_ERR_INVALID_CHUNK = 0xFF
MSG_HEARTBEAT = 0x40
MSG_STATE_UPDATE = 0x30
MSG_EVENT = 0x31
MSG_SET_PROPERTY = 0x32
MSG_RESET_PROPERTY = 0x33

# ── Inbound message types (mirrors protocol.h's proto_inbound_type_t) ──────

PROTO_UNKNOWN = 0
PROTO_HANDSHAKE_ACK = 1
PROTO_CLIENT_READY = 2
PROTO_CHUNK_HEADER_REQUEST = 3
PROTO_CHUNK_REQUEST = 4
PROTO_EVENT = 5
PROTO_HEARTBEAT = 6


# ── Outbound encode (mirrors protocol.c's proto_* encoders) ────────────────

def proto_handshake(root, chunk_count):
    """HANDSHAKE, no-auth (39 bytes): msg_type, proto_version, flags=0x00,
    root[32], chunk_count u16, chunk_size u16 (always 256)."""
    return bytes([MSG_HANDSHAKE, UDISPLAY_PROTO_VERSION, 0x00]) + root + struct.pack("<HH", chunk_count, 256)


def proto_heartbeat():
    return bytes([MSG_HEARTBEAT])


def proto_state_float(widget_id, value):
    return bytes([MSG_STATE_UPDATE, widget_id, 0x01]) + struct.pack("<f", value)


def proto_state_int32(widget_id, value):
    return bytes([MSG_STATE_UPDATE, widget_id, 0x02]) + struct.pack("<i", value)


def proto_state_uint8(widget_id, value):
    return bytes([MSG_STATE_UPDATE, widget_id, 0x03, value])


def _utf8_safe_truncate(b, maxlen):
    """Truncate `b` (assumed valid UTF-8) to at most `maxlen` bytes without
    splitting a multi-byte codepoint. A raw byte cut can only land inside
    the LAST codepoint (everything before it was already valid), so back
    off up to 3 bytes -- the longest UTF-8 sequence -- until it decodes
    clean. Found via adversarial review, 2026-09-11: a raw b[:255] cut
    emitted invalid UTF-8 on the wire for e.g. "e-acute"*128."""
    if len(b) <= maxlen:
        return b
    b = b[:maxlen]
    for _ in range(4):
        try:
            b.decode("utf-8")
            return b
        except UnicodeError:
            # CPython raises UnicodeDecodeError (a UnicodeError subclass);
            # MicroPython raises plain UnicodeError -- catch the common
            # base so this runs unmodified on both (verified against a
            # real MicroPython 1.24.1 build, 2026-09-11).
            b = b[:-1]
    return b


def proto_state_string(widget_id, s):
    b = s.encode() if isinstance(s, str) else bytes(s)
    if len(b) > 255:
        # The wire format's length field is one byte (matches the C
        # reference's `uint8_t len` parameter, which truncates implicitly
        # at the call site) -- truncate here instead of crashing on
        # bytes([...]) below. Found via red-team review, 2026-09-11:
        # proto_state_string(widget_id, "x"*300) raised ValueError before
        # this fix, crashing device-controlled code (a long label or
        # accumulated sensor string), not just attacker-supplied input.
        b = _utf8_safe_truncate(b, 255)
    return bytes([MSG_STATE_UPDATE, widget_id, 0x04, len(b)]) + b


def proto_set_property(target_id, property_id, value):
    return bytes([MSG_SET_PROPERTY, target_id, property_id, value])


def proto_reset_property(target_id, property_id):
    return bytes([MSG_RESET_PROPERTY, target_id, property_id])


def proto_chunk_header_response(chunk_hash, len_byte):
    """(34 bytes): msg_type, hash[32], len_byte. len_byte=0 means a full
    256-byte chunk; 1-255 means the partial last chunk."""
    return bytes([MSG_CHUNK_HEADER_RESPONSE]) + chunk_hash + bytes([len_byte])


def proto_chunk_response(idx, data):
    return bytes([MSG_CHUNK_RESPONSE]) + struct.pack("<HH", idx, len(data)) + data


def proto_err_invalid_chunk(idx):
    return bytes([MSG_ERR_INVALID_CHUNK]) + struct.pack("<H", idx)


# ── Inbound decode (mirrors protocol.c's proto_parse) ───────────────────────

class Inbound:
    """Parsed inbound message. Mirrors proto_inbound_t's C union — only the
    fields relevant to `type` are meaningful."""
    __slots__ = ("type", "chunk_idx", "widget_id", "event_type", "payload")

    def __init__(self):
        self.type = PROTO_UNKNOWN
        self.chunk_idx = 0
        self.widget_id = 0
        self.event_type = 0
        self.payload = b""


def proto_parse(msg):
    """Parse one inbound message. Returns an Inbound, or None on malformed
    or unknown input (mirrors proto_parse's 0 return — auth (HANDSHAKE_ACK
    credential) fields are intentionally not parsed here, since v0 has no
    auth; see module docstring)."""
    if len(msg) == 0:
        return None

    mtype = msg[0]
    out = Inbound()

    if mtype == MSG_HANDSHAKE_ACK:
        if len(msg) < 2:
            return None
        out.type = PROTO_HANDSHAKE_ACK
        return out

    if mtype == MSG_CLIENT_READY:
        out.type = PROTO_CLIENT_READY
        return out

    if mtype == MSG_HEARTBEAT:
        out.type = PROTO_HEARTBEAT
        return out

    if mtype == MSG_CHUNK_HEADER_REQUEST:
        if len(msg) < 3:
            return None
        out.type = PROTO_CHUNK_HEADER_REQUEST
        out.chunk_idx = struct.unpack("<H", msg[1:3])[0]
        return out

    if mtype == MSG_CHUNK_REQUEST:
        if len(msg) < 3:
            return None
        out.type = PROTO_CHUNK_REQUEST
        out.chunk_idx = struct.unpack("<H", msg[1:3])[0]
        return out

    if mtype == MSG_EVENT:
        if len(msg) < 3:
            return None
        out.type = PROTO_EVENT
        out.widget_id = msg[1]
        out.event_type = msg[2]
        out.payload = msg[3:]
        return out

    return None  # unknown message type


# ── TCP framing (mirrors framing.c's TCP section) ───────────────────────────

def tcp_frame(msg):
    """Prepend a u16_le length prefix. Mirrors udisplay_tcp_frame."""
    return struct.pack("<H", len(msg)) + msg


def tcp_unframe(buf):
    """Parse one u16_le length-prefixed frame from the start of `buf`.
    Returns (payload_bytes, total_consumed) or (None, 0) if `buf` doesn't
    yet contain a complete frame. Mirrors udisplay_tcp_unframe."""
    if len(buf) < 2:
        return None, 0
    payload_len = struct.unpack("<H", buf[0:2])[0]
    if payload_len > UDISPLAY_MAX_MSG_SIZE:
        return None, 0
    if len(buf) < 2 + payload_len:
        return None, 0
    return bytes(buf[2:2 + payload_len]), 2 + payload_len


# Mirrors UDISPLAY_RX_BUF_SIZE in udisplay.h: worst case is up to
# (MAX_MSG_SIZE+1) leftover bytes from a prior partial frame, plus one full
# (MAX_MSG_SIZE+2) framed message.
UDISPLAY_RX_BUF_SIZE = 2 * (UDISPLAY_MAX_MSG_SIZE + 2)


class TcpRx:
    """Bounded inbound TCP reassembly buffer. Mirrors tcp_rx_t / tcp_rx_feed
    in framing.c: a pre-sized bytearray with in-place compaction and an
    explicit overflow signal — deliberately NOT unbounded `bytes`
    concatenation, which would silently drop the RAM-budget discipline the
    C version enforces (design review finding, 2026-09-11)."""
    __slots__ = ("buf", "used")

    def __init__(self):
        self.buf = bytearray(UDISPLAY_RX_BUF_SIZE)
        self.used = 0

    def reset(self):
        self.used = 0

    def feed(self, data, on_message):
        """Append `data`, then drain every complete frame now available,
        calling on_message(payload_bytes) once per frame — mirrors
        tcp_rx_feed's while loop, so multiple frames delivered in a single
        recv() are all processed. Returns True on overflow (the caller must
        reset() — the stream is desynced and cannot recover byte-by-byte,
        matching udisplay_feed's TCP overflow handling), False otherwise."""
        if self.used + len(data) > len(self.buf):
            return True
        self.buf[self.used:self.used + len(data)] = data
        self.used += len(data)

        while True:
            msg, consumed = tcp_unframe(memoryview(self.buf)[:self.used])
            if msg is None:
                break
            # Compact BEFORE dispatching: if on_message raises (a bug in a
            # decode path or a user callback), the buffer must already be
            # past this frame, or the connection gets permanently wedged --
            # every future feed() would re-parse and re-raise on the same
            # poisoned frame forever (found via red-team review, 2026-09-11:
            # confirmed this way with a real malformed-UTF-8 TEXT_SUBMIT
            # frame followed by an unrelated, well-formed HEARTBEAT frame).
            remaining = self.used - consumed
            if remaining:
                self.buf[0:remaining] = self.buf[consumed:self.used]
            self.used = remaining
            on_message(msg)
        return False


def tcp_send_all(sock, data):
    """Helper for a `send` callback: loop until every byte is written,
    handling a partial socket.send() return. In the C reference,
    framed_send_raw() hands a complete framed message to the firmware's
    send callback in ONE call — the callback itself (this helper, or your
    own) is responsible for actually writing every byte to a possibly-slow
    socket, exactly like a real firmware transport adapter must. Not part
    of UDisplayDevice's own state machine; provided for main.py to use."""
    view = memoryview(data)
    sent = 0
    while sent < len(view):
        n = sock.send(view[sent:])
        if not n:
            raise OSError("socket closed during send")
        sent += n


# ── Chunk server (mirrors chunk_server.c) ───────────────────────────────────

class ChunkServer:
    """Serves pre-baked chunks and their pre-computed hashes. Never computes
    a hash at runtime — see module docstring."""
    __slots__ = ("chunks", "chunk_hashes", "chunk_lens", "chunk_count")

    def __init__(self, chunks, chunk_hashes, chunk_lens):
        self.chunks = chunks
        self.chunk_hashes = chunk_hashes
        self.chunk_lens = chunk_lens
        self.chunk_count = len(chunks)

    def header_response(self, idx):
        if idx >= self.chunk_count:
            return proto_err_invalid_chunk(idx)
        length = self.chunk_lens[idx]
        len_byte = 0 if length == 256 else length
        return proto_chunk_header_response(self.chunk_hashes[idx], len_byte)

    def respond(self, idx):
        if idx >= self.chunk_count:
            return proto_err_invalid_chunk(idx)
        return proto_chunk_response(idx, self.chunks[idx])


# ── Device: connection lifecycle + dispatch (mirrors udisplay.c) ───────────

class UDisplayDevice:
    """Device-side uDisplay protocol state machine. TCP transport only, no
    auth (v0 scope — see module docstring).

    Unlike the C API, callbacks here are plain Python callables with no
    `userdata` parameter: Python closures capture their context naturally,
    so the C API's userdata-threading (needed because C has no closures)
    has no equivalent purpose here. This is a deliberate simplification,
    not a missing feature.

    `on_event(widget_id, event_type, value)` is a single generic dispatch
    point, not per-widget named callbacks — mapping widget_id to a named
    setter/callback is the generated glue file's job (python_backend.py),
    not this library's. See "Output ownership constraint" in the design doc.
    """

    def __init__(self, merkle_root, chunks, chunk_hashes, chunk_lens,
                 send, on_event=None, on_client_ready=None, on_comms_error=None):
        self.merkle_root = merkle_root
        self.chunk_srv = ChunkServer(chunks, chunk_hashes, chunk_lens)
        self.send = send
        self.on_event = on_event
        self.on_client_ready = on_client_ready
        self.on_comms_error = on_comms_error

        self.rx = TcpRx()
        self.connected = False
        self.active = False
        self.comms_miss_count = 0

    # ── lifecycle ────────────────────────────────────────────────────────

    def on_connect(self):
        """Call when a client connects. Sends HANDSHAKE immediately."""
        self.connected = True
        self.active = False
        self.comms_miss_count = 0
        self.rx.reset()
        self._send_always(proto_handshake(self.merkle_root, self.chunk_srv.chunk_count))

    def on_disconnect(self):
        """Call when the client disconnects. Resets bootstrap state."""
        self.connected = False
        self.active = False
        self.comms_miss_count = 0
        self.rx.reset()

    # ── inbound ──────────────────────────────────────────────────────────

    def feed(self, data):
        """Feed raw inbound TCP bytes. Mirrors udisplay_feed's TCP case:
        reassembles, dispatching udisplay_on_message once per complete
        frame; resets the reassembly buffer on overflow (stream desynced,
        cannot recover byte-by-byte)."""
        if self.rx.feed(data, self._on_message):
            self.rx.reset()

    def _on_message(self, msg):
        in_ = proto_parse(msg)
        if in_ is None:
            return  # malformed / unknown: silently dropped, matches proto_parse's 0 return

        # NOTE: comms_miss_count is reset per-case below, matching
        # udisplay_on_message's switch exactly — PROTO_EVENT deliberately
        # does NOT reset it. Only HEARTBEAT and bootstrap-progress messages
        # count as proof of life; an EVENT does not, even though a client
        # sending one is clearly alive. This is faithful C behavior, not a
        # bug to fix.

        if in_.type == PROTO_HANDSHAKE_ACK:
            self.comms_miss_count = 0
            # v0 has no auth; nothing else to do (see module docstring).

        elif in_.type == PROTO_CLIENT_READY:
            self.comms_miss_count = 0
            if not self.active:
                self.active = True
                if self.on_client_ready:
                    self.on_client_ready()

        elif in_.type == PROTO_HEARTBEAT:
            self.comms_miss_count = 0

        elif in_.type == PROTO_CHUNK_HEADER_REQUEST:
            self.comms_miss_count = 0
            self._send_always(self.chunk_srv.header_response(in_.chunk_idx))

        elif in_.type == PROTO_CHUNK_REQUEST:
            self.comms_miss_count = 0
            self._send_always(self.chunk_srv.respond(in_.chunk_idx))

        elif in_.type == PROTO_EVENT:
            if self.active:
                self._dispatch_event(in_)

    def _dispatch_event(self, in_):
        if not self.on_event:
            return
        payload = in_.payload
        event_type = in_.event_type
        widget_id = in_.widget_id

        if event_type == UDISPLAY_EVENT_SLIDER_CHANGE:
            value = struct.unpack("<f", payload[0:4])[0] if len(payload) >= 4 else 0.0
        elif event_type == UDISPLAY_EVENT_TOGGLE_CHANGE:
            value = payload[0] if len(payload) >= 1 else 0
        elif event_type == UDISPLAY_EVENT_TEXT_SUBMIT:
            if len(payload) >= 1:
                length = payload[0]
                # errors="replace", not strict decode: payload bytes come
                # directly from an unauthenticated TCP peer (v0 has no
                # auth). A single invalid UTF-8 byte must not raise --
                # this module's own contract (see docstring, and
                # proto_parse's malformed-input handling) is that bad
                # input is handled gracefully, never crashes the caller.
                # Found via specialist + red-team review, 2026-09-11.
                value = bytes(payload[1:1 + length]).decode("utf-8", "replace") if length > 0 else ""
            else:
                value = ""
        elif event_type == UDISPLAY_EVENT_SELECTION_CHANGE:
            value = payload[0] if len(payload) >= 1 else 0
        else:
            # BUTTON_CLICK/PRESS/RELEASE and any unknown type: no payload.
            value = None

        self.on_event(widget_id, event_type, value)

    # ── heartbeat watchdog ───────────────────────────────────────────────

    def heartbeat(self):
        """Call periodically (e.g. every 5s) from your own timer. Single
        miss-count watchdog covering both connection phases — mirrors
        udisplay_heartbeat exactly:
          - BOOTSTRAP (connected, not active): resets on HANDSHAKE_ACK,
            CLIENT_READY, CHUNK_HEADER_REQUEST, CHUNK_REQUEST.
          - ACTIVE: resets only on a HEARTBEAT echo.
        Fires on_comms_error() once, the instant the count reaches
        UDISPLAY_HB_MISS_MAX — not on every subsequent miss."""
        self._send_always(proto_heartbeat())
        if self.connected and self.comms_miss_count < UDISPLAY_HB_MISS_MAX:
            self.comms_miss_count += 1
            if self.comms_miss_count == UDISPLAY_HB_MISS_MAX:
                if self.on_comms_error:
                    self.on_comms_error()

    # ── state senders ────────────────────────────────────────────────────

    def send_float(self, widget_id, value):
        self._send(proto_state_float(widget_id, value))

    def send_int(self, widget_id, value):
        self._send(proto_state_int32(widget_id, value))

    def send_bool(self, widget_id, value):
        self._send(proto_state_uint8(widget_id, 1 if value else 0))

    def send_uint8(self, widget_id, value):
        self._send(proto_state_uint8(widget_id, value))

    def send_string(self, widget_id, s):
        self._send(proto_state_string(widget_id, s))

    def set_property(self, target_id, property_id, value):
        self._send(proto_set_property(target_id, property_id, value))

    def reset_property(self, target_id, property_id):
        self._send(proto_reset_property(target_id, property_id))

    # ── outbound framing ─────────────────────────────────────────────────

    def _send(self, msg):
        """Send only while connected and active. Mirrors
        framed_send_raw(always=0)."""
        if not self.connected or not self.send or not msg:
            return
        if not self.active:
            return
        self.send(tcp_frame(msg))

    def _send_always(self, msg):
        """Send even before `active` (bootstrap). Mirrors
        framed_send_raw(always=1)."""
        if not self.connected or not self.send or not msg:
            return
        self.send(tcp_frame(msg))
