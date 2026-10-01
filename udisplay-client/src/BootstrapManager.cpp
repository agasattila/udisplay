// SPDX-License-Identifier: LGPL-3.0-only
// Copyright (c) 2026 Attila Agas

#include "BootstrapManager.h"
#include "Transport.h"
#include <QCryptographicHash>
#include <QDebug>

BootstrapManager::BootstrapManager(Transport* transport, QObject* parent,
                                   int timeoutMs)
    : QObject(parent), m_transport(transport), m_timeoutMs(timeoutMs)
{
    connect(m_transport, &Transport::connected,
            this, &BootstrapManager::onConnected);
    connect(m_transport, &Transport::disconnected,
            this, &BootstrapManager::onDisconnected);
    connect(m_transport, &Transport::messageReceived,
            this, &BootstrapManager::onMessage);
    connect(m_transport, &Transport::errorOccurred,
            this, [this](const QString& err) {
                emit failed(err);
            });
}

void BootstrapManager::setBlobLookup(
    std::function<QByteArray(const QByteArray&)> fn)
{
    m_blobLookup = std::move(fn);
}

void BootstrapManager::setAuthCredentialProvider(
    std::function<QByteArray(uint8_t algo, QByteArray salt)> fn)
{
    m_authCredentialProvider = std::move(fn);
}

void BootstrapManager::setCachedBlob(const QByteArray& blobRoot,
                                     const QByteArray& compressedBlob)
{
    setBlobLookup([blobRoot, compressedBlob](const QByteArray& root) -> QByteArray {
        return (root == blobRoot) ? compressedBlob : QByteArray{};
    });
}

void BootstrapManager::start()
{
    m_state = State::Idle;
    m_bootstrapTimer.setSingleShot(true);
    m_bootstrapTimer.setInterval(m_timeoutMs);
    connect(&m_bootstrapTimer, &QTimer::timeout, this, [this]() {
        emit failed(QStringLiteral("Bootstrap timeout — device did not respond"));
    });
    m_bootstrapTimer.start();
    m_transport->connectToDevice();
}

/* ── Transport slots ────────────────────────────────────────────────────── */

void BootstrapManager::onConnected()
{
    m_state = State::AwaitingHandshake;
}

void BootstrapManager::onDisconnected()
{
    if ((m_state != State::Idle) && (m_state != State::Running))
        emit failed(QStringLiteral("Disconnected"));
    m_state = State::Idle;
}

void BootstrapManager::onMessage(QByteArray msg)
{
    if (msg.isEmpty()) return;
    uint8_t type = Proto::peekType(msg);

    switch (m_state) {
    case State::AwaitingHandshake:
    case State::Authenticating:
        if (type == Proto::MSG_HANDSHAKE)
            handleHandshake(msg);
        break;
    case State::RequestingHeaders:
        if (type == Proto::MSG_CHUNK_HEADER_RESPONSE)
            handleChunkHeaderResponse(msg);
        else if (type == Proto::MSG_ERR_INVALID_CHUNK) {
            Proto::ErrInvalidChunk err;
            Proto::parseErrInvalidChunk(msg, err);
            emit failed(QStringLiteral("Device reported ERR_INVALID_CHUNK for header index %1")
                        .arg(err.index));
        }
        break;
    case State::RequestingChunks:
        if (type == Proto::MSG_CHUNK_RESPONSE)
            handleChunkResponse(msg);
        else if (type == Proto::MSG_ERR_INVALID_CHUNK) {
            Proto::ErrInvalidChunk err;
            Proto::parseErrInvalidChunk(msg, err);
            emit failed(QStringLiteral("Device reported ERR_INVALID_CHUNK for index %1")
                        .arg(err.index));
        }
        break;
    case State::Running:
        handleStateUpdate(msg);
        break;
    default:
        break;
    }
}

/* ── State handlers ─────────────────────────────────────────────────────── */

