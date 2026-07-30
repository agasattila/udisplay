// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 Attila Agas

#include "udisplay.h"
#include "protocol.h"
#include "chunk_server.h"
#include "framing.h"
#include "udisplay/version.h"
#include <string.h>

// Library version
const char *udisplay_version(void)
{
    return UDISPLAY_VERSION_STRING;
}

/* ── Internal helpers ────────────────────────────────────────────────────── */

/* Route a complete protocol message through transport framing.
 * always=1: send even before ctx->active (bootstrap); always=0: require active. */
static void framed_send_raw(udisplay_t* ctx, const uint8_t* msg, uint16_t len, int always)
{
    if (!ctx->connected || !ctx->cfg.send || len == 0) return;
    if (!always && !ctx->active) return;

    switch (ctx->cfg.transport) {
        case UDISPLAY_TRANSPORT_BLE: {
            uint16_t mtu = ctx->ble_mtu_payload;
            if (mtu < 7u) mtu = UDISPLAY_BLE_MTU_PAYLOAD_DEFAULT;
            udisplay_ble_fragment(msg, len, mtu, ctx->ble_tx_packet_id,
                                   ctx->tx_buf.ble_frag, (uint16_t)sizeof(ctx->tx_buf.ble_frag),
                                   ctx->cfg.send, ctx->cfg.userdata);
            ctx->ble_tx_packet_id++;
            break;
        }
        case UDISPLAY_TRANSPORT_TCP: {
            uint16_t n = udisplay_tcp_frame(ctx->tx_buf.tcp_framed,
                                             (uint16_t)sizeof(ctx->tx_buf.tcp_framed),
                                             msg, len);
            if (n > 0) {
                ctx->cfg.send(ctx->tx_buf.tcp_framed, n, ctx->cfg.userdata);
            }
            break;
        }
        default: /* TRANSPORT_NONE */
            ctx->cfg.send(msg, len, ctx->cfg.userdata);
            break;
    }
}

static void do_send(udisplay_t* ctx, const uint8_t* msg, uint16_t len)
{
    framed_send_raw(ctx, msg, len, 0);
}

static void do_send_always(udisplay_t* ctx, const uint8_t* msg, uint16_t len)
{
    framed_send_raw(ctx, msg, len, 1);
}

static void dispatch_event(udisplay_t* ctx, const proto_inbound_t* in)
{
    if (!ctx->cfg.on_event) return;

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

    ctx->cfg.on_event(&ev, ctx->cfg.userdata);
}

/* ── Public API ──────────────────────────────────────────────────────────── */

/* Reset whichever inbound reassembly state is active for cfg.transport.
 * ctx->rx is a union (ble_rx and tcp_rx never active simultaneously — see the
 * udisplay_t comment in udisplay.h), so only the current transport's member
 * is meaningful to reset. */
static void rx_reset(udisplay_t* ctx)
{
    if (ctx->cfg.transport == UDISPLAY_TRANSPORT_BLE) {
        ble_rx_reset(&ctx->rx.ble_rx);
    } else if (ctx->cfg.transport == UDISPLAY_TRANSPORT_TCP) {
        tcp_rx_reset(&ctx->rx.tcp_rx);
    }
}

void udisplay_init(udisplay_t* ctx, const udisplay_config_t* cfg)
{
    memcpy(&ctx->cfg, cfg, sizeof(*cfg));
    chunk_server_init(&ctx->chunk_srv,
                      cfg->chunks,
                      cfg->chunk_hashes,
                      cfg->chunk_lens,
                      cfg->chunk_count);
    rx_reset(ctx);
    ctx->connected       = 0;
    ctx->ble_mtu_payload = (cfg->ble_mtu_payload >= 7u)
                        ? cfg->ble_mtu_payload
                        : UDISPLAY_BLE_MTU_PAYLOAD_DEFAULT;
}

static void fill_auth_salt(udisplay_t* ctx)
{
    if (ctx->cfg.fill_random) {
        ctx->cfg.fill_random(ctx->auth_salt, 32u, ctx->cfg.userdata);
    } else {
        /* Insecure deterministic fallback — firmware MUST provide fill_random.
         * Counter lives in ctx (not a function-local static) so it can't race
         * across instances -- see udisplay_t's insecure_salt_ctr field. */
        uint8_t i;
        for (i = 0u; i < 32u; i++) {
            ctx->auth_salt[i] = ++ctx->insecure_salt_ctr ^ (uint8_t)(i * 7u);
        }
    }
}

