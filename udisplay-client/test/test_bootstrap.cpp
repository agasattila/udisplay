/**
 * BootstrapManager state machine tests.
 * Uses MockTransport to inject fake device messages.
 * All test vectors from tests/protocol_vectors.json.
 */
#include <QtTest>
#include "Protocol.h"
#include "BootstrapManager.h"
#include "MockTransport.h"

static QByteArray fromHex(const char* s)
{
    return QByteArray::fromHex(QByteArray(s).replace(' ', ""));
}

/* ── Wire-format helpers ─────────────────────────────────────────────────── */

/* HANDSHAKE (legacy ≤0x03): type(1) + version(1) + root(32) + chunkCount(2LE) + chunkSize(2LE) = 38 bytes */
static QByteArray makeHandshake(uint8_t version, const QByteArray& root,
                                uint16_t chunkCount, uint16_t chunkSize)
{
    QByteArray msg(38, '\0');
    msg[0] = static_cast<char>(Proto::MSG_HANDSHAKE);
    msg[1] = static_cast<char>(version);
    memcpy(msg.data() + 2, root.constData(), 32);
    msg[34] = static_cast<char>(chunkCount & 0xFF);
    msg[35] = static_cast<char>((chunkCount >> 8) & 0xFF);
    msg[36] = static_cast<char>(chunkSize & 0xFF);
    msg[37] = static_cast<char>((chunkSize >> 8) & 0xFF);
    return msg;
}

/* HANDSHAKE (proto 0x04, flags=0x00): type(1)+version(1)+flags(1)+root(32)+count(2LE)+size(2LE) = 39 bytes */
static QByteArray makeHandshakeV4(const QByteArray& root,
                                  uint16_t chunkCount, uint16_t chunkSize)
{
    QByteArray msg(39, '\0');
    msg[0] = static_cast<char>(Proto::MSG_HANDSHAKE);
    msg[1] = static_cast<char>(0x04);
    msg[2] = static_cast<char>(0x00); /* flags: no auth */
    memcpy(msg.data() + 3, root.constData(), 32);
    msg[35] = static_cast<char>(chunkCount & 0xFF);
    msg[36] = static_cast<char>((chunkCount >> 8) & 0xFF);
    msg[37] = static_cast<char>(chunkSize & 0xFF);
    msg[38] = static_cast<char>((chunkSize >> 8) & 0xFF);
    return msg;
}

/* HANDSHAKE auth-challenge (proto 0x04, flags=0x01): type(1)+version(1)+flags(1)+algo(1)+salt(32) = 36 bytes */
static QByteArray makeHandshakeAuthChallenge(uint8_t algo, const QByteArray& salt)
{
    QByteArray msg;
    msg.append(static_cast<char>(Proto::MSG_HANDSHAKE));
    msg.append(static_cast<char>(0x04));
    msg.append(static_cast<char>(0x01)); /* flags: auth required */
    msg.append(static_cast<char>(algo));
    QByteArray s = salt.left(32);
    s.append(32 - s.size(), '\0');
    msg.append(s);
    return msg;
}

/* CHUNK_HEADER_RESPONSE: type(1) + hash(32) + len_byte(1) = 34 bytes */
static QByteArray makeChunkHeaderResponse(const QByteArray& hash, uint8_t lenByte)
{
    QByteArray msg;
    msg.append(static_cast<char>(Proto::MSG_CHUNK_HEADER_RESPONSE));
    msg.append(hash);
    msg.append(static_cast<char>(lenByte));
    return msg;
}

/* CHUNK_RESPONSE: type(1) + index(2LE) + len(2LE) + data */
static QByteArray makeChunkResponse(uint16_t index, const QByteArray& data)
{
    QByteArray msg;
    auto len = static_cast<uint16_t>(data.size());
    msg.append(static_cast<char>(Proto::MSG_CHUNK_RESPONSE));
    msg.append(static_cast<char>(index & 0xFF));
    msg.append(static_cast<char>((index >> 8) & 0xFF));
    msg.append(static_cast<char>(len & 0xFF));
    msg.append(static_cast<char>((len >> 8) & 0xFF));
    msg.append(data);
    return msg;
}

/* ── Canonical test data (protocol_vectors.json) ─────────────────────────── */

