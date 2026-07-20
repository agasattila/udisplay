// SPDX-License-Identifier: LGPL-3.0-only
// Copyright (c) 2026 Attila Agas

#include "DeviceController.h"
#include "TcpTransport.h"
#include "DeviceInfo.h"
#include "WidgetDump.h"
#include <QDebug>
#include <functional>
#ifdef HAVE_BLE
#include "BleTransport.h"
#include <QBluetoothDeviceInfo>
#endif
#include <QFile>
#include <QSqlError>
#include <QSqlQuery>
#include <QStandardPaths>
#include <QDir>
#include <zlib.h>

/* ── Style helpers ──────────────────────────────────────────────────────── */

static QVariantMap buildStyleVariantMap(const StyleToken& t)
{
    QVariantMap m;
    m[QStringLiteral("background")]   = t.background;
    m[QStringLiteral("surface")]      = t.surface;
    m[QStringLiteral("text")]         = t.text;
    m[QStringLiteral("text_muted")]   = t.text_muted;
    m[QStringLiteral("text_heading")] = t.text_heading;
    m[QStringLiteral("border")]       = t.border;
    m[QStringLiteral("line")]         = t.line;
    m[QStringLiteral("accent")]       = t.accent;
    m[QStringLiteral("button")]       = t.button;
    m[QStringLiteral("button_text")]  = t.button_text;
    m[QStringLiteral("led_on")]       = t.led_on;
    m[QStringLiteral("led_off")]      = t.led_off;
    m[QStringLiteral("led_border")]   = t.led_border;
    m[QStringLiteral("success")]      = t.success;
    m[QStringLiteral("warning")]      = t.warning;
    m[QStringLiteral("error")]        = t.error;
    return m;
}

DeviceController::DeviceController(QObject* parent)
    : QObject(parent)
{
    m_styles[QStringLiteral("default")] = StyleToken{};
    m_activeStyleMap = buildStyleVariantMap(m_styles[QStringLiteral("default")]);
    initCache();
    m_watchdog.setSingleShot(true);
    m_watchdog.setInterval(kWatchdogMs);
    connect(&m_watchdog, &QTimer::timeout, this, [this]() {
        setError(QStringLiteral("Heartbeat timeout — device not responding"));
        teardown();
        m_model.clear();
    });

    m_debounce.setSingleShot(true);
    connect(&m_debounce, &QTimer::timeout,
            this, &DeviceController::reloadDesignFile);
    connect(&m_watcher, &QFileSystemWatcher::fileChanged,
            this, &DeviceController::onFileChanged);
}

DeviceController::~DeviceController()
{
    teardown();
}

/* ── Cache (SQLite) ─────────────────────────────────────────────────────── */

void DeviceController::initCache()
{
    m_db = QSqlDatabase::addDatabase(
        QStringLiteral("QSQLITE"),
        QStringLiteral("udisplay_cache"));
    QString path = QStandardPaths::writableLocation(
        QStandardPaths::AppDataLocation);
    QDir().mkpath(path);
    m_db.setDatabaseName(path + QStringLiteral("/blob_cache.sqlite"));
    if (!m_db.open()) {
        qWarning("DeviceController: blob cache unavailable: %s",
                 qPrintable(m_db.lastError().text()));
        return;
    }
    QSqlQuery q(m_db);
    if (!q.exec(QStringLiteral(
            "CREATE TABLE IF NOT EXISTS blobs "
            "(root BLOB PRIMARY KEY, compressed BLOB NOT NULL)")))
        qWarning("DeviceController: failed to create cache table: %s",
                 qPrintable(q.lastError().text()));
}

QByteArray DeviceController::cachedBlob(const QByteArray& root) const
{
    if (!m_db.isOpen()) return {};
    QSqlQuery q(m_db);
    q.prepare(QStringLiteral("SELECT compressed FROM blobs WHERE root = ?"));
    q.addBindValue(root);
    if (q.exec() && q.next())
        return q.value(0).toByteArray();
    return {};
}

