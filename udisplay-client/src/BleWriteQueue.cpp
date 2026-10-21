// SPDX-License-Identifier: LGPL-3.0-only
// Copyright (c) 2026 Attila Agas

#include "BleWriteQueue.h"

BleWriteQueue::BleWriteQueue(int timeoutMs, QObject* parent)
    : QObject(parent)
{
    m_timer.setSingleShot(true);
    m_timer.setInterval(timeoutMs);
    connect(&m_timer, &QTimer::timeout, this, &BleWriteQueue::onTimeout);
}

void BleWriteQueue::enqueue(const QByteArray& fragment)
{
    m_pending.enqueue(fragment);
    if (!m_inFlight)
        sendNext();
}

void BleWriteQueue::confirm()
{
    if (!m_inFlight)
        return;
    m_inFlight = false;
    m_timer.stop();
    sendNext();
}

void BleWriteQueue::clear()
{
    m_inFlight = false;
    m_timer.stop();
    m_pending.clear();
}

bool BleWriteQueue::isIdle() const
{
    return !m_inFlight && m_pending.isEmpty();
}

void BleWriteQueue::sendNext()
{
    if (m_pending.isEmpty())
        return;
    /* State first: the handler may confirm, clear or enqueue synchronously. */
    const QByteArray fragment = m_pending.dequeue();
    m_inFlight = true;
    m_timer.start();
    emit writeRequested(fragment);
}

void BleWriteQueue::onTimeout()
{
    clear();
    emit timedOut();
}
