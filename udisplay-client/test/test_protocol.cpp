/**
 * Protocol encode/decode tests.
 * All expected byte sequences taken from tests/protocol_vectors.json.
 */
#include <QtTest>
#include "Protocol.h"

static QByteArray fromHex(const char* s)
{
    return QByteArray::fromHex(QByteArray(s).replace(' ', ""));
}

class TestProtocol : public QObject
{
    Q_OBJECT

private slots:
    /* ── Encode ─────────────────────────────────────────────────── */

    void encode_HandshakeAck()
    {
        /* Expected: "01 04 00" (PROTO_VERSION=0x04, flags=0x00 no-auth) */
        QCOMPARE(Proto::encodeHandshakeAck(), fromHex("01 04 00"));
    }

    void encode_HandshakeAckAuth()
    {
        /* HANDSHAKE_ACK_AUTH: "01 04 01 credential[32]" = 35 bytes */
        QByteArray cred = fromHex(
            "21 22 23 24 25 26 27 28 29 2a 2b 2c 2d 2e 2f 30"
            "31 32 33 34 35 36 37 38 39 3a 3b 3c 3d 3e 3f 40");
        QByteArray expected = fromHex(
            "01 04 01"
            "21 22 23 24 25 26 27 28 29 2a 2b 2c 2d 2e 2f 30"
            "31 32 33 34 35 36 37 38 39 3a 3b 3c 3d 3e 3f 40");
        QCOMPARE(Proto::encodeHandshakeAckAuth(cred), expected);
    }

    void encode_ClientReady()
    {
        /* Expected: "02" — single byte CLIENT_READY */
        QCOMPARE(Proto::encodeClientReady(), fromHex("02"));
    }

    void encode_ChunkHeaderRequest_index0()
    {
        /* Expected: "10 00 00" */
        QCOMPARE(Proto::encodeChunkHeaderRequest(0), fromHex("10 00 00"));
    }

    void encode_ChunkHeaderRequest_index5()
    {
        /* Expected: "10 05 00" */
        QCOMPARE(Proto::encodeChunkHeaderRequest(5), fromHex("10 05 00"));
    }

    void encode_ChunkHeaderRequest_index256()
    {
        /* idx=256: lo=0x00, hi=0x01 → "10 00 01" */
        QCOMPARE(Proto::encodeChunkHeaderRequest(256), fromHex("10 00 01"));
    }

    void encode_ChunkRequest_index0()
    {
        /* Expected: "20 00 00" */
        QCOMPARE(Proto::encodeChunkRequest(0), fromHex("20 00 00"));
    }

    void encode_ChunkRequest_index5()
    {
        /* Expected: "20 05 00" */
        QCOMPARE(Proto::encodeChunkRequest(5), fromHex("20 05 00"));
    }

    void encode_EventButtonPress()
    {
        /* Expected: "31 10 06" (EVT_BUTTON_PRESS = 0x06) */
        QCOMPARE(Proto::encodeEventButtonPress(0x10), fromHex("31 10 06"));
    }

    void encode_EventSliderChange()
    {
        /* widget_id=0x11, value=75.0 → "31 11 02 00 00 96 42" */
        QCOMPARE(Proto::encodeEventSliderChange(0x11, 75.0f),
                 fromHex("31 11 02 00 00 96 42"));
    }

    void encode_EventToggleChange()
    {
        /* widget_id=0x12, state=1 → "31 12 03 01" */
        QCOMPARE(Proto::encodeEventToggleChange(0x12, 1),
                 fromHex("31 12 03 01"));
    }

    void encode_EventTextSubmit()
    {
        /* widget_id=0x13, "hello" → "31 13 04 05 68 65 6c 6c 6f" */
        QCOMPARE(Proto::encodeEventTextSubmit(0x13, QStringLiteral("hello")),
                 fromHex("31 13 04 05 68 65 6c 6c 6f"));
    }

    void encode_EventSelectionChange()
    {
        /* widget_id=0x14, index=2 → "31 14 05 02" */
        QCOMPARE(Proto::encodeEventSelectionChange(0x14, 2),
                 fromHex("31 14 05 02"));
    }

