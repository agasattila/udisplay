/**
 * udisplay dispatch layer tests.
 *
 * Verifies the active-state gating introduced by CLIENT_READY (0x02):
 *   - EVENT messages are dispatched only when active=1.
 *   - STATE_UPDATE sends are blocked when active=0.
 *   - HEARTBEAT is always sent when connected (bypasses active gate).
 *   - active resets to 0 on connect and on disconnect.
 *
 * Also verifies the shared comms-miss watchdog (TODO-014): it now covers
 * BOOTSTRAP stalls (connected=1, active=0) in addition to the original
 * post-active missed-heartbeat-echo case, sharing one counter and
 * threshold (UDISPLAY_HB_MISS_MAX) since the two phases are mutually
 * exclusive per connection.
 */
#include <gtest/gtest.h>
#include "../include/udisplay.h"
#include "protocol.h"
#include <cstring>
#include <vector>

/* ── Minimal chunk fixture (1 dummy chunk) ───────────────────────────────── */

static const uint8_t kDummyChunk[256]  = {};
static const uint8_t kDummyHash[32]    = {};
static const uint8_t kDummyRoot[32]    = {};
static const uint8_t* kChunks[1]       = { kDummyChunk };
static const uint8_t* kHashes[1]       = { kDummyHash  };
static const uint16_t kLens[1]         = { 50u };

/* ── Test fixture ────────────────────────────────────────────────────────── */

struct Sent {
    std::vector<uint8_t> data;
};

static void on_send(const uint8_t* d, uint16_t n, void* ud)
{
    auto* v = static_cast<std::vector<Sent>*>(ud);
    v->push_back({ std::vector<uint8_t>(d, d + n) });
}

static int g_event_calls = 0;
static int g_ready_calls = 0;
static int g_error_calls = 0;

static void on_event(const udisplay_event_t*, void*)
{
    ++g_event_calls;
}

static void on_ready(void*)
{
    ++g_ready_calls;
}

static void on_comms_error(void*)
{
    ++g_error_calls;
}

class UDisplayTest : public ::testing::Test {
protected:
    std::vector<Sent> sent;

    void SetUp() override {
        g_event_calls = 0;
        g_ready_calls = 0;
        g_error_calls = 0;
        sent.clear();

        udisplay_config_t cfg{};
        cfg.merkle_root      = kDummyRoot;
        cfg.chunks           = kChunks;
        cfg.chunk_hashes     = kHashes;
        cfg.chunk_lens       = kLens;
        cfg.chunk_count      = 1u;
        cfg.send             = on_send;
        cfg.on_event         = on_event;
        cfg.on_client_ready  = on_ready;
        cfg.on_comms_error   = on_comms_error;
        cfg.userdata         = &sent;

        udisplay_init(&cfg);
    }

    /* Helper: feed one raw message */
    void feed(std::initializer_list<uint8_t> bytes) {
        std::vector<uint8_t> v(bytes);
        udisplay_on_message(v.data(), static_cast<uint16_t>(v.size()));
    }

    /* Helper: build a minimal EVENT (button press on widget 0x10) */
    static std::vector<uint8_t> make_event() {
        /* MSG_EVENT=0x31, widget_id=0x10, event_type=0x01 (button) */
        return { 0x31u, 0x10u, 0x01u };
    }
};

/* ── Tests ───────────────────────────────────────────────────────────────── */

TEST_F(UDisplayTest, ClientReady_SetsActive)
{
    udisplay_on_connect();
    feed({ 0x02u });   /* CLIENT_READY */

    /* EVENT should now be dispatched */
    auto ev = make_event();
    udisplay_on_message(ev.data(), static_cast<uint16_t>(ev.size()));
    EXPECT_EQ(g_event_calls, 1);
}

TEST_F(UDisplayTest, ClientReady_Idempotent)
{
    udisplay_on_connect();
    feed({ 0x02u });
    feed({ 0x02u });   /* second CLIENT_READY */

    auto ev = make_event();
    udisplay_on_message(ev.data(), static_cast<uint16_t>(ev.size()));
    EXPECT_EQ(g_event_calls, 1);
    EXPECT_EQ(g_ready_calls, 1);   /* on_client_ready fires exactly once */
}

TEST_F(UDisplayTest, Event_DroppedBeforeActive)
{
    udisplay_on_connect();
    /* No CLIENT_READY — active=0 */
    auto ev = make_event();
    udisplay_on_message(ev.data(), static_cast<uint16_t>(ev.size()));
    EXPECT_EQ(g_event_calls, 0);
}

