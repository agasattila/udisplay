/**
 * WidgetDump unit tests.
 *
 * Covers:
 *   - Empty top-level widget list (device header only)
 *   - Leaf widget of each type prints its type-specific fields
 *   - value and debugValue print as two distinct fields
 *   - Nested row/grid/section (depth 2+)
 *   - Button with LED/display/label face children
 *   - Empty children list on a container
 */
#include <QtTest>
#include "WidgetDump.h"
#include "WidgetDef.h"

/* ── Fixture helpers ─────────────────────────────────────────────────────── */

static WidgetDef makeDisplay(uint8_t id)
{
    WidgetDef w;
    w.keyPath      = QStringLiteral("temp");
    w.widgetId     = id;
    w.type         = WidgetType::Display;
    w.label        = QStringLiteral("Temperature");
    w.unit         = QStringLiteral("C");
    w.format       = QStringLiteral("%.1f");
    w.displayStyle = QStringLiteral("large");
    return w;
}

static WidgetDef makeLed(uint8_t id)
{
    WidgetDef w;
    w.keyPath  = QStringLiteral("status");
    w.widgetId = id;
    w.type     = WidgetType::Led;
    w.label    = QStringLiteral("Status");
    w.color    = QStringLiteral("#00d4aa");
    return w;
}

static WidgetDef makeLabel()
{
    WidgetDef w;
    w.keyPath    = QStringLiteral("hdg");
    w.widgetId   = 0;
    w.type       = WidgetType::Label;
    w.labelText  = QStringLiteral("Section Heading");
    w.labelStyle = QStringLiteral("heading");
    return w;
}

static WidgetDef makeButtonWithFaceChildren(uint8_t btnId, uint8_t ledId, uint8_t dispId)
{
    WidgetDef w;
    w.keyPath  = QStringLiteral("power_btn");
    w.widgetId = btnId;
    w.type     = WidgetType::Button;
    w.label    = QStringLiteral("Power");
    w.shape    = QStringLiteral("circle");
    w.color    = QStringLiteral("#ff0000");
    w.children.append(makeLed(ledId));
    w.children.append(makeDisplay(dispId));
    w.children.append(makeLabel());
    return w;
}

static WidgetDef makeToggle(uint8_t id, const QString& key)
{
    WidgetDef w;
    w.keyPath  = key;
    w.widgetId = id;
    w.type     = WidgetType::Toggle;
    w.label    = QStringLiteral("Toggle");
    return w;
}

static WidgetDef makeRow(const QString& key, const QList<WidgetDef>& children)
{
    WidgetDef w;
    w.keyPath  = key;
    w.widgetId = 0;
    w.type     = WidgetType::Row;
    w.children = children;
    return w;
}

static WidgetDef makeGrid(const QString& key, int columns, const QList<WidgetDef>& children)
{
    WidgetDef w;
    w.keyPath    = key;
    w.widgetId   = 0;
    w.type       = WidgetType::Grid;
    w.gridColumns = columns;
    w.children   = children;
    return w;
}

static WidgetDef makeSection(const QString& key, bool collapsible, const QList<WidgetDef>& children)
{
    WidgetDef w;
    w.keyPath     = key;
    w.widgetId    = 0;
    w.type        = WidgetType::Section;
    w.collapsible = collapsible;
    w.children    = children;
    return w;
}

class TestWidgetDump : public QObject
{
    Q_OBJECT

private slots:

    void emptyTopLevelList_printsDeviceHeaderOnly()
    {
        const QString out = dumpWidgetTree({}, QStringLiteral("Dev"), QStringLiteral("1.0"),
                                            QStringLiteral("default"));
        QVERIFY(out.contains(QStringLiteral("name=\"Dev\"")));
        QVERIFY(out.contains(QStringLiteral("version=\"1.0\"")));
        QVERIFY(out.contains(QStringLiteral("activeStyle=\"default\"")));
        QVERIFY(out.contains(QStringLiteral("(0 top-level)")));
    }

    void leafWidget_display_printsTypeSpecificFields()
    {
        const QString out = dumpWidgetTree({ makeDisplay(0x10) }, QStringLiteral("Dev"),
                                            QStringLiteral("1.0"), QStringLiteral("default"));
        QVERIFY(out.contains(QStringLiteral("[0x10] display")));
        QVERIFY(out.contains(QStringLiteral("unit: \"C\"")));
        QVERIFY(out.contains(QStringLiteral("format: \"%.1f\"")));
        QVERIFY(out.contains(QStringLiteral("style: \"large\"")));
    }

