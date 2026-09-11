"""
Tests for udisplay_gen.runtime.udisplay_runtime — the device-side MicroPython
protocol port (docs/designs/micropython-backend.md).

Two groups:
  - TestRuntimeMessageVectors: cross-checks this module's encoders against
    tests/protocol_vectors.json, the same fixture already shared with the C
    and C++ implementations (extends TestMessageVectors in test_vectors.py,
    which only validated the fixture's internal consistency — no Python
    encoder existed to check against it before this).
  - TestRuntimeStateMachine: the Test Plan (v0) items from the design's
    eng-review — heartbeat watchdog, malformed messages, all 5 event types,
    TCP reassembly edge cases, chunk out-of-range handling.
"""
import struct

import pytest

from udisplay_gen.runtime.udisplay_runtime import (
    ChunkServer,
    Inbound,
    PROTO_CHUNK_HEADER_REQUEST,
    PROTO_CHUNK_REQUEST,
    PROTO_CLIENT_READY,
    PROTO_EVENT,
    PROTO_HANDSHAKE_ACK,
    PROTO_HEARTBEAT,
    TcpRx,
    UDisplayDevice,
    UDISPLAY_EVENT_BUTTON_CLICK,
    UDISPLAY_EVENT_SELECTION_CHANGE,
    UDISPLAY_EVENT_SLIDER_CHANGE,
    UDISPLAY_EVENT_TEXT_SUBMIT,
    UDISPLAY_EVENT_TOGGLE_CHANGE,
    UDISPLAY_HB_MISS_MAX,
    UDISPLAY_MAX_MSG_SIZE,
    MSG_CHUNK_REQUEST,
    MSG_CLIENT_READY,
    MSG_EVENT,
    MSG_HANDSHAKE_ACK,
    MSG_HEARTBEAT,
    proto_chunk_header_response,
    proto_chunk_response,
    proto_err_invalid_chunk,
    proto_handshake,
    proto_heartbeat,
    proto_parse,
    proto_reset_property,
    proto_set_property,
    proto_state_float,
    proto_state_int32,
    proto_state_string,
    proto_state_uint8,
    tcp_frame,
    tcp_send_all,
    tcp_unframe,
)


def _vec_bytes(vec: dict) -> bytes:
    return bytes.fromhex(vec["bytes"].replace(" ", ""))


# ── Golden-vector encoder cross-check ───────────────────────────────────────