TEST_F(UDisplayTest, Event_DispatchedAfterActive)
{
    udisplay_on_connect();
    feed({ 0x02u });   /* CLIENT_READY → active=1 */
    auto ev = make_event();
    udisplay_on_message(ev.data(), static_cast<uint16_t>(ev.size()));
    EXPECT_EQ(g_event_calls, 1);
}

TEST_F(UDisplayTest, SendFloat_DroppedBeforeActive)
{
    udisplay_on_connect();
    /* active=0: send callbacks should NOT fire for STATE_UPDATE */
    size_t before = sent.size();
    udisplay_send_float(0x10u, 3.14f);
    EXPECT_EQ(sent.size(), before);   /* nothing sent */
}

TEST_F(UDisplayTest, SendFloat_AllowedAfterActive)
{
    udisplay_on_connect();
    feed({ 0x02u });   /* CLIENT_READY */
    size_t before = sent.size();
    udisplay_send_float(0x10u, 3.14f);
    EXPECT_GT(sent.size(), before);   /* something sent */
}

TEST_F(UDisplayTest, ActiveReset_OnDisconnect)
{
    udisplay_on_connect();
    feed({ 0x02u });   /* active=1 */
    udisplay_on_disconnect();

    /* Reconnect without another CLIENT_READY */
    udisplay_on_connect();
    auto ev = make_event();
    udisplay_on_message(ev.data(), static_cast<uint16_t>(ev.size()));
    EXPECT_EQ(g_event_calls, 0);   /* active reset by disconnect */
}

TEST_F(UDisplayTest, ActiveReset_OnConnect)
{
    /* Simulate BLE drop without disconnect: active left set */
    udisplay_on_connect();
    feed({ 0x02u });   /* active=1 */

    /* New connection arrives without a disconnect event first */
    udisplay_on_connect();   /* must reset active=0 */

    auto ev = make_event();
    udisplay_on_message(ev.data(), static_cast<uint16_t>(ev.size()));
    EXPECT_EQ(g_event_calls, 0);   /* active was reset by new connect */
}

TEST_F(UDisplayTest, Heartbeat_SentBeforeActive)
{
    udisplay_on_connect();
    /* No CLIENT_READY — active=0 */
    size_t before = sent.size();
    udisplay_heartbeat();
    EXPECT_GT(sent.size(), before);   /* HEARTBEAT bypasses active gate */
    EXPECT_EQ(sent.back().data[0], 0x40u);   /* MSG_HEARTBEAT */
}

/* ── on_client_ready tests ───────────────────────────────────────────────── */

TEST_F(UDisplayTest, ClientReady_CallsOnReadyCallback)
{
    udisplay_on_connect();
    EXPECT_EQ(g_ready_calls, 0);
    feed({ 0x02u });
    EXPECT_EQ(g_ready_calls, 1);
}

TEST_F(UDisplayTest, ClientReady_CallbackNotCalledIfAlreadyActive)
{
    udisplay_on_connect();
    feed({ 0x02u });
    EXPECT_EQ(g_ready_calls, 1);
    feed({ 0x02u });   /* second CLIENT_READY — already active */
    EXPECT_EQ(g_ready_calls, 1);
}

TEST_F(UDisplayTest, ClientReady_NullCallback_NoSEGV)
{
    /* Re-init with on_client_ready=NULL */
    udisplay_config_t cfg{};
    cfg.merkle_root  = kDummyRoot;
    cfg.chunks       = kChunks;
    cfg.chunk_hashes = kHashes;
    cfg.chunk_lens   = kLens;
    cfg.chunk_count  = 1u;
    cfg.send         = on_send;
    cfg.userdata     = &sent;
    /* on_client_ready left NULL */
    udisplay_init(&cfg);

    udisplay_on_connect();
    feed({ 0x02u });   /* must not crash */
    SUCCEED();
}

/* ── heartbeat watchdog tests ────────────────────────────────────────────── */

TEST_F(UDisplayTest, Heartbeat_BootstrapWatchdogFiresAt3Misses)
{
    udisplay_on_connect();
    /* active=0, no bootstrap progress — 3 heartbeats SHOULD fire
     * on_comms_error (TODO-014: bootstrap-stall watchdog). */
    udisplay_heartbeat();   /* miss 1 */
    udisplay_heartbeat();   /* miss 2 */
    udisplay_heartbeat();   /* miss 3 → fires */
    EXPECT_EQ(g_error_calls, 1);
}

