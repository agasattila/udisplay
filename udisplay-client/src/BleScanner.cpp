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

#ifdef Q_OS_ANDROID
    /* BLUETOOTH_SCAN/BLUETOOTH_CONNECT (API 31+) and ACCESS_FINE_LOCATION
     * are declared in AndroidManifest.xml but are runtime-dangerous
     * permissions — declaring them does not grant them. Request explicitly
     * before starting the agent; starting without a granted permission is
     * what silently returns zero scan results on-device. */
    requestAndroidPermissionsThenStart();
#else
    beginAgentScan();
#endif
}

void BleScanner::stopScan()
{
    if (m_agent) {
        _started = false;
        m_agent->stop();
        _availabilityTimer.stop();
    }
#ifdef Q_OS_ANDROID
    /* Invalidate any permission-request callback still in flight so it
     * cannot start a scan after the caller has already stopped/left. */
    ++m_scanGeneration;
#endif
}

#ifdef Q_OS_ANDROID
template <typename Permission>
void BleScanner::requestPermissionThenContinue(const Permission& permission,
                                                int generation,
                                                const QString& deniedMessage,
                                                std::function<void()> onGranted)
{
    auto proceed = [this, generation, permission, deniedMessage, onGranted]() {
        if (generation != m_scanGeneration)
            return; /* stopScan() or a newer startScan() ran meanwhile */

        if (qApp->checkPermission(permission) != Qt::PermissionStatus::Granted) {
            emit scanError(deniedMessage);
            return;
        }
        onGranted();
    };

    if (qApp->checkPermission(permission) == Qt::PermissionStatus::Undetermined) {
        qApp->requestPermission(permission, [proceed](const QPermission&) { proceed(); });
    } else {
        proceed();
    }
}

void BleScanner::requestAndroidPermissionsThenStart()
{
    /* Known narrow limitation: if startScan() is called again while an OS
     * permission dialog from a PRIOR call is still on screen (no stopScan()
     * in between), this newer call's generation wins — the older request's
     * callback becomes stale and no-ops — but it also issues a second
     * qApp->requestPermission() call for the same still-Undetermined
     * permission. DiscoveryScreen.qml's OWN transitions always pair
     * startScan() with a stopScan() first, but DeviceController can emit a
     * redundant stateChanged("error") for the SAME error (setError() has no
     * value-guard, unlike setState()) when a dropped TCP connection fires
     * both Transport::errorOccurred and Transport::disconnected — QML's
     * onStateChanged doesn't diff the value either, so that can call
     * startScan() twice with no stopScan() between them. See TODOS.md for
     * the DeviceController-side fix (out of scope here — the affected code
     * has nothing to do with Android permissions); an in-flight guard here
     * would only be needed if that fix isn't landed first. */
    const int generation = ++m_scanGeneration;

    QBluetoothPermission btPermission;
    btPermission.setCommunicationModes(QBluetoothPermission::Access);

    QLocationPermission locPermission;
    locPermission.setAccuracy(QLocationPermission::Precise);

    requestPermissionThenContinue(btPermission, generation,
        QStringLiteral("Bluetooth permission denied — enable it in Android "
                       "Settings to discover devices."),
        [this, generation, locPermission]() {
            requestPermissionThenContinue(locPermission, generation,
                QStringLiteral("Location permission denied — Android requires "
                               "it for Bluetooth scanning. Enable it in "
                               "Settings to discover devices."),
                [this]() { beginAgentScan(); });
        });
}
#endif

void BleScanner::beginAgentScan()
{
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
