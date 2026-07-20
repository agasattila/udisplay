// SPDX-License-Identifier: LGPL-3.0-only
// Copyright (c) 2026 Attila Agas

#include "MdnsScanner.h"
#include "Protocol.h"

#ifdef HAVE_QZEROCONF
#include <qzeroconf.h>
#endif

MdnsScanner::MdnsScanner(QObject* parent) : IScanner(parent)
{
#ifdef HAVE_QZEROCONF
    m_zeroConf = new QZeroConf(this);
    connect(m_zeroConf, &QZeroConf::serviceAdded,
            this, &MdnsScanner::onServiceAdded);
    connect(m_zeroConf, &QZeroConf::serviceUpdated,
            this, &MdnsScanner::onServiceAdded);
    connect(m_zeroConf, &QZeroConf::serviceRemoved,
            this, &MdnsScanner::onServiceRemoved);
    connect(m_zeroConf, QOverload<QZeroConf::error_t>::of(&QZeroConf::error),
            this, [this](QZeroConf::error_t e) { onError(static_cast<int>(e)); });
#endif
}

MdnsScanner::~MdnsScanner()
{
    stopScan();
}

void MdnsScanner::startScan()
{
#ifdef HAVE_QZEROCONF
    if (!m_browserActive) {
        m_zeroConf->startBrowser(
            QString::fromLatin1(Proto::kMdnsServiceType));
        m_browserActive = true;
    }
#else
    emit scanError(QStringLiteral(
        "mDNS discovery unavailable — install libavahi-client-dev and rebuild"));
#endif
}

void MdnsScanner::stopScan()
{
#ifdef HAVE_QZEROCONF
    if (m_browserActive) {
        m_zeroConf->stopBrowser();
        m_browserActive = false;
    }
#endif
}

#ifdef HAVE_QZEROCONF
void MdnsScanner::onServiceAdded(QZeroConfService service)
{
    if (!service) return;
    /* IP may not be resolved yet at serviceAdded time; serviceUpdated carries it. */
    if (service->ip().isNull()) return;

    /* Prefer TXT "name" record for display; fall back to mDNS instance name. */
    QString name = service->name();
    const auto& txt = service->txt();
    if (txt.contains("name") && !txt["name"].isEmpty())
        name = QString::fromUtf8(txt["name"]);

    DeviceInfo info = DeviceInfo::makeTcp(
        service->name(),          /* uniqueId = stable mDNS instance name */
        name,
        service->ip().toString(),
        static_cast<int>(service->port()));

    emit deviceFound(info);
}

void MdnsScanner::onServiceRemoved(QZeroConfService service)
{
    if (service)
        emit deviceLost(service->name());
}

void MdnsScanner::onError(int error)
{
    if (error != 0) /* QZeroConf::noError == 0 */
        emit scanError(QStringLiteral("mDNS browser error (code %1)").arg(error));
}
#endif
