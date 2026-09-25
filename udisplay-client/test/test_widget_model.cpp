/**
 * WidgetModel unit tests.
 *
 * Covers:
 *   - setValue/setProperty/resetProperty for a widget at ANY nesting depth —
 *     always targets that widget's OWN row now (WidgetModel is a flat list;
 *     see WidgetModel.h). This inverts the pre-flattening behavior, where a
 *     descendant mutation emitted PropsRole on its top-level ancestor
 *     instead — setValue_leafAtAnyDepth_emitsValueRoleOnOwnRow below is the
 *     regression-rule test for that inversion.
 *   - data() PropsRole for all typed widgets
 *   - childModel(parentId): row count, role passthrough, pointer stability
 *   - setWidgets populates m_idToRow correctly (single hash, every depth)
 *   - clear() resets all state
 *   - toggleSection via parentId-chain visibility
 */
#include <QtTest>
#include <QSet>
#include <QAbstractListModel>
#include "WidgetModel.h"
#include "WidgetDef.h"
#include "Protocol.h"

/* ── Fixture helpers ─────────────────────────────────────────────────────── */

/* Build a minimal toggle widget. */
static WidgetDef makeToggle(uint8_t id, const QString& key, int parentId = -1)
{
    WidgetDef w;
    w.keyPath  = key;
    w.widgetId = id;
    w.type     = WidgetType::Toggle;
    w.label    = QStringLiteral("Toggle");
    w.parentId = parentId;
    return w;
}

/* Flat list: [button, led]. */
static QList<WidgetDef> makeButtonWithLed(uint8_t btnId, uint8_t ledId)
{
    WidgetDef btn;
    btn.keyPath  = QStringLiteral("btn");
    btn.widgetId = btnId;
    btn.type     = WidgetType::Button;
    btn.label    = QStringLiteral("Fire");
    btn.props[QStringLiteral("shape")] = QStringLiteral("circle");

    WidgetDef led;
    led.keyPath  = QStringLiteral("btn.led");
    led.widgetId = ledId;
    led.type     = WidgetType::Led;
    led.label    = QStringLiteral("Active");
    led.parentId = 0;

    return { btn, led };
}

static WidgetDef makeDisplay(uint8_t id)
{
    WidgetDef w;
    w.keyPath  = QStringLiteral("disp");
    w.widgetId = id;
    w.type     = WidgetType::Display;
    w.label    = QStringLiteral("Voltage");
    w.props[QStringLiteral("unit")]   = QStringLiteral("V");
    w.props[QStringLiteral("format")] = QStringLiteral("%.3f");
    w.props[QStringLiteral("displayStyle")]  = QStringLiteral("large");
    return w;
}

static WidgetDef makeSlider(uint8_t id)
{
    WidgetDef w;
    w.keyPath  = QStringLiteral("rate");
    w.widgetId = id;
    w.type     = WidgetType::Slider;
    w.label    = QStringLiteral("Rate");
    w.props[QStringLiteral("min")]  = 1.0;
    w.props[QStringLiteral("max")]  = 100.0;
    w.props[QStringLiteral("step")] = 0.5;
    w.props[QStringLiteral("unit")] = QStringLiteral("Hz");
    return w;
}

static WidgetDef makeText(uint8_t id)
{
    WidgetDef w;
    w.keyPath  = QStringLiteral("ssid");
    w.widgetId = id;
    w.type     = WidgetType::Text;
    w.label    = QStringLiteral("SSID");
    w.props[QStringLiteral("mode")]        = QStringLiteral("rw");
    w.props[QStringLiteral("defaultMode")] = QStringLiteral("rw");
    w.props[QStringLiteral("placeholder")] = QStringLiteral("Enter SSID");
    w.props[QStringLiteral("maxlength")]   = 32;
    return w;
}

/* Flat list: [group, dc-item, ac-item]. */
static QList<WidgetDef> makeButtonGroup(uint8_t groupId, uint8_t dcId, uint8_t acId)
{
    WidgetDef w;
    w.keyPath  = QStringLiteral("mode");
    w.widgetId = groupId;
    w.type     = WidgetType::ButtonGroup;
    w.label    = QStringLiteral("Mode");
    w.props[QStringLiteral("layout")] = QStringLiteral("grid");

    WidgetDef dc;
    dc.keyPath  = QStringLiteral("mode.dc");
    dc.widgetId = dcId;
    dc.type     = WidgetType::Button;
    dc.label    = QStringLiteral("DCV");
    dc.parentId = 0;

    WidgetDef ac;
    ac.keyPath  = QStringLiteral("mode.ac");
    ac.widgetId = acId;
    ac.type     = WidgetType::Button;
    ac.label    = QStringLiteral("ACV");
    ac.parentId = 0;

    return { w, dc, ac };
}

/* [section 0x10] > [row 0x11] > [toggle 0x12]; plus sibling toggle 0x13
 * outside the section. Every widget, containers included, has an ID. */
static QList<WidgetDef> makeSectionRowToggle()
{
    WidgetDef sec;
    sec.keyPath  = QStringLiteral("sec");
    sec.widgetId = 0x10;
    sec.type     = WidgetType::Section;
    WidgetDef row;
    row.keyPath  = QStringLiteral("row");
    row.widgetId = 0x11;
    row.type     = WidgetType::Row;
    row.parentId = 0;
    return { sec, row, makeToggle(0x12, "inner", 1), makeToggle(0x13, "outer") };
}

/* Retrieve a role value from the model at the given row. */
static QVariant roleAt(WidgetModel& m, int row, int role)
{
    return m.data(m.index(row), role);
}

/* ─────────────────────────────────────────────────────────────────────────── */

class TestWidgetModel : public QObject
{
    Q_OBJECT

private slots:

    /* ── setWidgets ───────────────────────────────────────────────────── */

    void setWidgets_rowCount()
    {
        WidgetModel m;
        QCOMPARE(m.rowCount(), 0);
        m.setWidgets({ makeToggle(0x10, "a"), makeToggle(0x11, "b") });
        QCOMPARE(m.rowCount(), 2);
    }

    void setWidgets_declarationOrderPreserved()
    {
        WidgetModel m;
        m.setWidgets({ makeToggle(0x10, "first"), makeToggle(0x11, "second") });
        QCOMPARE(roleAt(m, 0, WidgetModel::WidgetIdRole).toInt(), 0x10);
        QCOMPARE(roleAt(m, 1, WidgetModel::WidgetIdRole).toInt(), 0x11);
    }

    void setWidgets_childIndex_populated()
    {
        /* After setWidgets, setValue on a child ID must route to ITS OWN
         * row (every widget, any depth, is a real row now — this is the
         * regression guard for the ButtonWidget LED bug, updated for the
         * flat model). */
        WidgetModel m;
        m.setWidgets(makeButtonWithLed(0x10, 0x11));

        QSignalSpy spy(&m, &WidgetModel::dataChanged);
        m.setValue(0x11, true); /* child LED ID, row 1 */

        QCOMPARE(spy.count(), 1);
        QCOMPARE(spy.at(0).at(0).value<QModelIndex>(), m.index(1));
        QVector<int> roles = spy.at(0).at(2).value<QVector<int>>();
        QVERIFY(roles.contains(WidgetModel::ValueRole));
    }