class TestRuntimeMessageVectors:
    def test_handshake(self, vectors):
        v = vectors["messages"]["HANDSHAKE"]
        expected = _vec_bytes(v)
        root = expected[3:35]
        chunk_count = v["input"]["chunk_count"]
        assert proto_handshake(root, chunk_count) == expected

    def test_heartbeat(self, vectors):
        assert proto_heartbeat() == _vec_bytes(vectors["messages"]["HEARTBEAT"])

    def test_state_update_float32(self, vectors):
        v = vectors["messages"]["STATE_UPDATE_float32"]
        widget_id = int(v["input"]["widget_id"], 16)
        assert proto_state_float(widget_id, v["input"]["value"]) == _vec_bytes(v)

    def test_state_update_int32(self, vectors):
        v = vectors["messages"]["STATE_UPDATE_int32"]
        widget_id = int(v["input"]["widget_id"], 16)
        assert proto_state_int32(widget_id, v["input"]["value"]) == _vec_bytes(v)

    def test_state_update_uint8(self, vectors):
        v = vectors["messages"]["STATE_UPDATE_uint8"]
        widget_id = int(v["input"]["widget_id"], 16)
        assert proto_state_uint8(widget_id, v["input"]["value"]) == _vec_bytes(v)

    def test_state_update_string(self, vectors):
        v = vectors["messages"]["STATE_UPDATE_string"]
        widget_id = int(v["input"]["widget_id"], 16)
        assert proto_state_string(widget_id, v["input"]["value"]) == _vec_bytes(v)

    def test_state_string_overlong_truncates_instead_of_raising(self):
        """Regression test (red-team finding, 2026-09-11): proto_state_string
        raised ValueError for strings encoding to >255 bytes (the wire
        format's length field is one byte) -- confirmed this crashed
        UDisplayDevice.send_string() / generated TextRwWidget.set() for
        ordinary device-controlled data like a long label or sensor string,
        not just attacker input. Must truncate, not raise."""
        result = proto_state_string(0x10, "x" * 300)
        assert len(result) <= 4 + 255  # header (4 bytes) + max 255-byte payload
        assert result[3] == 255  # declared length byte reflects the truncation

    def test_state_string_overlong_truncation_does_not_split_a_utf8_codepoint(self):
        """Regression test (adversarial review, 2026-09-11): the first
        truncation fix cut at a raw byte offset, which can land mid
        multi-byte codepoint and emit invalid UTF-8 on the wire (confirmed
        with "e-acute" * 128: cut to a dangling 0xC3 lead byte). Truncation
        must back off to a UTF-8 character boundary instead."""
        result = proto_state_string(0x10, "é" * 128)  # 2 bytes/char, 256 bytes total
        declared_len = result[3]
        payload = result[4:4 + declared_len]
        assert len(payload) < 256  # did truncate
        payload.decode("utf-8")  # must not raise -- no split codepoint

        # 4-byte codepoints (astral plane) exercise the full backoff range.
        result2 = proto_state_string(0x11, "\U0001F389" * 100)
        payload2 = result2[4:4 + result2[3]]
        payload2.decode("utf-8")

    def test_set_property(self, vectors):
        v = vectors["messages"]["SET_PROPERTY"]
        target_id = int(v["input"]["target_id"], 16)
        property_id = int(v["input"]["property_id"], 16)
        assert proto_set_property(target_id, property_id, v["input"]["value"]) == _vec_bytes(v)

    def test_reset_property(self, vectors):
        v = vectors["messages"]["RESET_PROPERTY"]
        target_id = int(v["input"]["target_id"], 16)
        property_id = int(v["input"]["property_id"], 16)
        assert proto_reset_property(target_id, property_id) == _vec_bytes(v)

    def test_chunk_header_response_full(self, vectors):
        v = vectors["messages"]["CHUNK_HEADER_RESPONSE_full"]
        chunk_hash = bytes.fromhex(v["input"]["chunk_hash"])
        assert proto_chunk_header_response(chunk_hash, v["input"]["len_byte"]) == _vec_bytes(v)

    def test_chunk_header_response_partial(self, vectors):
        v = vectors["messages"]["CHUNK_HEADER_RESPONSE_partial"]
        chunk_hash = bytes.fromhex(v["input"]["chunk_hash"])
        assert proto_chunk_header_response(chunk_hash, v["input"]["len_byte"]) == _vec_bytes(v)

    def test_chunk_response(self, vectors):
        v = vectors["messages"]["CHUNK_RESPONSE"]
        data = bytes.fromhex(v["input"]["chunk_data_hex"])
        assert proto_chunk_response(v["input"]["chunk_index"], data) == _vec_bytes(v)

    def test_err_invalid_chunk(self, vectors):
        v = vectors["messages"]["ERR_INVALID_CHUNK"]
        assert proto_err_invalid_chunk(v["input"]["chunk_index"]) == _vec_bytes(v)

    def test_tcp_framing(self, vectors):
        v = vectors["messages"]["TCP_framed_STATE_float32"]
        inner = vectors["messages"]["STATE_UPDATE_float32"]
        widget_id = int(inner["input"]["widget_id"], 16)
        payload = proto_state_float(widget_id, inner["input"]["value"])
        assert tcp_frame(payload) == _vec_bytes(v)

    def test_tcp_unframe_roundtrip(self, vectors):
        v = vectors["messages"]["TCP_framed_STATE_float32"]
        framed = _vec_bytes(v)
        payload, consumed = tcp_unframe(framed)
        assert consumed == len(framed)
        assert payload == _vec_bytes(vectors["messages"]["STATE_UPDATE_float32"])


# ── proto_parse (inbound decode) ────────────────────────────────────────────

