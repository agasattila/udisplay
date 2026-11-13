// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 Attila Agas

/*
 * NimBLE transport backend — the generic part. Uses only the NimBLE host API
 * and its NPL porting layer; everything platform-specific (bringing the
 * stack up, running the host task) goes through udisplay_nimble_port.h.
 *
 * Outbound path. The data characteristic uses INDICATE: the peer's ATT layer
 * confirms each one, and ATT allows one unconfirmed indication per
 * connection. NimBLE doesn't enforce that (a second indicate overwrites the
 * pending one's bookkeeping), so this file is the only guard, and firmware
 * must not send indications of its own on the same connection. The library
 * calls udisplay_nimble_send() once per fragment, possibly many times in a
 * row, so it can neither block nor drop. Fragments go into a ring; tx_pump()
 * sends one and BLE_GAP_EVENT_NOTIFY_TX (confirmation) sends the next.
 *
 *   libudisplay ──send──► ring ──tx_pump──► indicate ──► peer
 *                           ▲                              │
 *                           └── NOTIFY_TX (EDONE) ◄────────┘
 *
 * Anything that would lose or reorder a fragment (ring full, mbuf exhausted,
 * indicate error, confirmation timeout) is a link error: disconnect. A link
 * error names the connection it belongs to and is dropped if that
 * connection has since been replaced, so it can't kill a newer one.
 *
 * Advertising restarts only while the host is synced with the controller;
 * during a host reset (which reports open connections as disconnected)
 * on_sync() restarts it instead.
 *
 * The protocol core can end a session on its own (auth_check() returning
 * -1, a malformed auth ACK): it clears ctx->connected but has no way to
 * close the transport. The backend checks for that after every control
 * write and on every heartbeat tick and terminates the link, so a rejected
 * client can't hold the only connection slot.
 *
 * Locking. s.lock (recursive) guards the link state (conn_handle included,
 * also in the public helpers), the ring, the in-flight flag and every call
 * into the udisplay_t instance. tx_pump()
 * holds it across ble_gatts_indicate_custom() too, so the fragment buffer
 * has one user and a fragment can't go out on a connection that replaced
 * the one it was queued for. That is deadlock-free because NimBLE never
 * calls the GAP/GATT callbacks (which take s.lock) with its own host lock
 * held; the only callback indicate runs synchronously is the "sent"
 * NOTIFY_TX, in the same task, and the lock is recursive.
 */

#include "libudisplay/udisplay_nimble.h"
#include "udisplay_nimble_port.h"
#include "tx_ring.h"

#include <string.h>

#include "host/ble_hs.h"
#include "host/util/util.h"
#include "services/gap/ble_svc_gap.h"
#include "services/gatt/ble_svc_gatt.h"
#include "modlog/modlog.h"

/* Legacy advertising only: with extended advertising enabled, NimBLE's
 * ble_gap_adv_* calls return BLE_HS_ENOTSUP and the device would never
 * advertise. */
#ifdef MYNEWT_VAL_BLE_EXT_ADV
#if MYNEWT_VAL_BLE_EXT_ADV
#error "udisplay NimBLE backend: disable extended advertising (CONFIG_BT_NIMBLE_EXT_ADV); it uses legacy advertising"
#endif
#endif

/* Longest GAP device name the host accepts. */
#ifdef MYNEWT_VAL_BLE_SVC_GAP_DEVICE_NAME_MAX_LENGTH
#define DEVICE_NAME_MAX MYNEWT_VAL_BLE_SVC_GAP_DEVICE_NAME_MAX_LENGTH
#else
#define DEVICE_NAME_MAX 31
#endif

#ifndef UDISPLAY_NIMBLE_TX_QUEUE_BYTES
/* 1 KB message at MTU 23: ~60 fragments, ~1.3 KB with length prefixes. */
#define UDISPLAY_NIMBLE_TX_QUEUE_BYTES 2048u
#endif

/* Largest control-characteristic write: the ATT maximum attribute length. */
#define RX_WRITE_MAX 512u

/* The library's own fragment buffer bounds every fragment it sends. */
#define TX_FRAG_MAX ((uint16_t)sizeof(((udisplay_t*)0)->tx_buf.ble_frag))

/* Advertising data is 31 bytes: flags (3) + 128-bit UUID (18) + name header
 * (2) leave 8 bytes for the name. */