    void setWidgets_replaceClears()
    {
        /* Calling setWidgets a second time must replace, not accumulate. */
        WidgetModel m;
        m.setWidgets({ makeToggle(0x10, "a"), makeToggle(0x11, "b") });
        m.setWidgets({ makeToggle(0x12, "c") });
        QCOMPARE(m.rowCount(), 1);
        QCOMPARE(roleAt(m, 0, WidgetModel::WidgetIdRole).toInt(), 0x12);
    }

    /* ── clear ────────────────────────────────────────────────────────── */

    void clear_resetsModel()
    {
        WidgetModel m;
        m.setWidgets({ makeToggle(0x10, "a") });
        m.clear();
        QCOMPARE(m.rowCount(), 0);
    }

    void clear_childIndexAlsoClear()
    {
        /* After clear(), a subsequent setValue on the old child ID must be a no-op. */
        WidgetModel m;
        m.setWidgets(makeButtonWithLed(0x10, 0x11));
        m.clear();

        QSignalSpy spy(&m, &WidgetModel::dataChanged);
        m.setValue(0x11, true);
        QCOMPARE(spy.count(), 0); /* no-op: id→row index cleared */
    }

    /* ── setValue — top-level ─────────────────────────────────────────── */

    void setValue_topLevel_storesValue()
    {
        WidgetModel m;
        m.setWidgets({ makeToggle(0x10, "relay") });
        m.setValue(0x10, true);
        QCOMPARE(roleAt(m, 0, WidgetModel::ValueRole).toBool(), true);
    }

    void setValue_topLevel_emitsValueRole()
    {
        WidgetModel m;
        m.setWidgets({ makeToggle(0x10, "relay") });
        QSignalSpy spy(&m, &WidgetModel::dataChanged);
        m.setValue(0x10, 42);
        QCOMPARE(spy.count(), 1);
        QVector<int> roles = spy.at(0).at(2).value<QVector<int>>();
        QVERIFY(roles.contains(WidgetModel::ValueRole));
    }

    void setValue_unknownId_isNoop()
    {
        WidgetModel m;
        m.setWidgets({ makeToggle(0x10, "relay") });
        QSignalSpy spy(&m, &WidgetModel::dataChanged);
        m.setValue(0xFF, 1); /* unknown id */
        QCOMPARE(spy.count(), 0);
    }

    /* ── setValue — descendant (REGRESSION TEST) ──────────────────────── */

    void setValue_leafAtAnyDepth_emitsValueRoleOnOwnRow()
    {
        /* THE regression-rule test for this refactor: a widget nested at
         * any depth, when its value changes, must emit dataChanged with
         * ValueRole targeted at ITS OWN row — not PropsRole on some
         * ancestor. Before the flattening, this exact scenario
         * (setValue on a button's LED child) emitted PropsRole on the
         * parent row and explicitly did NOT emit ValueRole at all. A
         * regression back to that whole-branch-rebuild behavior must fail
         * this test. */
        WidgetModel m;
        m.setWidgets(makeButtonWithLed(0x10, 0x11));
        QSignalSpy spy(&m, &WidgetModel::dataChanged);
        m.setValue(0x11, true);

        QCOMPARE(spy.count(), 1);
        QCOMPARE(spy.at(0).at(0).value<QModelIndex>(), m.index(1));
        QVector<int> roles = spy.at(0).at(2).value<QVector<int>>();
        QVERIFY(roles.contains(WidgetModel::ValueRole));
        QCOMPARE(roleAt(m, 1, WidgetModel::ValueRole).toBool(), true);

        /* Parent row (0) must NOT have been touched at all. */
        QCOMPARE(spy.count(), 1); /* only the one signal, for row 1 */
    }

    /* ── setProperty ──────────────────────────────────────────────────── */

    void setProperty_enabled_false()
    {
        WidgetModel m;
        m.setWidgets({ makeToggle(0x10, "relay") });
        QVERIFY(roleAt(m, 0, WidgetModel::EnabledRole).toBool()); /* default true */
        m.setProperty(0x10, Proto::PROP_ENABLED, 0);
        QVERIFY(!roleAt(m, 0, WidgetModel::EnabledRole).toBool());
    }

    void setProperty_enabled_emitsEnabledRole()
    {
        WidgetModel m;
        m.setWidgets({ makeToggle(0x10, "relay") });
        QSignalSpy spy(&m, &WidgetModel::dataChanged);
        m.setProperty(0x10, Proto::PROP_ENABLED, 0);
        QCOMPARE(spy.count(), 1);
        QVector<int> roles = spy.at(0).at(2).value<QVector<int>>();
        QVERIFY(roles.contains(WidgetModel::EnabledRole));
    }

    void setProperty_enabled_noChangeNoSignal()
    {
        /* Setting enabled=true when it's already true must not emit. */
        WidgetModel m;
        m.setWidgets({ makeToggle(0x10, "relay") });
        QSignalSpy spy(&m, &WidgetModel::dataChanged);
        m.setProperty(0x10, Proto::PROP_ENABLED, 1); /* already true */
        QCOMPARE(spy.count(), 0);
    }

    void setProperty_visible_false()
    {
        WidgetModel m;
        m.setWidgets({ makeToggle(0x10, "relay") });
        m.setProperty(0x10, Proto::PROP_VISIBLE, 0);
        QVERIFY(!roleAt(m, 0, WidgetModel::VisibleRole).toBool());
    }

    void setProperty_textMode_readonlyToRw()
    {
        WidgetModel m;
        m.setWidgets({ makeText(0x10) });
        /* mode starts as "rw" from makeText; set to readonly (0) */
        m.setProperty(0x10, Proto::PROP_MODE, 0);
        QVariantMap props = roleAt(m, 0, WidgetModel::PropsRole).toMap();
        QCOMPARE(props[QStringLiteral("mode")].toString(),
                 QStringLiteral("readonly"));
    }

    void setProperty_unknownId_isNoop()
    {
        WidgetModel m;
        m.setWidgets({ makeToggle(0x10, "relay") });
        QSignalSpy spy(&m, &WidgetModel::dataChanged);
        m.setProperty(0xFF, Proto::PROP_ENABLED, 0);
        QCOMPARE(spy.count(), 0);
    }

    /* ── resetProperty ────────────────────────────────────────────────── */

    void resetProperty_enabled_restoresTrue()
    {
        WidgetModel m;
        m.setWidgets({ makeToggle(0x10, "relay") });
        m.setProperty(0x10, Proto::PROP_ENABLED, 0);
        QVERIFY(!roleAt(m, 0, WidgetModel::EnabledRole).toBool());
        m.resetProperty(0x10, Proto::PROP_ENABLED);
        QVERIFY(roleAt(m, 0, WidgetModel::EnabledRole).toBool());
    }

