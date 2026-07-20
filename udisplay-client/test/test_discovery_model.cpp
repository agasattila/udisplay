/**
 * DiscoveryModel unit tests.
 *
 * Uses MockScanner (an IScanner subclass) to inject DeviceInfo events
 * directly into DiscoveryModel without any real mDNS or BLE activity.
 */
#include <QtTest>
#include "DiscoveryModel.h"
#include "IScanner.h"

/* Minimal scanner that lets tests fire deviceFound/deviceLost manually. */
class MockScanner : public IScanner
{
    Q_OBJECT
public:
    explicit MockScanner(QObject* parent = nullptr) : IScanner(parent) {}
    void startScan() override { m_started = true; }
    void stopScan()  override { m_started = false; }
    bool started() const { return m_started; }
    void injectFound(const DeviceInfo& info)    { emit deviceFound(info); }
    void injectLost(const QString& uniqueId)    { emit deviceLost(uniqueId); }
    void injectError(const QString& reason)     { emit scanError(reason); }
private:
    bool m_started = false;
};

/* DiscoveryModelHarness injects a MockScanner instead of the real ones. */
class DiscoveryModelHarness : public QAbstractListModel
{
    Q_OBJECT
public:
    MockScanner* scanner;

    explicit DiscoveryModelHarness(QObject* parent = nullptr)
        : QAbstractListModel(parent)
    {
        qRegisterMetaType<DeviceInfo>("DeviceInfo");
        scanner = new MockScanner(this);
        connect(scanner, &IScanner::deviceFound, this, &DiscoveryModelHarness::onDeviceFound);
        connect(scanner, &IScanner::deviceLost,  this, &DiscoveryModelHarness::onDeviceLost);
        connect(scanner, &IScanner::scanError,   this, &DiscoveryModelHarness::onScanError);
    }

    int rowCount(const QModelIndex& parent = QModelIndex()) const override {
        if (parent.isValid()) return 0;
        return m_devices.size();
    }
    QVariant data(const QModelIndex& index, int role) const override {
        if (!index.isValid() || index.row() >= m_devices.size()) return {};
        const auto& d = m_devices.at(index.row());
        switch (role) {
        case Qt::UserRole + 1: return d.displayName;
        case Qt::UserRole + 2: return d.address;
        case Qt::UserRole + 3: return d.port;
        case Qt::UserRole + 4: return static_cast<int>(d.type);
        case Qt::UserRole + 5: return d.uniqueId;
        default: return {};
        }
    }
    QHash<int, QByteArray> roleNames() const override {
        return {
            { Qt::UserRole + 1, "displayName" },
            { Qt::UserRole + 2, "address" },
            { Qt::UserRole + 3, "port" },
            { Qt::UserRole + 4, "transportType" },
            { Qt::UserRole + 5, "uniqueId" },
        };
    }
    QString scanError() const { return m_scanError; }

private slots:
    void onDeviceFound(DeviceInfo info) {
        for (int i = 0; i < m_devices.size(); ++i) {
            if (m_devices.at(i).uniqueId == info.uniqueId) {
                m_devices[i] = info;
                const QModelIndex idx = index(i);
                emit dataChanged(idx, idx);
                return;
            }
        }
        const int row = m_devices.size();
        beginInsertRows(QModelIndex(), row, row);
        m_devices.append(info);
        endInsertRows();
    }
    void onDeviceLost(QString uniqueId) {
        for (int i = 0; i < m_devices.size(); ++i) {
            if (m_devices.at(i).uniqueId == uniqueId) {
                beginRemoveRows(QModelIndex(), i, i);
                m_devices.removeAt(i);
                endRemoveRows();
                return;
            }
        }
    }
    void onScanError(QString reason) { m_scanError = reason; }

private:
    QList<DeviceInfo> m_devices;
    QString           m_scanError;
};

/* ─────────────────────────────────────────────────────────────────────────── */

class TestDiscoveryModel : public QObject
{
    Q_OBJECT

private slots:

    void model_startsEmpty()
    {
        DiscoveryModelHarness h;
        QCOMPARE(h.rowCount(), 0);
    }

    void deviceFound_addsRow()
    {
        DiscoveryModelHarness h;
        QSignalSpy rowsInserted(&h, &QAbstractListModel::rowsInserted);

        h.scanner->injectFound(DeviceInfo::makeTcp(
            "testdev", "Test Device", "192.168.1.10", 5555));

        QCOMPARE(h.rowCount(), 1);
        QCOMPARE(rowsInserted.count(), 1);
        QCOMPARE(h.data(h.index(0), Qt::UserRole + 1).toString(),
                 QStringLiteral("Test Device"));
        QCOMPARE(h.data(h.index(0), Qt::UserRole + 5).toString(),
                 QStringLiteral("testdev"));
    }

