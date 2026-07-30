/**
 * Two-context adversarial tests for the multi-instance libudisplay refactor.
 *
 * Proves the core refactor is real, not cosmetic: two independent udisplay_t
 * instances must never share state. Covers the three scenarios named in the
 * design doc (prog-main-design-20260730-192038.md, Design Decision #2 /
 * Success Criteria): alternating partial frames, independent auth/reset,
 * separate write callbacks.
 */
#include <gtest/gtest.h>
#include "../include/udisplay.h"
#include <cstring>
#include <vector>

namespace {

const uint8_t kDummyChunk[256] = {};
const uint8_t kDummyHash[32]   = {};
const uint8_t kDummyRootA[32]  = { 0xAA };
const uint8_t kDummyRootB[32]  = { 0xBB };
const uint8_t* kChunks[1]      = { kDummyChunk };
const uint8_t* kHashes[1]      = { kDummyHash };
const uint16_t kLens[1]        = { 50u };

struct Sent {
    std::vector<uint8_t> data;
};

} // namespace

/* ── Two-context isolation ────────────────────────────────────────────────── */

class MultiInstanceTest : public ::testing::Test {
protected:
    udisplay_t ctx_a{};
    udisplay_t ctx_b{};
    std::vector<Sent> sent_a;
    std::vector<Sent> sent_b;

    static void on_send_a(const uint8_t* d, uint16_t n, void* ud) {
        auto* v = static_cast<std::vector<Sent>*>(ud);
        v->push_back({ std::vector<uint8_t>(d, d + n) });
    }
    static void on_send_b(const uint8_t* d, uint16_t n, void* ud) {
        auto* v = static_cast<std::vector<Sent>*>(ud);
        v->push_back({ std::vector<uint8_t>(d, d + n) });
    }

    void SetUp() override {
        udisplay_config_t cfg_a{};
        cfg_a.merkle_root  = kDummyRootA;
        cfg_a.chunks       = kChunks;
        cfg_a.chunk_hashes = kHashes;
        cfg_a.chunk_lens   = kLens;
        cfg_a.chunk_count  = 1u;
        cfg_a.send         = on_send_a;
        cfg_a.userdata     = &sent_a;
        cfg_a.transport    = UDISPLAY_TRANSPORT_TCP;
        udisplay_init(&ctx_a, &cfg_a);

        udisplay_config_t cfg_b{};
        cfg_b.merkle_root  = kDummyRootB;
        cfg_b.chunks       = kChunks;
        cfg_b.chunk_hashes = kHashes;
        cfg_b.chunk_lens   = kLens;
        cfg_b.chunk_count  = 1u;
        cfg_b.send         = on_send_b;
        cfg_b.userdata     = &sent_b;
        cfg_b.transport    = UDISPLAY_TRANSPORT_TCP;
        udisplay_init(&ctx_b, &cfg_b);
    }
};

/* 1. Alternating partial TCP frames fed into two contexts must not
 *    cross-contaminate each other's reassembly buffer. */
TEST_F(MultiInstanceTest, AlternatingPartialFrames_NoCrossContamination)
{
    udisplay_on_connect(&ctx_a);
    udisplay_on_connect(&ctx_b);
    sent_a.clear();
    sent_b.clear();

    uint8_t raw_a[1] = { 0x02u };   /* CLIENT_READY */
    uint8_t raw_b[1] = { 0x02u };
    uint8_t framed_a[3] = {};
    uint8_t framed_b[3] = {};
    uint16_t na = udisplay_tcp_frame(framed_a, sizeof(framed_a), raw_a, 1u);
    uint16_t nb = udisplay_tcp_frame(framed_b, sizeof(framed_b), raw_b, 1u);
    ASSERT_EQ(na, 3u);
    ASSERT_EQ(nb, 3u);

    /* Interleave byte-by-byte across both contexts: a[0], b[0], a[1], b[1], ... */
    for (uint16_t i = 0; i < na; ++i) {
        udisplay_feed(&ctx_a, framed_a + i, 1u);
        udisplay_feed(&ctx_b, framed_b + i, 1u);
    }

    /* Both must have independently activated -- neither should have consumed
     * or corrupted the other's partial bytes. */
    udisplay_send_float(&ctx_a, 0x01u, 1.0f);
    udisplay_send_float(&ctx_b, 0x01u, 2.0f);
    EXPECT_FALSE(sent_a.empty()) << "ctx_a failed to activate independently";
    EXPECT_FALSE(sent_b.empty()) << "ctx_b failed to activate independently";
}