    void encode_EventSelectionChange_index0()
    {
        /* widget_id=0x10, index=0 → "31 10 05 00" */
        QCOMPARE(Proto::encodeEventSelectionChange(0x10, 0),
                 fromHex("31 10 05 00"));
    }

    void encode_Heartbeat()
    {
        /* Expected: "40" — single byte HEARTBEAT echo */
        QCOMPARE(Proto::encodeHeartbeat(), fromHex("40"));
    }

    /* ── TCP framing ─────────────────────────────────────────────── */

    void tcpFrame_StateFloat32()
    {
        /* Payload: STATE_UPDATE float32 = "30 10 01 c3 f5 48 40" (7 bytes)
         * Framed:  "07 00 30 10 01 c3 f5 48 40"                          */
        QByteArray payload = fromHex("30 10 01 c3 f5 48 40");
        QByteArray framed  = fromHex("07 00 30 10 01 c3 f5 48 40");
        QCOMPARE(Proto::tcpFrame(payload), framed);
    }

    void tcpFrame_LengthIsLE()
    {
        /* 256-byte payload → prefix = 00 01 (LE) */
        QByteArray payload(256, 'A');
        QByteArray framed = Proto::tcpFrame(payload);
        QCOMPARE(framed.size(), 258);
        QCOMPARE(static_cast<uint8_t>(framed[0]), uint8_t(0x00));
        QCOMPARE(static_cast<uint8_t>(framed[1]), uint8_t(0x01));
    }

    void tcpUnframe_StateFloat32()
    {
        QByteArray framed = fromHex("07 00 30 10 01 c3 f5 48 40");
        QByteArray msg;
        int consumed = 0;
        QVERIFY(Proto::tcpUnframe(framed, msg, consumed));
        QCOMPARE(consumed, 9);
        QCOMPARE(msg, fromHex("30 10 01 c3 f5 48 40"));
    }

    void tcpUnframe_Truncated()
    {
        QByteArray framed = fromHex("07 00 30 10"); /* declares 7, only 2 present */
        QByteArray msg;
        int consumed = 0;
        QVERIFY(!Proto::tcpUnframe(framed, msg, consumed));
    }

    void tcpUnframe_OnlyPrefix()
    {
        QByteArray framed = fromHex("01"); /* only 1 byte — need at least 2 */
        QByteArray msg;
        int consumed = 0;
        QVERIFY(!Proto::tcpUnframe(framed, msg, consumed));
    }

    void tcpUnframe_ZeroLength()
    {
        QByteArray framed = fromHex("00 00");
        QByteArray msg;
        int consumed = 0;
        QVERIFY(Proto::tcpUnframe(framed, msg, consumed));
        QCOMPARE(consumed, 2);
        QVERIFY(msg.isEmpty());
    }

    /* ── Parse: HANDSHAKE ────────────────────────────────────────── */

    void parse_Handshake()
    {
        /* From vectors: merkle_root = v1_tiny, chunk_count=1, chunk_size=256 */
        QByteArray msg = fromHex(
            "00 01"
            "74 ac 9f a6 84 28 7d 0c ab 3e 64 e3 f2 5e e3 69"
            "c7 4b 46 e7 f2 ec 00 25 cd 10 ba c7 bd ff db 6e"
            "01 00"   /* chunk_count = 1 LE */
            "00 01"   /* chunk_size = 256 LE */
        );
        Proto::Handshake hs;
        QVERIFY(Proto::parseHandshake(msg, hs));
        QCOMPARE(hs.protoVersion, uint8_t(1));
        QCOMPARE(hs.chunkCount, uint16_t(1));
        QCOMPARE(hs.chunkSize,  uint16_t(256));
        QCOMPARE(hs.merkleRoot,
                 fromHex("74ac9fa684287d0cab3e64e3f25ee369"
                          "c74b46e7f2ec0025cd10bac7bdffdb6e"));
    }

