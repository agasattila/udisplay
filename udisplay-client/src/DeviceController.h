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
 *   successful parse, in BOTH design mode (applyParsedYaml) and real mode
 *   (onBootstrapSucceeded) — these are two separate functions, not one shared
 *   choke point (see applyParsedYaml's doc comment below). On parse failure,
 *   prints the failure reason instead. On capability rejection specifically,
 *   prints both the full dump AND the rejection reason, since the model is
 *   fully resolved at that point — only the compatibility check failed.
 *
 * DESIGN MODE FLOW
 *
 *  main.cpp
 *    QCommandLineParser sees -design <file>
 *    dc.startDesignMode(path)
 *          │
 *          ├─ reads file (raw YAML bytes)
 *          ├─ applyParsedYaml(raw)
 *          │     ├─ success → m_designErrorString = ""
 *          │     │             (--debug: printDebugDump → qInfo)
 *          │     └─ failure → m_designErrorString = yamlParser.errorString()
 *          │                   (--debug: printDebugFailure → qInfo)
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
 *                         └─ applyParsedYaml(raw)  (same --debug behavior as above)
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
    void setState(const QString& s);
    void setError(const QString& msg);
    void setDesignError(const QString& msg);
    void teardown();
    /** Wire BootstrapManager signals and start the connection. Called by both
     *  connectTcp() and connectDiscovered() after transport creation. */
    void startConnection(Transport* t);
    void initCache();
    QByteArray cachedBlob(const QByteArray& root) const;
    void storeBlob(const QByteArray& root, const QByteArray& blob);
    QByteArray decompressBlob(const QByteArray& compressed);
    /** Parse raw YAML bytes and populate the widget model.
     *  Returns true on success. On failure, sets m_designErrorString.
     *  Used by design mode (reloadDesignFile, direct file read) ONLY —
     *  onBootstrapSucceeded (real mode) does NOT call this; it independently
     *  re-implements parse + populate (pre-existing duplication, see
     *  TODO-034). Both paths call printDebugDump()/printDebugFailure() when
     *  --debug is set, so debug output covers both modes despite the split. */
    bool applyParsedYaml(const QByteArray& raw);

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
