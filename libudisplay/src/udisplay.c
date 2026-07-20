// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 Attila Agas

#include "../include/udisplay.h"
#include "protocol.h"
#include "chunk_server.h"
#include "framing.h"
#include <string.h>

/* ── Library state (single static instance) ─────────────────────────────── */

typedef struct {
    udisplay_config_t cfg;
    chunk_server_t    chunk_srv;
    /* Inbound reassembly: exactly one of {ble_rx, tcp_rx} is ever active for
     * the life of a connection (cfg.transport is fixed at udisplay_init()
     * and never changes), so they share one union rather than two
     * always-allocated arrays. */
    union {
        ble_rx_t ble_rx;
        tcp_rx_t tcp_rx;
    }                 rx;
    int               connected;
    int               active;              /**< 1 after CLIENT_READY received; 0 until then */
    uint8_t           comms_miss_count;    /**< Consecutive comms misses; watches BOOTSTRAP stalls
                                                 (resets on any bootstrap-progress message) and,
                                                 once active, missed HEARTBEAT echoes */
    uint8_t           msg_buf[UDISPLAY_MAX_MSG_SIZE];
    /* Auth state (proto 0x04) */
    uint8_t           auth_salt[32];       /**< Current challenge salt */
    uint8_t           awaiting_auth_ack;   /**< 1 after HANDSHAKE(flags=1) sent */
    uint8_t           pending_disconnect;  /**< Set when auth_check returns -1 */
    /* Transport framing */
    uint16_t          ble_mtu_payload;     /**< BLE ATT data bytes per fragment */
    uint8_t           ble_tx_packet_id;    /**< Per-connection outbound packet counter */
    union {
        uint8_t ble_frag[517];                        /**< BLE outbound: one ATT fragment */
        uint8_t tcp_framed[UDISPLAY_MAX_MSG_SIZE + 2u]; /**< TCP outbound: length + payload */
    } tx_buf;
} udisplay_state_t;

static udisplay_state_t s;

/* ── Internal helpers ────────────────────────────────────────────────────── */

/* Route a complete protocol message through transport framing.
 * always=1: send even before s.active (bootstrap); always=0: require active. */
static void framed_send_raw(const uint8_t* msg, uint16_t len, int always)
{
    if (!s.connected || !s.cfg.send || len == 0) return;
    if (!always && !s.active) return;

    switch (s.cfg.transport) {
        case UDISPLAY_TRANSPORT_BLE: {
            uint16_t mtu = s.ble_mtu_payload;
            if (mtu < 7u) mtu = UDISPLAY_BLE_MTU_PAYLOAD_DEFAULT;
            udisplay_ble_fragment(msg, len, mtu, s.ble_tx_packet_id,
                                   s.tx_buf.ble_frag, (uint16_t)sizeof(s.tx_buf.ble_frag),
                                   s.cfg.send, s.cfg.userdata);
            s.ble_tx_packet_id++;
            break;
        }
        case UDISPLAY_TRANSPORT_TCP: {
            uint16_t n = udisplay_tcp_frame(s.tx_buf.tcp_framed,
                                             (uint16_t)sizeof(s.tx_buf.tcp_framed),
                                             msg, len);
            if (n > 0) {
                s.cfg.send(s.tx_buf.tcp_framed, n, s.cfg.userdata);
            }
            break;
        }
        default: /* TRANSPORT_NONE */
            s.cfg.send(msg, len, s.cfg.userdata);
            break;
    }
}

static void do_send(const uint8_t* msg, uint16_t len)
{
    framed_send_raw(msg, len, 0);
}

static void do_send_always(const uint8_t* msg, uint16_t len)
{
    framed_send_raw(msg, len, 1);
}

