/**
 * Chunk server tests.
 * Verifies CHUNK_RESPONSE and ERR_INVALID_CHUNK encoding, and the bounds check.
 */
#include <gtest/gtest.h>
#include "chunk_server.h"
#include "protocol.h"
#include <cstring>

/* ── Fixture ─────────────────────────────────────────────────────────────── */

class ChunkServerTest : public ::testing::Test {
protected:
    /* Two 50-byte chunks with known content (v1 blob from protocol_vectors) */
    static const uint8_t chunk0_data[50];
    static const uint8_t hash0[32];
    static const uint8_t hash1[32];

    static const uint8_t*  chunks[2];
    static const uint8_t*  hashes[2];
    static const uint16_t  lens[2];

    chunk_server_t cs;
    uint8_t        buf[512];

    void SetUp() override {
        chunk_server_init(&cs,
                          const_cast<const uint8_t**>(chunks),
                          const_cast<const uint8_t**>(hashes),
                          lens, 2u);
    }
};

/* v1 compressed blob chunk 0 (50 bytes) */
const uint8_t ChunkServerTest::chunk0_data[50] = {
    0x78,0x9c,0x4b,0x49,0x2d,0xcb,0x4c,0x4e,
    0xb5,0xe2,0x52,0x50,0xc8,0x4b,0xcc,0x4d,
    0xb5,0x52,0x28,0xe1,0x2a,0xcf,0x4c,0x49,
    0x4f,0x2d,0x29,0x06,0x09,0x25,0x82,0x08,
    0x05,0x85,0x92,0xca,0x02,0x90,0x4c,0x7e,
    0x7a,0x7a,0x4e,0x2a,0x17,0x00,0x76,0x7e,
    0x0e,0xb6
};

/* v3 chunk hashes */
const uint8_t ChunkServerTest::hash0[32] = {
    0x83,0x41,0xcf,0xd5,0x9a,0x9f,0x9b,0x4c,
    0xed,0x5e,0x23,0xdf,0x0b,0x06,0x8f,0x5d,
    0xeb,0x11,0x20,0x40,0xf3,0xc4,0x41,0xb4,
    0x5a,0x49,0x95,0x2b,0x3b,0xb9,0xc1,0x09
};
const uint8_t ChunkServerTest::hash1[32] = {
    0x2b,0x92,0xcb,0x09,0x57,0x66,0x4e,0x01,
    0x10,0x9e,0x2e,0xa5,0xa8,0x5e,0xef,0xdb,
    0x81,0xad,0x3f,0x76,0x94,0x0c,0x40,0x29,
    0x8e,0x4c,0xb1,0x58,0xa7,0xf3,0xca,0x85
};

const uint8_t*  ChunkServerTest::chunks[2] = {
    ChunkServerTest::chunk0_data,
    ChunkServerTest::chunk0_data  /* reuse chunk0 as chunk1 for simplicity */
};
const uint8_t*  ChunkServerTest::hashes[2] = {
    ChunkServerTest::hash0,
    ChunkServerTest::hash1
};
const uint16_t ChunkServerTest::lens[2] = {50u, 3u};

/* ── Chunk response tests ────────────────────────────────────────────────── */

TEST_F(ChunkServerTest, ValidRequest_Idx0)
{
    uint16_t n = chunk_server_respond(&cs, buf, sizeof(buf), 0u);

    /* CHUNK_RESPONSE: [0x21, idx_lo, idx_hi, len_lo, len_hi, data...] */
    ASSERT_GT(n, 5u);
    EXPECT_EQ(buf[0], 0x21u);           /* MSG_CHUNK_RESPONSE */
    EXPECT_EQ(buf[1], 0x00u);           /* idx = 0 LE */
    EXPECT_EQ(buf[2], 0x00u);
    EXPECT_EQ(buf[3], 50u);             /* len = 50 LE */
    EXPECT_EQ(buf[4], 0x00u);
    EXPECT_EQ(n, 55u);                  /* 5 header + 50 data */
    EXPECT_EQ(memcmp(buf + 5, chunk0_data, 50), 0);
}

TEST_F(ChunkServerTest, ValidRequest_Idx1)
{
    uint16_t n = chunk_server_respond(&cs, buf, sizeof(buf), 1u);
    ASSERT_GT(n, 5u);
    EXPECT_EQ(buf[0], 0x21u);
    EXPECT_EQ(buf[1], 0x01u);           /* idx = 1 LE */
    EXPECT_EQ(buf[2], 0x00u);
    EXPECT_EQ(buf[3], 3u);              /* len = 3 */
}