TEST_F(UDisplayTest, Heartbeat_BootstrapChunkRequest_ResetsCounter)
{
    udisplay_on_connect();
    udisplay_heartbeat();   /* miss 1 */
    udisplay_heartbeat();   /* miss 2 */
    feed({ 0x20u, 0x00u, 0x00u });   /* CHUNK_REQUEST idx 0 — resets counter */
    udisplay_heartbeat();
    udisplay_heartbeat();
    EXPECT_EQ(g_error_calls, 0);   /* only 2 misses since the reset */
    udisplay_heartbeat();   /* miss 3 → fires */
    EXPECT_EQ(g_error_calls, 1);
}

TEST_F(UDisplayTest, Heartbeat_BootstrapChunkHeaderRequest_ResetsCounter)
{
    udisplay_on_connect();
    udisplay_heartbeat();   /* miss 1 */
    udisplay_heartbeat();   /* miss 2 */
    feed({ 0x10u, 0x00u, 0x00u });   /* CHUNK_HEADER_REQUEST idx 0 — resets counter */
    udisplay_heartbeat();
    udisplay_heartbeat();
    EXPECT_EQ(g_error_calls, 0);
    udisplay_heartbeat();   /* miss 3 → fires */
    EXPECT_EQ(g_error_calls, 1);
}

TEST_F(UDisplayTest, Heartbeat_BootstrapHandshakeAck_ResetsCounter)
{
    udisplay_on_connect();
    udisplay_heartbeat();   /* miss 1 */
    udisplay_heartbeat();   /* miss 2 */
    feed({ 0x01u, 0x04u });   /* HANDSHAKE_ACK (no-auth) — resets counter */
    udisplay_heartbeat();
    udisplay_heartbeat();
    EXPECT_EQ(g_error_calls, 0);
    udisplay_heartbeat();   /* miss 3 → fires */
    EXPECT_EQ(g_error_calls, 1);
}

TEST_F(UDisplayTest, Heartbeat_ActiveTransition_NoCarryOverFire)
{
    udisplay_on_connect();
    udisplay_heartbeat();   /* miss 1 */
    udisplay_heartbeat();   /* miss 2 */
    feed({ 0x02u });        /* CLIENT_READY — resets counter, active=1 */
    udisplay_heartbeat();
    EXPECT_EQ(g_error_calls, 0);   /* must not carry the 2 bootstrap misses over */
}

TEST_F(UDisplayTest, Heartbeat_MissCountIncrementsAfterActive)
{
    udisplay_on_connect();
    feed({ 0x02u });   /* active=1 */
    udisplay_heartbeat();   /* miss 1 */
    udisplay_heartbeat();   /* miss 2 */
    EXPECT_EQ(g_error_calls, 0);   /* not yet at threshold */
}

TEST_F(UDisplayTest, Heartbeat_WatchdogFiresAt3Misses)
{
    udisplay_on_connect();
    feed({ 0x02u });   /* active=1 */
    udisplay_heartbeat();   /* miss 1 */
    udisplay_heartbeat();   /* miss 2 */
    udisplay_heartbeat();   /* miss 3 → fires */
    EXPECT_EQ(g_error_calls, 1);
    /* 4th heartbeat must NOT fire again (counter capped) */
    udisplay_heartbeat();
    EXPECT_EQ(g_error_calls, 1);
}

TEST_F(UDisplayTest, Heartbeat_WatchdogNullCallback_NoSEGV)
{
    /* Re-init with on_comms_error=NULL */
    udisplay_config_t cfg{};
    cfg.merkle_root  = kDummyRoot;
    cfg.chunks       = kChunks;
    cfg.chunk_hashes = kHashes;
    cfg.chunk_lens   = kLens;
    cfg.chunk_count  = 1u;
    cfg.send         = on_send;
    cfg.userdata     = &sent;
    /* on_comms_error left NULL */
    udisplay_init(&cfg);

    udisplay_on_connect();
    feed({ 0x02u });
    udisplay_heartbeat();
    udisplay_heartbeat();
    udisplay_heartbeat();   /* must not crash */
    SUCCEED();
}

TEST_F(UDisplayTest, Heartbeat_EchoResetsCounter)
{
    udisplay_on_connect();
    feed({ 0x02u });   /* active=1 */
    udisplay_heartbeat();   /* miss 1 */
    udisplay_heartbeat();   /* miss 2 */
    feed({ 0x40u });        /* echo received — counter resets to 0 */
    /* Now need 3 more misses to fire */
    udisplay_heartbeat();
    udisplay_heartbeat();
    EXPECT_EQ(g_error_calls, 0);
    udisplay_heartbeat();   /* miss 3 → fires */
    EXPECT_EQ(g_error_calls, 1);
}