    void resetProperty_visible_restoresTrue()
    {
        WidgetModel m;
        m.setWidgets({ makeToggle(0x10, "relay") });
        m.setProperty(0x10, Proto::PROP_VISIBLE, 0);
        m.resetProperty(0x10, Proto::PROP_VISIBLE);
        QVERIFY(roleAt(m, 0, WidgetModel::VisibleRole).toBool());
    }

    void resetProperty_alreadyTrue_noSignal()
    {
        WidgetModel m;
        m.setWidgets({ makeToggle(0x10, "relay") });
        QSignalSpy spy(&m, &WidgetModel::dataChanged);
        m.resetProperty(0x10, Proto::PROP_ENABLED); /* already true */
        QCOMPARE(spy.count(), 0);
    }

    /* ── resetProperty PROP_MODE restores YAML default ────────────────── */

    void resetProperty_textMode_restoresYamlDefault()
    {
        WidgetModel m;
        m.setWidgets({ makeText(0x10) }); /* defaultMode = "rw" */

        /* Device sets mode to readonly */
        m.setProperty(0x10, Proto::PROP_MODE, 0);
        QCOMPARE(roleAt(m, 0, WidgetModel::PropsRole).toMap()[QStringLiteral("mode")].toString(),
                 QStringLiteral("readonly"));

        /* Device resets mode — should restore YAML default "rw" */
        QSignalSpy spy(&m, &WidgetModel::dataChanged);
        m.resetProperty(0x10, Proto::PROP_MODE);

        QCOMPARE(roleAt(m, 0, WidgetModel::PropsRole).toMap()[QStringLiteral("mode")].toString(),
                 QStringLiteral("rw"));
        QCOMPARE(spy.count(), 1);
    }

    void resetProperty_textMode_alreadyDefault_noSignal()
    {
        WidgetModel m;
        m.setWidgets({ makeText(0x10) }); /* mode already == defaultMode */
        QSignalSpy spy(&m, &WidgetModel::dataChanged);
        m.resetProperty(0x10, Proto::PROP_MODE);
        QCOMPARE(spy.count(), 0);
    }

    /* ── data() PropsRole for all typed widgets ───────────────────────── */

    void data_propsRole_display()
    {
        WidgetModel m;
        m.setWidgets({ makeDisplay(0x10) });
        QVariantMap props = roleAt(m, 0, WidgetModel::PropsRole).toMap();
        QCOMPARE(props[QStringLiteral("unit")].toString(),   QStringLiteral("V"));
        QCOMPARE(props[QStringLiteral("format")].toString(), QStringLiteral("%.3f"));
        QCOMPARE(props[QStringLiteral("displayStyle")].toString(),  QStringLiteral("large"));
    }

    void data_propsRole_button()
    {
        /* Face children are no longer enumerated inside props — they are
         * ordinary flat rows of their own, reached via their parent's
         * widgetId (see WidgetModel::childModel()), not via a props["items"]
         * key. */
        WidgetModel m;
        m.setWidgets(makeButtonWithLed(0x10, 0x11));
        QVariantMap props = roleAt(m, 0, WidgetModel::PropsRole).toMap();
        QCOMPARE(props[QStringLiteral("shape")].toString(), QStringLiteral("circle"));
        QVERIFY(!props.contains(QStringLiteral("color")));
        QVERIFY(!props.contains(QStringLiteral("items")));

        QCOMPARE(roleAt(m, 1, WidgetModel::WidgetIdRole).toInt(), 0x11);
        QCOMPARE(roleAt(m, 1, WidgetModel::LabelRole).toString(), QStringLiteral("Active"));
        /* value starts as null (no setValue called yet) */
        QVERIFY(!roleAt(m, 1, WidgetModel::ValueRole).isValid());
    }

    void data_propsRole_buttonGroup()
    {
        WidgetModel m;
        m.setWidgets(makeButtonGroup(0x10, 0x11, 0x12));
        QVariantMap props = roleAt(m, 0, WidgetModel::PropsRole).toMap();
        QCOMPARE(props[QStringLiteral("layout")].toString(), QStringLiteral("grid"));
        QVERIFY(!props.contains(QStringLiteral("items")));

        /* Items are ordinary flat rows now, declaration order preserved. */
        QCOMPARE(roleAt(m, 1, WidgetModel::LabelRole).toString(), QStringLiteral("DCV"));
        QCOMPARE(roleAt(m, 2, WidgetModel::LabelRole).toString(), QStringLiteral("ACV"));
    }

    void data_propsRole_slider()
    {
        WidgetModel m;
        m.setWidgets({ makeSlider(0x10) });
        QVariantMap props = roleAt(m, 0, WidgetModel::PropsRole).toMap();
        QCOMPARE(props[QStringLiteral("min")].toDouble(),  1.0);
        QCOMPARE(props[QStringLiteral("max")].toDouble(),  100.0);
        QCOMPARE(props[QStringLiteral("step")].toDouble(), 0.5);
        QCOMPARE(props[QStringLiteral("unit")].toString(), QStringLiteral("Hz"));
    }

    void data_propsRole_text()
    {
        WidgetModel m;
        m.setWidgets({ makeText(0x10) });
        QVariantMap props = roleAt(m, 0, WidgetModel::PropsRole).toMap();
        QCOMPARE(props[QStringLiteral("mode")].toString(),
                 QStringLiteral("rw"));
        QCOMPARE(props[QStringLiteral("placeholder")].toString(),
                 QStringLiteral("Enter SSID"));
        QCOMPARE(props[QStringLiteral("maxlength")].toInt(), 32);
    }

    /* ── data() common roles ──────────────────────────────────────────── */

    void data_labelRole()
    {
        WidgetModel m;
        m.setWidgets({ makeDisplay(0x10) });
        QCOMPARE(roleAt(m, 0, WidgetModel::LabelRole).toString(),
                 QStringLiteral("Voltage"));
    }

    void data_typeRole()
    {
        WidgetModel m;
        m.setWidgets({ makeDisplay(0x10) });
        QCOMPARE(roleAt(m, 0, WidgetModel::TypeRole).toString(),
                 QStringLiteral("display"));
    }

    void data_defaultEnabled()
    {
        WidgetModel m;
        m.setWidgets({ makeToggle(0x10, "t") });
        QVERIFY(roleAt(m, 0, WidgetModel::EnabledRole).toBool());
    }

    void data_defaultVisible()
    {
        WidgetModel m;
        m.setWidgets({ makeToggle(0x10, "t") });
        QVERIFY(roleAt(m, 0, WidgetModel::VisibleRole).toBool());
    }

    void data_invalidIndex_returnsEmpty()
    {
        WidgetModel m;
        m.setWidgets({ makeToggle(0x10, "t") });
        QVERIFY(!m.data(m.index(99), WidgetModel::LabelRole).isValid());
        QVERIFY(!m.data(QModelIndex(), WidgetModel::LabelRole).isValid());
    }

    void data_parentRole_topLevelIsNegativeOne()
    {
        WidgetModel m;
        m.setWidgets({ makeToggle(0x10, "t") });
        QCOMPARE(roleAt(m, 0, WidgetModel::ParentRole).toInt(), -1);
    }

