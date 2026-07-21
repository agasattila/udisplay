/**
 * WidgetModel unit tests.
 *
 * Covers:
 *   - setValue for top-level widgets (ValueRole + dataChanged)
 *   - setValue for button children (PropsRole + bc.value storage)  ← regression test
 *   - setProperty / resetProperty for ENABLED, VISIBLE, MODE
 *   - data() PropsRole for all 5 typed widgets
 *   - setWidgets populates m_childIndex correctly
 *   - clear() resets all state
 */
#include <QtTest>
#include "WidgetModel.h"
#include "WidgetDef.h"
#include "Protocol.h"

/* ── Fixture helpers ─────────────────────────────────────────────────────── */

/* Build a minimal toggle widget. */
static WidgetDef makeToggle(uint8_t id, const QString& key)
{
    WidgetDef w;
    w.keyPath  = key;
    w.widgetId = id;
    w.type     = WidgetType::Toggle;
    w.label    = QStringLiteral("Toggle");
    return w;
}

/* Build a button with one LED child. */
static WidgetDef makeButtonWithLed(uint8_t btnId, uint8_t ledId)
{
    WidgetDef w;
    w.keyPath  = QStringLiteral("btn");
    w.widgetId = btnId;
    w.type     = WidgetType::Button;
    w.label    = QStringLiteral("Fire");
    w.shape    = QStringLiteral("circle");

    WidgetDef led;
    led.keyPath  = QStringLiteral("btn.led");
    led.widgetId = ledId;
    led.type     = WidgetType::Led;
    led.label    = QStringLiteral("Active");
    w.children.append(led);
    return w;
}

/* Build a display widget. */
static WidgetDef makeDisplay(uint8_t id)
{
    WidgetDef w;
    w.keyPath  = QStringLiteral("disp");
    w.widgetId = id;
    w.type     = WidgetType::Display;
    w.label    = QStringLiteral("Voltage");
    w.unit     = QStringLiteral("V");
    w.format   = QStringLiteral("%.3f");
    w.displayStyle = QStringLiteral("large");
    return w;
}

/* Build a slider widget. */
static WidgetDef makeSlider(uint8_t id)
{
    WidgetDef w;
    w.keyPath   = QStringLiteral("rate");
    w.widgetId  = id;
    w.type      = WidgetType::Slider;
    w.label     = QStringLiteral("Rate");
    w.sliderMin  = 1.0;
    w.sliderMax  = 100.0;
    w.sliderStep = 0.5;
    w.unit       = QStringLiteral("Hz");
    return w;
}

/* Build a text widget. */
static WidgetDef makeText(uint8_t id)
{
    WidgetDef w;
    w.keyPath        = QStringLiteral("ssid");
    w.widgetId       = id;
    w.type           = WidgetType::Text;
    w.label          = QStringLiteral("SSID");
    w.textMode        = QStringLiteral("rw");
    w.defaultTextMode = QStringLiteral("rw");
    w.textPlaceholder = QStringLiteral("Enter SSID");
    w.textMaxLength  = 32;
    return w;
}

