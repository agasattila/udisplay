/**
 * NimBLE backend tests (src/nimble/udisplay_nimble.c).
 *
 * The generic backend runs against a fake NimBLE host (test/nimble_fake/):
 * the fake records indications, terminations and advertising, and the tests
 * play the host task by injecting GAP events, characteristic writes and
 * callout expiries.
 */
#include <gtest/gtest.h>

#include <cstring>
#include <string>
#include <vector>

#include "libudisplay/udisplay.h"
#include "libudisplay/udisplay_nimble.h"
#include "fake_nimble.h"

extern "C" {
void udisplay_nimble_test_reset(void);
struct ble_npl_callout* udisplay_nimble_test_heartbeat(void);
struct ble_npl_callout* udisplay_nimble_test_ud_timer(void);
}

namespace {

const uint8_t  kChunk[256] = {};
const uint8_t  kHash[32]   = {};
const uint8_t  kRoot[32]   = {};
const uint8_t* kChunks[1]  = { kChunk };
const uint8_t* kHashes[1]  = { kHash };
const uint16_t kLens[1]    = { 50u };

constexpr uint16_t kConn = 7;
constexpr uint16_t kData = FakeNimble::kDataValHandle;

/* Client echo of a HELLO: single fragment, flags=CONTROL, payload=HELLO. */
const std::vector<uint8_t> kHelloEcho = { 0x00, 0x00, 0x00, 0x01, 0x00,
                                          UDISPLAY_BLE_FLAGS_CONTROL,
                                          UDISPLAY_BLE_CTRL_HELLO };

int g_app_comms_errors;
int g_app_connects;
int g_app_disconnects;
int g_last_disconnect_reason;
int g_lock_depth_in_disconnect;

void app_comms_error(void*) { ++g_app_comms_errors; }
void app_connect(void*) { ++g_app_connects; }
void app_disconnect(int reason, void*)
{
    ++g_app_disconnects;
    g_last_disconnect_reason = reason;
    g_lock_depth_in_disconnect = fake_lock_depth();
}

class NimbleBackendTest : public ::testing::Test {
protected:
    udisplay_t               ctx_{};
    udisplay_nimble_config_t ble_{};

    void SetUp() override
    {
        fake_nimble_reset();
        udisplay_nimble_test_reset();
        g_app_comms_errors = g_app_connects = g_app_disconnects = 0;
        g_last_disconnect_reason = g_lock_depth_in_disconnect = -1;

        udisplay_config_t cfg{};
        cfg.merkle_root    = kRoot;
        cfg.chunks         = kChunks;
        cfg.chunk_hashes   = kHashes;
        cfg.chunk_lens     = kLens;
        cfg.chunk_count    = 1u;
        cfg.send           = udisplay_nimble_send;
        cfg.on_comms_error = app_comms_error;
        cfg.transport      = UDISPLAY_TRANSPORT_BLE;
        udisplay_init(&ctx_, &cfg);

        ble_.device_name   = "Demo05";
        ble_.on_connect    = app_connect;
        ble_.on_disconnect = app_disconnect;
    }

    void TearDown() override { EXPECT_EQ(fake_lock_depth(), 0); }

    /* init + start + host sync (advertising up). */
    void bring_up()
    {
        ASSERT_EQ(udisplay_nimble_init(&ctx_, &ble_), UDISPLAY_NIMBLE_OK);
        ASSERT_EQ(udisplay_nimble_start(), UDISPLAY_NIMBLE_OK);
        ASSERT_NE(ble_hs_cfg.sync_cb, nullptr);
        ble_hs_cfg.sync_cb();
        ASSERT_EQ(g_fake.adv_starts, 1);
    }

    int gap(ble_gap_event ev) { return fake_gap_event(ev); }

    void connect(int status = 0)
    {
        ble_gap_event ev{};
        ev.type = BLE_GAP_EVENT_CONNECT;
        ev.connect.status = status;
        ev.connect.conn_handle = kConn;
        gap(ev);
    }

    void disconnect(int reason = 0x213)
    {
        ble_gap_event ev{};
        ev.type = BLE_GAP_EVENT_DISCONNECT;
        ev.disconnect.reason = reason;
        ev.disconnect.conn.conn_handle = kConn;
        gap(ev);
    }

