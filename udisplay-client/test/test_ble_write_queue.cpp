/**
 * BleWriteQueue tests: one-in-flight ordering, the per-write ATT timeout
 * (issue #23 / TODO-030), late confirmations, clear(), and re-entrant calls
 * from inside the queue's own signals (BleTransport::failLink can end in
 * DeviceController::teardown → clear() while timedOut is still being emitted).
 *
 * Timing tests use wide margins (hundreds of ms) so a loaded build host
 * doesn't make them flaky.
 */
#include <QtTest>
#include "BleWriteQueue.h"

class TestBleWriteQueue : public QObject
{
    Q_OBJECT

    static constexpr int kLongMs = 60'000; /* never fires during a test */

    static QList<QByteArray> written(const QSignalSpy& spy)
    {
        QList<QByteArray> out;
        for (const QList<QVariant>& args : spy)
            out << args.at(0).toByteArray();
        return out;
    }

private slots:
    void defaultTimeout_isFiveSeconds()
    {
        QCOMPARE(BleWriteQueue::kDefaultTimeoutMs, 5'000);
        BleWriteQueue q;
        QCOMPARE(q.timeoutMs(), 5'000);
    }

    void writes_oneAtATime_inOrder()
    {
        BleWriteQueue q(kLongMs);
        QSignalSpy writes(&q, &BleWriteQueue::writeRequested);

        q.enqueue("a");
        q.enqueue("b");
        q.enqueue("c");
        QCOMPARE(written(writes), (QList<QByteArray>{"a"}));
        QVERIFY(!q.isIdle());

        q.confirm();
        QCOMPARE(written(writes), (QList<QByteArray>{"a", "b"}));
        q.confirm();
        QCOMPARE(written(writes), (QList<QByteArray>{"a", "b", "c"}));
        q.confirm();
        QCOMPARE(writes.count(), 3);
        QVERIFY(q.isIdle());

        /* Idle again: the next fragment goes out immediately. */
        q.enqueue("d");
        QCOMPARE(written(writes).last(), QByteArray("d"));
    }

    void unconfirmedWrite_timesOutOnce_andDropsQueue()
    {
        BleWriteQueue q(50);
        QSignalSpy writes(&q, &BleWriteQueue::writeRequested);
        QSignalSpy timeouts(&q, &BleWriteQueue::timedOut);

        q.enqueue("a");
        q.enqueue("b");
        QVERIFY(timeouts.wait(2'000));
        QCOMPARE(timeouts.count(), 1);
        QVERIFY(q.isIdle());
        QCOMPARE(written(writes), (QList<QByteArray>{"a"})); /* "b" dropped */

        /* Single-shot: nothing in flight, so no second timeout. */
        QTest::qWait(200);
        QCOMPARE(timeouts.count(), 1);
    }

    void timer_restartsForEachFragment()
    {
        BleWriteQueue q(1'000);
        QSignalSpy writes(&q, &BleWriteQueue::writeRequested);
        QSignalSpy timeouts(&q, &BleWriteQueue::timedOut);

        q.enqueue("a");
        q.enqueue("b");
        QTest::qWait(700);
        q.confirm();                     /* "b" goes out with a fresh window */
        QCOMPARE(writes.count(), 2);
        QTest::qWait(700);               /* 1.4 s since "a", 0.7 s since "b" */
        QCOMPARE(timeouts.count(), 0);

        QVERIFY(timeouts.wait(2'000));   /* "b" itself is never confirmed */
        QCOMPARE(timeouts.count(), 1);
    }

    void confirm_afterTimeout_isIgnored()
    {
        BleWriteQueue q(50);
        QSignalSpy writes(&q, &BleWriteQueue::writeRequested);
        QSignalSpy timeouts(&q, &BleWriteQueue::timedOut);

        q.enqueue("a");
        q.enqueue("b");
        QVERIFY(timeouts.wait(2'000));

        q.confirm();                     /* late ack for "a" on a dead link */
        QCOMPARE(writes.count(), 1);
        QVERIFY(q.isIdle());
    }

    void confirm_whenIdle_isNoOp()
    {
        BleWriteQueue q(kLongMs);
        QSignalSpy writes(&q, &BleWriteQueue::writeRequested);

        q.confirm();
        QCOMPARE(writes.count(), 0);
        QVERIFY(q.isIdle());

        q.enqueue("a");
        QCOMPARE(writes.count(), 1);
    }

    void clear_stopsTimer_andDropsQueue()
    {
        BleWriteQueue q(100);
        QSignalSpy writes(&q, &BleWriteQueue::writeRequested);
        QSignalSpy timeouts(&q, &BleWriteQueue::timedOut);

        q.enqueue("a");
        q.enqueue("b");
        q.clear();
        QVERIFY(q.isIdle());

        QTest::qWait(400);
        QCOMPARE(timeouts.count(), 0);   /* no stale timer from the old link */
        QCOMPARE(writes.count(), 1);

        q.enqueue("c");                  /* reconnect: starts from scratch */
        QCOMPARE(written(writes).last(), QByteArray("c"));
    }

    /* ── Re-entrancy ─────────────────────────────────────────────────────── */

    void timedOutHandler_canClearAndEnqueue()
    {
        BleWriteQueue q(50);
        QSignalSpy writes(&q, &BleWriteQueue::writeRequested);
        QSignalSpy timeouts(&q, &BleWriteQueue::timedOut);
        connect(&q, &BleWriteQueue::timedOut, this, [&q]() {
            q.clear();                   /* what teardown → disconnect does */
            q.enqueue("x");
        });

        q.enqueue("a");
        QVERIFY(timeouts.wait(2'000));
        /* "x" survives: the queue doesn't touch its state after emitting. */
        QCOMPARE(written(writes), (QList<QByteArray>{"a", "x"}));
        QVERIFY(!q.isIdle());
    }

    void writeHandler_confirmingSynchronously_drainsInOrder()
    {
        BleWriteQueue q(100);
        QList<QByteArray> seen;
        connect(&q, &BleWriteQueue::writeRequested, this,
                [&q, &seen](const QByteArray& f) {
                    seen << f;
                    q.confirm();
                });
        QSignalSpy timeouts(&q, &BleWriteQueue::timedOut);

        /* Each fragment is acked inside its own write, so the queue goes
         * idle after every enqueue. */
        q.enqueue("a");
        QCOMPARE(seen, (QList<QByteArray>{"a"}));
        QVERIFY(q.isIdle());

        q.enqueue("b");
        q.enqueue("c");
        QCOMPARE(seen, (QList<QByteArray>{"a", "b", "c"}));
        QVERIFY(q.isIdle());

        QTest::qWait(300);
        QCOMPARE(timeouts.count(), 0);   /* timer stopped by each confirm */
    }

    void writeHandler_drainsBacklog_whenConfirmingSynchronously()
    {
        BleWriteQueue q(100);
        QList<QByteArray> seen;
        bool syncConfirm = false;
        connect(&q, &BleWriteQueue::writeRequested, this,
                [&](const QByteArray& f) {
                    seen << f;
                    if (syncConfirm)
                        q.confirm();
                });

        q.enqueue("a");                  /* held in flight */
        q.enqueue("b");
        q.enqueue("c");
        syncConfirm = true;
        q.confirm();                     /* ack "a": b and c flow out nested */
        QCOMPARE(seen, (QList<QByteArray>{"a", "b", "c"}));
        QVERIFY(q.isIdle());
    }

    void writeHandler_canClear()
    {
        BleWriteQueue q(100);
        QSignalSpy writes(&q, &BleWriteQueue::writeRequested);
        QSignalSpy timeouts(&q, &BleWriteQueue::timedOut);
        connect(&q, &BleWriteQueue::writeRequested, this,
                [&q](const QByteArray&) { q.clear(); });

        q.enqueue("a");
        q.enqueue("b");
        QVERIFY(q.isIdle());
        QCOMPARE(writes.count(), 2);     /* "b" sent fresh after the clear */

        /* clear() inside the handler stopped the timer; nothing restarts it
         * after the emit. */
        QTest::qWait(300);
        QCOMPARE(timeouts.count(), 0);
    }
};

QTEST_MAIN(TestBleWriteQueue)
#include "test_ble_write_queue.moc"