    void parse_Handshake_Proto04_NoAuth()
    {
        /* From vectors HANDSHAKE (proto 0x04, flags=0x00): 39 bytes */
        QByteArray msg = fromHex(
            "00 04 00"
            "74 ac 9f a6 84 28 7d 0c ab 3e 64 e3 f2 5e e3 69"
            "c7 4b 46 e7 f2 ec 00 25 cd 10 ba c7 bd ff db 6e"
            "01 00"   /* chunk_count = 1 LE */
            "00 01"   /* chunk_size = 256 LE */
        );
        Proto::Handshake hs;
        QVERIFY(Proto::parseHandshake(msg, hs));
        QCOMPARE(hs.protoVersion, uint8_t(4));
        QCOMPARE(hs.flags,        uint8_t(0));
        QCOMPARE(hs.chunkCount,   uint16_t(1));
        QCOMPARE(hs.chunkSize,    uint16_t(256));
        QCOMPARE(hs.merkleRoot,
                 fromHex("74ac9fa684287d0cab3e64e3f25ee369"
                          "c74b46e7f2ec0025cd10bac7bdffdb6e"));
    }

    void parse_Handshake_Proto04_AuthChallenge()
    {
        /* From vectors HANDSHAKE_AUTH (proto 0x04, flags=0x01): 36 bytes */
        QByteArray msg = fromHex(
            "00 04 01 01"
            "01 02 03 04 05 06 07 08 09 0a 0b 0c 0d 0e 0f 10"
            "11 12 13 14 15 16 17 18 19 1a 1b 1c 1d 1e 1f 20"
        );
        Proto::Handshake hs;
        QVERIFY(Proto::parseHandshake(msg, hs));
        QCOMPARE(hs.protoVersion,  uint8_t(4));
        QCOMPARE(hs.flags,         uint8_t(1));
        QCOMPARE(hs.authAlgorithm, uint8_t(1)); /* SHA-256 */
        QCOMPARE(hs.authSalt,
                 fromHex("0102030405060708090a0b0c0d0e0f10"
                          "1112131415161718191a1b1c1d1e1f20"));
    }

    void parse_Handshake_Truncated()
    {
        QByteArray msg = fromHex("00 01 aa bb"); /* too short */
        Proto::Handshake hs;
        QVERIFY(!Proto::parseHandshake(msg, hs));
    }

    void parse_Handshake_Proto04_MissingFlagsByte()
    {
        /* proto 0x04, only 2 bytes — flags byte absent → reject */
        QByteArray msg = fromHex("00 04");
        Proto::Handshake hs;
        QVERIFY(!Proto::parseHandshake(msg, hs));
    }

    void parse_Handshake_Proto04_NoAuth_Truncated()
    {
        /* proto 0x04, flags=0x00, only 10 bytes — needs 39 → reject */
        QByteArray msg = fromHex("00 04 00 74 ac 9f a6 84 28 7d");
        Proto::Handshake hs;
        QVERIFY(!Proto::parseHandshake(msg, hs));
    }

    void parse_Handshake_Proto04_AuthChallenge_Truncated()
    {
        /* proto 0x04, flags=0x01, only 10 bytes — needs 36 → reject */
        QByteArray msg = fromHex("00 04 01 01 aa bb cc dd ee ff");
        Proto::Handshake hs;
        QVERIFY(!Proto::parseHandshake(msg, hs));
    }

    void parse_Handshake_Proto04_UnknownFlags()
    {
        /* proto 0x04, flags=0x02 — unknown → reject */
        QByteArray msg(39, '\xAA');
        msg[0] = static_cast<char>(0x00); /* MSG_HANDSHAKE */
        msg[1] = static_cast<char>(0x04); /* proto 0x04 */
        msg[2] = static_cast<char>(0x02); /* unknown flags */
        Proto::Handshake hs;
        QVERIFY(!Proto::parseHandshake(msg, hs));
    }

    /* ── Parse: CHUNK_HEADER_RESPONSE ───────────────────────────── */

    void parse_ChunkHeaderResponse_Full()
    {
        /* From vectors: CHUNK_HEADER_RESPONSE_full — len_byte=0 (full 256-byte chunk) */
        QByteArray msg = fromHex(
            "11"
            "83 41 cf d5 9a 9f 9b 4c ed 5e 23 df 0b 06 8f 5d"
            "eb 11 20 40 f3 c4 41 b4 5a 49 95 2b 3b b9 c1 09"
            "00"
        );
        Proto::ChunkHeaderResponse chr;
        QVERIFY(Proto::parseChunkHeaderResponse(msg, chr));
        QCOMPARE(chr.hash,
                 fromHex("8341cfd59a9f9b4ced5e23df0b068f5d"
                          "eb112040f3c441b45a49952b3bb9c109"));
        QCOMPARE(chr.lenByte, uint8_t(0));
    }