void DeviceController::storeBlob(const QByteArray& root,
                                 const QByteArray& blob)
{
    if (!m_db.isOpen()) return;
    QSqlQuery q(m_db);
    q.prepare(QStringLiteral(
        "INSERT OR REPLACE INTO blobs (root, compressed) VALUES (?, ?)"));
    q.addBindValue(root);
    q.addBindValue(blob);
    if (!q.exec())
        qWarning("DeviceController: storeBlob failed: %s",
                 qPrintable(q.lastError().text()));
}

/* ── Blob decompression (zlib) ──────────────────────────────────────────── */

QByteArray DeviceController::decompressBlob(const QByteArray& compressed)
{
    /* Protocol invariant: YAML blobs are small (<64 KB compressed).
     * Reject anything larger to prevent int overflow in the size math below. */
    static constexpr int kMaxCompressedBytes = 256 * 1024; /* 256 KB hard cap */
    if (compressed.size() > kMaxCompressedBytes) return {};

    /* Restart inflate from scratch each time the output buffer is too small.
     * YAML blobs decompress to at most ~4× their compressed size. */
    int outSize = qMax(compressed.size() * 4, 1024);

    for (int attempt = 0; attempt < 8; ++attempt) {
        QByteArray out(outSize, '\0');

        z_stream zs{};
        zs.next_in   = reinterpret_cast<Bytef*>(
            const_cast<char*>(compressed.constData()));
        zs.avail_in  = static_cast<uInt>(compressed.size());
        zs.next_out  = reinterpret_cast<Bytef*>(out.data());
        zs.avail_out = static_cast<uInt>(outSize);

        if (inflateInit(&zs) != Z_OK) return {};
        int ret = inflate(&zs, Z_FINISH);
        uLong produced = zs.total_out;
        inflateEnd(&zs);

        if (ret == Z_STREAM_END) {
            if (produced > static_cast<uLong>(outSize)) return {}; /* sanity */
            out.resize(static_cast<int>(produced));
            return out;
        }
        if (ret != Z_BUF_ERROR && ret != Z_OK) return {}; /* real error */
        outSize *= 2;
        static constexpr int kMaxDecompressedBytes = 1024 * 1024; /* 1 MB cap */
        if (outSize > kMaxDecompressedBytes) return {};  /* reject runaway blobs */
    }
    return {};
}

/* ── Connect / disconnect ───────────────────────────────────────────────── */

void DeviceController::startConnection(Transport* t)
{
    m_bootstrap = new BootstrapManager(t, this);

    m_bootstrap->setBlobLookup([this](const QByteArray& root) {
        return cachedBlob(root);
    });

    connect(m_bootstrap, &BootstrapManager::succeeded,
            this, &DeviceController::onBootstrapSucceeded);
    connect(m_bootstrap, &BootstrapManager::failed,
            this, &DeviceController::onBootstrapFailed);
    connect(m_bootstrap, &BootstrapManager::progressChanged,
            this, [this](int pct) { Q_UNUSED(pct) });
    connect(m_bootstrap, &BootstrapManager::stateUpdateData,
            this, &DeviceController::onStateUpdateData);
    connect(m_bootstrap, &BootstrapManager::propertyCommand,
            this, &DeviceController::onPropertyCommand);
    connect(m_bootstrap, &BootstrapManager::heartbeat,
            this, &DeviceController::onHeartbeat);
    connect(t, &Transport::connected,
            this, [this]() {
                setState(QStringLiteral("bootstrapping"));
            });

    m_bootstrap->start();
}

void DeviceController::connectTcp(const QString& host, quint16 port)
{
    teardown();
    setState(QStringLiteral("connecting"));
    auto* tcp = new TcpTransport(host, port, this);
    m_transport = tcp;
    startConnection(tcp);
}

void DeviceController::connectDiscovered(const QVariant& info)
{
    DeviceInfo di = info.value<DeviceInfo>();
    if (di.type == DeviceInfo::Ble) {
#ifdef HAVE_BLE
        teardown();
        setState(QStringLiteral("connecting"));
        QBluetoothDeviceInfo btInfo = di.nativeHandle.value<QBluetoothDeviceInfo>();
        auto* ble = new BleTransport(btInfo, this);
        m_transport = ble;
        startConnection(ble);
#else
        setError(QStringLiteral("BLE not available in this build"));
#endif
    } else {
        connectTcp(di.address, static_cast<quint16>(di.port));
    }
}

