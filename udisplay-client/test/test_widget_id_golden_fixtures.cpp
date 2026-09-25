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
    /* widgets is a flat list now — every widget, any nesting depth
     * (including former button-group items and dpad items), is its own
     * entry, so no recursion is needed. widgetId 0 = no ID (containers and
     * decorations under the legacy leaf-only scheme only). */
    for (const auto& w : widgets) {
        if (w.widgetId != 0)
            out[w.keyPath] = w.widgetId;
    }
}

} // namespace

class TestWidgetIdGoldenFixtures : public QObject
{
    Q_OBJECT

    /* Parses every fixture in `fixtureSet` under `scheme` and compares the
     * full path -> id map. Under EveryWidget, additionally asserts that NO
     * row is left at widgetId 0 (issue #43: every widget is addressable). */
    void checkFixtureSet(const char* fixtureSet, YamlParser::IdScheme scheme)
    {
        QFile f(QStringLiteral(UDISPLAY_PROTOCOL_VECTORS_JSON));
        QVERIFY2(f.open(QIODevice::ReadOnly), qPrintable(f.errorString()));
        QJsonParseError err;
        QJsonDocument doc = QJsonDocument::fromJson(f.readAll(), &err);
        QVERIFY2(err.error == QJsonParseError::NoError, qPrintable(err.errorString()));

        QJsonObject fixtures = doc.object().value(QLatin1String(fixtureSet)).toObject();
        QVERIFY2(!fixtures.isEmpty(), fixtureSet);

        for (auto it = fixtures.begin(); it != fixtures.end(); ++it) {
            const QString fixtureName = it.key();
            QJsonObject fixture = it.value().toObject();
            QByteArray yamlBytes = fixture.value(QStringLiteral("yaml")).toString().toUtf8();
            QJsonObject expectedIds = fixture.value(QStringLiteral("widget_ids")).toObject();

            YamlParser p;
            p.setIdScheme(scheme);
            QList<WidgetDef> widgets;
            QString name, version;
            QVERIFY2(p.parse(yamlBytes, widgets, name, version),
                     qPrintable(fixtureName + ": " + p.errorString()));

            if (scheme == YamlParser::IdScheme::EveryWidget) {
                for (const auto& w : widgets)
                    QVERIFY2(w.widgetId != 0,
                             qPrintable(fixtureName + ": widget '" + w.keyPath + "' has no ID"));
            }

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

private slots:

    /* v5 scheme (PROTO_VERSION >= 0x05): must match udisplay-gen's assign()
     * on the same fixtures (udisplay-gen/tests/test_vectors.py). */
    void fixtures_match_golden_ids()
    {
        checkFixtureSet("widget_id_fixtures", YamlParser::IdScheme::EveryWidget);
    }

    /* Frozen pre-v5 scheme: old firmware headers were generated with it, so
     * the client must keep reproducing it byte-for-byte for devices that
     * report PROTO_VERSION < 0x05. */
    void legacy_fixtures_match_leaf_only_scheme()
    {
        checkFixtureSet("widget_id_fixtures_legacy", YamlParser::IdScheme::LeafOnly);
    }

    void scheme_selected_by_proto_version()
    {
        QCOMPARE(YamlParser::idSchemeForProtoVersion(0x03), YamlParser::IdScheme::LeafOnly);
        QCOMPARE(YamlParser::idSchemeForProtoVersion(0x04), YamlParser::IdScheme::LeafOnly);
        QCOMPARE(YamlParser::idSchemeForProtoVersion(0x05), YamlParser::IdScheme::EveryWidget);
        QCOMPARE(YamlParser::idSchemeForProtoVersion(0x06), YamlParser::IdScheme::EveryWidget);
    }
};

QTEST_MAIN(TestWidgetIdGoldenFixtures)
#include "test_widget_id_golden_fixtures.moc"