/* v1_tiny_single_chunk: 1 chunk (50 bytes raw), YAML-derived */
static const char* V1_ROOT   = "74ac9fa684287d0cab3e64e3f25ee369c74b46e7f2ec0025cd10bac7bdffdb6e";
static const char* V1_HASH0  = "847679e35883a47a6e35b2c4113ec7edcda6b1eba5c402f6dfb7bb7097919490";
static const char* V1_CHUNK0 =
    "789c4b492dcb4c4eb5e25250c84bcc4db55228e1"
    "2acf4c494f2d290609258208058592ca02904c7e"
    "7a7a4e2a1700767e0eb6";

/* v3_multi_chunk_yaml: 2 chunks (256 + 3 bytes raw), YAML-derived */
static const char* V3_ROOT   = "4242869d756f22c2d2318270ec4a6c31600e52fe333842ab4b4fd7660e8897d1";
static const char* V3_HASH0  = "8341cfd59a9f9b4ced5e23df0b068f5deb112040f3c441b45a49952b3bb9c109";
static const char* V3_HASH1  = "2b92cb0957664e01109e2ea5a85eefdb81ad3f76940c40298e4cb158a7f3ca85";
static const char* V3_CHUNK0 =
    "789c8dd1316ec2501084e1dea7d88e2e7a330b06"
    "7c0c2e1039f19365c981085b44dc3e3145a69d6e"
    "35dabffa86fa983e6bd7445cfbafdac55a973586"
    "d7f8b73dea7d996ed72e76782bbbe6671ac6ba2e"
    "dbf7bdcefdf3bd94ed8e589fdf5b7b1bc7b9be86"
    "b9ffa8731797ed2b4a51002b80025a0115a415a4"
    "82bd15ec151cace0a0a0b58256c1d10a8e0a4e56"
    "705270b682f37f004b1a9286250d49c39286a461"
    "4943d2b0a4216958d290342c69481a9634240d4b"
    "1a9286250d49d392a6a4694953d2b4a429695ad2"
    "94342d694a9a9634254d4b9a92a6254d49d392a6"
    "a4694953d26949a7a4d3924e49a7259d924e4b3a"
    "259d96744a3a2de994745ad2d936bfca";
static const char* V3_CHUNK1 = "2b1575";

/* ─────────────────────────────────────────────────────────────────────────── */

class TestBootstrap : public QObject
{
    Q_OBJECT

private slots:

    /* ── Complete bootstrap sequences ──────────────────────────────── */

    void bootstrap_SingleChunk()
    {
        MockTransport t;
        BootstrapManager bm(&t);
        QSignalSpy succeededSpy(&bm, &BootstrapManager::succeeded);
        QSignalSpy failedSpy   (&bm, &BootstrapManager::failed);

        /* Connect */
        t.simulateConnect();
        QCOMPARE(bm.state(), BootstrapManager::State::AwaitingHandshake);
        QCOMPARE(t.sentCount(), 0);

        /* Device sends HANDSHAKE (v1, 1 chunk, V1 root) */
        t.injectMessage(makeHandshake(1, fromHex(V1_ROOT), 1, 256));
        QCOMPARE(bm.state(), BootstrapManager::State::RequestingHeaders);
        QCOMPARE(t.sentCount(), 2);
        QCOMPARE(t.takeSent(), Proto::encodeHandshakeAck());          /* "01 01"    */
        QCOMPARE(t.takeSent(), Proto::encodeChunkHeaderRequest(0));   /* "10 00 00" */

        /* Device sends CHUNK_HEADER_RESPONSE for chunk 0 (partial: 50 bytes) */
        t.injectMessage(makeChunkHeaderResponse(fromHex(V1_HASH0), 50u));
        QCOMPARE(bm.state(), BootstrapManager::State::RequestingChunks);
        QCOMPARE(t.sentCount(), 1);
        QCOMPARE(t.takeSent(), Proto::encodeChunkRequest(0)); /* "20 00 00" */

        /* Device sends CHUNK_RESPONSE for index 0 */
        t.injectMessage(makeChunkResponse(0, fromHex(V1_CHUNK0)));
        QCOMPARE(bm.state(), BootstrapManager::State::Running);
        QCOMPARE(failedSpy.count(), 0);
        QCOMPARE(succeededSpy.count(), 1);

        QCOMPARE(succeededSpy.at(0).at(0).toByteArray(), fromHex(V1_ROOT));
        QCOMPARE(succeededSpy.at(0).at(1).toByteArray(), fromHex(V1_CHUNK0));
    }

