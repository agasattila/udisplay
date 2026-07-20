/**
 * BLE fragmentation/reassembly and TCP framing tests.
 *
 * BLE fragment byte sequences are taken from protocol_vectors.json
 * (BLE_fragment_first / BLE_fragment_last at mtu_payload=23, source=CHUNK_HEADER_RESPONSE_full).
 * All BLE tests validate the v2.2 offset+packet_id framing scheme.
 */
#include <gtest/gtest.h>
#include "framing.h"
#include "../include/udisplay.h"
#include <cstring>
#include <vector>

/* ── Shared test data ────────────────────────────────────────────────────── */

/* v3 CHUNK_HEADER_RESPONSE for 2-chunk blob (67 bytes): opcode 0x11 + both chunk hashes */
static const uint8_t kHashResponse[67] = {
    0x11, 0x02, 0x00,
    0x83,0x41,0xcf,0xd5,0x9a,0x9f,0x9b,0x4c,
    0xed,0x5e,0x23,0xdf,0x0b,0x06,0x8f,0x5d,
    0xeb,0x11,0x20,0x40,0xf3,0xc4,0x41,0xb4,
    0x5a,0x49,0x95,0x2b,0x3b,0xb9,0xc1,0x09,
    0x2b,0x92,0xcb,0x09,0x57,0x66,0x4e,0x01,
    0x10,0x9e,0x2e,0xa5,0xa8,0x5e,0xef,0xdb,
    0x81,0xad,0x3f,0x76,0x94,0x0c,0x40,0x29,
    0x8e,0x4c,0xb1,0x58,0xa7,0xf3,0xca,0x85
};

/* CHUNK_HEADER_RESPONSE_full from protocol_vectors.json (34 bytes): opcode 0x11 + chunk-0 hash */
static const uint8_t kChunkHeaderResponse[34] = {
    0x11,
    0x83,0x41,0xcf,0xd5,0x9a,0x9f,0x9b,0x4c,
    0xed,0x5e,0x23,0xdf,0x0b,0x06,0x8f,0x5d,
    0xeb,0x11,0x20,0x40,0xf3,0xc4,0x41,0xb4,
    0x5a,0x49,0x95,0x2b,0x3b,0xb9,0xc1,0x09,
    0x00
};

/* ── BLE fragmentation (sender) ──────────────────────────────────────────── */

struct Frag {
    std::vector<uint8_t> bytes;
};

static void collect_emit(const uint8_t* frag, uint16_t len, void* ud)
{
    auto* frags = static_cast<std::vector<Frag>*>(ud);
    frags->push_back({std::vector<uint8_t>(frag, frag + len)});
}

static uint8_t sBuf[517];

TEST(BleFragment, SingleFragment_MessageFitsInFirstMtu)
{
    /* 5-byte message at mtu_payload=20: 6-byte header + 5 payload = 11 bytes, 1 fragment */
    static const uint8_t msg[] = {0x01, 0x02, 0x03, 0x04, 0x05};
    std::vector<Frag> frags;
    udisplay_ble_fragment(msg, 5, 20u, 0, sBuf, sizeof(sBuf), collect_emit, &frags);

    ASSERT_EQ(frags.size(), 1u);
    ASSERT_EQ(frags[0].bytes.size(), 11u);  /* 6 header + 5 payload */
    EXPECT_EQ(memcmp(frags[0].bytes.data() + 6, msg, 5), 0);
}

TEST(BleFragment, MultiFragment_FourChunks)
{
    /*
     * kHashResponse (67 bytes) at mtu_payload=23:
     *   first cap = 23-6 = 17 bytes  → fragment 0: 17 bytes payload
     *   cont  cap = 23-3 = 20 bytes  → fragments 1,2: 20 bytes each
     *                                  fragment 3: 10 bytes (67-17-20-20=10)
     * Total: 4 fragments.
     */
    std::vector<Frag> frags;
    udisplay_ble_fragment(kHashResponse, 67, 23u, 0, sBuf, sizeof(sBuf), collect_emit, &frags);

    ASSERT_EQ(frags.size(), 4u);
    EXPECT_EQ(frags[0].bytes.size(), 23u);  /* 6+17 */
    EXPECT_EQ(frags[1].bytes.size(), 23u);  /* 3+20 */
    EXPECT_EQ(frags[2].bytes.size(), 23u);  /* 3+20 */
    EXPECT_EQ(frags[3].bytes.size(), 13u);  /* 3+10 */

    /* Reassemble by stripping headers */
    std::vector<uint8_t> reassembled;
    reassembled.insert(reassembled.end(),
                       frags[0].bytes.begin() + 6, frags[0].bytes.end());
    for (size_t i = 1; i < frags.size(); ++i) {
        reassembled.insert(reassembled.end(),
                           frags[i].bytes.begin() + 3, frags[i].bytes.end());
    }
    ASSERT_EQ(reassembled.size(), 67u);
    EXPECT_EQ(memcmp(reassembled.data(), kHashResponse, 67), 0);
}