    void subscribe(uint16_t attr = kData, bool indicate = true)
    {
        ble_gap_event ev{};
        ev.type = BLE_GAP_EVENT_SUBSCRIBE;
        ev.subscribe.conn_handle = kConn;
        ev.subscribe.attr_handle = attr;
        ev.subscribe.cur_indicate = indicate;
        gap(ev);
    }

    void notify_tx(int status, uint16_t attr = kData)
    {
        ble_gap_event ev{};
        ev.type = BLE_GAP_EVENT_NOTIFY_TX;
        ev.notify_tx.status = status;
        ev.notify_tx.conn_handle = kConn;
        ev.notify_tx.attr_handle = attr;
        ev.notify_tx.indication = 1;
        gap(ev);
    }

    void mtu(uint16_t value)
    {
        ble_gap_event ev{};
        ev.type = BLE_GAP_EVENT_MTU;
        ev.mtu.conn_handle = kConn;
        ev.mtu.value = value;
        gap(ev);
    }

    void confirm() { notify_tx(BLE_HS_EDONE); }

    /* Confirm indications until none is outstanding. */
    void drain()
    {
        for (size_t seen = 0; seen < g_fake.indications.size(); ) {
            seen = g_fake.indications.size();
            confirm();
        }
    }

    const ble_gatt_chr_def& ctrl_chr() const { return g_fake.svcs[0].characteristics[0]; }
    const ble_gatt_chr_def& data_chr() const { return g_fake.svcs[0].characteristics[1]; }

    int write_ctrl(const std::vector<uint8_t>& data) { return fake_write(ctrl_chr(), data); }

    /* Connected, subscribed, HELLO echoed, HANDSHAKE fully confirmed. */
    void session_ready()
    {
        bring_up();
        connect();
        subscribe();
        confirm();                              /* HELLO */
        ASSERT_EQ(write_ctrl(kHelloEcho), 0);   /* → HANDSHAKE */
        drain();
        g_fake.indications.clear();
    }
};

}  // namespace

/* ── init / start ──────────────────────────────────────────────────────── */

TEST_F(NimbleBackendTest, InitRejectsBadArguments)
{
    EXPECT_EQ(udisplay_nimble_init(nullptr, &ble_), UDISPLAY_NIMBLE_EINVAL);
    EXPECT_EQ(udisplay_nimble_init(&ctx_, nullptr), UDISPLAY_NIMBLE_EINVAL);
    ble_.device_name = nullptr;
    EXPECT_EQ(udisplay_nimble_init(&ctx_, &ble_), UDISPLAY_NIMBLE_EINVAL);
    ble_.device_name = "";
    EXPECT_EQ(udisplay_nimble_init(&ctx_, &ble_), UDISPLAY_NIMBLE_EINVAL);
    EXPECT_EQ(g_fake.port_init_calls, 0);
}

TEST_F(NimbleBackendTest, InitRejectsInstanceNotSetUpForBackend)
{
    ctx_.cfg.transport = UDISPLAY_TRANSPORT_TCP;
    EXPECT_EQ(udisplay_nimble_init(&ctx_, &ble_), UDISPLAY_NIMBLE_EINVAL);
    ctx_.cfg.transport = UDISPLAY_TRANSPORT_BLE;
    ctx_.cfg.send = nullptr;
    EXPECT_EQ(udisplay_nimble_init(&ctx_, &ble_), UDISPLAY_NIMBLE_EINVAL);
    EXPECT_EQ(g_fake.port_init_calls, 0);
}

TEST_F(NimbleBackendTest, InitPropagatesPortFailure)
{
    g_fake.port_init_rc = 0x103;
    EXPECT_EQ(udisplay_nimble_init(&ctx_, &ble_), 0x103);
    EXPECT_EQ(udisplay_nimble_start(), UDISPLAY_NIMBLE_ESTATE);
}

TEST_F(NimbleBackendTest, InitFailureLeavesInstanceUntouched)
{
    g_fake.add_svcs_rc = BLE_HS_ENOMEM;
    EXPECT_EQ(udisplay_nimble_init(&ctx_, &ble_), BLE_HS_ENOMEM);
    EXPECT_EQ(ctx_.cfg.on_comms_error, app_comms_error);
    EXPECT_EQ(ctx_.cfg.timer_start, nullptr);
    EXPECT_EQ(udisplay_nimble_start(), UDISPLAY_NIMBLE_ESTATE);
}