static void dispatch_event(const proto_inbound_t* in)
{
    if (!s.cfg.on_event) return;

    udisplay_event_t ev;
    ev.widget_id  = in->event.widget_id;
    ev.event_type = in->event.event_type;

    const uint8_t* payload = in->event.payload;
    uint8_t        plen    = in->event.payload_len;

    switch (ev.event_type) {
        case UDISPLAY_EVENT_SLIDER_CHANGE:
            if (plen >= 4u) {
                uint32_t bits = (uint32_t)payload[0]
                              | ((uint32_t)payload[1] << 8u)
                              | ((uint32_t)payload[2] << 16u)
                              | ((uint32_t)payload[3] << 24u);
                memcpy(&ev.slider_value, &bits, 4);
            } else {
                ev.slider_value = 0.0f;
            }
            break;

        case UDISPLAY_EVENT_TOGGLE_CHANGE:
            ev.toggle_state = (plen >= 1u) ? payload[0] : 0u;
            break;

        case UDISPLAY_EVENT_TEXT_SUBMIT:
            if (plen >= 1u) {
                ev.text.len = payload[0];
                ev.text.str = (plen > 1u) ? (const char*)(payload + 1) : "";
            } else {
                ev.text.len = 0;
                ev.text.str = "";
            }
            break;

        case UDISPLAY_EVENT_SELECTION_CHANGE:
            ev.selection_index = (plen >= 1u) ? payload[0] : 0u;
            break;

        case UDISPLAY_EVENT_BUTTON_CLICK:
        case UDISPLAY_EVENT_BUTTON_PRESS:
        case UDISPLAY_EVENT_BUTTON_RELEASE:
            /* no payload; forwarded as-is */
            break;

        default:
            /* unknown types forwarded as-is */
            break;
    }

    s.cfg.on_event(&ev, s.cfg.userdata);
}

/* ── Public API ──────────────────────────────────────────────────────────── */

/* Reset whichever inbound reassembly state is active for cfg.transport.
 * s.rx is a union (ble_rx and tcp_rx never active simultaneously — see the
 * udisplay_state_t comment), so only the current transport's member is
 * meaningful to reset. */
static void rx_reset(void)
{
    if (s.cfg.transport == UDISPLAY_TRANSPORT_BLE) {
        ble_rx_reset(&s.rx.ble_rx);
    } else if (s.cfg.transport == UDISPLAY_TRANSPORT_TCP) {
        tcp_rx_reset(&s.rx.tcp_rx);
    }
}

void udisplay_init(const udisplay_config_t* cfg)
{
    memcpy(&s.cfg, cfg, sizeof(*cfg));
    chunk_server_init(&s.chunk_srv,
                      cfg->chunks,
                      cfg->chunk_hashes,
                      cfg->chunk_lens,
                      cfg->chunk_count);
    rx_reset();
    s.connected       = 0;
    s.ble_mtu_payload = (cfg->ble_mtu_payload >= 7u)
                        ? cfg->ble_mtu_payload
                        : UDISPLAY_BLE_MTU_PAYLOAD_DEFAULT;
}

static void fill_auth_salt(void)
{
    if (s.cfg.fill_random) {
        s.cfg.fill_random(s.auth_salt, 32u, s.cfg.userdata);
    } else {
        /* Insecure deterministic fallback — firmware MUST provide fill_random */
        static uint8_t ctr = 0u;
        uint8_t i;
        for (i = 0u; i < 32u; i++) {
            s.auth_salt[i] = ++ctr ^ (uint8_t)(i * 7u);
        }
    }
}

void udisplay_on_connect(void)
{
    s.connected          = 1;
    s.active             = 0;
    s.comms_miss_count   = 0;
    s.awaiting_auth_ack  = 0;
    s.pending_disconnect = 0;
    s.ble_tx_packet_id   = 0;
    rx_reset();

    if (s.cfg.auth_algo != UDISPLAY_AUTH_NONE) {
        fill_auth_salt();
        s.awaiting_auth_ack = 1;
        uint16_t n = proto_handshake_auth(s.msg_buf, UDISPLAY_MAX_MSG_SIZE,
                                           s.cfg.auth_algo, s.auth_salt);
        if (n > 0) {
            framed_send_raw(s.msg_buf, n, 1);
        }
    } else {
        uint16_t n = proto_handshake(s.msg_buf, UDISPLAY_MAX_MSG_SIZE,
                                      s.cfg.merkle_root, s.cfg.chunk_count);
        if (n > 0) {
            framed_send_raw(s.msg_buf, n, 1);
        }
    }
}

void udisplay_on_disconnect(void)
{
    s.connected          = 0;
    s.active             = 0;
    s.comms_miss_count   = 0;
    s.awaiting_auth_ack  = 0;
    s.pending_disconnect = 0;
    s.ble_tx_packet_id   = 0;
    rx_reset();
}