    void bootstrap_MultiChunk()
    {
        MockTransport t;
        BootstrapManager bm(&t);
        QSignalSpy succeededSpy(&bm, &BootstrapManager::succeeded);
        QSignalSpy failedSpy   (&bm, &BootstrapManager::failed);

        t.simulateConnect();

        /* HANDSHAKE: v3 root, 2 chunks */
        t.injectMessage(makeHandshake(1, fromHex(V3_ROOT), 2, 256));
        QCOMPARE(bm.state(), BootstrapManager::State::RequestingHeaders);
        t.clearSent(); /* discard HANDSHAKE_ACK + 2×CHUNK_HEADER_REQUEST */

        /* CHUNK_HEADER_RESPONSE for chunk 0 (full: 256 bytes) */
        t.injectMessage(makeChunkHeaderResponse(fromHex(V3_HASH0), 0u));
        QCOMPARE(bm.state(), BootstrapManager::State::RequestingHeaders); /* still waiting for chunk 1 */
        QCOMPARE(t.sentCount(), 0);

        /* CHUNK_HEADER_RESPONSE for chunk 1 (partial: 3 bytes) — triggers Merkle verify */
        t.injectMessage(makeChunkHeaderResponse(fromHex(V3_HASH1), 3u));
        QCOMPARE(bm.state(), BootstrapManager::State::RequestingChunks);
        /* Client fires both chunk requests at once after Merkle verify */
        QCOMPARE(t.sentCount(), 2);
        QCOMPARE(t.takeSent(), Proto::encodeChunkRequest(0)); /* "20 00 00" */
        QCOMPARE(t.takeSent(), Proto::encodeChunkRequest(1)); /* "20 01 00" */

        /* Deliver chunk 1 before chunk 0 (out-of-order OK) */
        t.injectMessage(makeChunkResponse(1, fromHex(V3_CHUNK1)));
        QCOMPARE(bm.state(), BootstrapManager::State::RequestingChunks); /* not done yet */
        QCOMPARE(succeededSpy.count(), 0);

        t.injectMessage(makeChunkResponse(0, fromHex(V3_CHUNK0)));
        QCOMPARE(bm.state(), BootstrapManager::State::Running);
        QCOMPARE(failedSpy.count(), 0);
        QCOMPARE(succeededSpy.count(), 1);

        QCOMPARE(succeededSpy.at(0).at(0).toByteArray(), fromHex(V3_ROOT));
        /* Assembled blob = chunk0_data + chunk1_data (in index order) */
        QByteArray expected = fromHex(V3_CHUNK0) + fromHex(V3_CHUNK1);
        QCOMPARE(succeededSpy.at(0).at(1).toByteArray(), expected);
    }

    /* ── Cache hit ──────────────────────────────────────────────────── */

    void bootstrap_CacheHit()
    {
        MockTransport t;
        BootstrapManager bm(&t);
        QSignalSpy succeededSpy(&bm, &BootstrapManager::succeeded);

        const QByteArray cachedBlob = fromHex(V1_CHUNK0);
        bm.setCachedBlob(fromHex(V1_ROOT), cachedBlob);

        t.simulateConnect();
        t.injectMessage(makeHandshake(1, fromHex(V1_ROOT), 1, 256));

        /* Should skip to Running without requesting hashes or chunks */
        QCOMPARE(bm.state(), BootstrapManager::State::Running);
        QCOMPARE(t.sentCount(), 2);               /* HANDSHAKE_ACK + CLIENT_READY */
        QCOMPARE(t.lastSent(), Proto::encodeClientReady());
        QCOMPARE(succeededSpy.count(), 1);
        QCOMPARE(succeededSpy.at(0).at(0).toByteArray(), fromHex(V1_ROOT));
        QCOMPARE(succeededSpy.at(0).at(1).toByteArray(), cachedBlob);
    }

    void bootstrap_SingleChunk_SendsClientReady()
    {
        MockTransport t;
        BootstrapManager bm(&t);

        t.simulateConnect();
        t.injectMessage(makeHandshake(1, fromHex(V1_ROOT), 1, 256));
        t.clearSent();
        t.injectMessage(makeChunkHeaderResponse(fromHex(V1_HASH0), 50u));
        t.clearSent();

        t.injectMessage(makeChunkResponse(0, fromHex(V1_CHUNK0)));
        QCOMPARE(bm.state(), BootstrapManager::State::Running);

        /* CLIENT_READY must be sent (and must be the last message sent) */
        QVERIFY(t.sentCount() >= 1);
        QCOMPARE(t.lastSent(), Proto::encodeClientReady());
    }