TEST_F(NimbleBackendTest, InitRegistersUDisplayService)
{
    ASSERT_EQ(udisplay_nimble_init(&ctx_, &ble_), UDISPLAY_NIMBLE_OK);
    EXPECT_EQ(g_fake.port_init_calls, 1);
    EXPECT_EQ(g_fake.device_name, "Demo05");

    ASSERT_NE(g_fake.svcs, nullptr);
    const ble_gatt_svc_def& svc = g_fake.svcs[0];
    EXPECT_EQ(svc.type, BLE_GATT_SVC_TYPE_PRIMARY);
    EXPECT_EQ(g_fake.svcs[1].type, 0) << "service table must be terminated";

    /* UUIDs 29825AAA-D882-46F7-A4D6-EA8431AD34{55,56,57}, little-endian. */
    auto uuid_low = [](const ble_uuid_t* u) {
        return reinterpret_cast<const ble_uuid128_t*>(u)->value[0];
    };
    auto uuid_high = [](const ble_uuid_t* u) {
        return reinterpret_cast<const ble_uuid128_t*>(u)->value[15];
    };
    EXPECT_EQ(uuid_low(svc.uuid), 0x55);
    EXPECT_EQ(uuid_high(svc.uuid), 0x29);

    const ble_gatt_chr_def& ctrl = svc.characteristics[0];
    const ble_gatt_chr_def& data = svc.characteristics[1];
    EXPECT_EQ(uuid_low(ctrl.uuid), 0x56);
    EXPECT_EQ(ctrl.flags, BLE_GATT_CHR_F_WRITE);   /* write with response only */
    EXPECT_EQ(uuid_low(data.uuid), 0x57);
    EXPECT_EQ(data.flags, BLE_GATT_CHR_F_INDICATE);
    EXPECT_EQ(svc.characteristics[2].uuid, nullptr);
}

TEST_F(NimbleBackendTest, InitInstallsTimerAndCommsErrorHook)
{
    ASSERT_EQ(udisplay_nimble_init(&ctx_, &ble_), UDISPLAY_NIMBLE_OK);
    EXPECT_NE(ctx_.cfg.timer_start, nullptr);
    EXPECT_NE(ctx_.cfg.timer_stop, nullptr);
    EXPECT_NE(ctx_.cfg.on_comms_error, app_comms_error);
    EXPECT_NE(ctx_.cfg.on_comms_error, nullptr);
}

TEST_F(NimbleBackendTest, InitAndStartAreOneShot)
{
    EXPECT_EQ(udisplay_nimble_start(), UDISPLAY_NIMBLE_ESTATE);
    ASSERT_EQ(udisplay_nimble_init(&ctx_, &ble_), UDISPLAY_NIMBLE_OK);
    EXPECT_EQ(udisplay_nimble_init(&ctx_, &ble_), UDISPLAY_NIMBLE_ESTATE);
    EXPECT_EQ(udisplay_nimble_start(), UDISPLAY_NIMBLE_OK);
    EXPECT_EQ(udisplay_nimble_start(), UDISPLAY_NIMBLE_ESTATE);
    EXPECT_EQ(g_fake.port_init_calls, 1);
    EXPECT_EQ(g_fake.port_start_calls, 1);
}

TEST_F(NimbleBackendTest, StartArmsHeartbeatAtDefaultPeriod)
{
    ASSERT_EQ(udisplay_nimble_init(&ctx_, &ble_), UDISPLAY_NIMBLE_OK);
    EXPECT_FALSE(udisplay_nimble_test_heartbeat()->armed);
    ASSERT_EQ(udisplay_nimble_start(), UDISPLAY_NIMBLE_OK);
    EXPECT_TRUE(udisplay_nimble_test_heartbeat()->armed);
    EXPECT_EQ(udisplay_nimble_test_heartbeat()->ticks, UDISPLAY_NIMBLE_HEARTBEAT_MS_DEFAULT);
}