TEST_F(UDisplayTest, Heartbeat_MissCountResetsOnConnect)
{
    udisplay_on_connect();
    feed({ 0x02u });        /* active=1 */
    udisplay_heartbeat();   /* miss 1 */
    udisplay_heartbeat();   /* miss 2 */
    /* Reconnect resets counter */
    udisplay_on_connect();
    feed({ 0x02u });        /* active=1 again */
    udisplay_heartbeat();
    udisplay_heartbeat();
    EXPECT_EQ(g_error_calls, 0);   /* fresh start: only 2 misses */
    udisplay_heartbeat();
    EXPECT_EQ(g_error_calls, 1);
}

TEST_F(UDisplayTest, Heartbeat_MissCountResetsOnDisconnect)
{
    udisplay_on_connect();
    feed({ 0x02u });        /* active=1 */
    udisplay_heartbeat();   /* miss 1 */
    udisplay_heartbeat();   /* miss 2 */
    udisplay_on_disconnect();   /* resets counter */
    /* Reconnect — counter must be 0 */
    udisplay_on_connect();
    feed({ 0x02u });
    udisplay_heartbeat();
    udisplay_heartbeat();
    EXPECT_EQ(g_error_calls, 0);
}

/* ── Authentication tests ────────────────────────────────────────────────── */

/* Auth fixture: device configured with SHA-256 auth */
struct AuthSent { std::vector<uint8_t> data; };
static std::vector<AuthSent> g_auth_sent;
static int g_auth_check_calls = 0;
static int g_auth_check_result = 1; /* default: accept */
static uint8_t g_last_salt[32] = {};
static uint8_t g_last_cred[32] = {};

static void auth_send(const uint8_t* d, uint16_t n, void*)
{
    g_auth_sent.push_back({ std::vector<uint8_t>(d, d + n) });
}

static int auth_check_fn(const uint8_t* cred, uint8_t len, const uint8_t* salt, void*)
{
    (void)len;
    ++g_auth_check_calls;
    memcpy(g_last_cred, cred, 32);
    memcpy(g_last_salt, salt, 32);
    return g_auth_check_result;
}

static void fill_rand_fn(uint8_t* buf, uint8_t len, void*)
{
    /* Deterministic "random" for test predictability */
    for (uint8_t i = 0; i < len; i++) buf[i] = (uint8_t)(i + 1);
}

static udisplay_config_t make_auth_cfg()
{
    udisplay_config_t cfg{};
    cfg.merkle_root      = kDummyRoot;
    cfg.chunks           = kChunks;
    cfg.chunk_hashes     = kHashes;
    cfg.chunk_lens       = kLens;
    cfg.chunk_count      = 1u;
    cfg.send             = auth_send;
    cfg.on_event         = on_event;
    cfg.on_client_ready  = on_ready;
    cfg.on_comms_error   = on_comms_error;
    cfg.userdata         = nullptr;
    cfg.auth_algo        = UDISPLAY_AUTH_HMAC_SHA256;
    cfg.auth_check       = auth_check_fn;
    cfg.fill_random      = fill_rand_fn;
    return cfg;
}

class AuthTest : public ::testing::Test {
protected:
    void SetUp() override {
        g_auth_sent.clear();
        g_auth_check_calls = 0;
        g_auth_check_result = 1;
        g_event_calls = 0;
        g_ready_calls = 0;
        memset(g_last_salt, 0, 32);
        memset(g_last_cred, 0, 32);
        udisplay_config_t cfg = make_auth_cfg();
        udisplay_init(&cfg);
    }

    /* Build and feed a HANDSHAKE_ACK(flags=1, credential) message */
    void feed_auth_ack(const uint8_t credential[32]) {
        uint8_t msg[35];
        msg[0] = 0x01u; msg[1] = 0x04u; msg[2] = 0x01u;
        memcpy(msg + 3, credential, 32);
        udisplay_on_message(msg, 35u);
    }

    /* Build and feed a HANDSHAKE_ACK(flags=0) message */
    void feed_noauth_ack() {
        uint8_t msg[3] = {0x01u, 0x04u, 0x00u};
        udisplay_on_message(msg, 3u);
    }