    void data_rowRole_matchesFlatIndex()
    {
        WidgetModel m;
        m.setWidgets(makeButtonWithLed(0x10, 0x11));
        QCOMPARE(roleAt(m, 0, WidgetModel::RowRole).toInt(), 0);
        QCOMPARE(roleAt(m, 1, WidgetModel::RowRole).toInt(), 1);
    }

    /* ── Container-targeted properties (issue #43) ──────────────────── */

    void setProperty_enabled_onContainer_disablesSubtree()
    {
        WidgetModel m;
        m.setWidgets(makeSectionRowToggle());
        m.setProperty(0x10, Proto::PROP_ENABLED, 0);
        QCOMPARE(roleAt(m, 0, WidgetModel::EnabledRole).toBool(), false);
        QCOMPARE(roleAt(m, 1, WidgetModel::EnabledRole).toBool(), false);
        QCOMPARE(roleAt(m, 2, WidgetModel::EnabledRole).toBool(), false);
        QCOMPARE(roleAt(m, 3, WidgetModel::EnabledRole).toBool(), true);
    }

    void setProperty_enabled_onContainer_emitsForEveryDescendant()
    {
        WidgetModel m;
        m.setWidgets(makeSectionRowToggle());
        QSignalSpy spy(&m, &WidgetModel::dataChanged);
        m.setProperty(0x11, Proto::PROP_ENABLED, 0);  /* the row */
        QSet<int> rows;
        for (const auto& args : spy) {
            QCOMPARE(args.at(2).value<QVector<int>>(), QVector<int>{ WidgetModel::EnabledRole });
            rows.insert(args.at(0).toModelIndex().row());
        }
        QCOMPARE(rows, (QSet<int>{ 1, 2 }));  /* row itself + its toggle, not section/outer */
    }

    void resetProperty_enabled_onContainer_restoresSubtree()
    {
        WidgetModel m;
        m.setWidgets(makeSectionRowToggle());
        m.setProperty(0x10, Proto::PROP_ENABLED, 0);
        QSignalSpy spy(&m, &WidgetModel::dataChanged);
        m.resetProperty(0x10, Proto::PROP_ENABLED);
        QCOMPARE(roleAt(m, 2, WidgetModel::EnabledRole).toBool(), true);
        QCOMPARE(spy.count(), 3);  /* section, row, inner toggle */
    }

    void enabled_ownFlagStillWinsUnderEnabledContainer()
    {
        WidgetModel m;
        m.setWidgets(makeSectionRowToggle());
        m.setProperty(0x12, Proto::PROP_ENABLED, 0);
        m.setProperty(0x10, Proto::PROP_ENABLED, 0);
        m.resetProperty(0x10, Proto::PROP_ENABLED);
        QCOMPARE(roleAt(m, 2, WidgetModel::EnabledRole).toBool(), false);
    }

    void setProperty_visible_onContainer_hidesSubtree()
    {
        WidgetModel m;
        m.setWidgets(makeSectionRowToggle());
        QSignalSpy spy(&m, &WidgetModel::dataChanged);
        m.setProperty(0x10, Proto::PROP_VISIBLE, 0);
        QCOMPARE(roleAt(m, 0, WidgetModel::VisibleRole).toBool(), false);
        QCOMPARE(roleAt(m, 1, WidgetModel::VisibleRole).toBool(), false);
        QCOMPARE(roleAt(m, 2, WidgetModel::VisibleRole).toBool(), false);
        QCOMPARE(roleAt(m, 3, WidgetModel::VisibleRole).toBool(), true);
        QCOMPARE(spy.count(), 3);
        m.resetProperty(0x10, Proto::PROP_VISIBLE);
        QCOMPARE(roleAt(m, 2, WidgetModel::VisibleRole).toBool(), true);
    }

    void setProperty_enabled_onContainer_reachesChildModel()
    {
        /* QML Repeaters bind ChildModels, not WidgetModel — the descendant
         * notification must be forwarded to the row's ChildModel too. */
        WidgetModel m;
        m.setWidgets(makeSectionRowToggle());
        auto* cm = qobject_cast<QAbstractItemModel*>(m.childModel(1));
        QVERIFY(cm);
        QSignalSpy spy(cm, &QAbstractItemModel::dataChanged);
        m.setProperty(0x10, Proto::PROP_ENABLED, 0);
        QCOMPARE(spy.count(), 1);
        QCOMPARE(cm->data(cm->index(0, 0), WidgetModel::EnabledRole).toBool(), false);
    }

    /* setProperty PROP_MODE on a non-Text widget must be a silent no-op. */
    void setProperty_mode_onNonText_isNoop()
    {
        WidgetModel m;
        m.setWidgets({ makeToggle(0x10, "t") });
        QSignalSpy spy(&m, &WidgetModel::dataChanged);
        m.setProperty(0x10, Proto::PROP_MODE, 1);
        QCOMPARE(spy.count(), 0);
    }

    /* ── PropsRole for new types ─────────────────────────────────────── */

    void data_propsRole_dropdown()
    {
        /* Dropdown items are config-only (key/label pairs, no widgetId, no
         * state — the schema never assigns them protocol IDs) so they stay
         * inside props["items"] as a plain QVariantList; they never became
         * flat rows. */
        WidgetDef w;
        w.keyPath  = QStringLiteral("mode");
        w.widgetId = 0x10;
        w.type     = WidgetType::Dropdown;
        QVariantMap sta; sta[QStringLiteral("key")] = QStringLiteral("sta"); sta[QStringLiteral("label")] = QStringLiteral("Station");
        QVariantMap ap;  ap[QStringLiteral("key")]  = QStringLiteral("ap");  ap[QStringLiteral("label")]  = QStringLiteral("AP");
        w.props[QStringLiteral("items")] = QVariantList{ sta, ap };

        WidgetModel m;
        m.setWidgets({ w });
        QVariantMap props = roleAt(m, 0, WidgetModel::PropsRole).toMap();
        QVariantList items = props[QStringLiteral("items")].toList();
        QCOMPARE(items.size(), 2);
        QCOMPARE(items.at(0).toMap()[QStringLiteral("key")].toString(),   QStringLiteral("sta"));
        QCOMPARE(items.at(0).toMap()[QStringLiteral("label")].toString(), QStringLiteral("Station"));
        QCOMPARE(items.at(1).toMap()[QStringLiteral("key")].toString(),   QStringLiteral("ap"));
    }

    void data_propsRole_label()
    {
        WidgetDef w;
        w.keyPath  = QStringLiteral("title");
        w.widgetId = 0;
        w.type     = WidgetType::Label;
        w.props[QStringLiteral("text")]  = QStringLiteral("Hello");
        w.props[QStringLiteral("labelStyle")] = QStringLiteral("heading");

        WidgetModel m;
        m.setWidgets({ w });
        QVariantMap props = roleAt(m, 0, WidgetModel::PropsRole).toMap();
        QCOMPARE(props[QStringLiteral("text")].toString(),  QStringLiteral("Hello"));
        QCOMPARE(props[QStringLiteral("labelStyle")].toString(), QStringLiteral("heading"));
    }

