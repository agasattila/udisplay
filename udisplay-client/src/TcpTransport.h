// SPDX-License-Identifier: LGPL-3.0-only
// Copyright (c) 2026 Attila Agas

/**
 * TCP transport for uDisplay WiFi connections.
 *
 * Handles the 2-byte LE length prefix framing internally.
 * The caller sees only fully assembled, unframed messages.
 */
#pragma once

#include "Transport.h"
#include <QTcpSocket>

class TcpTransport : public Transport
{
    Q_OBJECT

public:
    explicit TcpTransport(const QString& host, quint16 port,
                          QObject* parent = nullptr);

    void send(const QByteArray& msg) override;
    void connectToDevice() override;
    void disconnectFromDevice() override;
    bool isConnected() const override;

private slots:
    void onSocketConnected();
    void onSocketDisconnected();
    void onReadyRead();
    void onSocketError(QAbstractSocket::SocketError error);

private:
    QTcpSocket m_socket;
    QString    m_host;
    quint16    m_port;
    QByteArray m_readBuf; /* accumulates partial TCP frames */
};
