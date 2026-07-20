// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 Attila Agas

#include "protocol.h"
#include "../include/udisplay.h"
#include <string.h>

/* ── Little-endian helpers ───────────────────────────────────────────────── */

static inline void put_u16le(uint8_t* p, uint16_t v)
{
    p[0] = (uint8_t)(v & 0xFFu);
    p[1] = (uint8_t)(v >> 8u);
}

static inline void put_u32le(uint8_t* p, uint32_t v)
{
    p[0] = (uint8_t)(v);
    p[1] = (uint8_t)(v >> 8u);
    p[2] = (uint8_t)(v >> 16u);
    p[3] = (uint8_t)(v >> 24u);
}

static inline uint16_t get_u16le(const uint8_t* p)
{
    return (uint16_t)p[0] | ((uint16_t)p[1] << 8u);
}

/* ── Outbound encode ─────────────────────────────────────────────────────── */

uint16_t proto_handshake(uint8_t* buf, uint16_t cap,
                          const uint8_t root[32], uint16_t chunk_count)
{
    /* 39 bytes: msg_type, proto_version, flags=0x00, root[32], count u16, size u16 */
    if (cap < 39u) return 0;
    buf[0] = MSG_HANDSHAKE;
    buf[1] = UDISPLAY_PROTO_VERSION;
    buf[2] = 0x00u;                  /* flags: no auth */
    memcpy(buf + 3, root, 32);
    put_u16le(buf + 35, chunk_count);
    put_u16le(buf + 37, 256u);       /* chunk_size always 256 */
    return 39u;
}

uint16_t proto_handshake_auth(uint8_t* buf, uint16_t cap,
                               uint8_t algo, const uint8_t salt[32])
{
    /* 36 bytes: msg_type, proto_version, flags=0x01, algorithm, salt[32] */
    if (cap < 36u) return 0;
    buf[0] = MSG_HANDSHAKE;
    buf[1] = UDISPLAY_PROTO_VERSION;
    buf[2] = 0x01u;                  /* flags: auth required */
    buf[3] = algo;
    memcpy(buf + 4, salt, 32);
    return 36u;
}

uint16_t proto_chunk_header_response(uint8_t* buf, uint16_t cap,
                                      const uint8_t hash[32], uint8_t len_byte)
{
    if (cap < 34u) return 0;
    buf[0] = MSG_CHUNK_HEADER_RESPONSE;
    memcpy(buf + 1, hash, 32);
    buf[33] = len_byte;
    return 34u;
}

uint16_t proto_chunk_response(uint8_t* buf, uint16_t cap,
                               uint16_t idx,
                               const uint8_t* data, uint16_t len)
{
    uint16_t need = 5u + len;
    if (cap < need) return 0;
    buf[0] = MSG_CHUNK_RESPONSE;
    put_u16le(buf + 1, idx);
    put_u16le(buf + 3, len);
    memcpy(buf + 5, data, len);
    return need;
}

uint16_t proto_err_invalid_chunk(uint8_t* buf, uint16_t cap, uint16_t idx)
{
    if (cap < 3u) return 0;
    buf[0] = MSG_ERR_INVALID_CHUNK;
    put_u16le(buf + 1, idx);
    return 3u;
}

uint16_t proto_heartbeat(uint8_t* buf, uint16_t cap)
{
    if (cap < 1u) return 0;
    buf[0] = MSG_HEARTBEAT;
    return 1u;
}

/* STATE_UPDATE header helper: writes [0x30, widget_id, value_type] */
static inline void state_header(uint8_t* buf, uint8_t wid, uint8_t vtype)
{
    buf[0] = MSG_STATE_UPDATE;
    buf[1] = wid;
    buf[2] = vtype;
}

uint16_t proto_state_float(uint8_t* buf, uint16_t cap,
                            uint8_t widget_id, float v)
{
    if (cap < 7u) return 0;
    state_header(buf, widget_id, 0x01u);
    uint32_t bits;
    memcpy(&bits, &v, 4);   /* portable: no UB float→uint cast */
    put_u32le(buf + 3, bits);
    return 7u;
}