    /* ── childModel(): children as a real model, not props["items"] ───── */

    void childModel_row_exposesChildrenWithFlexAndRoles()
    {
        WidgetDef row;
        row.keyPath  = QStringLiteral("ctrl_row");
        row.widgetId = 0;
        row.type     = WidgetType::Row;

        WidgetDef child1 = makeToggle(0x10, "relay", 0);
        child1.flex = 1;
        WidgetDef child2 = makeToggle(0x11, "fan", 0);

        WidgetModel m;
        m.setWidgets({ row, child1, child2 });

        auto* cm = qobject_cast<QAbstractListModel*>(m.childModel(0));
        QVERIFY(cm);
        QCOMPARE(cm->rowCount(), 2);
        QCOMPARE(cm->data(cm->index(0, 0), WidgetModel::WidgetIdRole).toInt(), 0x10);
        QCOMPARE(cm->data(cm->index(0, 0), WidgetModel::FlexRole).toInt(), 1);
        QVERIFY(cm->data(cm->index(0, 0), WidgetModel::EnabledRole).toBool());
        QVERIFY(cm->data(cm->index(0, 0), WidgetModel::VisibleRole).toBool());
        QCOMPARE(cm->data(cm->index(1, 0), WidgetModel::WidgetIdRole).toInt(), 0x11);
        QCOMPARE(cm->data(cm->index(1, 0), WidgetModel::FlexRole).toInt(), 0);
    }

    void childModel_grid_exposesColumnsInOwnPropsAndChildrenSeparately()
    {
        WidgetDef grid;
        grid.keyPath  = QStringLiteral("g");
        grid.widgetId = 0;
        grid.type     = WidgetType::Grid;
        grid.props[QStringLiteral("columns")] = 3;

        WidgetDef child = makeToggle(0x10, "a", 0);

        WidgetModel m;
        m.setWidgets({ grid, child });

        QCOMPARE(roleAt(m, 0, WidgetModel::PropsRole).toMap()[QStringLiteral("columns")].toInt(), 3);
        auto* cm = qobject_cast<QAbstractListModel*>(m.childModel(0));
        QVERIFY(cm);
        QCOMPARE(cm->rowCount(), 1);
        QCOMPARE(cm->data(cm->index(0, 0), WidgetModel::WidgetIdRole).toInt(), 0x10);
    }

    void childModel_get_returnsAllRolesAsMap()
    {
        WidgetDef row;
        row.keyPath  = QStringLiteral("r");
        row.widgetId = 0;
        row.type     = WidgetType::Row;
        WidgetDef child = makeToggle(0x10, "relay", 0);
        child.flex = 2;

        WidgetModel m;
        m.setWidgets({ row, child });

        auto* cm = qobject_cast<ChildModel*>(m.childModel(0));
        QVERIFY(cm);
        QVariantMap item = cm->get(0);
        QCOMPARE(item[QStringLiteral("widgetId")].toInt(), 0x10);
        QCOMPARE(item[QStringLiteral("flex")].toInt(), 2);
        QCOMPARE(item[QStringLiteral("type")].toString(), QStringLiteral("toggle"));
    }

    void childModel_isCachedAndStableAcrossCalls()
    {
        WidgetModel m;
        m.setWidgets(makeButtonWithLed(0x10, 0x11));
        QObject* cm1 = m.childModel(0);
        QObject* cm2 = m.childModel(0);
        QCOMPARE(cm1, cm2);
    }

    void childModel_isRecreatedAfterReset()
    {
        WidgetModel m;
        m.setWidgets(makeButtonWithLed(0x10, 0x11));
        QObject* before = m.childModel(0);
        m.setWidgets(makeButtonWithLed(0x12, 0x13));
        QObject* after = m.childModel(0);
        QVERIFY(before != after);
    }

    /* REGRESSION: every QML container (Row/Grid/ButtonGroup/Dpad, and the
     * top-level Repeater via childModel(-1)) binds its Repeater to a
     * ChildModel instance, not to WidgetModel itself. A live setValue()
     * (STATE_UPDATE from the device) must still reach that ChildModel's own
     * dataChanged, or the change never reaches the screen after initial
     * load — this exact gap existed until ChildModel started forwarding
     * dataChanged from its source. */
    void childModel_forwardsDataChangedFromSource()
    {
        WidgetDef row;
        row.keyPath  = QStringLiteral("r");
        row.widgetId = 0;
        row.type     = WidgetType::Row;
        WidgetDef child = makeToggle(0x10, "relay", 0);

        WidgetModel m;
        m.setWidgets({ row, child });

        auto* cm = qobject_cast<QAbstractListModel*>(m.childModel(0));
        QVERIFY(cm);
        QSignalSpy spy(cm, &QAbstractListModel::dataChanged);

        m.setValue(0x10, true);

        QCOMPARE(spy.count(), 1);
        const QModelIndex idx = spy.at(0).at(0).value<QModelIndex>();
        QCOMPARE(idx.row(), 0); /* child is ChildModel's own row 0, not the flat row */
        QVector<int> roles = spy.at(0).at(2).value<QVector<int>>();
        QVERIFY(roles.contains(WidgetModel::ValueRole));
        QVERIFY(cm->data(cm->index(0, 0), WidgetModel::ValueRole).toBool());
    }

    /* A dataChanged emitted for a row that belongs to a DIFFERENT parent
     * must not leak into a sibling container's ChildModel — only the
     * matching row's own ChildModel should re-emit. */
    void childModel_doesNotForwardDataChangedForOtherParentsRows()
    {
        WidgetDef rowA;
        rowA.keyPath  = QStringLiteral("a");
        rowA.widgetId = 0;
        rowA.type     = WidgetType::Row;
        WidgetDef childA = makeToggle(0x10, "a-child", 0);

        WidgetDef rowB;
        rowB.keyPath  = QStringLiteral("b");
        rowB.widgetId = 0;
        rowB.type     = WidgetType::Row;
        WidgetDef childB = makeToggle(0x11, "b-child", 2); /* rowB's own flat row index */

        WidgetModel m;
        m.setWidgets({ rowA, childA, rowB, childB });

        auto* cmA = qobject_cast<QAbstractListModel*>(m.childModel(0));
        auto* cmB = qobject_cast<QAbstractListModel*>(m.childModel(2));
        QSignalSpy spyA(cmA, &QAbstractListModel::dataChanged);
        QSignalSpy spyB(cmB, &QAbstractListModel::dataChanged);

        m.setValue(0x11, true); /* childB's own value, row belongs to rowB's ChildModel only */

        QCOMPARE(spyA.count(), 0);
        QCOMPARE(spyB.count(), 1);
    }

