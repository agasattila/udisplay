// SPDX-License-Identifier: LGPL-3.0-only
// Copyright (c) 2026 Attila Agas

#pragma once
#include "Transport.h"
#include "Protocol.h"

#ifdef HAVE_BLE

#include <QBluetoothDeviceInfo>
#include <QBluetoothUuid>
#include <QLowEnergyController>
#include <QLowEnergyDescriptor>
#include <QLowEnergyService>
#include <QQueue>

/**
 * BLE GATT transport.
 *
 * Implements the Transport interface over a BLE GATT connection.
 * Outbound messages are fragmented via Proto::bleFrame() into ATT packets
 * written sequentially to the Control characteristic (WRITE_WITH_RESPONSE).
 * Inbound ATT indications on the Data characteristic are reassembled via
 * Proto::bleFeed() and emitted as messageReceived(). A framing error is a
 * link error: the transport reports it and disconnects.
 *
 * connected() is emitted only once the Data characteristic's CCCD write
 * (0x0002, indications on) is confirmed: indication delivery is part of the
 * transport contract, not an optional setup step. A missing CCCD or a failed
 * GATT write is a link error.
 *
 * GATT service / characteristic layout (must match libudisplay firmware):
 *   Service  29825AAA-D882-46F7-A4D6-EA8431AD3455
 *     Ctrl   29825AAA-D882-46F7-A4D6-EA8431AD3456  (WRITE_WITH_RESPONSE)
 *     Data   29825AAA-D882-46F7-A4D6-EA8431AD3457  (INDICATE)
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
    void onDescriptorWritten(const QLowEnergyDescriptor& d,
                             const QByteArray& value);
    void onServiceError(QLowEnergyService::ServiceError error);
    void drainWriteQueue();

private:
    void failLink(const QString& reason);

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