uint16_t proto_state_int32(uint8_t* buf, uint16_t cap,
                            uint8_t widget_id, int32_t v)
{
    if (cap < 7u) return 0;
    state_header(buf, widget_id, 0x02u);
    uint32_t bits;
    memcpy(&bits, &v, 4);
    put_u32le(buf + 3, bits);
    return 7u;
}

uint16_t proto_state_uint8(uint8_t* buf, uint16_t cap,
                            uint8_t widget_id, uint8_t v)
{
    if (cap < 4u) return 0;
    state_header(buf, widget_id, 0x03u);
    buf[3] = v;
    return 4u;
}

uint16_t proto_state_string(uint8_t* buf, uint16_t cap,
                             uint8_t widget_id, const char* str, uint8_t len)
{
    uint16_t need = 4u + len;
    if (cap < need) return 0;
    state_header(buf, widget_id, 0x04u);
    buf[3] = len;
    if (len > 0) memcpy(buf + 4, str, len);
    return need;
}

uint16_t proto_set_property(uint8_t* buf, uint16_t cap,
                             uint8_t target_id, uint8_t prop_id,
                             uint8_t value)
{
    if (cap < 4u) return 0;
    buf[0] = MSG_SET_PROPERTY;
    buf[1] = target_id;
    buf[2] = prop_id;
    buf[3] = value;
    return 4u;
}

uint16_t proto_reset_property(uint8_t* buf, uint16_t cap,
                               uint8_t target_id, uint8_t prop_id)
{
    if (cap < 3u) return 0;
    buf[0] = MSG_RESET_PROPERTY;
    buf[1] = target_id;
    buf[2] = prop_id;
    return 3u;
}

/* ── Inbound decode ──────────────────────────────────────────────────────── */

int proto_parse(const uint8_t* msg, uint16_t len, proto_inbound_t* out)
{
    if (len == 0) return 0;

    switch (msg[0]) {
        case MSG_HANDSHAKE_ACK:
            /* Proto 0x04: [msg_type, proto_max, flags, credential?]
             * Old clients (proto ≤ 0x03) send 2 bytes; we accept len ≥ 2. */
            if (len < 2u) return 0;
            out->type                     = PROTO_HANDSHAKE_ACK;
            out->handshake_ack.proto_max  = msg[1];
            out->handshake_ack.flags      = (len >= 3u) ? msg[2] : 0x00u;
            if (out->handshake_ack.flags == 0x01u) {
                /* Credential field required: flags=1 without 32-byte credential is invalid */
                if (len < 35u) return 0;
                out->handshake_ack.credential = msg + 3;
            } else {
                out->handshake_ack.credential = (const uint8_t*)0;
            }
            return 1;

        case MSG_CLIENT_READY:
            out->type = PROTO_CLIENT_READY;
            return 1;

        case MSG_HEARTBEAT:
            out->type = PROTO_HEARTBEAT;
            return 1;

        case MSG_CHUNK_HEADER_REQUEST:
            if (len < 3u) return 0;
            out->type      = PROTO_CHUNK_HEADER_REQUEST;
            out->chunk_idx = get_u16le(msg + 1);
            return 1;

        case MSG_CHUNK_REQUEST:
            if (len < 3u) return 0;
            out->type      = PROTO_CHUNK_REQUEST;
            out->chunk_idx = get_u16le(msg + 1);
            return 1;

        case MSG_EVENT:
            if (len < 3u) return 0;
            out->type              = PROTO_EVENT;
            out->event.widget_id   = msg[1];
            out->event.event_type  = msg[2];
            out->event.payload     = msg + 3;
            out->event.payload_len = (uint8_t)(len - 3u);
            return 1;

        default:
            out->type = PROTO_UNKNOWN;
            return 0;
    }
}
