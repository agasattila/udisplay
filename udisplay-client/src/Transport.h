// SPDX-License-Identifier: LGPL-3.0-only
// Copyright (c) 2026 Attila Agas

/**
 * Abstract transport interface for uDisplay connections.
 *
 * Concrete implementations: TcpTransport (TCP/WiFi), and in future BleTransport.
 * MockTransport is used in unit tests.
 *
 * The transport operates on fully assembled messages (no framing concerns at
 * this level — TcpTransport handles the 2-byte length prefix internally;
 * BleTransport handles ATT fragmentation internally).
 */
#pragma once

#include <QByteArray>
#include <QObject>
#include <QString>

class Transport : public QObject
{
    Q_OBJECT

public:
    explicit Transport(QObject* parent = nullptr) : QObject(parent) {}
    virtual ~Transport() = default;

    /** Send a complete, unframed message to the device. */
    virtual void send(const QByteArray& msg) = 0;

    /** Begin connecting. Emits connected() or errorOccurred() asynchronously. */
    virtual void connectToDevice() = 0;

    /** Disconnect and clean up. Emits disconnected() if currently connected. */
    virtual void disconnectFromDevice() = 0;

    virtual bool isConnected() const = 0;

signals:
    /** Emitted when the connection is established and ready to exchange messages. */
    void connected();

    /** Emitted when the connection is closed (normally or due to error). */
    void disconnected();

    /** Emitted for each fully assembled inbound message. */
    void messageReceived(QByteArray msg);

    /** Emitted on transport errors (connection refused, timeout, etc.). */
    void errorOccurred(QString errorString);
};