/* 2. Independent auth/reset: ctx_a authenticates, ctx_b (no auth configured)
 *    is untouched by anything that happens on ctx_a. */
TEST_F(MultiInstanceTest, IndependentAuthAndReset)
{
    /* Re-init ctx_a with auth enabled; ctx_b stays auth-disabled from SetUp(). */
    static int auth_calls = 0;
    auth_calls = 0;
    auto auth_check = [](const uint8_t*, uint8_t, const uint8_t*, void*) -> int {
        ++auth_calls;
        return 1;
    };
    auto fill_random = [](uint8_t* buf, uint8_t len, void*) {
        for (uint8_t i = 0; i < len; i++) buf[i] = i;
    };

    udisplay_config_t cfg_a{};
    cfg_a.merkle_root  = kDummyRootA;
    cfg_a.chunks       = kChunks;
    cfg_a.chunk_hashes = kHashes;
    cfg_a.chunk_lens   = kLens;
    cfg_a.chunk_count  = 1u;
    cfg_a.send         = on_send_a;
    cfg_a.userdata     = &sent_a;
    cfg_a.transport    = UDISPLAY_TRANSPORT_TCP;
    cfg_a.auth_algo    = UDISPLAY_AUTH_HMAC_SHA256;
    cfg_a.auth_check   = auth_check;
    cfg_a.fill_random  = fill_random;
    udisplay_init(&ctx_a, &cfg_a);

    udisplay_on_connect(&ctx_a);
    udisplay_on_connect(&ctx_b);

    /* ctx_a is mid-auth-handshake; ctx_b (no auth) should already be past
     * bootstrap and able to send state updates freely once active. */
    uint8_t cr[1] = { 0x02u };
    udisplay_on_message(&ctx_b, cr, 1u);   /* CLIENT_READY on ctx_b only */

    sent_b.clear();
    udisplay_send_float(&ctx_b, 0x01u, 5.0f);
    EXPECT_FALSE(sent_b.empty()) << "ctx_b's state must be unaffected by ctx_a's auth flow";

    /* ctx_a must still be gated on auth (never sent CLIENT_READY) -- state
     * sends must be dropped since active=0. */
    size_t before = sent_a.size();
    udisplay_send_float(&ctx_a, 0x01u, 9.0f);
    EXPECT_EQ(sent_a.size(), before) << "ctx_a must still be inactive, independent of ctx_b's activity";

    /* Disconnecting ctx_a must not affect ctx_b's active state. */
    udisplay_on_disconnect(&ctx_a);
    sent_b.clear();
    udisplay_send_float(&ctx_b, 0x01u, 7.0f);
    EXPECT_FALSE(sent_b.empty()) << "ctx_b must remain active after ctx_a resets";
}

/* 3. Separate write callbacks: sends from ctx_a must only ever reach
 *    sent_a, and sends from ctx_b must only ever reach sent_b. */
TEST_F(MultiInstanceTest, SeparateWriteCallbacks_NeverCrossDeliver)
{
    udisplay_on_connect(&ctx_a);
    udisplay_on_connect(&ctx_b);
    uint8_t cr[1] = { 0x02u };
    udisplay_on_message(&ctx_a, cr, 1u);
    udisplay_on_message(&ctx_b, cr, 1u);

    sent_a.clear();
    sent_b.clear();

    udisplay_send_float(&ctx_a, 0x01u, 1.0f);
    EXPECT_EQ(sent_a.size(), 1u);
    EXPECT_TRUE(sent_b.empty()) << "ctx_a's send must never reach ctx_b's callback";

    udisplay_send_float(&ctx_b, 0x01u, 2.0f);
    EXPECT_EQ(sent_b.size(), 1u);
    EXPECT_EQ(sent_a.size(), 1u) << "ctx_b's send must never reach ctx_a's callback";
}

/* 4. Distinct merkle roots per context prove cfg is genuinely per-instance,
 *    not shared/aliased. */
