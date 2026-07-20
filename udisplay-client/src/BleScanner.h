// SPDX-License-Identifier: LGPL-3.0-only
// Copyright (c) 2026 Attila Agas

#pragma once
#include "IScanner.h"

#ifdef HAVE_BLE
#include <QBluetoothDeviceDiscoveryAgent>
#include <QBluetoothUuid>
#include <QElapsedTimer>
#include <QTimer>
#endif

/**
 * BLE device scanner.
 *
 * When built with HAVE_BLE (Qt6::Bluetooth available), uses
 * QBluetoothDeviceDiscoveryAgent to scan for BLE devices advertising the
 * uDisplay service UUID (29825AAA-...).  Stores QBluetoothDeviceInfo in
 * DeviceInfo::nativeHandle so BleTransport can connect without re-scanning.
 *
 * Without HAVE_BLE the scanner is a stub that emits scanError() on startScan().
 */
class BleScanner : public IScanner
{
    Q_OBJECT

public:
    explicit BleScanner(QObject* parent = nullptr);

    void startScan() override;
    void stopScan() override;

#ifdef HAVE_BLE
private slots:
    void onDeviceDiscovered(const QBluetoothDeviceInfo& info);
    void onDeviceUpdated(const QBluetoothDeviceInfo& info);
    void onScanFinished();
    void onAgentError(QBluetoothDeviceDiscoveryAgent::Error error);
    void checkAvailability();

private:
    QString deviceKey(const QBluetoothDeviceInfo &info) const;
    void processDevice(const QBluetoothDeviceInfo& info);

    QBluetoothDeviceDiscoveryAgent* m_agent = nullptr;

    /* uDisplay GATT service UUID — must match libudisplay firmware. */
    static const QBluetoothUuid kUDisplaySvcUuid;

    bool _permGranted;
    bool _started;

    struct DeviceEntry
    {
        DeviceInfo info;
        qint64 lastSeenMs = 0;
        bool available = false;
    };

    QHash<QString, DeviceEntry> _devices;
    QElapsedTimer _elapsedTimer;
    QTimer _availabilityTimer;
#endif
};
