// SPDX-License-Identifier: LGPL-3.0-only
// Copyright (c) 2026 Attila Agas

#include "BleScanner.h"
#include "DeviceInfo.h"

#ifdef HAVE_BLE
#include <QBluetoothDeviceInfo>
#include <QVariant>

#ifdef Q_OS_ANDROID
// #include <QtCore/private/qandroidextras_p.h>
#endif

/* uDisplay GATT service UUID — generated for uDisplay
 * Must match libudisplay firmware advertisement. */
const QBluetoothUuid BleScanner::kUDisplaySvcUuid{
    QStringLiteral("29825AAA-D882-46F7-A4D6-EA8431AD3455")};

BleScanner::BleScanner(QObject* parent) : IScanner(parent), _started(false)
{
    _elapsedTimer.start();
}

void BleScanner::startScan()
{
    if (m_agent) {
        m_agent->stop();
        m_agent->deleteLater();
        m_agent = nullptr;
    }

    _devices.clear();

    this->_permGranted = false;

#ifdef Q_OS_ANDROID
#if QT_VERSION >= QT_VERSION_CHECK(6, 5, 0)
    /* On Android 12+ Qt 6.5+ surfaces runtime permission requests automatically
     * through QBluetoothDeviceDiscoveryAgent::start().  No explicit request
     * needed here — the agent will emit error(InputOutputError) if the user
     * denies and the caller can surface the message from the scanError signal. */
#endif
#endif

    m_agent = new QBluetoothDeviceDiscoveryAgent(this);
    m_agent->setLowEnergyDiscoveryTimeout(0);

    connect(m_agent, &QBluetoothDeviceDiscoveryAgent::deviceDiscovered,
            this, &BleScanner::onDeviceDiscovered);
    connect(m_agent, &QBluetoothDeviceDiscoveryAgent::deviceUpdated,
            this, &BleScanner::onDeviceUpdated);
    connect(m_agent, &QBluetoothDeviceDiscoveryAgent::finished,
            this, &BleScanner::onScanFinished);
    connect(m_agent,
            QOverload<QBluetoothDeviceDiscoveryAgent::Error>::of(
                &QBluetoothDeviceDiscoveryAgent::errorOccurred),
            this, &BleScanner::onAgentError);

    _availabilityTimer.setInterval(500);

    connect(&_availabilityTimer, &QTimer::timeout,
            this, &BleScanner::checkAvailability);

    _availabilityTimer.start();

    m_agent->start(QBluetoothDeviceDiscoveryAgent::LowEnergyMethod);
    _started = true;
}

void BleScanner::stopScan()
{
    if (m_agent) {
        _started = false;
        m_agent->stop();
        _availabilityTimer.stop();
    }
}

QString BleScanner::deviceKey(const QBluetoothDeviceInfo &info) const
{
#if defined(Q_OS_MACOS) || defined(Q_OS_IOS)
    return info.deviceUuid().toString();
#else
    if (!info.address().isNull())
        return info.address().toString();

    return info.deviceUuid().toString();
#endif
}

void BleScanner::processDevice(const QBluetoothDeviceInfo &info)
{
    /* Filter: only report devices that advertise the uDisplay service UUID. */
    if (!info.serviceUuids().contains(kUDisplaySvcUuid))
        return;

    const QString key = deviceKey(info);

    auto &entry = _devices[key];

    entry.lastSeenMs = _elapsedTimer.elapsed();


    const QString& addr = key;
    DeviceInfo di = DeviceInfo::makeBle(
        addr,
        info.name().isEmpty() ? addr : info.name(),
        addr,
        info.rssi(),
        QVariant::fromValue(info));

    if (!entry.available || entry.info != di)
        emit deviceFound(di); //Report discovered or updated

    entry.info = di;

    if (!entry.available) {
        entry.available = true;
    }

}

void BleScanner::onDeviceDiscovered(const QBluetoothDeviceInfo& info)
{
    processDevice(info);
}

void BleScanner::onDeviceUpdated(const QBluetoothDeviceInfo& info)
{
    processDevice(info);
}


void BleScanner::onScanFinished()
{
    /* Scan completed naturally (timeout or explicit stop) — no error. */
    /*
    if (m_agent) {
        QList<QBluetoothDeviceInfo> devices = m_agent -> discoveredDevices();
        QSet<QString> filtered_devices;
        for(auto& device : devices) {
            if (device.serviceUuids().contains(kUDisplaySvcUuid)) {
                const QString addr = device.address().toString();
                filtered_devices.insert(addr);
            }
        }

        for(auto& addr : _devices) {
            if (!filtered_devices.contains(addr))
                deviceLost(addr);
        }

        if (_started) {
            // _Discovered.clear();
            m_agent->start(QBluetoothDeviceDiscoveryAgent::LowEnergyMethod);
        }
    }*/
}

void BleScanner::onAgentError(QBluetoothDeviceDiscoveryAgent::Error error)
{
    Q_UNUSED(error)
    if (m_agent)
        emit scanError(m_agent->errorString());
}

void BleScanner::checkAvailability()
{
    constexpr qint64 unavailableAfterMs = 5000;

    const qint64 now = _elapsedTimer.elapsed();

    for (auto it = _devices.begin(); it != _devices.end(); ++it) {
        DeviceEntry &entry = it.value();

        if (entry.available &&
            now - entry.lastSeenMs > unavailableAfterMs) {

            entry.available = false;
            emit deviceLost(it.key());
        }
    }
}

#else /* !HAVE_BLE */

BleScanner::BleScanner(QObject* parent) : IScanner(parent) {}

void BleScanner::startScan()
{
    emit scanError(QStringLiteral("Bluetooth not available in this build"));
}

void BleScanner::stopScan() {}

#endif /* HAVE_BLE */