    void parse_ChunkHeaderResponse_Partial()
    {
        /* From vectors: CHUNK_HEADER_RESPONSE_partial — len_byte=3 (3-byte last chunk) */
        QByteArray msg = fromHex(
            "11"
            "2b 92 cb 09 57 66 4e 01 10 9e 2e a5 a8 5e ef db"
            "81 ad 3f 76 94 0c 40 29 8e 4c b1 58 a7 f3 ca 85"
            "03"
        );
        Proto::ChunkHeaderResponse chr;
        QVERIFY(Proto::parseChunkHeaderResponse(msg, chr));
        QCOMPARE(chr.hash,
                 fromHex("2b92cb0957664e01109e2ea5a85eefdb"
                          "81ad3f76940c40298e4cb158a7f3ca85"));
        QCOMPARE(chr.lenByte, uint8_t(3));
    }

    void parse_ChunkHeaderResponse_Truncated()
    {
        /* Only 33 bytes — missing len_byte */
        QByteArray msg = fromHex(
            "11"
            "83 41 cf d5 9a 9f 9b 4c ed 5e 23 df 0b 06 8f 5d"
            "eb 11 20 40 f3 c4 41 b4 5a 49 95 2b 3b b9 c1 09"
        );
        Proto::ChunkHeaderResponse chr;
        QVERIFY(!Proto::parseChunkHeaderResponse(msg, chr));
    }

    /* ── Parse: CHUNK_RESPONSE ───────────────────────────────────── */

    void parse_ChunkResponse()
    {
        /* From vectors: chunk_index=0, len=50, data=v1 blob chunk 0 */
        QByteArray msg = fromHex(
            "21 00 00 32 00"
            "78 9c 4b 49 2d cb 4c 4e b5 e2 52 50 c8 4b cc 4d"
            "b5 52 28 e1 2a cf 4c 49 4f 2d 29 06 09 25 82 08"
            "05 85 92 ca 02 90 4c 7e 7a 7a 4e 2a 17 00 76 7e"
            "0e b6"
        );
        Proto::ChunkResponse cr;
        QVERIFY(Proto::parseChunkResponse(msg, cr));
        QCOMPARE(cr.index, uint16_t(0));
        QCOMPARE(cr.data.size(), 50);
        QCOMPARE(static_cast<uint8_t>(cr.data[0]), uint8_t(0x78));
        QCOMPARE(static_cast<uint8_t>(cr.data[1]), uint8_t(0x9c));
    }

    /* ── Parse: ERR_INVALID_CHUNK ────────────────────────────────── */

    void parse_ErrInvalidChunk()
    {
        /* "ff 05 00" — index=5 */
        QByteArray msg = fromHex("ff 05 00");
        Proto::ErrInvalidChunk err;
        QVERIFY(Proto::parseErrInvalidChunk(msg, err));
        QCOMPARE(err.index, uint16_t(5));
    }

    /* ── Parse: STATE_UPDATE ─────────────────────────────────────── */

    void parse_StateUpdate_Float32()
    {
        /* "30 10 01 c3 f5 48 40" → widget 0x10, float32, 3.14 */
        QByteArray msg = fromHex("30 10 01 c3 f5 48 40");
        Proto::StateUpdateData data{};
        QVERIFY(Proto::parseStateUpdate(msg, data));
        QCOMPARE(data.widgetId,  uint8_t(0x10));
        QCOMPARE(data.valueType, uint8_t(Proto::VAL_FLOAT32));
        QVERIFY(qAbs(data.f32Value - 3.14f) < 0.0001f);
    }

    void parse_StateUpdate_Int32()
    {
        /* "30 10 02 d6 ff ff ff" → -42 */
        QByteArray msg = fromHex("30 10 02 d6 ff ff ff");
        Proto::StateUpdateData data{};
        QVERIFY(Proto::parseStateUpdate(msg, data));
        QCOMPARE(data.i32Value, -42);
    }

