// SPDX-License-Identifier: LGPL-3.0-only
// Copyright (c) 2026 Attila Agas

/**
 * DeviceController — top-level QML-accessible object.
 *
 * Exposed to QML as "controller".  Owns the transport, bootstrap manager,
 * widget model, and SQLite blob cache.
 *
 * QML usage:
 *   controller.connectTcp("192.168.1.42", 5555)
 *   controller.disconnect()
 *   controller.sendButtonPress(widgetId)
 *   controller.widgetModel        // ListView model
 *   controller.state              // "disconnected"|"connecting"|"bootstrapping"|"running"|"error"
 *   controller.deviceName
 *   controller.errorString
 *   controller.designErrorString  // non-empty when in design mode with a parse error
 *
 * Design Mode:
 *   Activated via startDesignMode(filePath) (called from main.cpp when -design
 *   argument is present). Reads the YAML file directly (no TCP, no decompression),
 *   watches for file changes via QFileSystemWatcher, and re-renders on every save.
 *   Parse errors are shown in-place via the designErrorString property; state
 *   stays "running" so the QML nav stack is unaffected.
 *
 * Debug Dump:
 *   Activated via setDebugMode(true) (called from main.cpp when --debug is
 *   present). Prints the fully-resolved widget tree via qInfo() after every
 *   successful parse, in BOTH design mode and real mode — both now go
 *   through the single shared applyParsedYaml() (see its doc comment below;
 *   TODO-034 collapsed the two independent implementations into one). On
 *   parse failure, prints the failure reason instead. On capability
 *   rejection specifically, prints both the full dump AND the rejection
 *   reason, since the model is fully resolved at that point — only the
 *   compatibility check failed.
 *
 * DESIGN MODE FLOW
 *
 *  main.cpp
 *    QCommandLineParser sees -design <file>
 *    dc.startDesignMode(path)
 *          │
 *          ├─ reads file (raw YAML bytes)
 *          ├─ applyParsedYaml(raw, /*designMode=*\/true)
 *          │     ├─ Success      → m_designErrorString = ""
 *          │     │                 (--debug: printDebugDump → qInfo)
 *          │     └─ ParseFailed  → m_designErrorString = yamlParser.errorString()
 *          │                       (--debug: printDebugFailure → qInfo)
 *          │                       (design mode never passes a capabilityGate,
 *          │                        so Rejected can't happen on this path)
 *          ├─ setState("running")
 *          └─ m_watcher.addPath(path)
 *
 *  QFileSystemWatcher::fileChanged(path)
 *          │
 *          ├─ m_watcher.addPath(path)   ← re-add: atomic-rename editors drop it
 *          └─ m_debounce.start(150)     ← absorb multi-flush saves
 *                   │ (150ms later)
 *                   └─ reloadDesignFile()
 *                         ├─ reads file
 *                         └─ applyParsedYaml(raw, /*designMode=*\/true)  (same as above)
 *
 * BOOTSTRAP FLOW (real device)
 *
 *  BootstrapManager::succeeded(merkleRoot, compressedBlob)
 *          │
 *          └─ onBootstrapSucceeded(merkleRoot, compressedBlob)
 *                ├─ decompressBlob(compressedBlob)
 *                │     └─ empty → setError("Failed to decompress...") + printDebugFailure, return
 *                ├─ capabilityGate = lambda checking kKnownCapabilities,
 *                │                   captures rejectionReason by reference
 *                ├─ applyParsedYaml(yaml, /*designMode=*\/false, capabilityGate)
 *                │     ├─ ParseFailed → setError("YAML parse failed: " + yamlParser.errorString())
 *                │     │                 (no teardown() — matches historical behavior, see TODO-046)
 *                │     ├─ Rejected    → setError(rejectionReason), teardown()
 *                │     │                 (model/name/styles left untouched — gate ran
 *                │     │                  before any mutation, see applyParsedYaml doc)
 *                │     └─ Success     → (falls through below)
 *                ├─ storeBlob(merkleRoot, compressedBlob)
 *                ├─ setState("running")
 *                └─ m_watchdog.start()
 *
 *  connectTcp()/connectDiscovered()/connectDevice() refuse to run
 *  (qWarning + return, no state mutation — see rejectConnectionInDesignMode()
 *  doc) while m_designMode is true — the two flows above never interleave on
 *  one instance (TODO-034 D2: this used to be enforced by nothing).
 */