void DeviceController::connectDevice(int transportType,
                                     const QString& address,
                                     quint16 port)
{
    if (m_state != QStringLiteral("disconnected") && m_state != QStringLiteral("error"))
        return;
    if (transportType == 1) {
        setError(QStringLiteral("BLE connections not supported via connectDevice — use connectDiscovered()"));
        return;
    }
    if (transportType != 0) {
        setError(QStringLiteral("Unknown transport type %1").arg(transportType));
        return;
    }
    connectTcp(address, port);
}

void DeviceController::disconnectDevice()
{
    teardown();
    m_model.clear();
    setState(QStringLiteral("disconnected"));
}

void DeviceController::teardown()
{
    m_watchdog.stop();
    if (m_bootstrap) {
        m_bootstrap->deleteLater();
        m_bootstrap = nullptr;
    }
    if (m_transport) {
        m_transport->disconnectFromDevice();
        m_transport->deleteLater();
        m_transport = nullptr;
    }
}

/* ── Design Mode ────────────────────────────────────────────────────────── */

void DeviceController::startDesignMode(const QString& filePath)
{
    m_designMode = true;
    m_designFilePath = filePath;

    /* Do an initial load. State becomes "running" whether the parse succeeds
     * or fails — the file watcher is active and the developer can fix the YAML. */
    reloadDesignFile();
    setState(QStringLiteral("running"));

    m_watcher.addPath(filePath);
}

void DeviceController::onFileChanged(const QString& path)
{
    /* Re-add the path unconditionally.
     * Atomic-rename editors (vim, vscode, neovim) write to a temp file and
     * rename it into place. Qt's QFileSystemWatcher sees the old path as gone
     * and silently removes it from the watch list. Without this re-add, watching
     * stops after the first save from any such editor. */
    m_watcher.addPath(path);

    /* Debounce: some editors flush in two writes. Restart the 150ms timer on
     * each signal so we only re-parse once the file stops changing. */
    m_debounce.start(150);
}

void DeviceController::reloadDesignFile()
{
    QFile f(m_designFilePath);
    if (!f.open(QIODevice::ReadOnly)) {
        setDesignError(QStringLiteral("Cannot open file: %1").arg(m_designFilePath));
        return;
    }
    const QByteArray raw = f.readAll();
    f.close();

    if (!applyParsedYaml(raw))
        setDesignError(m_yamlParser.errorString());
    else
        setDesignError(QString());
}

void DeviceController::setDesignError(const QString& msg)
{
    if (m_designErrorString != msg) {
        m_designErrorString = msg;
        emit designErrorStringChanged();
    }
}

void DeviceController::setActiveStyle(const QString& name)
{
    if (!m_styles.contains(name)) return;
    m_activeStyleName = name;
    m_activeStyleMap  = buildStyleVariantMap(m_styles[name]);
    emit activeStyleChanged();
}

/* ── Shared YAML → model helper ─────────────────────────────────────────── */

bool DeviceController::applyParsedYaml(const QByteArray& raw)
{
    QList<WidgetDef> widgets;
    QString name, version;
    QStringList caps;
    QMap<QString, StyleToken> styles;
    if (!m_yamlParser.parse(raw, widgets, name, version, caps, styles)) {
        printDebugFailure(m_yamlParser.errorString());
        return false;
    }

    const QList<YamlParser::ParseDiagnostic>& diags = m_yamlParser.diagnostics();
    if (!diags.isEmpty())
        emit parseWarningsChanged(diags);

    if (m_deviceName != name) {
        m_deviceName = name;
        emit deviceNameChanged();
    }

    bool stylesChanged = (m_styles.keys() != styles.keys());
    m_styles = styles;
    if (stylesChanged)
        emit availableStylesChanged();

    /* Reset to "default" on each new YAML load; keep named style if still present. */
    QString newActive = m_styles.contains(m_activeStyleName)
                        ? m_activeStyleName
                        : QStringLiteral("default");
    m_activeStyleName = newActive;
    m_activeStyleMap  = buildStyleVariantMap(m_styles.value(newActive));
    emit activeStyleChanged();

    m_model.setWidgets(widgets);

    /* Inject debug_state preview values in design mode only.
     * applyParsedYaml is never called from onBootstrapSucceeded, so this
     * block cannot execute when connected to a real device. The guard is
     * explicit for safety in case of future refactors. */
    if (m_designMode) {
        std::function<void(const QList<WidgetDef>&)> injectDebugValues =
            [&](const QList<WidgetDef>& list) {
                for (const WidgetDef& w : list) {
                    if (!w.debugValue.isNull())
                        m_model.setValue(w.widgetId, w.debugValue);
                    if (!w.children.isEmpty())
                        injectDebugValues(w.children);
                }
            };
        injectDebugValues(widgets);
    }

    printDebugDump(widgets, name, version, m_activeStyleName);
    return true;
}

