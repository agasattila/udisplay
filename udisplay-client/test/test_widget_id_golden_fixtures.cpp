/**
 * Golden-fixture cross-check: YamlParser's widget-ID assignment (this file)
 * vs. udisplay-gen's widget_ids.assign() (see udisplay-gen/tests/
 * test_vectors.py's TestWidgetIdGoldenFixtures). Both are two fully
 * independent re-derivations of the same ID-assignment algorithm; both are
 * checked here against the SAME fixtures in tests/protocol_vectors.json's
 * widget_id_fixtures — an actual automated cross-check, not just matching
 * code comments (see the dpad-split design doc's golden-fixture task).
 *
 * UDISPLAY_PROTOCOL_VECTORS_JSON is injected by CMake as the absolute path
 * to tests/protocol_vectors.json (repo root), so this test works regardless
 * of the CTest working directory.
 */
#include <QtTest>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>
#include <QFile>
#include <QMap>
#include "YamlParser.h"
#include "WidgetDef.h"

#ifndef UDISPLAY_PROTOCOL_VECTORS_JSON
#error "UDISPLAY_PROTOCOL_VECTORS_JSON must be defined by CMake"
#endif

namespace {

void collectIds(const QList<WidgetDef>& widgets, QMap<QString, uint8_t>& out)
{
    for (const auto& w : widgets) {
        if (w.widgetId != 0)
            out[w.keyPath] = w.widgetId;
        collectIds(w.children, out);
        if (w.type == WidgetType::ButtonGroup) {
            for (const auto& item : w.groupItems)
                out[item.keyPath] = item.widgetId;
        }
    }
}

} // namespace

class TestWidgetIdGoldenFixtures : public QObject
{
    Q_OBJECT

private slots:

    void fixtures_match_golden_ids()
    {
        QFile f(QStringLiteral(UDISPLAY_PROTOCOL_VECTORS_JSON));
        QVERIFY2(f.open(QIODevice::ReadOnly), qPrintable(f.errorString()));
        QJsonParseError err;
        QJsonDocument doc = QJsonDocument::fromJson(f.readAll(), &err);
        QVERIFY2(err.error == QJsonParseError::NoError, qPrintable(err.errorString()));

        QJsonObject fixtures = doc.object().value(QStringLiteral("widget_id_fixtures")).toObject();
        QVERIFY2(!fixtures.isEmpty(), "widget_id_fixtures must not be empty");

        for (auto it = fixtures.begin(); it != fixtures.end(); ++it) {
            const QString fixtureName = it.key();
            QJsonObject fixture = it.value().toObject();
            QByteArray yamlBytes = fixture.value(QStringLiteral("yaml")).toString().toUtf8();
            QJsonObject expectedIds = fixture.value(QStringLiteral("widget_ids")).toObject();

            YamlParser p;
            QList<WidgetDef> widgets;
            QString name, version;
            QVERIFY2(p.parse(yamlBytes, widgets, name, version),
                     qPrintable(fixtureName + ": " + p.errorString()));

            QMap<QString, uint8_t> actual;
            collectIds(widgets, actual);

            QCOMPARE(actual.size(), expectedIds.size());
            for (auto eit = expectedIds.begin(); eit != expectedIds.end(); ++eit) {
                const QString path = eit.key();
                bool ok = false;
                uint8_t expectedId = static_cast<uint8_t>(eit.value().toString().toUInt(&ok, 16));
                QVERIFY2(ok, qPrintable(fixtureName + "." + path + ": bad hex in fixture"));
                QVERIFY2(actual.contains(path),
                         qPrintable(fixtureName + ": missing path '" + path + "'"));
                QCOMPARE(actual.value(path), expectedId);
            }
        }
    }
};

QTEST_MAIN(TestWidgetIdGoldenFixtures)
#include "test_widget_id_golden_fixtures.moc"