#pragma once

#include "BootstrapManager.h"
#include "WidgetModel.h"
#include "YamlParser.h"
#include <QFileSystemWatcher>
#include <QMap>
#include <QObject>
#include <QSqlDatabase>
#include <QString>
#include <QTimer>
#include <QVariantMap>
#include <functional>

class Transport;

class DeviceController : public QObject
{
    Q_OBJECT

    Q_PROPERTY(QString     state             READ state             NOTIFY stateChanged)
    Q_PROPERTY(QString     deviceName        READ deviceName        NOTIFY deviceNameChanged)
    Q_PROPERTY(QString     errorString       READ errorString       NOTIFY errorStringChanged)
    Q_PROPERTY(QString     designErrorString READ designErrorString NOTIFY designErrorStringChanged)
    Q_PROPERTY(WidgetModel* widgetModel      READ widgetModel       CONSTANT)
    Q_PROPERTY(QVariantMap  activeStyle      READ activeStyle       NOTIFY activeStyleChanged)
    Q_PROPERTY(QStringList  availableStyles  READ availableStyles   NOTIFY availableStylesChanged)

public:
    explicit DeviceController(QObject* parent = nullptr);
    ~DeviceController() override;

    QString     state()             const { return m_state; }
    QString     deviceName()        const { return m_deviceName; }
    QString     errorString()       const { return m_errorString; }
    QString     designErrorString() const { return m_designErrorString; }
    WidgetModel* widgetModel()            { return &m_model; }
    QVariantMap activeStyle()       const { return m_activeStyleMap; }
    QStringList availableStyles()   const { return m_styles.keys(); }

    /* ── Design Mode ────────────────────────────────────────────── */
    /** Call from main() when -design <file> is present.
     *  Reads the file, populates the model, and starts watching for changes. */
    void startDesignMode(const QString& filePath);

    /* ── Debug Dump ─────────────────────────────────────────────── */
    /** Call from main() when --debug is present. Must be called before any
     *  parse happens (before startDesignMode() / connectTcp() / etc.) for the
     *  first dump to fire. */
    void setDebugMode(bool enabled) { m_debugMode = enabled; }

    /* ── QML-invokable actions ──────────────────────────────────── */
    Q_INVOKABLE void connectTcp(const QString& host, quint16 port);
    Q_INVOKABLE void connectDevice(int transportType, const QString& address, quint16 port);
    Q_INVOKABLE void connectDiscovered(const QVariant& deviceInfo);
    Q_INVOKABLE void disconnectDevice();
    Q_INVOKABLE void setActiveStyle(const QString& name);

    Q_INVOKABLE void sendButtonPress(int widgetId);
    Q_INVOKABLE void sendButtonRelease(int widgetId);
    Q_INVOKABLE void sendButtonClick(int widgetId);
    Q_INVOKABLE void sendSliderChange(int widgetId, double value);
    Q_INVOKABLE void sendToggleChange(int widgetId, bool state);
    Q_INVOKABLE void sendTextSubmit(int widgetId, const QString& text);
    Q_INVOKABLE void sendSelectionChange(int widgetId, int index);

signals:
    void stateChanged();
    void deviceNameChanged();
    void errorStringChanged();
    void designErrorStringChanged();
    void activeStyleChanged();
    void availableStylesChanged();
    void parseWarningsChanged(QList<YamlParser::ParseDiagnostic> diagnostics);

private slots:
    void onBootstrapSucceeded(QByteArray merkleRoot, QByteArray compressedBlob);
    void onBootstrapFailed(QString reason);
    void onStateUpdateData(Proto::StateUpdateData update);
    void onPropertyCommand(Proto::PropertyCommand cmd);
    void onHeartbeat();
    void onFileChanged(const QString& path);
    void reloadDesignFile();

private:
    /** Result of applyParsedYaml(). ParseFailed and Rejected are distinct
     *  because callers react differently: a capability Rejected connection
     *  calls teardown(), a ParseFailed one does not (TODO-046 tracks whether
     *  that historical asymmetry should change — this refactor preserves it
     *  exactly, not decide it fresh). */
    enum class ApplyResult { Success, ParseFailed, Rejected };