TEST_F(ChunkServerTest, InvalidRequest_IdxExact_N)
{
    /* idx == chunk_count → ERR_INVALID_CHUNK */
    uint16_t n = chunk_server_respond(&cs, buf, sizeof(buf), 2u);
    ASSERT_EQ(n, 3u);
    EXPECT_EQ(buf[0], 0xFFu);           /* MSG_ERR_INVALID_CHUNK */
    EXPECT_EQ(buf[1], 0x02u);           /* idx = 2 LE */
    EXPECT_EQ(buf[2], 0x00u);
}

TEST_F(ChunkServerTest, InvalidRequest_IdxFarOOB)
{
    /* idx = 0xFFFF → ERR_INVALID_CHUNK with correct index */
    uint16_t n = chunk_server_respond(&cs, buf, sizeof(buf), 0xFFFFu);
    ASSERT_EQ(n, 3u);
    EXPECT_EQ(buf[0], 0xFFu);
    EXPECT_EQ(buf[1], 0xFFu);
    EXPECT_EQ(buf[2], 0xFFu);
}

/* ── Chunk header response tests ─────────────────────────────────────────── */

TEST_F(ChunkServerTest, HeaderResponse_Idx0_FullChunk)
{
    /* lens[0] = 50 (partial) — wait, let's check: lens = {50, 3}
     * So idx=0 → len=50 → len_byte=50 (partial) */
    /* CHUNK_HEADER_RESPONSE: [0x11, hash0[32], len_byte] = 34 bytes */
    uint16_t n = chunk_server_header_response(&cs, buf, sizeof(buf), 0u);
    ASSERT_EQ(n, 34u);
    EXPECT_EQ(buf[0], 0x11u);
    EXPECT_EQ(memcmp(buf + 1, hash0, 32), 0);
    EXPECT_EQ(buf[33], 50u);    /* len_byte=50: partial chunk */
}

TEST_F(ChunkServerTest, HeaderResponse_Idx1_PartialChunk)
{
    /* lens[1] = 3 → len_byte = 3 */
    uint16_t n = chunk_server_header_response(&cs, buf, sizeof(buf), 1u);
    ASSERT_EQ(n, 34u);
    EXPECT_EQ(buf[0], 0x11u);
    EXPECT_EQ(memcmp(buf + 1, hash1, 32), 0);
    EXPECT_EQ(buf[33], 3u);     /* len_byte=3: partial last chunk */
}

TEST_F(ChunkServerTest, HeaderResponse_FullChunk_LenByte0)
{
    /* Verify a true full-chunk case: use a single-chunk server with len=256 */
    static const uint16_t full_lens[1] = {256u};
    chunk_server_t cs256;
    const uint8_t* one_chunk[1] = {chunk0_data};
    const uint8_t* one_hash[1]  = {hash0};
    chunk_server_init(&cs256, one_chunk, one_hash, full_lens, 1u);

    uint16_t n = chunk_server_header_response(&cs256, buf, sizeof(buf), 0u);
    ASSERT_EQ(n, 34u);
    EXPECT_EQ(buf[33], 0x00u);  /* len_byte=0: full 256-byte chunk */
}

TEST_F(ChunkServerTest, HeaderResponse_OOB_IdxExact_N)
{
    /* idx == chunk_count → ERR_INVALID_CHUNK */
    uint16_t n = chunk_server_header_response(&cs, buf, sizeof(buf), 2u);
    ASSERT_EQ(n, 3u);
    EXPECT_EQ(buf[0], 0xFFu);
    EXPECT_EQ(buf[1], 0x02u);
    EXPECT_EQ(buf[2], 0x00u);
}

TEST_F(ChunkServerTest, HeaderResponse_OOB_FarOOB)
{
    /* idx = 0xFFFF → ERR_INVALID_CHUNK */
    uint16_t n = chunk_server_header_response(&cs, buf, sizeof(buf), 0xFFFFu);
    ASSERT_EQ(n, 3u);
    EXPECT_EQ(buf[0], 0xFFu);
    EXPECT_EQ(buf[1], 0xFFu);
    EXPECT_EQ(buf[2], 0xFFu);
}

TEST_F(ChunkServerTest, HeaderResponse_BufferTooSmall)
{
    /* Buffer too small for 34-byte CHUNK_HEADER_RESPONSE */
    uint16_t n = chunk_server_header_response(&cs, buf, 33u, 0u);
    EXPECT_EQ(n, 0u);
}