TEST(BleFragment, MatchesVectorFirstFragmentBytes)
{
    /*
     * Verify exact first-fragment bytes for CHUNK_HEADER_RESPONSE_full (34 bytes)
     * at mtu_payload=23, packet_id=0 — matches BLE_fragment_first in protocol_vectors.json:
     *   00 00 00 22 00 00 11 83 41 cf d5 9a 9f 9b 4c ed 5e 23 df 0b 06 8f 5d
     */
    static const uint8_t kFirst[23] = {
        0x00, 0x00,              /* offset = 0 LE */
        0x00,                    /* packet_id = 0 */
        0x22, 0x00,              /* length = 34 LE */
        0x00,                    /* flags = 0 */
        0x11,                    /* kChunkHeaderResponse[0] */
        0x83,0x41,0xcf,0xd5,0x9a,0x9f,0x9b,0x4c,  /* bytes 1..8 */
        0xed,0x5e,0x23,0xdf,0x0b,0x06,0x8f,0x5d    /* bytes 9..16 */
    };

    std::vector<Frag> frags;
    udisplay_ble_fragment(kChunkHeaderResponse, 34, 23u, 0,
                          sBuf, sizeof(sBuf), collect_emit, &frags);

    ASSERT_GE(frags.size(), 1u);
    ASSERT_EQ(frags[0].bytes.size(), 23u);
    EXPECT_EQ(memcmp(frags[0].bytes.data(), kFirst, 23), 0);
}

TEST(BleFragment, ContinuationHeaderLayout)
{
    /*
     * Use mtu_payload=10 so the 5-byte message splits:
     *   first cap=4 → fragment 0: 4 bytes at offset=0
     *   cont  cap=7 → fragment 1: 1 byte at offset=4
     * Verify continuation header: [u16 offset=4 LE][u8 packet_id=7].
     */
    static const uint8_t msg[] = {0xAA, 0xBB, 0xCC, 0xDD, 0xEE};
    std::vector<Frag> frags;
    udisplay_ble_fragment(msg, 5, 10u, 7, sBuf, sizeof(sBuf), collect_emit, &frags);

    ASSERT_EQ(frags.size(), 2u);
    /* Continuation header: offset=4 LE, packet_id=7 */
    EXPECT_EQ(frags[1].bytes[0], 0x04u);  /* offset lo */
    EXPECT_EQ(frags[1].bytes[1], 0x00u);  /* offset hi */
    EXPECT_EQ(frags[1].bytes[2], 0x07u);  /* packet_id */
    EXPECT_EQ(frags[1].bytes[3], 0xEEu);  /* payload */
}

TEST(BleFragment, PacketIdAppearsInAllFragments)
{
    /* All fragments (first + continuations) must carry the same packet_id */
    std::vector<Frag> frags;
    udisplay_ble_fragment(kHashResponse, 67, 23u, 42, sBuf, sizeof(sBuf), collect_emit, &frags);

    ASSERT_EQ(frags.size(), 4u);
    EXPECT_EQ(frags[0].bytes[2], 42u);   /* first fragment: byte[2] = packet_id */
    EXPECT_EQ(frags[1].bytes[2], 42u);   /* continuation: byte[2] = packet_id */
    EXPECT_EQ(frags[2].bytes[2], 42u);
    EXPECT_EQ(frags[3].bytes[2], 42u);
}

/* ── BLE reassembly (receiver) ───────────────────────────────────────────── */

