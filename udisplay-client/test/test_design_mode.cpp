/**
 * Design Mode unit tests.
 *
 * Tests startDesignMode() and reloadDesignFile() paths in DeviceController:
 *   - valid YAML → state "running", model populated, designErrorString empty
 *   - missing file → state "running", designErrorString set
 *   - parse error → state "running", designErrorString set
 *   - reload after error → designErrorString clears, model re-populated
 *   - reload after success → model re-populated with new content
 *
 * All tests use direct method calls (no QFileSystemWatcher events needed).
 * The watcher fires reloadDesignFile() via the debounce timer; we test
 * reloadDesignFile() directly by mutating the temp file and calling
 * the slot via QMetaObject (same pattern as test_device_controller.cpp).
 */
#include <QtTest>
#include <QFile>
#include <QTemporaryFile>
#include <zlib.h>
#include "DeviceController.h"
#include "DeviceInfo.h"
#include "WidgetModel.h"

/* Helper: compress raw bytes with zlib (RFC 1950 deflate format).
 * Used to synthesise the blob that onBootstrapSucceeded receives. */
static QByteArray zlibCompress(const QByteArray& raw)
{
    uLong bound = compressBound(static_cast<uLong>(raw.size()));
    QByteArray out(static_cast<int>(bound), '\0');
    uLong outLen = bound;
    compress(reinterpret_cast<Bytef*>(out.data()), &outLen,
             reinterpret_cast<const Bytef*>(raw.constData()),
             static_cast<uLong>(raw.size()));
    out.resize(static_cast<int>(outLen));
    return out;
}

/* Helper: inject a YAML blob via onBootstrapSucceeded (bypasses transport). */
static void injectBootstrap(DeviceController& dc, const char* yaml)
{
    QByteArray blob = zlibCompress(QByteArray(yaml));
    QMetaObject::invokeMethod(&dc, "onBootstrapSucceeded",
                              Qt::DirectConnection,
                              Q_ARG(QByteArray, QByteArray(32, '\0')),
                              Q_ARG(QByteArray, blob));
}

/* Helper: look up the model value for a top-level widget by its widgetId. */
static QVariant modelValueForId(WidgetModel* m, uint8_t id)
{
    for (int r = 0; r < m->rowCount(); ++r) {
        QModelIndex idx = m->index(r);
        if (m->data(idx, WidgetModel::WidgetIdRole).toUInt() == static_cast<uint>(id))
            return m->data(idx, WidgetModel::ValueRole);
    }
    return QVariant();
}

/* Helper: write content to a named temp file and return the path.
 * The caller owns the QTemporaryFile and must keep it alive. */
static QString writeTempYaml(QTemporaryFile& f, const char* content)
{
    f.setAutoRemove(true);
    if (!f.isOpen()) {
        if (!f.open()) return {};
    }
    f.seek(0);
    f.resize(0);
    f.write(content);
    f.flush();
    return f.fileName();
}

/* Helper: call reloadDesignFile() directly via Qt meta-object.
 * (The slot is private — same technique as test_device_controller.cpp.) */