void udisplay_on_connect(udisplay_t* ctx)
{
    ctx->connected          = 1;
    ctx->active             = 0;
    ctx->comms_miss_count   = 0;
    ctx->awaiting_auth_ack  = 0;
    ctx->pending_disconnect = 0;
    ctx->ble_tx_packet_id   = 0;
    rx_reset(ctx);

    if (ctx->cfg.auth_algo != UDISPLAY_AUTH_NONE) {
        fill_auth_salt(ctx);
        ctx->awaiting_auth_ack = 1;
        uint16_t n = proto_handshake_auth(ctx->msg_buf, UDISPLAY_MAX_MSG_SIZE,
                                           ctx->cfg.auth_algo, ctx->auth_salt);
        if (n > 0) {
            framed_send_raw(ctx, ctx->msg_buf, n, 1);
        }
    } else {
        uint16_t n = proto_handshake(ctx->msg_buf, UDISPLAY_MAX_MSG_SIZE,
                                      ctx->cfg.merkle_root, ctx->cfg.chunk_count);
        if (n > 0) {
            framed_send_raw(ctx, ctx->msg_buf, n, 1);
        }
    }
}

void udisplay_on_disconnect(udisplay_t* ctx)
{
    ctx->connected          = 0;
    ctx->active             = 0;
    ctx->comms_miss_count   = 0;
    ctx->awaiting_auth_ack  = 0;
    ctx->pending_disconnect = 0;
    ctx->ble_tx_packet_id   = 0;
    rx_reset(ctx);
}

void udisplay_ble_feed(udisplay_t* ctx, const uint8_t* att_payload, uint16_t len)
{
    ble_rx_status_t status = ble_rx_feed(&ctx->rx.ble_rx, att_payload, len);
    if (status == BLE_RX_DONE) {
        udisplay_on_message(ctx, ctx->rx.ble_rx.buf, ctx->rx.ble_rx.len);
        ble_rx_reset(&ctx->rx.ble_rx);
    } else if (status == BLE_RX_ERROR) {
        ble_rx_reset(&ctx->rx.ble_rx);
    }
}

int udisplay_ble_set_mtu(udisplay_t* ctx, uint16_t mtu_payload)
{
    if (mtu_payload < 7u) return 0;
    if (mtu_payload > (uint16_t)sizeof(ctx->tx_buf.ble_frag)) return 0;
    ctx->ble_mtu_payload = mtu_payload;
    return 1;
}

static void on_tcp_message(const uint8_t* msg, uint16_t msg_len, void* ud)
{
    udisplay_t* ctx = (udisplay_t*)ud;
    udisplay_on_message(ctx, msg, msg_len);
}

void udisplay_feed(udisplay_t* ctx, const uint8_t* data, uint16_t len)
{
    switch (ctx->cfg.transport) {
        case UDISPLAY_TRANSPORT_BLE:
            udisplay_ble_feed(ctx, data, len);
            break;
        case UDISPLAY_TRANSPORT_TCP:
            if (tcp_rx_feed(&ctx->rx.tcp_rx, data, len, on_tcp_message, ctx) != 0) {
                /* Overflow: stream desynced, cannot recover byte-by-byte. */
                tcp_rx_reset(&ctx->rx.tcp_rx);
            }
            break;
        default: /* TRANSPORT_NONE */
            udisplay_on_message(ctx, data, len);
            break;
    }
}