/* Helper: build a first fragment for msg with given packet_id */
static std::vector<uint8_t> make_first(const uint8_t* msg, uint16_t msg_len,
                                        uint8_t pkt_id, uint16_t mtu)
{
    uint16_t first_cap = mtu - 6u;
    uint16_t chunk = (msg_len < first_cap) ? msg_len : first_cap;
    std::vector<uint8_t> f;
    f.push_back(0x00); f.push_back(0x00);               /* offset=0 */
    f.push_back(pkt_id);
    f.push_back(msg_len & 0xFF); f.push_back(msg_len >> 8); /* length LE */
    f.push_back(0x00);                                       /* flags=0 */
    f.insert(f.end(), msg, msg + chunk);
    return f;
}

/* Helper: build a continuation fragment */
static std::vector<uint8_t> make_cont(const uint8_t* msg, uint16_t offset,
                                       uint16_t payload_len, uint8_t pkt_id)
{
    std::vector<uint8_t> f;
    f.push_back(offset & 0xFF); f.push_back(offset >> 8);
    f.push_back(pkt_id);
    f.insert(f.end(), msg + offset, msg + offset + payload_len);
    return f;
}

TEST(BleRx, SingleFragment_Completes)
{
    /* Single ATT packet whose payload exactly fills the declared length */
    static const uint8_t msg[] = {0x11, 0x22, 0x33};
    auto att = make_first(msg, 3, 0, 20u);  /* mtu=20: first_cap=14, all 3 bytes fit */

    ble_rx_t rx; ble_rx_reset(&rx);
    EXPECT_EQ(ble_rx_feed(&rx, att.data(), (uint16_t)att.size()), BLE_RX_DONE);
    EXPECT_EQ(rx.len, 3u);
    EXPECT_EQ(memcmp(rx.buf, msg, 3), 0);
}

TEST(BleRx, TwoFragments_VectorData)
{
    /*
     * BLE_fragment_first and BLE_fragment_last from protocol_vectors.json.
     * Reassembling them must yield kChunkHeaderResponse exactly.
     */
    static const uint8_t first[23] = {
        0x00, 0x00,   /* offset=0 */
        0x00,         /* packet_id=0 */
        0x22, 0x00,   /* length=34 */
        0x00,         /* flags=0 */
        0x11,0x83,0x41,0xcf,0xd5,0x9a,0x9f,0x9b,
        0x4c,0xed,0x5e,0x23,0xdf,0x0b,0x06,0x8f,0x5d
    };
    static const uint8_t last[20] = {
        0x11, 0x00,   /* offset=17 */
        0x00,         /* packet_id=0 */
        0xeb,0x11,0x20,0x40,0xf3,0xc4,0x41,0xb4,
        0x5a,0x49,0x95,0x2b,0x3b,0xb9,0xc1,0x09,0x00
    };

    ble_rx_t rx; ble_rx_reset(&rx);
    EXPECT_EQ(ble_rx_feed(&rx, first, sizeof(first)), BLE_RX_MORE);
    EXPECT_EQ(ble_rx_feed(&rx, last,  sizeof(last)),  BLE_RX_DONE);

    ASSERT_EQ(rx.len, 34u);
    EXPECT_EQ(memcmp(rx.buf, kChunkHeaderResponse, 34), 0);
}

TEST(BleRx, Error_FlagsNotZero)
{
    /* Error rule 1: first fragment with flags != 0x00 */
    static const uint8_t att[] = {
        0x00, 0x00,   /* offset=0 */
        0x00,         /* packet_id=0 */
        0x05, 0x00,   /* length=5 */
        0x01,         /* flags=0x01 — INVALID */
        0xAA, 0xBB, 0xCC, 0xDD, 0xEE
    };
    ble_rx_t rx; ble_rx_reset(&rx);
    EXPECT_EQ(ble_rx_feed(&rx, att, sizeof(att)), BLE_RX_ERROR);
    EXPECT_EQ(rx.in_progress, 0);
}

TEST(BleRx, Error_LengthExceedsMax)
{
    /* Error rule 2: declared length > UDISPLAY_MAX_MSG_SIZE */
    const uint16_t bad_len = UDISPLAY_MAX_MSG_SIZE + 1u;
    uint8_t att[7] = {
        0x00, 0x00,
        0x00,
        (uint8_t)(bad_len & 0xFF), (uint8_t)(bad_len >> 8),
        0x00,
        0xAA
    };
    ble_rx_t rx; ble_rx_reset(&rx);
    EXPECT_EQ(ble_rx_feed(&rx, att, sizeof(att)), BLE_RX_ERROR);
    EXPECT_EQ(rx.in_progress, 0);
}

