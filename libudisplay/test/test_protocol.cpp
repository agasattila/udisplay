/**
 * Protocol encode/decode tests.
 *
 * Expected byte sequences are taken verbatim from tests/protocol_vectors.json
 * so any divergence is a protocol compatibility bug.
 */
#include <gtest/gtest.h>
#include "protocol.h"
#include <cstring>
#include <cmath>

/* ── Helpers ─────────────────────────────────────────────────────────────── */

static std::vector<uint8_t> encode(uint16_t (*fn)(uint8_t*, uint16_t),
                                    uint16_t cap = 512)
{
    std::vector<uint8_t> buf(cap);
    uint16_t n = fn(buf.data(), cap);
    buf.resize(n);
    return buf;
}

/* ── HANDSHAKE ───────────────────────────────────────────────────────────── */

TEST(ProtoEncode, Handshake_39bytes)
{
    /* v1 merkle root from protocol_vectors.json — proto 0x04 format (39 bytes) */
    static const uint8_t root[32] = {
        0x74,0xac,0x9f,0xa6,0x84,0x28,0x7d,0x0c,
        0xab,0x3e,0x64,0xe3,0xf2,0x5e,0xe3,0x69,
        0xc7,0x4b,0x46,0xe7,0xf2,0xec,0x00,0x25,
        0xcd,0x10,0xba,0xc7,0xbd,0xff,0xdb,0x6e
    };
    uint8_t buf[64];
    uint16_t n = proto_handshake(buf, sizeof(buf), root, 1);
    ASSERT_EQ(n, 39u);

    EXPECT_EQ(buf[0], 0x00u);           /* msg_type */
    EXPECT_EQ(buf[1], 0x04u);           /* proto_version = UDISPLAY_PROTO_VERSION */
    EXPECT_EQ(buf[2], 0x00u);           /* flags = 0x00 (no auth) */
    EXPECT_EQ(memcmp(buf + 3, root, 32), 0);
    /* chunk_count = 1 little-endian at offset 35 */
    EXPECT_EQ(buf[35], 0x01u);
    EXPECT_EQ(buf[36], 0x00u);
    /* chunk_size = 256 little-endian at offset 37 */
    EXPECT_EQ(buf[37], 0x00u);
    EXPECT_EQ(buf[38], 0x01u);
}

TEST(ProtoEncode, Handshake_ExactBytes)
{
    /* Validates against protocol_vectors.json HANDSHAKE entry */
    static const uint8_t root[32] = {
        0x74,0xac,0x9f,0xa6,0x84,0x28,0x7d,0x0c,
        0xab,0x3e,0x64,0xe3,0xf2,0x5e,0xe3,0x69,
        0xc7,0x4b,0x46,0xe7,0xf2,0xec,0x00,0x25,
        0xcd,0x10,0xba,0xc7,0xbd,0xff,0xdb,0x6e
    };
    static const uint8_t expected[] = {
        0x00,0x04,0x00,  /* msg_type, proto_version, flags=0 */
        0x74,0xac,0x9f,0xa6,0x84,0x28,0x7d,0x0c,
        0xab,0x3e,0x64,0xe3,0xf2,0x5e,0xe3,0x69,
        0xc7,0x4b,0x46,0xe7,0xf2,0xec,0x00,0x25,
        0xcd,0x10,0xba,0xc7,0xbd,0xff,0xdb,0x6e,
        0x01,0x00,   /* chunk_count = 1 LE */
        0x00,0x01    /* chunk_size  = 256 LE */
    };
    uint8_t buf[64];
    uint16_t n = proto_handshake(buf, sizeof(buf), root, 1);
    ASSERT_EQ(n, 39u);
    EXPECT_EQ(memcmp(buf, expected, 39), 0);
}

TEST(ProtoEncode, Handshake_BufferTooSmall)
{
    uint8_t buf[38]; /* needs 39 */
    static const uint8_t root[32] = {};
    EXPECT_EQ(proto_handshake(buf, sizeof(buf), root, 1), 0u);
}