TEST_F(NimbleBackendTest, HeartbeatPeriodConfigurableAndRearms)
{
    ble_.heartbeat_interval_ms = 1234;
    bring_up();
    ble_npl_callout* hb = udisplay_nimble_test_heartbeat();
    EXPECT_EQ(hb->ticks, 1234u);
    fake_fire(hb);
    EXPECT_TRUE(hb->armed);
    EXPECT_EQ(hb->ticks, 1234u);
}

/* ── advertising ───────────────────────────────────────────────────────── */

TEST_F(NimbleBackendTest, ShortNameAdvertisedWithServiceUuid)
{
    bring_up();
    EXPECT_EQ(g_fake.adv_name, "Demo05");
    EXPECT_TRUE(g_fake.adv.name_is_complete);
    ASSERT_EQ(g_fake.adv.num_uuids128, 1);
    EXPECT_EQ(g_fake.adv.uuids128->value[0], 0x55);
    EXPECT_FALSE(g_fake.rsp_set);
}

TEST_F(NimbleBackendTest, LongNameMovesToScanResponse)
{
    ble_.device_name = "Greenhouse controller";
    bring_up();
    EXPECT_EQ(g_fake.adv_name, "");
    EXPECT_EQ(g_fake.adv.num_uuids128, 1) << "UUID stays in the advertising data";
    ASSERT_TRUE(g_fake.rsp_set);
    EXPECT_EQ(g_fake.rsp_name, "Greenhouse controller");
    EXPECT_TRUE(g_fake.rsp_name_complete);
}

TEST_F(NimbleBackendTest, OversizedNameTruncatedInScanResponse)
{
    std::string name(40, 'x');
    ble_.device_name = name.c_str();
    bring_up();
    EXPECT_EQ(g_fake.rsp_name.size(), static_cast<size_t>(BLE_HS_ADV_MAX_FIELD_SZ));
    EXPECT_FALSE(g_fake.rsp_name_complete);
}

TEST_F(NimbleBackendTest, AdvertisingRestartsAfterFailedConnectAndAdvComplete)
{
    bring_up();
    connect(/*status=*/0x3E);
    EXPECT_EQ(g_fake.adv_starts, 2);
    EXPECT_EQ(g_app_connects, 0);
    EXPECT_FALSE(udisplay_nimble_is_connected());

    ble_gap_event ev{};
    ev.type = BLE_GAP_EVENT_ADV_COMPLETE;
    gap(ev);
    EXPECT_EQ(g_fake.adv_starts, 3);
}

/* ── connection lifecycle ──────────────────────────────────────────────── */

TEST_F(NimbleBackendTest, ConnectAloneSendsNothing)
{
    bring_up();
    connect();
    EXPECT_TRUE(udisplay_nimble_is_connected());
    EXPECT_EQ(g_app_connects, 1);
    EXPECT_TRUE(g_fake.indications.empty())
        << "indications before the CCCD write are dropped by the stack";
    EXPECT_EQ(ctx_.connected, 0);
}

TEST_F(NimbleBackendTest, SubscribeStartsSessionWithHello)
{
    bring_up();
    connect();
    subscribe();
    EXPECT_EQ(ctx_.connected, 1);
    ASSERT_EQ(g_fake.indications.size(), 1u);
    const auto& hello = g_fake.indications[0];
    ASSERT_EQ(hello.size(), 7u);
    EXPECT_EQ(hello[5], UDISPLAY_BLE_FLAGS_CONTROL);
    EXPECT_EQ(hello[6], UDISPLAY_BLE_CTRL_HELLO);
    EXPECT_EQ(g_fake.indicate_conn_handles[0], kConn);
    EXPECT_TRUE(udisplay_nimble_test_ud_timer()->armed);
    EXPECT_EQ(udisplay_nimble_test_ud_timer()->ticks, UDISPLAY_BLE_HELLO_INTERVAL_MS);
}

TEST_F(NimbleBackendTest, SubscribeIgnoredForOtherAttrsUnsubscribeAndRepeats)
{
    bring_up();
    connect();
    subscribe(/*attr=*/99);
    subscribe(kData, /*indicate=*/false);
    EXPECT_EQ(ctx_.connected, 0);
    EXPECT_TRUE(g_fake.indications.empty());

    subscribe();
    confirm();
    ASSERT_EQ(write_ctrl(kHelloEcho), 0);
    ASSERT_EQ(ctx_.ble_ready, 1);
    subscribe();   /* e.g. resubscribe after MTU renegotiation */
    EXPECT_EQ(ctx_.ble_ready, 1) << "a second on_connect would restart the bootstrap";
}