TEST(BleRx, Error_OffsetGap)
{
    /* Error rule 3: offset > expected_offset (gap in sequence) */
    static const uint8_t msg[] = {0x01, 0x02, 0x03, 0x04, 0x05};
    auto first = make_first(msg, 5, 0, 8u);  /* first_cap=2; offset after first=2 */

    ble_rx_t rx; ble_rx_reset(&rx);
    EXPECT_EQ(ble_rx_feed(&rx, first.data(), (uint16_t)first.size()), BLE_RX_MORE);

    /* Send continuation at offset=4 instead of expected offset=2 */
    auto cont = make_cont(msg, 4, 1, 0);
    EXPECT_EQ(ble_rx_feed(&rx, cont.data(), (uint16_t)cont.size()), BLE_RX_ERROR);
    EXPECT_EQ(rx.in_progress, 0);
}

TEST(BleRx, Error_WrongOffset)
{
    /* Error rule 4: offset < expected_offset (overlap / wrong position) */
    static const uint8_t msg[] = {0x01, 0x02, 0x03, 0x04, 0x05};
    auto first = make_first(msg, 5, 0, 8u);  /* first_cap=2; expected next=2 */

    ble_rx_t rx; ble_rx_reset(&rx);
    EXPECT_EQ(ble_rx_feed(&rx, first.data(), (uint16_t)first.size()), BLE_RX_MORE);

    /* Send continuation at offset=1 (below expected 2) */
    auto cont = make_cont(msg, 1, 2, 0);
    EXPECT_EQ(ble_rx_feed(&rx, cont.data(), (uint16_t)cont.size()), BLE_RX_ERROR);
    EXPECT_EQ(rx.in_progress, 0);
}

TEST(BleRx, Error_UnexpectedPacketId)
{
    /* Error rule 5: continuation fragment has different packet_id */
    static const uint8_t msg[] = {0x01, 0x02, 0x03, 0x04, 0x05};
    auto first = make_first(msg, 5, 3, 8u);  /* packet_id=3 */

    ble_rx_t rx; ble_rx_reset(&rx);
    EXPECT_EQ(ble_rx_feed(&rx, first.data(), (uint16_t)first.size()), BLE_RX_MORE);

    /* Continuation with wrong packet_id=4 */
    auto cont = make_cont(msg, 2, 3, 4);
    EXPECT_EQ(ble_rx_feed(&rx, cont.data(), (uint16_t)cont.size()), BLE_RX_ERROR);
    EXPECT_EQ(rx.in_progress, 0);
}

TEST(BleRx, Error_OverCompletion)
{
    /* Error rule 6: accumulated + fragment payload would exceed declared length */
    static const uint8_t msg[] = {0x01, 0x02, 0x03};
    auto first = make_first(msg, 3, 0, 8u);  /* first_cap=2; 2 bytes accum, 1 remain */

    ble_rx_t rx; ble_rx_reset(&rx);
    EXPECT_EQ(ble_rx_feed(&rx, first.data(), (uint16_t)first.size()), BLE_RX_MORE);

    /* Continuation at offset=2 with 2 payload bytes — exceeds declared length of 3 */
    std::vector<uint8_t> cont = {0x02, 0x00, 0x00, 0xCC, 0xDD};
    EXPECT_EQ(ble_rx_feed(&rx, cont.data(), (uint16_t)cont.size()), BLE_RX_ERROR);
    EXPECT_EQ(rx.in_progress, 0);
}

TEST(BleRx, PacketIdWrapAround_ZeroAfter255)
{
    /* A first fragment with packet_id=0 after previously using packet_id=255.
     * offset=0 always starts a fresh reassembly — packet_id 0 must be accepted. */
    static const uint8_t msg[] = {0x42};
    auto first255 = make_first(msg, 1, 255, 20u);

    ble_rx_t rx; ble_rx_reset(&rx);
    EXPECT_EQ(ble_rx_feed(&rx, first255.data(), (uint16_t)first255.size()), BLE_RX_DONE);

    /* New message with packet_id=0 (wrapped) — must be accepted as fresh */
    auto first0 = make_first(msg, 1, 0, 20u);
    EXPECT_EQ(ble_rx_feed(&rx, first0.data(), (uint16_t)first0.size()), BLE_RX_DONE);
    EXPECT_EQ(rx.buf[0], 0x42u);
}

