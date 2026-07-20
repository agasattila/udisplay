// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 Attila Agas

/**
 * @file udisplay.h
 * @brief uDisplay firmware library — C public API.
 *
 * Include this header in firmware code. All symbols are extern "C" so
 * the library is usable from both C and C++ firmware.
 *
 * Usage pattern (ESP-IDF / FreeRTOS, BLE transport):
 *
 *   // Provide send callback. The library calls this once per BLE ATT fragment
 *   // (BLE transport) or once per length-prefixed TCP write (TCP transport).
 *   // Framing and fragmentation are handled automatically by the library.
 *   //
 *   static void my_send(const uint8_t* data, uint16_t len, void* ud) {
 *       // BLE: each call is one ATT notification (already fragmented, v2.2 scheme).
 *       // TCP: each call is one length-prefixed message.
 *       ble_gatts_notify_custom(conn_handle, data_attr_handle,
 *                               ble_hs_mbuf_from_flat(data, len));
 *   }
 *
 *   // Provide event callback (called when client interacts with a widget)
 *   static void my_event(const udisplay_event_t* ev, void* ud) {
 *       if (ev->widget_id == WIDGET_ID_RELAY1 &&
 *           ev->event_type == UDISPLAY_EVENT_TOGGLE_CHANGE) {
 *           gpio_set_level(RELAY_PIN, ev->toggle_state);
 *       }
 *   }
 *
 *   void app_main(void) {
 *       udisplay_config_t cfg = {
 *           .merkle_root  = UDISPLAY_MERKLE_ROOT,
 *           .chunks       = UDISPLAY_CHUNKS,
 *           .chunk_hashes = UDISPLAY_CHUNK_HASHES,
 *           .chunk_lens   = UDISPLAY_CHUNK_LENS,
 *           .chunk_count  = UDISPLAY_CHUNK_COUNT,
 *           .send         = my_send,
 *           .on_event     = my_event,
 *           .userdata     = NULL,
 *           .transport    = UDISPLAY_TRANSPORT_BLE,
 *       };
 *       udisplay_init(&cfg);
 *
 *       // On BLE connect:    udisplay_on_connect()
 *       // On BLE disconnect: udisplay_on_disconnect()
 *       // On BLE ATT write from client: udisplay_feed(data, len)
 *       // From timer (e.g. every 5s): udisplay_heartbeat()
 *       // From sensor loop: udisplay_send_float(WIDGET_ID_READING, value)
 *       // On BLE MTU event:  udisplay_ble_set_mtu(mtu_value - 3)
 *   }
 */

#pragma once

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ── Constants ───────────────────────────────────────────────────────────── */

#define UDISPLAY_PROTO_VERSION    0x04u
#define UDISPLAY_CHUNK_SIZE_BYTES 256u
#define UDISPLAY_HB_MISS_MAX      3u  /**< Consecutive comms misses (missed HEARTBEAT echo once
                                            active, or missed bootstrap progress before active)
                                            that trigger on_comms_error */

/** Default BLE ATT payload data capacity per fragment (ATT_MTU=23 minus 3 ATT header). */
#define UDISPLAY_BLE_MTU_PAYLOAD_DEFAULT 20u

/*
 * Maximum reassembled/framed message size.
 * Bounds the BLE reassembly buffer and the TCP frame size.
 * All messages larger than this are rejected at the framing layer.
 *
 * The largest per-message response is CHUNK_HEADER_RESPONSE at 34 bytes,
 * so this limit applies uniformly to all message types.
 */
#define UDISPLAY_MAX_MSG_SIZE     1024u

/*
 * TCP inbound reassembly buffer size (udisplay_feed(), TRANSPORT_TCP case).
 * Worst case: up to (UDISPLAY_MAX_MSG_SIZE+1) leftover bytes from a prior
 * partial frame, plus one full (UDISPLAY_MAX_MSG_SIZE+2) framed message =
 * 2*(UDISPLAY_MAX_MSG_SIZE+2) bytes always fits. Firmware that never uses
 * TRANSPORT_TCP (e.g. BLE-only builds) may override this via a build-system
 * define (-DUDISPLAY_RX_BUF_SIZE=...) to shrink the static footprint.
 */
#ifndef UDISPLAY_RX_BUF_SIZE
#define UDISPLAY_RX_BUF_SIZE      (2u * (UDISPLAY_MAX_MSG_SIZE + 2u))
#endif

/* Value types (STATE_UPDATE value_type field) */
#define UDISPLAY_VALUE_FLOAT32  0x01u
#define UDISPLAY_VALUE_INT32    0x02u
#define UDISPLAY_VALUE_UINT8    0x03u
#define UDISPLAY_VALUE_STRING   0x04u