#define ADV_NAME_MAX 8u

/* uDisplay's own session starts only once the client subscribes to
 * indications, so the library's bootstrap watchdog can't see the window
 * between link-level connect and subscribe. This covers it, counted in
 * heartbeat ticks, at the library's threshold. */
#define SUBSCRIBE_WAIT_TICKS_MAX UDISPLAY_HB_MISS_MAX

/* Advertising retry after a recoverable failure: doubles from the first
 * delay up to the cap, and keeps going at the cap. Bounding the count
 * instead would bring back the state the retry exists to prevent: a device
 * nobody can find until it reboots. */
#define ADV_RETRY_FIRST_MS 250u
#define ADV_RETRY_MAX_MS   8000u

/* ── UUIDs (128-bit, little-endian byte order) ────────────────────────────
 * Service: 29825AAA-D882-46F7-A4D6-EA8431AD3455
 * Control: 29825AAA-D882-46F7-A4D6-EA8431AD3456  (WRITE from client)
 * Data:    29825AAA-D882-46F7-A4D6-EA8431AD3457  (INDICATE to client)
 */
static const ble_uuid128_t s_svc_uuid = BLE_UUID128_INIT(
    0x55, 0x34, 0xAD, 0x31, 0x84, 0xEA, 0xD6, 0xA4,
    0xF7, 0x46, 0x82, 0xD8, 0xAA, 0x5A, 0x82, 0x29
);
static const ble_uuid128_t s_ctrl_uuid = BLE_UUID128_INIT(
    0x56, 0x34, 0xAD, 0x31, 0x84, 0xEA, 0xD6, 0xA4,
    0xF7, 0x46, 0x82, 0xD8, 0xAA, 0x5A, 0x82, 0x29
);
static const ble_uuid128_t s_data_uuid = BLE_UUID128_INIT(
    0x57, 0x34, 0xAD, 0x31, 0x84, 0xEA, 0xD6, 0xA4,
    0xF7, 0x46, 0x82, 0xD8, 0xAA, 0x5A, 0x82, 0x29
);

static struct {
    udisplay_t*              ctx;
    udisplay_nimble_config_t cfg;
    udisplay_error_fn        app_comms_error;  /* ctx's on_comms_error before init */
    uint32_t                 heartbeat_ms;
    int                      initialized;
    int                      started;

    struct ble_npl_mutex     lock;
    struct ble_npl_callout   ud_timer;         /* library one-shot (HELLO retry) */
    struct ble_npl_callout   heartbeat;
    struct ble_npl_callout   adv_retry;
    uint32_t                 adv_retry_ms;     /* next retry delay; 0 = no failures in a row */

    uint16_t                 conn_handle;
    uint16_t                 data_attr_handle;
    uint8_t                  own_addr_type;
    int                      session_started;  /* udisplay_on_connect() called */
    uint8_t                  subscribe_wait_ticks;

    tx_ring_t                tx;
    int                      tx_inflight;
} s;

static uint8_t s_tx_storage[UDISPLAY_NIMBLE_TX_QUEUE_BYTES];
/* Used by tx_pump() under s.lock. Static: too big for the host task stack. */
static uint8_t s_tx_frag[TX_FRAG_MAX];
/* Control writes arrive only in the host task. */
static uint8_t s_rx_buf[RX_WRITE_MAX];

static void lock(void)   { ble_npl_mutex_pend(&s.lock, BLE_NPL_TIME_FOREVER); }
static void unlock(void) { ble_npl_mutex_release(&s.lock); }

/* Terminate @p conn_handle if it is still the current connection. Takes
 * s.lock (recursive), so the check and the terminate see the same link. */
static void terminate_conn(uint16_t conn_handle)
{
    lock();
    if (conn_handle != BLE_HS_CONN_HANDLE_NONE && conn_handle == s.conn_handle) {
        ble_gap_terminate(conn_handle, BLE_ERR_REM_USER_CONN_TERM);
    }
    unlock();
}

/* Terminate whatever connection is current. */
static void terminate(void)
{
    lock();
    terminate_conn(s.conn_handle);
    unlock();
}

static void link_error(uint16_t conn_handle, const char* why)
{
    MODLOG_DFLT(ERROR, "udisplay: link error: %s, disconnecting", why);
    terminate_conn(conn_handle);
}