TEST(BleRx, Reset_ClearsAllState)
{
    static const uint8_t msg[] = {0xAA, 0xBB, 0xCC, 0xDD, 0xEE};
    auto first = make_first(msg, 5, 7, 8u);  /* partial: 2 bytes, in_progress=1 */

    ble_rx_t rx; ble_rx_reset(&rx);
    EXPECT_EQ(ble_rx_feed(&rx, first.data(), (uint16_t)first.size()), BLE_RX_MORE);
    EXPECT_NE(rx.in_progress, 0);
    EXPECT_NE(rx.len, 0u);

    ble_rx_reset(&rx);
    EXPECT_EQ(rx.len, 0u);
    EXPECT_EQ(rx.msg_len, 0u);
    EXPECT_EQ(rx.expected_offset, 0u);
    EXPECT_EQ(rx.packet_id, 0u);
    EXPECT_EQ(rx.in_progress, 0);
    EXPECT_EQ(rx.overflow, 0);
}

TEST(BleRx, NewFirstFragment_AbortsInProgress)
{
    /* A new first fragment (offset=0) mid-reassembly discards prior state */
    static const uint8_t msg1[] = {0x01, 0x02, 0x03, 0x04};
    static const uint8_t msg2[] = {0xAA};
    auto first1 = make_first(msg1, 4, 0, 8u);  /* 2 bytes in, 2 remaining */
    auto first2 = make_first(msg2, 1, 1, 20u); /* completely different message */

    ble_rx_t rx; ble_rx_reset(&rx);
    EXPECT_EQ(ble_rx_feed(&rx, first1.data(), (uint16_t)first1.size()), BLE_RX_MORE);
    EXPECT_NE(rx.in_progress, 0);

    /* New first fragment abandons the old reassembly */
    EXPECT_EQ(ble_rx_feed(&rx, first2.data(), (uint16_t)first2.size()), BLE_RX_DONE);
    EXPECT_EQ(rx.len, 1u);
    EXPECT_EQ(rx.buf[0], 0xAAu);
}

/* ── TCP framing ─────────────────────────────────────────────────────────── */

TEST(TcpFrame, StateFloat32_ExactBytes)
{
    /* TCP_framed_STATE_float32: 07 00 30 10 01 c3 f5 48 40 */
    static const uint8_t payload[] = {0x30,0x10,0x01,0xc3,0xf5,0x48,0x40};
    static const uint8_t expected[] = {0x07,0x00, 0x30,0x10,0x01,0xc3,0xf5,0x48,0x40};

    uint8_t out[16];
    uint16_t n = udisplay_tcp_frame(out, sizeof(out), payload, sizeof(payload));
    ASSERT_EQ(n, 9u);
    EXPECT_EQ(memcmp(out, expected, 9), 0);
}

TEST(TcpFrame, LengthPrefixIsLittleEndian)
{
    /* Frame a 256-byte message and verify the length prefix */
    std::vector<uint8_t> msg(256, 0xAA);
    std::vector<uint8_t> out(260);
    uint16_t n = udisplay_tcp_frame(out.data(), (uint16_t)out.size(),
                                     msg.data(), 256u);
    ASSERT_EQ(n, 258u);
    EXPECT_EQ(out[0], 0x00u);   /* 256 = 0x0100 LE */
    EXPECT_EQ(out[1], 0x01u);
}

TEST(TcpFrame, BufferTooSmall)
{
    static const uint8_t payload[] = {0x40};
    uint8_t out[2];   /* need 3, only have 2 */
    EXPECT_EQ(udisplay_tcp_frame(out, sizeof(out), payload, sizeof(payload)), 0u);
}