TEST_F(NimbleBackendTest, HelloRetryTimerResendsHello)
{
    bring_up();
    connect();
    subscribe();
    confirm();
    fake_fire(udisplay_nimble_test_ud_timer());
    ASSERT_EQ(g_fake.indications.size(), 2u);
    EXPECT_EQ(g_fake.indications[1][6], UDISPLAY_BLE_CTRL_HELLO);
}

TEST_F(NimbleBackendTest, HelloEchoTriggersFragmentedHandshake)
{
    bring_up();
    connect();
    subscribe();
    confirm();
    g_fake.indications.clear();
    ASSERT_EQ(write_ctrl(kHelloEcho), 0);
    EXPECT_FALSE(udisplay_nimble_test_ud_timer()->armed);
    drain();
    /* 39-byte HANDSHAKE at the default 20-byte payload: 3 fragments. */
    EXPECT_EQ(g_fake.indications.size(), 3u);
}

TEST_F(NimbleBackendTest, OneIndicationInFlightUntilConfirmed)
{
    bring_up();
    connect();
    subscribe();
    confirm();
    g_fake.indications.clear();
    ASSERT_EQ(write_ctrl(kHelloEcho), 0);
    EXPECT_EQ(g_fake.indications.size(), 1u);
    notify_tx(0);   /* "sent, awaiting confirmation" — not a release */
    EXPECT_EQ(g_fake.indications.size(), 1u);
    notify_tx(BLE_HS_EDONE, /*attr=*/99);   /* other characteristic */
    EXPECT_EQ(g_fake.indications.size(), 1u);
    confirm();
    EXPECT_EQ(g_fake.indications.size(), 2u);
    confirm();
    EXPECT_EQ(g_fake.indications.size(), 3u);
    confirm();
    EXPECT_EQ(g_fake.indications.size(), 3u);
    EXPECT_TRUE(g_fake.terminated.empty());
}

TEST_F(NimbleBackendTest, FragmentsKeepOrderAcrossMessages)
{
    session_ready();
    uint8_t ready = 0x02;   /* CLIENT_READY */
    udisplay_on_message(&ctx_, &ready, 1);
    udisplay_send_string(&ctx_, 0x10, "first message, long enough to fragment", 38);
    udisplay_send_string(&ctx_, 0x11, "second", 6);
    drain();
    ASSERT_GE(g_fake.indications.size(), 4u);
    /* Reassemble: each first fragment has offset 0; packet ids increase. */
    std::vector<uint8_t> first_ids;
    for (const auto& f : g_fake.indications) {
        if (f[0] == 0 && f[1] == 0) first_ids.push_back(f[2]);
    }
    ASSERT_EQ(first_ids.size(), 2u);
    EXPECT_EQ(static_cast<uint8_t>(first_ids[1] - first_ids[0]), 1u);
    EXPECT_TRUE(g_fake.terminated.empty());
}

TEST_F(NimbleBackendTest, MtuEventUpdatesFragmentSize)
{
    bring_up();
    connect();
    mtu(247);
    EXPECT_EQ(ctx_.ble_mtu_payload, 244u);
    mtu(3);       /* nonsense: keep the previous value */
    EXPECT_EQ(ctx_.ble_mtu_payload, 244u);
    mtu(600);     /* beyond the library's fragment buffer */
    EXPECT_EQ(ctx_.ble_mtu_payload, 244u);

    subscribe();
    confirm();
    g_fake.indications.clear();
    ASSERT_EQ(write_ctrl(kHelloEcho), 0);
    drain();
    EXPECT_EQ(g_fake.indications.size(), 1u) << "HANDSHAKE fits one fragment at MTU 247";
}