    void parse_StateUpdate_Uint8()
    {
        /* "30 10 03 01" */
        QByteArray msg = fromHex("30 10 03 01");
        Proto::StateUpdateData data{};
        QVERIFY(Proto::parseStateUpdate(msg, data));
        QCOMPARE(data.u8Value, uint8_t(1));
    }

    void parse_StateUpdate_String()
    {
        /* "30 10 04 05 68 65 6c 6c 6f" → "hello" */
        QByteArray msg = fromHex("30 10 04 05 68 65 6c 6c 6f");
        Proto::StateUpdateData data{};
        QVERIFY(Proto::parseStateUpdate(msg, data));
        QCOMPARE(data.strValue, QStringLiteral("hello"));
    }

    void parse_SetProperty()
    {
        /* "32 10 01 00" → SET_PROPERTY, target=0x10, ENABLED=0 */
        QByteArray msg = fromHex("32 10 01 00");
        Proto::PropertyCommand prop{};
        QVERIFY(Proto::parseSetProperty(msg, prop));
        QVERIFY(prop.isSet);
        QCOMPARE(prop.targetId,   uint8_t(0x10));
        QCOMPARE(prop.propertyId, uint8_t(Proto::PROP_ENABLED));
        QCOMPARE(prop.value,      uint8_t(0));
    }

    void parse_ResetProperty()
    {
        /* "33 10 01" → RESET_PROPERTY, target=0x10, ENABLED */
        QByteArray msg = fromHex("33 10 01");
        Proto::PropertyCommand prop{};
        QVERIFY(Proto::parseResetProperty(msg, prop));
        QVERIFY(!prop.isSet);
        QCOMPARE(prop.targetId,   uint8_t(0x10));
        QCOMPARE(prop.propertyId, uint8_t(Proto::PROP_ENABLED));
    }

    void parse_SetProperty_Truncated()
    {
        /* Only 3 bytes — missing value byte */
        QByteArray msg = fromHex("32 10 01");
        Proto::PropertyCommand prop{};
        QVERIFY(!Proto::parseSetProperty(msg, prop));
    }

    void parse_ResetProperty_Truncated()
    {
        /* Only 2 bytes — missing prop_id */
        QByteArray msg = fromHex("33 10");
        Proto::PropertyCommand prop{};
        QVERIFY(!Proto::parseResetProperty(msg, prop));
    }

    void parse_StateUpdate_TruncatedFloat32()
    {
        QByteArray msg = fromHex("30 10 01 c3 f5"); /* only 2 of 4 value bytes */
        Proto::StateUpdateData data{};
        QVERIFY(!Proto::parseStateUpdate(msg, data));
    }

    void parse_StateUpdate_SystemWidgetId()
    {
        /* widget_id=0x00 is system-reserved; must be rejected */
        QByteArray msg = fromHex("30 00 03 01");
        Proto::StateUpdateData data{};
        QVERIFY(!Proto::parseStateUpdate(msg, data));
    }

    void parse_SetProperty_SystemTargetId()
    {
        /* target_id=0x0F is system-reserved; must be rejected */
        QByteArray msg = fromHex("32 0F 01 00");
        Proto::PropertyCommand prop{};
        QVERIFY(!Proto::parseSetProperty(msg, prop));
    }

    void parse_SetProperty_UnknownPropertyId()
    {
        /* property_id=0x05 is undefined; must be rejected */
        QByteArray msg = fromHex("32 10 05 00");
        Proto::PropertyCommand prop{};
        QVERIFY(!Proto::parseSetProperty(msg, prop));
    }

    void parse_ResetProperty_SystemTargetId()
    {
        /* target_id=0x00 is system-reserved; must be rejected */
        QByteArray msg = fromHex("33 00 01");
        Proto::PropertyCommand prop{};
        QVERIFY(!Proto::parseResetProperty(msg, prop));
    }

    void parse_ResetProperty_UnknownPropertyId()
    {
        /* property_id=0x00 is undefined; must be rejected */
        QByteArray msg = fromHex("33 10 00");
        Proto::PropertyCommand prop{};
        QVERIFY(!Proto::parseResetProperty(msg, prop));
    }