/* Build a button-group with two items. */
static WidgetDef makeButtonGroup(uint8_t groupId, uint8_t dcId, uint8_t acId)
{
    WidgetDef w;
    w.keyPath     = QStringLiteral("mode");
    w.widgetId    = groupId;
    w.type        = WidgetType::ButtonGroup;
    w.label       = QStringLiteral("Mode");
    w.groupLayout = QStringLiteral("grid");

    ButtonGroupItem dc;
    dc.keyPath  = QStringLiteral("mode.dc");
    dc.widgetId = dcId;
    dc.label    = QStringLiteral("DCV");
    w.groupItems.append(dc);

    ButtonGroupItem ac;
    ac.keyPath  = QStringLiteral("mode.ac");
    ac.widgetId = acId;
    ac.label    = QStringLiteral("ACV");
    w.groupItems.append(ac);
    return w;
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
        /* After setWidgets, setValue on a child ID must route to the parent row.
         * This is the regression guard for the ButtonWidget LED bug. */
        WidgetModel m;
        m.setWidgets({ makeButtonWithLed(0x10, 0x11) });

        QSignalSpy spy(&m, &WidgetModel::dataChanged);
        m.setValue(0x11, true); /* child LED ID */

        /* PropsRole on the parent row (0) must have been notified */
        QCOMPARE(spy.count(), 1);
        QModelIndex parentIdx = m.index(0);
        QCOMPARE(spy.at(0).at(0).value<QModelIndex>(), parentIdx);
        QVector<int> roles = spy.at(0).at(2).value<QVector<int>>();
        QVERIFY(roles.contains(WidgetModel::PropsRole));
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
        m.setWidgets({ makeButtonWithLed(0x10, 0x11) });
        m.clear();

        QSignalSpy spy(&m, &WidgetModel::dataChanged);
        m.setValue(0x11, true);
        QCOMPARE(spy.count(), 0); /* no-op: child index cleared */
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

    /* ── setValue — button child (REGRESSION TEST for LED bug) ────────── */

    void setValue_buttonChild_storesValueInBc()
    {
        WidgetModel m;
        m.setWidgets({ makeButtonWithLed(0x10, 0x11) });

        /* Set the LED (child 0x11) value to true */
        m.setValue(0x11, true);

        /* Retrieve via PropsRole — children serialised under "items" key */
        QVariant propsVar = roleAt(m, 0, WidgetModel::PropsRole);
        QVariantMap props = propsVar.toMap();
        QVERIFY(props.contains(QStringLiteral("items")));
        QVariantList kids = props[QStringLiteral("items")].toList();
        QCOMPARE(kids.size(), 1);
        QVariantMap kid = kids.at(0).toMap();
        QCOMPARE(kid[QStringLiteral("widgetId")].toInt(), 0x11);
        QCOMPARE(kid[QStringLiteral("value")].toBool(), true);
    }

    void setValue_buttonChild_emitsPropsRoleOnParent()
    {
        WidgetModel m;
        m.setWidgets({ makeButtonWithLed(0x10, 0x11) });
        QSignalSpy spy(&m, &WidgetModel::dataChanged);
        m.setValue(0x11, true);

        QCOMPARE(spy.count(), 1);
        QVector<int> roles = spy.at(0).at(2).value<QVector<int>>();
        QVERIFY(roles.contains(WidgetModel::PropsRole));
        /* Must NOT emit ValueRole on the child (children are not in m_idToRow) */
        QVERIFY(!roles.contains(WidgetModel::ValueRole));
    }

    void setValue_buttonChild_doesNotEmitValueRoleForParent()
    {
        /* Sanity: a child setState emits PropsRole, not ValueRole, on the parent */
        WidgetModel m;
        m.setWidgets({ makeButtonWithLed(0x10, 0x11) });
        QSignalSpy spy(&m, &WidgetModel::dataChanged);
        m.setValue(0x11, false);
        QVector<int> roles = spy.at(0).at(2).value<QVector<int>>();
        QVERIFY(!roles.contains(WidgetModel::ValueRole));
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
        /* textMode starts as "rw" from makeText; set to readonly (0) */
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

    /* ── TODO-028: resetProperty PROP_MODE restores YAML default ─────── */

    void resetProperty_textMode_restoresYamlDefault()
    {
        WidgetModel m;
        m.setWidgets({ makeText(0x10) }); /* defaultTextMode = "rw" */

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
        m.setWidgets({ makeText(0x10) }); /* textMode already == defaultTextMode */
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
        QCOMPARE(props[QStringLiteral("style")].toString(),  QStringLiteral("large"));
    }

    void data_propsRole_button()
    {
        WidgetModel m;
        m.setWidgets({ makeButtonWithLed(0x10, 0x11) });
        QVariantMap props = roleAt(m, 0, WidgetModel::PropsRole).toMap();
        QCOMPARE(props[QStringLiteral("shape")].toString(), QStringLiteral("circle"));
        QVERIFY(!props.contains(QStringLiteral("color")));
        QVariantList kids = props[QStringLiteral("items")].toList();
        QCOMPARE(kids.size(), 1);
        QVariantMap kid = kids.at(0).toMap();
        QCOMPARE(kid[QStringLiteral("widgetId")].toInt(), 0x11);
        QCOMPARE(kid[QStringLiteral("label")].toString(), QStringLiteral("Active"));
        /* value starts as null (no setValue called yet) */
        QVERIFY(!kid[QStringLiteral("value")].isValid());
    }

    void data_propsRole_buttonGroup()
    {
        WidgetModel m;
        m.setWidgets({ makeButtonGroup(0x10, 0x11, 0x12) });
        QVariantMap props = roleAt(m, 0, WidgetModel::PropsRole).toMap();
        QCOMPARE(props[QStringLiteral("layout")].toString(), QStringLiteral("grid"));
        QVariantList items = props[QStringLiteral("items")].toList();
        QCOMPARE(items.size(), 2);
        /* Items preserve declaration order (dc first, ac second) */
        QCOMPARE(items.at(0).toMap()[QStringLiteral("label")].toString(), QStringLiteral("DCV"));
        QCOMPARE(items.at(1).toMap()[QStringLiteral("label")].toString(), QStringLiteral("ACV"));
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
        WidgetDef w;
        w.keyPath  = QStringLiteral("mode");
        w.widgetId = 0x10;
        w.type     = WidgetType::Dropdown;
        DropdownItem sta; sta.key = QStringLiteral("sta"); sta.label = QStringLiteral("Station");
        DropdownItem ap;  ap.key  = QStringLiteral("ap");  ap.label  = QStringLiteral("AP");
        w.dropdownItems.append(sta);
        w.dropdownItems.append(ap);

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
        w.keyPath    = QStringLiteral("title");
        w.widgetId   = 0;
        w.type       = WidgetType::Label;
        w.labelText  = QStringLiteral("Hello");
        w.labelStyle = QStringLiteral("heading");

        WidgetModel m;
        m.setWidgets({ w });
        QVariantMap props = roleAt(m, 0, WidgetModel::PropsRole).toMap();
        QCOMPARE(props[QStringLiteral("text")].toString(),  QStringLiteral("Hello"));
        QCOMPARE(props[QStringLiteral("style")].toString(), QStringLiteral("heading"));
    }

    void data_propsRole_row_hasItems()
    {
        WidgetDef child1 = makeToggle(0x10, "relay");
        child1.flex = 1;
        WidgetDef child2 = makeToggle(0x11, "fan");

        WidgetDef row;
        row.keyPath  = QStringLiteral("ctrl_row");
        row.widgetId = 0;
        row.type     = WidgetType::Row;
        row.children.append(child1);
        row.children.append(child2);

        WidgetModel m;
        m.setWidgets({ row });
        QVariantMap props = roleAt(m, 0, WidgetModel::PropsRole).toMap();
        QVariantList items = props[QStringLiteral("items")].toList();
        QCOMPARE(items.size(), 2);

        QVariantMap item0 = items.at(0).toMap();
        QCOMPARE(item0[QStringLiteral("widgetId")].toInt(), 0x10);
        QCOMPARE(item0[QStringLiteral("type")].toString(),  QStringLiteral("toggle"));
        QCOMPARE(item0[QStringLiteral("flex")].toInt(),     1);
        QVERIFY(item0[QStringLiteral("enabled")].toBool());
        QVERIFY(item0[QStringLiteral("visible")].toBool());
    }

    void data_propsRole_grid_hasColumnsAndItems()
    {
        WidgetDef child = makeToggle(0x10, "a");

        WidgetDef grid;
        grid.keyPath     = QStringLiteral("g");
        grid.widgetId    = 0;
        grid.type        = WidgetType::Grid;
        grid.gridColumns = 3;
        grid.children.append(child);

        WidgetModel m;
        m.setWidgets({ grid });
        QVariantMap props = roleAt(m, 0, WidgetModel::PropsRole).toMap();
        QCOMPARE(props[QStringLiteral("columns")].toInt(), 3);
        QVariantList items = props[QStringLiteral("items")].toList();
        QCOMPARE(items.size(), 1);
    }

    /* ── container child reactive updates (unified m_childIndex) ──────── */

    void setValue_containerChild_storesValueInChild()
    {
        WidgetDef child = makeToggle(0x11, "relay");
        WidgetDef row;
        row.keyPath  = QStringLiteral("r");
        row.widgetId = 0;
        row.type     = WidgetType::Row;
        row.children.append(child);

        WidgetModel m;
        m.setWidgets({ row });
        m.setValue(0x11, true);

        QVariantMap props = roleAt(m, 0, WidgetModel::PropsRole).toMap();
        QVariantList items = props[QStringLiteral("items")].toList();
        QCOMPARE(items.size(), 1);
        QCOMPARE(items.at(0).toMap()[QStringLiteral("value")].toBool(), true);
    }

    void setValue_containerChild_emitsPropsRoleOnContainer()
    {
        WidgetDef child = makeToggle(0x11, "relay");
        WidgetDef row;
        row.keyPath  = QStringLiteral("r");
        row.widgetId = 0;
        row.type     = WidgetType::Row;
        row.children.append(child);

        WidgetModel m;
        m.setWidgets({ row });
        QSignalSpy spy(&m, &WidgetModel::dataChanged);
        m.setValue(0x11, true);

        QCOMPARE(spy.count(), 1);
        QVector<int> roles = spy.at(0).at(2).value<QVector<int>>();
        QVERIFY(roles.contains(WidgetModel::PropsRole));
        QVERIFY(!roles.contains(WidgetModel::ValueRole));
    }

    void setProperty_containerChild_enabled_routesToContainer()
    {
        WidgetDef child = makeToggle(0x11, "relay");
        WidgetDef row;
        row.keyPath  = QStringLiteral("r");
        row.widgetId = 0;
        row.type     = WidgetType::Row;
        row.children.append(child);

        WidgetModel m;
        m.setWidgets({ row });
        QVERIFY(m.data(m.index(0), WidgetModel::PropsRole)
                  .toMap()[QStringLiteral("items")].toList()
                  .at(0).toMap()[QStringLiteral("enabled")].toBool());

        QSignalSpy spy(&m, &WidgetModel::dataChanged);
        m.setProperty(0x11, Proto::PROP_ENABLED, 0);

        QCOMPARE(spy.count(), 1);
        QVector<int> roles = spy.at(0).at(2).value<QVector<int>>();
        QVERIFY(roles.contains(WidgetModel::PropsRole));

        bool enabled = m.data(m.index(0), WidgetModel::PropsRole)
                         .toMap()[QStringLiteral("items")].toList()
                         .at(0).toMap()[QStringLiteral("enabled")].toBool();
        QVERIFY(!enabled);
    }

    void setProperty_containerChild_visible_routesToContainer()
    {
        WidgetDef child = makeToggle(0x11, "fan");
        WidgetDef row;
        row.keyPath  = QStringLiteral("r");
        row.widgetId = 0;
        row.type     = WidgetType::Row;
        row.children.append(child);

        WidgetModel m;
        m.setWidgets({ row });
        m.setProperty(0x11, Proto::PROP_VISIBLE, 0);

        bool visible = m.data(m.index(0), WidgetModel::PropsRole)
                         .toMap()[QStringLiteral("items")].toList()
                         .at(0).toMap()[QStringLiteral("visible")].toBool();
        QVERIFY(!visible);
    }

    void resetProperty_containerChild_enabled_restoresTrue()
    {
        WidgetDef child = makeToggle(0x11, "relay");
        WidgetDef row;
        row.keyPath  = QStringLiteral("r");
        row.widgetId = 0;
        row.type     = WidgetType::Row;
        row.children.append(child);

        WidgetModel m;
        m.setWidgets({ row });
        m.setProperty(0x11, Proto::PROP_ENABLED, 0);
        m.resetProperty(0x11, Proto::PROP_ENABLED);

        bool enabled = m.data(m.index(0), WidgetModel::PropsRole)
                         .toMap()[QStringLiteral("items")].toList()
                         .at(0).toMap()[QStringLiteral("enabled")].toBool();
        QVERIFY(enabled);
    }

    /* Button children now participate in the unified m_childIndex and receive
     * property commands the same way container children do (new in this PR). */
    void setProperty_buttonChild_enabled_routesToParent()
    {
        WidgetModel m;
        m.setWidgets({ makeButtonWithLed(0x10, 0x11) });
        QVERIFY(m.data(m.index(0), WidgetModel::PropsRole)
                  .toMap()[QStringLiteral("items")].toList()
                  .at(0).toMap()[QStringLiteral("enabled")].toBool());

        QSignalSpy spy(&m, &WidgetModel::dataChanged);
        m.setProperty(0x11, Proto::PROP_ENABLED, 0);

        QCOMPARE(spy.count(), 1);
        QVector<int> roles = spy.at(0).at(2).value<QVector<int>>();
        QVERIFY(roles.contains(WidgetModel::PropsRole));

        bool enabled = m.data(m.index(0), WidgetModel::PropsRole)
                         .toMap()[QStringLiteral("items")].toList()
                         .at(0).toMap()[QStringLiteral("enabled")].toBool();
        QVERIFY(!enabled);
    }

    void resetProperty_buttonChild_restoresEnabled()
    {
        WidgetModel m;
        m.setWidgets({ makeButtonWithLed(0x10, 0x11) });
        m.setProperty(0x11, Proto::PROP_ENABLED, 0);
        m.resetProperty(0x11, Proto::PROP_ENABLED);

        bool enabled = m.data(m.index(0), WidgetModel::PropsRole)
                         .toMap()[QStringLiteral("items")].toList()
                         .at(0).toMap()[QStringLiteral("enabled")].toBool();
        QVERIFY(enabled);
    }

    /* Unified m_childIndex covers both button children and container children;
     * clear() must flush the entire map. This tests the container-child path. */
    void clear_unifiedChildIndexAlsoClear()
    {
        WidgetDef child = makeToggle(0x11, "relay");
        WidgetDef row;
        row.keyPath  = QStringLiteral("r");
        row.widgetId = 0;
        row.type     = WidgetType::Row;
        row.children.append(child);

        WidgetModel m;
        m.setWidgets({ row });
        m.clear();

        QSignalSpy spy(&m, &WidgetModel::dataChanged);
        m.setValue(0x11, true);
        QCOMPARE(spy.count(), 0);
    }

    /* Unified map routes BOTH button child and container child IDs after setWidgets
     * with mixed parent types. */
    void setWidgets_unifiedIndex_buttonAndContainerChild()
    {
        WidgetDef row;
        row.keyPath  = QStringLiteral("r");
        row.widgetId = 0;
        row.type     = WidgetType::Row;
        row.children.append(makeToggle(0x12, "toggle"));

        WidgetModel m;
        /* row (container child 0x12) + button (button child 0x11) in same model */
        m.setWidgets({ makeButtonWithLed(0x10, 0x11), row });

        QSignalSpy spy(&m, &WidgetModel::dataChanged);
        m.setValue(0x11, true);
        QCOMPARE(spy.count(), 1);
        spy.clear();
        m.setValue(0x12, true);
        QCOMPARE(spy.count(), 1);
    }

    /* ── toggleSection ───────────────────────────────────────────────── */

    /* Helper: build a collapsible section at row 0 with two child toggles */
    static QList<WidgetDef> makeCollapsibleSection()
    {
        WidgetDef section;
        section.keyPath     = QStringLiteral("ctrl");
        section.widgetId    = 0;
        section.type        = WidgetType::Section;
        section.label       = QStringLiteral("Controls");
        section.collapsible = true;

        WidgetDef child1 = makeToggle(0x10, "relay");
        child1.sectionOwnerRow = 0;

        WidgetDef child2 = makeToggle(0x11, "fan");
        child2.sectionOwnerRow = 0;

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
         *   row 0: Section A (collapsible, sectionOwnerRow=-1)
         *   row 1: Section B (collapsible, sectionOwnerRow=0)
         *   row 2: Widget C  (sectionOwnerRow=1)
         */
        WidgetDef sectionA;
        sectionA.keyPath     = QStringLiteral("a");
        sectionA.widgetId    = 0;
        sectionA.type        = WidgetType::Section;
        sectionA.collapsible = true;

        WidgetDef sectionB;
        sectionB.keyPath        = QStringLiteral("b");
        sectionB.widgetId       = 0;
        sectionB.type           = WidgetType::Section;
        sectionB.collapsible    = true;
        sectionB.sectionOwnerRow = 0;  /* owned by Section A */

        WidgetDef widgetC = makeToggle(0x10, "c");
        widgetC.sectionOwnerRow = 1;   /* owned by Section B */

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

    void toggleSection_ignoresNonCollapsibleSection()
    {
        WidgetDef section;
        section.keyPath     = QStringLiteral("info");
        section.widgetId    = 0;
        section.type        = WidgetType::Section;
        section.label       = QStringLiteral("Info");
        section.collapsible = false;

        WidgetDef child = makeToggle(0x10, "relay");
        /* sectionOwnerRow stays -1 for non-collapsible sections */

        WidgetModel m;
        m.setWidgets({ section, child });

        QSignalSpy spy(&m, &WidgetModel::dataChanged);
        m.toggleSection(0); /* should be a no-op */
        QCOMPARE(spy.count(), 0);

        /* Child remains visible */
        QVERIFY(roleAt(m, 1, WidgetModel::VisibleRole).toBool());
    }

    /* ── TODO-031: depth-2 and depth-3 descendant indexing ──────────────── */

    /* Build outerRow(widgetId=0) → innerRow(widgetId=0) → toggle(id) */
    static WidgetDef makeRowDepth2(uint8_t toggleId)
    {
        WidgetDef innerRow;
        innerRow.keyPath  = QStringLiteral("inner");
        innerRow.widgetId = 0;
        innerRow.type     = WidgetType::Row;
        innerRow.children.append(makeToggle(toggleId, QStringLiteral("deep")));

        WidgetDef outerRow;
        outerRow.keyPath  = QStringLiteral("outer");
        outerRow.widgetId = 0;
        outerRow.type     = WidgetType::Row;
        outerRow.children.append(innerRow);
        return outerRow;
    }

    /* m_childPath correctly indexes a depth-2 widget after setWidgets. */
    void setWidgets_depth2_childIndexed()
    {
        WidgetModel m;
        m.setWidgets({ makeRowDepth2(0x15) });

        /* setValue on the depth-2 toggle must emit a signal (proves it was found) */
        QSignalSpy spy(&m, &WidgetModel::dataChanged);
        m.setValue(0x15, true);
        QCOMPARE(spy.count(), 1);
    }

    /* setValue on a depth-2 widget stores the value in the serialized tree. */
    void setValue_depth2_storesValue()
    {
        WidgetModel m;
        m.setWidgets({ makeRowDepth2(0x15) });
        m.setValue(0x15, true);

        /* Navigate: props["items"][0]["props"]["items"][0]["value"] */
        QVariantList outerItems =
            roleAt(m, 0, WidgetModel::PropsRole).toMap()[QStringLiteral("items")].toList();
        QCOMPARE(outerItems.size(), 1); /* inner row */
        QVariantList innerItems =
            outerItems.at(0).toMap()[QStringLiteral("props")].toMap()
                             [QStringLiteral("items")].toList();
        QCOMPARE(innerItems.size(), 1); /* toggle */
        QCOMPARE(innerItems.at(0).toMap()[QStringLiteral("value")].toBool(), true);
    }

    /* setValue on a depth-2 widget emits PropsRole on the top-level row (row 0). */
    void setValue_depth2_emitsPropsRoleOnTopLevel()
    {
        WidgetModel m;
        m.setWidgets({ makeRowDepth2(0x15) });
        QSignalSpy spy(&m, &WidgetModel::dataChanged);
        m.setValue(0x15, true);

        QCOMPARE(spy.count(), 1);
        /* Signal must reference row 0 (top-level outer row) */
        QCOMPARE(spy.at(0).at(0).value<QModelIndex>(), m.index(0));
        QVector<int> roles = spy.at(0).at(2).value<QVector<int>>();
        QVERIFY(roles.contains(WidgetModel::PropsRole));
        QVERIFY(!roles.contains(WidgetModel::ValueRole));
    }

    /* setProperty PROP_ENABLED correctly reaches a depth-2 widget. */
    void setProperty_depth2_enabled()
    {
        WidgetModel m;
        m.setWidgets({ makeRowDepth2(0x15) });

        /* Toggle starts enabled */
        QVariantList outerItems =
            roleAt(m, 0, WidgetModel::PropsRole).toMap()[QStringLiteral("items")].toList();
        bool startEnabled = outerItems.at(0).toMap()[QStringLiteral("props")].toMap()
                                          [QStringLiteral("items")].toList()
                                          .at(0).toMap()[QStringLiteral("enabled")].toBool();
        QVERIFY(startEnabled);

        m.setProperty(0x15, Proto::PROP_ENABLED, 0);

        outerItems = roleAt(m, 0, WidgetModel::PropsRole).toMap()
                        [QStringLiteral("items")].toList();
        bool nowEnabled = outerItems.at(0).toMap()[QStringLiteral("props")].toMap()
                                         [QStringLiteral("items")].toList()
                                         .at(0).toMap()[QStringLiteral("enabled")].toBool();
        QVERIFY(!nowEnabled);
    }

    /* resetProperty PROP_ENABLED correctly restores a depth-2 widget. */
    void resetProperty_depth2_restoresEnabled()
    {
        WidgetModel m;
        m.setWidgets({ makeRowDepth2(0x15) });
        m.setProperty(0x15, Proto::PROP_ENABLED, 0);
        m.resetProperty(0x15, Proto::PROP_ENABLED);

        QVariantList outerItems =
            roleAt(m, 0, WidgetModel::PropsRole).toMap()[QStringLiteral("items")].toList();
        bool enabled = outerItems.at(0).toMap()[QStringLiteral("props")].toMap()
                                       [QStringLiteral("items")].toList()
                                       .at(0).toMap()[QStringLiteral("enabled")].toBool();
        QVERIFY(enabled);
    }

    /* 3-level nesting: outer row → inner row → innermost row → toggle(0x16). */
    void setWidgets_depth3_childIndexed()
    {
        WidgetDef innermostRow;
        innermostRow.keyPath  = QStringLiteral("lv3");
        innermostRow.widgetId = 0;
        innermostRow.type     = WidgetType::Row;
        innermostRow.children.append(makeToggle(0x16, QStringLiteral("deep3")));

        WidgetDef innerRow;
        innerRow.keyPath  = QStringLiteral("lv2");
        innerRow.widgetId = 0;
        innerRow.type     = WidgetType::Row;
        innerRow.children.append(innermostRow);

        WidgetDef outerRow;
        outerRow.keyPath  = QStringLiteral("lv1");
        outerRow.widgetId = 0;
        outerRow.type     = WidgetType::Row;
        outerRow.children.append(innerRow);

        WidgetModel m;
        m.setWidgets({ outerRow });

        /* setValue on the depth-3 toggle must route to top-level row 0 */
        QSignalSpy spy(&m, &WidgetModel::dataChanged);
        m.setValue(0x16, true);
        QCOMPARE(spy.count(), 1);
        QCOMPARE(spy.at(0).at(0).value<QModelIndex>(), m.index(0));
        QVector<int> roles = spy.at(0).at(2).value<QVector<int>>();
        QVERIFY(roles.contains(WidgetModel::PropsRole));
    }

    /* ── TODO-020: LED PropsRole includes color ────────────────────────── */

    void data_propsRole_led_includesColor()
    {
        WidgetDef w;
        w.keyPath  = QStringLiteral("status");
        w.widgetId = 0x10;
        w.type     = WidgetType::Led;
        w.label    = QStringLiteral("Status");
        w.color    = QStringLiteral("#ff0000");

        WidgetModel m;
        m.setWidgets({ w });
        QVariantMap props = roleAt(m, 0, WidgetModel::PropsRole).toMap();
        QCOMPARE(props[QStringLiteral("color")].toString(), QStringLiteral("#ff0000"));
    }

    /* ── Alignment: label text align + row/grid content align ──────────── */

    void data_propsRole_label_includesLabelAlign()
    {
        /* A label's own text alignment serializes under "labelAlign" —
         * "align" is reserved for row/grid position (see eefe866's rename;
         * WidgetModel.cpp only sets props["align"] for Row/Grid types). */
        WidgetDef w;
        w.keyPath    = QStringLiteral("caption");
        w.type       = WidgetType::Label;
        w.labelText  = QStringLiteral("Hi");
        w.labelAlign = QStringLiteral("center");

        WidgetModel m;
        m.setWidgets({ w });
        QVariantMap props = roleAt(m, 0, WidgetModel::PropsRole).toMap();
        QCOMPARE(props[QStringLiteral("labelAlign")].toString(), QStringLiteral("center"));
    }

    void data_propsRole_row_includesContainerAndChildAlign()
    {
        WidgetDef relay;
        relay.keyPath = QStringLiteral("relay");
        relay.type    = WidgetType::Toggle;
        relay.align   = QStringLiteral("right"); /* own override */

        WidgetDef fan;
        fan.keyPath = QStringLiteral("fan");
        fan.type    = WidgetType::Toggle;
        /* fan.align left at default (empty) — inherits container */

        WidgetDef row;
        row.keyPath = QStringLiteral("ctrl_row");
        row.type    = WidgetType::Row;
        row.align   = QStringLiteral("center");
        row.children = { relay, fan };

        WidgetModel m;
        m.setWidgets({ row });
        QVariantMap props = roleAt(m, 0, WidgetModel::PropsRole).toMap();
        QCOMPARE(props[QStringLiteral("align")].toString(), QStringLiteral("center"));

        QVariantList items = props[QStringLiteral("items")].toList();
        QCOMPARE(items.size(), 2);
        QCOMPARE(items[0].toMap()[QStringLiteral("align")].toString(), QStringLiteral("right"));
        QVERIFY(items[1].toMap()[QStringLiteral("align")].toString().isEmpty());
    }

    /* ── TODO-021: RgbLed value round-trips as int ─────────────────────── */

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
