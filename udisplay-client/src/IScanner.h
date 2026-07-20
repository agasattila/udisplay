// SPDX-License-Identifier: LGPL-3.0-only
// Copyright (c) 2026 Attila Agas

#pragma once
#include "DeviceInfo.h"
#include <QObject>

/**
 * Abstract scanner interface — parallel to Transport.h.
 *
 * Concrete implementations: MdnsScanner (mDNS/DNS-SD via QZeroConf),
 * BleScanner (TODO-011, Qt Bluetooth). DiscoveryModel owns both.
 *
 * Implementations must be safe to call startScan/stopScan multiple times.
 */
class IScanner : public QObject
{
    Q_OBJECT

public:
    explicit IScanner(QObject* parent = nullptr) : QObject(parent) {}
    virtual ~IScanner() = default;

    virtual void startScan() = 0;
    virtual void stopScan() = 0;

signals:
    /** Emitted when a new device is found or its properties change. */
    void deviceFound(DeviceInfo info);

    /** Emitted when a device is no longer visible (deregistered / out of range). */
    void deviceLost(QString uniqueId);

    /** Emitted on non-fatal scanner errors (e.g., daemon not running). */
    void scanError(QString reason);
};
