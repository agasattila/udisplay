/**
 * DeviceController unit tests.
 *
 * Tests the bootstrap-to-running pathway, specifically capability gating.
 * Calls onBootstrapSucceeded() directly via QMetaObject to bypass the TCP
 * transport layer (which is integration-tested in test_bootstrap.cpp).
 */
#include <QtTest>
#include <QCryptographicHash>
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QTcpServer>
#include <QTcpSocket>
#include <zlib.h>
#include "DeviceController.h"
#include "Protocol.h"
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

/* Helper: builds a YAML doc with `depth` nested `row`s below a `style:
 * alarm`-styled root (l0), the innermost being a `toggle` leaf. Shared by
 * the three depth-cap tests below (they only differ in `depth` and which
 * row they query), extracted after the pre-landing maintainability
 * specialist flagged the copy-paste. */
static QByteArray nestedRowYaml(int depth)
{
    QString yaml =
        "style:\n"
        "  default:\n"
        "    accent: \"#00d4aa\"\n"
        "  alarm:\n"
        "    accent: \"#e05555\"\n"
        "widgets:\n"
        "  l0:\n"
        "    type: row\n"
        "    style: alarm\n";
    QString indent = QStringLiteral("    ");
    for (int i = 1; i <= depth; ++i) {
        yaml += indent + QStringLiteral("widgets:\n");
        indent += QStringLiteral("  ");
        yaml += indent + QStringLiteral("l%1:\n").arg(i);
        indent += QStringLiteral("  ");
        yaml += indent + (i < depth
            ? QStringLiteral("type: row\n")
            : QStringLiteral("type: toggle\n"));
    }
    return yaml.toUtf8();
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

/* Merkle root exactly as BootstrapManager re-derives it for a cached blob:
 * SHA-256 over the concatenated SHA-256 of each zero-padded 256-byte chunk. */
static QByteArray merkleRootOf(const QByteArray& blob)
{
    QCryptographicHash rootHasher(QCryptographicHash::Sha256);
    for (int off = 0; off < blob.size(); off += 256) {
        QByteArray piece = blob.mid(off, 256);
        piece.append(256 - piece.size(), '\0');
        rootHasher.addData(QCryptographicHash::hash(piece, QCryptographicHash::Sha256));
    }
    return rootHasher.result();
}

/* Device->client HANDSHAKE (39-byte layout, proto >= 0x04, flags=0). */
static QByteArray handshakeMsg(uint8_t protoVersion, const QByteArray& root, uint16_t chunkCount)
{
    QByteArray msg(39, '\0');
    msg[0] = static_cast<char>(Proto::MSG_HANDSHAKE);
    msg[1] = static_cast<char>(protoVersion);
    msg[2] = 0x00;
    memcpy(msg.data() + 3, root.constData(), 32);
    msg[35] = static_cast<char>(chunkCount & 0xFF);
    msg[36] = static_cast<char>((chunkCount >> 8) & 0xFF);
    msg[37] = 0x00;  /* chunk_size = 256 LE */
    msg[38] = 0x01;
    return msg;
}

/* End-to-end over a real TCP socket: a local QTcpServer plays the device and
 * sends HANDSHAKE(protoVersion); the blob is pre-seeded in the client's blob
 * cache so bootstrap completes on the cache-hit path without a chunk
 * download. Returns the section row's widgetId (row 0), or -1. */
static int sectionIdAfterBootstrap(uint8_t protoVersion)
{
    static const char* yaml =
        "device:\n"
        "  name: scheme\n"
        "widgets:\n"
        "  panel:\n"
        "    type: section\n"
        "    widgets:\n"
        "      relay:\n"
        "        type: toggle\n";
    const QByteArray blob = zlibCompress(QByteArray(yaml));
    const QByteArray root = merkleRootOf(blob);

    QStandardPaths::setTestModeEnabled(true);
    DeviceController dc;
    {
        QSqlQuery q(QSqlDatabase::database(QStringLiteral("udisplay_cache")));
        q.prepare(QStringLiteral("INSERT OR REPLACE INTO blobs (root, compressed) VALUES (?, ?)"));
        q.addBindValue(root);
        q.addBindValue(blob);
        if (!q.exec()) return -1;
    }

    QTcpServer server;
    if (!server.listen(QHostAddress::LocalHost)) return -1;
    dc.connectTcp(QStringLiteral("127.0.0.1"), server.serverPort());
    if (!server.waitForNewConnection(5000)) return -1;
    QTcpSocket* device = server.nextPendingConnection();
    device->write(Proto::tcpFrame(handshakeMsg(
        protoVersion, root, static_cast<uint16_t>((blob.size() + 255) / 256))));
    device->flush();

    WidgetModel* m = dc.widgetModel();
    if (!QTest::qWaitFor([m]() { return m->rowCount() == 2; }, 5000)) return -1;
    return m->data(m->index(0), WidgetModel::WidgetIdRole).toInt();
}

/* ─────────────────────────────────────────────────────────────────────────── */

class TestDeviceController : public QObject
{
    Q_OBJECT

private slots:

    /* ── Widget-ID scheme follows the device's proto_version (issue #43) ── */

    /* A pre-v5 device's firmware header was generated with leaf-only IDs:
     * the section must get NO ID, and relay keeps 0x10. */
    void bootstrap_protoV4Device_usesLeafOnlyIds()
    {
        QCOMPARE(sectionIdAfterBootstrap(0x04), 0);
    }

    /* A v5 device: every widget has an ID — panel (0x10) < relay (0x11). */
    void bootstrap_protoV5Device_usesEveryWidgetIds()
    {
        QCOMPARE(sectionIdAfterBootstrap(0x05), 0x10);
    }

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

    /* ── TODO-034 regression tests: merged applyParsedYaml behavior ─── */

    /* The capability gate runs BEFORE any member state mutates (D1) — a
     * rejected reconnection attempt must not clobber an already-connected
     * device's model/name. This is existing pre-refactor behavior with no
     * prior test pinning it; the merge's restructuring is exactly what could
     * silently invert it. */
    void capability_rejection_preserves_existing_model()
    {
        const char* validYaml =
            "device:\n"
            "  name: devA\n"
            "style:\n"
            "  default:\n"
            "    accent: \"#00d4aa\"\n"
            "  warning:\n"
            "    accent: \"#f5a623\"\n"
            "widgets:\n"
            "  a:\n"
            "    type: display\n"
            "    label: FromDeviceA\n";

        DeviceController dc;
        injectBootstrap(dc, validYaml);
        QCOMPARE(dc.state(), QStringLiteral("running"));
        QCOMPARE(dc.deviceName(), QStringLiteral("devA"));
        QCOMPARE(dc.widgetModel()->rowCount(), 1);

        /* A non-default style selection must ALSO survive a rejected
         * reconnect — the capability gate runs before m_activeStyleName
         * mutates, same as m_deviceName/m_model (D1). */
        dc.setActiveStyle(QStringLiteral("warning"));
        QCOMPARE(dc.activeStyle().value(QStringLiteral("accent")).toString(),
                  QStringLiteral("#f5a623"));

        const char* rejectedYaml =
            "device:\n"
            "  name: devB\n"
            "  capabilities:\n"
            "    - some_unknown_cap\n"
            "widgets:\n"
            "  b:\n"
            "    type: display\n"
            "    label: FromDeviceB\n";
        injectBootstrap(dc, rejectedYaml);

        QCOMPARE(dc.state(), QStringLiteral("error"));
        QCOMPARE(dc.deviceName(), QStringLiteral("devA"));
        QCOMPARE(dc.widgetModel()->rowCount(), 1);
        const QModelIndex idx = dc.widgetModel()->index(0);
        QCOMPARE(dc.widgetModel()->data(idx, WidgetModel::LabelRole).toString(),
                  QStringLiteral("FromDeviceA"));
        QCOMPARE(dc.activeStyle().value(QStringLiteral("accent")).toString(),
                  QStringLiteral("#f5a623"));
    }

    /* A fresh bootstrap connection always resets the active style to
     * "default" (D3) — even if a previous connection had a named style
     * selected. Design mode's reload does the opposite (preserve-if-valid);
     * no prior test pinned the bootstrap side of this divergence. */
    void bootstrap_reconnect_resets_style_to_default()
    {
        const char* yaml =
            "device:\n"
            "  name: testdev\n"
            "style:\n"
            "  default:\n"
            "    accent: \"#00d4aa\"\n"
            "  warning:\n"
            "    accent: \"#f5a623\"\n"
            "widgets:\n"
            "  a:\n"
            "    type: toggle\n";

        DeviceController dc;
        injectBootstrap(dc, yaml);
        QCOMPARE(dc.state(), QStringLiteral("running"));

        dc.setActiveStyle(QStringLiteral("warning"));
        QCOMPARE(dc.activeStyle().value(QStringLiteral("accent")).toString(),
                  QStringLiteral("#f5a623"));

        /* Reconnect (fresh bootstrap) — must reset to "default", not carry
         * over the previously-selected "warning" style. */
        injectBootstrap(dc, yaml);
        QCOMPARE(dc.state(), QStringLiteral("running"));
        QCOMPARE(dc.activeStyle().value(QStringLiteral("accent")).toString(),
                  QStringLiteral("#00d4aa"));
    }

    /* Bootstrap-mode diagnostics log via qWarning() in addition to the
     * parseWarningsChanged signal (D4) — only the signal was tested before,
     * not the qWarning() text itself. */
    void bootstrap_warningYaml_logsQWarning()
    {
        const char* yaml =
            "widgets:\n"
            "  led:\n"
            "    type: led\n"
            "    color: not_a_hex_color\n";

        DeviceController dc;
        const QString captured = captureDebugOutput([&]() { injectBootstrap(dc, yaml); });

        QCOMPARE(dc.state(), QStringLiteral("running"));
        QVERIFY(captured.contains(QStringLiteral("[YamlParser WARNING]")));
        QVERIFY(captured.contains(QStringLiteral("led.color")));
    }

    /* D6: diagnostics now emit right after parse, before the capability gate
     * runs — so a rejected connection still surfaces the parse warnings it
     * found, a deliberate behavior improvement over the pre-refactor code
     * (which short-circuited before the diagnostics step ever ran). */
    void capability_rejection_stillEmitsDiagnostics()
    {
        const char* yaml =
            "device:\n"
            "  name: testdev\n"
            "  capabilities:\n"
            "    - some_unknown_cap\n"
            "widgets:\n"
            "  led:\n"
            "    type: led\n"
            "    color: not_a_hex_color\n";

        DeviceController dc;
        bool signalReceived = false;
        connect(&dc, &DeviceController::parseWarningsChanged,
                [&](const QList<YamlParser::ParseDiagnostic>&) { signalReceived = true; });

        const QString captured = captureDebugOutput([&]() { injectBootstrap(dc, yaml); });

        QCOMPARE(dc.state(), QStringLiteral("error"));
        QVERIFY(dc.errorString().contains(QStringLiteral("unknown capability")));
        QVERIFY(captured.contains(QStringLiteral("[YamlParser WARNING]")));
        QVERIFY(captured.contains(QStringLiteral("led.color")));
        QVERIFY2(signalReceived,
                  "parseWarningsChanged must fire even though the connection "
                  "was ultimately rejected (TODO-034 D6)");
    }

    /* ── parseWarnings property (TODO-040) ───────────────────────────── */

    void parseWarnings_readableAfterParse_withoutListeningLive()
    {
        /* No Connections/lambda attached before the parse — this is exactly
         * the "constructed after the fact" scenario the property exists
         * for: reading controller.parseWarnings must show the current
         * state even though nothing was listening when the signal fired. */
        const char* yaml =
            "widgets:\n"
            "  led:\n"
            "    type: led\n"
            "    color: not_a_hex_color\n";

        DeviceController dc;
        injectBootstrap(dc, yaml);

        QCOMPARE(dc.state(), QStringLiteral("running"));
        const QVariantList warnings = dc.parseWarnings();
        QCOMPARE(warnings.size(), 1);
        const QVariantMap w = warnings.first().toMap();
        QCOMPARE(w.value(QStringLiteral("severity")).toString(), QStringLiteral("warning"));
        QCOMPARE(w.value(QStringLiteral("widgetKey")).toString(), QStringLiteral("led"));
        QCOMPARE(w.value(QStringLiteral("field")).toString(), QStringLiteral("color"));
        QVERIFY(w.value(QStringLiteral("message")).toString().contains(QStringLiteral("hex")));
    }

    void parseWarnings_clearOnCleanReparse_signalFiresOnTransition()
    {
        const char* warningYaml =
            "widgets:\n"
            "  led:\n"
            "    type: led\n"
            "    color: not_a_hex_color\n";
        const char* cleanYaml =
            "widgets:\n"
            "  led:\n"
            "    type: led\n";

        DeviceController dc;
        injectBootstrap(dc, warningYaml);
        QCOMPARE(dc.parseWarnings().size(), 1);

        bool signalReceived = false;
        connect(&dc, &DeviceController::parseWarningsChanged,
                [&](const QList<YamlParser::ParseDiagnostic>&) { signalReceived = true; });

        injectBootstrap(dc, cleanYaml);

        QCOMPARE(dc.state(), QStringLiteral("running"));
        QVERIFY(dc.parseWarnings().isEmpty());
        QVERIFY2(signalReceived,
                  "clearing prior warnings on a clean reparse must still notify, "
                  "or a UI bound to parseWarnings goes stale after the fix "
                  "(the qml-invokable-no-notify-binding-staleness pitfall class)");
    }

    /* ── effectiveStyleFor() — docs/designs/unify-widget-style-handling.md ── */

    void effectiveStyleFor_noStyleProp_fallsBackToActiveStyle()
    {
        const char* yaml =
            "style:\n"
            "  default:\n"
            "    accent: \"#00d4aa\"\n"
            "widgets:\n"
            "  a:\n"
            "    type: toggle\n";
        DeviceController dc;
        injectBootstrap(dc, yaml);
        QCOMPARE(dc.effectiveStyleFor(0).value(QStringLiteral("accent")).toString(),
                  dc.activeStyle().value(QStringLiteral("accent")).toString());
    }

    void effectiveStyleFor_validStyleName_returnsThatStylesheet()
    {
        const char* yaml =
            "style:\n"
            "  default:\n"
            "    accent: \"#00d4aa\"\n"
            "  alarm:\n"
            "    accent: \"#e05555\"\n"
            "widgets:\n"
            "  d:\n"
            "    type: section\n"
            "    style: alarm\n";
        DeviceController dc;
        injectBootstrap(dc, yaml);
        QCOMPARE(dc.effectiveStyleFor(0).value(QStringLiteral("accent")).toString(),
                  QStringLiteral("#e05555"));
    }

    void effectiveStyleFor_outOfRangeRow_fallsBackNoCrash()
    {
        const char* yaml =
            "widgets:\n"
            "  a:\n"
            "    type: toggle\n";
        DeviceController dc;
        injectBootstrap(dc, yaml);
        QCOMPARE(dc.effectiveStyleFor(999), dc.activeStyle());
        QCOMPARE(dc.effectiveStyleFor(-1),  dc.activeStyle());
    }

    /* CRITICAL — same staleness bug class PR9's adversarial review already
     * caught once for childModel(): a full reparse reassigns row indices,
     * so the resolver must reflect the NEW row's data at a given index, not
     * data left over from the row that used to occupy it. */
    void effectiveStyleFor_afterReparse_reflectsNewRowData()
    {
        DeviceController dc;
        injectBootstrap(dc,
            "style:\n"
            "  default:\n"
            "    accent: \"#00d4aa\"\n"
            "  alarm:\n"
            "    accent: \"#e05555\"\n"
            "widgets:\n"
            "  a:\n"
            "    type: toggle\n");
        QCOMPARE(dc.effectiveStyleFor(0).value(QStringLiteral("accent")).toString(),
                  QStringLiteral("#00d4aa"));

        /* Reparse: row 0 is now a different widget, explicitly styled. */
        injectBootstrap(dc,
            "style:\n"
            "  default:\n"
            "    accent: \"#00d4aa\"\n"
            "  alarm:\n"
            "    accent: \"#e05555\"\n"
            "widgets:\n"
            "  b:\n"
            "    type: section\n"
            "    style: alarm\n");
        QCOMPARE(dc.effectiveStyleFor(0).value(QStringLiteral("accent")).toString(),
                  QStringLiteral("#e05555"));
    }

    /* Resolves the former "runtime stylesheet redefinition" open question:
     * a reparse with unchanged widgets but a redefined stylesheet updates
     * both a pinned and an unpinned widget's resolved colors correctly. */
    void effectiveStyleFor_redefinedStylesheet_updatesPinnedAndUnpinned()
    {
        const char* yamlBefore =
            "style:\n"
            "  default:\n"
            "    accent: \"#00d4aa\"\n"
            "  alarm:\n"
            "    accent: \"#e05555\"\n"
            "widgets:\n"
            "  unpinned:\n"
            "    type: toggle\n"
            "  pinned:\n"
            "    type: section\n"
            "    style: alarm\n";
        DeviceController dc;
        injectBootstrap(dc, yamlBefore);
        QCOMPARE(dc.effectiveStyleFor(0).value(QStringLiteral("accent")).toString(),
                  QStringLiteral("#00d4aa"));
        QCOMPARE(dc.effectiveStyleFor(1).value(QStringLiteral("accent")).toString(),
                  QStringLiteral("#e05555"));

        /* Same widgets, redefined "alarm" (and "default") accent colors. */
        const char* yamlAfter =
            "style:\n"
            "  default:\n"
            "    accent: \"#111111\"\n"
            "  alarm:\n"
            "    accent: \"#222222\"\n"
            "widgets:\n"
            "  unpinned:\n"
            "    type: toggle\n"
            "  pinned:\n"
            "    type: section\n"
            "    style: alarm\n";
        injectBootstrap(dc, yamlAfter);
        QCOMPARE(dc.effectiveStyleFor(0).value(QStringLiteral("accent")).toString(),
                  QStringLiteral("#111111"));
        QCOMPARE(dc.effectiveStyleFor(1).value(QStringLiteral("accent")).toString(),
                  QStringLiteral("#222222"));
    }

    /* ── Container-level style cascading — docs/designs/container-style-cascading.md ── */

    /* Rows are appended pre-order (buildWidget appends the container before
     * recursing into its children — see YamlParser.cpp), so row 0 = outer,
     * row 1 = middle, row 2 = leaf below. */
    void effectiveStyleFor_cascadesThroughUnstyledIntermediateAncestor()
    {
        DeviceController dc;
        injectBootstrap(dc,
            "style:\n"
            "  default:\n"
            "    accent: \"#00d4aa\"\n"
            "  alarm:\n"
            "    accent: \"#e05555\"\n"
            "widgets:\n"
            "  outer:\n"
            "    type: row\n"
            "    style: alarm\n"
            "    widgets:\n"
            "      middle:\n"
            "        type: row\n"
            "        widgets:\n"
            "          leaf:\n"
            "            type: toggle\n");
        QCOMPARE(dc.effectiveStyleFor(2).value(QStringLiteral("accent")).toString(),
                  QStringLiteral("#e05555"));
    }

    void effectiveStyleFor_explicitStyleWinsOverStyledAncestor()
    {
        DeviceController dc;
        injectBootstrap(dc,
            "style:\n"
            "  default:\n"
            "    accent: \"#00d4aa\"\n"
            "  alarm:\n"
            "    accent: \"#e05555\"\n"
            "  night:\n"
            "    accent: \"#111111\"\n"
            "widgets:\n"
            "  outer:\n"
            "    type: row\n"
            "    style: alarm\n"
            "    widgets:\n"
            "      leaf:\n"
            "        type: toggle\n"
            "        style: night\n");
        QCOMPARE(dc.effectiveStyleFor(1).value(QStringLiteral("accent")).toString(),
                  QStringLiteral("#111111"));
    }

    void effectiveStyleFor_nearestStyledAncestorWins()
    {
        DeviceController dc;
        injectBootstrap(dc,
            "style:\n"
            "  default:\n"
            "    accent: \"#00d4aa\"\n"
            "  alarm:\n"
            "    accent: \"#e05555\"\n"
            "  warning:\n"
            "    accent: \"#f5a623\"\n"
            "widgets:\n"
            "  outer:\n"
            "    type: row\n"
            "    style: alarm\n"
            "    widgets:\n"
            "      middle:\n"
            "        type: row\n"
            "        style: warning\n"
            "        widgets:\n"
            "          leaf:\n"
            "            type: toggle\n");
        /* rows: outer=0, middle=1, leaf=2 */
        QCOMPARE(dc.effectiveStyleFor(2).value(QStringLiteral("accent")).toString(),
                  QStringLiteral("#f5a623"));
    }

    void effectiveStyleFor_outsideAnyStyledContainer_followsActiveStyle()
    {
        DeviceController dc;
        injectBootstrap(dc,
            "style:\n"
            "  default:\n"
            "    accent: \"#00d4aa\"\n"
            "  alarm:\n"
            "    accent: \"#e05555\"\n"
            "widgets:\n"
            "  outer:\n"
            "    type: row\n"
            "    style: alarm\n"
            "    widgets:\n"
            "      leaf:\n"
            "        type: toggle\n"
            "  sibling:\n"
            "    type: toggle\n");
        /* rows: outer=0, leaf=1, sibling=2 (sibling has no styled ancestor) */
        dc.setActiveStyle(QStringLiteral("alarm"));
        QCOMPARE(dc.effectiveStyleFor(2).value(QStringLiteral("accent")).toString(),
                  QStringLiteral("#e05555"));
        dc.setActiveStyle(QStringLiteral("default"));
        QCOMPARE(dc.effectiveStyleFor(2).value(QStringLiteral("accent")).toString(),
                  QStringLiteral("#00d4aa"));
    }

    /* Depth cap: a leaf nested past kMaxStyleAncestorDepth (10) levels below
     * a styled root falls back to activeStyle instead of walking further —
     * defensive bound on this runtime walk, consistent with TODO-036's
     * proposed nesting limit for the separate parse-time recursion guard. */
    void effectiveStyleFor_deeplyNestedBeyondCap_fallsBackToActiveStyle()
    {
        /* 12 nested rows below the styled root — deeper than the 10-level cap. */
        DeviceController dc;
        injectBootstrap(dc, nestedRowYaml(12).constData());
        /* Rows are pre-order: l0=0, l1=1, ..., l12=12 (the innermost toggle). */
        QCOMPARE(dc.effectiveStyleFor(12).value(QStringLiteral("accent")).toString(),
                  dc.activeStyle().value(QStringLiteral("accent")).toString());
    }

    /* ── Button cascading (PR22 change request) — docs/designs/
     * container-style-cascading.md, Revision ── */

    /* `button` was the last type parseStyleProp() rejected; lifting it means
     * a button's own style: makes it a cascade root for its face children,
     * exactly like row/grid/dpad — no resolver change needed, this proves
     * the WIRING (button face children get parentId = the button's own row,
     * per YamlParser.cpp's Button case in buildWidget()) produces the
     * correct ancestor chain. */
    void effectiveStyleFor_buttonFaceChild_inheritsButtonStyle()
    {
        DeviceController dc;
        injectBootstrap(dc,
            "style:\n"
            "  default:\n"
            "    accent: \"#00d4aa\"\n"
            "  warning:\n"
            "    accent: \"#f5a623\"\n"
            "widgets:\n"
            "  my_button:\n"
            "    type: button\n"
            "    style: warning\n"
            "    widgets:\n"
            "      hello:\n"
            "        type: label\n"
            "        text: \"hello\"\n");
        /* rows: my_button=0, hello=1 */
        QCOMPARE(dc.effectiveStyleFor(1).value(QStringLiteral("accent")).toString(),
                  QStringLiteral("#f5a623"));
    }

    /* Button-group items are hand-built (not via buildWidget()), but each
     * item's parentId = the group's own row, so the generic ancestor walk
     * already covers them once parseStyleProp() is called from that
     * construction site too. Also serves as the sibling-regression case: a
     * sibling item with NO style: of its own must still resolve to the
     * GROUP's cascaded style, not fall back to activeStyle. */
    void effectiveStyleFor_buttonGroupItem_explicitOverridesGroup_siblingInherits()
    {
        DeviceController dc;
        injectBootstrap(dc,
            "style:\n"
            "  default:\n"
            "    accent: \"#00d4aa\"\n"
            "  alarm:\n"
            "    accent: \"#e05555\"\n"
            "  warning:\n"
            "    accent: \"#f5a623\"\n"
            "widgets:\n"
            "  my_group:\n"
            "    type: button-group\n"
            "    style: alarm\n"
            "    items:\n"
            "      a:\n"
            "        label: \"A\"\n"
            "        style: warning\n"
            "      b:\n"
            "        label: \"B\"\n");
        /* rows: my_group=0, a=1, b=2 */
        QCOMPARE(dc.effectiveStyleFor(1).value(QStringLiteral("accent")).toString(),
                  QStringLiteral("#f5a623"));
        QCOMPARE(dc.effectiveStyleFor(2).value(QStringLiteral("accent")).toString(),
                  QStringLiteral("#e05555"));
    }

    /* ── Coverage audit follow-ups (ship-stage subagent, 2026-09-23) ── */

    /* Every other cascading test uses `row` as the styled ancestor type.
     * The resolver is type-agnostic (verified by reading it — no type
     * check anywhere in effectiveStyleFor()), but this is the literal
     * scenario docs/widgets.md calls out by name as the documented
     * behavior change ("an unstyled child of a section/button-group...
     * now inherits the container's style instead") — deserves its own
     * assertion, not just an inference from the row-based tests. */
    void effectiveStyleFor_sectionAsCascadeRoot_unstyledChildInherits()
    {
        DeviceController dc;
        injectBootstrap(dc,
            "style:\n"
            "  default:\n"
            "    accent: \"#00d4aa\"\n"
            "  alarm:\n"
            "    accent: \"#e05555\"\n"
            "widgets:\n"
            "  panel:\n"
            "    type: section\n"
            "    style: alarm\n"
            "    widgets:\n"
            "      unstyled_leaf:\n"
            "        type: toggle\n");
        /* rows: panel=0, unstyled_leaf=1 */
        QCOMPARE(dc.effectiveStyleFor(1).value(QStringLiteral("accent")).toString(),
                  QStringLiteral("#e05555"));
    }

    /* Exact depth-cap boundary (kMaxStyleAncestorDepth = 10 in
     * DeviceController.cpp): the existing
     * effectiveStyleFor_deeplyNestedBeyondCap_fallsBackToActiveStyle test
     * only proves "well beyond the cap (12 levels) falls back" — it does
     * not pin down where the boundary actually is. The loop condition is
     * `depth < kMaxStyleAncestorDepth`, checking the leaf's own row at
     * depth 0 and each ancestor up to depth 9 (10 checks total) — so a
     * styled ancestor exactly 9 hops above the leaf is the LAST position
     * still found; 10 hops above is the FIRST position that falls back. */
    void effectiveStyleFor_depthCapBoundary_exactlyAtCap_succeeds()
    {
        /* 9 nested rows below the styled root — leaf is exactly 9 hops
         * above the styled ancestor (depth 9, still < 10). */
        DeviceController dc;
        injectBootstrap(dc, nestedRowYaml(9).constData());
        /* Rows are pre-order: l0=0, l1=1, ..., l9=9 (the innermost toggle). */
        QCOMPARE(dc.effectiveStyleFor(9).value(QStringLiteral("accent")).toString(),
                  QStringLiteral("#e05555"));
    }

    void effectiveStyleFor_depthCapBoundary_oneOverCap_fallsBack()
    {
        /* 10 nested rows below the styled root — one hop deeper than the
         * "exactly at cap" case above; the leaf is now 10 hops above the
         * styled ancestor (depth 10, fails depth < 10). Minimal failing
         * case, as opposed to the existing 12-level "well beyond cap" test. */
        DeviceController dc;
        injectBootstrap(dc, nestedRowYaml(10).constData());
        /* Rows are pre-order: l0=0, l1=1, ..., l10=10 (the innermost toggle). */
        QCOMPARE(dc.effectiveStyleFor(10).value(QStringLiteral("accent")).toString(),
                  dc.activeStyle().value(QStringLiteral("accent")).toString());
    }
};

QTEST_MAIN(TestDeviceController)
#include "test_device_controller.moc"