    static bool sent_is_auth_challenge(const std::vector<uint8_t>& d) {
        return d.size() == 36u && d[0] == 0x00u && d[2] == 0x01u;
    }
    static bool sent_is_noauth_handshake(const std::vector<uint8_t>& d) {
        return d.size() == 39u && d[0] == 0x00u && d[2] == 0x00u;
    }
};

TEST_F(AuthTest, Connect_SendsAuthChallenge)
{
    udisplay_on_connect();
    ASSERT_FALSE(g_auth_sent.empty());
    EXPECT_TRUE(sent_is_auth_challenge(g_auth_sent[0].data));
    EXPECT_EQ(g_auth_sent[0].data[3], 0x01u); /* algo = SHA-256 */
    /* fill_rand_fn fills bytes 1..32 */
    EXPECT_EQ(g_auth_sent[0].data[4], 0x01u);
    EXPECT_EQ(g_auth_sent[0].data[36 - 1], 0x20u);
}

TEST_F(AuthTest, AuthPass_SendsNoAuthHandshake)
{
    udisplay_on_connect();
    g_auth_check_result = 1; /* accept */
    uint8_t cred[32] = {};
    feed_auth_ack(cred);
    EXPECT_EQ(g_auth_check_calls, 1);
    /* After pass, second message must be HANDSHAKE(flags=0) with merkle_root */
    ASSERT_GE(g_auth_sent.size(), 2u);
    EXPECT_TRUE(sent_is_noauth_handshake(g_auth_sent[1].data));
}

TEST_F(AuthTest, AuthFail_ResendsAuthChallenge_WithNewSalt)
{
    udisplay_on_connect();
    g_auth_check_result = 0; /* reject */
    uint8_t cred[32] = {};
    size_t before = g_auth_sent.size();
    feed_auth_ack(cred);
    EXPECT_EQ(g_auth_check_calls, 1);
    /* Must re-send auth challenge (not normal HANDSHAKE) */
    ASSERT_GT(g_auth_sent.size(), before);
    EXPECT_TRUE(sent_is_auth_challenge(g_auth_sent.back().data));
}

TEST_F(AuthTest, AuthDisconnect_ClearsState)
{
    udisplay_on_connect();
    g_auth_check_result = -1; /* terminate */
    uint8_t cred[32] = {};
    feed_auth_ack(cred);
    EXPECT_EQ(g_auth_check_calls, 1);
    /* Library must have cleared connection state — no new auth challenge sent */
    size_t after = g_auth_sent.size();
    /* Feeding another ACK should be ignored (disconnected) */
    feed_auth_ack(cred);
    EXPECT_EQ(g_auth_sent.size(), after);
}

TEST_F(AuthTest, WrongFlags_SilentlyDisconnects)
{
    /* Auth is expected, but client sends flags=0 ACK — reject */
    udisplay_on_connect();
    size_t before = g_auth_sent.size();
    feed_noauth_ack();
    EXPECT_EQ(g_auth_check_calls, 0); /* auth_check must not have been called */
    /* Feeding another message should be ignored (state cleared) */
    feed_noauth_ack();
    EXPECT_EQ(g_auth_sent.size(), before);
}

TEST_F(AuthTest, SaltPassedToAuthCheck)
{
    udisplay_on_connect();
    /* fill_rand_fn fills salt with 0x01..0x20 */
    g_auth_check_result = 1;
    uint8_t cred[32] = {};
    feed_auth_ack(cred);
    EXPECT_EQ(g_last_salt[0], 0x01u);
    EXPECT_EQ(g_last_salt[31], 0x20u);
}

/* ── Transport framing tests ─────────────────────────────────────────────── */

/*
 * HANDSHAKE message size with kDummyRoot / chunk_count=1 is 39 bytes.
 * Confirmed by ProtoEncode.Handshake_39bytes.
 */
static constexpr uint16_t kHandshakeSize = 39u;

static std::vector<Sent> g_transport_sent;

static void transport_send_cb(const uint8_t* d, uint16_t n, void*)
{
    g_transport_sent.push_back({ std::vector<uint8_t>(d, d + n) });
}

class TransportTest : public ::testing::Test {
protected:
    void SetUp() override { g_transport_sent.clear(); }

    udisplay_config_t ble_cfg(uint16_t mtu = 0u) const
    {
        udisplay_config_t cfg{};
        cfg.merkle_root     = kDummyRoot;
        cfg.chunks          = kChunks;
        cfg.chunk_hashes    = kHashes;
        cfg.chunk_lens      = kLens;
        cfg.chunk_count     = 1u;
        cfg.send            = transport_send_cb;
        cfg.transport       = UDISPLAY_TRANSPORT_BLE;
        cfg.ble_mtu_payload = mtu;
        return cfg;
    }

