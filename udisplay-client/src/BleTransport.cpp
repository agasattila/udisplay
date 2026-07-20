// SPDX-License-Identifier: LGPL-3.0-only
// Copyright (c) 2026 Attila Agas

#include "BleTransport.h"

#ifdef HAVE_BLE

#include <QLowEnergyCharacteristic>
#include <QLowEnergyDescriptor>

/* GATT service/characteristic UUIDs — generated for uDisplay, must match libudisplay firmware. */
const QBluetoothUuid BleTransport::kUDisplaySvcUuid{
    QStringLiteral("29825AAA-D882-46F7-A4D6-EA8431AD3455")};
const QBluetoothUuid BleTransport::kCtrlCharUuid{
    QStringLiteral("29825AAA-D882-46F7-A4D6-EA8431AD3456")};
const QBluetoothUuid BleTransport::kDataCharUuid{
    QStringLiteral("29825AAA-D882-46F7-A4D6-EA8431AD3457")};

/* CCCD value to enable notifications (little-endian 0x0001). */
static const QByteArray kCccdNotifyEnable = QByteArray::fromHex("0100");

BleTransport::BleTransport(const QBluetoothDeviceInfo& deviceInfo,
                           QObject* parent)
    : Transport(parent)
    , m_deviceInfo(deviceInfo)
{}

BleTransport::~BleTransport()
{
    disconnectFromDevice();
}

bool BleTransport::isConnected() const
{
    return m_connected;
}

void BleTransport::connectToDevice()
{
    if (m_controller) {
        m_controller->disconnectFromDevice();
        m_controller->deleteLater();
        m_controller = nullptr;
    }

    m_connected     = false;
    m_writeInFlight = false;
    m_writeQueue.clear();
    m_txPacketId    = 0xFF;
    m_rxState       = Proto::BleRxState{};

    m_controller = QLowEnergyController::createCentral(m_deviceInfo, this);

    connect(m_controller, &QLowEnergyController::connected,
            this, &BleTransport::onControllerConnected);
    connect(m_controller, &QLowEnergyController::disconnected,
            this, &BleTransport::onControllerDisconnected);
    connect(m_controller,
            QOverload<QLowEnergyController::Error>::of(
                &QLowEnergyController::errorOccurred),
            this, &BleTransport::onControllerError);
    connect(m_controller, &QLowEnergyController::discoveryFinished,
            this, &BleTransport::onDiscoveryFinished);
    connect(m_controller, &QLowEnergyController::mtuChanged,
            this, [this](int mtu) {
                m_attPayloadSize = static_cast<uint8_t>(qMax(7, mtu - 3));
            });

    m_controller->connectToDevice();
}

void BleTransport::disconnectFromDevice()
{
    m_connected     = false;
    m_writeInFlight = false;
    m_writeQueue.clear();

    if (m_service) {
        m_service->deleteLater();
        m_service = nullptr;
    }
    if (m_controller) {
        m_controller->disconnectFromDevice();
        m_controller->deleteLater();
        m_controller = nullptr;
    }
}

void BleTransport::send(const QByteArray& msg)
{
    if (!m_connected || !m_service)
        return;

    const QVector<QByteArray> fragments =
        Proto::bleFrame(msg, m_attPayloadSize, m_txPacketId);

    for (const QByteArray& frag : fragments)
        m_writeQueue.enqueue(frag);

    drainWriteQueue();
}

void BleTransport::drainWriteQueue()
{
    if (m_writeInFlight || m_writeQueue.isEmpty() || !m_service)
        return;

    m_writeInFlight = true;
    m_service->writeCharacteristic(
        m_ctrlChar,
        m_writeQueue.dequeue(),
        QLowEnergyService::WriteWithResponse);
}

/* ── QLowEnergyController slots ─────────────────────────────────────────── */

void BleTransport::onControllerConnected()
{
    m_controller->discoverServices();
}

void BleTransport::onControllerDisconnected()
{
    m_connected = false;
    emit disconnected();
}

void BleTransport::onControllerError(QLowEnergyController::Error error)
{
    Q_UNUSED(error)
    emit errorOccurred(m_controller ? m_controller->errorString()
                                    : QStringLiteral("BLE controller error"));
}

void BleTransport::onDiscoveryFinished()
{
    if (!m_controller->services().contains(kUDisplaySvcUuid)) {
        emit errorOccurred(QStringLiteral(
            "uDisplay GATT service not found on device"));
        return;
    }

    m_service = m_controller->createServiceObject(kUDisplaySvcUuid, this);
    if (!m_service) {
        emit errorOccurred(QStringLiteral("Failed to create GATT service object"));
        return;
    }

    connect(m_service, &QLowEnergyService::stateChanged,
            this, &BleTransport::onServiceStateChanged);
    connect(m_service, &QLowEnergyService::characteristicChanged,
            this, &BleTransport::onCharacteristicChanged);
    connect(m_service, &QLowEnergyService::characteristicWritten,
            this, &BleTransport::onCharacteristicWritten);

    m_service->discoverDetails();
}

void BleTransport::onServiceStateChanged(QLowEnergyService::ServiceState state)
{
    if (state != QLowEnergyService::RemoteServiceDiscovered)
        return;

    m_ctrlChar = m_service->characteristic(kCtrlCharUuid);
    m_dataChar = m_service->characteristic(kDataCharUuid);

    if (!m_ctrlChar.isValid() || !m_dataChar.isValid()) {
        emit errorOccurred(QStringLiteral(
            "uDisplay GATT characteristics not found"));
        return;
    }

    /* Enable notifications on the Data characteristic via its CCCD. */
    const QLowEnergyDescriptor cccd = m_dataChar.descriptor(
        QBluetoothUuid::DescriptorType::ClientCharacteristicConfiguration);
    if (cccd.isValid())
        m_service->writeDescriptor(cccd, kCccdNotifyEnable);

    m_connected = true;
    emit connected();
}

void BleTransport::onCharacteristicChanged(const QLowEnergyCharacteristic& c,
                                           const QByteArray& value)
{
    if (c.uuid() != kDataCharUuid)
        return;

    QByteArray msg;
    if (Proto::bleUnframe(value, m_rxState, msg))
        emit messageReceived(msg);
}

void BleTransport::onCharacteristicWritten(const QLowEnergyCharacteristic& c,
                                           const QByteArray& value)
{
    Q_UNUSED(c)
    Q_UNUSED(value)
    m_writeInFlight = false;
    drainWriteQueue();
}

#endif /* HAVE_BLE */