void udisplay_ble_feed(const uint8_t* att_payload, uint16_t len)
{
    ble_rx_status_t status = ble_rx_feed(&s.rx.ble_rx, att_payload, len);
    if (status == BLE_RX_DONE) {
        udisplay_on_message(s.rx.ble_rx.buf, s.rx.ble_rx.len);
        ble_rx_reset(&s.rx.ble_rx);
    } else if (status == BLE_RX_ERROR) {
        ble_rx_reset(&s.rx.ble_rx);
    }
}

int udisplay_ble_set_mtu(uint16_t mtu_payload)
{
    if (mtu_payload < 7u) return 0;
    if (mtu_payload > (uint16_t)sizeof(s.tx_buf.ble_frag)) return 0;
    s.ble_mtu_payload = mtu_payload;
    return 1;
}

static void on_tcp_message(const uint8_t* msg, uint16_t msg_len, void* ud)
{
    (void)ud;
    udisplay_on_message(msg, msg_len);
}

void udisplay_feed(const uint8_t* data, uint16_t len)
{
    switch (s.cfg.transport) {
        case UDISPLAY_TRANSPORT_BLE:
            udisplay_ble_feed(data, len);
            break;
        case UDISPLAY_TRANSPORT_TCP:
            if (tcp_rx_feed(&s.rx.tcp_rx, data, len, on_tcp_message, NULL) != 0) {
                /* Overflow: stream desynced, cannot recover byte-by-byte. */
                tcp_rx_reset(&s.rx.tcp_rx);
            }
            break;
        default: /* TRANSPORT_NONE */
            udisplay_on_message(data, len);
            break;
    }
}

void udisplay_on_message(const uint8_t* msg, uint16_t len)
{
    proto_inbound_t in;
    if (!proto_parse(msg, len, &in)) return;

    switch (in.type) {
        case PROTO_HANDSHAKE_ACK:
            /* Bootstrap-progress signal: proves the client is still there,
             * even though it hasn't reached CLIENT_READY yet. */
            s.comms_miss_count = 0;
            if (s.awaiting_auth_ack) {
                /* Validate flags: must echo 0x01 while we're in auth phase */
                if (in.handshake_ack.flags != 0x01u || !in.handshake_ack.credential) {
                    /* Misbehaving client — silently clear connection state */
                    s.connected         = 0;
                    s.active            = 0;
                    s.awaiting_auth_ack = 0;
                    rx_reset();
                    break;
                }
                int result = s.cfg.auth_check(
                    in.handshake_ack.credential, 32u,
                    s.auth_salt, s.cfg.userdata);
                if (result == 1) {
                    /* Auth passed: clear auth gate and send normal HANDSHAKE */
                    s.awaiting_auth_ack = 0;
                    uint16_t n = proto_handshake(s.msg_buf, UDISPLAY_MAX_MSG_SIZE,
                                                  s.cfg.merkle_root, s.cfg.chunk_count);
                    if (n > 0) {
                        framed_send_raw(s.msg_buf, n, 1);
                    }
                } else if (result == 0) {
                    /* Auth failed: issue fresh salt, re-send auth challenge */
                    fill_auth_salt();
                    uint16_t n = proto_handshake_auth(s.msg_buf, UDISPLAY_MAX_MSG_SIZE,
                                                       s.cfg.auth_algo, s.auth_salt);
                    if (n > 0) {
                        framed_send_raw(s.msg_buf, n, 1);
                    }
                } else {
                    /* result == -1: disconnect requested by firmware.
                     * Do NOT call on_disconnect() here — re-entrant state mutation.
                     * Set flag; state is cleared after the switch. */
                    s.pending_disconnect = 1;
                }
            }
            /* If !awaiting_auth_ack: normal no-auth ACK, nothing to do */
            break;

        case PROTO_CLIENT_READY:
            /* Reset unconditionally: CLIENT_READY is itself proof of life,
             * so bootstrap-phase misses must not carry over and combine
             * with post-active misses to fire spuriously right after the
             * transition. */
            s.comms_miss_count = 0;
            if (!s.active) {
                s.active = 1;
                if (s.cfg.on_client_ready)
                    s.cfg.on_client_ready(s.cfg.userdata);
            }
            break;

        case PROTO_HEARTBEAT:
            s.comms_miss_count = 0;
            break;

        case PROTO_CHUNK_HEADER_REQUEST: {
            s.comms_miss_count = 0;   /* bootstrap-progress signal */
            uint16_t n = chunk_server_header_response(
                &s.chunk_srv, s.msg_buf, UDISPLAY_MAX_MSG_SIZE, in.chunk_idx);
            do_send_always(s.msg_buf, n);   /* bootstrap: active may be 0 */
            break;
        }

        case PROTO_CHUNK_REQUEST: {
            s.comms_miss_count = 0;   /* bootstrap-progress signal */
            uint16_t n = chunk_server_respond(
                &s.chunk_srv, s.msg_buf, UDISPLAY_MAX_MSG_SIZE, in.chunk_idx);
            do_send_always(s.msg_buf, n);   /* bootstrap: active may be 0 */
            break;
        }

        case PROTO_EVENT:
            if (s.active) dispatch_event(&in);
            break;

        default:
            break;
    }

    if (s.pending_disconnect) {
        s.pending_disconnect = 0;
        s.connected          = 0;
        s.active             = 0;
        s.awaiting_auth_ack  = 0;
        s.comms_miss_count   = 0;
        rx_reset();
        /* Transport close is firmware's responsibility via its own transport API */
    }
}