TEST_F(MultiInstanceTest, DistinctConfigPerInstance_HandshakeCarriesOwnRoot)
{
    udisplay_on_connect(&ctx_a);
    udisplay_on_connect(&ctx_b);

    ASSERT_FALSE(sent_a.empty());
    ASSERT_FALSE(sent_b.empty());
    /* SetUp() configures TCP transport, so each sent message carries a
     * 2-byte u16le length prefix before the actual protocol bytes:
     * [len_lo][len_hi][type=0x00][proto_ver][flags][merkle_root(32)]... */
    const auto& ha = sent_a.front().data;
    const auto& hb = sent_b.front().data;
    constexpr size_t kRootOffset = 2u /* TCP length prefix */ + 3u /* HANDSHAKE header */;
    ASSERT_GE(ha.size(), kRootOffset + 32u);
    ASSERT_GE(hb.size(), kRootOffset + 32u);
    EXPECT_EQ(ha[kRootOffset], kDummyRootA[0]);
    EXPECT_EQ(hb[kRootOffset], kDummyRootB[0]);
    EXPECT_NE(ha[kRootOffset], hb[kRootOffset]) << "each context must carry its own configured merkle root";
}

/* 5. Regression test for a bug caught during adversarial review: the
 *    fill_random-not-configured fallback salt generator used to keep its
 *    counter in a function-local `static`, shared across every udisplay_t
 *    instance. That meant ctx_b's salt sequence depended on how many times
 *    ctx_a had already generated a salt -- a deterministic, single-threaded
 *    ordering bug (not just a concurrency hazard) that directly contradicted
 *    udisplay.h's documented concurrency contract ("instances share no
 *    state"). Fixed by moving the counter into udisplay_t
 *    (insecure_salt_ctr). This test proves ctx_b's first salt byte does NOT
 *    depend on ctx_a having already run its salt generator first. */
TEST_F(MultiInstanceTest, InsecureSaltFallback_CounterDoesNotLeakAcrossInstances)
{
    auto auth_cfg_no_fill_random = [](const uint8_t* root, udisplay_send_fn send, void* ud) {
        udisplay_config_t cfg{};
        cfg.merkle_root  = root;
        cfg.chunks       = kChunks;
        cfg.chunk_hashes = kHashes;
        cfg.chunk_lens   = kLens;
        cfg.chunk_count  = 1u;
        cfg.send         = send;
        cfg.userdata     = ud;
        cfg.transport    = UDISPLAY_TRANSPORT_TCP;
        cfg.auth_algo    = UDISPLAY_AUTH_HMAC_SHA256;
        cfg.auth_check   = [](const uint8_t*, uint8_t, const uint8_t*, void*) -> int { return 0; };
        /* fill_random left NULL -- exercises the insecure fallback path */
        return cfg;
    };

    udisplay_config_t cfg_a = auth_cfg_no_fill_random(kDummyRootA, on_send_a, &sent_a);
    udisplay_config_t cfg_b = auth_cfg_no_fill_random(kDummyRootB, on_send_b, &sent_b);
    udisplay_init(&ctx_a, &cfg_a);
    udisplay_init(&ctx_b, &cfg_b);

    /* Exhaust several salt generations on ctx_a BEFORE ctx_b ever runs. Under
     * the old shared-static-counter bug, this would advance a global counter
     * that ctx_b's first salt would then inherit. */
    for (int i = 0; i < 5; ++i) {
        udisplay_on_connect(&ctx_a);   /* each call regenerates the salt */
    }

    /* ctx_b's first-ever salt must be identical to what a completely fresh
     * instance would produce -- NOT shifted by ctx_a's prior activity. */
    udisplay_on_connect(&ctx_b);
    ASSERT_FALSE(sent_b.empty());
    constexpr size_t kSaltOffset = 2u /* TCP length prefix */ + 4u /* HANDSHAKE_AUTH header up to salt */;
    ASSERT_GE(sent_b.front().data.size(), kSaltOffset + 1u);
    /* Deterministic fallback: first salt byte is always (0 ^ (0*7)) ^ ++ctr
     * starting from ctr=0 -- i.e. 1 -- for any FRESH instance, regardless of
     * how many times a different instance has generated salts before it. */
    EXPECT_EQ(sent_b.front().data[kSaltOffset], 0x01u)
        << "ctx_b's salt counter must start fresh, not inherit ctx_a's prior "
           "salt-generation count (this is the bug the fix closes)";
}
