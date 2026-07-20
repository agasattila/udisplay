// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 Attila Agas

/**
 * @file protocol.h
 * @brief Internal message encode/decode for the uDisplay wire protocol.
 *
 * All encode functions write into a caller-supplied buffer and return the
 * number of bytes written, or 0 if the buffer is too small.
 *
 * All decode functions operate on a pointer into the receive buffer;
 * the pointer remains valid for the caller's lifetime of that buffer.
 */
#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

/* ── Message type bytes ──────────────────────────────────────────────────── */

#define MSG_HANDSHAKE           0x00u
#define MSG_HANDSHAKE_ACK       0x01u
#define MSG_CLIENT_READY        0x02u
#define MSG_CHUNK_HEADER_REQUEST  0x10u
#define MSG_CHUNK_HEADER_RESPONSE 0x11u
#define MSG_CHUNK_REQUEST       0x20u
#define MSG_CHUNK_RESPONSE      0x21u
#define MSG_ERR_INVALID_CHUNK   0xFFu
#define MSG_HEARTBEAT           0x40u
#define MSG_STATE_UPDATE        0x30u
#define MSG_EVENT               0x31u
#define MSG_SET_PROPERTY        0x32u
#define MSG_RESET_PROPERTY      0x33u

/* ── Outbound encode: firmware → client ──────────────────────────────────── */

/**
 * HANDSHAKE no-auth (39 bytes): msg_type, proto_version, flags=0x00,
 * 32-byte root, u16 chunk_count, u16 chunk_size.
 */
uint16_t proto_handshake(uint8_t* buf, uint16_t cap,
                          const uint8_t root[32], uint16_t chunk_count);

/**
 * HANDSHAKE auth-challenge (36 bytes): msg_type, proto_version, flags=0x01,
 * algorithm, 32-byte salt.
 * @param algo  UDISPLAY_AUTH_HMAC_SHA256 (0x01)
 * @param salt  32-byte random challenge salt
 */
uint16_t proto_handshake_auth(uint8_t* buf, uint16_t cap,
                               uint8_t algo, const uint8_t salt[32]);

/** CHUNK_HEADER_RESPONSE (34 bytes): msg_type, hash[32], len_byte.
 *  len_byte = 0 for a full 256-byte chunk; 1-255 for the partial last chunk. */
uint16_t proto_chunk_header_response(uint8_t* buf, uint16_t cap,
                                      const uint8_t hash[32], uint8_t len_byte);

/** CHUNK_RESPONSE: msg_type, u16 idx, u16 len, data[len]. */
uint16_t proto_chunk_response(uint8_t* buf, uint16_t cap,
                               uint16_t idx,
                               const uint8_t* data, uint16_t len);

/** ERR_INVALID_CHUNK (3 bytes): msg_type, u16 idx. */
uint16_t proto_err_invalid_chunk(uint8_t* buf, uint16_t cap, uint16_t idx);

/** HEARTBEAT (1 byte). */
uint16_t proto_heartbeat(uint8_t* buf, uint16_t cap);

/** STATE_UPDATE float32 (7 bytes). */
uint16_t proto_state_float(uint8_t* buf, uint16_t cap,
                            uint8_t widget_id, float v);

/** STATE_UPDATE int32 (7 bytes). */
uint16_t proto_state_int32(uint8_t* buf, uint16_t cap,
                            uint8_t widget_id, int32_t v);

/** STATE_UPDATE uint8 (4 bytes). */
uint16_t proto_state_uint8(uint8_t* buf, uint16_t cap,
                            uint8_t widget_id, uint8_t v);

/** STATE_UPDATE string (4 + len bytes). */
uint16_t proto_state_string(uint8_t* buf, uint16_t cap,
                             uint8_t widget_id, const char* str, uint8_t len);

/** SET_PROPERTY (0x32): [msg_type, target_id, prop_id, value] = 4 bytes. */
uint16_t proto_set_property(uint8_t* buf, uint16_t cap,
                             uint8_t target_id, uint8_t prop_id,
                             uint8_t value);

/** RESET_PROPERTY (0x33): [msg_type, target_id, prop_id] = 3 bytes. */
uint16_t proto_reset_property(uint8_t* buf, uint16_t cap,
                               uint8_t target_id, uint8_t prop_id);

/* ── Inbound decode: client → firmware ──────────────────────────────────── */

typedef enum {
    PROTO_UNKNOWN       = 0,
    PROTO_HANDSHAKE_ACK,
    PROTO_CLIENT_READY,
    PROTO_CHUNK_HEADER_REQUEST,
    PROTO_CHUNK_REQUEST,
    PROTO_EVENT,
    PROTO_HEARTBEAT,
} proto_inbound_type_t;

typedef struct {
    proto_inbound_type_t type;
    union {
        struct {
            uint8_t        proto_max;   /**< client_proto_max byte */
            uint8_t        flags;       /**< 0x00 = no auth, 0x01 = credential present */
            const uint8_t* credential;  /**< 32-byte credential when flags=1; NULL otherwise */
        } handshake_ack;
        uint16_t chunk_idx;     /**< CHUNK_REQUEST / CHUNK_HEADER_REQUEST: chunk index */
        struct {
            uint8_t        widget_id;
            uint8_t        event_type;
            const uint8_t* payload;     /**< points into original msg buffer */
            uint8_t        payload_len;
        } event;
    };
} proto_inbound_t;

/**
 * Parse one inbound message.
 * Returns 1 on success, 0 on malformed or unknown message type.
 */
int proto_parse(const uint8_t* msg, uint16_t len, proto_inbound_t* out);

#ifdef __cplusplus
}
#endif