    /* ── BLE framing: bleFrame ─────────────────────────────────────────────── */

    void bleFrame_SingleFragment()
    {
        /* 3-byte message, MTU payload = 20: fits in one packet */
        QByteArray msg = fromHex("30 10 01"); /* STATE_UPDATE, widget=0x10, type=float */
        uint8_t packetId = 0xFF; /* wraps to 0 on first call */
        auto packets = Proto::bleFrame(msg, 20, packetId);
        QCOMPARE(packetId, uint8_t(0));
        QCOMPARE(packets.size(), 1);
        /* First fragment header: offset=0x0000, packet_id=0x00, length=0x0003, flags=0x00 */
        QCOMPARE(static_cast<uint8_t>(packets[0][0]), uint8_t(0x00)); /* offset lo */
        QCOMPARE(static_cast<uint8_t>(packets[0][1]), uint8_t(0x00)); /* offset hi */
        QCOMPARE(static_cast<uint8_t>(packets[0][2]), uint8_t(0x00)); /* packet_id */
        QCOMPARE(static_cast<uint8_t>(packets[0][3]), uint8_t(0x03)); /* length lo */
        QCOMPARE(static_cast<uint8_t>(packets[0][4]), uint8_t(0x00)); /* length hi */
        QCOMPARE(static_cast<uint8_t>(packets[0][5]), uint8_t(0x00)); /* flags */
        QCOMPARE(packets[0].mid(6), msg);
    }

    void bleFrame_MultiFragment()
    {
        /* 10-byte message, MTU payload = 9: first fragment carries 3 bytes (9-6),
           continuation carries remaining 7 bytes across 2 more packets */
        QByteArray msg(10, '\xAB');
        uint8_t packetId = 0;
        auto packets = Proto::bleFrame(msg, 9, packetId);
        QCOMPARE(packetId, uint8_t(1));
        /* first: header(6) + 3 payload = 9 bytes */
        QCOMPARE(packets[0].size(), 9);
        QCOMPARE(static_cast<uint8_t>(packets[0][0]), uint8_t(0x00)); /* offset=0 lo */
        QCOMPARE(static_cast<uint8_t>(packets[0][1]), uint8_t(0x00)); /* offset=0 hi */
        QCOMPARE(static_cast<uint8_t>(packets[0][3]), uint8_t(0x0A)); /* length=10 lo */
        /* continuation 1: offset=3, packet_id=1, 6 payload bytes */
        QCOMPARE(static_cast<uint8_t>(packets[1][0]), uint8_t(0x03)); /* offset=3 lo */
        QCOMPARE(static_cast<uint8_t>(packets[1][2]), uint8_t(0x01)); /* same packet_id */
        /* continuation 2: offset=9, 1 payload byte */
        QCOMPARE(static_cast<uint8_t>(packets[2][0]), uint8_t(0x09)); /* offset=9 lo */
    }

    void bleFrame_PacketIdIncrementsAndWraps()
    {
        QByteArray msg(1, '\x40');
        uint8_t packetId = 254;
        Proto::bleFrame(msg, 20, packetId);
        QCOMPARE(packetId, uint8_t(255));
        Proto::bleFrame(msg, 20, packetId);
        QCOMPARE(packetId, uint8_t(0)); /* wrap 255 → 0 */
        Proto::bleFrame(msg, 20, packetId);
        QCOMPARE(packetId, uint8_t(1));
    }

    /* ── BLE framing: bleUnframe ───────────────────────────────────────────── */

    void bleUnframe_SingleFragment()
    {
        /* 3-byte message in one ATT packet */
        QByteArray msg = fromHex("30 10 01");
        uint8_t packetId = 0xFF;
        auto packets = Proto::bleFrame(msg, 20, packetId);
        Proto::BleRxState state{};
        QByteArray out;
        QVERIFY(Proto::bleUnframe(packets[0], state, out));
        QCOMPARE(out, msg);
    }

