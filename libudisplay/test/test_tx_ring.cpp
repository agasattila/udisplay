/**
 * tx_ring tests — the outbound fragment FIFO used by the NimBLE backend.
 */
#include <gtest/gtest.h>
#include "tx_ring.h"
#include <cstring>
#include <vector>

namespace {

std::vector<uint8_t> bytes(uint16_t n, uint8_t seed)
{
    std::vector<uint8_t> v(n);
    for (uint16_t i = 0; i < n; i++) v[i] = static_cast<uint8_t>(seed + i);
    return v;
}

class TxRingTest : public ::testing::Test {
protected:
    uint8_t   storage_[64]{};
    tx_ring_t r_{};
    uint8_t   out_[64]{};

    void SetUp() override { tx_ring_init(&r_, storage_, sizeof(storage_)); }

    std::vector<uint8_t> pop()
    {
        uint16_t n = tx_ring_pop(&r_, out_, sizeof(out_));
        return std::vector<uint8_t>(out_, out_ + n);
    }
};

}  // namespace

TEST_F(TxRingTest, StartsEmpty)
{
    EXPECT_TRUE(tx_ring_empty(&r_));
    EXPECT_EQ(tx_ring_pop(&r_, out_, sizeof(out_)), 0u);
}

TEST_F(TxRingTest, FifoOrderAndLengthsPreserved)
{
    auto a = bytes(5, 1), b = bytes(1, 100), c = bytes(20, 50);
    ASSERT_TRUE(tx_ring_push(&r_, a.data(), 5));
    ASSERT_TRUE(tx_ring_push(&r_, b.data(), 1));
    ASSERT_TRUE(tx_ring_push(&r_, c.data(), 20));
    EXPECT_EQ(pop(), a);
    EXPECT_EQ(pop(), b);
    EXPECT_EQ(pop(), c);
    EXPECT_TRUE(tx_ring_empty(&r_));
}

TEST_F(TxRingTest, RejectsEmptyFragment)
{
    uint8_t x = 0;
    EXPECT_FALSE(tx_ring_push(&r_, &x, 0));
    EXPECT_TRUE(tx_ring_empty(&r_));
}

TEST_F(TxRingTest, FillsExactlyThenRefuses)
{
    auto a = bytes(62, 7);   /* 62 + 2-byte prefix = all 64 bytes */
    ASSERT_TRUE(tx_ring_push(&r_, a.data(), 62));
    uint8_t x = 1;
    EXPECT_FALSE(tx_ring_push(&r_, &x, 1));
    EXPECT_EQ(pop(), a);
    EXPECT_TRUE(tx_ring_push(&r_, &x, 1));
}

TEST_F(TxRingTest, OverflowLeavesQueueIntact)
{
    auto a = bytes(30, 1), big = bytes(40, 9);
    ASSERT_TRUE(tx_ring_push(&r_, a.data(), 30));
    EXPECT_FALSE(tx_ring_push(&r_, big.data(), 40));
    EXPECT_EQ(pop(), a);
    EXPECT_TRUE(tx_ring_empty(&r_));
}

TEST_F(TxRingTest, WrapsAroundBufferEnd)
{
    /* Advance head so later entries straddle the end, including a split
     * length prefix. */
    for (int round = 0; round < 50; round++) {
        auto a = bytes(static_cast<uint16_t>(7 + round % 13), static_cast<uint8_t>(round));
        auto b = bytes(static_cast<uint16_t>(3 + round % 5), static_cast<uint8_t>(round * 3));
        ASSERT_TRUE(tx_ring_push(&r_, a.data(), static_cast<uint16_t>(a.size())));
        ASSERT_TRUE(tx_ring_push(&r_, b.data(), static_cast<uint16_t>(b.size())));
        ASSERT_EQ(pop(), a) << "round " << round;
        ASSERT_EQ(pop(), b) << "round " << round;
    }
    EXPECT_TRUE(tx_ring_empty(&r_));
}

TEST_F(TxRingTest, ResetDropsEverything)
{
    auto a = bytes(10, 1);
    ASSERT_TRUE(tx_ring_push(&r_, a.data(), 10));
    tx_ring_reset(&r_);
    EXPECT_TRUE(tx_ring_empty(&r_));
    EXPECT_EQ(tx_ring_pop(&r_, out_, sizeof(out_)), 0u);
}

TEST_F(TxRingTest, OversizedFragmentForCallerBufferIsDiscarded)
{
    auto a = bytes(20, 1), b = bytes(4, 2);
    ASSERT_TRUE(tx_ring_push(&r_, a.data(), 20));
    ASSERT_TRUE(tx_ring_push(&r_, b.data(), 4));
    uint8_t small[8];
    EXPECT_EQ(tx_ring_pop(&r_, small, sizeof(small)), 0u);
    EXPECT_EQ(pop(), b);   /* the next fragment is still intact */
}