    /* REGRESSION: every childModel()-calling QML binding (WidgetDelegate.qml's
     * _childModel, DeviceScreen.qml's top-level Repeater and rowComp/gridComp/
     * dpadComp) needs a NOTIFYing dependency to force re-evaluation after a
     * live design-mode reload calls setWidgets() again — childModel() itself
     * has no NOTIFY, so without `generation`, every such binding would keep
     * referencing a ChildModel already deleted by clearChildModels(). This
     * test covers the C++ half of that contract: generation must increment
     * and emit exactly once per reset, so a QML binding that reads it (see
     * WidgetModel.h's `generation` doc) is guaranteed to re-evaluate. */
    void generation_incrementsAndEmitsOnSetWidgetsAndClear()
    {
        WidgetModel m;
        QSignalSpy spy(&m, &WidgetModel::generationChanged);

        int before = m.generation();
        m.setWidgets({ makeToggle(0x10, "a") });
        QCOMPARE(m.generation(), before + 1);
        QCOMPARE(spy.count(), 1);

        m.clear();
        QCOMPARE(m.generation(), before + 2);
        QCOMPARE(spy.count(), 2);

        /* setValue/setProperty/toggleSection are NOT resets — they must not
         * bump generation, or every live device update would needlessly
         * force every QML container to re-fetch (and every Repeater to
         * re-instantiate) its childModel(). */
        m.setWidgets({ makeToggle(0x10, "a") });
        int afterSetup = m.generation();
        spy.clear();
        m.setValue(0x10, true);
        QCOMPARE(m.generation(), afterSetup);
        QCOMPARE(spy.count(), 0);
    }

    /* ── setValue/setProperty/resetProperty at any nesting depth ──────── */

    void setValue_containerChild_storesValueInOwnRow()
    {
        WidgetDef row;
        row.keyPath  = QStringLiteral("r");
        row.widgetId = 0;
        row.type     = WidgetType::Row;
        WidgetDef child = makeToggle(0x11, "relay", 0);

        WidgetModel m;
        m.setWidgets({ row, child });
        m.setValue(0x11, true);

        QCOMPARE(roleAt(m, 1, WidgetModel::ValueRole).toBool(), true);
    }

    void setProperty_containerChild_enabled_routesToOwnRow()
    {
        WidgetDef row;
        row.keyPath  = QStringLiteral("r");
        row.widgetId = 0;
        row.type     = WidgetType::Row;
        WidgetDef child = makeToggle(0x11, "relay", 0);

        WidgetModel m;
        m.setWidgets({ row, child });
        QVERIFY(roleAt(m, 1, WidgetModel::EnabledRole).toBool());

        QSignalSpy spy(&m, &WidgetModel::dataChanged);
        m.setProperty(0x11, Proto::PROP_ENABLED, 0);

        QCOMPARE(spy.count(), 1);
        QCOMPARE(spy.at(0).at(0).value<QModelIndex>(), m.index(1));
        QVector<int> roles = spy.at(0).at(2).value<QVector<int>>();
        QVERIFY(roles.contains(WidgetModel::EnabledRole));
        QVERIFY(!roleAt(m, 1, WidgetModel::EnabledRole).toBool());
    }

    void setProperty_containerChild_visible_routesToOwnRow()
    {
        WidgetDef row;
        row.keyPath  = QStringLiteral("r");
        row.widgetId = 0;
        row.type     = WidgetType::Row;
        WidgetDef child = makeToggle(0x11, "fan", 0);

        WidgetModel m;
        m.setWidgets({ row, child });
        m.setProperty(0x11, Proto::PROP_VISIBLE, 0);
        QVERIFY(!roleAt(m, 1, WidgetModel::VisibleRole).toBool());
    }

    void resetProperty_containerChild_enabled_restoresTrue()
    {
        WidgetDef row;
        row.keyPath  = QStringLiteral("r");
        row.widgetId = 0;
        row.type     = WidgetType::Row;
        WidgetDef child = makeToggle(0x11, "relay", 0);

        WidgetModel m;
        m.setWidgets({ row, child });
        m.setProperty(0x11, Proto::PROP_ENABLED, 0);
        m.resetProperty(0x11, Proto::PROP_ENABLED);
        QVERIFY(roleAt(m, 1, WidgetModel::EnabledRole).toBool());
    }

    void setProperty_buttonChild_enabled_routesToOwnRow()
    {
        WidgetModel m;
        m.setWidgets(makeButtonWithLed(0x10, 0x11));
        QVERIFY(roleAt(m, 1, WidgetModel::EnabledRole).toBool());

        QSignalSpy spy(&m, &WidgetModel::dataChanged);
        m.setProperty(0x11, Proto::PROP_ENABLED, 0);

        QCOMPARE(spy.count(), 1);
        QVector<int> roles = spy.at(0).at(2).value<QVector<int>>();
        QVERIFY(roles.contains(WidgetModel::EnabledRole));
        QVERIFY(!roleAt(m, 1, WidgetModel::EnabledRole).toBool());
    }

    void resetProperty_buttonChild_restoresEnabled()
    {
        WidgetModel m;
        m.setWidgets(makeButtonWithLed(0x10, 0x11));
        m.setProperty(0x11, Proto::PROP_ENABLED, 0);
        m.resetProperty(0x11, Proto::PROP_ENABLED);
        QVERIFY(roleAt(m, 1, WidgetModel::EnabledRole).toBool());
    }

    void clear_unifiedChildIndexAlsoClear()
    {
        WidgetDef row;
        row.keyPath  = QStringLiteral("r");
        row.widgetId = 0;
        row.type     = WidgetType::Row;
        WidgetDef child = makeToggle(0x11, "relay", 0);

        WidgetModel m;
        m.setWidgets({ row, child });
        m.clear();

        QSignalSpy spy(&m, &WidgetModel::dataChanged);
        m.setValue(0x11, true);
        QCOMPARE(spy.count(), 0);
    }

    void setWidgets_unifiedIndex_buttonAndContainerChild()
    {
        WidgetDef row;
        row.keyPath  = QStringLiteral("r");
        row.widgetId = 0;
        row.type     = WidgetType::Row;

        QList<WidgetDef> widgets = makeButtonWithLed(0x10, 0x11);
        widgets.append(row);
        widgets.append(makeToggle(0x12, "toggle", widgets.size() - 1));

        WidgetModel m;
        m.setWidgets(widgets);

        QSignalSpy spy(&m, &WidgetModel::dataChanged);
        m.setValue(0x11, true);
        QCOMPARE(spy.count(), 1);
        spy.clear();
        m.setValue(0x12, true);
        QCOMPARE(spy.count(), 1);
    }

    /* ── toggleSection (parentId-chain visibility) ────────────────────── */

    /* Helper: build a collapsible section at row 0 with two child toggles,
     * each a direct child (parentId=0) — matches how a real YAML section's
     * children are parented under the new flat model, regardless of
     * collapsibility. */
    static QList<WidgetDef> makeCollapsibleSection()
    {
        WidgetDef section;
        section.keyPath = QStringLiteral("ctrl");
        section.widgetId = 0;
        section.type = WidgetType::Section;
        section.label = QStringLiteral("Controls");
        section.props[QStringLiteral("collapsible")] = true;

        WidgetDef child1 = makeToggle(0x10, "relay", 0);
        WidgetDef child2 = makeToggle(0x11, "fan", 0);

        return { section, child1, child2 };
    }