/* Authentication algorithm identifiers (auth_algo field in udisplay_config_t) */
#define UDISPLAY_AUTH_NONE    0x00u  /**< No authentication (default) */
#define UDISPLAY_AUTH_HMAC_SHA256  0x01u  /**< HMAC-SHA256(key=password, message=salt) per RFC 2104 */

/* Event types (EVENT event_type field) */
#define UDISPLAY_EVENT_BUTTON_CLICK       0x01u  /**< Complete tap: press + release within bounds */
#define UDISPLAY_EVENT_SLIDER_CHANGE      0x02u
#define UDISPLAY_EVENT_TOGGLE_CHANGE      0x03u
#define UDISPLAY_EVENT_TEXT_SUBMIT        0x04u
#define UDISPLAY_EVENT_SELECTION_CHANGE   0x05u  /**< Dropdown selection changed */
#define UDISPLAY_EVENT_BUTTON_PRESS       0x06u  /**< Button pressed down */
#define UDISPLAY_EVENT_BUTTON_RELEASE     0x07u  /**< Button released */
/* v1.7 deprecated alias — remove in v2 */
#define UDISPLAY_EVENT_BUTTON_PRESS_LEGACY 0x01u

/* Property IDs */
#define UDISPLAY_PROP_ENABLED   0x01u
#define UDISPLAY_PROP_VISIBLE   0x02u
#define UDISPLAY_PROP_MODE      0x03u
#define UDISPLAY_PROP_STYLE     0x04u

/* ── Types ───────────────────────────────────────────────────────────────── */

/**
 * Transport mode. Controls how the library frames outbound messages and how
 * inbound data should be fed (see udisplay_feed).
 *
 * UDISPLAY_TRANSPORT_NONE (0) — no auto-framing; send callback receives raw
 *   protocol bytes. Use in unit tests or when the firmware handles framing.
 * UDISPLAY_TRANSPORT_BLE  (2) — outbound messages are BLE-fragmented using the
 *   v2.2 offset+packet_id scheme; send callback is called once per ATT fragment.
 * UDISPLAY_TRANSPORT_TCP  (1) — outbound messages are prepended with a u16_le
 *   length header; send callback is called once per framed message.
 */
typedef enum {
    UDISPLAY_TRANSPORT_NONE = 0,
    UDISPLAY_TRANSPORT_TCP  = 1,
    UDISPLAY_TRANSPORT_BLE  = 2,
} udisplay_transport_t;

/** Event received from the client (user interacted with a widget). */
typedef struct {
    uint8_t  widget_id;    /**< Which widget generated the event */
    uint8_t  event_type;   /**< UDISPLAY_EVENT_* constant */
    union {
        float   slider_value;     /**< EVENT_SLIDER_CHANGE */
        uint8_t toggle_state;     /**< EVENT_TOGGLE_CHANGE (0=off, 1=on) */
        uint8_t selection_index;  /**< EVENT_SELECTION_CHANGE — 0-based item index */
        struct {
            const char* str;    /**< EVENT_TEXT_SUBMIT — valid only during callback */
            uint8_t     len;
        } text;
    };
} udisplay_event_t;

/**
 * Transport send function. Called by the library to transmit a complete
 * (already-framed) message. The implementation writes to the BLE
 * characteristic or TCP socket.
 */
typedef void (*udisplay_send_fn)(const uint8_t* data, uint16_t len, void* userdata);

/**
 * Event callback. Called when the client sends an EVENT message.
 * The event pointer is valid only for the duration of the callback.
 */
typedef void (*udisplay_event_fn)(const udisplay_event_t* event, void* userdata);

/** Called once when the client transitions to active state (first CLIENT_READY per connection). */
typedef void (*udisplay_ready_fn)(void* userdata);

/**
 * Called when the comms watchdog detects UDISPLAY_HB_MISS_MAX consecutive
 * misses — either missed HEARTBEAT echoes after the client is active, or a
 * stalled bootstrap (no HANDSHAKE_ACK / CLIENT_READY / chunk request) before
 * the client ever became active. The firmware's response in both cases is
 * typically the same: tear down the connection so the client can retry.
 */
typedef void (*udisplay_error_fn)(void* userdata);

