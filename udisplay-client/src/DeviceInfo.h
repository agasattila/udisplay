// SPDX-License-Identifier: LGPL-3.0-only
// Copyright (c) 2026 Attila Agas

#pragma once
#include <QMetaType>
#include <QString>
#include <QVariant>

/**
 * Value type representing a discovered device candidate.
 *
 * Produced by IScanner implementations, consumed by DiscoveryModel.
 * Registered as a metatype so it can cross signal/slot boundaries.
 *
 * TransportType: Tcp=0, Ble=1  (matches connectDevice() int parameter)
 *
 * nativeHandle: transport-specific opaque handle.
 *   BLE: holds QBluetoothDeviceInfo so BleTransport can connect without
 *        re-scanning.  Empty QVariant for TCP devices.
 */
struct DeviceInfo
{
    enum TransportType {
        Tcp = 0,
        Ble = 1
    };

    TransportType type       = Tcp;
    QString       uniqueId;     /* mDNS instance name or BT address */
    QString       displayName;  /* human-readable label for UI */
    QString       address;      /* IP address (TCP) or BT address (BLE) */
    int           port   = 0;   /* TCP port; 0 for BLE */
    int           rssi   = -1;  /* signal strength in dBm; -1 if unavailable */
    QVariant      nativeHandle; /* QBluetoothDeviceInfo for BLE; empty for TCP */

    QString sourceLabel() const {
        return type == Ble ? QStringLiteral("Bluetooth") : QStringLiteral("Wi-Fi");
    }

    static DeviceInfo makeTcp(const QString& uniqueId, const QString& displayName,
                              const QString& address, int port)
    {
        DeviceInfo d;
        d.type = Tcp; d.uniqueId = uniqueId; d.displayName = displayName;
        d.address = address; d.port = port; d.rssi = -1;
        return d;
    }

    static DeviceInfo makeBle(const QString& uniqueId, const QString& displayName,
                              const QString& address, int rssi = -1,
                              const QVariant& handle = {})
    {
        DeviceInfo d;
        d.type = Ble; d.uniqueId = uniqueId; d.displayName = displayName;
        d.address = address; d.port = 0; d.rssi = rssi; d.nativeHandle = handle;
        return d;
    }

    bool operator==(const DeviceInfo& o) const { return uniqueId == o.uniqueId; }
    bool operator!=(const DeviceInfo& o) const { return uniqueId != o.uniqueId; }
};

Q_DECLARE_METATYPE(DeviceInfo)
