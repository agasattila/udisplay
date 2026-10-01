// SPDX-License-Identifier: LGPL-3.0-only
// Copyright (c) 2026 Attila Agas

/**
 * BootstrapManager — client-side bootstrap state machine.
 *
 * State diagram:
 *
 *   start() ── starts 30s bootstrap timer ─────────────────────────── (any state)
 *    │  timer fires → failed("Bootstrap timeout")
 *    │
 *   IDLE
 *    │  transport::connected()
 *    ▼
 *   AWAITING_HANDSHAKE
 *    │  HANDSHAKE(flags=0) received (version OK)
 *    │  → sends HANDSHAKE_ACK(flags=0)
 *    │  HANDSHAKE(flags=1) received → sends HANDSHAKE_ACK(flags=1, credential)
 *    │  → transition to AUTHENTICATING
 *    ▼
 *   AUTHENTICATING
 *    │  HANDSHAKE(flags=0) received → auth passed, send HANDSHAKE_ACK
 *    │  HANDSHAKE(flags=1) received → auth failed, emit authFailed(), retry
 *    ▼
 *   (back to normal flow below)
 *    ├─ cache hit: re-derive Merkle root from blob ──────────────────────┐
 *    │    root match → stop timer, emit succeeded()                      │
 *    │    root mismatch → fall through to download                       │
 *    │  cache miss / mismatch                                            │
 *    │  → transition to RequestingHeaders, then sends N CHUNK_HEADER_REQUESTs
 *    ▼                                                                   │
 *   REQUESTING_HEADERS                                                   │
 *    │  N CHUNK_HEADER_RESPONSEs arrive sequentially (no index field)   │
 *    │  all N received → verify Merkle root → enqueue N CHUNK_REQUESTs  │
 *    │  any error response → failed()                                    │
 *    ▼                                                                   │
 *   REQUESTING_CHUNKS                                                    │
 *    │  each CHUNK_RESPONSE: verify hash (retry up to 3×), store        │
 *    │  all chunks done → assemble blob, stop timer                      │
 *    ▼                                                                   │
 *   RUNNING ◄──────────────────────────────────────────────────────────┘
 *    │  HEARTBEAT / STATE_UPDATE / PROPERTY_COMMAND forwarded via signals
 *    │  disconnect → IDLE + failed()
 *
 * No SQLite cache access in this class. The caller (DeviceController) may
 * provide a cached blob via setCachedBlob() before calling start(). If the
 * blob's Merkle root matches the HANDSHAKE root, the download is skipped.
 */
#pragma once

#include "Protocol.h"
#include <QByteArray>
#include <QHash>
#include <QObject>
#include <QTimer>
#include <QVector>
#include <QVariant>
#include <functional>

class Transport;

class BootstrapManager : public QObject
{
    Q_OBJECT

public:
    enum class State {
        Idle,
        AwaitingHandshake,
        Authenticating,
        RequestingHeaders,
        RequestingChunks,
        Running,
    };

    explicit BootstrapManager(Transport* transport, QObject* parent = nullptr,
                              int timeoutMs = kBootstrapTimeoutMs);

    /**
     * Register a blob lookup function (called during HANDSHAKE handling).
     * The function receives the Merkle root and returns the cached compressed blob,
     * or an empty QByteArray on cache miss. Must be set before start().
     */
    void setBlobLookup(std::function<QByteArray(const QByteArray&)> fn);

    /**
     * Convenience wrapper around setBlobLookup for tests that know the root upfront.
     */
    void setCachedBlob(const QByteArray& blobRoot, const QByteArray& compressedBlob);

    /**
     * Register an auth credential provider. Called when the device sends a
     * HANDSHAKE(flags=1) challenge. The function receives (algo, 32-byte salt)
     * and must return a 32-byte credential = HMAC-SHA256(key=password, message=salt).
     * If not set and the device requires auth, bootstrap fails.
     */
    void setAuthCredentialProvider(
        std::function<QByteArray(uint8_t algo, QByteArray salt)> fn);

    /** Begin the bootstrap: connect the transport. */
    void start();

    State state() const { return m_state; }

signals:
    /** Bootstrap complete. merkleRoot is the verified root; compressedBlob is the raw zlib-compressed YAML. */
    void succeeded(QByteArray merkleRoot, QByteArray compressedBlob);

    /** Unrecoverable error. Connection will be dropped. */
    void failed(QString reason);

    /** Authentication failed (wrong credential) — a new challenge was issued.
     *  The provider should present a re-try UI. Bootstrap continues. */
    void authFailed();

    /** Download progress 0–100 (only meaningful during REQUESTING_CHUNKS). */
    void progressChanged(int percent);

    /* ── Running-phase forwarded events ─────────────────────────────── */
    void heartbeat();
    void stateUpdateData(Proto::StateUpdateData update);
    void propertyCommand(Proto::PropertyCommand cmd);

private slots:
    void onConnected();
    void onDisconnected();
    void onMessage(QByteArray msg);

private:
    void handleHandshake(const QByteArray& msg);
    void handleAuthChallenge(const Proto::Handshake& hs);
    void handleChunkHeaderResponse(const QByteArray& msg);
    void handleChunkResponse(const QByteArray& msg);
    void handleStateUpdate(const QByteArray& msg);

    Transport* m_transport;
    State      m_state = State::Idle;

    /* From HANDSHAKE */
    QByteArray m_merkleRoot;
    uint16_t   m_chunkCount = 0;
    uint16_t   m_chunkSize  = 0;

    /* Accumulated from CHUNK_HEADER_RESPONSEs (filled sequentially, no index) */
    QVector<QByteArray> m_chunkHashes;
    int                 m_headersReceived = 0;

    /* Chunk download state */
    QVector<QByteArray>  m_chunks;         /* received raw (unpadded) chunks */
    QVector<bool>        m_chunkReceived;
    int                  m_chunksReceived = 0;
    QHash<uint16_t, int> m_chunkRetries;   /* retry count per chunk index (max 3) */

    /* Optional blob lookup (replaces pre-set cached blob) */
    std::function<QByteArray(const QByteArray&)> m_blobLookup;

    /* Optional auth credential provider */
    std::function<QByteArray(uint8_t, QByteArray)> m_authCredentialProvider;

    /* Bootstrap phase timeout. Default 30s; injectable for testing. */
    QTimer m_bootstrapTimer;
    int    m_timeoutMs;
    static constexpr int kBootstrapTimeoutMs = 30'000;
};
