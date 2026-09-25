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

/* Deliver a device STATE_UPDATE carrying a uint8 value, the same way
 * BootstrapManager::stateUpdateData reaches the controller at runtime. */
static void injectStateUpdateU8(DeviceController& dc, uint8_t widgetId, uint8_t value)
{
    Proto::StateUpdateData u{};
    u.widgetId  = widgetId;
    u.valueType = Proto::VAL_UINT8;
    u.u8Value   = value;
    QMetaObject::invokeMethod(&dc, "onStateUpdateData",
                              Qt::DirectConnection,
                              Q_ARG(Proto::StateUpdateData, u));
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
    /* ── button-group exclusive select (issue #41, TODO-006) ── */

    /* Round-trip contract for the generated set_<group>()/clear_<group>():
     * firmware sends STATE_UPDATE(group_id, VAL_UINT8, item_widget_id) and
     * the group row's `value` becomes that item's widget ID — exactly what
     * ButtonGroupWidget.qml compares against each item's model.widgetId to
     * draw the selection ring. 0 (reserved ID, never a real item) clears. */
    void buttonGroup_stateUpdate_selectsItemByWidgetId_zeroClears()
    {
        DeviceController dc;
        injectBootstrap(dc,
            "widgets:\n"
            "  mode:\n"
            "    type: button-group\n"
            "    items:\n"
            "      fast:\n"
            "        label: \"Fast\"\n"
            "      slow:\n"
            "        label: \"Slow\"\n");
        WidgetModel* m = dc.widgetModel();
        auto at = [m](int row, int role) { return m->data(m->index(row, 0), role); };
        /* rows: mode=0, fast=1, slow=2; IDs are alphabetical by path:
         * mode=0x10, mode.fast=0x11, mode.slow=0x12 (same as codegen). */
        const int groupId = at(0, WidgetModel::WidgetIdRole).toInt();
        const int fastId  = at(1, WidgetModel::WidgetIdRole).toInt();
        const int slowId  = at(2, WidgetModel::WidgetIdRole).toInt();
        QCOMPARE(groupId, 0x10);
        QCOMPARE(fastId,  0x11);
        QCOMPARE(slowId,  0x12);

        /* Nothing selected until the device says so. */
        QVERIFY(at(0, WidgetModel::ValueRole).isNull());

        injectStateUpdateU8(dc, groupId, slowId);
        QCOMPARE(at(0, WidgetModel::ValueRole).toInt(), slowId);
        /* The update targets the group row only — items carry no value. */
        QVERIFY(at(1, WidgetModel::ValueRole).isNull());
        QVERIFY(at(2, WidgetModel::ValueRole).isNull());

        injectStateUpdateU8(dc, groupId, fastId);
        QCOMPARE(at(0, WidgetModel::ValueRole).toInt(), fastId);

        injectStateUpdateU8(dc, groupId, 0);
        QCOMPARE(at(0, WidgetModel::ValueRole).toInt(), 0);
        QVERIFY(at(0, WidgetModel::ValueRole).toInt() != fastId);
        QVERIFY(at(0, WidgetModel::ValueRole).toInt() != slowId);
    }

    /* Device-authoritative: an item press sends only the item's EVENT; it
     * must never commit the selection on the client side. */
    void buttonGroup_itemPress_doesNotChangeGroupValue()
    {
        DeviceController dc;
        injectBootstrap(dc,
            "widgets:\n"
            "  mode:\n"
            "    type: button-group\n"
            "    items:\n"
            "      fast:\n"
            "        label: \"Fast\"\n"
            "      slow:\n"
            "        label: \"Slow\"\n");
        WidgetModel* m = dc.widgetModel();
        injectStateUpdateU8(dc, 0x10, 0x11);
        dc.sendButtonPress(0x12);
        dc.sendButtonRelease(0x12);
        dc.sendButtonClick(0x12);
        QCOMPARE(m->data(m->index(0, 0), WidgetModel::ValueRole).toInt(), 0x11);
    }
};

QTEST_MAIN(TestDeviceController)
#include "test_device_controller.moc"