/* The core cleared its session (auth_check() == -1, malformed auth ACK, or
 * the app called udisplay_on_disconnect()) while the link is still up:
 * nothing will talk on it again, so close it. Caller holds s.lock. */
static void drop_link_if_session_ended(void)
{
    if (s.session_started && !s.ctx->connected &&
        s.conn_handle != BLE_HS_CONN_HANDLE_NONE) {
        MODLOG_DFLT(WARN, "udisplay: session ended by the protocol, disconnecting");
        terminate_conn(s.conn_handle);
    }
}

/* ── Outbound queue ─────────────────────────────────────────────────────── */

/* Send the next queued fragment if no indication is unconfirmed. */
static void tx_pump(void)
{
    uint16_t len;
    struct os_mbuf* om;
    int rc;

    lock();
    if (s.tx_inflight || s.conn_handle == BLE_HS_CONN_HANDLE_NONE ||
        tx_ring_empty(&s.tx)) {
        unlock();
        return;
    }
    len = tx_ring_pop(&s.tx, s_tx_frag, sizeof(s_tx_frag));
    if (len == 0u) {
        unlock();
        return;
    }
    s.tx_inflight = 1;

    om = ble_hs_mbuf_from_flat(s_tx_frag, len);
    if (!om) {
        link_error(s.conn_handle, "mbuf alloc failed");
    } else {
        rc = ble_gatts_indicate_custom(s.conn_handle, s.data_attr_handle, om);
        if (rc != 0) {
            MODLOG_DFLT(ERROR, "udisplay: ble_gatts_indicate_custom rc=%d", rc);
            link_error(s.conn_handle, "indicate failed");
        }
    }
    unlock();
}

void udisplay_nimble_send(const uint8_t* data, uint16_t len, void* userdata)
{
    (void)userdata;

    if (!s.initialized) return;
    /* Normally called with s.lock already held (the library runs under it);
     * taken again so the connection checked is the one queued for. */
    lock();
    if (s.conn_handle == BLE_HS_CONN_HANDLE_NONE) {
        /* not connected: dropped */
    } else if (len == 0u || len > TX_FRAG_MAX) {
        link_error(s.conn_handle, "fragment size out of range");
    } else if (!tx_ring_push(&s.tx, data, len)) {
        link_error(s.conn_handle, "TX queue overflow");
    } else {
        tx_pump();
    }
    unlock();
}

/* ── Library timer + heartbeat (both NPL callouts in the host task) ─────────
 *
 * Running them in the host task, the same task that delivers the client's
 * writes (ctrl_access -> udisplay_feed), means a HELLO retry or heartbeat
 * can never interleave with a response the client's write triggers; the
 * lock covers application tasks. */

static void ud_timer_cb(struct ble_npl_event* ev)
{
    (void)ev;
    lock();
    udisplay_timer_expired(s.ctx);
    unlock();
}

static void ud_timer_start(udisplay_t* ctx, uint32_t delay_ms)
{
    (void)ctx;
    ble_npl_callout_reset(&s.ud_timer, ble_npl_time_ms_to_ticks32(delay_ms));
}

static void ud_timer_stop(udisplay_t* ctx)
{
    (void)ctx;
    ble_npl_callout_stop(&s.ud_timer);
}

static void heartbeat_cb(struct ble_npl_event* ev)
{
    (void)ev;
    ble_npl_callout_reset(&s.heartbeat, ble_npl_time_ms_to_ticks32(s.heartbeat_ms));

    lock();
    udisplay_heartbeat(s.ctx);
    if (s.conn_handle != BLE_HS_CONN_HANDLE_NONE && !s.session_started &&
        ++s.subscribe_wait_ticks >= SUBSCRIBE_WAIT_TICKS_MAX) {
        MODLOG_DFLT(WARN, "udisplay: client never subscribed to indications, disconnecting");
        terminate();
    }
    /* Catch-all for a session ended outside a control write (e.g. the app
     * calling udisplay_on_disconnect() itself): the watchdog stops counting
     * once ctx->connected is 0, so nothing else would notice. */
    drop_link_if_session_ended();
    unlock();
}

