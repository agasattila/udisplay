/**
 * DeviceController unit tests.
 *
 * Tests the bootstrap-to-running pathway, specifically capability gating.
 * Calls onBootstrapSucceeded() directly via QMetaObject to bypass the TCP
 * transport layer (which is integration-tested in test_bootstrap.cpp).
 */
#include <QtTest>
#include <zlib.h>
#include "DeviceController.h"
#include "DeviceInfo.h"
#ifdef HAVE_BLE
#include <QBluetoothDeviceInfo>
#endif

/* Compress raw bytes into standard zlib deflate format (RFC 1950).
 * DeviceController::decompressBlob() expects this format (inflateInit). */
static QByteArray zlibCompress(const QByteArray& raw)
{
    uLong bound = compressBound(static_cast<uLong>(raw.size()));
    QByteArray out(static_cast<int>(bound), '\0');
    uLong outLen = bound;
    int r = compress(reinterpret_cast<Bytef*>(out.data()), &outLen,
                     reinterpret_cast<const Bytef*>(raw.constData()),
                     static_cast<uLong>(raw.size()));
    if (r != Z_OK) return {};
    out.resize(static_cast<int>(outLen));
    return out;
}

/* Drive onBootstrapSucceeded directly without a real transport.
 * Qt's meta-object system can invoke private slots by name — access specifiers
 * are a C++ concept, not a Qt concept. */
static void injectBootstrap(DeviceController& dc, const char* yaml)
{
    QByteArray blob = zlibCompress(QByteArray(yaml));
    QMetaObject::invokeMethod(&dc, "onBootstrapSucceeded",
                              Qt::DirectConnection,
                              Q_ARG(QByteArray, QByteArray(32, '\0')),
                              Q_ARG(QByteArray, blob));
}

/* Same as injectBootstrap, but passes the blob through uncompressed/raw —
 * used to trigger the decompression-failure branch. */
static void injectBootstrapRawBlob(DeviceController& dc, const QByteArray& rawBlob)
{
    QMetaObject::invokeMethod(&dc, "onBootstrapSucceeded",
                              Qt::DirectConnection,
                              Q_ARG(QByteArray, QByteArray(32, '\0')),
                              Q_ARG(QByteArray, rawBlob));
}

/* Helper: capture qInfo()/qWarning() output produced while running fn().
 * --debug output goes through qInfo(), which the default Qt message handler
 * sends to stderr — installing a temporary handler is the simplest way to
 * assert on it without spawning a subprocess. */
static QStringList* g_capturedMessages = nullptr;

static void captureMessageHandler(QtMsgType type, const QMessageLogContext& context,
                                   const QString& msg)
{
    Q_UNUSED(type);
    Q_UNUSED(context);
    if (g_capturedMessages)
        g_capturedMessages->append(msg);
}

template <typename Fn>
static QString captureDebugOutput(Fn&& fn)
{
    QStringList captured;
    g_capturedMessages = &captured;
    QtMessageHandler prev = qInstallMessageHandler(captureMessageHandler);
    fn();
    qInstallMessageHandler(prev);
    g_capturedMessages = nullptr;
    return captured.join(QLatin1Char('\n'));
}

/* ─────────────────────────────────────────────────────────────────────────── */

class TestDeviceController : public QObject
{
    Q_OBJECT

private slots:

    /* ── Capability gating ────────────────────────────────────────── */

    /* Device YAML with an unknown capability → connection rejected.
     * kKnownCapabilities is currently empty, so ANY capability string fails. */
    void capability_gating_rejects_unknown_cap()
    {
        const char* yaml =
            "device:\n"
            "  name: testdev\n"
            "  capabilities:\n"
            "    - some_unknown_cap\n"
            "widgets:\n"
            "  a:\n"
            "    type: toggle\n";

        DeviceController dc;
        QSignalSpy errorSpy(&dc, &DeviceController::errorStringChanged);

        injectBootstrap(dc, yaml);

        QCOMPARE(errorSpy.count(), 1);
        QCOMPARE(dc.state(), QStringLiteral("error"));
        QVERIFY(dc.errorString().contains(QStringLiteral("unknown capability")));
        QVERIFY(dc.errorString().contains(QStringLiteral("some_unknown_cap")));
    }

