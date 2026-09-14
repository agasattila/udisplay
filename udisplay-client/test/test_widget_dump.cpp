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
 *
 * widgets are built as FLAT lists (WidgetModel's own shape) — every helper
 * appends to an out-list and returns the row index it just appended, so
 * callers can set parentId on subsequent appends, mirroring how
 * YamlParser::buildWidget() constructs the real flat list.
 */
#include <QtTest>
#include "WidgetDump.h"
#include "WidgetDef.h"

/* ── Fixture helpers ─────────────────────────────────────────────────────── */

static int appendDisplay(QList<WidgetDef>& out, uint8_t id, int parentId = -1)
{
    WidgetDef w;
    w.keyPath  = QStringLiteral("temp");
    w.widgetId = id;
    w.type     = WidgetType::Display;
    w.label    = QStringLiteral("Temperature");
    w.parentId = parentId;
    w.props[QStringLiteral("unit")]   = QStringLiteral("C");
    w.props[QStringLiteral("format")] = QStringLiteral("%.1f");
    w.props[QStringLiteral("style")]  = QStringLiteral("large");
    out.append(w);
    return out.size() - 1;
}

static int appendLed(QList<WidgetDef>& out, uint8_t id, int parentId = -1)
{
    WidgetDef w;
    w.keyPath  = QStringLiteral("status");
    w.widgetId = id;
    w.type     = WidgetType::Led;
    w.label    = QStringLiteral("Status");
    w.parentId = parentId;
    w.props[QStringLiteral("color")] = QStringLiteral("#00d4aa");
    out.append(w);
    return out.size() - 1;
}

static int appendLabel(QList<WidgetDef>& out, int parentId = -1)
{
    WidgetDef w;
    w.keyPath  = QStringLiteral("hdg");
    w.widgetId = 0;
    w.type     = WidgetType::Label;
    w.parentId = parentId;
    w.props[QStringLiteral("text")]  = QStringLiteral("Section Heading");
    w.props[QStringLiteral("style")] = QStringLiteral("heading");
    out.append(w);
    return out.size() - 1;
}

static int appendButtonWithFaceChildren(QList<WidgetDef>& out, uint8_t btnId,
                                         uint8_t ledId, uint8_t dispId, int parentId = -1)
{
    WidgetDef w;
    w.keyPath  = QStringLiteral("power_btn");
    w.widgetId = btnId;
    w.type     = WidgetType::Button;
    w.label    = QStringLiteral("Power");
    w.parentId = parentId;
    w.props[QStringLiteral("shape")] = QStringLiteral("circle");
    out.append(w);
    int row = out.size() - 1;
    appendLed(out, ledId, row);
    appendDisplay(out, dispId, row);
    appendLabel(out, row);
    return row;
}

static int appendToggle(QList<WidgetDef>& out, uint8_t id, const QString& key, int parentId = -1)
{
    WidgetDef w;
    w.keyPath  = key;
    w.widgetId = id;
    w.type     = WidgetType::Toggle;
    w.label    = QStringLiteral("Toggle");
    w.parentId = parentId;
    out.append(w);
    return out.size() - 1;
}

static int appendRow(QList<WidgetDef>& out, const QString& key, int parentId = -1)
{
    WidgetDef w;
    w.keyPath  = key;
    w.widgetId = 0;
    w.type     = WidgetType::Row;
    w.parentId = parentId;
    out.append(w);
    return out.size() - 1;
}

static int appendGrid(QList<WidgetDef>& out, const QString& key, int columns, int parentId = -1)
{
    WidgetDef w;
    w.keyPath  = key;
    w.widgetId = 0;
    w.type     = WidgetType::Grid;
    w.parentId = parentId;
    w.props[QStringLiteral("columns")] = columns;
    out.append(w);
    return out.size() - 1;
}

static int appendSection(QList<WidgetDef>& out, const QString& key, bool collapsible, int parentId = -1)
{
    WidgetDef w;
    w.keyPath  = key;
    w.widgetId = 0;
    w.type     = WidgetType::Section;
    w.parentId = parentId;
    w.props[QStringLiteral("collapsible")] = collapsible;
    out.append(w);
    return out.size() - 1;
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
        QList<WidgetDef> widgets;
        appendDisplay(widgets, 0x10);
        const QString out = dumpWidgetTree(widgets, QStringLiteral("Dev"),
                                            QStringLiteral("1.0"), QStringLiteral("default"));
        QVERIFY(out.contains(QStringLiteral("[0x10] display")));
        QVERIFY(out.contains(QStringLiteral("unit: \"C\"")));
        QVERIFY(out.contains(QStringLiteral("format: \"%.1f\"")));
        QVERIFY(out.contains(QStringLiteral("style: \"large\"")));
    }

    void leafWidget_led_printsColor()
    {
        QList<WidgetDef> widgets;
        appendLed(widgets, 0x11);
        const QString out = dumpWidgetTree(widgets, QStringLiteral("Dev"),
                                            QStringLiteral("1.0"), QStringLiteral("default"));
        QVERIFY(out.contains(QStringLiteral("[0x11] led")));
        QVERIFY(out.contains(QStringLiteral("color: \"#00d4aa\"")));
    }

    void leafWidget_slider_printsMinMaxStepUnit()
    {
        WidgetDef w;
        w.keyPath  = QStringLiteral("rate");
        w.widgetId = 0x12;
        w.type     = WidgetType::Slider;
        w.props[QStringLiteral("min")]  = 1.0;
        w.props[QStringLiteral("max")]  = 100.0;
        w.props[QStringLiteral("step")] = 0.5;
        w.props[QStringLiteral("unit")] = QStringLiteral("Hz");

        const QString out = dumpWidgetTree({ w }, QStringLiteral("Dev"), QStringLiteral("1.0"),
                                            QStringLiteral("default"));
        QVERIFY(out.contains(QStringLiteral("min: 1")));
        QVERIFY(out.contains(QStringLiteral("max: 100")));
        QVERIFY(out.contains(QStringLiteral("step: 0.5")));
        QVERIFY(out.contains(QStringLiteral("unit: \"Hz\"")));
    }

    void leafWidget_text_printsModeAndDefaultModeSeparately()
    {
        WidgetDef w;
        w.keyPath  = QStringLiteral("ssid");
        w.widgetId = 0x13;
        w.type     = WidgetType::Text;
        w.props[QStringLiteral("mode")]        = QStringLiteral("readonly");
        w.props[QStringLiteral("defaultMode")] = QStringLiteral("rw");
        w.props[QStringLiteral("placeholder")] = QStringLiteral("Enter SSID");
        w.props[QStringLiteral("maxlength")]   = 32;

        const QString out = dumpWidgetTree({ w }, QStringLiteral("Dev"), QStringLiteral("1.0"),
                                            QStringLiteral("default"));
        QVERIFY(out.contains(QStringLiteral("mode: \"readonly\"")));
        QVERIFY(out.contains(QStringLiteral("defaultMode: \"rw\"")));
        QVERIFY(out.contains(QStringLiteral("placeholder: \"Enter SSID\"")));
        QVERIFY(out.contains(QStringLiteral("maxlength: 32")));
    }

    void leafWidget_dropdown_printsItems()
    {
        WidgetDef w;
        w.keyPath  = QStringLiteral("wifi_mode");
        w.widgetId = 0x14;
        w.type     = WidgetType::Dropdown;
        QVariantMap sta;
        sta[QStringLiteral("key")]   = QStringLiteral("sta");
        sta[QStringLiteral("label")] = QStringLiteral("Station");
        w.props[QStringLiteral("items")] = QVariantList{ sta };

        const QString out = dumpWidgetTree({ w }, QStringLiteral("Dev"), QStringLiteral("1.0"),
                                            QStringLiteral("default"));
        QVERIFY(out.contains(QStringLiteral("key=\"sta\" label=\"Station\"")));
    }

    void leafWidget_buttonGroup_printsLayoutAndItem()
    {
        QList<WidgetDef> widgets;
        WidgetDef w;
        w.keyPath  = QStringLiteral("mode_sel");
        w.widgetId = 0x15;
        w.type     = WidgetType::ButtonGroup;
        w.props[QStringLiteral("layout")] = QStringLiteral("grid");
        widgets.append(w);
        int groupRow = widgets.size() - 1;

        WidgetDef dc;
        dc.keyPath  = QStringLiteral("mode_sel.dc");
        dc.widgetId = 0x16;
        dc.type     = WidgetType::Button;
        dc.label    = QStringLiteral("DCV");
        dc.parentId = groupRow;
        dc.props[QStringLiteral("position")] = QStringLiteral("top");
        widgets.append(dc);

        const QString out = dumpWidgetTree(widgets, QStringLiteral("Dev"), QStringLiteral("1.0"),
                                            QStringLiteral("default"));
        QVERIFY(out.contains(QStringLiteral("layout: \"grid\"")));
        QVERIFY(out.contains(QStringLiteral("[0x16] button \"DCV\" keyPath=\"mode_sel.dc\"")));
        QVERIFY(out.contains(QStringLiteral("position: \"top\"")));
    }

    void leafWidget_valueAndDebugValue_printSeparately()
    {
        QList<WidgetDef> widgets;
        appendToggle(widgets, 0x17, QStringLiteral("relay"));
        widgets[0].value      = true;
        widgets[0].debugValue = false;

        const QString out = dumpWidgetTree(widgets, QStringLiteral("Dev"), QStringLiteral("1.0"),
                                            QStringLiteral("default"));
        QVERIFY(out.contains(QStringLiteral("value: true")));
        QVERIFY(out.contains(QStringLiteral("debugValue: false")));
    }

    void leafWidget_nullValueAndDebugValue_printAsNull()
    {
        QList<WidgetDef> widgets;
        appendToggle(widgets, 0x18, QStringLiteral("fan"));
        const QString out = dumpWidgetTree(widgets, QStringLiteral("Dev"), QStringLiteral("1.0"),
                                            QStringLiteral("default"));
        QVERIFY(out.contains(QStringLiteral("value: (null)")));
        QVERIFY(out.contains(QStringLiteral("debugValue: (null)")));
    }

    void container_row_withEmptyChildren_printsNoChildrenSection()
    {
        QList<WidgetDef> widgets;
        appendRow(widgets, QStringLiteral("empty_row"));
        const QString out = dumpWidgetTree(widgets, QStringLiteral("Dev"), QStringLiteral("1.0"),
                                            QStringLiteral("default"));
        QVERIFY(out.contains(QStringLiteral("row")));
        /* Deprecated key name — must never appear (see
         * buttonWithFaceChildren_printsLedDisplayAndLabelChildren's identical
         * check). Note "widgets:" itself always appears once, in the
         * unconditional device-header summary line ("widgets: (N
         * top-level)") — that's not what this asserts. */
        QVERIFY(!out.contains(QStringLiteral("children:")));
    }

    void container_grid_printsColumnsAndChildren()
    {
        QList<WidgetDef> widgets;
        int gridRow = appendGrid(widgets, QStringLiteral("panel"), 3);
        appendToggle(widgets, 0x19, QStringLiteral("a"), gridRow);
        appendToggle(widgets, 0x1a, QStringLiteral("b"), gridRow);

        const QString out = dumpWidgetTree(widgets, QStringLiteral("Dev"), QStringLiteral("1.0"),
                                            QStringLiteral("default"));
        QVERIFY(out.contains(QStringLiteral("columns: 3")));
        QVERIFY(out.contains(QStringLiteral("[0x19] toggle")));
        QVERIFY(out.contains(QStringLiteral("[0x1a] toggle")));
    }

    void nestedContainers_depth3_rendersFullyIndented()
    {
        QList<WidgetDef> widgets;
        int sectionRow = appendSection(widgets, QStringLiteral("outer"), true);
        int gridRow    = appendGrid(widgets, QStringLiteral("mid"), 2, sectionRow);
        int innerRow   = appendRow(widgets, QStringLiteral("inner"), gridRow);
        appendToggle(widgets, 0x1b, QStringLiteral("leaf"), innerRow);

        const QString out = dumpWidgetTree(widgets, QStringLiteral("Dev"), QStringLiteral("1.0"),
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
        QList<WidgetDef> widgets;
        appendButtonWithFaceChildren(widgets, 0x1c, 0x1d, 0x1e);

        const QString out = dumpWidgetTree(widgets, QStringLiteral("Dev"), QStringLiteral("1.0"),
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