    void bootstrap_CacheHit_SendsClientReady()
    {
        MockTransport t;
        BootstrapManager bm(&t);

        bm.setCachedBlob(fromHex(V1_ROOT), fromHex(V1_CHUNK0));
        t.simulateConnect();
        t.injectMessage(makeHandshake(1, fromHex(V1_ROOT), 1, 256));

        QCOMPARE(bm.state(), BootstrapManager::State::Running);
        QCOMPARE(t.sentCount(), 2);
        /* First: HANDSHAKE_ACK, second: CLIENT_READY */
        t.takeSent(); /* HANDSHAKE_ACK */
        QCOMPARE(t.takeSent(), Proto::encodeClientReady());
    }

    void bootstrap_CacheMiss_RootMismatch()
    {
        MockTransport t;
        BootstrapManager bm(&t);
        QSignalSpy succeededSpy(&bm, &BootstrapManager::succeeded);

        /* Cached blob has a different root — must trigger full download */
        bm.setCachedBlob(fromHex(V3_ROOT), fromHex(V1_CHUNK0));

        t.simulateConnect();
        t.injectMessage(makeHandshake(1, fromHex(V1_ROOT), 1, 256));

        QCOMPARE(bm.state(), BootstrapManager::State::RequestingHeaders);
        QCOMPARE(t.sentCount(), 2); /* HANDSHAKE_ACK + CHUNK_HEADER_REQUEST(0) */
    }

    /* ── Error paths ────────────────────────────────────────────────── */

    void bootstrap_VersionMismatch()
    {
        MockTransport t;
        BootstrapManager bm(&t);
        QSignalSpy failedSpy(&bm, &BootstrapManager::failed);

        t.simulateConnect();
        t.injectMessage(makeHandshake(4, fromHex(V1_ROOT), 1, 256)); /* version 4 > PROTO_VERSION(3) */

        QCOMPARE(failedSpy.count(), 1);
        QVERIFY(!failedSpy.at(0).at(0).toString().isEmpty());
    }

    void bootstrap_BadMerkleRoot()
    {
        MockTransport t;
        BootstrapManager bm(&t);
        QSignalSpy failedSpy(&bm, &BootstrapManager::failed);

        t.simulateConnect();
        /* Send HANDSHAKE with a garbage root */
        QByteArray badRoot(32, '\xAB');
        t.injectMessage(makeHandshake(1, badRoot, 1, 256));
        t.clearSent();

        /* Send one CHUNK_HEADER_RESPONSE — SHA256(V1_HASH0) != badRoot */
        t.injectMessage(makeChunkHeaderResponse(fromHex(V1_HASH0), 50u));

        QCOMPARE(failedSpy.count(), 1);
        QVERIFY(failedSpy.at(0).at(0).toString().contains("Merkle"));
    }

    void bootstrap_BadChunkHash()
    {
        /* Chunk retry: 3 bad chunks must be received before failed() fires. */
        MockTransport t;
        BootstrapManager bm(&t);
        QSignalSpy failedSpy(&bm, &BootstrapManager::failed);

        t.simulateConnect();
        t.injectMessage(makeHandshake(1, fromHex(V1_ROOT), 1, 256));
        t.clearSent();
        t.injectMessage(makeChunkHeaderResponse(fromHex(V1_HASH0), 50u));
        t.clearSent();

        /* First two corrupt deliveries → retry re-request, no failure yet */
        QByteArray badChunk = fromHex(V1_CHUNK0);
        badChunk[0] = static_cast<char>(badChunk[0] ^ 0xFF);

        t.injectMessage(makeChunkResponse(0, badChunk));
        QCOMPARE(failedSpy.count(), 0);        /* retry 1 */
        QCOMPARE(t.sentCount(), 1);            /* CHUNK_REQUEST re-sent */
        t.clearSent();

        t.injectMessage(makeChunkResponse(0, badChunk));
        QCOMPARE(failedSpy.count(), 0);        /* retry 2 */
        QCOMPARE(t.sentCount(), 1);
        t.clearSent();

        /* Third corrupt delivery → gives up and emits failed */
        t.injectMessage(makeChunkResponse(0, badChunk));
        QCOMPARE(failedSpy.count(), 1);
        QVERIFY(failedSpy.at(0).at(0).toString().contains("hash mismatch"));
    }