    /* onHeartbeat() with no transport connected must not crash.
     * The null guard prevents a SEGV when m_transport == nullptr. */
    void heartbeat_echo_noCrash_withoutTransport()
    {
        DeviceController dc;
        QMetaObject::invokeMethod(&dc, "onHeartbeat", Qt::DirectConnection);
        QVERIFY(true); /* reached — no crash */
    }

    /* YAML with a validation warning → parseWarningsChanged is emitted.
     * Uses an invalid LED color (Warning, not Error) so bootstrap succeeds. */
    void bootstrap_warningYaml_emitsParseWarningsChanged()
    {
        const char* yaml =
            "widgets:\n"
            "  led:\n"
            "    type: led\n"
            "    color: not_a_hex_color\n";

        DeviceController dc;
        bool signalReceived = false;
        QList<YamlParser::ParseDiagnostic> receivedDiags;
        connect(&dc, &DeviceController::parseWarningsChanged,
                [&](const QList<YamlParser::ParseDiagnostic>& d) {
                    signalReceived = true;
                    receivedDiags = d;
                });

        injectBootstrap(dc, yaml);

        QCOMPARE(dc.state(), QStringLiteral("running"));
        QVERIFY(signalReceived);
        QVERIFY(!receivedDiags.isEmpty());
        QCOMPARE(receivedDiags[0].severity, YamlParser::Severity::Warning);
        QCOMPARE(receivedDiags[0].field,    QStringLiteral("color"));
    }

    /* YAML with no warnings → parseWarningsChanged is NOT emitted. */
    void bootstrap_cleanYaml_noParseWarningsChanged()
    {
        const char* yaml =
            "widgets:\n"
            "  a:\n"
            "    type: toggle\n";

        DeviceController dc;
        bool signalReceived = false;
        connect(&dc, &DeviceController::parseWarningsChanged,
                [&](const QList<YamlParser::ParseDiagnostic>&) {
                    signalReceived = true;
                });

        injectBootstrap(dc, yaml);

        QCOMPARE(dc.state(), QStringLiteral("running"));
        QVERIFY(!signalReceived);
    }

    /* Device YAML without capabilities → no gating, bootstrap completes. */
    void capability_gating_accepts_no_capabilities()
    {
        const char* yaml =
            "device:\n"
            "  name: testdev\n"
            "widgets:\n"
            "  a:\n"
            "    type: toggle\n";

        DeviceController dc;
        QSignalSpy stateSpy(&dc, &DeviceController::stateChanged);

        injectBootstrap(dc, yaml);

        QCOMPARE(dc.state(), QStringLiteral("running"));
        QVERIFY(dc.errorString().isEmpty());
    }

    /* Device YAML with an empty capabilities list → no gating (loop never runs). */
    void capability_gating_empty_list_not_rejected()
    {
        /* Note: the schema rejects empty capabilities arrays (minItems: 1),
         * so this case should never come from a valid YAML. But the code must
         * be resilient: an empty QStringList → loop never executes → no rejection. */
        const char* yaml =
            "device:\n"
            "  name: testdev\n"
            "  capabilities: []\n"
            "widgets:\n"
            "  a:\n"
            "    type: toggle\n";

        DeviceController dc;
        injectBootstrap(dc, yaml);

        /* Schema would reject this YAML before it reaches the device, but
         * the parser accepts it and produces an empty list — no gating fires. */
        QCOMPARE(dc.state(), QStringLiteral("running"));
    }

    /* ── connectDiscovered() — BLE transport ─────────────────────── */

    /* Without HAVE_BLE: connectDiscovered() with a BLE DeviceInfo must set
     * state=error rather than crashing or silently no-oping. */
    void connectDiscovered_BleType_WithoutBleSupport()
    {
#ifndef HAVE_BLE
        DeviceController dc;
        DeviceInfo di = DeviceInfo::makeBle(
            QStringLiteral("AA:BB:CC:DD:EE:FF"),
            QStringLiteral("BLE Device"),
            QStringLiteral("AA:BB:CC:DD:EE:FF"));

        dc.connectDiscovered(QVariant::fromValue(di));

        QCOMPARE(dc.state(), QStringLiteral("error"));
        QVERIFY(!dc.errorString().isEmpty());
#else
        QSKIP("Test only valid when HAVE_BLE is not defined");
#endif
    }

#ifdef HAVE_BLE
    /* With HAVE_BLE: connectDiscovered() with a BLE DeviceInfo passes through
     * "connecting" on the way to attempting the GATT connection.
     * On hardware-less CI the BlueZ backend may reject the invalid adapter
     * synchronously, so we capture the "connecting" state via a signal
     * connection rather than checking the final state. */
    void connectDiscovered_BleType_EntersConnecting()
    {
        DeviceController dc;
        bool sawConnecting = false;
        QObject::connect(&dc, &DeviceController::stateChanged, [&]() {
            if (dc.state() == QStringLiteral("connecting"))
                sawConnecting = true;
        });

        DeviceInfo di = DeviceInfo::makeBle(
            QStringLiteral("AA:BB:CC:DD:EE:FF"),
            QStringLiteral("BLE Device"),
            QStringLiteral("AA:BB:CC:DD:EE:FF"),
            -60,
            QVariant::fromValue(QBluetoothDeviceInfo()));

        dc.connectDiscovered(QVariant::fromValue(di));

        QVERIFY2(sawConnecting,
                 "connectDiscovered() must transition through 'connecting' state");
    }
#endif

