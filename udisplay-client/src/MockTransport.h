// SPDX-License-Identifier: LGPL-3.0-only
// Copyright (c) 2026 Attila Agas

/**
 * In-memory transport for unit tests.
 *
 * Usage pattern:
 *   MockTransport t;
 *   BootstrapManager bm(&t);
 *   t.simulateConnect();
 *   t.injectMessage(fakeHandshakeBytes);
 *   QVERIFY(t.lastSent() == expected);
 */
#pragma once

#include "Transport.h"
#include <QList>

class MockTransport : public Transport
{
    Q_OBJECT

public:
    explicit MockTransport(QObject* parent = nullptr)
        : Transport(parent), m_connected(false) {}

    /* ── Transport interface ─────────────────────────────────────────── */

    void send(const QByteArray& msg) override
    {
        m_sent.append(msg);
    }

    void connectToDevice() override
    {
        /* Tests call simulateConnect() manually for precise timing control. */
    }

    void disconnectFromDevice() override
    {
        if (m_connected) {
            m_connected = false;
            emit disconnected();
        }
    }

    bool isConnected() const override { return m_connected; }

    /* ── Test helpers ────────────────────────────────────────────────── */

    /** Simulate a successful connection from the device side. */
    void simulateConnect()
    {
        m_connected = true;
        emit connected();
    }

    /** Simulate the device sending a message to the client. */
    void injectMessage(const QByteArray& msg)
    {
        emit messageReceived(msg);
    }

    /** All messages sent by the client (in order). */
    const QList<QByteArray>& sentMessages() const { return m_sent; }

    /** The most recent message sent. Returns empty if nothing sent yet. */
    QByteArray lastSent() const
    {
        return m_sent.isEmpty() ? QByteArray{} : m_sent.last();
    }

    /** Remove and return the first message sent (FIFO). */
    QByteArray takeSent()
    {
        return m_sent.isEmpty() ? QByteArray{} : m_sent.takeFirst();
    }

    /** Drain all sent messages. */
    void clearSent() { m_sent.clear(); }

    /** Number of messages sent by the client. */
    int sentCount() const { return m_sent.size(); }

private:
    bool             m_connected;
    QList<QByteArray> m_sent;
};