class TestProtoParse:
    def test_handshake_ack(self):
        in_ = proto_parse(bytes([MSG_HANDSHAKE_ACK, 0x02]))
        assert in_.type == PROTO_HANDSHAKE_ACK

    def test_handshake_ack_too_short_is_malformed(self):
        assert proto_parse(bytes([MSG_HANDSHAKE_ACK])) is None

    def test_client_ready(self):
        assert proto_parse(bytes([MSG_CLIENT_READY])).type == PROTO_CLIENT_READY

    def test_heartbeat(self):
        assert proto_parse(bytes([MSG_HEARTBEAT])).type == PROTO_HEARTBEAT

    def test_empty_message_is_malformed(self):
        assert proto_parse(b"") is None

    def test_unknown_message_type_is_malformed(self):
        assert proto_parse(bytes([0xAB])) is None

    def test_event_button_click(self, vectors):
        raw = _vec_bytes(vectors["messages"]["EVENT_button_click"])
        in_ = proto_parse(raw)
        assert in_.type == PROTO_EVENT
        assert in_.widget_id == raw[1]
        assert in_.event_type == UDISPLAY_EVENT_BUTTON_CLICK

    def test_event_too_short_is_malformed(self):
        assert proto_parse(bytes([MSG_EVENT, 0x10])) is None


# ── ChunkServer ──────────────────────────────────────────────────────────────

class TestChunkServer:
    def test_header_response_full_chunk(self):
        srv = ChunkServer(chunks=[b"x" * 256], chunk_hashes=[b"h" * 32], chunk_lens=[256])
        resp = srv.header_response(0)
        in_ = proto_parse(resp)
        assert resp[0] == 0x11
        assert resp[33] == 0  # len_byte=0 for a full chunk

    def test_header_response_partial_chunk(self):
        srv = ChunkServer(chunks=[b"x" * 3], chunk_hashes=[b"h" * 32], chunk_lens=[3])
        resp = srv.header_response(0)
        assert resp[33] == 3

    def test_out_of_range_index_returns_error_not_crash(self):
        """Test Plan (v0): chunk serving for an out-of-range index returns
        the error response, not a crash — the device's actual
        integrity-adjacent responsibility (verifying a chunk's hash is the
        client's job, not tested here)."""
        srv = ChunkServer(chunks=[b"x" * 256], chunk_hashes=[b"h" * 32], chunk_lens=[256])
        resp = srv.header_response(5)
        assert resp[0] == 0xFF
        assert struct.unpack("<H", resp[1:3])[0] == 5

        resp2 = srv.respond(5)
        assert resp2[0] == 0xFF

    def test_boundary_idx_equal_to_count_is_invalid(self):
        """Pins the exact `>=` comparison chunk_server.py relies on: idx==
        chunk_count (one past the last valid index, not some arbitrary
        out-of-range value like idx=5 above) must also be rejected --
        catches an off-by-one regression the other out-of-range test
        wouldn't (testing specialist finding, 2026-09-11)."""
        srv = ChunkServer(chunks=[b"x" * 256, b"y" * 256],
                           chunk_hashes=[b"h" * 32, b"j" * 32], chunk_lens=[256, 256])
        resp = srv.header_response(2)  # chunk_count == 2
        assert resp[0] == 0xFF
        resp2 = srv.respond(2)
        assert resp2[0] == 0xFF


# ── TCP reassembly (Test Plan v0) ────────────────────────────────────────────

class TestTcpReassembly:
    def test_partial_frame_then_rest_arrives_later(self):
        messages = []
        rx = TcpRx()
        framed = tcp_frame(b"hello world")
        overflow1 = rx.feed(framed[:5], messages.append)
        assert overflow1 is False
        assert messages == []  # not enough bytes yet
        overflow2 = rx.feed(framed[5:], messages.append)
        assert overflow2 is False
        assert messages == [b"hello world"]

    def test_multiple_complete_frames_in_one_call(self):
        messages = []
        rx = TcpRx()
        combined = tcp_frame(b"first") + tcp_frame(b"second") + tcp_frame(b"third")
        rx.feed(combined, messages.append)
        assert messages == [b"first", b"second", b"third"]

    def test_overflow_at_exact_capacity_signals_true(self):
        from udisplay_gen.runtime.udisplay_runtime import UDISPLAY_RX_BUF_SIZE
        rx = TcpRx()
        too_big = b"x" * (UDISPLAY_RX_BUF_SIZE + 1)
        overflow = rx.feed(too_big, lambda m: None)
        assert overflow is True

    def test_buffer_compacts_after_draining_a_frame(self):
        """After a frame is consumed, `used` drops back down — proves this
        is bounded reuse, not unbounded growth."""
        rx = TcpRx()
        rx.feed(tcp_frame(b"one"), lambda m: None)
        assert rx.used == 0  # fully drained and compacted