    void bleUnframe_MultiFragment()
    {
        /* 10-byte message, MTU payload = 9 */
        QByteArray msg(10, '\xBB');
        uint8_t packetId = 0;
        auto packets = Proto::bleFrame(msg, 9, packetId);
        Proto::BleRxState state{};
        QByteArray out;
        QVERIFY(!Proto::bleUnframe(packets[0], state, out)); /* first: not done yet */
        QVERIFY(!Proto::bleUnframe(packets[1], state, out)); /* second: not done yet */
        QVERIFY(Proto::bleUnframe(packets[2], state, out));  /* third: complete */
        QCOMPARE(out, msg);
    }

    void bleUnframe_RejectNonZeroFlags()
    {
        /* First fragment with flags = 0x01 (reserved, must be 0x00) */
        QByteArray pkt(9, '\x00');
        pkt[3] = 0x05; /* length lo = 5 */
        pkt[5] = 0x01; /* flags = 0x01 — invalid */
        Proto::BleRxState state{};
        QByteArray out;
        QVERIFY(!Proto::bleUnframe(pkt, state, out));
        QVERIFY(!state.active); /* state reset */
    }

    void bleUnframe_RejectLengthOverMax()
    {
        /* First fragment claiming length = 1025 (> UDISPLAY_MAX_MSG_SIZE) */
        QByteArray pkt(9, '\x00');
        pkt[3] = 0x01; /* length lo */
        pkt[4] = 0x04; /* length hi → 0x0401 = 1025 */
        Proto::BleRxState state{};
        QByteArray out;
        QVERIFY(!Proto::bleUnframe(pkt, state, out));
        QVERIFY(!state.active);
    }

    void bleUnframe_RejectOffsetGap()
    {
        /* Send first fragment then a continuation with wrong (skipped) offset */
        QByteArray msg(20, '\xCC');
        uint8_t packetId = 0;
        auto packets = Proto::bleFrame(msg, 9, packetId);

        Proto::BleRxState state{};
        QByteArray out;
        Proto::bleUnframe(packets[0], state, out); /* prime state with first fragment */

        /* Replace offset in continuation with a larger-than-expected value */
        QByteArray badPkt = packets[2]; /* skip packets[1] — offset would jump */
        QVERIFY(!Proto::bleUnframe(badPkt, state, out));
        QVERIFY(!state.active); /* reassembly discarded */
    }

    void bleUnframe_RejectWrongPacketId()
    {
        /* Send first fragment, then a continuation with different packet_id */
        QByteArray msg(10, '\xDD');
        uint8_t packetId = 3;
        auto packets = Proto::bleFrame(msg, 9, packetId); /* packet_id = 4 */

        Proto::BleRxState state{};
        QByteArray out;
        Proto::bleUnframe(packets[0], state, out);

        /* Tamper with packet_id in continuation */
        QByteArray badPkt = packets[1];
        badPkt[2] = 0xFF; /* wrong packet_id */
        QVERIFY(!Proto::bleUnframe(badPkt, state, out));
        QVERIFY(!state.active);
    }

    void bleUnframe_PacketIdWrapAround()
    {
        /* packet_id wraps 255 → 0; new message with id=0 must be accepted */
        QByteArray msg = fromHex("40"); /* HEARTBEAT, 1 byte */
        uint8_t packetId = 254;
        Proto::bleFrame(msg, 20, packetId); /* id becomes 255, discarded */
        auto packets = Proto::bleFrame(msg, 20, packetId); /* id wraps to 0 */
        QCOMPARE(packetId, uint8_t(0));

        Proto::BleRxState state{};
        QByteArray out;
        QVERIFY(Proto::bleUnframe(packets[0], state, out));
        QCOMPARE(out, msg);
    }

    void bleUnframe_ConnectionReset()
    {
        /* After a connection reset (state cleared), first fragment with offset=0 is accepted */
        QByteArray msg = fromHex("02"); /* CLIENT_READY */
        uint8_t packetId = 0;
        auto packets = Proto::bleFrame(msg, 20, packetId);

        /* Simulate connection reset: fresh BleRxState */
        Proto::BleRxState state{};
        QByteArray out;
        QVERIFY(Proto::bleUnframe(packets[0], state, out));
        QCOMPARE(out, msg);
    }
};

QTEST_MAIN(TestProtocol)
#include "test_protocol.moc"