TEST(TcpUnframe, StateFloat32)
{
    static const uint8_t framed[] = {0x07,0x00, 0x30,0x10,0x01,0xc3,0xf5,0x48,0x40};
    const uint8_t* msg;
    uint16_t       msg_len;
    ASSERT_EQ(udisplay_tcp_unframe(framed, sizeof(framed), &msg, &msg_len), 1);
    EXPECT_EQ(msg_len, 7u);
    EXPECT_EQ(memcmp(msg, framed + 2, 7), 0);
}

TEST(TcpUnframe, TruncatedBuffer)
{
    static const uint8_t framed[] = {0x07,0x00, 0x30,0x10};  /* declares 7 bytes, only 2 present */
    const uint8_t* msg;
    uint16_t       msg_len;
    EXPECT_EQ(udisplay_tcp_unframe(framed, sizeof(framed), &msg, &msg_len), 0);
}

TEST(TcpUnframe, OnlyLengthPrefix_TooShort)
{
    static const uint8_t buf[] = {0x01};   /* only 1 byte, need at least 2 */
    const uint8_t* msg;
    uint16_t       msg_len;
    EXPECT_EQ(udisplay_tcp_unframe(buf, sizeof(buf), &msg, &msg_len), 0);
}

TEST(TcpUnframe, ZeroLengthPayload)
{
    static const uint8_t framed[] = {0x00, 0x00};   /* length = 0, valid */
    const uint8_t* msg;
    uint16_t       msg_len;
    ASSERT_EQ(udisplay_tcp_unframe(framed, sizeof(framed), &msg, &msg_len), 1);
    EXPECT_EQ(msg_len, 0u);
}

/* ── Protocol max frame size enforcement ─────────────────────────────────── */

TEST(TcpFrame, MaxSizePayloadAccepted)
{
    std::vector<uint8_t> msg(UDISPLAY_MAX_MSG_SIZE, 0xAA);
    std::vector<uint8_t> out(UDISPLAY_MAX_MSG_SIZE + 2u);
    uint16_t n = udisplay_tcp_frame(out.data(), (uint16_t)out.size(),
                                     msg.data(), UDISPLAY_MAX_MSG_SIZE);
    EXPECT_EQ(n, (uint16_t)(UDISPLAY_MAX_MSG_SIZE + 2u));
}

TEST(TcpFrame, OversizePayloadRejected)
{
    std::vector<uint8_t> msg(UDISPLAY_MAX_MSG_SIZE + 1u, 0xAA);
    std::vector<uint8_t> out(UDISPLAY_MAX_MSG_SIZE + 3u);
    uint16_t n = udisplay_tcp_frame(out.data(), (uint16_t)out.size(),
                                     msg.data(), UDISPLAY_MAX_MSG_SIZE + 1u);
    EXPECT_EQ(n, 0u);
}

TEST(TcpUnframe, MaxSizePayloadAccepted)
{
    /* Build a framed buffer: [lo, hi] + 1024 bytes of payload */
    const uint16_t plen = UDISPLAY_MAX_MSG_SIZE;
    std::vector<uint8_t> buf(2u + plen, 0xAA);
    buf[0] = (uint8_t)(plen & 0xFFu);
    buf[1] = (uint8_t)(plen >> 8u);
    const uint8_t* msg;
    uint16_t       msg_len;
    EXPECT_EQ(udisplay_tcp_unframe(buf.data(), (uint16_t)buf.size(), &msg, &msg_len), 1);
    EXPECT_EQ(msg_len, plen);
}

TEST(TcpUnframe, OversizePayloadRejected)
{
    /* Declare payload_len = UDISPLAY_MAX_MSG_SIZE + 1 in the header */
    const uint16_t plen = UDISPLAY_MAX_MSG_SIZE + 1u;
    std::vector<uint8_t> buf(2u + plen, 0xAA);
    buf[0] = (uint8_t)(plen & 0xFFu);
    buf[1] = (uint8_t)(plen >> 8u);
    const uint8_t* msg;
    uint16_t       msg_len;
    EXPECT_EQ(udisplay_tcp_unframe(buf.data(), (uint16_t)buf.size(), &msg, &msg_len), 0);
}

/* ── TCP inbound reassembly (tcp_rx_t) ───────────────────────────────────── */

struct TcpMsg {
    std::vector<uint8_t> bytes;
};

static void collect_tcp_message(const uint8_t* msg, uint16_t len, void* ud)
{
    auto* msgs = static_cast<std::vector<TcpMsg>*>(ud);
    msgs->push_back({std::vector<uint8_t>(msg, msg + len)});
}