    void leafWidget_led_printsColor()
    {
        const QString out = dumpWidgetTree({ makeLed(0x11) }, QStringLiteral("Dev"),
                                            QStringLiteral("1.0"), QStringLiteral("default"));
        QVERIFY(out.contains(QStringLiteral("[0x11] led")));
        QVERIFY(out.contains(QStringLiteral("color: \"#00d4aa\"")));
    }

    void leafWidget_slider_printsMinMaxStepUnit()
    {
        WidgetDef w;
        w.keyPath   = QStringLiteral("rate");
        w.widgetId  = 0x12;
        w.type      = WidgetType::Slider;
        w.sliderMin  = 1.0;
        w.sliderMax  = 100.0;
        w.sliderStep = 0.5;
        w.unit       = QStringLiteral("Hz");

        const QString out = dumpWidgetTree({ w }, QStringLiteral("Dev"), QStringLiteral("1.0"),
                                            QStringLiteral("default"));
        QVERIFY(out.contains(QStringLiteral("min: 1")));
        QVERIFY(out.contains(QStringLiteral("max: 100")));
        QVERIFY(out.contains(QStringLiteral("step: 0.5")));
        QVERIFY(out.contains(QStringLiteral("unit: \"Hz\"")));
    }

    void leafWidget_text_printsModeAndDefaultTextModeSeparately()
    {
        WidgetDef w;
        w.keyPath         = QStringLiteral("ssid");
        w.widgetId        = 0x13;
        w.type            = WidgetType::Text;
        w.textMode        = QStringLiteral("readonly");
        w.defaultTextMode = QStringLiteral("rw");
        w.textPlaceholder = QStringLiteral("Enter SSID");
        w.textMaxLength   = 32;

        const QString out = dumpWidgetTree({ w }, QStringLiteral("Dev"), QStringLiteral("1.0"),
                                            QStringLiteral("default"));
        QVERIFY(out.contains(QStringLiteral("mode: \"readonly\"")));
        QVERIFY(out.contains(QStringLiteral("defaultTextMode: \"rw\"")));
        QVERIFY(out.contains(QStringLiteral("placeholder: \"Enter SSID\"")));
        QVERIFY(out.contains(QStringLiteral("maxlength: 32")));
    }

    void leafWidget_dropdown_printsItems()
    {
        WidgetDef w;
        w.keyPath  = QStringLiteral("wifi_mode");
        w.widgetId = 0x14;
        w.type     = WidgetType::Dropdown;
        DropdownItem sta;
        sta.key = QStringLiteral("sta");
        sta.label = QStringLiteral("Station");
        w.dropdownItems.append(sta);

        const QString out = dumpWidgetTree({ w }, QStringLiteral("Dev"), QStringLiteral("1.0"),
                                            QStringLiteral("default"));
        QVERIFY(out.contains(QStringLiteral("key=\"sta\" label=\"Station\"")));
    }

    void leafWidget_buttonGroup_printsLayoutAndItems()
    {
        WidgetDef w;
        w.keyPath     = QStringLiteral("mode_sel");
        w.widgetId    = 0x15;
        w.type        = WidgetType::ButtonGroup;
        w.groupLayout = QStringLiteral("grid");
        ButtonGroupItem dc;
        dc.keyPath  = QStringLiteral("mode_sel.dc");
        dc.widgetId = 0x16;
        dc.label    = QStringLiteral("DCV");
        dc.position = QStringLiteral("top");
        w.groupItems.append(dc);

        const QString out = dumpWidgetTree({ w }, QStringLiteral("Dev"), QStringLiteral("1.0"),
                                            QStringLiteral("default"));
        QVERIFY(out.contains(QStringLiteral("layout: \"grid\"")));
        QVERIFY(out.contains(QStringLiteral("[0x16] \"DCV\" keyPath=\"mode_sel.dc\" position=\"top\"")));
    }

    void leafWidget_valueAndDebugValue_printSeparately()
    {
        WidgetDef w = makeToggle(0x17, QStringLiteral("relay"));
        w.value      = true;
        w.debugValue = false;

        const QString out = dumpWidgetTree({ w }, QStringLiteral("Dev"), QStringLiteral("1.0"),
                                            QStringLiteral("default"));
        QVERIFY(out.contains(QStringLiteral("value: true")));
        QVERIFY(out.contains(QStringLiteral("debugValue: false")));
    }