void udisplay_on_message(udisplay_t* ctx, const uint8_t* msg, uint16_t len)
{
    proto_inbound_t in;
    if (!proto_parse(msg, len, &in)) return;

    switch (in.type) {
        case PROTO_HANDSHAKE_ACK:
            /* Bootstrap-progress signal: proves the client is still there,
             * even though it hasn't reached CLIENT_READY yet. */
            ctx->comms_miss_count = 0;
            if (ctx->awaiting_auth_ack) {
                /* Validate flags: must echo 0x01 while we're in auth phase */
                if (in.handshake_ack.flags != 0x01u || !in.handshake_ack.credential) {
                    /* Misbehaving client — silently clear connection state */
                    ctx->connected         = 0;
                    ctx->active            = 0;
                    ctx->awaiting_auth_ack = 0;
                    rx_reset(ctx);
                    break;
                }
                int result = ctx->cfg.auth_check(
                    in.handshake_ack.credential, 32u,
                    ctx->auth_salt, ctx->cfg.userdata);
                if (result == 1) {
                    /* Auth passed: clear auth gate and send normal HANDSHAKE */
                    ctx->awaiting_auth_ack = 0;
                    uint16_t n = proto_handshake(ctx->msg_buf, UDISPLAY_MAX_MSG_SIZE,
                                                  ctx->cfg.merkle_root, ctx->cfg.chunk_count);
                    if (n > 0) {
                        framed_send_raw(ctx, ctx->msg_buf, n, 1);
                    }
                } else if (result == 0) {
                    /* Auth failed: issue fresh salt, re-send auth challenge */
                    fill_auth_salt(ctx);
                    uint16_t n = proto_handshake_auth(ctx->msg_buf, UDISPLAY_MAX_MSG_SIZE,
                                                       ctx->cfg.auth_algo, ctx->auth_salt);
                    if (n > 0) {
                        framed_send_raw(ctx, ctx->msg_buf, n, 1);
                    }
                } else {
                    /* result == -1: disconnect requested by firmware.
                     * Do NOT call on_disconnect() here — re-entrant state mutation.
                     * Set flag; state is cleared after the switch. */
                    ctx->pending_disconnect = 1;
                }
            }
            /* If !awaiting_auth_ack: normal no-auth ACK, nothing to do */
            break;

        case PROTO_CLIENT_READY:
            /* Reset unconditionally: CLIENT_READY is itself proof of life,
             * so bootstrap-phase misses must not carry over and combine
             * with post-active misses to fire spuriously right after the
             * transition. */
            ctx->comms_miss_count = 0;
            if (!ctx->active) {
                ctx->active = 1;
                if (ctx->cfg.on_client_ready)
                    ctx->cfg.on_client_ready(ctx->cfg.userdata);
            }
            break;

        case PROTO_HEARTBEAT:
            ctx->comms_miss_count = 0;
            break;

        case PROTO_CHUNK_HEADER_REQUEST: {
            ctx->comms_miss_count = 0;   /* bootstrap-progress signal */
            uint16_t n = chunk_server_header_response(
                &ctx->chunk_srv, ctx->msg_buf, UDISPLAY_MAX_MSG_SIZE, in.chunk_idx);
            do_send_always(ctx, ctx->msg_buf, n);   /* bootstrap: active may be 0 */
            break;
        }

        case PROTO_CHUNK_REQUEST: {
            ctx->comms_miss_count = 0;   /* bootstrap-progress signal */
            uint16_t n = chunk_server_respond(
                &ctx->chunk_srv, ctx->msg_buf, UDISPLAY_MAX_MSG_SIZE, in.chunk_idx);
            do_send_always(ctx, ctx->msg_buf, n);   /* bootstrap: active may be 0 */
            break;
        }

        case PROTO_EVENT:
            if (ctx->active) dispatch_event(ctx, &in);
            break;

        default:
            break;
    }

    if (ctx->pending_disconnect) {
        ctx->pending_disconnect = 0;
        ctx->connected          = 0;
        ctx->active             = 0;
        ctx->awaiting_auth_ack  = 0;
        ctx->comms_miss_count   = 0;
        rx_reset(ctx);
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
void udisplay_heartbeat(udisplay_t* ctx)
{
    uint16_t n = proto_heartbeat(ctx->msg_buf, UDISPLAY_MAX_MSG_SIZE);
    do_send_always(ctx, ctx->msg_buf, n);

    if (ctx->connected && ctx->comms_miss_count < UDISPLAY_HB_MISS_MAX) {
        if (++ctx->comms_miss_count == UDISPLAY_HB_MISS_MAX) {
            if (ctx->cfg.on_comms_error)
                ctx->cfg.on_comms_error(ctx->cfg.userdata);
        }
    }
}

void udisplay_send_float(udisplay_t* ctx, uint8_t widget_id, float value)
{
    uint16_t n = proto_state_float(ctx->msg_buf, UDISPLAY_MAX_MSG_SIZE, widget_id, value);
    do_send(ctx, ctx->msg_buf, n);
}

void udisplay_send_int(udisplay_t* ctx, uint8_t widget_id, int32_t value)
{
    uint16_t n = proto_state_int32(ctx->msg_buf, UDISPLAY_MAX_MSG_SIZE, widget_id, value);
    do_send(ctx, ctx->msg_buf, n);
}

void udisplay_send_bool(udisplay_t* ctx, uint8_t widget_id, uint8_t value)
{
    uint16_t n = proto_state_uint8(ctx->msg_buf, UDISPLAY_MAX_MSG_SIZE, widget_id, value);
    do_send(ctx, ctx->msg_buf, n);
}

void udisplay_send_uint8(udisplay_t* ctx, uint8_t widget_id, uint8_t value)
{
    uint16_t n = proto_state_uint8(ctx->msg_buf, UDISPLAY_MAX_MSG_SIZE, widget_id, value);
    do_send(ctx, ctx->msg_buf, n);
}

void udisplay_send_string(udisplay_t* ctx, uint8_t widget_id, const char* str, uint8_t len)
{
    uint16_t n = proto_state_string(ctx->msg_buf, UDISPLAY_MAX_MSG_SIZE, widget_id, str, len);
    do_send(ctx, ctx->msg_buf, n);
}

void udisplay_set_property(udisplay_t* ctx, uint8_t target_id, uint8_t property_id,
                            uint8_t value)
{
    uint16_t n = proto_set_property(ctx->msg_buf, UDISPLAY_MAX_MSG_SIZE,
                                     target_id, property_id, value);
    do_send(ctx, ctx->msg_buf, n);
}

void udisplay_reset_property(udisplay_t* ctx, uint8_t target_id, uint8_t property_id)
{
    uint16_t n = proto_reset_property(ctx->msg_buf, UDISPLAY_MAX_MSG_SIZE,
                                       target_id, property_id);
    do_send(ctx, ctx->msg_buf, n);
}
