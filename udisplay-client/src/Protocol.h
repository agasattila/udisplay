// SPDX-License-Identifier: LGPL-3.0-only
// Copyright (c) 2026 Attila Agas

/**
 * uDisplay client-side protocol encode / decode.
 *
 * Pure namespace — no QObject.  All encode functions return QByteArray.
 * All parse functions take const QByteArray& and populate an out-struct,
 * returning false on truncation or format errors.
 *
 * Wire format authority: tests/protocol_vectors.json.
 * CHUNK_RESPONSE has a 5-byte header (type + idx u16 + len u16),
 * matching libudisplay — the protocol.md 3-byte description is outdated.
 */
#pragma once

#include <QByteArray>
#include <QString>
#include <QVector>
#include <cstdint>

namespace Proto {

/* ── Message type bytes ─────────────────────────────────────────────────── */
constexpr uint8_t MSG_HANDSHAKE          = 0x00;
constexpr uint8_t MSG_HANDSHAKE_ACK      = 0x01;
constexpr uint8_t MSG_CLIENT_READY       = 0x02;
constexpr uint8_t MSG_CHUNK_HEADER_REQUEST  = 0x10;
constexpr uint8_t MSG_CHUNK_HEADER_RESPONSE = 0x11;
constexpr uint8_t MSG_CHUNK_REQUEST      = 0x20;
constexpr uint8_t MSG_CHUNK_RESPONSE     = 0x21;
constexpr uint8_t MSG_STATE_UPDATE       = 0x30;
constexpr uint8_t MSG_EVENT              = 0x31;
constexpr uint8_t MSG_SET_PROPERTY       = 0x32;
constexpr uint8_t MSG_RESET_PROPERTY     = 0x33;
constexpr uint8_t MSG_HEARTBEAT          = 0x40;
constexpr uint8_t MSG_ERR_INVALID_CHUNK  = 0xFF;

/* ── Value types ────────────────────────────────────────────────────────── */
constexpr uint8_t VAL_FLOAT32 = 0x01;
constexpr uint8_t VAL_INT32   = 0x02;
constexpr uint8_t VAL_UINT8   = 0x03;
constexpr uint8_t VAL_STRING  = 0x04;

/* ── Event types ────────────────────────────────────────────────────────── */
constexpr uint8_t EVT_BUTTON_CLICK      = 0x01;  /* complete tap */
constexpr uint8_t EVT_SLIDER_CHANGE     = 0x02;
constexpr uint8_t EVT_TOGGLE_CHANGE     = 0x03;
constexpr uint8_t EVT_TEXT_SUBMIT       = 0x04;
constexpr uint8_t EVT_SELECTION_CHANGE  = 0x05;
constexpr uint8_t EVT_BUTTON_PRESS      = 0x06;  /* pressed down */
constexpr uint8_t EVT_BUTTON_RELEASE    = 0x07;  /* released */

/* ── Property IDs ───────────────────────────────────────────────────────── */
constexpr uint8_t PROP_ENABLED = 0x01;
constexpr uint8_t PROP_VISIBLE = 0x02;
constexpr uint8_t PROP_MODE    = 0x03;
constexpr uint8_t PROP_STYLE   = 0x04;

/* ── Protocol version ───────────────────────────────────────────────────── */
constexpr uint8_t PROTO_VERSION = 0x04;

/* ── Auth algorithm IDs ─────────────────────────────────────────────────── */
constexpr uint8_t AUTH_NONE   = 0x00;
constexpr uint8_t AUTH_HMAC_SHA256 = 0x01;

/* ── Widget ID boundaries ────────────────────────────────────────────────── */
constexpr uint8_t SYSTEM_WIDGET_ID_MAX = 0x0F;  /* 0x00–0x0F: system   */
constexpr uint8_t USER_WIDGET_ID_MIN   = 0x10;  /* 0x10–0xFF: user     */

/* ── Chunk size (v1) ─────────────────────────────────────────────────────── */
constexpr uint16_t CHUNK_SIZE = 256;

/* ── mDNS/DNS-SD service type ────────────────────────────────────────────── */
/* ESP32 libudisplay must advertise this type for mDNS discovery to work.
 * Service instance name becomes the DeviceInfo::uniqueId in DiscoveryModel. */
constexpr char kMdnsServiceType[] = "_udisplay._tcp";

/* ══════════════════════════════════════════════════════════════════════════
 *  Decoded message structs
 * ══════════════════════════════════════════════════════════════════════════ */

struct Handshake {
    uint8_t    protoVersion;
    uint8_t    flags;           /* 0x00 = no-auth, 0x01 = auth challenge */
    /* flags=0x00: normal bootstrap fields */
    QByteArray merkleRoot;      /* 32 bytes; only valid when flags=0x00 */
    uint16_t   chunkCount;
    uint16_t   chunkSize;
    /* flags=0x01: auth challenge fields */
    uint8_t    authAlgorithm;   /* AUTH_HMAC_SHA256=0x01; only valid when flags=0x01 */
    QByteArray authSalt;        /* 32 bytes; only valid when flags=0x01 */
};

struct ChunkHeaderResponse {
    QByteArray hash;    /* always 32 bytes */
    uint8_t    lenByte; /* 0 = full 256-byte chunk; 1-255 = partial last chunk */
};

struct ChunkResponse {
    uint16_t   index;
    QByteArray data;
};

struct ErrInvalidChunk {
    uint16_t index;
};

struct StateUpdateData {
    uint8_t widgetId;
    uint8_t valueType;
    /* One of these is valid, depending on valueType: */
    float   f32Value;
    int32_t i32Value;
    uint8_t u8Value;
    QString strValue;
};

struct PropertyCommand {
    uint8_t targetId;
    uint8_t propertyId;
    uint8_t value;      /* only valid for SET_PROPERTY; 0 for RESET_PROPERTY */
    bool    isSet;      /* true = SET_PROPERTY (0x32), false = RESET_PROPERTY (0x33) */
};

/* ══════════════════════════════════════════════════════════════════════════
 *  Encode — client → device
 * ══════════════════════════════════════════════════════════════════════════ */

QByteArray encodeHandshakeAck();
/** HANDSHAKE_ACK(flags=1, credential[32]): 35 bytes — auth credential response. */
QByteArray encodeHandshakeAckAuth(const QByteArray& credential);
QByteArray encodeClientReady();
QByteArray encodeHeartbeat();
QByteArray encodeChunkHeaderRequest(uint16_t index);
QByteArray encodeChunkRequest(uint16_t index);
QByteArray encodeEventButtonClick(uint8_t widgetId);
QByteArray encodeEventButtonPress(uint8_t widgetId);
QByteArray encodeEventButtonRelease(uint8_t widgetId);
QByteArray encodeEventSliderChange(uint8_t widgetId, float value);
QByteArray encodeEventToggleChange(uint8_t widgetId, uint8_t state);
QByteArray encodeEventTextSubmit(uint8_t widgetId, const QString& text);
QByteArray encodeEventSelectionChange(uint8_t widgetId, uint8_t index);

/* ══════════════════════════════════════════════════════════════════════════
 *  Parse — device → client
 *
 *  Returns false if the message is truncated or malformed.
 * ══════════════════════════════════════════════════════════════════════════ */

/** Peek the first byte of a raw message to determine type. */
uint8_t peekType(const QByteArray& msg);

/**
 * Parse HANDSHAKE (0x00).
 * Proto ≤ 0x03: 38-byte no-flags format; out.flags is set to 0x00.
 * Proto ≥ 0x04, flags=0x00: 39-byte normal bootstrap (merkleRoot/chunkCount/chunkSize valid).
 * Proto ≥ 0x04, flags=0x01: 36-byte auth challenge (authAlgorithm/authSalt valid).
 */
bool parseHandshake(const QByteArray& msg, Handshake& out);
bool parseChunkHeaderResponse(const QByteArray& msg, ChunkHeaderResponse& out);
bool parseChunkResponse(const QByteArray& msg, ChunkResponse& out);
bool parseErrInvalidChunk(const QByteArray& msg, ErrInvalidChunk& out);

/** Parse STATE_UPDATE (0x30) — widget data only. */
bool parseStateUpdate(const QByteArray& msg, StateUpdateData& out);

/** Parse SET_PROPERTY (0x32): [msg_type, target_id, prop_id, value] = 4 bytes. */
bool parseSetProperty(const QByteArray& msg, PropertyCommand& out);

/** Parse RESET_PROPERTY (0x33): [msg_type, target_id, prop_id] = 3 bytes. */
bool parseResetProperty(const QByteArray& msg, PropertyCommand& out);

/* ══════════════════════════════════════════════════════════════════════════
 *  TCP transport framing
 * ══════════════════════════════════════════════════════════════════════════ */

/** Prepend a 2-byte LE length prefix to msg. */
QByteArray tcpFrame(const QByteArray& msg);

/**
 * Attempt to unframe one message from buf.
 * Returns true and sets msg if a complete message is available.
 * Returns false if more data is needed.
 * consumed is set to the number of bytes consumed from buf.
 */
bool tcpUnframe(const QByteArray& buf, QByteArray& msg, int& consumed);

/* ══════════════════════════════════════════════════════════════════════════
 *  BLE GATT transport framing  (implemented in TODO-011 / BleTransport)
 *
 *  Fragment formats (little-endian fields):
 *    First fragment:       [u16 offset=0][u8 packet_id][u16 length][u8 flags=0x00][payload]
 *    Continuation fragment:[u16 offset  ][u8 packet_id][payload]
 *
 *  Completion: offset + fragment_payload_size == length
 *  packet_id: 8-bit, increments per message, wraps 255→0, resets to 0 on reconnect.
 *  Max message size: UDISPLAY_MAX_MSG_SIZE (1024 bytes).
 *  Minimum MTU: 7 effective bytes (6-byte first-fragment header + 1 payload byte).
 * ══════════════════════════════════════════════════════════════════════════ */

constexpr uint16_t UDISPLAY_MAX_MSG_SIZE = 1024;

/** Per-connection receiver state for BLE reassembly.  Reset on each new connection. */
struct BleRxState {
    QByteArray buf;        /**< accumulated payload bytes */
    uint16_t   length = 0; /**< total message length from first fragment */
    uint8_t    packetId = 0;
    bool       active  = false; /**< true after a valid first fragment is received */
};

/**
 * Split msg into ATT-sized fragments for transmission over one BLE characteristic.
 * attPayloadSize is the effective per-packet payload capacity (negotiated MTU − 3).
 * packetId is incremented by this function before use (caller maintains the counter
 * per characteristic; pass 0xFF to get packet_id=0 on the first call).
 * Returns one QByteArray per ATT packet to write in order.
 */
QVector<QByteArray> bleFrame(const QByteArray& msg, uint8_t attPayloadSize,
                              uint8_t& packetId);

/**
 * Feed one incoming ATT packet into the receiver state machine.
 * Returns true and sets out to the reassembled message when a complete message arrives.
 * Returns false when more fragments are needed or the fragment is discarded on error.
 * On error the state is reset; the next call with a valid first fragment starts fresh.
 */
bool bleUnframe(const QByteArray& attPkt, BleRxState& state, QByteArray& out);

} // namespace Proto