    udisplay_config_t tcp_cfg() const
    {
        udisplay_config_t cfg{};
        cfg.merkle_root  = kDummyRoot;
        cfg.chunks       = kChunks;
        cfg.chunk_hashes = kHashes;
        cfg.chunk_lens   = kLens;
        cfg.chunk_count  = 1u;
        cfg.send         = transport_send_cb;
        cfg.transport    = UDISPLAY_TRANSPORT_TCP;
        return cfg;
    }

    static void make_active()
    {
        uint8_t cr = 0x02u;
        udisplay_on_message(&cr, 1u);   /* CLIENT_READY → active=1 */
    }
};

/* 1: HANDSHAKE on BLE transport arrives as v2.2 fragmented ATT notifications */
TEST_F(TransportTest, Ble_Handshake_IsFragmented)
{
    auto cfg = ble_cfg();   /* MTU=0 → clamps to default (20), first_cap=14, cont_cap=17 */
    udisplay_init(&cfg);
    udisplay_on_connect();

    /* 39 bytes at mtu=20: first 14 + cont 17 + cont 8 = 3 fragments */
    ASSERT_EQ(g_transport_sent.size(), 3u);
    /* First fragment: v2.2 offset field = 0x0000 */
    EXPECT_EQ(g_transport_sent[0].data[0], 0x00u);
    EXPECT_EQ(g_transport_sent[0].data[1], 0x00u);
    /* Continuations have increasing offsets */
    uint16_t off1 = (uint16_t)g_transport_sent[1].data[0]
                  | ((uint16_t)g_transport_sent[1].data[1] << 8u);
    uint16_t off2 = (uint16_t)g_transport_sent[2].data[0]
                  | ((uint16_t)g_transport_sent[2].data[1] << 8u);
    EXPECT_GT(off1, 0u);
    EXPECT_GT(off2, off1);
}

/* 2: State update on BLE transport carries a v2.2 fragment header */
TEST_F(TransportTest, Ble_StateUpdate_HasFlagByte)
{
    auto cfg = ble_cfg();
    udisplay_init(&cfg);
    udisplay_on_connect();
    g_transport_sent.clear();
    make_active();

    udisplay_send_float(0x01u, 1.0f);   /* 7-byte message; fits in 1 fragment */

    ASSERT_EQ(g_transport_sent.size(), 1u);
    /* Single fragment: v2.2 offset lo = 0x00 (single fragment starts at offset 0) */
    EXPECT_EQ(g_transport_sent[0].data[0], 0x00u);
    EXPECT_GT(g_transport_sent[0].data.size(), 6u);   /* 6-byte header + payload */
}

/* 3: Fragment count scales with MTU under v2.2 header overhead */
TEST_F(TransportTest, Ble_FragmentCount_MatchesMtu)
{
    /* mtu=10: first_cap=4, cont_cap=7 → 1 + ceil((39-4)/7) = 6 fragments */
    auto cfg = ble_cfg(10u);
    udisplay_init(&cfg);
    udisplay_on_connect();

    ASSERT_EQ(g_transport_sent.size(), 6u);
    /* All but last are full ATT payload (10 bytes) */
    for (size_t i = 0; i + 1 < g_transport_sent.size(); ++i)
        EXPECT_EQ(g_transport_sent[i].data.size(), 10u) << "frag " << i;
    /* Reconstruct total message bytes by subtracting per-fragment header sizes */
    uint32_t total_data = (uint32_t)(g_transport_sent[0].data.size()) - 6u; /* first: 6-byte header */
    for (size_t i = 1; i < g_transport_sent.size(); ++i)
        total_data += (uint32_t)(g_transport_sent[i].data.size()) - 3u;     /* cont: 3-byte header */
    EXPECT_EQ(total_data, kHandshakeSize);
}

/* 4: HANDSHAKE on TCP transport arrives as a single length-prefixed frame */
TEST_F(TransportTest, Tcp_Handshake_IsTcpFramed)
{
    auto cfg = tcp_cfg();
    udisplay_init(&cfg);
    udisplay_on_connect();

    ASSERT_EQ(g_transport_sent.size(), 1u);
    const auto& framed = g_transport_sent[0].data;
    ASSERT_GE(framed.size(), 2u);
    uint16_t declared_len = static_cast<uint16_t>(framed[0])
                          | (static_cast<uint16_t>(framed[1]) << 8u);
    EXPECT_EQ(declared_len, static_cast<uint16_t>(framed.size() - 2u));
    EXPECT_EQ(framed[2], 0x00u);   /* first byte of HANDSHAKE = MSG_HANDSHAKE */
}