    void leafWidget_nullValueAndDebugValue_printAsNull()
    {
        WidgetDef w = makeToggle(0x18, QStringLiteral("fan"));
        const QString out = dumpWidgetTree({ w }, QStringLiteral("Dev"), QStringLiteral("1.0"),
                                            QStringLiteral("default"));
        QVERIFY(out.contains(QStringLiteral("value: (null)")));
        QVERIFY(out.contains(QStringLiteral("debugValue: (null)")));
    }

    void container_row_withEmptyChildren_printsNoChildrenSection()
    {
        WidgetDef row = makeRow(QStringLiteral("empty_row"), {});
        const QString out = dumpWidgetTree({ row }, QStringLiteral("Dev"), QStringLiteral("1.0"),
                                            QStringLiteral("default"));
        QVERIFY(out.contains(QStringLiteral("row")));
        QVERIFY(!out.contains(QStringLiteral("children:")));
    }

    void container_grid_printsColumnsAndChildren()
    {
        WidgetDef grid = makeGrid(QStringLiteral("panel"), 3,
                                   { makeToggle(0x19, QStringLiteral("a")),
                                     makeToggle(0x1a, QStringLiteral("b")) });
        const QString out = dumpWidgetTree({ grid }, QStringLiteral("Dev"), QStringLiteral("1.0"),
                                            QStringLiteral("default"));
        QVERIFY(out.contains(QStringLiteral("columns: 3")));
        QVERIFY(out.contains(QStringLiteral("[0x19] toggle")));
        QVERIFY(out.contains(QStringLiteral("[0x1a] toggle")));
    }

    void nestedContainers_depth3_rendersFullyIndented()
    {
        WidgetDef innerRow = makeRow(QStringLiteral("inner"),
                                      { makeToggle(0x1b, QStringLiteral("leaf")) });
        WidgetDef grid = makeGrid(QStringLiteral("mid"), 2, { innerRow });
        WidgetDef section = makeSection(QStringLiteral("outer"), true, { grid });

        const QString out = dumpWidgetTree({ section }, QStringLiteral("Dev"), QStringLiteral("1.0"),
                                            QStringLiteral("default"));
        QVERIFY(out.contains(QStringLiteral("section")));
        QVERIFY(out.contains(QStringLiteral("collapsible: true")));
        QVERIFY(out.contains(QStringLiteral("grid")));
        QVERIFY(out.contains(QStringLiteral("columns: 2")));
        QVERIFY(out.contains(QStringLiteral("row")));
        QVERIFY(out.contains(QStringLiteral("[0x1b] toggle")));

        /* Depth-3 leaf must be indented deeper than the depth-1 section. */
        const int sectionIndent = out.indexOf(QStringLiteral("[0x00] section"));
        const int leafIndent    = out.indexOf(QStringLiteral("[0x1b] toggle"));
        QVERIFY(sectionIndent >= 0 && leafIndent >= 0);
        auto leadingSpaces = [&](int pos) {
            int start = out.lastIndexOf(QLatin1Char('\n'), pos) + 1;
            return pos - start;
        };
        QVERIFY(leadingSpaces(leafIndent) > leadingSpaces(sectionIndent));
    }

    void buttonWithFaceChildren_printsLedDisplayAndLabelChildren()
    {
        WidgetDef btn = makeButtonWithFaceChildren(0x1c, 0x1d, 0x1e);
        const QString out = dumpWidgetTree({ btn }, QStringLiteral("Dev"), QStringLiteral("1.0"),
                                            QStringLiteral("default"));
        QVERIFY(out.contains(QStringLiteral("[0x1c] button")));
        QVERIFY(out.contains(QStringLiteral("shape: \"circle\"")));
        QVERIFY(out.contains(QStringLiteral("[0x1d] led")));
        QVERIFY(out.contains(QStringLiteral("[0x1e] display")));
        QVERIFY(out.contains(QStringLiteral("label")));
        QVERIFY(out.contains(QStringLiteral("text: \"Section Heading\"")));
        /* Section header for non-empty children must say "widgets:" (renamed
         * from "children:" to match the current YAML key), not the old label. */
        QVERIFY(out.contains(QStringLiteral("widgets:")));
        QVERIFY(!out.contains(QStringLiteral("children:")));
    }
};

QTEST_MAIN(TestWidgetDump)
#include "test_widget_dump.moc"