/* Interposed on the instance's on_comms_error by udisplay_nimble_init(). */
static void comms_error_cb(void* userdata)
{
    MODLOG_DFLT(WARN, "udisplay: comms error (heartbeat/bootstrap timeout), disconnecting");
    if (s.app_comms_error) s.app_comms_error(userdata);
    terminate();
}

/* ── GATT ───────────────────────────────────────────────────────────────── */

static int ctrl_access(uint16_t conn_handle, uint16_t attr_handle,
                       struct ble_gatt_access_ctxt* ctxt, void* arg)
{
    uint16_t len = 0;
    (void)attr_handle; (void)arg;

    if (ctxt->op != BLE_GATT_ACCESS_OP_WRITE_CHR) return 0;
    if (ble_hs_mbuf_to_flat(ctxt->om, s_rx_buf, sizeof(s_rx_buf), &len) != 0) {
        return BLE_ATT_ERR_INSUFFICIENT_RES;
    }

    lock();
    /* Only the current connection feeds the instance, and only once it has
     * subscribed: before that no session exists and a write is just a
     * client skipping the handshake. The core does its own state checks
     * too (it drops anything before the HELLO echo, and everything but the
     * auth answer while auth is pending); this keeps pre-session bytes out
     * of its reassembly buffer altogether. */
    if (conn_handle == s.conn_handle && s.session_started) {
        if (udisplay_feed(s.ctx, s_rx_buf, len) != 0) {
            link_error(conn_handle, "framing error on control write");
        } else {
            drop_link_if_session_ended();
        }
    }
    unlock();
    return 0;
}

static int data_access(uint16_t conn_handle, uint16_t attr_handle,
                       struct ble_gatt_access_ctxt* ctxt, void* arg)
{
    (void)conn_handle; (void)attr_handle; (void)ctxt; (void)arg;
    return 0;
}

static const struct ble_gatt_chr_def s_chrs[] = {
    {
        .uuid       = &s_ctrl_uuid.u,
        .access_cb  = ctrl_access,
        .flags      = BLE_GATT_CHR_F_WRITE,
    },
    {
        .uuid       = &s_data_uuid.u,
        .access_cb  = data_access,
        .flags      = BLE_GATT_CHR_F_INDICATE,
        .val_handle = &s.data_attr_handle,
    },
    { 0 }
};

static const struct ble_gatt_svc_def s_svcs[] = {
    {
        .type            = BLE_GATT_SVC_TYPE_PRIMARY,
        .uuid            = &s_svc_uuid.u,
        .characteristics = s_chrs,
    },
    { 0 }
};

/* ── GAP ────────────────────────────────────────────────────────────────── */

static int gap_event_cb(struct ble_gap_event* ev, void* arg);

static void start_adv(void);

/* Failures that a later attempt can't fix: bad parameters or data (a
 * configuration bug), unsupported (extended advertising, caught at build
 * time), host stopped. Not synced: on_sync() starts advertising itself. */
static int adv_error_is_permanent(int rc)
{
    return rc == BLE_HS_EINVAL || rc == BLE_HS_EMSGSIZE || rc == BLE_HS_ENOTSUP ||
           rc == BLE_HS_EDISABLED || rc == BLE_HS_ENOTSYNCED;
}

static void adv_retry_cb(struct ble_npl_event* ev)
{
    int connected;
    (void)ev;
    lock();
    connected = s.conn_handle != BLE_HS_CONN_HANDLE_NONE;
    unlock();
    if (!connected) start_adv();
}

/* An advertising call returned @p rc: decide what happens next. */
static void adv_failed(const char* what, int rc)
{
    if (adv_error_is_permanent(rc)) {
        MODLOG_DFLT(ERROR, "udisplay: %s rc=%d, not retrying", what, rc);
        s.adv_retry_ms = 0;
        return;
    }
    if (s.adv_retry_ms == 0u) s.adv_retry_ms = ADV_RETRY_FIRST_MS;
    MODLOG_DFLT(WARN, "udisplay: %s rc=%d, retrying in %u ms",
                what, rc, (unsigned)s.adv_retry_ms);
    ble_npl_callout_reset(&s.adv_retry, ble_npl_time_ms_to_ticks32(s.adv_retry_ms));
    s.adv_retry_ms = s.adv_retry_ms >= ADV_RETRY_MAX_MS / 2u ? ADV_RETRY_MAX_MS
                                                            : s.adv_retry_ms * 2u;
}