    void bootstrap_ErrInvalidChunk()
    {
        MockTransport t;
        BootstrapManager bm(&t);
        QSignalSpy failedSpy(&bm, &BootstrapManager::failed);

        t.simulateConnect();
        t.injectMessage(makeHandshake(1, fromHex(V1_ROOT), 1, 256));
        t.clearSent();
        t.injectMessage(makeChunkHeaderResponse(fromHex(V1_HASH0), 50u));
        t.clearSent();

        /* Device reports chunk 0 is invalid */
        QByteArray errMsg;
        errMsg.append(static_cast<char>(Proto::MSG_ERR_INVALID_CHUNK));
        errMsg.append(static_cast<char>(0x00)); /* index 0 LE */
        errMsg.append(static_cast<char>(0x00));
        t.injectMessage(errMsg);

        QCOMPARE(failedSpy.count(), 1);
    }

    void bootstrap_Disconnect_DuringHandshake()
    {
        MockTransport t;
        BootstrapManager bm(&t);
        QSignalSpy failedSpy(&bm, &BootstrapManager::failed);

        t.simulateConnect();
        QCOMPARE(bm.state(), BootstrapManager::State::AwaitingHandshake);

        t.disconnectFromDevice();

        QCOMPARE(bm.state(), BootstrapManager::State::Idle);
        QCOMPARE(failedSpy.count(), 1);
    }

    void bootstrap_DuplicateChunk_Ignored()
    {
        MockTransport t;
        BootstrapManager bm(&t);
        QSignalSpy succeededSpy(&bm, &BootstrapManager::succeeded);

        t.simulateConnect();
        t.injectMessage(makeHandshake(1, fromHex(V1_ROOT), 1, 256));
        t.clearSent();
        t.injectMessage(makeChunkHeaderResponse(fromHex(V1_HASH0), 50u));
        t.clearSent();

        /* Send valid chunk twice */
        t.injectMessage(makeChunkResponse(0, fromHex(V1_CHUNK0)));
        t.injectMessage(makeChunkResponse(0, fromHex(V1_CHUNK0)));

        /* succeeded must fire exactly once */
        QCOMPARE(succeededSpy.count(), 1);
    }

    /* ── Running-phase signal forwarding ────────────────────────────── */

    void bootstrap_Running_Heartbeat()
    {
        MockTransport t;
        BootstrapManager bm(&t);

        /* Reach RUNNING via cache hit */
        bm.setCachedBlob(fromHex(V1_ROOT), fromHex(V1_CHUNK0));
        t.simulateConnect();
        t.injectMessage(makeHandshake(1, fromHex(V1_ROOT), 1, 256));
        QCOMPARE(bm.state(), BootstrapManager::State::Running);

        QSignalSpy heartbeatSpy(&bm, &BootstrapManager::heartbeat);
        QByteArray hb;
        hb.append(static_cast<char>(Proto::MSG_HEARTBEAT));
        t.injectMessage(hb);
        QCOMPARE(heartbeatSpy.count(), 1);
    }

    void bootstrap_Running_StateUpdate_Float32()
    {
        MockTransport t;
        BootstrapManager bm(&t);

        bm.setCachedBlob(fromHex(V1_ROOT), fromHex(V1_CHUNK0));
        t.simulateConnect();
        t.injectMessage(makeHandshake(1, fromHex(V1_ROOT), 1, 256));

        Proto::StateUpdateData received{};
        bool gotSignal = false;
        connect(&bm, &BootstrapManager::stateUpdateData,
                [&](Proto::StateUpdateData d) {
                    received  = d;
                    gotSignal = true;
                });

        /* STATE_UPDATE: widget 0x10, float32, 3.14 (from vectors) */
        t.injectMessage(fromHex("30 10 01 c3 f5 48 40"));

        QVERIFY(gotSignal);
        QCOMPARE(received.widgetId,  uint8_t(0x10));
        QCOMPARE(received.valueType, uint8_t(Proto::VAL_FLOAT32));
        QVERIFY(qAbs(received.f32Value - 3.14f) < 0.0001f);
    }

