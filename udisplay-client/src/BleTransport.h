// SPDX-License-Identifier: LGPL-3.0-only
// Copyright (c) 2026 Attila Agas

#pragma once
#include "Transport.h"
#include "Protocol.h"

#ifdef HAVE_BLE

#include <QBluetoothDeviceInfo>
#include <QBluetoothUuid>
#include <QLowEnergyController>
#include <QLowEnergyService>
#include <QQueue>

/**
 * BLE GATT transport.
 *
 * Implements the Transport interface over a BLE GATT connection.
 * Outbound messages are fragmented via Proto::bleFrame() into ATT packets
 * written sequentially to the Control characteristic (WRITE_WITH_RESPONSE).
 * Inbound ATT notifications on the Data characteristic are reassembled via
 * Proto::bleUnframe() and emitted as messageReceived().
 *
 * GATT service / characteristic layout (must match libudisplay firmware):
 *   Service  29825AAA-D882-46F7-A4D6-EA8431AD3455
 *     Ctrl   29825AAA-D882-46F7-A4D6-EA8431AD3456  (WRITE_WITH_RESPONSE)
 *     Data   29825AAA-D882-46F7-A4D6-EA8431AD3457  (NOTIFY)
 */
class BleTransport : public Transport
{
    Q_OBJECT

public:
    explicit BleTransport(const QBluetoothDeviceInfo& deviceInfo,
                          QObject* parent = nullptr);
    ~BleTransport() override;

    void send(const QByteArray& msg) override;
    void connectToDevice() override;
    void disconnectFromDevice() override;
    bool isConnected() const override;

private slots:
    void onControllerConnected();
    void onControllerDisconnected();
    void onControllerError(QLowEnergyController::Error error);
    void onDiscoveryFinished();
    void onServiceStateChanged(QLowEnergyService::ServiceState state);
    void onCharacteristicChanged(const QLowEnergyCharacteristic& c,
                                 const QByteArray& value);
    void onCharacteristicWritten(const QLowEnergyCharacteristic& c,
                                 const QByteArray& value);
    void drainWriteQueue();

private:
    static const QBluetoothUuid kUDisplaySvcUuid;
    static const QBluetoothUuid kCtrlCharUuid;
    static const QBluetoothUuid kDataCharUuid;

    QBluetoothDeviceInfo     m_deviceInfo;
    QLowEnergyController*    m_controller      = nullptr;
    QLowEnergyService*       m_service         = nullptr;
    QLowEnergyCharacteristic m_ctrlChar;
    QLowEnergyCharacteristic m_dataChar;
    bool                     m_connected       = false;
    /* packetId wraps 255→0; initial 0xFF makes first bleFrame() call produce id=0. */
    uint8_t                  m_txPacketId      = 0xFF;
    /* Updated by mtuChanged; floor of 7 (6-byte first-fragment header + 1 payload). */
    uint8_t                  m_attPayloadSize  = 20;
    Proto::BleRxState        m_rxState;
    QQueue<QByteArray>       m_writeQueue;
    bool                     m_writeInFlight   = false;
};

#endif /* HAVE_BLE */