# ── UDisplayDevice state machine (Test Plan v0) ─────────────────────────────

def _make_device(on_event=None, on_client_ready=None, on_comms_error=None, sent=None):
    sent_list = sent if sent is not None else []
    dev = UDisplayDevice(
        merkle_root=b"\x00" * 32,
        chunks=[b"x" * 256],
        chunk_hashes=[b"h" * 32],
        chunk_lens=[256],
        send=sent_list.append,
        on_event=on_event,
        on_client_ready=on_client_ready,
        on_comms_error=on_comms_error,
    )
    return dev, sent_list


class TestUDisplayDeviceLifecycle:
    def test_on_connect_sends_handshake(self):
        dev, sent = _make_device()
        dev.on_connect()
        assert dev.connected is True
        assert dev.active is False
        assert len(sent) == 1
        payload, _ = tcp_unframe(sent[0])
        assert payload[0] == 0x00  # MSG_HANDSHAKE

    def test_on_disconnect_resets_bootstrap_state(self):
        """Test Plan (v0): on_disconnect() resets bootstrap state correctly."""
        dev, _ = _make_device()
        dev.on_connect()
        dev.feed(tcp_frame(bytes([MSG_CLIENT_READY])))
        assert dev.active is True

        dev.on_disconnect()
        assert dev.connected is False
        assert dev.active is False
        assert dev.comms_miss_count == 0
        assert dev.rx.used == 0

    def test_malformed_message_does_not_raise(self):
        """Test Plan (v0): malformed/unknown message type is rejected
        without raising, matching the C dispatcher's behavior."""
        dev, sent = _make_device()
        dev.on_connect()
        sent.clear()
        dev.feed(tcp_frame(bytes([0xAB, 0x01, 0x02])))  # unknown msg type
        assert sent == []  # no crash, no response


class TestUDisplayDeviceEvents:
    """Test Plan (v0): all 5 event types through dispatch_event()."""

    def _active_device(self, on_event):
        dev, sent = _make_device(on_event=on_event)
        dev.on_connect()
        dev.feed(tcp_frame(bytes([MSG_CLIENT_READY])))
        sent.clear()
        return dev

    def test_button_click(self):
        events = []
        dev = self._active_device(lambda w, t, v: events.append((w, t, v)))
        dev.feed(tcp_frame(bytes([MSG_EVENT, 0x10, UDISPLAY_EVENT_BUTTON_CLICK])))
        assert events == [(0x10, UDISPLAY_EVENT_BUTTON_CLICK, None)]

    def test_slider_change(self):
        events = []
        dev = self._active_device(lambda w, t, v: events.append((w, t, v)))
        payload = bytes([MSG_EVENT, 0x11, UDISPLAY_EVENT_SLIDER_CHANGE]) + struct.pack("<f", 75.0)
        dev.feed(tcp_frame(payload))
        assert events[0][0:2] == (0x11, UDISPLAY_EVENT_SLIDER_CHANGE)
        assert abs(events[0][2] - 75.0) < 1e-4

    def test_toggle_change(self):
        events = []
        dev = self._active_device(lambda w, t, v: events.append((w, t, v)))
        dev.feed(tcp_frame(bytes([MSG_EVENT, 0x12, UDISPLAY_EVENT_TOGGLE_CHANGE, 0x01])))
        assert events == [(0x12, UDISPLAY_EVENT_TOGGLE_CHANGE, 1)]

    def test_text_submit(self):
        events = []
        dev = self._active_device(lambda w, t, v: events.append((w, t, v)))
        text = b"hello"
        payload = bytes([MSG_EVENT, 0x13, UDISPLAY_EVENT_TEXT_SUBMIT, len(text)]) + text
        dev.feed(tcp_frame(payload))
        assert events == [(0x13, UDISPLAY_EVENT_TEXT_SUBMIT, "hello")]

    def test_text_submit_invalid_utf8_does_not_crash_or_wedge_connection(self):
        """Regression test (security + testing specialists + red team,
        2026-09-11): a single malformed UTF-8 byte in a TEXT_SUBMIT payload
        from an unauthenticated TCP peer must not raise UnicodeDecodeError
        -- confirmed this crashed the whole demos/demo04/main.py process
        before the fix. Also confirmed the deeper bug: TcpRx.feed() didn't
        advance its buffer when dispatch raised, so the connection stayed
        permanently wedged -- every subsequent feed() re-raised the same
        error even for unrelated, well-formed frames. This test exercises
        both halves of the fix in one place."""
        events = []
        dev = self._active_device(lambda w, t, v: events.append((w, t, v)))
        bad = bytes([MSG_EVENT, 0x13, UDISPLAY_EVENT_TEXT_SUBMIT, 1, 0xFF])
        dev.feed(tcp_frame(bad))  # must not raise
        assert events and events[0][0:2] == (0x13, UDISPLAY_EVENT_TEXT_SUBMIT)

        # connection must not be wedged: an unrelated, well-formed frame
        # fed right after must still work normally.
        dev.feed(tcp_frame(bytes([MSG_HEARTBEAT])))
        assert dev.comms_miss_count == 0

    def test_selection_change(self):
        events = []
        dev = self._active_device(lambda w, t, v: events.append((w, t, v)))
        dev.feed(tcp_frame(bytes([MSG_EVENT, 0x14, UDISPLAY_EVENT_SELECTION_CHANGE, 2])))
        assert events == [(0x14, UDISPLAY_EVENT_SELECTION_CHANGE, 2)]

    def test_event_before_active_is_ignored(self):
        """dispatch_event only fires once ctx->active (mirrors udisplay.c's
        `if (ctx->active) dispatch_event(...)` guard)."""
        events = []
        dev, sent = _make_device(on_event=lambda w, t, v: events.append((w, t, v)))
        dev.on_connect()  # connected, but not yet active
        dev.feed(tcp_frame(bytes([MSG_EVENT, 0x10, UDISPLAY_EVENT_BUTTON_CLICK])))
        assert events == []