/* Runs only in the host task (GAP events, on_sync, the retry callout). */
static void start_adv(void)
{
    struct ble_hs_adv_fields f;
    struct ble_gap_adv_params ap;
    size_t name_len = strlen(s.cfg.device_name);
    int name_in_adv = name_len <= ADV_NAME_MAX;
    int rc;

    ble_npl_callout_stop(&s.adv_retry);

    /* A host reset clears the sync state first and then reports every open
     * connection as broken, so BLE_GAP_EVENT_DISCONNECT lands here while the
     * controller is being reset. Any HCI command now (set_fields is the
     * first) is unsafe; on_sync() restarts advertising once the host is
     * back. Covers every caller: disconnect, failed connect, ADV_COMPLETE
     * and the retry callout. */
    if (!ble_hs_synced()) {
        MODLOG_DFLT(INFO, "udisplay: host not synced, advertising waits for sync");
        s.adv_retry_ms = 0;
        return;
    }

    memset(&f, 0, sizeof(f));
    f.flags                = BLE_HS_ADV_F_DISC_GEN | BLE_HS_ADV_F_BREDR_UNSUP;
    f.uuids128             = &s_svc_uuid;
    f.num_uuids128         = 1;
    f.uuids128_is_complete = 1;
    if (name_in_adv) {
        f.name             = (const uint8_t*)s.cfg.device_name;
        f.name_len         = (uint8_t)name_len;
        f.name_is_complete = 1;
    }
    rc = ble_gap_adv_set_fields(&f);
    if (rc != 0) {
        adv_failed("ble_gap_adv_set_fields", rc);
        return;
    }

    if (!name_in_adv) {
        /* The scanner filters on the service UUID, so the UUID keeps its
         * place in the advertising data and the name moves out. */
        memset(&f, 0, sizeof(f));
        f.name             = (const uint8_t*)s.cfg.device_name;
        f.name_len         = (uint8_t)(name_len > BLE_HS_ADV_MAX_FIELD_SZ
                                       ? BLE_HS_ADV_MAX_FIELD_SZ : name_len);
        f.name_is_complete = name_len <= BLE_HS_ADV_MAX_FIELD_SZ;
        rc = ble_gap_adv_rsp_set_fields(&f);
        if (rc != 0) {
            adv_failed("ble_gap_adv_rsp_set_fields", rc);
            return;
        }
    }

    memset(&ap, 0, sizeof(ap));
    ap.conn_mode = BLE_GAP_CONN_MODE_UND;
    ap.disc_mode = BLE_GAP_DISC_MODE_GEN;
    rc = ble_gap_adv_start(s.own_addr_type, NULL, BLE_HS_FOREVER, &ap,
                           gap_event_cb, NULL);
    if (rc == BLE_HS_EALREADY) {
        /* Already advertising (e.g. a retry racing a GAP-event restart). */
        s.adv_retry_ms = 0;
    } else if (rc != 0) {
        adv_failed("ble_gap_adv_start", rc);
    } else {
        s.adv_retry_ms = 0;
        MODLOG_DFLT(INFO, "udisplay: advertising as \"%s\"", s.cfg.device_name);
    }
}

/* Caller holds s.lock. */
static void reset_link_state(uint16_t conn_handle)
{
    s.conn_handle          = conn_handle;
    tx_ring_reset(&s.tx);
    s.tx_inflight          = 0;
    s.session_started      = 0;
    s.subscribe_wait_ticks = 0;
}

