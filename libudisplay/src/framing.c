// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 Attila Agas

#include "framing.h"
#include "../include/udisplay.h"
#include <string.h>

/* ── BLE inbound reassembly ─────────────────────────────────────────────── */

void ble_rx_reset(ble_rx_t* rx)
{
    rx->len             = 0;
    rx->msg_len         = 0;
    rx->expected_offset = 0;
    rx->packet_id       = 0;
    rx->in_progress     = 0;
    rx->overflow        = 0;
}

ble_rx_status_t ble_rx_feed(ble_rx_t* rx,
                             const uint8_t* att_payload, uint16_t att_len)
{
    if (att_len < 2u) return BLE_RX_ERROR;

    uint16_t offset = (uint16_t)att_payload[0] | ((uint16_t)att_payload[1] << 8u);

    if (offset == 0u) {
        /* First fragment — [u16 offset=0][u8 packet_id][u16 length][u8 flags][payload...] */
        if (att_len < 6u) { ble_rx_reset(rx); return BLE_RX_ERROR; }

        uint8_t  pkt_id  = att_payload[2];
        uint16_t msg_len = (uint16_t)att_payload[3] | ((uint16_t)att_payload[4] << 8u);
        uint8_t  flags   = att_payload[5];

        /* Error rule 1: flags field must be 0x00 */
        if (flags != 0x00u) { ble_rx_reset(rx); return BLE_RX_ERROR; }
        /* Error rule 2: declared length exceeds reassembly buffer */
        if (msg_len > UDISPLAY_MAX_MSG_SIZE) { ble_rx_reset(rx); return BLE_RX_ERROR; }

        uint16_t payload_len = att_len - 6u;

        /* First fragment payload must not exceed declared message length */
        if (payload_len > msg_len) { ble_rx_reset(rx); return BLE_RX_ERROR; }

        /* offset=0 always starts a fresh reassembly */
        ble_rx_reset(rx);

        if (payload_len > 0u) {
            memcpy(rx->buf, att_payload + 6, payload_len);
        }
        rx->len             = payload_len;
        rx->msg_len         = msg_len;
        rx->expected_offset = payload_len;
        rx->packet_id       = pkt_id;
        rx->in_progress     = 1;

        if (payload_len == msg_len) {
            rx->in_progress = 0;
            return BLE_RX_DONE;
        }
        return BLE_RX_MORE;

    } else {
        /* Continuation fragment — [u16 offset][u8 packet_id][payload...] */
        if (!rx->in_progress) { return BLE_RX_ERROR; }
        if (att_len < 3u) { ble_rx_reset(rx); return BLE_RX_ERROR; }

        uint8_t  pkt_id      = att_payload[2];
        uint16_t payload_len = att_len - 3u;

        /* Error rule 5: unexpected packet_id on continuation */
        if (pkt_id != rx->packet_id) { ble_rx_reset(rx); return BLE_RX_ERROR; }
        /* Error rules 3+4: offset gap or wrong byte position */
        if (offset != rx->expected_offset) { ble_rx_reset(rx); return BLE_RX_ERROR; }
        /* Error rule 6: accumulated bytes would exceed declared message length */
        if ((uint32_t)rx->len + payload_len > rx->msg_len) {
            ble_rx_reset(rx);
            return BLE_RX_ERROR;
        }

        memcpy(rx->buf + rx->len, att_payload + 3, payload_len);
        rx->len             += payload_len;
        rx->expected_offset += payload_len;

        if (rx->len == rx->msg_len) {
            rx->in_progress = 0;
            return BLE_RX_DONE;
        }
        return BLE_RX_MORE;
    }
}

/* ── BLE outbound fragmentation (public API, declared in udisplay.h) ─────── */

