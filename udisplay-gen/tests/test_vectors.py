"""
Canonical protocol vector tests.
Validates that udisplay-gen produces byte-for-byte identical output to
tests/protocol_vectors.json. Any failure here is a protocol compatibility bug.
"""
import hashlib
import math
import struct
import zlib

import pytest

from udisplay_gen.merkle import CHUNK_SIZE, chunk_hashes, compute, root
from udisplay_gen.widget_ids import assign


# ── Merkle vectors ────────────────────────────────────────────────────────────

class TestMerkleVectors:
    def test_v1_tiny_single_chunk(self, vectors):
        v = vectors["merkle"]["v1_tiny_single_chunk"]
        yaml_bytes = bytes.fromhex(v["yaml_hex"])
        blob, r, hashes = compute(yaml_bytes)
        assert blob.hex() == v["blob_hex"]
        assert r.hex() == v["root_hex"]
        assert len(hashes) == v["n_chunks"] == 1
        assert [h.hex() for h in hashes] == v["chunk_hashes"]

    def test_v2_raw_exact_256_no_padding(self, vectors):
        v = vectors["merkle"]["v2_raw_exact_256_no_padding"]
        blob = bytes.fromhex(v["blob_hex"])
        assert len(blob) == 256
        hashes = chunk_hashes(blob)
        assert len(hashes) == 1
        assert hashes[0].hex() == v["chunk_hashes"][0]
        assert root(hashes).hex() == v["root_hex"]

    def test_v3_multi_chunk(self, vectors):
        v = vectors["merkle"]["v3_multi_chunk_yaml"]
        yaml_bytes = bytes.fromhex(v["yaml_hex"])
        blob, r, hashes = compute(yaml_bytes)
        assert blob.hex() == v["blob_hex"]
        assert r.hex() == v["root_hex"]
        assert len(hashes) == v["n_chunks"] == 2
        assert [h.hex() for h in hashes] == v["chunk_hashes"]

    def test_v4_raw_max_padding(self, vectors):
        """1 byte in last chunk → 255 zero bytes appended before hash."""
        v = vectors["merkle"]["v4_raw_max_padding"]
        blob = bytes.fromhex(v["blob_hex"])
        assert len(blob) == 257
        hashes = chunk_hashes(blob)
        assert len(hashes) == 2
        # Last chunk: 1 raw byte padded to 256
        last_padded = blob[256:].ljust(CHUNK_SIZE, b"\x00")
        assert len(last_padded) == CHUNK_SIZE
        assert hashes[1] == hashlib.sha256(last_padded).digest()
        assert root(hashes).hex() == v["root_hex"]

    def test_v5_full_vocabulary_root(self, vectors):
        v = vectors["merkle"]["v5_full_vocabulary"]
        yaml_bytes = bytes.fromhex(v["yaml_hex"])
        blob, r, hashes = compute(yaml_bytes)
        assert r.hex() == v["root_hex"]

    def test_v5_widget_id_assignment(self, vectors):
        """Widget IDs are assigned alphabetically by key path starting at 0x10."""
        v = vectors["merkle"]["v5_full_vocabulary"]
        import yaml as pyyaml
        doc = pyyaml.safe_load(bytes.fromhex(v["yaml_hex"]))
        ids = assign(doc["widgets"])
        expected = {k: int(vid, 16) for k, vid in v["widget_ids"].items()}
        assert ids == expected


# ── Message encoding vectors ──────────────────────────────────────────────────

def _bytes(vec: dict) -> bytes:
    return bytes.fromhex(vec["bytes"].replace(" ", ""))