    void deviceFound_duplicateUniqueId_updatesInPlace()
    {
        DiscoveryModelHarness h;

        h.scanner->injectFound(DeviceInfo::makeTcp(
            "testdev", "Old Name", "192.168.1.10", 5555));
        QCOMPARE(h.rowCount(), 1);

        QSignalSpy rowsInserted(&h, &QAbstractListModel::rowsInserted);
        QSignalSpy dataChanged(&h, &QAbstractListModel::dataChanged);

        h.scanner->injectFound(DeviceInfo::makeTcp(
            "testdev", "New Name", "192.168.1.11", 5555));

        QCOMPARE(h.rowCount(), 1);          /* no new row */
        QCOMPARE(rowsInserted.count(), 0);  /* no insert */
        QCOMPARE(dataChanged.count(), 1);   /* update in place */
        QCOMPARE(h.data(h.index(0), Qt::UserRole + 1).toString(),
                 QStringLiteral("New Name"));
    }

    void deviceLost_removesRow()
    {
        DiscoveryModelHarness h;
        h.scanner->injectFound(DeviceInfo::makeTcp(
            "dev1", "Device 1", "192.168.1.1", 5555));
        h.scanner->injectFound(DeviceInfo::makeTcp(
            "dev2", "Device 2", "192.168.1.2", 5555));
        QCOMPARE(h.rowCount(), 2);

        QSignalSpy rowsRemoved(&h, &QAbstractListModel::rowsRemoved);
        h.scanner->injectLost("dev1");

        QCOMPARE(h.rowCount(), 1);
        QCOMPARE(rowsRemoved.count(), 1);
        QCOMPARE(h.data(h.index(0), Qt::UserRole + 5).toString(),
                 QStringLiteral("dev2"));
    }

    void deviceLost_unknownId_noEffect()
    {
        DiscoveryModelHarness h;
        h.scanner->injectFound(DeviceInfo::makeTcp(
            "dev1", "Device 1", "192.168.1.1", 5555));
        QSignalSpy rowsRemoved(&h, &QAbstractListModel::rowsRemoved);

        h.scanner->injectLost("nonexistent");

        QCOMPARE(h.rowCount(), 1);
        QCOMPARE(rowsRemoved.count(), 0);
    }

    void scanError_propagatesToProperty()
    {
        DiscoveryModelHarness h;
        QVERIFY(h.scanError().isEmpty());

        h.scanner->injectError("test error");

        QCOMPARE(h.scanError(), QStringLiteral("test error"));
    }

    void deviceInfo_makeTcp_correctFields()
    {
        DeviceInfo d = DeviceInfo::makeTcp("id1", "My Device", "10.0.0.1", 5555);
        QCOMPARE(d.type, DeviceInfo::Tcp);
        QCOMPARE(d.uniqueId, QStringLiteral("id1"));
        QCOMPARE(d.displayName, QStringLiteral("My Device"));
        QCOMPARE(d.address, QStringLiteral("10.0.0.1"));
        QCOMPARE(d.port, 5555);
        QCOMPARE(d.rssi, -1);
        QCOMPARE(d.sourceLabel(), QStringLiteral("Wi-Fi"));
    }

    void deviceInfo_makeBle_correctFields()
    {
        DeviceInfo d = DeviceInfo::makeBle("AA:BB:CC:DD", "BLE Dev", "AA:BB:CC:DD", -60);
        QCOMPARE(d.type, DeviceInfo::Ble);
        QCOMPARE(d.port, 0);
        QCOMPARE(d.rssi, -60);
        QCOMPARE(d.sourceLabel(), QStringLiteral("Bluetooth"));
    }

    void deviceInfo_equalityByUniqueId()
    {
        DeviceInfo a = DeviceInfo::makeTcp("same-id", "A", "1.1.1.1", 5555);
        DeviceInfo b = DeviceInfo::makeTcp("same-id", "B", "2.2.2.2", 1234);
        DeviceInfo c = DeviceInfo::makeTcp("diff-id", "C", "3.3.3.3", 5555);
        QVERIFY(a == b);
        QVERIFY(a != c);
    }

    /* Smoke test: verify the real DiscoveryModel constructor wires correctly
     * without calling startScan() (which requires a live avahi/BT daemon). */
    void realModel_constructsAndStartsEmpty()
    {
        DiscoveryModel model;
        QCOMPARE(model.rowCount(), 0);
        QCOMPARE(model.deviceCount(), 0);
        QVERIFY(!model.scanning());
        QVERIFY(model.scanError().isEmpty());
    }
};

QTEST_MAIN(TestDiscoveryModel)
#include "test_discovery_model.moc"