TEST_F(NimbleBackendTest, ReconnectStartsAtDefaultMtu)
{
    bring_up();
    connect();
    mtu(247);
    ASSERT_EQ(ctx_.ble_mtu_payload, 244u);
    disconnect();

    /* A new link runs at ATT_MTU 23 until its own exchange: fragments sized
     * for the old link would not fit. */
    connect();
    EXPECT_EQ(ctx_.ble_mtu_payload, UDISPLAY_BLE_MTU_PAYLOAD_DEFAULT);
    subscribe();
    confirm();
    g_fake.indications.clear();
    ASSERT_EQ(write_ctrl(kHelloEcho), 0);
    drain();
    EXPECT_EQ(g_fake.indications.size(), 3u);
    for (const auto& f : g_fake.indications) EXPECT_LE(f.size(), 20u);
}

TEST_F(NimbleBackendTest, DisconnectResetsAndReadvertises)
{
    bring_up();
    connect();
    subscribe();   /* HELLO in flight, timer armed */
    ASSERT_EQ(g_fake.indications.size(), 1u);
    udisplay_nimble_send(kHelloEcho.data(), 7, nullptr);   /* queued behind it */

    disconnect(0x13);
    EXPECT_FALSE(udisplay_nimble_is_connected());
    EXPECT_EQ(ctx_.connected, 0);
    EXPECT_FALSE(udisplay_nimble_test_ud_timer()->armed);
    EXPECT_EQ(g_app_disconnects, 1);
    EXPECT_EQ(g_last_disconnect_reason, 0x13);
    EXPECT_EQ(g_lock_depth_in_disconnect, 1) << "app callback runs under the backend lock";
    EXPECT_EQ(g_fake.adv_starts, 2);

    /* The queue was dropped: a reconnect starts clean. */
    connect();
    subscribe();
    ASSERT_EQ(g_fake.indications.size(), 2u);
    EXPECT_EQ(g_fake.indications[1][6], UDISPLAY_BLE_CTRL_HELLO);
    confirm();
    EXPECT_EQ(g_fake.indications.size(), 2u);
}

TEST_F(NimbleBackendTest, SendWhileDisconnectedIsDropped)
{
    bring_up();
    udisplay_nimble_send(kHelloEcho.data(), 7, nullptr);
    EXPECT_TRUE(g_fake.indications.empty());
    EXPECT_TRUE(g_fake.terminated.empty());
}

TEST_F(NimbleBackendTest, SendBeforeInitIsIgnored)
{
    udisplay_nimble_send(kHelloEcho.data(), 7, nullptr);
    udisplay_nimble_lock();
    udisplay_nimble_unlock();
    udisplay_nimble_disconnect();
    EXPECT_FALSE(udisplay_nimble_is_connected());
    EXPECT_TRUE(g_fake.indications.empty());
}

/* ── link errors ───────────────────────────────────────────────────────── */

TEST_F(NimbleBackendTest, IndicationTimeoutDropsLink)
{
    bring_up();
    connect();
    subscribe();
    notify_tx(BLE_HS_ETIMEOUT);
    ASSERT_EQ(g_fake.terminated.size(), 1u);
    EXPECT_EQ(g_fake.terminated[0], kConn);
}

TEST_F(NimbleBackendTest, IndicateErrorDropsLink)
{
    bring_up();
    connect();
    g_fake.indicate_rc = BLE_HS_ENOMEM;
    subscribe();
    EXPECT_EQ(g_fake.terminated.size(), 1u);
}

TEST_F(NimbleBackendTest, MbufExhaustionDropsLink)
{
    bring_up();
    connect();
    g_fake.mbuf_alloc_fails = true;
    subscribe();
    EXPECT_EQ(g_fake.terminated.size(), 1u);
}

TEST_F(NimbleBackendTest, QueueOverflowDropsLink)
{
    bring_up();
    connect();
    std::vector<uint8_t> frag(20, 0xAB);
    /* First goes in flight; the ring then holds 2048 / 22 = 93 more. */
    for (int i = 0; i < 94; i++) udisplay_nimble_send(frag.data(), 20, nullptr);
    EXPECT_TRUE(g_fake.terminated.empty());
    udisplay_nimble_send(frag.data(), 20, nullptr);
    EXPECT_EQ(g_fake.terminated.size(), 1u);
}