void udisplay_ble_fragment(const uint8_t* msg, uint16_t msg_len,
                            uint16_t mtu_payload, uint8_t packet_id,
                            uint8_t* frag_buf, uint16_t frag_buf_cap,
                            void (*emit)(const uint8_t* frag, uint16_t frag_len, void* ud),
                            void* userdata)
{
    if (msg_len == 0u) return;
    /* Minimum effective payload: 6-byte first-fragment header + 1 payload byte */
    if (mtu_payload < 7u) return;
    /* Maximum BLE ATT_MTU payload is 517 bytes (ATT_MTU=520, minus 3 ATT header) */
    if (mtu_payload > 517u) mtu_payload = 517u;
    /* Clamp to caller-provided buffer capacity */
    if (mtu_payload > frag_buf_cap) mtu_payload = (uint16_t)frag_buf_cap;

    uint16_t first_data_cap = mtu_payload - 6u;  /* payload bytes in first fragment */
    uint16_t cont_data_cap  = mtu_payload - 3u;  /* payload bytes per continuation */
    uint16_t offset         = 0u;

    /* First fragment */
    {
        uint16_t chunk = (msg_len < first_data_cap) ? msg_len : first_data_cap;

        frag_buf[0] = 0x00u;                                /* offset lo */
        frag_buf[1] = 0x00u;                                /* offset hi */
        frag_buf[2] = packet_id;
        frag_buf[3] = (uint8_t)(msg_len & 0xFFu);           /* length lo */
        frag_buf[4] = (uint8_t)(msg_len >> 8u);             /* length hi */
        frag_buf[5] = 0x00u;                                /* flags = 0 */
        memcpy(frag_buf + 6, msg, chunk);

        emit(frag_buf, (uint16_t)(6u + chunk), userdata);
        offset = chunk;
    }

    /* Continuation fragments */
    while (offset < msg_len) {
        uint16_t remaining = msg_len - offset;
        uint16_t chunk     = (remaining < cont_data_cap) ? remaining : cont_data_cap;

        frag_buf[0] = (uint8_t)(offset & 0xFFu);
        frag_buf[1] = (uint8_t)(offset >> 8u);
        frag_buf[2] = packet_id;
        memcpy(frag_buf + 3, msg + offset, chunk);

        emit(frag_buf, (uint16_t)(3u + chunk), userdata);
        offset += chunk;
    }
}

/* ── TCP framing (public API, declared in udisplay.h) ───────────────────── */

uint16_t udisplay_tcp_frame(uint8_t* out, uint16_t out_cap,
                             const uint8_t* msg, uint16_t msg_len)
{
    if (msg_len > UDISPLAY_MAX_MSG_SIZE) return 0;
    if (out_cap < (uint16_t)(msg_len + 2u)) return 0;
    out[0] = (uint8_t)(msg_len & 0xFFu);
    out[1] = (uint8_t)(msg_len >> 8u);
    memcpy(out + 2, msg, msg_len);
    return (uint16_t)(msg_len + 2u);
}

int udisplay_tcp_unframe(const uint8_t* buf, uint16_t buf_len,
                          const uint8_t** msg_out, uint16_t* msg_len_out)
{
    if (buf_len < 2u) return 0;
    uint16_t payload_len = (uint16_t)buf[0] | ((uint16_t)buf[1] << 8u);
    if (payload_len > UDISPLAY_MAX_MSG_SIZE) return 0;
    if ((uint32_t)buf_len < (uint32_t)(2u + payload_len)) return 0;
    *msg_out     = buf + 2;
    *msg_len_out = payload_len;
    return 1;
}

/* ── TCP inbound reassembly ──────────────────────────────────────────────── */

void tcp_rx_reset(tcp_rx_t* rx)
{
    rx->used = 0;
}

int tcp_rx_feed(tcp_rx_t* rx, const uint8_t* data, uint16_t len,
                 void (*on_message)(const uint8_t* msg, uint16_t msg_len, void* ud),
                 void* userdata)
{
    if ((uint32_t)rx->used + len > sizeof(rx->buf)) {
        return -1;
    }
    memcpy(rx->buf + rx->used, data, len);
    rx->used += len;

    const uint8_t* msg;
    uint16_t       msg_len;
    while (udisplay_tcp_unframe(rx->buf, rx->used, &msg, &msg_len)) {
        uint16_t total = (uint16_t)(2u + msg_len);
        on_message(msg, msg_len, userdata);
        rx->used -= total;
        if (rx->used) memmove(rx->buf, rx->buf + total, rx->used);
    }
    return 0;
}