    void bootstrap_Running_PropertyCommand()
    {
        MockTransport t;
        BootstrapManager bm(&t);

        bm.setCachedBlob(fromHex(V1_ROOT), fromHex(V1_CHUNK0));
        t.simulateConnect();
        t.injectMessage(makeHandshake(1, fromHex(V1_ROOT), 1, 256));

        Proto::PropertyCommand received{};
        bool gotSignal = false;
        connect(&bm, &BootstrapManager::propertyCommand,
                [&](Proto::PropertyCommand p) {
                    received  = p;
                    gotSignal = true;
                });

        /* SET_PROPERTY (0x32): target=0x10, ENABLED=0 (from vectors) */
        t.injectMessage(fromHex("32 10 01 00"));

        QVERIFY(gotSignal);
        QVERIFY(received.isSet);
        QCOMPARE(received.targetId,   uint8_t(0x10));
        QCOMPARE(received.propertyId, uint8_t(Proto::PROP_ENABLED));
        QCOMPARE(received.value,      uint8_t(0));
    }

    void bootstrap_Running_ResetPropertyCommand()
    {
        MockTransport t;
        BootstrapManager bm(&t);

        bm.setCachedBlob(fromHex(V1_ROOT), fromHex(V1_CHUNK0));
        t.simulateConnect();
        t.injectMessage(makeHandshake(1, fromHex(V1_ROOT), 1, 256));

        Proto::PropertyCommand received{};
        bool gotSignal = false;
        connect(&bm, &BootstrapManager::propertyCommand,
                [&](Proto::PropertyCommand p) {
                    received  = p;
                    gotSignal = true;
                });

        /* RESET_PROPERTY (0x33): target=0x10, ENABLED */
        t.injectMessage(fromHex("33 10 01"));

        QVERIFY(gotSignal);
        QVERIFY(!received.isSet);
        QCOMPARE(received.targetId,   uint8_t(0x10));
        QCOMPARE(received.propertyId, uint8_t(Proto::PROP_ENABLED));
    }

    void bootstrap_Running_UnknownMessage_Ignored()
    {
        MockTransport t;
        BootstrapManager bm(&t);
        QSignalSpy failedSpy(&bm, &BootstrapManager::failed);

        bm.setCachedBlob(fromHex(V1_ROOT), fromHex(V1_CHUNK0));
        t.simulateConnect();
        t.injectMessage(makeHandshake(1, fromHex(V1_ROOT), 1, 256));

        /* Unknown type 0xAA — should be silently dropped */
        QByteArray unk;
        unk.append(static_cast<char>(0xAA));
        unk.append("payload");
        t.injectMessage(unk);

        QCOMPARE(bm.state(), BootstrapManager::State::Running);
        QCOMPARE(failedSpy.count(), 0);
    }

    void bootstrap_Timeout()
    {
        MockTransport t;
        /* 50ms timeout — device never responds. */
        BootstrapManager bm(&t, nullptr, 50);
        QSignalSpy failedSpy(&bm, &BootstrapManager::failed);

        bm.start();
        QTest::qWait(150);

        QCOMPARE(failedSpy.count(), 1);
        QVERIFY(failedSpy.at(0).at(0).toString().contains("timeout"));
    }

    void bootstrap_MalformedHandshake()
    {
        MockTransport t;
        BootstrapManager bm(&t);
        QSignalSpy failedSpy(&bm, &BootstrapManager::failed);

        t.simulateConnect();
        /* Inject a HANDSHAKE that is too short to parse (< 38 bytes). */
        QByteArray bad;
        bad.append(static_cast<char>(Proto::MSG_HANDSHAKE));
        bad.append("short");
        t.injectMessage(bad);

        QCOMPARE(failedSpy.count(), 1);
        QVERIFY(failedSpy.at(0).at(0).toString().contains("Malformed"));
    }

    void bootstrap_StateTransitionBeforeSend()
    {
        /* State must be RequestingHeaders before the first CHUNK_HEADER_REQUEST
         * is sent, so that any synchronous/early response is dispatched correctly. */
        MockTransport t;
        BootstrapManager bm(&t);

        t.simulateConnect();
        t.injectMessage(makeHandshake(1, fromHex(V1_ROOT), 1, 256));

        /* By the time injectMessage returns, state must already be RequestingHeaders
         * AND the header request must have been sent. */
        QCOMPARE(bm.state(), BootstrapManager::State::RequestingHeaders);
        QCOMPARE(t.sentCount(), 2); /* HANDSHAKE_ACK + CHUNK_HEADER_REQUEST(0) */
        t.takeSent(); /* HANDSHAKE_ACK */
        QCOMPARE(t.takeSent(), Proto::encodeChunkHeaderRequest(0));
    }