    /* ── --debug: real (bootstrap) mode ─────────────────────────────── */

    void debugMode_off_producesNoDumpOutput()
    {
        const char* yaml =
            "device:\n"
            "  name: testdev\n"
            "widgets:\n"
            "  a:\n"
            "    type: toggle\n";

        DeviceController dc;
        /* setDebugMode() never called — --debug not passed */
        const QString captured = captureDebugOutput([&]() { injectBootstrap(dc, yaml); });

        QVERIFY(!captured.contains(QStringLiteral("widgets: (")));
    }

    void debugMode_on_dumpsFullyResolvedTreeOnBootstrapSuccess()
    {
        const char* yaml =
            "device:\n"
            "  name: testdev\n"
            "widgets:\n"
            "  temp:\n"
            "    type: display\n"
            "    label: Temperature\n";

        DeviceController dc;
        dc.setDebugMode(true);
        const QString captured = captureDebugOutput([&]() { injectBootstrap(dc, yaml); });

        QCOMPARE(dc.state(), QStringLiteral("running"));
        QVERIFY(captured.contains(QStringLiteral("name=\"testdev\"")));
        QVERIFY(captured.contains(QStringLiteral("[0x10] display \"Temperature\"")));
    }

    void debugMode_on_printsReasonOnDecompressionFailure()
    {
        DeviceController dc;
        dc.setDebugMode(true);
        const QString captured = captureDebugOutput([&]() {
            injectBootstrapRawBlob(dc, QByteArray("not valid zlib data at all"));
        });

        QCOMPARE(dc.state(), QStringLiteral("error"));
        QVERIFY(captured.contains(QStringLiteral("[debug] parse failed:")));
        QVERIFY(captured.contains(QStringLiteral("decompress")));
        QVERIFY(!captured.contains(QStringLiteral("widgets: (")));
    }

    void debugMode_on_printsReasonOnYamlParseFailure()
    {
        const char* badYaml = "not valid yaml [\n";

        DeviceController dc;
        dc.setDebugMode(true);
        const QString captured = captureDebugOutput([&]() { injectBootstrap(dc, badYaml); });

        QCOMPARE(dc.state(), QStringLiteral("error"));
        QVERIFY(captured.contains(QStringLiteral("[debug] parse failed:")));
        QVERIFY(!captured.contains(QStringLiteral("widgets: (")));
    }

    /* Capability rejection is the one failure branch where the model IS fully
     * resolved — dump the tree AND the rejection reason (Issue 5). */
    void debugMode_on_dumpsFullTreeAndReasonOnCapabilityRejection()
    {
        const char* yaml =
            "device:\n"
            "  name: testdev\n"
            "  capabilities:\n"
            "    - some_unknown_cap\n"
            "widgets:\n"
            "  temp:\n"
            "    type: display\n"
            "    label: Temperature\n";

        DeviceController dc;
        dc.setDebugMode(true);
        const QString captured = captureDebugOutput([&]() { injectBootstrap(dc, yaml); });

        QCOMPARE(dc.state(), QStringLiteral("error"));
        QVERIFY(captured.contains(QStringLiteral("[0x10] display \"Temperature\"")));
        QVERIFY(captured.contains(QStringLiteral("[debug] parse failed:")));
        QVERIFY(captured.contains(QStringLiteral("unknown capability")));
    }
};

QTEST_MAIN(TestDeviceController)
#include "test_device_controller.moc"