TEST(ProtoEncode, HandshakeAuth_36bytes)
{
    /* Validates against protocol_vectors.json HANDSHAKE_AUTH entry */
    static const uint8_t salt[32] = {
        0x01,0x02,0x03,0x04,0x05,0x06,0x07,0x08,
        0x09,0x0a,0x0b,0x0c,0x0d,0x0e,0x0f,0x10,
        0x11,0x12,0x13,0x14,0x15,0x16,0x17,0x18,
        0x19,0x1a,0x1b,0x1c,0x1d,0x1e,0x1f,0x20
    };
    static const uint8_t expected[] = {
        0x00,0x04,0x01,0x01,  /* msg_type, proto_version, flags=1, algo=SHA-256 */
        0x01,0x02,0x03,0x04,0x05,0x06,0x07,0x08,
        0x09,0x0a,0x0b,0x0c,0x0d,0x0e,0x0f,0x10,
        0x11,0x12,0x13,0x14,0x15,0x16,0x17,0x18,
        0x19,0x1a,0x1b,0x1c,0x1d,0x1e,0x1f,0x20
    };
    uint8_t buf[64];
    uint16_t n = proto_handshake_auth(buf, sizeof(buf), 0x01u, salt);
    ASSERT_EQ(n, 36u);
    EXPECT_EQ(memcmp(buf, expected, 36), 0);
}

TEST(ProtoEncode, HandshakeAuth_BufferTooSmall)
{
    uint8_t buf[35]; /* needs 36 */
    static const uint8_t salt[32] = {};
    EXPECT_EQ(proto_handshake_auth(buf, sizeof(buf), 0x01u, salt), 0u);
}

/* ── HEARTBEAT ───────────────────────────────────────────────────────────── */

TEST(ProtoEncode, Heartbeat)
{
    uint8_t buf[4];
    uint16_t n = proto_heartbeat(buf, sizeof(buf));
    ASSERT_EQ(n, 1u);
    EXPECT_EQ(buf[0], 0x40u);
}

/* ── ERR_INVALID_CHUNK ───────────────────────────────────────────────────── */

TEST(ProtoEncode, ErrInvalidChunk_Index5)
{
    /* expected: ff 05 00 */
    uint8_t buf[4];
    uint16_t n = proto_err_invalid_chunk(buf, sizeof(buf), 5u);
    ASSERT_EQ(n, 3u);
    EXPECT_EQ(buf[0], 0xFFu);
    EXPECT_EQ(buf[1], 0x05u);
    EXPECT_EQ(buf[2], 0x00u);
}

/* ── STATE_UPDATE ────────────────────────────────────────────────────────── */

TEST(ProtoEncode, StateFloat32_3_14)
{
    /* expected: 30 10 01 c3 f5 48 40 */
    static const uint8_t expected[] = {0x30,0x10,0x01,0xc3,0xf5,0x48,0x40};
    uint8_t buf[8];
    uint16_t n = proto_state_float(buf, sizeof(buf), 0x10u, 3.14f);
    ASSERT_EQ(n, 7u);
    EXPECT_EQ(memcmp(buf, expected, 7), 0);
}

TEST(ProtoEncode, StateInt32_Minus42)
{
    /* expected: 30 10 02 d6 ff ff ff */
    static const uint8_t expected[] = {0x30,0x10,0x02,0xd6,0xff,0xff,0xff};
    uint8_t buf[8];
    uint16_t n = proto_state_int32(buf, sizeof(buf), 0x10u, -42);
    ASSERT_EQ(n, 7u);
    EXPECT_EQ(memcmp(buf, expected, 7), 0);
}

TEST(ProtoEncode, StateUint8_True)
{
    /* expected: 30 10 03 01 */
    static const uint8_t expected[] = {0x30,0x10,0x03,0x01};
    uint8_t buf[8];
    uint16_t n = proto_state_uint8(buf, sizeof(buf), 0x10u, 1u);
    ASSERT_EQ(n, 4u);
    EXPECT_EQ(memcmp(buf, expected, 4), 0);
}

TEST(ProtoEncode, StateString_Hello)
{
    /* expected: 30 10 04 05 68 65 6c 6c 6f */
    static const uint8_t expected[] = {0x30,0x10,0x04,0x05,'h','e','l','l','o'};
    uint8_t buf[16];
    uint16_t n = proto_state_string(buf, sizeof(buf), 0x10u, "hello", 5u);
    ASSERT_EQ(n, 9u);
    EXPECT_EQ(memcmp(buf, expected, 9), 0);
}

TEST(ProtoEncode, SetProperty_Disable)
{
    /* expected: 32 10 01 00 */
    static const uint8_t expected[] = {0x32,0x10,0x01,0x00};
    uint8_t buf[8];
    uint16_t n = proto_set_property(buf, sizeof(buf), 0x10u, 0x01u, 0x00u);
    ASSERT_EQ(n, 4u);
    EXPECT_EQ(memcmp(buf, expected, 4), 0);
}