TEST_F(NimbleBackendTest, FragmentSizeOutOfRangeDropsLink)
{
    bring_up();
    connect();
    std::vector<uint8_t> big(518, 0);
    udisplay_nimble_send(big.data(), 518, nullptr);
    EXPECT_EQ(g_fake.terminated.size(), 1u);
    udisplay_nimble_send(big.data(), 0, nullptr);
    EXPECT_EQ(g_fake.terminated.size(), 2u);
    EXPECT_TRUE(g_fake.indications.empty());
}

TEST_F(NimbleBackendTest, FramingErrorOnControlWriteDropsLink)
{
    bring_up();
    connect();
    subscribe();
    std::vector<uint8_t> bad = { 0x00, 0x00, 0x00, 0x01, 0x00, 0x7F, 0x00 };   /* bad flags */
    EXPECT_EQ(write_ctrl(bad), 0) << "the write itself is acknowledged";
    EXPECT_EQ(g_fake.terminated.size(), 1u);
}

TEST_F(NimbleBackendTest, OversizedControlWriteRejected)
{
    bring_up();
    connect();
    std::vector<uint8_t> big(513, 0);
    EXPECT_EQ(write_ctrl(big), BLE_ATT_ERR_INSUFFICIENT_RES);
}

TEST_F(NimbleBackendTest, DataCharacteristicReadIsHarmless)
{
    bring_up();
    os_mbuf om{};
    ble_gatt_access_ctxt ctxt{BLE_GATT_ACCESS_OP_READ_CHR, &om};
    EXPECT_EQ(data_chr().access_cb(kConn, kData, &ctxt, nullptr), 0);
}

TEST_F(NimbleBackendTest, ClientThatNeverSubscribesIsDropped)
{
    bring_up();
    connect();
    ble_npl_callout* hb = udisplay_nimble_test_heartbeat();
    fake_fire(hb);
    fake_fire(hb);
    EXPECT_TRUE(g_fake.terminated.empty());
    fake_fire(hb);
    EXPECT_EQ(g_fake.terminated.size(), 1u);
}

TEST_F(NimbleBackendTest, CommsErrorRunsAppHandlerThenDropsLink)
{
    bring_up();
    connect();
    subscribe();
    drain();
    ble_npl_callout* hb = udisplay_nimble_test_heartbeat();
    for (unsigned i = 0; i < UDISPLAY_HB_MISS_MAX; i++) fake_fire(hb);   /* HELLO never echoed */
    EXPECT_EQ(g_app_comms_errors, 1);
    ASSERT_EQ(g_fake.terminated.size(), 1u);
    EXPECT_EQ(g_fake.terminated[0], kConn);
}

TEST_F(NimbleBackendTest, CommsErrorWithoutAppHandler)
{
    ctx_.cfg.on_comms_error = nullptr;
    bring_up();
    connect();
    subscribe();
    drain();
    for (unsigned i = 0; i < UDISPLAY_HB_MISS_MAX; i++) fake_fire(udisplay_nimble_test_heartbeat());
    EXPECT_EQ(g_fake.terminated.size(), 1u);
}

TEST_F(NimbleBackendTest, HeartbeatSentOnceActive)
{
    session_ready();
    uint8_t ready = 0x02;   /* CLIENT_READY */
    udisplay_on_message(&ctx_, &ready, 1);
    fake_fire(udisplay_nimble_test_heartbeat());
    EXPECT_EQ(g_fake.indications.size(), 1u);
    EXPECT_TRUE(g_fake.terminated.empty());
}

/* ── app-facing helpers ────────────────────────────────────────────────── */

TEST_F(NimbleBackendTest, DisconnectApiTerminatesOnlyWhenConnected)
{
    bring_up();
    udisplay_nimble_disconnect();
    EXPECT_TRUE(g_fake.terminated.empty());
    connect();
    udisplay_nimble_disconnect();
    ASSERT_EQ(g_fake.terminated.size(), 1u);
    EXPECT_EQ(g_fake.terminated[0], kConn);
}

TEST_F(NimbleBackendTest, LockIsRecursiveAndBalanced)
{
    bring_up();
    udisplay_nimble_lock();
    udisplay_nimble_lock();
    EXPECT_EQ(fake_lock_depth(), 2);
    udisplay_nimble_unlock();
    udisplay_nimble_unlock();
    EXPECT_EQ(fake_lock_depth(), 0);
}
