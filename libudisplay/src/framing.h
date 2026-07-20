// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 Attila Agas

/**
 * @file framing.h
 * @brief BLE and TCP inbound reassembly state.
 *
 * BLE fragmentation (outbound) and TCP framing (single-message frame/unframe)
 * are implemented as the public udisplay_ble_fragment / udisplay_tcp_frame /
 * udisplay_tcp_unframe functions declared in udisplay.h.
 *
 * This header exposes the inbound BLE and TCP reassembly state machines,
 * which are internal to the library. Both are stream-oriented: BLE fragments
 * one ATT notification at a time (ble_rx_feed), TCP buffers arbitrary stream
 * chunks and drains every complete length-prefixed message found in each
 * feed (tcp_rx_feed) -- a single TCP recv() can contain a partial message,
 * exactly one, or several back to back.
 */
#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include "../include/udisplay.h"

typedef struct {
    uint8_t  buf[UDISPLAY_MAX_MSG_SIZE]; /**< Reassembly buffer */
    uint16_t len;             /**< Bytes accumulated so far */
    uint16_t msg_len;         /**< Total message length declared in first-fragment header */
    uint16_t expected_offset; /**< Next expected fragment byte offset */
    uint8_t  packet_id;       /**< packet_id from first fragment; expected on continuations */
    int      in_progress;     /**< 1 while reassembling; 0 when idle */
    int      overflow;        /**< Set on over-completion (cleared by ble_rx_reset) */
} ble_rx_t;

/** Reset reassembly state. Call on connect, disconnect, or after consuming a message. */
void ble_rx_reset(ble_rx_t* rx);

typedef enum {
    BLE_RX_MORE  = 0,   /**< Fragment accepted; message not yet complete. */
    BLE_RX_DONE  = 1,   /**< Last fragment received; rx->buf/len has full message. */
    BLE_RX_ERROR = 2,   /**< Framing error; state reset to idle. */
} ble_rx_status_t;

/**
 * Feed one ATT notification into the reassembly state machine.
 *
 * BLE fragmentation format (v2.2, offset+packet_id scheme):
 *
 * First fragment (offset == 0):
 *   [u16 offset=0x0000][u8 packet_id][u16 length][u8 flags=0x00][payload...]
 *   Header: 6 bytes. flags MUST be 0x00; length is the total unframed message size.
 *
 * Continuation fragment (offset > 0):
 *   [u16 offset][u8 packet_id][payload...]
 *   Header: 3 bytes. offset is the byte position of this fragment's payload.
 *
 * A fragment with offset=0 always starts a fresh reassembly, discarding any
 * previously accumulated bytes. Completion: rx->len == rx->msg_len.
 */
ble_rx_status_t ble_rx_feed(ble_rx_t* rx,
                             const uint8_t* att_payload, uint16_t att_len);

/* ── TCP inbound reassembly ──────────────────────────────────────────────── */

typedef struct {
    uint8_t  buf[UDISPLAY_RX_BUF_SIZE]; /**< Reassembly buffer (partial + in-flight bytes) */
    uint16_t used;                      /**< Bytes currently buffered */
} tcp_rx_t;

/** Reset reassembly state. Call on connect or disconnect. */
void tcp_rx_reset(tcp_rx_t* rx);

/**
 * Append newly-received TCP stream bytes and dispatch every complete
 * u16_le length-prefixed message found -- including a message left partial
 * by a previous call, and multiple complete messages arriving in one call.
 *
 * @p on_message is called once per complete message, in stream order; its
 * @p msg pointer is only valid for the duration of that call (the internal
 * buffer is shifted immediately afterward).
 *
 * @return 0 on success, -1 if @p len would overflow the reassembly buffer
 *         (caller should tcp_rx_reset() -- the stream is desynced and
 *         cannot be recovered byte-by-byte).
 */
int tcp_rx_feed(tcp_rx_t* rx, const uint8_t* data, uint16_t len,
                 void (*on_message)(const uint8_t* msg, uint16_t msg_len, void* ud),
                 void* userdata);

#ifdef __cplusplus
}
#endif