static int gap_event_cb(struct ble_gap_event* ev, void* arg)
{
    (void)arg;
    switch (ev->type) {
    case BLE_GAP_EVENT_CONNECT:
        if (ev->connect.status != 0) {
            MODLOG_DFLT(WARN, "udisplay: connection failed status=%d", ev->connect.status);
            start_adv();
            break;
        }
        MODLOG_DFLT(INFO, "udisplay: connected handle=%d", ev->connect.conn_handle);
        ble_npl_callout_stop(&s.adv_retry);
        s.adv_retry_ms = 0;
        lock();
        reset_link_state(ev->connect.conn_handle);
        /* Every connection starts at the default ATT MTU. The library
         * keeps the last value it was given, which belongs to the previous
         * connection; BLE_GAP_EVENT_MTU raises it again. */
        udisplay_ble_set_mtu(s.ctx, UDISPLAY_BLE_MTU_PAYLOAD_DEFAULT);
        /* No session yet: an indication sent before the peer writes the
         * CCCD is silently dropped. It starts on BLE_GAP_EVENT_SUBSCRIBE. */
        if (s.cfg.on_connect) s.cfg.on_connect(s.cfg.userdata);
        unlock();
        break;

    case BLE_GAP_EVENT_DISCONNECT:
        MODLOG_DFLT(INFO, "udisplay: disconnected reason=%d", ev->disconnect.reason);
        lock();
        reset_link_state(BLE_HS_CONN_HANDLE_NONE);
        udisplay_on_disconnect(s.ctx);
        if (s.cfg.on_disconnect) s.cfg.on_disconnect(ev->disconnect.reason, s.cfg.userdata);
        unlock();
        start_adv();
        break;

    case BLE_GAP_EVENT_ADV_COMPLETE: {
        int connected;
        lock();
        connected = s.conn_handle != BLE_HS_CONN_HANDLE_NONE;
        unlock();
        if (!connected) start_adv();
        break;
    }

    case BLE_GAP_EVENT_SUBSCRIBE:
        /* Fires on every CCCD write, including unsubscribe and, on some
         * stacks, a resubscribe mid-connection (e.g. after MTU
         * renegotiation). Gate on indicate-enable of the data characteristic
         * and start the session once per connection: udisplay_on_connect()
         * resets fragment/reassembly state, so a second call would corrupt
         * an in-progress bootstrap. It sends HELLO first; HANDSHAKE follows
         * once the client echoes it. */
        lock();
        if (ev->subscribe.attr_handle == s.data_attr_handle &&
            ev->subscribe.cur_indicate && !s.session_started) {
            s.session_started = 1;
            MODLOG_DFLT(INFO, "udisplay: client subscribed, sending HELLO");
            udisplay_on_connect(s.ctx);
        }
        unlock();
        break;

    case BLE_GAP_EVENT_NOTIFY_TX:
        if (ev->notify_tx.attr_handle != s.data_attr_handle) break;
        lock();
        /* A link that just broke reports its in-flight indication as failed
         * before the DISCONNECT event: nothing left to tear down. */
        if (ev->notify_tx.conn_handle != s.conn_handle ||
            ev->notify_tx.status == BLE_HS_ENOTCONN) {
            /* not ours, or already gone */
        } else if (ev->notify_tx.status == BLE_HS_EDONE) {
            /* Peer confirmed the indication: send the next fragment. */
            s.tx_inflight = 0;
            tx_pump();
        } else if (ev->notify_tx.status != 0) {
            /* BLE_HS_ETIMEOUT (no confirmation within the ATT timeout) or a
             * send error. Status 0 only means "sent, awaiting confirmation". */
            MODLOG_DFLT(ERROR, "udisplay: indication status=%d", ev->notify_tx.status);
            link_error(ev->notify_tx.conn_handle, "indication not confirmed");
        }
        unlock();
        break;

    case BLE_GAP_EVENT_MTU: {
        int ok;
        MODLOG_DFLT(INFO, "udisplay: MTU %d", ev->mtu.value);
        lock();
        /* NimBLE allows ATT_MTU up to 527; the library's fragment buffer
         * holds 517 bytes. Use what fits rather than staying at 20. */
        ok = ev->mtu.value > 3u &&
             udisplay_ble_set_mtu(s.ctx, (uint16_t)(ev->mtu.value - 3u > TX_FRAG_MAX
                                                    ? TX_FRAG_MAX : ev->mtu.value - 3u));
        unlock();
        if (!ok) {
            MODLOG_DFLT(WARN, "udisplay: MTU %d rejected, keeping previous value",
                        ev->mtu.value);
        }
        break;
    }

    default:
        break;
    }
    return 0;
}

static void on_sync(void)
{
    int rc = ble_hs_util_ensure_addr(0);
    if (rc == 0) rc = ble_hs_id_infer_auto(0, &s.own_addr_type);
    if (rc != 0) {
        MODLOG_DFLT(ERROR, "udisplay: no usable BLE address rc=%d", rc);
        return;
    }
    start_adv();
}