void BootstrapManager::handleHandshake(const QByteArray& msg)
{
    Proto::Handshake hs;
    if (!Proto::parseHandshake(msg, hs)) {
        emit failed(QStringLiteral("Malformed HANDSHAKE"));
        return;
    }
    if (hs.protoVersion > Proto::PROTO_VERSION) {
        emit failed(QStringLiteral(
            "Incompatible device: requires uDisplay protocol v%1. Update the app.")
            .arg(hs.protoVersion));
        return;
    }

    /* Auth challenge: device wants credentials before sending merkle root. */
    if (hs.flags == 0x01u) {
        if (m_state == State::Authenticating)
            emit authFailed();
        handleAuthChallenge(hs);
        return;
    }

    /* flags=0x00: normal (or post-auth) bootstrap handshake. */
    m_merkleRoot = hs.merkleRoot;
    m_chunkCount = hs.chunkCount;
    m_chunkSize  = hs.chunkSize;

    /* Always send HANDSHAKE_ACK first. */
    m_transport->send(Proto::encodeHandshakeAck());

    /* Cache hit? Re-derive Merkle root from cached blob to detect corruption.
     * The cached blob is the assembled compressed YAML. We split it into
     * m_chunkSize-sized pieces, hash each (with zero-padding on the last),
     * and SHA256 all hashes concatenated — exactly the root derivation. */
    if (m_blobLookup) {
        QByteArray cached = m_blobLookup(m_merkleRoot);
        if (!cached.isEmpty()) {
            QCryptographicHash rootHasher(QCryptographicHash::Sha256);
            int cs = static_cast<int>(m_chunkSize > 0 ? m_chunkSize : 256);
            for (int off = 0; off < cached.size(); off += cs) {
                QByteArray piece = cached.mid(off, cs);
                if (piece.size() < cs)
                    piece.append(cs - piece.size(), '\0');
                rootHasher.addData(
                    QCryptographicHash::hash(piece, QCryptographicHash::Sha256));
            }
            if (rootHasher.result() == m_merkleRoot) {
                m_bootstrapTimer.stop();
                m_state = State::Running;
                m_transport->send(Proto::encodeClientReady());
                emit succeeded(m_merkleRoot, cached);
                return;
            }
            /* Root mismatch: cached blob is corrupted — fall through to download. */
            m_blobLookup = nullptr;
        }
    }

    /* Cache miss → download: prepare header accumulation state. */
    m_chunkHashes.clear();
    m_chunkHashes.resize(m_chunkCount);
    m_headersReceived = 0;

    m_chunks.resize(m_chunkCount);
    m_chunkReceived.fill(false, m_chunkCount);
    m_chunksReceived = 0;

    /* Transition state BEFORE sending requests so any early response is handled
     * correctly if the transport delivers synchronously. */
    m_state = State::RequestingHeaders;

    for (uint16_t i = 0; i < m_chunkCount; ++i)
        m_transport->send(Proto::encodeChunkHeaderRequest(i));
}

void BootstrapManager::handleAuthChallenge(const Proto::Handshake& hs)
{
    if (!m_authCredentialProvider) {
        emit failed(QStringLiteral("Device requires authentication but no credential provider is set"));
        return;
    }
    m_state = State::Authenticating;
    QByteArray credential = m_authCredentialProvider(hs.authAlgorithm, hs.authSalt);
    m_transport->send(Proto::encodeHandshakeAckAuth(credential));
}

void BootstrapManager::handleChunkHeaderResponse(const QByteArray& msg)
{
    Proto::ChunkHeaderResponse chr;
    if (!Proto::parseChunkHeaderResponse(msg, chr)) {
        emit failed(QStringLiteral("Malformed CHUNK_HEADER_RESPONSE"));
        return;
    }

    /* Accumulate sequentially — no index field in response. */
    m_chunkHashes[m_headersReceived] = chr.hash;
    ++m_headersReceived;

    if (m_headersReceived < static_cast<int>(m_chunkCount))
        return; /* waiting for more headers */

    /* All headers received — verify Merkle root. */
    QCryptographicHash hasher(QCryptographicHash::Sha256);
    for (const auto& h : m_chunkHashes)
        hasher.addData(h);
    if (hasher.result() != m_merkleRoot) {
        emit failed(QStringLiteral("Merkle root mismatch — chunk headers are corrupted"));
        return;
    }

    m_chunkRetries.clear();
    m_state = State::RequestingChunks;
    emit progressChanged(0);

    for (uint16_t i = 0; i < m_chunkCount; ++i)
        m_transport->send(Proto::encodeChunkRequest(i));
}