TEST(TcpRx, SingleCompleteFrame_DispatchesOnce)
{
    static const uint8_t payload[] = {0x11, 0x22, 0x33};
    uint8_t framed[16];
    uint16_t n = udisplay_tcp_frame(framed, sizeof(framed), payload, sizeof(payload));
    ASSERT_GT(n, 0u);

    tcp_rx_t rx; tcp_rx_reset(&rx);
    std::vector<TcpMsg> got;
    EXPECT_EQ(tcp_rx_feed(&rx, framed, n, collect_tcp_message, &got), 0);

    ASSERT_EQ(got.size(), 1u);
    EXPECT_EQ(got[0].bytes, std::vector<uint8_t>(payload, payload + sizeof(payload)));
    EXPECT_EQ(rx.used, 0u);   /* fully consumed */
}

TEST(TcpRx, PartialFrame_CarriesOverToNextFeed)
{
    static const uint8_t payload[] = {0xAA, 0xBB};
    uint8_t framed[16];
    uint16_t n = udisplay_tcp_frame(framed, sizeof(framed), payload, sizeof(payload));
    ASSERT_GT(n, 0u);

    tcp_rx_t rx; tcp_rx_reset(&rx);
    std::vector<TcpMsg> got;
    /* Split the framed message across two feed() calls */
    EXPECT_EQ(tcp_rx_feed(&rx, framed, (uint16_t)(n - 1), collect_tcp_message, &got), 0);
    EXPECT_TRUE(got.empty()) << "must not dispatch until the frame is complete";

    EXPECT_EQ(tcp_rx_feed(&rx, framed + (n - 1), 1u, collect_tcp_message, &got), 0);
    ASSERT_EQ(got.size(), 1u);
    EXPECT_EQ(got[0].bytes, std::vector<uint8_t>(payload, payload + sizeof(payload)));
}

TEST(TcpRx, MultipleFramesInOneFeed_DispatchesAllInOrder)
{
    static const uint8_t p1[] = {0x01};
    static const uint8_t p2[] = {0x02, 0x03};
    uint8_t framed[32];
    uint16_t n1 = udisplay_tcp_frame(framed, sizeof(framed), p1, sizeof(p1));
    uint16_t n2 = udisplay_tcp_frame(framed + n1, (uint16_t)(sizeof(framed) - n1), p2, sizeof(p2));
    ASSERT_GT(n1, 0u);
    ASSERT_GT(n2, 0u);

    tcp_rx_t rx; tcp_rx_reset(&rx);
    std::vector<TcpMsg> got;
    EXPECT_EQ(tcp_rx_feed(&rx, framed, (uint16_t)(n1 + n2), collect_tcp_message, &got), 0);

    ASSERT_EQ(got.size(), 2u);
    EXPECT_EQ(got[0].bytes, std::vector<uint8_t>(p1, p1 + sizeof(p1)));
    EXPECT_EQ(got[1].bytes, std::vector<uint8_t>(p2, p2 + sizeof(p2)));
}

TEST(TcpRx, Overflow_ReturnsError)
{
    tcp_rx_t rx; tcp_rx_reset(&rx);
    std::vector<uint8_t> huge(sizeof(rx.buf) + 1u, 0xAA);
    std::vector<TcpMsg> got;
    EXPECT_EQ(tcp_rx_feed(&rx, huge.data(), (uint16_t)huge.size(), collect_tcp_message, &got), -1);
}

TEST(TcpRx, Reset_ClearsBufferedBytes)
{
    static const uint8_t payload[] = {0x99};
    uint8_t framed[16];
    uint16_t n = udisplay_tcp_frame(framed, sizeof(framed), payload, sizeof(payload));
    ASSERT_GT(n, 0u);

    tcp_rx_t rx; tcp_rx_reset(&rx);
    std::vector<TcpMsg> got;
    /* Feed only a partial frame, leaving bytes buffered */
    tcp_rx_feed(&rx, framed, (uint16_t)(n - 1), collect_tcp_message, &got);
    EXPECT_GT(rx.used, 0u);

    tcp_rx_reset(&rx);
    EXPECT_EQ(rx.used, 0u);
}