class TestUDisplayDeviceSenders:
    """Test Plan (v0): set_property / reset_property senders, not just the
    one state-update sender the round-trip criterion covers."""

    def _active_device(self):
        dev, sent = _make_device()
        dev.on_connect()
        dev.feed(tcp_frame(bytes([MSG_CLIENT_READY])))
        sent.clear()
        return dev, sent

    def test_send_float(self):
        dev, sent = self._active_device()
        dev.send_float(0x10, 3.14)
        payload, _ = tcp_unframe(sent[0])
        assert payload == proto_state_float(0x10, 3.14)

    def test_set_property(self):
        dev, sent = self._active_device()
        dev.set_property(0x10, 0x01, 0)
        payload, _ = tcp_unframe(sent[0])
        assert payload == proto_set_property(0x10, 0x01, 0)

    def test_reset_property(self):
        dev, sent = self._active_device()
        dev.reset_property(0x10, 0x01)
        payload, _ = tcp_unframe(sent[0])
        assert payload == proto_reset_property(0x10, 0x01)

    def test_send_before_active_is_dropped(self):
        """_send() (as opposed to _send_always()) requires active — mirrors
        framed_send_raw(always=0)."""
        dev, sent = _make_device()
        dev.on_connect()  # connected, not active
        sent.clear()  # discard the HANDSHAKE that on_connect() itself sent
        dev.send_float(0x10, 1.0)
        assert sent == []