/* 5: State update on TCP transport has the u16le length prefix */
TEST_F(TransportTest, Tcp_StateUpdate_IsTcpFramed)
{
    auto cfg = tcp_cfg();
    udisplay_init(&cfg);
    udisplay_on_connect();
    g_transport_sent.clear();
    make_active();

    udisplay_send_float(0x01u, 1.0f);

    ASSERT_EQ(g_transport_sent.size(), 1u);
    const auto& framed = g_transport_sent[0].data;
    ASSERT_GE(framed.size(), 2u);
    uint16_t declared_len = static_cast<uint16_t>(framed[0])
                          | (static_cast<uint16_t>(framed[1]) << 8u);
    EXPECT_EQ(declared_len + 2u, static_cast<uint16_t>(framed.size()));
}

/* 6: udisplay_ble_set_mtu() reduces fragment count for subsequent messages */
TEST_F(TransportTest, Ble_SetMtu_UpdatesFragmentSize)
{
    auto cfg = ble_cfg();   /* default mtu=20: 3 frags for 39-byte HANDSHAKE */
    udisplay_init(&cfg);
    udisplay_on_connect();
    size_t frags_small_mtu = g_transport_sent.size();
    ASSERT_GT(frags_small_mtu, 1u);   /* must be fragmented */

    g_transport_sent.clear();
    udisplay_on_disconnect();

    /* mtu=45: first_cap=39 → HANDSHAKE (39 bytes) fits in exactly 1 fragment */
    udisplay_ble_set_mtu(45u);
    udisplay_on_connect();

    ASSERT_EQ(g_transport_sent.size(), 1u);
    /* Single fragment: v2.2 offset = 0x0000 */
    EXPECT_EQ(g_transport_sent[0].data[0], 0x00u);
    EXPECT_EQ(g_transport_sent[0].data[1], 0x00u);
}

/* 7: ble_mtu_payload=0 in config clamps to default, no infinite loop */
TEST_F(TransportTest, Ble_ZeroMtuConfig_ClampsToDefault)
{
    auto cfg = ble_cfg(0u);   /* explicit zero → must clamp to mtu=20 */
    udisplay_init(&cfg);
    udisplay_on_connect();    /* must not loop or crash */

    ASSERT_FALSE(g_transport_sent.empty());
    /* Same fragment count as explicit mtu=20: 1 + ceil((39-14)/17) = 3 */
    EXPECT_EQ(g_transport_sent.size(), 3u);
    /* First fragment: v2.2 offset = 0x0000 */
    EXPECT_EQ(g_transport_sent[0].data[0], 0x00u);
    EXPECT_EQ(g_transport_sent[0].data[1], 0x00u);
}

/* 8: udisplay_feed() on TCP transport unframes and dispatches the message */
TEST_F(TransportTest, Feed_Tcp_RoundTrip_DispatchesMessage)
{
    auto cfg = tcp_cfg();
    udisplay_init(&cfg);
    udisplay_on_connect();
    g_transport_sent.clear();

    /* Frame CLIENT_READY (0x02) as a TCP message and feed it back */
    uint8_t raw[1]    = { 0x02u };
    uint8_t framed[3] = {};
    uint16_t n = udisplay_tcp_frame(framed, sizeof(framed), raw, 1u);
    ASSERT_EQ(n, 3u);

    udisplay_feed(framed, n);

    /* Library should now be active: state updates must produce output */
    udisplay_send_float(0x01u, 1.0f);
    EXPECT_FALSE(g_transport_sent.empty());
}

/* 9: udisplay_feed() on TCP transport carries a partial frame across calls */
TEST_F(TransportTest, Feed_Tcp_PartialFrame_CarriesOverToNextCall)
{
    auto cfg = tcp_cfg();
    udisplay_init(&cfg);
    udisplay_on_connect();
    g_transport_sent.clear();

    uint8_t raw[1]    = { 0x02u };   /* CLIENT_READY */
    uint8_t framed[3] = {};
    uint16_t n = udisplay_tcp_frame(framed, sizeof(framed), raw, 1u);
    ASSERT_EQ(n, 3u);

    /* Split the 3-byte frame across two feed() calls */
    udisplay_feed(framed, 2u);
    udisplay_send_float(0x01u, 1.0f);
    EXPECT_TRUE(g_transport_sent.empty()) << "must not activate on a partial frame";

    udisplay_feed(framed + 2, 1u);
    udisplay_send_float(0x01u, 1.0f);
    EXPECT_FALSE(g_transport_sent.empty()) << "completed frame must activate the client";
}

