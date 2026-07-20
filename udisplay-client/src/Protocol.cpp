// SPDX-License-Identifier: LGPL-3.0-only
// Copyright (c) 2026 Attila Agas

#include "Protocol.h"
#include <cstring>

namespace Proto {

/* ── LE helpers ─────────────────────────────────────────────────────────── */

static void putU16le(uint8_t* p, uint16_t v)
{
    p[0] = static_cast<uint8_t>(v & 0xFF);
    p[1] = static_cast<uint8_t>((v >> 8) & 0xFF);
}

static void putU32le(uint8_t* p, uint32_t v)
{
    p[0] = static_cast<uint8_t>(v & 0xFF);
    p[1] = static_cast<uint8_t>((v >> 8) & 0xFF);
    p[2] = static_cast<uint8_t>((v >> 16) & 0xFF);
    p[3] = static_cast<uint8_t>((v >> 24) & 0xFF);
}

static uint16_t getU16le(const uint8_t* p)
{
    return static_cast<uint16_t>(p[0]) | (static_cast<uint16_t>(p[1]) << 8);
}

static uint32_t getU32le(const uint8_t* p)
{
    return static_cast<uint32_t>(p[0])
         | (static_cast<uint32_t>(p[1]) << 8)
         | (static_cast<uint32_t>(p[2]) << 16)
         | (static_cast<uint32_t>(p[3]) << 24);
}

/* ── Encode ─────────────────────────────────────────────────────────────── */

QByteArray encodeHandshakeAck()
{
    /* [0x01, client_proto_max, flags=0x00] = 3 bytes (proto 0x04+) */
    uint8_t buf[3] = { MSG_HANDSHAKE_ACK, PROTO_VERSION, 0x00 };
    return QByteArray(reinterpret_cast<char*>(buf), 3);
}

QByteArray encodeHandshakeAckAuth(const QByteArray& credential)
{
    /* [0x01, client_proto_max, flags=0x01, credential[32]] = 35 bytes */
    QByteArray out;
    out.reserve(35);
    out.append(static_cast<char>(MSG_HANDSHAKE_ACK));
    out.append(static_cast<char>(PROTO_VERSION));
    out.append(static_cast<char>(0x01));
    QByteArray cred = credential.left(32);
    cred.append(32 - cred.size(), '\0'); /* zero-pad if short */
    out.append(cred);
    return out;
}

QByteArray encodeClientReady()
{
    uint8_t b = MSG_CLIENT_READY;
    return QByteArray(reinterpret_cast<char*>(&b), 1);
}

QByteArray encodeHeartbeat()
{
    uint8_t b = MSG_HEARTBEAT;
    return QByteArray(reinterpret_cast<char*>(&b), 1);
}

QByteArray encodeChunkHeaderRequest(uint16_t index)
{
    /* [0x10, idx_lo, idx_hi] */
    uint8_t buf[3];
    buf[0] = MSG_CHUNK_HEADER_REQUEST;
    putU16le(buf + 1, index);
    return QByteArray(reinterpret_cast<char*>(buf), 3);
}

QByteArray encodeChunkRequest(uint16_t index)
{
    /* [0x20, idx_lo, idx_hi] */
    uint8_t buf[3];
    buf[0] = MSG_CHUNK_REQUEST;
    putU16le(buf + 1, index);
    return QByteArray(reinterpret_cast<char*>(buf), 3);
}

QByteArray encodeEventButtonClick(uint8_t widgetId)
{
    uint8_t buf[3] = { MSG_EVENT, widgetId, EVT_BUTTON_CLICK };
    return QByteArray(reinterpret_cast<char*>(buf), 3);
}

QByteArray encodeEventButtonPress(uint8_t widgetId)
{
    uint8_t buf[3] = { MSG_EVENT, widgetId, EVT_BUTTON_PRESS };
    return QByteArray(reinterpret_cast<char*>(buf), 3);
}

QByteArray encodeEventButtonRelease(uint8_t widgetId)
{
    uint8_t buf[3] = { MSG_EVENT, widgetId, EVT_BUTTON_RELEASE };
    return QByteArray(reinterpret_cast<char*>(buf), 3);
}

QByteArray encodeEventSliderChange(uint8_t widgetId, float value)
{
    /* [0x31, wid, 0x02, f32_le...] */
    uint8_t buf[7];
    buf[0] = MSG_EVENT;
    buf[1] = widgetId;
    buf[2] = EVT_SLIDER_CHANGE;
    uint32_t bits;
    memcpy(&bits, &value, 4);
    putU32le(buf + 3, bits);
    return QByteArray(reinterpret_cast<char*>(buf), 7);
}

QByteArray encodeEventToggleChange(uint8_t widgetId, uint8_t state)
{
    /* [0x31, wid, 0x03, state] */
    uint8_t buf[4] = { MSG_EVENT, widgetId, EVT_TOGGLE_CHANGE, state };
    return QByteArray(reinterpret_cast<char*>(buf), 4);
}

QByteArray encodeEventTextSubmit(uint8_t widgetId, const QString& text)
{
    /* [0x31, wid, 0x04, len_u8, utf8...] — max 255 chars */
    QByteArray utf8 = text.toUtf8();
    if (utf8.size() > 255) utf8.truncate(255);
    uint8_t len = static_cast<uint8_t>(utf8.size());
    QByteArray out;
    out.reserve(4 + len);
    out.append(static_cast<char>(MSG_EVENT));
    out.append(static_cast<char>(widgetId));
    out.append(static_cast<char>(EVT_TEXT_SUBMIT));
    out.append(static_cast<char>(len));
    out.append(utf8);
    return out;
}

QByteArray encodeEventSelectionChange(uint8_t widgetId, uint8_t index)
{
    /* [0x31, wid, 0x05, index_u8] */
    uint8_t buf[4] = { MSG_EVENT, widgetId, EVT_SELECTION_CHANGE, index };
    return QByteArray(reinterpret_cast<char*>(buf), 4);
}

/* ── Parse helpers ──────────────────────────────────────────────────────── */

uint8_t peekType(const QByteArray& msg)
{
    if (msg.isEmpty()) return 0xFE; /* sentinel: empty */
    return static_cast<uint8_t>(msg[0]);
}

bool parseHandshake(const QByteArray& msg, Handshake& out)
{
    if (msg.size() < 2) return false;
    if (static_cast<uint8_t>(msg[0]) != MSG_HANDSHAKE) return false;

    out.protoVersion = static_cast<uint8_t>(msg[1]);
    out.flags        = 0x00;
    const auto* p    = reinterpret_cast<const uint8_t*>(msg.constData());

    if (out.protoVersion < 0x04u) {
        /* Legacy 38-byte format: no flags byte, merkle at offset 2 */
        if (msg.size() < 38) return false;
        out.merkleRoot = msg.mid(2, 32);
        out.chunkCount = getU16le(p + 34);
        out.chunkSize  = getU16le(p + 36);
        return true;
    }

    /* Proto 0x04+: flags byte at offset 2 */
    if (msg.size() < 3) return false;
    out.flags = p[2];

    if (out.flags == 0x00u) {
        /* No-auth handshake: [msg_type, version, flags=0, root[32], count u16, size u16] = 39 bytes */
        if (msg.size() < 39) return false;
        out.merkleRoot = msg.mid(3, 32);
        out.chunkCount = getU16le(p + 35);
        out.chunkSize  = getU16le(p + 37);
        return true;
    }

    if (out.flags == 0x01u) {
        /* Auth challenge: [msg_type, version, flags=1, algo, salt[32]] = 36 bytes */
        if (msg.size() < 36) return false;
        out.authAlgorithm = p[3];
        out.authSalt      = msg.mid(4, 32);
        return true;
    }

    return false; /* unknown flags value */
}

bool parseChunkHeaderResponse(const QByteArray& msg, ChunkHeaderResponse& out)
{
    /* [0x11, hash[32], len_byte] = 34 bytes */
    if (msg.size() < 34) return false;
    if (static_cast<uint8_t>(msg[0]) != MSG_CHUNK_HEADER_RESPONSE) return false;
    out.hash    = msg.mid(1, 32);
    out.lenByte = static_cast<uint8_t>(msg[33]);
    return true;
}

bool parseChunkResponse(const QByteArray& msg, ChunkResponse& out)
{
    /* [0x21, idx_lo, idx_hi, len_lo, len_hi, data...] = 5-byte header */
    if (msg.size() < 5) return false;
    if (static_cast<uint8_t>(msg[0]) != MSG_CHUNK_RESPONSE) return false;
    const auto* p = reinterpret_cast<const uint8_t*>(msg.constData());
    out.index      = getU16le(p + 1);
    uint16_t len   = getU16le(p + 3);
    if (msg.size() < 5 + len) return false;
    out.data = msg.mid(5, len);
    return true;
}

bool parseErrInvalidChunk(const QByteArray& msg, ErrInvalidChunk& out)
{
    /* [0xFF, idx_lo, idx_hi] */
    if (msg.size() < 3) return false;
    if (static_cast<uint8_t>(msg[0]) != MSG_ERR_INVALID_CHUNK) return false;
    const auto* p = reinterpret_cast<const uint8_t*>(msg.constData());
    out.index = getU16le(p + 1);
    return true;
}

bool parseStateUpdate(const QByteArray& msg, StateUpdateData& out)
{
    /* Minimum: [0x30, wid, type] = 3 bytes */
    if (msg.size() < 3) return false;
    if (static_cast<uint8_t>(msg[0]) != MSG_STATE_UPDATE) return false;

    const auto* p = reinterpret_cast<const uint8_t*>(msg.constData());
    out.widgetId  = p[1];
    if (out.widgetId < USER_WIDGET_ID_MIN) return false; /* system IDs reserved */
    out.valueType = p[2];

    switch (out.valueType) {
    case VAL_FLOAT32:
        if (msg.size() < 7) return false;
        {
            uint32_t bits = getU32le(p + 3);
            memcpy(&out.f32Value, &bits, 4);
        }
        break;
    case VAL_INT32:
        if (msg.size() < 7) return false;
        out.i32Value = static_cast<int32_t>(getU32le(p + 3));
        break;
    case VAL_UINT8:
        if (msg.size() < 4) return false;
        out.u8Value = p[3];
        break;
    case VAL_STRING: {
        if (msg.size() < 4) return false;
        uint8_t len = p[3];
        if (msg.size() < 4 + len) return false;
        out.strValue = QString::fromUtf8(msg.constData() + 4, len);
        break;
    }
    default:
        return false;
    }
    return true;
}

bool parseSetProperty(const QByteArray& msg, PropertyCommand& out)
{
    /* [0x32, target_id, prop_id, value] = 4 bytes */
    if (msg.size() < 4) return false;
    if (static_cast<uint8_t>(msg[0]) != MSG_SET_PROPERTY) return false;
    const auto* p  = reinterpret_cast<const uint8_t*>(msg.constData());
    out.targetId   = p[1];
    if (out.targetId < USER_WIDGET_ID_MIN) return false; /* system IDs reserved */
    out.propertyId = p[2];
    if (out.propertyId < PROP_ENABLED || out.propertyId > PROP_STYLE) return false;
    out.value      = p[3];
    out.isSet      = true;
    return true;
}

bool parseResetProperty(const QByteArray& msg, PropertyCommand& out)
{
    /* [0x33, target_id, prop_id] = 3 bytes */
    if (msg.size() < 3) return false;
    if (static_cast<uint8_t>(msg[0]) != MSG_RESET_PROPERTY) return false;
    const auto* p  = reinterpret_cast<const uint8_t*>(msg.constData());
    out.targetId   = p[1];
    if (out.targetId < USER_WIDGET_ID_MIN) return false; /* system IDs reserved */
    out.propertyId = p[2];
    if (out.propertyId < PROP_ENABLED || out.propertyId > PROP_STYLE) return false;
    out.value      = 0;
    out.isSet      = false;
    return true;
}

/* ── TCP framing ────────────────────────────────────────────────────────── */

QByteArray tcpFrame(const QByteArray& msg)
{
    Q_ASSERT(msg.size() <= 1024); /* protocol spec: max framed message is 1024 bytes */
    uint16_t len = static_cast<uint16_t>(
        qMin(msg.size(), static_cast<int>(0xFFFF)));
    uint8_t prefix[2];
    putU16le(prefix, len);
    QByteArray out;
    out.reserve(2 + len);
    out.append(reinterpret_cast<char*>(prefix), 2);
    out.append(msg.constData(), len);
    return out;
}

bool tcpUnframe(const QByteArray& buf, QByteArray& msg, int& consumed)
{
    if (buf.size() < 2) return false;
    const auto* p = reinterpret_cast<const uint8_t*>(buf.constData());
    uint16_t len = getU16le(p);
    if (static_cast<uint32_t>(buf.size()) < static_cast<uint32_t>(2 + len))
        return false;
    msg      = buf.mid(2, len);
    consumed = 2 + len;
    return true;
}

/* ── BLE GATT framing ───────────────────────────────────────────────────── */
/* Implemented as part of TODO-011 (BleTransport). The spec is in            */
/* docs/protocol.md § BLE GATT. Tests live in test_protocol.cpp.            */

QVector<QByteArray> bleFrame(const QByteArray& msg, uint8_t attPayloadSize,
                              uint8_t& packetId)
{
    Q_ASSERT(attPayloadSize >= 7); /* minimum: 6-byte first-fragment header + 1 payload byte */
    Q_ASSERT(msg.size() > 0);
    Q_ASSERT(msg.size() <= static_cast<int>(UDISPLAY_MAX_MSG_SIZE));

    packetId = static_cast<uint8_t>(packetId + 1); /* wrap 255 → 0 is automatic for u8 */

    QVector<QByteArray> packets;
    uint16_t total    = static_cast<uint16_t>(msg.size());
    uint16_t offset   = 0;

    while (offset < total) {
        QByteArray pkt;
        if (offset == 0) {
            /* First fragment: [u16 offset=0][u8 packet_id][u16 length][u8 flags=0x00][payload] */
            uint8_t hdr[6];
            putU16le(hdr + 0, 0);
            hdr[2] = packetId;
            putU16le(hdr + 3, total);
            hdr[5] = 0x00;
            int payloadBytes = qMin(static_cast<int>(attPayloadSize) - 6,
                                    static_cast<int>(total));
            pkt.reserve(6 + payloadBytes);
            pkt.append(reinterpret_cast<char*>(hdr), 6);
            pkt.append(msg.constData(), payloadBytes);
            offset += static_cast<uint16_t>(payloadBytes);
        } else {
            /* Continuation fragment: [u16 offset][u8 packet_id][payload] */
            uint8_t hdr[3];
            putU16le(hdr + 0, offset);
            hdr[2] = packetId;
            int payloadBytes = qMin(static_cast<int>(attPayloadSize) - 3,
                                    static_cast<int>(total - offset));
            pkt.reserve(3 + payloadBytes);
            pkt.append(reinterpret_cast<char*>(hdr), 3);
            pkt.append(msg.constData() + offset, payloadBytes);
            offset += static_cast<uint16_t>(payloadBytes);
        }
        packets.append(pkt);
    }
    return packets;
}

bool bleUnframe(const QByteArray& attPkt, BleRxState& state, QByteArray& out)
{
    const auto* p = reinterpret_cast<const uint8_t*>(attPkt.constData());
    int sz = attPkt.size();

    if (sz < 3) return false; /* too short for any valid fragment header */

    uint16_t offset = getU16le(p);

    if (offset == 0) {
        /* First fragment requires 6-byte header */
        if (sz < 6) return false;
        uint8_t pktId  = p[2];
        uint16_t length = getU16le(p + 3);
        uint8_t  flags  = p[5];

        if (flags != 0x00)                               return false; /* D1: reserved flags */
        if (length == 0 || length > UDISPLAY_MAX_MSG_SIZE) return false; /* D10: cap at 1024 */

        int payloadBytes = sz - 6;
        if (payloadBytes < 0 || static_cast<uint32_t>(payloadBytes) > length) return false;

        /* Start fresh reassembly */
        state.buf.clear();
        state.buf.reserve(length);
        state.buf.append(attPkt.constData() + 6, payloadBytes);
        state.length   = length;
        state.packetId = pktId;
        state.active   = true;

        if (static_cast<uint16_t>(payloadBytes) == length) {
            out = state.buf;
            state.active = false;
            return true;
        }
        return false;
    }

    /* Continuation fragment */
    if (!state.active)                       return false; /* no in-flight message */
    if (p[2] != state.packetId)              { state.active = false; return false; } /* wrong id */

    uint16_t expected = static_cast<uint16_t>(state.buf.size());
    if (offset != expected)                  { state.active = false; return false; } /* bad offset */

    int payloadBytes = sz - 3;
    if (payloadBytes <= 0)                   { state.active = false; return false; }

    uint32_t accumulated = static_cast<uint32_t>(state.buf.size()) +
                           static_cast<uint32_t>(payloadBytes);
    if (accumulated > static_cast<uint32_t>(state.length)) {
        state.active = false;
        return false; /* over-completion */
    }

    state.buf.append(attPkt.constData() + 3, payloadBytes);

    if (static_cast<uint16_t>(state.buf.size()) == state.length) {
        out = state.buf;
        state.active = false;
        return true;
    }
    return false;
}

} // namespace Proto