    void setState(const QString& s);
    void setError(const QString& msg);
    void setDesignError(const QString& msg);
    void teardown();
    /** True if a connection attempt should be refused because we're in
     *  design mode. connectTcp()/connectDiscovered()/connectDevice() call
     *  this first — design mode and a real bootstrap must never run
     *  concurrently on one instance (TODO-034 D2). Logs via qWarning() only
     *  — deliberately does NOT call setError(): design mode's contract is
     *  that state() stays "running" and errorString()/state() are reserved
     *  for the real-device lifecycle (see class doc above). */
    bool rejectConnectionInDesignMode();
    /** Wire BootstrapManager signals and start the connection. Called by both
     *  connectTcp() and connectDiscovered() after transport creation. */
    void startConnection(Transport* t);
    void initCache();
    QByteArray cachedBlob(const QByteArray& root) const;
    void storeBlob(const QByteArray& root, const QByteArray& blob);
    QByteArray decompressBlob(const QByteArray& compressed);
    /** Parse raw YAML bytes and populate the widget model. Shared by both
     *  design mode (reloadDesignFile) and real-device bootstrap
     *  (onBootstrapSucceeded) — see the DESIGN MODE FLOW / BOOTSTRAP FLOW
     *  diagrams above (TODO-034 collapsed what used to be two independent
     *  implementations).
     *
     *  designMode is passed explicitly by each caller rather than read from
     *  m_designMode, so behavior is tied to which call path actually ran, not
     *  a mutable flag — it gates debug_state injection, the active-style
     *  reset-vs-preserve divergence, and qWarning() diagnostic logging
     *  (bootstrap only).
     *
     *  capabilityGate, if provided, runs right after a successful parse but
     *  BEFORE any member state (m_deviceName/m_styles/m_model) is mutated. A
     *  non-empty return rejects the YAML (ApplyResult::Rejected) without
     *  touching that state — a rejected connection never clobbers whatever
     *  was previously loaded. Design mode never passes a gate, so it can
     *  only return Success or ParseFailed.
     *
     *  Diagnostics (parseWarningsChanged, plus qWarning() in bootstrap mode)
     *  emit right after parse, before mutation — even when a capabilityGate
     *  subsequently rejects the connection, since the parser's findings are
     *  independent of protocol compatibility. */
    ApplyResult applyParsedYaml(const QByteArray& raw, bool designMode,
                                const std::function<QString(const QStringList&)>& capabilityGate = nullptr);

    /** --debug: print the fully-resolved widget tree via qInfo(). No-op if
     *  m_debugMode is false. */
    void printDebugDump(const QList<WidgetDef>& widgets, const QString& name,
                         const QString& version, const QString& activeStyleName) const;
    /** --debug: print a parse/bootstrap failure reason via qInfo(). No-op if
     *  m_debugMode is false. */
    void printDebugFailure(const QString& reason) const;

    WidgetModel      m_model;
    YamlParser       m_yamlParser;
    Transport*       m_transport       = nullptr;
    BootstrapManager* m_bootstrap      = nullptr;
    QString          m_state           = QStringLiteral("disconnected");
    QMap<QString, StyleToken> m_styles;
    QString          m_activeStyleName = QStringLiteral("default");
    QVariantMap      m_activeStyleMap;
    QString          m_deviceName;
    QString          m_errorString;
    QString          m_designErrorString;
    QSqlDatabase     m_db;

    /* Heartbeat watchdog: 15s timeout (3× the 5s device heartbeat interval).
     * Started when bootstrap succeeds. Reset on each HEARTBEAT message.
     * On expiry → setError("Heartbeat timeout"). */
    QTimer           m_watchdog;
    static constexpr int kWatchdogMs = 15'000;

    /* Design mode state */
    bool             m_designMode      = false;
    QString          m_designFilePath;
    QFileSystemWatcher m_watcher;
    QTimer           m_debounce;       /* 150ms debounce — absorbs multi-flush saves */

    /* --debug: set via setDebugMode(), gates printDebugDump()/printDebugFailure() */
    bool             m_debugMode       = false;
};