    void toggleSection_collapsesChildren()
    {
        WidgetModel m;
        m.setWidgets(makeCollapsibleSection());

        /* Children start visible */
        QVERIFY(roleAt(m, 1, WidgetModel::VisibleRole).toBool());
        QVERIFY(roleAt(m, 2, WidgetModel::VisibleRole).toBool());

        m.toggleSection(0);

        /* Children now hidden */
        QVERIFY(!roleAt(m, 1, WidgetModel::VisibleRole).toBool());
        QVERIFY(!roleAt(m, 2, WidgetModel::VisibleRole).toBool());
    }

    void toggleSection_uncollapses()
    {
        WidgetModel m;
        m.setWidgets(makeCollapsibleSection());
        m.toggleSection(0); /* collapse */
        m.toggleSection(0); /* expand  */
        QVERIFY(roleAt(m, 1, WidgetModel::VisibleRole).toBool());
        QVERIFY(roleAt(m, 2, WidgetModel::VisibleRole).toBool());
    }

    void toggleSection_emitsDataChangedForChildren()
    {
        WidgetModel m;
        m.setWidgets(makeCollapsibleSection());
        QSignalSpy spy(&m, &WidgetModel::dataChanged);
        m.toggleSection(0);

        /* At minimum 3 signals: section header (PropsRole) + 2 children (VisibleRole) */
        QVERIFY(spy.count() >= 3);

        /* Children signals carry VisibleRole */
        bool childSignalFound = false;
        for (int i = 0; i < spy.count(); ++i) {
            QModelIndex idx = spy.at(i).at(0).value<QModelIndex>();
            QVector<int> roles = spy.at(i).at(2).value<QVector<int>>();
            if (idx.row() > 0 && roles.contains(WidgetModel::VisibleRole))
                childSignalFound = true;
        }
        QVERIFY(childSignalFound);
    }

    void toggleSection_section_propsRole_hasCollapsedTrue()
    {
        WidgetModel m;
        m.setWidgets(makeCollapsibleSection());

        /* Before collapse: props.collapsed == false */
        QVariantMap props = roleAt(m, 0, WidgetModel::PropsRole).toMap();
        QCOMPARE(props[QStringLiteral("collapsed")].toBool(), false);

        m.toggleSection(0);

        /* After collapse: props.collapsed == true */
        props = roleAt(m, 0, WidgetModel::PropsRole).toMap();
        QCOMPARE(props[QStringLiteral("collapsed")].toBool(), true);
    }

    void toggleSection_collapsingOuterSection_hidesNestedChildren()
    {
        /* Layout:
         *   row 0: Section A (collapsible, parentId=-1)
         *   row 1: Section B (collapsible, parentId=0, owned by A)
         *   row 2: Widget C  (parentId=1, owned by B)
         */
        WidgetDef sectionA;
        sectionA.keyPath  = QStringLiteral("a");
        sectionA.widgetId = 0;
        sectionA.type     = WidgetType::Section;
        sectionA.props[QStringLiteral("collapsible")] = true;

        WidgetDef sectionB;
        sectionB.keyPath  = QStringLiteral("b");
        sectionB.widgetId = 0;
        sectionB.type     = WidgetType::Section;
        sectionB.props[QStringLiteral("collapsible")] = true;
        sectionB.parentId = 0;

        WidgetDef widgetC = makeToggle(0x10, "c", 1);

        WidgetModel m;
        m.setWidgets({ sectionA, sectionB, widgetC });

        /* Sanity: all visible before any collapse */
        QVERIFY(roleAt(m, 2, WidgetModel::VisibleRole).toBool());

        /* Collapse only Section A (not B) */
        m.toggleSection(0);

        /* Widget C must be hidden — ancestor chain includes collapsed Section A */
        QVERIFY(!roleAt(m, 2, WidgetModel::VisibleRole).toBool());

        /* Expand Section A again — Widget C must reappear */
        m.toggleSection(0);
        QVERIFY(roleAt(m, 2, WidgetModel::VisibleRole).toBool());
    }

    /* Regression guard for the toggleSection() -> emitDescendantsChanged()
     * refactor (issue #43): collapsing emits VisibleRole for EVERY
     * descendant (grandchildren included) and for nothing outside the
     * section. */
    void toggleSection_emitsVisibleRoleForExactlyItsDescendants()
    {
        QList<WidgetDef> ws = makeSectionRowToggle();
        ws[0].props[QStringLiteral("collapsible")] = true;
        WidgetModel m;
        m.setWidgets(ws);
        QSignalSpy spy(&m, &WidgetModel::dataChanged);
        m.toggleSection(0);
        QSet<int> visibleRows;
        for (const auto& args : spy) {
            if (args.at(2).value<QVector<int>>().contains(WidgetModel::VisibleRole))
                visibleRows.insert(args.at(0).toModelIndex().row());
        }
        QCOMPARE(visibleRows, (QSet<int>{ 1, 2 }));  /* row + grandchild toggle, not outer */
        QCOMPARE(roleAt(m, 2, WidgetModel::VisibleRole).toBool(), false);
        QCOMPARE(roleAt(m, 3, WidgetModel::VisibleRole).toBool(), true);
    }

    /* A hidden container keeps its subtree hidden even when a collapsible
     * section between them is expanded — both ancestor checks apply. */
    void visible_hiddenAncestorWinsOverExpandedSection()
    {
        QList<WidgetDef> ws = makeSectionRowToggle();
        ws[0].props[QStringLiteral("collapsible")] = true;
        WidgetModel m;
        m.setWidgets(ws);
        m.setProperty(0x11, Proto::PROP_VISIBLE, 0);  /* hide the row */
        QCOMPARE(roleAt(m, 2, WidgetModel::VisibleRole).toBool(), false);
        m.toggleSection(0);  /* collapse */
        m.toggleSection(0);  /* expand */
        QCOMPARE(roleAt(m, 2, WidgetModel::VisibleRole).toBool(), false);
        m.resetProperty(0x11, Proto::PROP_VISIBLE);
        QCOMPARE(roleAt(m, 2, WidgetModel::VisibleRole).toBool(), true);
    }

    void toggleSection_ignoresNonCollapsibleSection()
    {
        WidgetDef section;
        section.keyPath  = QStringLiteral("info");
        section.widgetId = 0;
        section.type     = WidgetType::Section;
        section.label    = QStringLiteral("Info");
        section.props[QStringLiteral("collapsible")] = false;

        WidgetDef child = makeToggle(0x10, "relay", 0);

        WidgetModel m;
        m.setWidgets({ section, child });

        QSignalSpy spy(&m, &WidgetModel::dataChanged);
        m.toggleSection(0); /* should be a no-op */
        QCOMPARE(spy.count(), 0);

        /* Child remains visible */
        QVERIFY(roleAt(m, 1, WidgetModel::VisibleRole).toBool());
    }

    /* ── depth-2 and depth-3 descendants: same one code path as depth-1 ─── */