TEST(ProtoEncode, ResetProperty)
{
    /* expected: 33 10 01 */
    static const uint8_t expected[] = {0x33,0x10,0x01};
    uint8_t buf[8];
    uint16_t n = proto_reset_property(buf, sizeof(buf), 0x10u, 0x01u);
    ASSERT_EQ(n, 3u);
    EXPECT_EQ(memcmp(buf, expected, 3), 0);
}

TEST(ProtoEncode, SetProperty_BufferTooSmall)
{
    uint8_t buf[3]; /* need 4 */
    uint16_t n = proto_set_property(buf, sizeof(buf), 0x10u, 0x01u, 0x00u);
    EXPECT_EQ(n, 0u);
}

TEST(ProtoEncode, ResetProperty_BufferTooSmall)
{
    uint8_t buf[2]; /* need 3 */
    uint16_t n = proto_reset_property(buf, sizeof(buf), 0x10u, 0x01u);
    EXPECT_EQ(n, 0u);
}

/* ── CHUNK_HEADER_RESPONSE ───────────────────────────────────────────────── */

TEST(ProtoEncode, ChunkHeaderResponse_Full)
{
    /* v3 chunk-0 hash, full chunk → len_byte = 0 */
    static const uint8_t hash[32] = {
        0x83,0x41,0xcf,0xd5,0x9a,0x9f,0x9b,0x4c,
        0xed,0x5e,0x23,0xdf,0x0b,0x06,0x8f,0x5d,
        0xeb,0x11,0x20,0x40,0xf3,0xc4,0x41,0xb4,
        0x5a,0x49,0x95,0x2b,0x3b,0xb9,0xc1,0x09
    };
    uint8_t buf[64];
    uint16_t n = proto_chunk_header_response(buf, sizeof(buf), hash, 0u);
    ASSERT_EQ(n, 34u);
    EXPECT_EQ(buf[0], 0x11u);
    EXPECT_EQ(memcmp(buf + 1, hash, 32), 0);
    EXPECT_EQ(buf[33], 0x00u);   /* len_byte = 0: full 256-byte chunk */
}

TEST(ProtoEncode, ChunkHeaderResponse_Partial)
{
    /* Partial last chunk with 3 bytes → len_byte = 3 */
    static const uint8_t hash[32] = {
        0x2b,0x92,0xcb,0x09,0x57,0x66,0x4e,0x01,
        0x10,0x9e,0x2e,0xa5,0xa8,0x5e,0xef,0xdb,
        0x81,0xad,0x3f,0x76,0x94,0x0c,0x40,0x29,
        0x8e,0x4c,0xb1,0x58,0xa7,0xf3,0xca,0x85
    };
    uint8_t buf[64];
    uint16_t n = proto_chunk_header_response(buf, sizeof(buf), hash, 3u);
    ASSERT_EQ(n, 34u);
    EXPECT_EQ(buf[0], 0x11u);
    EXPECT_EQ(memcmp(buf + 1, hash, 32), 0);
    EXPECT_EQ(buf[33], 0x03u);   /* len_byte = 3: partial last chunk */
}

TEST(ProtoEncode, ChunkHeaderResponse_BufferTooSmall)
{
    static const uint8_t hash[32] = {};
    uint8_t buf[33]; /* needs 34 */
    EXPECT_EQ(proto_chunk_header_response(buf, sizeof(buf), hash, 0u), 0u);
}

/* ── Inbound parse ───────────────────────────────────────────────────────── */

TEST(ProtoParse, HandshakeAck_NoAuth)
{
    /* protocol_vectors.json HANDSHAKE_ACK: 01 04 00 */
    static const uint8_t msg[] = {0x01, 0x04, 0x00};
    proto_inbound_t in;
    ASSERT_EQ(proto_parse(msg, sizeof(msg), &in), 1);
    EXPECT_EQ(in.type,                    PROTO_HANDSHAKE_ACK);
    EXPECT_EQ(in.handshake_ack.proto_max, 0x04u);
    EXPECT_EQ(in.handshake_ack.flags,     0x00u);
    EXPECT_EQ(in.handshake_ack.credential, (const uint8_t*)0);
}

TEST(ProtoParse, HandshakeAck_OldProto_TwoBytes)
{
    /* Old clients (proto ≤ 0x03) send 2-byte ACK; library accepts it */
    static const uint8_t msg[] = {0x01, 0x03};
    proto_inbound_t in;
    ASSERT_EQ(proto_parse(msg, sizeof(msg), &in), 1);
    EXPECT_EQ(in.type,                    PROTO_HANDSHAKE_ACK);
    EXPECT_EQ(in.handshake_ack.proto_max, 0x03u);
    EXPECT_EQ(in.handshake_ack.flags,     0x00u); /* defaults to 0 when absent */
}

