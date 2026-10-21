// SPDX-License-Identifier: LGPL-3.0-only
// Copyright (c) 2026 Attila Agas

#pragma once

#include <QByteArray>
#include <QObject>
#include <QQueue>
#include <QTimer>

/**
 * Outbound ATT write queue for BleTransport, kept free of Qt Bluetooth so it
 * can be unit-tested without an adapter.
 *
 * Writes go out one at a time: writeRequested() hands out the next fragment
 * only after confirm() reports that the previous one was acknowledged
 * (WRITE_WITH_RESPONSE). Each fragment handed out starts a single-shot timer.
 * If confirm() doesn't arrive in time, the queue drops everything and emits
 * timedOut(). The transport treats that as a dead link.
 *
 *   enqueue ─► idle? ─► in flight, timer started ─► writeRequested(frag)
 *   confirm ─► timer stopped ─► next fragment, or idle
 *   timer   ─► cleared ─► timedOut()
 *
 * Re-entrancy: handlers may call clear()/enqueue()/confirm() synchronously
 * from inside either signal (a timeout runs BleTransport::failLink, which can
 * end in DeviceController::teardown → disconnectFromDevice → clear()). The
 * queue therefore finishes changing its own state before every emit and never
 * touches its members after one. A backend that confirms synchronously inside
 * the write nests one writeRequested per queued fragment; client-to-device
 * messages are only a few fragments long, so the depth stays small.
 */
class BleWriteQueue : public QObject
{
    Q_OBJECT

public:
    /* Starting value from issue #23. Tune it once real-device data exists
     * (TODO-042). */
    static constexpr int kDefaultTimeoutMs = 5'000;

    explicit BleWriteQueue(int timeoutMs = kDefaultTimeoutMs,
                           QObject* parent = nullptr);

    /** Queue a fragment; it is handed out at once if nothing is in flight. */
    void enqueue(const QByteArray& fragment);

    /** The in-flight write was confirmed. Ignored when nothing is in flight
     *  (a confirmation that arrives after a timeout or clear()). */
    void confirm();

    /** Drop the in-flight write and all queued fragments; stop the timer. */
    void clear();

    /** True when nothing is in flight and nothing is queued. */
    bool isIdle() const;

    int timeoutMs() const { return m_timer.interval(); }

signals:
    /** Perform this ATT write now; call confirm() when it is acknowledged. */
    void writeRequested(const QByteArray& fragment);

    /** The in-flight write wasn't confirmed within timeoutMs(). The queue is
     *  already empty when this is emitted. */
    void timedOut();

private:
    void sendNext();
    void onTimeout();

    QQueue<QByteArray> m_pending;
    QTimer             m_timer;
    bool               m_inFlight = false;
};