/** Library configuration. Filled by the firmware using generated udisplay_ui.h. */
typedef struct {
    const uint8_t*  merkle_root;    /**< 32-byte Merkle root (UDISPLAY_MERKLE_ROOT) */
    const uint8_t* const* chunks;         /**< Chunk data pointers (UDISPLAY_CHUNKS) */
    const uint8_t* const* chunk_hashes;   /**< Per-chunk SHA-256 hashes (UDISPLAY_CHUNK_HASHES) */
    const uint16_t* chunk_lens;     /**< Actual byte count per chunk (UDISPLAY_CHUNK_LENS) */
    uint16_t        chunk_count;    /**< Total chunk count (UDISPLAY_CHUNK_COUNT) */
    udisplay_send_fn   send;            /**< Transport send callback */
    udisplay_event_fn  on_event;        /**< Widget event callback */
    udisplay_ready_fn  on_client_ready; /**< Called once on 0→1 active transition */
    udisplay_error_fn  on_comms_error;  /**< Called when watchdog fires (3 missed echoes) */
    void*              userdata;        /**< Passed through to callbacks */

    /* ── Optional authentication (proto 0x04+) ─────────────────────────────
     * Set auth_algo = UDISPLAY_AUTH_HMAC_SHA256 to require clients to prove knowledge
     * of a shared secret before bootstrap begins.  Leave as UDISPLAY_AUTH_NONE
     * (default zero-initialised value) to disable auth entirely.            */

    /** Hash algorithm to use. UDISPLAY_AUTH_NONE (0) disables auth. */
    uint8_t auth_algo;

    /**
     * Verify a client credential. Called when a HANDSHAKE_ACK(flags=1) arrives.
     * @param credential  32-byte value: HMAC-SHA256(key=password, message=salt)
     * @param hash_len    always 32
     * @param salt        the 32-byte challenge salt used for this attempt
     * @param userdata    opaque pointer from udisplay_config_t
     * @return  1 = accepted (device sends HANDSHAKE flags=0 with merkle_root)
     *          0 = rejected (device re-issues HANDSHAKE flags=1 with a fresh salt)
     *         -1 = disconnect (library clears connection state; transport close
     *              is the firmware's responsibility via its own transport API)
     *
     * The credential and salt pointers are valid only during this callback.
     * MUST NOT call udisplay_on_disconnect() from within this callback.
     * NULL when auth_algo == UDISPLAY_AUTH_NONE.
     */
    int (*auth_check)(const uint8_t* credential, uint8_t hash_len,
                      const uint8_t* salt, void* userdata);

    /**
     * Fill buf with len cryptographically random bytes. Required when
     * auth_algo != UDISPLAY_AUTH_NONE. If NULL a deterministic fallback is
     * used — insecure; firmware MUST provide a real PRNG (ESP-IDF: esp_fill_random).
     */
    void (*fill_random)(uint8_t* buf, uint8_t len, void* userdata);

    /* ── Transport framing ───────────────────────────────────────────────────
     * Set transport to select automatic framing. Defaults to
     * UDISPLAY_TRANSPORT_NONE (0) for backward compatibility.              */

    /** Transport mode. Selects automatic outbound framing. */
    udisplay_transport_t transport;

    /**
     * BLE ATT data capacity per fragment in bytes (ATT payload minus 3 ATT
     * header bytes). Ignored when transport != UDISPLAY_TRANSPORT_BLE.
     * Defaults to UDISPLAY_BLE_MTU_PAYLOAD_DEFAULT (20) when set to 0.
     * Call udisplay_ble_set_mtu() after MTU negotiation to update at runtime.
     */
    uint16_t ble_mtu_payload;
} udisplay_config_t;

/* ── Lifecycle ───────────────────────────────────────────────────────────── */

/** Initialise the library. Must be called once before any other function. */
void udisplay_init(const udisplay_config_t* cfg);

/**
 * Call when a client connects. Immediately transmits a HANDSHAKE message
 * via the configured send callback.
 */
void udisplay_on_connect(void);

/** Call when the client disconnects. Resets bootstrap state. */
void udisplay_on_disconnect(void);

/* ── Transport-aware inbound feed ────────────────────────────────────────── */

/**
 * Feed raw inbound transport bytes into the library. Dispatches on the
 * transport configured via udisplay_init() (mirrors the outbound dispatch
 * in framed_send_raw()):
 *   UDISPLAY_TRANSPORT_BLE  — routed to udisplay_ble_feed() (v2.2 offset+
 *     packet_id reassembly).
 *   UDISPLAY_TRANSPORT_TCP  — internally buffers and reassembles u16_le
 *     length-prefixed frames (partial frames carry over between calls;
 *     multiple complete frames in one call are drained in a loop), then
 *     calls udisplay_on_message() per complete frame.
 *   UDISPLAY_TRANSPORT_NONE — @p data is treated as one already-complete,
 *     already-unframed message and passed straight to udisplay_on_message().
 *
 * Call this from your transport's receive callback (BLE GATT write on the
 * `control` characteristic, or TCP socket read) regardless of which
 * transport is configured — this is the one entry point firmware needs.
 */
void udisplay_feed(const uint8_t* data, uint16_t len);

/* ── BLE transport ───────────────────────────────────────────────────────── */

