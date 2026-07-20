// SPDX-License-Identifier: LGPL-3.0-only
// Copyright (c) 2026 Attila Agas

#pragma once
#include "DeviceInfo.h"
#include <QAbstractListModel>
#include <QList>

class IScanner;

/**
 * List model of discovered uDisplay devices.
 *
 * Registered as "discoveryModel" QML context property in main.cpp.
 * Aggregates results from MdnsScanner and BleScanner (plug-in architecture
 * mirrors Transport/TcpTransport). Manual TCP entry lives in DiscoveryScreen.qml.
 *
 * QML roles:
 *   DisplayNameRole  — device display name
 *   AddressRole      — IP address (TCP) or BT address (BLE)
 *   PortRole         — TCP port (0 for BLE)
 *   TransportTypeRole — int: 0=TCP, 1=BLE
 *   UniqueIdRole     — stable dedup key (mDNS instance name or BT addr)
 *   SourceLabelRole  — "Wi-Fi" or "Bluetooth"
 *   RssiRole         — signal strength in dBm; -1 if unavailable
 *
 * scanError and scanning are Q_PROPERTYs on the model object, not per-row roles.
 */
class DiscoveryModel : public QAbstractListModel
{
    Q_OBJECT

    Q_PROPERTY(bool scanning READ scanning NOTIFY scanningChanged)
    Q_PROPERTY(QString scanError READ scanError NOTIFY scanErrorChanged)
    Q_PROPERTY(int count READ deviceCount NOTIFY countChanged)

public:
    enum Roles {
        DisplayNameRole  = Qt::UserRole + 1,
        AddressRole,
        PortRole,
        TransportTypeRole,
        UniqueIdRole,
        SourceLabelRole,
        RssiRole
    };

    explicit DiscoveryModel(QObject* parent = nullptr);

    /* QAbstractListModel */
    int      rowCount(const QModelIndex& parent = QModelIndex()) const override;
    QVariant data(const QModelIndex& index, int role = Qt::DisplayRole) const override;
    QHash<int, QByteArray> roleNames() const override;

    bool    scanning()    const { return m_scanning; }
    QString scanError()   const { return m_scanError; }
    int     deviceCount() const { return m_devices.size(); }

public slots:
    Q_INVOKABLE void startScan();
    Q_INVOKABLE void stopScan();
    /** Return DeviceInfo at row as QVariant for QML → DeviceController::connectDiscovered(). */
    Q_INVOKABLE QVariant deviceAt(int row) const;

signals:
    void scanningChanged();
    void scanErrorChanged();
    void countChanged();

private slots:
    void onDeviceFound(DeviceInfo info);
    void onDeviceLost(QString uniqueId);
    void onScanError(QString reason);

private:
    void addScanner(IScanner* scanner);
    void setScanning(bool v);
    void setScanError(const QString& v);

    QList<DeviceInfo> m_devices;
    QList<IScanner*>  m_scanners;
    bool              m_scanning  = false;
    QString           m_scanError;
};