void DeviceController::printDebugDump(const QList<WidgetDef>& widgets, const QString& name,
                                       const QString& version, const QString& activeStyleName) const
{
    if (!m_debugMode) return;
    qInfo().noquote() << dumpWidgetTree(widgets, name, version, activeStyleName);
}

void DeviceController::printDebugFailure(const QString& reason) const
{
    if (!m_debugMode) return;
    qInfo().noquote() << QStringLiteral("[debug] parse failed: %1").arg(reason);
}

/* ── Bootstrap callbacks ────────────────────────────────────────────────── */

/* Known capabilities this client version supports. */
static const QStringList kKnownCapabilities = {
    // QStringLiteral("exampe_cap1"),
    // QStringLiteral("exampe_cap2"),
};


void DeviceController::onBootstrapSucceeded(QByteArray merkleRoot,
                                            QByteArray compressedBlob)
{
    QByteArray yaml = decompressBlob(compressedBlob);
    if (yaml.isEmpty()) {
        setError(QStringLiteral("Failed to decompress YAML blob"));
        printDebugFailure(QStringLiteral("failed to decompress YAML blob"));
        return;
    }

    QList<WidgetDef> widgets;
    QString name, version;
    QStringList capabilities;
    QMap<QString, StyleToken> styles;
    if (!m_yamlParser.parse(yaml, widgets, name, version, capabilities, styles)) {
        setError(QStringLiteral("YAML parse failed: %1")
                 .arg(m_yamlParser.errorString()));
        printDebugFailure(m_yamlParser.errorString());
        return;
    }

    for (const QString& cap : capabilities) {
        if (!kKnownCapabilities.contains(cap)) {
            const QString reason = QStringLiteral(
                "Device requires app update — unknown capability: %1").arg(cap);
            setError(reason);
            /* Model is fully resolved at this point — only the compatibility
             * check failed. Dump it (--debug) alongside the rejection reason,
             * since this is the one failure branch where the resolved tree
             * is actually available and worth seeing. */
            printDebugDump(widgets, name, version, QStringLiteral("default"));
            printDebugFailure(reason);
            teardown();
            return;
        }
    }

    if (m_deviceName != name) {
        m_deviceName = name;
        emit deviceNameChanged();
    }

    bool stylesChanged = (m_styles.keys() != styles.keys());
    m_styles = styles;
    if (stylesChanged)
        emit availableStylesChanged();

    m_activeStyleName = QStringLiteral("default");
    m_activeStyleMap  = buildStyleVariantMap(m_styles.value(m_activeStyleName));
    emit activeStyleChanged();

    m_model.setWidgets(widgets);
    printDebugDump(widgets, name, version, m_activeStyleName);

    const QList<YamlParser::ParseDiagnostic>& diags = m_yamlParser.diagnostics();
    for (const auto& d : diags) {
        qWarning("[YamlParser %s] %s.%s: %s",
                 d.severity == YamlParser::Severity::Warning ? "WARNING" : "ERROR",
                 qPrintable(d.widgetKey),
                 qPrintable(d.field),
                 qPrintable(d.message));
    }
    if (!diags.isEmpty())
        emit parseWarningsChanged(diags);

    storeBlob(merkleRoot, compressedBlob);
    setState(QStringLiteral("running"));
    m_watchdog.start();
}