class TestUDisplayDeviceHeartbeat:
    """Test Plan (v0): heartbeat watchdog reaches HB_MISS_MAX and triggers
    teardown/on_comms_error, both during bootstrap stalls and after active."""

    def test_heartbeat_always_sends(self):
        dev, sent = _make_device()
        dev.on_connect()
        sent.clear()
        dev.heartbeat()
        payload, _ = tcp_unframe(sent[0])
        assert payload == bytes([MSG_HEARTBEAT])

    def test_fires_on_comms_error_at_exactly_hb_miss_max(self):
        errors = []
        dev, _ = _make_device(on_comms_error=lambda: errors.append(1))
        dev.on_connect()

        for i in range(UDISPLAY_HB_MISS_MAX - 1):
            dev.heartbeat()
            assert errors == []  # not yet

        dev.heartbeat()  # the Nth miss
        assert errors == [1]  # fires exactly once, right at the threshold

        dev.heartbeat()  # further misses must not re-fire
        assert errors == [1]

    def test_bootstrap_miss_count_resets_on_chunk_request(self):
        """BOOTSTRAP phase: resets on CHUNK_HEADER_REQUEST/CHUNK_REQUEST,
        not just HANDSHAKE_ACK/CLIENT_READY."""
        dev, _ = _make_device()
        dev.on_connect()
        dev.heartbeat()
        dev.heartbeat()
        assert dev.comms_miss_count == 2

        dev.feed(tcp_frame(bytes([0x10, 0x00, 0x00])))  # CHUNK_HEADER_REQUEST idx=0
        assert dev.comms_miss_count == 0

    def test_active_miss_count_does_not_reset_on_event(self):
        """ACTIVE phase: only a HEARTBEAT echo resets the miss count — an
        EVENT does not, even though the client is clearly alive. Faithful
        to udisplay_on_message's switch, not a bug."""
        events = []
        dev, sent = _make_device(on_event=lambda w, t, v: events.append(1))
        dev.on_connect()
        dev.feed(tcp_frame(bytes([MSG_CLIENT_READY])))
        dev.heartbeat()
        dev.heartbeat()
        assert dev.comms_miss_count == 2

        dev.feed(tcp_frame(bytes([MSG_EVENT, 0x10, UDISPLAY_EVENT_BUTTON_CLICK])))
        assert events == [1]
        assert dev.comms_miss_count == 2  # unchanged — matches the C reference

        dev.feed(tcp_frame(bytes([MSG_HEARTBEAT])))
        assert dev.comms_miss_count == 0


# ── Coverage-audit gap fill (2026-09-11 ship-workflow audit) ───────────────
#
# The 46 tests above (Test Plan v0) are thorough for the encode/decode/
# lifecycle surface. The classes below close specific branches that survived
# that pass untested: the send_int/send_bool/send_uint8/send_string device
# methods (only send_float + set_property/reset_property were exercised),
# ChunkServer.respond()'s happy path and the MSG_CHUNK_REQUEST dispatch arm
# in _on_message (only header_response and the out-of-range error path were
# covered), _dispatch_event's defensive fallbacks for truncated/absent
# payloads and a missing on_event callback, tcp_unframe's oversized-length
# guard, and tcp_send_all (previously zero coverage).

class TestUDisplayDeviceRemainingSenders:
    """send_int/send_bool/send_uint8/send_string as device-level wired
    methods -- the encoders they call were already vector-tested, but the
    device methods themselves (and the active-gating they inherit from
    _send()) were not."""

    def _active_device(self):
        dev, sent = _make_device()
        dev.on_connect()
        dev.feed(tcp_frame(bytes([MSG_CLIENT_READY])))
        sent.clear()
        return dev, sent

    def test_send_int(self):
        dev, sent = self._active_device()
        dev.send_int(0x10, -7)
        payload, _ = tcp_unframe(sent[0])
        assert payload == proto_state_int32(0x10, -7)

    def test_send_bool_and_send_uint8(self):
        dev, sent = self._active_device()
        dev.send_bool(0x10, True)
        dev.send_uint8(0x11, 42)
        p0, _ = tcp_unframe(sent[0])
        p1, _ = tcp_unframe(sent[1])
        assert p0 == proto_state_uint8(0x10, 1)  # bool coerced to 0/1
        assert p1 == proto_state_uint8(0x11, 42)

    def test_send_string(self):
        dev, sent = self._active_device()
        dev.send_string(0x12, "hi")
        payload, _ = tcp_unframe(sent[0])
        assert payload == proto_state_string(0x12, "hi")


class TestChunkRequestDispatch:
    """ChunkServer.respond()'s valid-index happy path, and the
    PROTO_CHUNK_REQUEST arm of UDisplayDevice._on_message -- previously only
    the header_response path and the shared out-of-range error path were
    covered for either the server or the device dispatch."""

    def test_chunk_server_respond_valid_index_returns_chunk_response(self):
        srv = ChunkServer(chunks=[b"abc", b"xyz"], chunk_hashes=[b"h" * 32, b"j" * 32],
                           chunk_lens=[3, 3])
        resp = srv.respond(1)
        assert resp == proto_chunk_response(1, b"xyz")

    def test_device_dispatches_chunk_request_to_chunk_server(self):
        dev, sent = _make_device()
        dev.chunk_srv = ChunkServer(chunks=[b"y" * 256], chunk_hashes=[b"h" * 32],
                                     chunk_lens=[256])
        dev.on_connect()
        sent.clear()
        dev.feed(tcp_frame(bytes([MSG_CHUNK_REQUEST]) + struct.pack("<H", 0)))
        payload, _ = tcp_unframe(sent[0])
        assert payload == proto_chunk_response(0, b"y" * 256)


