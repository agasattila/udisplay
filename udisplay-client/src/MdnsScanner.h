// SPDX-License-Identifier: LGPL-3.0-only
// Copyright (c) 2026 Attila Agas

#pragma once
#include "IScanner.h"

#ifdef HAVE_QZEROCONF
#include <qzeroconf.h>
#endif

/**
 * mDNS device scanner using QZeroConf.
 *
 * Browses for _udisplay._tcp services. Requires libavahi-client-dev on Linux
 * (see CMakeLists.txt QZeroConf section for the conditional build).
 *
 * Without HAVE_QZEROCONF: emits a scanError on startScan() so the
 * DiscoveryModel shows an informational message; the manual TCP fallback
 * section in DiscoveryScreen.qml still works normally.
 */
class MdnsScanner : public IScanner
{
    Q_OBJECT

public:
    explicit MdnsScanner(QObject* parent = nullptr);
    ~MdnsScanner() override;

    void startScan() override;
    void stopScan() override;

#ifdef HAVE_QZEROCONF
private slots:
    void onServiceAdded(QZeroConfService service);
    void onServiceRemoved(QZeroConfService service);
    void onError(int error);

private:
    QZeroConf* m_zeroConf      = nullptr;
    bool       m_browserActive = false;
#endif
};