void DeviceController::onBootstrapFailed(QString reason)
{
    setError(reason);
    teardown();
}

/* ── Running-phase events ───────────────────────────────────────────────── */

void DeviceController::onStateUpdateData(Proto::StateUpdateData update)
{
    QVariant val;
    switch (update.valueType) {
    case Proto::VAL_FLOAT32: val = static_cast<double>(update.f32Value); break;
    case Proto::VAL_INT32:   val = static_cast<int>(update.i32Value);    break;
    case Proto::VAL_UINT8:   val = static_cast<int>(update.u8Value);     break;
    case Proto::VAL_STRING:  val = update.strValue;                      break;
    default: return;
    }
    m_model.setValue(update.widgetId, val);
}

void DeviceController::onPropertyCommand(Proto::PropertyCommand cmd)
{
    if (cmd.isSet)
        m_model.setProperty(cmd.targetId, cmd.propertyId, cmd.value);
    else
        m_model.resetProperty(cmd.targetId, cmd.propertyId);
}

void DeviceController::onHeartbeat()
{
    m_watchdog.start();
    if (m_transport && m_transport->isConnected())
        m_transport->send(Proto::encodeHeartbeat());
}

/* ── QML invokables ─────────────────────────────────────────────────────── */

void DeviceController::sendButtonPress(int widgetId)
{
    if (widgetId < 0x10 || widgetId > 0xFF) return;
    if (m_transport && m_transport->isConnected())
        m_transport->send(Proto::encodeEventButtonPress(
            static_cast<uint8_t>(widgetId)));
}

void DeviceController::sendButtonRelease(int widgetId)
{
    if (widgetId < 0x10 || widgetId > 0xFF) return;
    if (m_transport && m_transport->isConnected())
        m_transport->send(Proto::encodeEventButtonRelease(
            static_cast<uint8_t>(widgetId)));
}

void DeviceController::sendButtonClick(int widgetId)
{
    if (widgetId < 0x10 || widgetId > 0xFF) return;
    if (m_transport && m_transport->isConnected())
        m_transport->send(Proto::encodeEventButtonClick(
            static_cast<uint8_t>(widgetId)));
}

void DeviceController::sendSliderChange(int widgetId, double value)
{
    if (widgetId < 0x10 || widgetId > 0xFF) return;
    if (m_transport && m_transport->isConnected())
        m_transport->send(Proto::encodeEventSliderChange(
            static_cast<uint8_t>(widgetId),
            static_cast<float>(value)));
}

void DeviceController::sendToggleChange(int widgetId, bool state)
{
    if (widgetId < 0x10 || widgetId > 0xFF) return;
    if (m_transport && m_transport->isConnected())
        m_transport->send(Proto::encodeEventToggleChange(
            static_cast<uint8_t>(widgetId),
            state ? 1u : 0u));
}

void DeviceController::sendTextSubmit(int widgetId, const QString& text)
{
    if (widgetId < 0x10 || widgetId > 0xFF) return;
    if (m_transport && m_transport->isConnected())
        m_transport->send(Proto::encodeEventTextSubmit(
            static_cast<uint8_t>(widgetId), text));
}

void DeviceController::sendSelectionChange(int widgetId, int index)
{
    if (widgetId < 0x10 || widgetId > 0xFF) return;
    if (index < 0 || index > 0xFF) return;
    if (m_transport && m_transport->isConnected())
        m_transport->send(Proto::encodeEventSelectionChange(
            static_cast<uint8_t>(widgetId),
            static_cast<uint8_t>(index)));
}

/* ── Private helpers ────────────────────────────────────────────────────── */

void DeviceController::setState(const QString& s)
{
    if (m_state != s) {
        m_state = s;
        if (s != QStringLiteral("error"))
            m_errorString.clear();
        emit stateChanged();
    }
}

void DeviceController::setError(const QString& msg)
{
    m_errorString = msg;
    m_state = QStringLiteral("error");
    emit errorStringChanged();
    emit stateChanged();
}