    /* Build outerRow(row0,parentId=-1) → innerRow(row1,parentId=0) →
     * toggle(row2,parentId=1). */
    static QList<WidgetDef> makeRowDepth2(uint8_t toggleId)
    {
        WidgetDef outerRow;
        outerRow.keyPath  = QStringLiteral("outer");
        outerRow.widgetId = 0;
        outerRow.type     = WidgetType::Row;

        WidgetDef innerRow;
        innerRow.keyPath  = QStringLiteral("inner");
        innerRow.widgetId = 0;
        innerRow.type     = WidgetType::Row;
        innerRow.parentId = 0;

        WidgetDef toggle = makeToggle(toggleId, QStringLiteral("deep"), 1);

        return { outerRow, innerRow, toggle };
    }

    void setValue_depth2_emitsValueRoleOnOwnRow()
    {
        WidgetModel m;
        m.setWidgets(makeRowDepth2(0x15));
        QSignalSpy spy(&m, &WidgetModel::dataChanged);
        m.setValue(0x15, true);

        QCOMPARE(spy.count(), 1);
        /* Signal targets the toggle's OWN row (2), not the top-level outer row (0). */
        QCOMPARE(spy.at(0).at(0).value<QModelIndex>(), m.index(2));
        QVector<int> roles = spy.at(0).at(2).value<QVector<int>>();
        QVERIFY(roles.contains(WidgetModel::ValueRole));
        QCOMPARE(roleAt(m, 2, WidgetModel::ValueRole).toBool(), true);
    }

    void setProperty_depth2_enabled()
    {
        WidgetModel m;
        m.setWidgets(makeRowDepth2(0x15));
        QVERIFY(roleAt(m, 2, WidgetModel::EnabledRole).toBool());
        m.setProperty(0x15, Proto::PROP_ENABLED, 0);
        QVERIFY(!roleAt(m, 2, WidgetModel::EnabledRole).toBool());
    }

    void resetProperty_depth2_restoresEnabled()
    {
        WidgetModel m;
        m.setWidgets(makeRowDepth2(0x15));
        m.setProperty(0x15, Proto::PROP_ENABLED, 0);
        m.resetProperty(0x15, Proto::PROP_ENABLED);
        QVERIFY(roleAt(m, 2, WidgetModel::EnabledRole).toBool());
    }

    /* 3-level nesting: outer row → inner row → innermost row → toggle(0x16). */
    void setValue_depth3_emitsValueRoleOnOwnRow()
    {
        WidgetDef outerRow;
        outerRow.keyPath = QStringLiteral("lv1"); outerRow.widgetId = 0; outerRow.type = WidgetType::Row;
        WidgetDef innerRow;
        innerRow.keyPath = QStringLiteral("lv2"); innerRow.widgetId = 0; innerRow.type = WidgetType::Row;
        innerRow.parentId = 0;
        WidgetDef innermostRow;
        innermostRow.keyPath = QStringLiteral("lv3"); innermostRow.widgetId = 0; innermostRow.type = WidgetType::Row;
        innermostRow.parentId = 1;
        WidgetDef toggle = makeToggle(0x16, QStringLiteral("deep3"), 2);

        WidgetModel m;
        m.setWidgets({ outerRow, innerRow, innermostRow, toggle });

        QSignalSpy spy(&m, &WidgetModel::dataChanged);
        m.setValue(0x16, true);
        QCOMPARE(spy.count(), 1);
        QCOMPARE(spy.at(0).at(0).value<QModelIndex>(), m.index(3));
        QVector<int> roles = spy.at(0).at(2).value<QVector<int>>();
        QVERIFY(roles.contains(WidgetModel::ValueRole));
    }

    /* ── LED PropsRole includes color ───────────────────────────────────── */

    void data_propsRole_led_includesColor()
    {
        WidgetDef w;
        w.keyPath  = QStringLiteral("status");
        w.widgetId = 0x10;
        w.type     = WidgetType::Led;
        w.label    = QStringLiteral("Status");
        w.props[QStringLiteral("color")] = QStringLiteral("#ff0000");

        WidgetModel m;
        m.setWidgets({ w });
        QVariantMap props = roleAt(m, 0, WidgetModel::PropsRole).toMap();
        QCOMPARE(props[QStringLiteral("color")].toString(), QStringLiteral("#ff0000"));
    }

    /* ── Alignment: label text align + row/grid content align ──────────── */

    void data_propsRole_label_includesLabelAlign()
    {
        /* A label's own text alignment serializes under "labelAlign" —
         * "align" (a common WidgetDef field) is reserved for row/grid
         * content alignment and child override, exposed via its own
         * AlignRole, not PropsRole. */
        WidgetDef w;
        w.keyPath = QStringLiteral("caption");
        w.type    = WidgetType::Label;
        w.props[QStringLiteral("text")]       = QStringLiteral("Hi");
        w.props[QStringLiteral("labelAlign")] = QStringLiteral("center");

        WidgetModel m;
        m.setWidgets({ w });
        QVariantMap props = roleAt(m, 0, WidgetModel::PropsRole).toMap();
        QCOMPARE(props[QStringLiteral("labelAlign")].toString(), QStringLiteral("center"));
    }

    void data_rowGrid_ownAlign_andChildAlign_viaAlignRole()
    {
        WidgetDef row;
        row.keyPath = QStringLiteral("ctrl_row");
        row.type    = WidgetType::Row;
        row.align   = QStringLiteral("center");

        WidgetDef relay;
        relay.keyPath  = QStringLiteral("relay");
        relay.type     = WidgetType::Toggle;
        relay.align    = QStringLiteral("right"); /* own override */
        relay.parentId = 0;

        WidgetDef fan;
        fan.keyPath  = QStringLiteral("fan");
        fan.type     = WidgetType::Toggle;
        fan.parentId = 0;
        /* fan.align left at default (empty) — inherits container */

        WidgetModel m;
        m.setWidgets({ row, relay, fan });

        /* The row's own content-alignment default is a common field,
         * exposed via AlignRole (NOT nested in props). */
        QCOMPARE(roleAt(m, 0, WidgetModel::AlignRole).toString(), QStringLiteral("center"));
        QCOMPARE(roleAt(m, 1, WidgetModel::AlignRole).toString(), QStringLiteral("right"));
        QVERIFY(roleAt(m, 2, WidgetModel::AlignRole).toString().isEmpty());
    }

    /* ── RgbLed value round-trips as int ─────────────────────────────── */

    void data_rgbled_valueRoundTripsInt()
    {
        WidgetDef w;
        w.keyPath  = QStringLiteral("rgb");
        w.widgetId = 0x10;
        w.type     = WidgetType::RgbLed;
        w.label    = QStringLiteral("RGB");

        WidgetModel m;
        m.setWidgets({ w });

        /* No value yet */
        QVERIFY(!roleAt(m, 0, WidgetModel::ValueRole).isValid());

        /* Device pushes 0x00FF8000 (orange) */
        m.setValue(0x10, static_cast<int>(0x00FF8000));
        QCOMPARE(roleAt(m, 0, WidgetModel::ValueRole).toInt(),
                 static_cast<int>(0x00FF8000));
    }
};

QTEST_MAIN(TestWidgetModel)
#include "test_widget_model.moc"