/* 10: udisplay_feed() on TCP transport drains multiple complete frames in one call */
TEST_F(TransportTest, Feed_Tcp_MultipleFramesInOneCall_DrainsAll)
{
    auto cfg = tcp_cfg();
    udisplay_init(&cfg);
    udisplay_on_connect();
    g_transport_sent.clear();

    uint8_t client_ready[1] = { 0x02u };
    uint8_t heartbeat[1]    = { 0x03u };
    uint8_t framed[6]       = {};
    uint16_t n1 = udisplay_tcp_frame(framed, sizeof(framed), client_ready, 1u);
    uint16_t n2 = udisplay_tcp_frame(framed + n1, (uint16_t)(sizeof(framed) - n1), heartbeat, 1u);
    ASSERT_EQ(n1, 3u);
    ASSERT_EQ(n2, 3u);

    udisplay_feed(framed, (uint16_t)(n1 + n2));

    /* CLIENT_READY (first frame) must have activated the client */
    udisplay_send_float(0x01u, 1.0f);
    EXPECT_FALSE(g_transport_sent.empty());
}

/* 11: udisplay_feed() on BLE transport routes through BLE reassembly */
TEST_F(TransportTest, Feed_Ble_RoutesThroughBleReassembly)
{
    auto cfg = ble_cfg(45u);   /* mtu=45: HANDSHAKE fits in 1 fragment */
    udisplay_init(&cfg);
    udisplay_on_connect();
    g_transport_sent.clear();

    /* Client subscribes and completes bootstrap out of band for this test;
     * feed a single-fragment CLIENT_READY (v2.2: offset=0, packet_id, len=1,
     * flags=0, payload=0x02) and confirm it activates the client exactly as
     * udisplay_ble_feed() would. */
    uint8_t frag[7] = { 0x00u, 0x00u, 0x00u, 0x01u, 0x00u, 0x00u, 0x02u };
    udisplay_feed(frag, sizeof(frag));

    udisplay_send_float(0x01u, 1.0f);
    EXPECT_FALSE(g_transport_sent.empty());
}

/* 12: udisplay_feed() on TRANSPORT_NONE passes data straight to udisplay_on_message */
TEST_F(TransportTest, Feed_None_PassesThroughRaw)
{
    udisplay_config_t cfg{};
    cfg.merkle_root  = kDummyRoot;
    cfg.chunks       = kChunks;
    cfg.chunk_hashes = kHashes;
    cfg.chunk_lens   = kLens;
    cfg.chunk_count  = 1u;
    cfg.send         = transport_send_cb;
    cfg.transport    = UDISPLAY_TRANSPORT_NONE;
    udisplay_init(&cfg);
    udisplay_on_connect();
    g_transport_sent.clear();

    uint8_t raw[1] = { 0x02u };   /* CLIENT_READY, already unframed */
    udisplay_feed(raw, 1u);

    udisplay_send_float(0x01u, 1.0f);
    EXPECT_FALSE(g_transport_sent.empty());
}

/* 13: udisplay_ble_set_mtu() rejects a value exceeding the fragment buffer capacity */
TEST_F(TransportTest, Ble_SetMtu_RejectsOverCapacity)
{
    auto cfg = ble_cfg(45u);
    udisplay_init(&cfg);

    EXPECT_EQ(udisplay_ble_set_mtu(518u), 0) << "one byte over the 517-byte fragment buffer";

    /* Prior value (45) must be retained: HANDSHAKE still fits in exactly 1 fragment */
    udisplay_on_connect();
    ASSERT_EQ(g_transport_sent.size(), 1u);
}

/* 14: udisplay_ble_set_mtu() accepts the exact fragment buffer capacity */
TEST_F(TransportTest, Ble_SetMtu_AcceptsExactCapacity)
{
    auto cfg = ble_cfg(45u);
    udisplay_init(&cfg);

    EXPECT_EQ(udisplay_ble_set_mtu(517u), 1);
}

/* 15: udisplay_ble_set_mtu() still rejects too-small values (existing behaviour, new return value) */
TEST_F(TransportTest, Ble_SetMtu_RejectsTooSmall)
{
    auto cfg = ble_cfg(45u);
    udisplay_init(&cfg);

    EXPECT_EQ(udisplay_ble_set_mtu(6u), 0);
}
