// SPDX-License-Identifier: LGPL-3.0-only
// Copyright (c) 2026 Attila Agas

#include "DiscoveryModel.h"
#include "MdnsScanner.h"
#include "BleScanner.h"

DiscoveryModel::DiscoveryModel(QObject* parent)
    : QAbstractListModel(parent)
{
    qRegisterMetaType<DeviceInfo>("DeviceInfo");

    addScanner(new MdnsScanner(this));
    addScanner(new BleScanner(this));
}

void DiscoveryModel::addScanner(IScanner* scanner)
{
    m_scanners.append(scanner);
    connect(scanner, &IScanner::deviceFound, this, &DiscoveryModel::onDeviceFound);
    connect(scanner, &IScanner::deviceLost,  this, &DiscoveryModel::onDeviceLost);
    connect(scanner, &IScanner::scanError,   this, &DiscoveryModel::onScanError);
}

/* ── QAbstractListModel ─────────────────────────────────────────────────── */

int DiscoveryModel::rowCount(const QModelIndex& parent) const
{
    if (parent.isValid()) return 0;
    return m_devices.size();
}

QVariant DiscoveryModel::data(const QModelIndex& index, int role) const
{
    if (!index.isValid() || index.row() >= m_devices.size())
        return {};

    const DeviceInfo& d = m_devices.at(index.row());
    switch (role) {
    case DisplayNameRole:  return d.displayName;
    case AddressRole:      return d.address;
    case PortRole:         return d.port;
    case TransportTypeRole: return static_cast<int>(d.type);
    case UniqueIdRole:     return d.uniqueId;
    case SourceLabelRole:  return d.sourceLabel();
    case RssiRole:         return d.rssi;
    default:               return {};
    }
}

QHash<int, QByteArray> DiscoveryModel::roleNames() const
{
    return {
        { DisplayNameRole,   "displayName" },
        { AddressRole,       "address" },
        { PortRole,          "port" },
        { TransportTypeRole, "transportType" },
        { UniqueIdRole,      "uniqueId" },
        { SourceLabelRole,   "sourceLabel" },
        { RssiRole,          "rssi" },
    };
}

QVariant DiscoveryModel::deviceAt(int row) const
{
    if (row < 0 || row >= m_devices.size()) return {};
    return QVariant::fromValue(m_devices.at(row));
}

/* ── Scan lifecycle ─────────────────────────────────────────────────────── */

void DiscoveryModel::startScan()
{
    setScanError(QString());
    for (auto* s : m_scanners)
        s->startScan();
    setScanning(true);
}

void DiscoveryModel::stopScan()
{
    for (auto* s : m_scanners)
        s->stopScan();
    setScanning(false);
}

/* ── Scanner callbacks ──────────────────────────────────────────────────── */

void DiscoveryModel::onDeviceFound(DeviceInfo info)
{
    /* Update in-place if uniqueId already present. */
    for (int i = 0; i < m_devices.size(); ++i) {
        if (m_devices.at(i).uniqueId == info.uniqueId) {
            m_devices[i] = info;
            const QModelIndex idx = index(i);
            emit dataChanged(idx, idx);
            return;
        }
    }
    /* New entry — append. */
    const int row = m_devices.size();
    beginInsertRows(QModelIndex(), row, row);
    m_devices.append(info);
    endInsertRows();
    emit countChanged();
}

void DiscoveryModel::onDeviceLost(QString uniqueId)
{
    for (int i = 0; i < m_devices.size(); ++i) {
        if (m_devices.at(i).uniqueId == uniqueId) {
            beginRemoveRows(QModelIndex(), i, i);
            m_devices.removeAt(i);
            endRemoveRows();
            emit countChanged();
            return;
        }
    }
}

void DiscoveryModel::onScanError(QString reason)
{
    setScanError(reason);
    setScanning(false);
}

/* ── Private setters ────────────────────────────────────────────────────── */

void DiscoveryModel::setScanning(bool v)
{
    if (m_scanning != v) {
        m_scanning = v;
        emit scanningChanged();
    }
}

void DiscoveryModel::setScanError(const QString& v)
{
    if (m_scanError != v) {
        m_scanError = v;
        emit scanErrorChanged();
    }
}