static void triggerReload(DeviceController& dc)
{
    QMetaObject::invokeMethod(&dc, "reloadDesignFile", Qt::DirectConnection);
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

class TestDesignMode : public QObject
{
    Q_OBJECT

private slots:

    /* ── startDesignMode — valid YAML ─────────────────────────── */

    void valid_yaml_sets_running_state()
    {
        const char* yaml =
            "device:\n"
            "  name: testdev\n"
            "widgets:\n"
            "  a:\n"
            "    type: toggle\n";

        QTemporaryFile f;
        const QString path = writeTempYaml(f, yaml);
        QVERIFY(!path.isEmpty());

        DeviceController dc;
        QSignalSpy stateSpy(&dc, &DeviceController::stateChanged);

        dc.startDesignMode(path);

        QCOMPARE(dc.state(), QStringLiteral("running"));
        QVERIFY(stateSpy.count() >= 1);
    }

    void valid_yaml_clears_design_error()
    {
        const char* yaml =
            "device:\n"
            "  name: testdev\n"
            "widgets:\n"
            "  a:\n"
            "    type: led\n";

        QTemporaryFile f;
        const QString path = writeTempYaml(f, yaml);
        QVERIFY(!path.isEmpty());

        DeviceController dc;
        dc.startDesignMode(path);

        QVERIFY(dc.designErrorString().isEmpty());
    }

    void valid_yaml_populates_device_name()
    {
        const char* yaml =
            "device:\n"
            "  name: MyDevice\n"
            "widgets:\n"
            "  a:\n"
            "    type: display\n"
            "    label: Value\n";

        QTemporaryFile f;
        const QString path = writeTempYaml(f, yaml);
        QVERIFY(!path.isEmpty());

        DeviceController dc;
        dc.startDesignMode(path);

        QCOMPARE(dc.deviceName(), QStringLiteral("MyDevice"));
    }

    void valid_yaml_populates_widget_model()
    {
        const char* yaml =
            "device:\n"
            "  name: testdev\n"
            "widgets:\n"
            "  a:\n"
            "    type: toggle\n"
            "  b:\n"
            "    type: led\n";

        QTemporaryFile f;
        const QString path = writeTempYaml(f, yaml);
        QVERIFY(!path.isEmpty());

        DeviceController dc;
        dc.startDesignMode(path);

        QVERIFY(dc.widgetModel()->rowCount() >= 2);
    }

    /* ── startDesignMode — missing file ──────────────────────── */

    void missing_file_sets_design_error()
    {
        DeviceController dc;
        QSignalSpy errSpy(&dc, &DeviceController::designErrorStringChanged);

        dc.startDesignMode(QStringLiteral("/tmp/this_file_does_not_exist_udisplay.yaml"));

        QCOMPARE(dc.state(), QStringLiteral("running"));
        QVERIFY(!dc.designErrorString().isEmpty());
        QVERIFY(dc.designErrorString().contains(
            QStringLiteral("/tmp/this_file_does_not_exist_udisplay.yaml")));
        QVERIFY(errSpy.count() >= 1);
    }

    /* ── startDesignMode — parse error ───────────────────────── */

    void invalid_yaml_sets_design_error()
    {
        const char* badYaml = "this: is: not: valid: yaml: [\n";

        QTemporaryFile f;
        const QString path = writeTempYaml(f, badYaml);
        QVERIFY(!path.isEmpty());

        DeviceController dc;
        dc.startDesignMode(path);

        QCOMPARE(dc.state(), QStringLiteral("running"));
        QVERIFY(!dc.designErrorString().isEmpty());
    }

    void invalid_yaml_does_not_crash()
    {
        const char* badYaml = ":\n  -\n    - bad\n  :\n";

        QTemporaryFile f;
        const QString path = writeTempYaml(f, badYaml);
        QVERIFY(!path.isEmpty());

        DeviceController dc;
        dc.startDesignMode(path);  /* must not crash */
        QCOMPARE(dc.state(), QStringLiteral("running"));
    }

    /* ── reloadDesignFile — error then success ───────────────── */

    void reload_after_error_clears_design_error()
    {
        /* Start with bad YAML */
        const char* badYaml = "not valid yaml [\n";
        QTemporaryFile f;
        QString path = writeTempYaml(f, badYaml);
        QVERIFY(!path.isEmpty());

        DeviceController dc;
        dc.startDesignMode(path);
        QVERIFY(!dc.designErrorString().isEmpty());

        /* Fix the file and trigger reload */
        const char* goodYaml =
            "device:\n"
            "  name: fixed\n"
            "widgets:\n"
            "  a:\n"
            "    type: toggle\n";
        writeTempYaml(f, goodYaml);

        QSignalSpy errSpy(&dc, &DeviceController::designErrorStringChanged);
        triggerReload(dc);

        QVERIFY(dc.designErrorString().isEmpty());
        QVERIFY(errSpy.count() >= 1);
    }

    /* ── reloadDesignFile — success then error ───────────────── */

    void reload_after_success_sets_design_error()
    {
        const char* goodYaml =
            "device:\n"
            "  name: testdev\n"
            "widgets:\n"
            "  a:\n"
            "    type: toggle\n";
        QTemporaryFile f;
        QString path = writeTempYaml(f, goodYaml);
        QVERIFY(!path.isEmpty());

        DeviceController dc;
        dc.startDesignMode(path);
        QVERIFY(dc.designErrorString().isEmpty());

        /* Corrupt the file and reload */
        writeTempYaml(f, "bad: yaml: [\n");
        triggerReload(dc);

        QVERIFY(!dc.designErrorString().isEmpty());
        QCOMPARE(dc.state(), QStringLiteral("running")); /* state must not change */
    }

    /* ── reloadDesignFile — model updates on re-parse ───────── */

    void reload_updates_widget_model()
    {
        const char* yaml1 =
            "device:\n"
            "  name: testdev\n"
            "widgets:\n"
            "  a:\n"
            "    type: toggle\n";
        QTemporaryFile f;
        QString path = writeTempYaml(f, yaml1);
        QVERIFY(!path.isEmpty());

        DeviceController dc;
        dc.startDesignMode(path);
        int count1 = dc.widgetModel()->rowCount();

        /* Add a second widget */
        const char* yaml2 =
            "device:\n"
            "  name: testdev\n"
            "widgets:\n"
            "  a:\n"
            "    type: toggle\n"
            "  b:\n"
            "    type: led\n";
        writeTempYaml(f, yaml2);
        triggerReload(dc);

        int count2 = dc.widgetModel()->rowCount();
        QVERIFY(count2 > count1);
    }

    /* ── design mode does not affect normal connect path ─────── */

    void normal_mode_unaffected_when_design_mode_not_started()
    {
        DeviceController dc;
        /* Not in design mode — designErrorString must be empty */
        QVERIFY(dc.designErrorString().isEmpty());
        QCOMPARE(dc.state(), QStringLiteral("disconnected"));
    }

    /* connectTcp() must refuse to run while in design mode (TODO-034 D2) —
     * a real bootstrap and a design-mode instance must never interleave, or
     * the mode-gated behavior in applyParsedYaml (style reset, qWarning
     * scope) would read the wrong side of the divergence.
     *
     * The guard must NOT mutate state()/errorString(): DeviceController.h
     * documents design mode's contract as "state stays running" — routing
     * this through setError() would silently and permanently corrupt that
     * (nothing ever resets state() back to "running" afterward). Caught by
     * adversarial review during /ship; the guard logs via qWarning() only. */
    void connectTcp_refusedWhileInDesignMode()
    {
        const char* yaml =
            "device:\n"
            "  name: testdev\n"
            "widgets:\n"
            "  a:\n"
            "    type: toggle\n";
        QTemporaryFile f;
        const QString path = writeTempYaml(f, yaml);
        QVERIFY(!path.isEmpty());

        DeviceController dc;
        dc.startDesignMode(path);
        QCOMPARE(dc.state(), QStringLiteral("running"));

        const QString captured = captureDebugOutput([&]() {
            dc.connectTcp(QStringLiteral("127.0.0.1"), 5555);
        });

        /* state()/errorString() are reserved for the real-device lifecycle —
         * a refused design-mode connection attempt must leave both alone. */
        QCOMPARE(dc.state(), QStringLiteral("running"));
        QVERIFY(dc.errorString().isEmpty());
        QVERIFY(captured.contains(QStringLiteral("design mode")));
        /* The design-mode model must survive the refused connection attempt. */
        QVERIFY(dc.designErrorString().isEmpty());
        QCOMPARE(dc.widgetModel()->rowCount(), 1);
    }

    /* connectDiscovered() must refuse to run while in design mode too — same
     * guard, different Q_INVOKABLE entry point. Uses a non-BLE DeviceInfo so
     * the test doesn't depend on HAVE_BLE being compiled in. */
    void connectDiscovered_refusedWhileInDesignMode()
    {
        const char* yaml =
            "device:\n"
            "  name: testdev\n"
            "widgets:\n"
            "  a:\n"
            "    type: toggle\n";
        QTemporaryFile f;
        const QString path = writeTempYaml(f, yaml);
        QVERIFY(!path.isEmpty());

        DeviceController dc;
        dc.startDesignMode(path);
        QCOMPARE(dc.state(), QStringLiteral("running"));

        DeviceInfo di = DeviceInfo::makeTcp(
            QStringLiteral("192.168.1.42"), QStringLiteral("Some Device"),
            QStringLiteral("192.168.1.42"), 5555);
        const QString captured = captureDebugOutput([&]() {
            dc.connectDiscovered(QVariant::fromValue(di));
        });

        QCOMPARE(dc.state(), QStringLiteral("running"));
        QVERIFY(dc.errorString().isEmpty());
        QVERIFY(captured.contains(QStringLiteral("design mode")));
        QVERIFY(dc.designErrorString().isEmpty());
        QCOMPARE(dc.widgetModel()->rowCount(), 1);
    }

    /* connectDevice() is a third Q_INVOKABLE entry point onto the same
     * connection path. Its pre-existing state guard (`state != disconnected
     * && state != error`) would otherwise swallow a design-mode call
     * silently — since design mode leaves state() == "running" — with no
     * signal at all, inconsistent with connectTcp()/connectDiscovered().
     * Found by the API-contract specialist during /ship. */
    void connectDevice_refusedWhileInDesignMode()
    {
        const char* yaml =
            "device:\n"
            "  name: testdev\n"
            "widgets:\n"
            "  a:\n"
            "    type: toggle\n";
        QTemporaryFile f;
        const QString path = writeTempYaml(f, yaml);
        QVERIFY(!path.isEmpty());

        DeviceController dc;
        dc.startDesignMode(path);
        QCOMPARE(dc.state(), QStringLiteral("running"));

        const QString captured = captureDebugOutput([&]() {
            dc.connectDevice(0, QStringLiteral("127.0.0.1"), 5555);
        });

        QCOMPARE(dc.state(), QStringLiteral("running"));
        QVERIFY(dc.errorString().isEmpty());
        QVERIFY(captured.contains(QStringLiteral("design mode")));
        QVERIFY(dc.designErrorString().isEmpty());
        QCOMPARE(dc.widgetModel()->rowCount(), 1);
    }

    /* D3's false branch: reloading after the previously-active style is
     * removed from the new YAML must fall back to "default", not keep
     * pointing at a style that no longer exists. */
    void reload_fallsBackToDefault_whenSelectedStyleRemoved()
    {
        const char* yamlWithWarning =
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
        QTemporaryFile f;
        const QString path = writeTempYaml(f, yamlWithWarning);
        QVERIFY(!path.isEmpty());

        DeviceController dc;
        dc.startDesignMode(path);
        dc.setActiveStyle(QStringLiteral("warning"));
        QCOMPARE(dc.activeStyle().value(QStringLiteral("accent")).toString(),
                  QStringLiteral("#f5a623"));

        const char* yamlWithoutWarning =
            "device:\n"
            "  name: testdev\n"
            "style:\n"
            "  default:\n"
            "    accent: \"#00d4aa\"\n"
            "widgets:\n"
            "  a:\n"
            "    type: toggle\n";
        writeTempYaml(f, yamlWithoutWarning);
        triggerReload(dc);

        QCOMPARE(dc.activeStyle().value(QStringLiteral("accent")).toString(),
                  QStringLiteral("#00d4aa"));
    }

    /* ── debug_state: design-mode preview injection ──────────────── */

    void debugState_appliedOnLoad()
    {
        const char* yaml =
            "device:\n"
            "  name: testdev\n"
            "widgets:\n"
            "  temp:\n"
            "    type: display\n"
            "    debug_state: 7.5\n";
        QTemporaryFile f;
        const QString path = writeTempYaml(f, yaml);
        QVERIFY(!path.isEmpty());

        DeviceController dc;
        dc.startDesignMode(path);

        QVERIFY(dc.designErrorString().isEmpty());
        QVariant val = modelValueForId(dc.widgetModel(), 0x10);
        QCOMPARE(val.toDouble(), 7.5);
    }

    void debugState_reappliedOnReload()
    {
        const char* yaml1 =
            "device:\n"
            "  name: testdev\n"
            "widgets:\n"
            "  temp:\n"
            "    type: display\n"
            "    debug_state: 7.5\n";
        QTemporaryFile f;
        const QString path = writeTempYaml(f, yaml1);
        QVERIFY(!path.isEmpty());

        DeviceController dc;
        dc.startDesignMode(path);
        QCOMPARE(modelValueForId(dc.widgetModel(), 0x10).toDouble(), 7.5);

        const char* yaml2 =
            "device:\n"
            "  name: testdev\n"
            "widgets:\n"
            "  temp:\n"
            "    type: display\n"
            "    debug_state: 99.9\n";
        writeTempYaml(f, yaml2);
        triggerReload(dc);

        QCOMPARE(modelValueForId(dc.widgetModel(), 0x10).toDouble(), 99.9);
    }

    void debugState_absent_valueNull()
    {
        const char* yaml =
            "device:\n"
            "  name: testdev\n"
            "widgets:\n"
            "  temp:\n"
            "    type: display\n"
            "    label: Temperature\n";
        QTemporaryFile f;
        const QString path = writeTempYaml(f, yaml);
        QVERIFY(!path.isEmpty());

        DeviceController dc;
        dc.startDesignMode(path);

        QVERIFY(dc.designErrorString().isEmpty());
        QVERIFY(modelValueForId(dc.widgetModel(), 0x10).isNull());
    }

    void debugState_containerChild_applied()
    {
        const char* yaml =
            "device:\n"
            "  name: testdev\n"
            "widgets:\n"
            "  ctrl_row:\n"
            "    type: row\n"
            "    widgets:\n"
            "      temp:\n"
            "        type: display\n"
            "        debug_state: 5.0\n";
        QTemporaryFile f;
        const QString path = writeTempYaml(f, yaml);
        QVERIFY(!path.isEmpty());

        DeviceController dc;
        dc.startDesignMode(path);

        QVERIFY(dc.designErrorString().isEmpty());
        /* Row is at row 0; its first container child should have value 5.0 */
        QVariant props = dc.widgetModel()->data(dc.widgetModel()->index(0),
                                                WidgetModel::PropsRole);
        QVariantList items = props.toMap()[QStringLiteral("items")].toList();
        QCOMPARE(items.size(), 1);
        QCOMPARE(items[0].toMap()[QStringLiteral("value")].toDouble(), 5.0);
    }

    /* CRITICAL: debug_state must NOT be injected when YAML comes from a real
     * device via onBootstrapSucceeded. Both paths now share applyParsedYaml()
     * (TODO-034), so the guard is the explicit designMode parameter passed by
     * each caller, not separate implementations. */
    void debugState_deviceMode_ignored()
    {
        const char* yaml =
            "device:\n"
            "  name: testdev\n"
            "widgets:\n"
            "  temp:\n"
            "    type: display\n"
            "    debug_state: 99.9\n";

        DeviceController dc;
        injectBootstrap(dc, yaml);

        QCOMPARE(dc.state(), QStringLiteral("running"));
        QVERIFY(modelValueForId(dc.widgetModel(), 0x10).isNull());
    }

    /* ── --debug: design mode ─────────────────────────────────────── */

    void debugMode_off_producesNoDumpOutput()
    {
        const char* yaml =
            "device:\n"
            "  name: testdev\n"
            "widgets:\n"
            "  a:\n"
            "    type: toggle\n";
        QTemporaryFile f;
        const QString path = writeTempYaml(f, yaml);
        QVERIFY(!path.isEmpty());

        DeviceController dc;
        /* setDebugMode() never called — --debug not passed */
        const QString captured = captureDebugOutput([&]() { dc.startDesignMode(path); });

        QVERIFY(!captured.contains(QStringLiteral("widgets: (")));
    }

    void debugMode_on_dumpsFullyResolvedTreeOnLoad()
    {
        const char* yaml =
            "device:\n"
            "  name: testdev\n"
            "widgets:\n"
            "  temp:\n"
            "    type: display\n"
            "    label: Temperature\n";
        QTemporaryFile f;
        const QString path = writeTempYaml(f, yaml);
        QVERIFY(!path.isEmpty());

        DeviceController dc;
        dc.setDebugMode(true);
        const QString captured = captureDebugOutput([&]() { dc.startDesignMode(path); });

        QVERIFY(captured.contains(QStringLiteral("name=\"testdev\"")));
        QVERIFY(captured.contains(QStringLiteral("[0x10] display \"Temperature\"")));
    }

    void debugMode_on_printsFailureReasonOnParseError()
    {
        const char* badYaml = "not valid yaml [\n";
        QTemporaryFile f;
        const QString path = writeTempYaml(f, badYaml);
        QVERIFY(!path.isEmpty());

        DeviceController dc;
        dc.setDebugMode(true);
        const QString captured = captureDebugOutput([&]() { dc.startDesignMode(path); });

        QVERIFY(captured.contains(QStringLiteral("[debug] parse failed:")));
        QVERIFY(!captured.contains(QStringLiteral("widgets: (")));
    }

    void debugMode_on_dumpsAgainOnReloadWithNewContent()
    {
        const char* yaml1 =
            "device:\n"
            "  name: testdev\n"
            "widgets:\n"
            "  a:\n"
            "    type: toggle\n";
        QTemporaryFile f;
        const QString path = writeTempYaml(f, yaml1);
        QVERIFY(!path.isEmpty());

        DeviceController dc;
        dc.setDebugMode(true);
        captureDebugOutput([&]() { dc.startDesignMode(path); }); /* initial load */

        const char* yaml2 =
            "device:\n"
            "  name: testdev\n"
            "widgets:\n"
            "  a:\n"
            "    type: toggle\n"
            "  b:\n"
            "    type: led\n";
        writeTempYaml(f, yaml2);
        const QString captured = captureDebugOutput([&]() { triggerReload(dc); });

        QVERIFY(captured.contains(QStringLiteral("led")));
        QVERIFY(captured.contains(QStringLiteral("(2 top-level)")));
    }

    /* ── TODO-034 regression tests: merged applyParsedYaml behavior ─── */

    /* Design-mode reload preserves the currently-selected style if the new
     * YAML still defines it (D3) — the opposite of a fresh bootstrap
     * connection, which always resets to "default". No prior test exercised
     * style selection across a design-mode reload at all. */
    void reload_preserves_selected_style_if_still_valid()
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
        QTemporaryFile f;
        const QString path = writeTempYaml(f, yaml);
        QVERIFY(!path.isEmpty());

        DeviceController dc;
        dc.startDesignMode(path);
        QVERIFY(dc.availableStyles().contains(QStringLiteral("warning")));

        dc.setActiveStyle(QStringLiteral("warning"));
        QCOMPARE(dc.activeStyle().value(QStringLiteral("accent")).toString(),
                  QStringLiteral("#f5a623"));

        /* Reload the same file (e.g. an unrelated edit) — the custom style
         * survives since it's still defined in the new YAML. */
        triggerReload(dc);
        QCOMPARE(dc.activeStyle().value(QStringLiteral("accent")).toString(),
                  QStringLiteral("#f5a623"));
    }

    /* Design mode must NOT log parse diagnostics via qWarning() (D4) —
     * designErrorString/UI already surfaces problems during hot-reload
     * iteration. Only the negative (bootstrap does, design mode doesn't) was
     * ever implicit; nothing asserted it directly. */
    void reload_warningYaml_producesNoQWarning()
    {
        const char* yaml =
            "widgets:\n"
            "  led:\n"
            "    type: led\n"
            "    color: not_a_hex_color\n";
        QTemporaryFile f;
        const QString path = writeTempYaml(f, yaml);
        QVERIFY(!path.isEmpty());

        DeviceController dc;
        const QString captured = captureDebugOutput([&]() { dc.startDesignMode(path); });

        QVERIFY(dc.designErrorString().isEmpty());
        QVERIFY2(!captured.contains(QStringLiteral("[YamlParser WARNING]")),
                  "design mode must not log parse diagnostics via qWarning() "
                  "(TODO-034 D4)");
    }
};

QTEST_MAIN(TestDesignMode)
#include "test_design_mode.moc"