TEST(ProtoParse, HandshakeAck_Auth)
{
    /* protocol_vectors.json HANDSHAKE_ACK_AUTH: 01 04 01 [32 bytes] */
    static const uint8_t credential[32] = {
        0x21,0x22,0x23,0x24,0x25,0x26,0x27,0x28,
        0x29,0x2a,0x2b,0x2c,0x2d,0x2e,0x2f,0x30,
        0x31,0x32,0x33,0x34,0x35,0x36,0x37,0x38,
        0x39,0x3a,0x3b,0x3c,0x3d,0x3e,0x3f,0x40
    };
    uint8_t msg[35];
    msg[0] = 0x01u; msg[1] = 0x04u; msg[2] = 0x01u;
    memcpy(msg + 3, credential, 32);
    proto_inbound_t in;
    ASSERT_EQ(proto_parse(msg, sizeof(msg), &in), 1);
    EXPECT_EQ(in.type,                    PROTO_HANDSHAKE_ACK);
    EXPECT_EQ(in.handshake_ack.proto_max, 0x04u);
    EXPECT_EQ(in.handshake_ack.flags,     0x01u);
    ASSERT_NE(in.handshake_ack.credential, (const uint8_t*)0);
    EXPECT_EQ(memcmp(in.handshake_ack.credential, credential, 32), 0);
}

TEST(ProtoParse, HandshakeAck_Auth_TruncatedCredential)
{
    /* flags=1 but only 34 bytes (credential must be 32 → needs 35 total) */
    uint8_t msg[34];
    memset(msg, 0x00u, sizeof(msg));
    msg[0] = 0x01u; msg[1] = 0x04u; msg[2] = 0x01u;
    proto_inbound_t in;
    EXPECT_EQ(proto_parse(msg, sizeof(msg), &in), 0);
}

TEST(ProtoParse, HandshakeAck_Truncated)
{
    /* < 2 bytes */
    static const uint8_t msg[] = {0x01};
    proto_inbound_t in;
    EXPECT_EQ(proto_parse(msg, sizeof(msg), &in), 0);
}

TEST(ProtoParse, ClientReady)
{
    static const uint8_t msg[] = {0x02};
    proto_inbound_t in;
    ASSERT_EQ(proto_parse(msg, sizeof(msg), &in), 1);
    EXPECT_EQ(in.type, PROTO_CLIENT_READY);
}

TEST(ProtoParse, ChunkHeaderRequest_Index0)
{
    /* [0x10, 0x00, 0x00] */
    static const uint8_t msg[] = {0x10, 0x00, 0x00};
    proto_inbound_t in;
    ASSERT_EQ(proto_parse(msg, sizeof(msg), &in), 1);
    EXPECT_EQ(in.type,      PROTO_CHUNK_HEADER_REQUEST);
    EXPECT_EQ(in.chunk_idx, 0u);
}

TEST(ProtoParse, ChunkHeaderRequest_Index5)
{
    /* [0x10, 0x05, 0x00] */
    static const uint8_t msg[] = {0x10, 0x05, 0x00};
    proto_inbound_t in;
    ASSERT_EQ(proto_parse(msg, sizeof(msg), &in), 1);
    EXPECT_EQ(in.type,      PROTO_CHUNK_HEADER_REQUEST);
    EXPECT_EQ(in.chunk_idx, 5u);
}

TEST(ProtoParse, ChunkHeaderRequest_Index256)
{
    /* idx = 256: [0x10, 0x00, 0x01] */
    static const uint8_t msg[] = {0x10, 0x00, 0x01};
    proto_inbound_t in;
    ASSERT_EQ(proto_parse(msg, sizeof(msg), &in), 1);
    EXPECT_EQ(in.chunk_idx, 256u);
}

TEST(ProtoParse, ChunkHeaderRequest_Truncated)
{
    /* Only 2 bytes — needs 3 */
    static const uint8_t msg[] = {0x10, 0x05};
    proto_inbound_t in;
    EXPECT_EQ(proto_parse(msg, sizeof(msg), &in), 0);
}