    void bootstrap_HeaderResponseError()
    {
        /* ERR_INVALID_CHUNK during header collection must emit failed(). */
        MockTransport t;
        BootstrapManager bm(&t);
        QSignalSpy failedSpy(&bm, &BootstrapManager::failed);

        t.simulateConnect();
        t.injectMessage(makeHandshake(1, fromHex(V1_ROOT), 1, 256));
        QCOMPARE(bm.state(), BootstrapManager::State::RequestingHeaders);
        t.clearSent();

        /* Device reports the header index is invalid */
        QByteArray errMsg;
        errMsg.append(static_cast<char>(Proto::MSG_ERR_INVALID_CHUNK));
        errMsg.append(static_cast<char>(0x00)); /* index 0 LE */
        errMsg.append(static_cast<char>(0x00));
        t.injectMessage(errMsg);

        QCOMPARE(failedSpy.count(), 1);
        QVERIFY(failedSpy.at(0).at(0).toString().contains("ERR_INVALID_CHUNK"));
    }

    void bootstrap_Running_MalformedStateUpdate_Dropped()
    {
        MockTransport t;
        BootstrapManager bm(&t);
        QSignalSpy stateSpy(&bm, &BootstrapManager::stateUpdateData);

        bm.setCachedBlob(fromHex(V1_ROOT), fromHex(V1_CHUNK0));
        t.simulateConnect();
        t.injectMessage(makeHandshake(1, fromHex(V1_ROOT), 1, 256));

        /* STATE_UPDATE with reserved widget_id 0x00 — must be dropped silently. */
        QByteArray bad;
        bad.append(static_cast<char>(Proto::MSG_STATE_UPDATE));
        bad.append(static_cast<char>(0x00)); /* reserved id */
        bad.append(static_cast<char>(Proto::VAL_UINT8));
        bad.append(static_cast<char>(42));
        t.injectMessage(bad);

        QCOMPARE(stateSpy.count(), 0);
        QCOMPARE(bm.state(), BootstrapManager::State::Running);
    }

    void bootstrap_Running_MalformedSetProperty_Dropped()
    {
        MockTransport t;
        BootstrapManager bm(&t);
        QSignalSpy propSpy(&bm, &BootstrapManager::propertyCommand);

        bm.setCachedBlob(fromHex(V1_ROOT), fromHex(V1_CHUNK0));
        t.simulateConnect();
        t.injectMessage(makeHandshake(1, fromHex(V1_ROOT), 1, 256));

        /* SET_PROPERTY truncated to 2 bytes — must be dropped silently. */
        QByteArray bad;
        bad.append(static_cast<char>(Proto::MSG_SET_PROPERTY));
        bad.append(static_cast<char>(0x10));
        t.injectMessage(bad);

        QCOMPARE(propSpy.count(), 0);
        QCOMPARE(bm.state(), BootstrapManager::State::Running);
    }

    void bootstrap_Running_MalformedResetProperty_Dropped()
    {
        MockTransport t;
        BootstrapManager bm(&t);
        QSignalSpy propSpy(&bm, &BootstrapManager::propertyCommand);

        bm.setCachedBlob(fromHex(V1_ROOT), fromHex(V1_CHUNK0));
        t.simulateConnect();
        t.injectMessage(makeHandshake(1, fromHex(V1_ROOT), 1, 256));

        /* RESET_PROPERTY with reserved target_id 0x00 — must be dropped silently. */
        QByteArray bad;
        bad.append(static_cast<char>(Proto::MSG_RESET_PROPERTY));
        bad.append(static_cast<char>(0x00)); /* reserved id */
        bad.append(static_cast<char>(Proto::PROP_ENABLED));
        t.injectMessage(bad);

        QCOMPARE(propSpy.count(), 0);
        QCOMPARE(bm.state(), BootstrapManager::State::Running);
    }
    /* ── Auth flow tests (proto 0x04, HANDSHAKE flags=0x01) ─────────────── */