/*
 * Single miss-count watchdog covering both connection phases:
 *   - BOOTSTRAP (connected=1, active=0): resets on HANDSHAKE_ACK,
 *     CLIENT_READY, CHUNK_HEADER_REQUEST, CHUNK_REQUEST (see
 *     udisplay_on_message) — any sign the client is still bootstrapping.
 *   - ACTIVE (connected=1, active=1): resets only on HEARTBEAT echo,
 *     unchanged from the original post-active-only watchdog.
 * connected/active are mutually exclusive per connection, so one counter
 * and one threshold (UDISPLAY_HB_MISS_MAX) serve both phases.
 */
void udisplay_heartbeat(void)
{
/*    if (s.connected && !s.active) {
      udisplay_on_connect();
    }*/

    uint16_t n = proto_heartbeat(s.msg_buf, UDISPLAY_MAX_MSG_SIZE);
    do_send_always(s.msg_buf, n);

    if (s.connected && s.comms_miss_count < UDISPLAY_HB_MISS_MAX) {
        if (++s.comms_miss_count == UDISPLAY_HB_MISS_MAX) {
            if (s.cfg.on_comms_error)
                s.cfg.on_comms_error(s.cfg.userdata);
        }
    }
}

void udisplay_send_float(uint8_t widget_id, float value)
{
    uint16_t n = proto_state_float(s.msg_buf, UDISPLAY_MAX_MSG_SIZE, widget_id, value);
    do_send(s.msg_buf, n);
}

void udisplay_send_int(uint8_t widget_id, int32_t value)
{
    uint16_t n = proto_state_int32(s.msg_buf, UDISPLAY_MAX_MSG_SIZE, widget_id, value);
    do_send(s.msg_buf, n);
}

void udisplay_send_bool(uint8_t widget_id, uint8_t value)
{
    uint16_t n = proto_state_uint8(s.msg_buf, UDISPLAY_MAX_MSG_SIZE, widget_id, value);
    do_send(s.msg_buf, n);
}

void udisplay_send_uint8(uint8_t widget_id, uint8_t value)
{
    uint16_t n = proto_state_uint8(s.msg_buf, UDISPLAY_MAX_MSG_SIZE, widget_id, value);
    do_send(s.msg_buf, n);
}

void udisplay_send_string(uint8_t widget_id, const char* str, uint8_t len)
{
    uint16_t n = proto_state_string(s.msg_buf, UDISPLAY_MAX_MSG_SIZE, widget_id, str, len);
    do_send(s.msg_buf, n);
}

void udisplay_set_property(uint8_t target_id, uint8_t property_id,
                            uint8_t value)
{
    uint16_t n = proto_set_property(s.msg_buf, UDISPLAY_MAX_MSG_SIZE,
                                     target_id, property_id, value);
    do_send(s.msg_buf, n);
}

void udisplay_reset_property(uint8_t target_id, uint8_t property_id)
{
    uint16_t n = proto_reset_property(s.msg_buf, UDISPLAY_MAX_MSG_SIZE,
                                       target_id, property_id);
    do_send(s.msg_buf, n);
}
