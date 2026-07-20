// SPDX-License-Identifier: LGPL-3.0-only
// Copyright (c) 2026 Attila Agas

#include "TcpTransport.h"
#include "Protocol.h"

TcpTransport::TcpTransport(const QString& host, quint16 port, QObject* parent)
    : Transport(parent), m_host(host), m_port(port)
{
    connect(&m_socket, &QTcpSocket::connected,
            this, &TcpTransport::onSocketConnected);
    connect(&m_socket, &QTcpSocket::disconnected,
            this, &TcpTransport::onSocketDisconnected);
    connect(&m_socket, &QTcpSocket::readyRead,
            this, &TcpTransport::onReadyRead);
    connect(&m_socket, &QAbstractSocket::errorOccurred,
            this, &TcpTransport::onSocketError);
}

void TcpTransport::connectToDevice()
{
    m_readBuf.clear();
    m_socket.connectToHost(m_host, m_port);
}

void TcpTransport::disconnectFromDevice()
{
    m_socket.disconnectFromHost();
}

bool TcpTransport::isConnected() const
{
    return m_socket.state() == QAbstractSocket::ConnectedState;
}

void TcpTransport::send(const QByteArray& msg)
{
    if (!isConnected()) return;
    m_socket.write(Proto::tcpFrame(msg));
}

void TcpTransport::onSocketConnected()
{
    emit connected();
}

void TcpTransport::onSocketDisconnected()
{
    emit disconnected();
}

void TcpTransport::onReadyRead()
{
    m_readBuf.append(m_socket.readAll());

    /* Consume as many complete frames as available. */
    while (true) {
        QByteArray msg;
        int consumed = 0;
        if (!Proto::tcpUnframe(m_readBuf, msg, consumed))
            break;
        m_readBuf.remove(0, consumed);
        emit messageReceived(msg);
    }
}

void TcpTransport::onSocketError(QAbstractSocket::SocketError)
{
    emit errorOccurred(m_socket.errorString());
}