static void on_reset(int reason)
{
    MODLOG_DFLT(WARN, "udisplay: NimBLE host reset reason=%d", reason);
    /* on_sync() restarts advertising once the host is back. */
    ble_npl_callout_stop(&s.adv_retry);
    s.adv_retry_ms = 0;
}

/* ── Public API ─────────────────────────────────────────────────────────── */

int udisplay_nimble_init(udisplay_t* ctx, const udisplay_nimble_config_t* cfg)
{
    int rc;

    if (s.initialized) return UDISPLAY_NIMBLE_ESTATE;
    if (!ctx || !cfg || !cfg->device_name || cfg->device_name[0] == '\0' ||
        strlen(cfg->device_name) > DEVICE_NAME_MAX) {
        return UDISPLAY_NIMBLE_EINVAL;
    }
    if (ctx->cfg.send != udisplay_nimble_send ||
        ctx->cfg.transport != UDISPLAY_TRANSPORT_BLE) {
        MODLOG_DFLT(ERROR, "udisplay: init the instance with udisplay_nimble_send "
                           "and UDISPLAY_TRANSPORT_BLE first");
        return UDISPLAY_NIMBLE_EINVAL;
    }

    rc = udisplay_nimble_port_init();
    if (rc != 0) return rc;

    s.ctx          = ctx;
    s.cfg          = *cfg;
    s.heartbeat_ms = cfg->heartbeat_interval_ms ? cfg->heartbeat_interval_ms
                                                : UDISPLAY_NIMBLE_HEARTBEAT_MS_DEFAULT;
    s.conn_handle  = BLE_HS_CONN_HANDLE_NONE;
    s.started      = 0;
    tx_ring_init(&s.tx, s_tx_storage, (uint16_t)sizeof(s_tx_storage));
    s.tx_inflight  = 0;
    s.session_started      = 0;
    s.subscribe_wait_ticks = 0;
    s.own_addr_type        = BLE_OWN_ADDR_PUBLIC;

    /* NPL objects only work once the port is up. */
    ble_npl_mutex_init(&s.lock);
    ble_npl_callout_init(&s.ud_timer, udisplay_nimble_port_eventq(), ud_timer_cb, NULL);
    ble_npl_callout_init(&s.heartbeat, udisplay_nimble_port_eventq(), heartbeat_cb, NULL);
    ble_npl_callout_init(&s.adv_retry, udisplay_nimble_port_eventq(), adv_retry_cb, NULL);
    s.adv_retry_ms = 0;

    ble_svc_gap_init();
    ble_svc_gatt_init();
    rc = ble_svc_gap_device_name_set(cfg->device_name);
    if (rc == 0) rc = ble_gatts_count_cfg(s_svcs);
    if (rc == 0) rc = ble_gatts_add_svcs(s_svcs);
    if (rc != 0) {
        MODLOG_DFLT(ERROR, "udisplay: GATT/GAP setup rc=%d", rc);
        return rc;
    }

    /* The instance is only touched once nothing can fail any more. */
    udisplay_set_timer(ctx, ud_timer_start, ud_timer_stop);
    s.app_comms_error       = ctx->cfg.on_comms_error;
    ctx->cfg.on_comms_error = comms_error_cb;

    ble_hs_cfg.sync_cb  = on_sync;
    ble_hs_cfg.reset_cb = on_reset;

    s.initialized = 1;
    return UDISPLAY_NIMBLE_OK;
}

int udisplay_nimble_start(void)
{
    int rc;
    if (!s.initialized || s.started) return UDISPLAY_NIMBLE_ESTATE;
    rc = udisplay_nimble_port_start();
    if (rc != 0) return rc;
    s.started = 1;
    ble_npl_callout_reset(&s.heartbeat, ble_npl_time_ms_to_ticks32(s.heartbeat_ms));
    return UDISPLAY_NIMBLE_OK;
}

void udisplay_nimble_disconnect(void)
{
    if (s.initialized) terminate();
}

int udisplay_nimble_is_connected(void)
{
    int connected;
    if (!s.initialized) return 0;
    lock();
    connected = s.conn_handle != BLE_HS_CONN_HANDLE_NONE;
    unlock();
    return connected;
}

void udisplay_nimble_lock(void)
{
    if (s.initialized) lock();
}

void udisplay_nimble_unlock(void)
{
    if (s.initialized) unlock();
}