TEST(ProtoParse, ChunkRequest_Index5)
{
    /* 20 05 00 */
    static const uint8_t msg[] = {0x20, 0x05, 0x00};
    proto_inbound_t in;
    ASSERT_EQ(proto_parse(msg, sizeof(msg), &in), 1);
    EXPECT_EQ(in.type,      PROTO_CHUNK_REQUEST);
    EXPECT_EQ(in.chunk_idx, 5u);
}

TEST(ProtoParse, ChunkRequest_Index0)
{
    static const uint8_t msg[] = {0x20, 0x00, 0x00};
    proto_inbound_t in;
    ASSERT_EQ(proto_parse(msg, sizeof(msg), &in), 1);
    EXPECT_EQ(in.chunk_idx, 0u);
}

TEST(ProtoParse, EventButtonPress)
{
    /* 31 10 01 */
    static const uint8_t msg[] = {0x31, 0x10, 0x01};
    proto_inbound_t in;
    ASSERT_EQ(proto_parse(msg, sizeof(msg), &in), 1);
    EXPECT_EQ(in.type,             PROTO_EVENT);
    EXPECT_EQ(in.event.widget_id,  0x10u);
    EXPECT_EQ(in.event.event_type, 0x01u);
    EXPECT_EQ(in.event.payload_len, 0u);
}

TEST(ProtoParse, EventSliderChange)
{
    /* 31 11 02 00 00 96 42  (75.0f LE) */
    static const uint8_t msg[] = {0x31, 0x11, 0x02, 0x00, 0x00, 0x96, 0x42};
    proto_inbound_t in;
    ASSERT_EQ(proto_parse(msg, sizeof(msg), &in), 1);
    EXPECT_EQ(in.event.widget_id,  0x11u);
    EXPECT_EQ(in.event.event_type, 0x02u);
    EXPECT_EQ(in.event.payload_len, 4u);
    /* verify float bytes match payload */
    EXPECT_EQ(memcmp(in.event.payload, msg + 3, 4), 0);
}

TEST(ProtoParse, EventToggleChange)
{
    /* 31 12 03 01 */
    static const uint8_t msg[] = {0x31, 0x12, 0x03, 0x01};
    proto_inbound_t in;
    ASSERT_EQ(proto_parse(msg, sizeof(msg), &in), 1);
    EXPECT_EQ(in.event.widget_id,  0x12u);
    EXPECT_EQ(in.event.event_type, 0x03u);
    EXPECT_EQ(in.event.payload[0], 0x01u);
}

TEST(ProtoParse, EventTextSubmit)
{
    /* 31 13 04 05 68 65 6c 6c 6f */
    static const uint8_t msg[] = {0x31, 0x13, 0x04, 0x05, 'h','e','l','l','o'};
    proto_inbound_t in;
    ASSERT_EQ(proto_parse(msg, sizeof(msg), &in), 1);
    EXPECT_EQ(in.event.widget_id,  0x13u);
    EXPECT_EQ(in.event.event_type, 0x04u);
    EXPECT_EQ(in.event.payload_len, 6u);   /* 1 len byte + 5 chars */
    EXPECT_EQ(in.event.payload[0], 0x05u); /* length prefix */
    EXPECT_EQ(memcmp(in.event.payload + 1, "hello", 5), 0);
}

TEST(ProtoParse, UnknownMsgType)
{
    static const uint8_t msg[] = {0xFF, 0x05, 0x00}; /* ERR_INVALID_CHUNK is outbound */
    proto_inbound_t in;
    EXPECT_EQ(proto_parse(msg, sizeof(msg), &in), 0);
    EXPECT_EQ(in.type, PROTO_UNKNOWN);
}

TEST(ProtoParse, EmptyBuffer)
{
    proto_inbound_t in;
    EXPECT_EQ(proto_parse(nullptr, 0, &in), 0);
}

TEST(ProtoParse, HandshakeAckTruncated)
{
    static const uint8_t msg[] = {0x01};   /* missing proto_max byte */
    proto_inbound_t in;
    EXPECT_EQ(proto_parse(msg, sizeof(msg), &in), 0);
}

TEST(ProtoParse, ChunkRequestTruncated)
{
    static const uint8_t msg[] = {0x20, 0x05};  /* missing second byte of idx */
    proto_inbound_t in;
    EXPECT_EQ(proto_parse(msg, sizeof(msg), &in), 0);
}

TEST(ProtoParse, HeartbeatInbound)
{
    static const uint8_t msg[] = {0x40};
    proto_inbound_t in;
    ASSERT_EQ(proto_parse(msg, sizeof(msg), &in), 1);
    EXPECT_EQ(in.type, PROTO_HEARTBEAT);
}