void BootstrapManager::handleChunkResponse(const QByteArray& msg)
{
    Proto::ChunkResponse cr;
    if (!Proto::parseChunkResponse(msg, cr)) {
        emit failed(QStringLiteral("Malformed CHUNK_RESPONSE"));
        return;
    }
    if (cr.index >= m_chunkCount) {
        emit failed(QStringLiteral("CHUNK_RESPONSE index %1 out of range").arg(cr.index));
        return;
    }
    if (m_chunkReceived[cr.index]) return; /* duplicate, ignore */

    /* Verify chunk hash.
     * Last chunk is padded to CHUNK_SIZE with zeros before hashing.
     * Padding is client-local — the device sends raw (unpadded) bytes.      */
    QByteArray padded = cr.data;
    if (padded.size() < Proto::CHUNK_SIZE)
        padded.append(Proto::CHUNK_SIZE - padded.size(), '\0');

    QByteArray hash = QCryptographicHash::hash(padded, QCryptographicHash::Sha256);
    if (hash != m_chunkHashes[cr.index]) {
        int retries = ++m_chunkRetries[cr.index];
        if (retries < 3) {
            /* Re-request this chunk (up to 3 attempts before giving up). */
            m_transport->send(Proto::encodeChunkRequest(cr.index));
            return;
        }
        emit failed(QStringLiteral("Chunk %1 hash mismatch after 3 attempts")
                    .arg(cr.index));
        return;
    }

    m_chunks[cr.index]         = cr.data; /* store raw (unpadded) */
    m_chunkReceived[cr.index]  = true;
    ++m_chunksReceived;

    int pct = (m_chunkCount > 0)
        ? (m_chunksReceived * 100 / m_chunkCount)
        : 100;
    emit progressChanged(pct);

    if (m_chunksReceived < m_chunkCount) return;

    /* All chunks received — assemble blob. */
    QByteArray blob;
    blob.reserve(m_chunkCount * Proto::CHUNK_SIZE);
    for (const auto& chunk : m_chunks)
        blob.append(chunk);

    m_bootstrapTimer.stop();
    m_state = State::Running;
    m_transport->send(Proto::encodeClientReady());
    emit succeeded(m_merkleRoot, blob);
}

void BootstrapManager::handleStateUpdate(const QByteArray& msg)
{
    uint8_t type = Proto::peekType(msg);

    if (type == Proto::MSG_HEARTBEAT) {
        emit heartbeat();
        return;
    }
    if (type == Proto::MSG_STATE_UPDATE) {
        Proto::StateUpdateData data{};
        if (!Proto::parseStateUpdate(msg, data)) {
            qWarning("BootstrapManager: STATE_UPDATE dropped (malformed or reserved widget_id)");
            return;
        }
        emit stateUpdateData(data);
        return;
    }
    if (type == Proto::MSG_SET_PROPERTY) {
        Proto::PropertyCommand prop{};
        if (!Proto::parseSetProperty(msg, prop)) {
            qWarning("BootstrapManager: SET_PROPERTY dropped (truncated, reserved target_id, or unknown property_id)");
            return;
        }
        emit propertyCommand(prop);
        return;
    }
    if (type == Proto::MSG_RESET_PROPERTY) {
        Proto::PropertyCommand prop{};
        if (!Proto::parseResetProperty(msg, prop)) {
            qWarning("BootstrapManager: RESET_PROPERTY dropped (truncated, reserved target_id, or unknown property_id)");
            return;
        }
        emit propertyCommand(prop);
        return;
    }
    /* Unknown message type in RUNNING state — silently ignore (forward compat). */
}