class TestDispatchEventFallbacks:
    def test_no_on_event_callback_does_not_raise(self):
        """_dispatch_event's `if not self.on_event: return` guard -- a
        device built without on_event must not crash on an EVENT while
        active."""
        dev, sent = _make_device(on_event=None)
        dev.on_connect()
        dev.feed(tcp_frame(bytes([MSG_CLIENT_READY])))
        dev.feed(tcp_frame(bytes([MSG_EVENT, 0x10, UDISPLAY_EVENT_BUTTON_CLICK])))  # must not raise

    def test_truncated_payloads_fall_back_to_defaults(self):
        """Each event type's payload-too-short guard in _dispatch_event:
        slider_change<4 bytes -> 0.0, toggle_change empty -> 0,
        selection_change empty -> 0, text_submit empty -> ''."""
        events = []
        dev, sent = _make_device(on_event=lambda w, t, v: events.append((t, v)))
        dev.on_connect()
        dev.feed(tcp_frame(bytes([MSG_CLIENT_READY])))

        dev.feed(tcp_frame(bytes([MSG_EVENT, 0x10, UDISPLAY_EVENT_SLIDER_CHANGE, 0x01, 0x02])))
        dev.feed(tcp_frame(bytes([MSG_EVENT, 0x11, UDISPLAY_EVENT_TOGGLE_CHANGE])))
        dev.feed(tcp_frame(bytes([MSG_EVENT, 0x12, UDISPLAY_EVENT_SELECTION_CHANGE])))
        dev.feed(tcp_frame(bytes([MSG_EVENT, 0x13, UDISPLAY_EVENT_TEXT_SUBMIT])))

        assert events == [
            (UDISPLAY_EVENT_SLIDER_CHANGE, 0.0),
            (UDISPLAY_EVENT_TOGGLE_CHANGE, 0),
            (UDISPLAY_EVENT_SELECTION_CHANGE, 0),
            (UDISPLAY_EVENT_TEXT_SUBMIT, ""),
        ]


class TestTcpUnframeGuards:
    def test_rejects_oversized_length_prefix(self):
        """A length prefix beyond UDISPLAY_MAX_MSG_SIZE must not be treated
        as a valid frame -- mirrors udisplay_tcp_unframe's bound check, and
        matters because a device that trusted this field could be made to
        wait forever for a frame that can never legally arrive."""
        buf = struct.pack("<H", UDISPLAY_MAX_MSG_SIZE + 1) + b"x" * 10
        payload, consumed = tcp_unframe(buf)
        assert payload is None
        assert consumed == 0


class TestTcpSendAll:
    """tcp_send_all had zero coverage: no test in the original 46 called it
    at all."""

    class _FakeSocket:
        def __init__(self, chunk_sizes):
            self.chunk_sizes = list(chunk_sizes)
            self.sent = bytearray()

        def send(self, view):
            n = self.chunk_sizes.pop(0) if self.chunk_sizes else len(view)
            n = min(n, len(view))
            self.sent.extend(bytes(view[:n]))
            return n

    def test_handles_partial_sends_until_all_bytes_written(self):
        data = b"0123456789"
        # Each send() only accepts part of the buffer (3, then 4, then
        # whatever's left) -- proves the helper loops on a partial write
        # instead of assuming one send() call writes everything.
        sock = self._FakeSocket(chunk_sizes=[3, 4, 3])
        tcp_send_all(sock, data)
        assert bytes(sock.sent) == data

    def test_raises_when_socket_reports_zero_bytes_sent(self):
        """A 0 return from send() means the socket closed mid-send; the
        helper must raise rather than spin forever."""
        class _ClosedSocket:
            def send(self, view):
                return 0

        with pytest.raises(OSError):
            tcp_send_all(_ClosedSocket(), b"hello")