/**
 * Feed a raw BLE ATT notification into the library. Internally reassembles
 * fragmented messages (v2.2 offset+packet_id scheme) and calls
 * udisplay_on_message() when a complete message is ready.
 *
 * Lower-level primitive — most firmware should call udisplay_feed() instead
 * and let it dispatch here based on the configured transport.
 */
void udisplay_ble_feed(const uint8_t* att_payload, uint16_t len);

/**
 * Update the BLE ATT MTU payload size after MTU negotiation.
 * @p mtu_payload = negotiated ATT MTU minus 3 (ATT header bytes).
 * Call this from your BLE_GAP_EVENT_MTU handler for optimal throughput.
 * @return 1 if accepted, 0 if @p mtu_payload is < 7 (too small to carry even
 *         a minimal fragment) or exceeds the library's fragment buffer
 *         capacity (517 bytes — the BLE 5.0 max ATT_MTU payload) — in
 *         either rejection case the previous value is retained unchanged.
 */
int udisplay_ble_set_mtu(uint16_t mtu_payload);

/* ── Raw receive (tests / TRANSPORT_NONE) ────────────────────────────────── */

/**
 * Feed a complete (already-unframed) message directly.
 * Use this for TRANSPORT_NONE (unit tests, MockTransport pattern) or when
 * the firmware handles framing itself before calling this function.
 */
void udisplay_on_message(const uint8_t* msg, uint16_t len);

/* ── Heartbeat ───────────────────────────────────────────────────────────── */

/**
 * Send a HEARTBEAT message. Call from a periodic timer (recommended: every 5s).
 * Ignored if no client is connected.
 */
void udisplay_heartbeat(void);

/* ── State update senders ───────────────────────────────────────────────── */

/** Send a float32 value for a widget. */
void udisplay_send_float(uint8_t widget_id, float value);

/** Send an int32 value for a widget. */
void udisplay_send_int(uint8_t widget_id, int32_t value);

/** Send a boolean (uint8) value for a widget. */
void udisplay_send_bool(uint8_t widget_id, uint8_t value);

/** Send a uint8 value for a widget (e.g. dropdown selection index). */
void udisplay_send_uint8(uint8_t widget_id, uint8_t value);

/** Send a string value for a widget. @p len must be ≤ 255. */
void udisplay_send_string(uint8_t widget_id, const char* str, uint8_t len);

/* ── Property commands ──────────────────────────────────────────────────── */

/**
 * Send SET_PROPERTY command to the client.
 * Overrides a runtime property of widget @p target_id with a uint8 value.
 */
void udisplay_set_property(uint8_t target_id, uint8_t property_id,
                            uint8_t value);

/**
 * Send RESET_PROPERTY command to the client.
 * Restores widget @p target_id's @p property_id to its YAML-defined default.
 */
void udisplay_reset_property(uint8_t target_id, uint8_t property_id);

/* ── BLE framing utilities (exposed for transport adapters and tests) ─────── */

/**
 * Fragment @p msg_len bytes from @p msg into BLE ATT notifications using the
 * v2.2 offset+packet_id scheme. Each notification fits within @p mtu_payload
 * bytes (ATT payload = negotiated_mtu - 3). @p packet_id is embedded in every
 * fragment header; the caller is responsible for incrementing it per message.
 *
 * @p frag_buf must be at least @p mtu_payload bytes. The caller owns the buffer
 * (pass a stack or static allocation; the library provides s.tx_buf.ble_frag).
 *
 * Calls @p emit once per fragment. @p userdata is forwarded to @p emit.
 * Minimum effective @p mtu_payload is 7 (6-byte header + 1 payload byte).
 */
void udisplay_ble_fragment(const uint8_t* msg, uint16_t msg_len,
                            uint16_t mtu_payload, uint8_t packet_id,
                            uint8_t* frag_buf, uint16_t frag_buf_cap,
                            void (*emit)(const uint8_t* frag, uint16_t frag_len, void* ud),
                            void* userdata);

/* ── TCP framing utilities ──────────────────────────────────────────────── */

/**
 * Prepend a u16_le length prefix to @p msg and write the framed message to
 * @p out (which must have capacity @p out_cap ≥ msg_len + 2).
 * Returns total bytes written (msg_len + 2), or 0 if @p out_cap is too small.
 */
uint16_t udisplay_tcp_frame(uint8_t* out, uint16_t out_cap,
                             const uint8_t* msg, uint16_t msg_len);

/**
 * Parse a u16_le length-prefixed TCP buffer.
 * Returns true and sets *msg_out / *msg_len_out on success.
 * Returns false if @p buf_len < 2 or < 2 + declared payload length.
 */
int udisplay_tcp_unframe(const uint8_t* buf, uint16_t buf_len,
                          const uint8_t** msg_out, uint16_t* msg_len_out);

#ifdef __cplusplus
}
#endif