    void bootstrap_Auth_NoCrendentialProvider_EmitsFailed()
    {
        /* Device sends auth challenge but no provider is configured → failed() */
        MockTransport t;
        BootstrapManager bm(&t);
        QSignalSpy failedSpy(&bm, &BootstrapManager::failed);

        t.simulateConnect();
        QByteArray salt(32, '\x42');
        t.injectMessage(makeHandshakeAuthChallenge(Proto::AUTH_HMAC_SHA256, salt));

        QCOMPARE(failedSpy.count(), 1);
        QVERIFY(failedSpy.at(0).at(0).toString().contains("credential provider"));
    }

    void bootstrap_Auth_WithProvider_TransitionsToAuthenticating()
    {
        /* Device sends auth challenge; provider returns credential; ACK sent */
        MockTransport t;
        BootstrapManager bm(&t);

        QByteArray salt(32, '\x01');
        QByteArray credential(32, '\xAB');
        uint8_t capturedAlgo = 0;
        QByteArray capturedSalt;
        bm.setAuthCredentialProvider([&](uint8_t algo, QByteArray s) -> QByteArray {
            capturedAlgo = algo;
            capturedSalt = s;
            return credential;
        });

        t.simulateConnect();
        t.injectMessage(makeHandshakeAuthChallenge(Proto::AUTH_HMAC_SHA256, salt));

        QCOMPARE(bm.state(), BootstrapManager::State::Authenticating);
        QCOMPARE(capturedAlgo, uint8_t(Proto::AUTH_HMAC_SHA256));
        QCOMPARE(capturedSalt, salt);

        /* Must have sent HANDSHAKE_ACK_AUTH */
        QCOMPARE(t.sentCount(), 1);
        QCOMPARE(t.takeSent(), Proto::encodeHandshakeAckAuth(credential));
    }

    void bootstrap_Auth_Retry_EmitsAuthFailed()
    {
        /* Device sends a second auth challenge (retry); authFailed() must be emitted */
        MockTransport t;
        BootstrapManager bm(&t);
        QSignalSpy authFailedSpy(&bm, &BootstrapManager::authFailed);

        QByteArray cred(32, '\xAB');
        bm.setAuthCredentialProvider([cred](uint8_t, QByteArray) { return cred; });

        t.simulateConnect();
        QByteArray salt1(32, '\x01');
        t.injectMessage(makeHandshakeAuthChallenge(Proto::AUTH_HMAC_SHA256, salt1));
        QCOMPARE(bm.state(), BootstrapManager::State::Authenticating);
        QCOMPARE(authFailedSpy.count(), 0);

        /* Second challenge (auth failed on device side) */
        QByteArray salt2(32, '\x02');
        t.injectMessage(makeHandshakeAuthChallenge(Proto::AUTH_HMAC_SHA256, salt2));

        QCOMPARE(authFailedSpy.count(), 1);
        QCOMPARE(bm.state(), BootstrapManager::State::Authenticating);
        /* Provider called again with new salt, another ACK sent */
        QCOMPARE(t.sentCount(), 2);
    }

    void bootstrap_Auth_Success_ProceedsToBootstrap()
    {
        /* After auth challenge, device sends flags=0x00 handshake → normal bootstrap */
        MockTransport t;
        BootstrapManager bm(&t);
        QSignalSpy succeededSpy(&bm, &BootstrapManager::succeeded);

        QByteArray cred(32, '\xAB');
        bm.setAuthCredentialProvider([cred](uint8_t, QByteArray) { return cred; });
        bm.setCachedBlob(fromHex(V1_ROOT), fromHex(V1_CHUNK0));

        t.simulateConnect();
        QByteArray salt(32, '\x01');
        t.injectMessage(makeHandshakeAuthChallenge(Proto::AUTH_HMAC_SHA256, salt));
        QCOMPARE(bm.state(), BootstrapManager::State::Authenticating);
        t.clearSent();

        /* Auth passed: device sends normal HANDSHAKE(flags=0) */
        t.injectMessage(makeHandshakeV4(fromHex(V1_ROOT), 1, 256));

        /* Should have sent HANDSHAKE_ACK and then progressed to Running via cache hit */
        QCOMPARE(succeededSpy.count(), 1);
        QCOMPARE(bm.state(), BootstrapManager::State::Running);
    }
};

QTEST_MAIN(TestBootstrap)
#include "test_bootstrap.moc"