class TestMessageVectors:
    def test_handshake(self, vectors):
        """HANDSHAKE layout since proto 0x04 (auth): msg_type, proto_version,
        flags, root[32], chunk_count u16, chunk_size u16 = 39 bytes. The
        flags byte (offset 2) shifted merkle_root from offset 2 to offset 3
        versus the pre-auth 38-byte layout."""
        v = vectors["messages"]["HANDSHAKE"]
        raw = _bytes(v)
        assert raw[0] == 0x00        # msg_type
        assert raw[1] == v["input"]["proto_version"]  # proto_version
        assert raw[2] == int(v["input"]["flags"], 16)  # flags: 0x00 = no auth
        root_hex = raw[3:35].hex()
        assert root_hex == v["input"]["merkle_root"]
        chunk_count = struct.unpack("<H", raw[35:37])[0]
        chunk_size  = struct.unpack("<H", raw[37:39])[0]
        assert chunk_count == v["input"]["chunk_count"]
        assert chunk_size  == v["input"]["chunk_size"] == 256
        assert len(raw) == 39

    def test_handshake_ack(self, vectors):
        raw = _bytes(vectors["messages"]["HANDSHAKE_ACK"])
        v = vectors["messages"]["HANDSHAKE_ACK"]
        assert raw[0] == 0x01
        assert raw[1] == v["input"]["client_proto_max"]  # 0x02 as of PROTO_VERSION bump

    def test_chunk_header_request(self, vectors):
        v = vectors["messages"]["CHUNK_HEADER_REQUEST"]
        raw = _bytes(v)
        assert raw[0] == 0x10
        assert len(raw) == 3
        idx = struct.unpack("<H", raw[1:3])[0]
        assert idx == v["input"]["chunk_index"]

    def test_chunk_header_response_full(self, vectors):
        v = vectors["messages"]["CHUNK_HEADER_RESPONSE_full"]
        raw = _bytes(v)
        assert raw[0] == 0x11
        assert len(raw) == 34
        assert raw[1:33].hex() == v["input"]["chunk_hash"]
        assert raw[33] == v["input"]["len_byte"] == 0

    def test_chunk_header_response_partial(self, vectors):
        v = vectors["messages"]["CHUNK_HEADER_RESPONSE_partial"]
        raw = _bytes(v)
        assert raw[0] == 0x11
        assert len(raw) == 34
        assert raw[1:33].hex() == v["input"]["chunk_hash"]
        assert raw[33] == v["input"]["len_byte"] == 3

    def test_chunk_request_0(self, vectors):
        raw = _bytes(vectors["messages"]["CHUNK_REQUEST_index_0"])
        assert raw[0] == 0x20
        assert struct.unpack("<H", raw[1:3])[0] == 0

    def test_chunk_request_5(self, vectors):
        raw = _bytes(vectors["messages"]["CHUNK_REQUEST_index_5"])
        assert raw[0] == 0x20
        assert struct.unpack("<H", raw[1:3])[0] == 5

    def test_chunk_response(self, vectors):
        v = vectors["messages"]["CHUNK_RESPONSE"]
        raw = _bytes(v)
        assert raw[0] == 0x21
        idx   = struct.unpack("<H", raw[1:3])[0]
        clen  = struct.unpack("<H", raw[3:5])[0]
        cdata = raw[5:]
        assert idx == 0
        assert len(cdata) == clen
        assert cdata.hex() == v["input"]["chunk_data_hex"]

    def test_err_invalid_chunk(self, vectors):
        raw = _bytes(vectors["messages"]["ERR_INVALID_CHUNK"])
        assert raw[0] == 0xFF
        assert struct.unpack("<H", raw[1:3])[0] == 5

    def test_heartbeat(self, vectors):
        assert _bytes(vectors["messages"]["HEARTBEAT"]) == bytes([0x40])

    def test_state_update_float32(self, vectors):
        raw = _bytes(vectors["messages"]["STATE_UPDATE_float32"])
        assert raw[0] == 0x30
        assert raw[1] == 0x10
        assert raw[2] == 0x01
        val = struct.unpack("<f", raw[3:])[0]
        assert abs(val - 3.14) < 1e-4

    def test_state_update_int32(self, vectors):
        raw = _bytes(vectors["messages"]["STATE_UPDATE_int32"])
        assert raw[2] == 0x02
        assert struct.unpack("<i", raw[3:])[0] == -42

    def test_state_update_uint8(self, vectors):
        raw = _bytes(vectors["messages"]["STATE_UPDATE_uint8"])
        assert raw[2] == 0x03
        assert raw[3] == 0x01

    def test_state_update_string(self, vectors):
        raw = _bytes(vectors["messages"]["STATE_UPDATE_string"])
        assert raw[2] == 0x04
        length = raw[3]
        assert raw[4 : 4 + length].decode() == "hello"

    def test_set_property(self, vectors):
        raw = _bytes(vectors["messages"]["SET_PROPERTY"])
        assert raw[0] == 0x32   # MSG_SET_PROPERTY
        assert raw[1] == 0x10   # target widget
        assert raw[2] == 0x01   # PROP_ENABLED
        assert raw[3] == 0x00   # disabled
        assert len(raw) == 4

    def test_reset_property(self, vectors):
        raw = _bytes(vectors["messages"]["RESET_PROPERTY"])
        assert raw[0] == 0x33   # MSG_RESET_PROPERTY
        assert raw[1] == 0x10   # target widget
        assert raw[2] == 0x01   # PROP_ENABLED
        assert len(raw) == 3

    def test_event_button_click(self, vectors):
        raw = _bytes(vectors["messages"]["EVENT_button_click"])
        assert raw == bytes([0x31, 0x10, 0x01])

    def test_event_button_press(self, vectors):
        raw = _bytes(vectors["messages"]["EVENT_button_press"])
        assert raw == bytes([0x31, 0x10, 0x06])

    def test_event_button_release(self, vectors):
        raw = _bytes(vectors["messages"]["EVENT_button_release"])
        assert raw == bytes([0x31, 0x10, 0x07])

    def test_event_slider_change(self, vectors):
        raw = _bytes(vectors["messages"]["EVENT_slider_change"])
        assert raw[0] == 0x31 and raw[1] == 0x11 and raw[2] == 0x02
        assert abs(struct.unpack("<f", raw[3:])[0] - 75.0) < 1e-4

    def test_event_toggle_change(self, vectors):
        raw = _bytes(vectors["messages"]["EVENT_toggle_change"])
        assert raw == bytes([0x31, 0x12, 0x03, 0x01])

    def test_event_text_submit(self, vectors):
        raw = _bytes(vectors["messages"]["EVENT_text_submit"])
        assert raw[0] == 0x31 and raw[1] == 0x13 and raw[2] == 0x04
        length = raw[3]
        assert raw[4 : 4 + length].decode() == "hello"

    def test_event_selection_change(self, vectors):
        raw = _bytes(vectors["messages"]["EVENT_selection_change"])
        assert raw[0] == 0x31           # MSG_EVENT
        assert raw[1] == 0x14           # widget_id
        assert raw[2] == 0x05           # EVT_SELECTION_CHANGE
        assert raw[3] == 2              # index
        assert len(raw) == 4

    def test_ble_fragment_reassembly(self, vectors):
        """
        Reassembling two v2.2 (offset+packet_id) BLE fragments yields the
        original CHUNK_HEADER_RESPONSE_full.

        First-fragment header: [u16 offset=0][u8 packet_id][u16 length][u8 flags]
        Continuation header:   [u16 offset][u8 packet_id]
        (Supersedes the old v1 1-byte more_fragments-flag scheme.)
        """
        first = _bytes(vectors["messages"]["BLE_fragment_first"])
        last  = _bytes(vectors["messages"]["BLE_fragment_last"])
        full  = _bytes(vectors["messages"]["CHUNK_HEADER_RESPONSE_full"])

        first_offset    = struct.unpack("<H", first[0:2])[0]
        first_packet_id = first[2]
        first_length    = struct.unpack("<H", first[3:5])[0]
        first_flags     = first[5]
        first_payload   = first[6:]
        assert first_offset == 0
        assert first_flags == 0x00
        assert first_length == len(full)

        last_offset    = struct.unpack("<H", last[0:2])[0]
        last_packet_id = last[2]
        last_payload   = last[3:]
        assert last_offset == len(first_payload)
        assert last_packet_id == first_packet_id

        reassembled = first_payload + last_payload
        assert reassembled == full

    def test_tcp_framing(self, vectors):
        """TCP frame: u16_le length + payload."""
        raw = _bytes(vectors["messages"]["TCP_framed_STATE_float32"])
        payload_len = struct.unpack("<H", raw[:2])[0]
        payload = raw[2:]
        assert len(payload) == payload_len
        # Payload should be a valid STATE_UPDATE_float32
        state = _bytes(vectors["messages"]["STATE_UPDATE_float32"])
        assert payload == state
