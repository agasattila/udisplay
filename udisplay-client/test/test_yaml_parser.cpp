/**
 * YamlParser tests.
 *
 * Critical property: widget ID assignment must exactly match udisplay-gen.
 * Canonical reference: tests/protocol_vectors.json → merkle.v5_full_vocabulary.
 */
#include <QtTest>
#include "YamlParser.h"
#include "WidgetDef.h"

/* ── Test YAML payloads ──────────────────────────────────────────────────── */

/* v1_tiny_single_chunk: device.name="t", one toggle "a" */
static const char* YAML_V1 =
    "device:\n"
    "  name: t\n"
    "widgets:\n"
    "  a:\n"
    "    type: toggle\n";

/* v5_full_vocabulary: 7 widget types, non-alphabetical declaration order.
 * Sorted alphabetical path order → ID assignment:
 *   0x10 display_volt
 *   0x11 fire_btn
 *   0x12 fire_btn.status_led
 *   0x13 mode_sel
 *   0x14 mode_sel.ac
 *   0x15 mode_sel.dc
 *   0x16 power_led
 *   0x17 slider_rate
 *   0x18 ssid_field
 *   0x19 toggle_relay
 */
static const char* YAML_V5 =
    "device:\n"
    "  name: full vocab test\n"
    "  version: '1.0'\n"
    "widgets:\n"
    "  slider_rate:\n"
    "    type: slider\n"
    "    label: Rate\n"
    "    min: 1\n"
    "    max: 100\n"
    "    step: 1\n"
    "    unit: Hz\n"
    "  toggle_relay:\n"
    "    type: toggle\n"
    "    label: Relay\n"
    "  fire_btn:\n"
    "    type: button\n"
    "    label: Fire\n"
    "    shape: circle\n"
    "    widgets:\n"
    "      status_led:\n"
    "        type: led\n"
    "        label: Active\n"
    "  display_volt:\n"
    "    type: display\n"
    "    label: Voltage\n"
    "    unit: V\n"
    "    format: '%.3f'\n"
    "    style: large\n"
    "  mode_sel:\n"
    "    type: button-group\n"
    "    layout: grid\n"
    "    items:\n"
    "      dc:\n"
    "        label: DCV\n"
    "      ac:\n"
    "        label: ACV\n"
    "  power_led:\n"
    "    type: led\n"
    "    label: Power\n"
    "  ssid_field:\n"
    "    type: text\n"
    "    label: SSID\n"
    "    mode: rw\n";

/* ─────────────────────────────────────────────────────────────────────────── */

class TestYamlParser : public QObject
{
    Q_OBJECT

private:
    static const WidgetDef* findByKey(const QList<WidgetDef>& list, const char* key)
    {
        QString k = QString::fromLatin1(key);
        for (const auto& w : list)
            if (w.keyPath == k) return &w;
        return nullptr;
    }

private slots:

    /* ── v1: minimal single-widget YAML ──────────────────────────── */

    void v1_deviceName()
    {
        YamlParser p;
        QList<WidgetDef> widgets;
        QString name, version;
        QVERIFY(p.parse(YAML_V1, widgets, name, version));
        QCOMPARE(name, QStringLiteral("t"));
    }

    void v1_singleToggle_assignsId0x10()
    {
        YamlParser p;
        QList<WidgetDef> widgets;
        QString name, version;
        QVERIFY(p.parse(YAML_V1, widgets, name, version));
        QCOMPARE(widgets.size(), 1);

        const WidgetDef& w = widgets[0];
        QCOMPARE(w.keyPath,  QStringLiteral("a"));
        QCOMPARE(w.widgetId, uint8_t(0x10));
        QCOMPARE(w.type,     WidgetType::Toggle);
    }

    /* ── v5: full vocabulary — widget ID assignment ───────────────── */

    /**
     * These 10 IDs must exactly match udisplay-gen's widget_ids.assign().
     * Reference: protocol_vectors.json → merkle.v5_full_vocabulary.widget_ids
     */
    void v5_topLevel_widgetIds_matchCodegen()
    {
        YamlParser p;
        QList<WidgetDef> widgets;
        QString name, version;
        QVERIFY(p.parse(YAML_V5, widgets, name, version));
        QCOMPARE(widgets.size(), 7);

        struct { const char* key; uint8_t id; } expected[] = {
            { "display_volt",  0x10 },
            { "fire_btn",      0x11 },
            { "mode_sel",      0x13 },
            { "power_led",     0x16 },
            { "slider_rate",   0x17 },
            { "ssid_field",    0x18 },
            { "toggle_relay",  0x19 },
        };
        for (auto& e : expected) {
            const WidgetDef* w = findByKey(widgets, e.key);
            QVERIFY2(w, e.key);
            QCOMPARE(w->widgetId, e.id);
        }
    }

    void v5_childIds_matchCodegen()
    {
        YamlParser p;
        QList<WidgetDef> widgets;
        QString name, version;
        QVERIFY(p.parse(YAML_V5, widgets, name, version));

        /* fire_btn child: status_led → 0x12 */
        const WidgetDef* fb = findByKey(widgets, "fire_btn");
        QVERIFY(fb);
        QCOMPARE(fb->children.size(), 1);
        QCOMPARE(fb->children[0].keyPath,  QStringLiteral("fire_btn.status_led"));
        QCOMPARE(fb->children[0].widgetId, uint8_t(0x12));
        QCOMPARE(fb->children[0].label,    QStringLiteral("Active"));

        /* mode_sel items: ac → 0x14, dc → 0x15 */
        const WidgetDef* ms = findByKey(widgets, "mode_sel");
        QVERIFY(ms);
        QCOMPARE(ms->groupItems.size(), 2);

        bool foundAc = false, foundDc = false;
        for (const auto& item : ms->groupItems) {
            if (item.keyPath == "mode_sel.ac") {
                QCOMPARE(item.widgetId, uint8_t(0x14));
                QCOMPARE(item.label, QStringLiteral("ACV"));
                foundAc = true;
            } else if (item.keyPath == "mode_sel.dc") {
                QCOMPARE(item.widgetId, uint8_t(0x15));
                QCOMPARE(item.label, QStringLiteral("DCV"));
                foundDc = true;
            }
        }
        QVERIFY(foundAc);
        QVERIFY(foundDc);
    }

    /* ── v5: widget type and property parsing ─────────────────────── */

    void v5_types_correct()
    {
        YamlParser p;
        QList<WidgetDef> widgets;
        QString name, version;
        QVERIFY(p.parse(YAML_V5, widgets, name, version));

        QCOMPARE(findByKey(widgets, "display_volt")->type,  WidgetType::Display);
        QCOMPARE(findByKey(widgets, "fire_btn")->type,      WidgetType::Button);
        QCOMPARE(findByKey(widgets, "mode_sel")->type,      WidgetType::ButtonGroup);
        QCOMPARE(findByKey(widgets, "power_led")->type,     WidgetType::Led);
        QCOMPARE(findByKey(widgets, "slider_rate")->type,   WidgetType::Slider);
        QCOMPARE(findByKey(widgets, "ssid_field")->type,    WidgetType::Text);
        QCOMPARE(findByKey(widgets, "toggle_relay")->type,  WidgetType::Toggle);
    }

    void v5_display_props()
    {
        YamlParser p;
        QList<WidgetDef> widgets;
        QString name, version;
        QVERIFY(p.parse(YAML_V5, widgets, name, version));

        const WidgetDef* dv = findByKey(widgets, "display_volt");
        QVERIFY(dv);
        QCOMPARE(dv->label,  QStringLiteral("Voltage"));
        QCOMPARE(dv->unit,   QStringLiteral("V"));
        QCOMPARE(dv->format, QStringLiteral("%.3f"));
        QCOMPARE(dv->displayStyle,  QStringLiteral("large"));
    }

    void v5_slider_props()
    {
        YamlParser p;
        QList<WidgetDef> widgets;
        QString name, version;
        QVERIFY(p.parse(YAML_V5, widgets, name, version));

        const WidgetDef* sr = findByKey(widgets, "slider_rate");
        QVERIFY(sr);
        QCOMPARE(sr->label,      QStringLiteral("Rate"));
        QCOMPARE(sr->sliderMin,  1.0);
        QCOMPARE(sr->sliderMax,  100.0);
        QCOMPARE(sr->sliderStep, 1.0);
        QCOMPARE(sr->unit,       QStringLiteral("Hz"));
    }

    void v5_text_props()
    {
        YamlParser p;
        QList<WidgetDef> widgets;
        QString name, version;
        QVERIFY(p.parse(YAML_V5, widgets, name, version));

        const WidgetDef* sf = findByKey(widgets, "ssid_field");
        QVERIFY(sf);
        QCOMPARE(sf->textMode, QStringLiteral("rw"));
    }

    void v5_button_props()
    {
        YamlParser p;
        QList<WidgetDef> widgets;
        QString name, version;
        QVERIFY(p.parse(YAML_V5, widgets, name, version));

        const WidgetDef* fb = findByKey(widgets, "fire_btn");
        QVERIFY(fb);
        QCOMPARE(fb->shape, QStringLiteral("circle"));
        /* button no longer stores per-widget color — style comes from global stylesheet */
        QVERIFY(fb->color.isEmpty());
    }

    void v5_deviceNameVersion()
    {
        YamlParser p;
        QList<WidgetDef> widgets;
        QString name, version;
        QVERIFY(p.parse(YAML_V5, widgets, name, version));
        QCOMPARE(name,    QStringLiteral("full vocab test"));
        QCOMPARE(version, QStringLiteral("1.0"));
    }

    /* ── Default values ───────────────────────────────────────────── */

    void defaults_enabled_and_visible()
    {
        YamlParser p;
        QList<WidgetDef> widgets;
        QString name, version;
        QVERIFY(p.parse(YAML_V1, widgets, name, version));
        QVERIFY(widgets[0].enabled);
        QVERIFY(widgets[0].visible);
    }

    void display_defaultFormat()
    {
        YamlParser p;
        QList<WidgetDef> widgets;
        QString name, version;
        const char* yaml =
            "widgets:\n"
            "  temp:\n"
            "    type: display\n"
            "    label: Temp\n";
        QVERIFY(p.parse(yaml, widgets, name, version));
        QCOMPARE(widgets[0].format, QStringLiteral("%.2f"));
        QCOMPARE(widgets[0].displayStyle,  QStringLiteral("default"));
    }

    /* ── Error handling ───────────────────────────────────────────── */

    void noWidgetsBlock_fails()
    {
        YamlParser p;
        QList<WidgetDef> widgets;
        QString name, version;
        QVERIFY(!p.parse("device:\n  name: test\n", widgets, name, version));
        QVERIFY(!p.errorString().isEmpty());
    }

    void unknownWidgetType_failsParse()
    {
        YamlParser p;
        QList<WidgetDef> widgets;
        QString name, version;
        const char* yaml =
            "widgets:\n"
            "  foo:\n"
            "    type: frobnitz\n"
            "    label: Foo\n";
        QVERIFY(!p.parse(yaml, widgets, name, version));
        QVERIFY(!p.errorString().isEmpty());
    }

    /* ── Multiple widgets: declaration order preserved ──────────── */

    void declarationOrder_preserved()
    {
        /* top-level widgets come out in YAML declaration order, not sorted */
        YamlParser p;
        QList<WidgetDef> widgets;
        QString name, version;
        QVERIFY(p.parse(YAML_V5, widgets, name, version));

        /* Declaration order: slider_rate, toggle_relay, fire_btn,
         * display_volt, mode_sel, power_led, ssid_field             */
        QCOMPARE(widgets[0].keyPath, QStringLiteral("slider_rate"));
        QCOMPARE(widgets[1].keyPath, QStringLiteral("toggle_relay"));
        QCOMPARE(widgets[2].keyPath, QStringLiteral("fire_btn"));
        QCOMPARE(widgets[3].keyPath, QStringLiteral("display_volt"));
        QCOMPARE(widgets[4].keyPath, QStringLiteral("mode_sel"));
        QCOMPARE(widgets[5].keyPath, QStringLiteral("power_led"));
        QCOMPARE(widgets[6].keyPath, QStringLiteral("ssid_field"));
    }

    /* ── Robustness / error paths ─────────────────────────────────── */

    /* >240 widget paths must be rejected (uint8_t ID space 0x10-0xFF = 240) */
    void tooManyWidgets_fails()
    {
        /* Build a YAML with 241 top-level widgets named w000..w240 */
        QByteArray yaml = "widgets:\n";
        for (int i = 0; i <= 240; ++i) {
            char buf[32];
            snprintf(buf, sizeof(buf), "  w%03d:\n    type: toggle\n", i);
            yaml += buf;
        }
        YamlParser p;
        QList<WidgetDef> widgets;
        QString name, version;
        QVERIFY(!p.parse(yaml, widgets, name, version));
        QVERIFY(!p.errorString().isEmpty());
    }

    /* m_error must be cleared when a subsequent parse succeeds */
    void reuseAfterFailure_clearsError()
    {
        YamlParser p;
        QList<WidgetDef> widgets;
        QString name, version;
        /* first parse: fail */
        QVERIFY(!p.parse("device:\n  name: x\n", widgets, name, version));
        QVERIFY(!p.errorString().isEmpty());
        /* second parse: succeed */
        QVERIFY(p.parse(YAML_V1, widgets, name, version));
        QVERIFY(p.errorString().isEmpty());
    }

    /* deviceNameOut must be cleared on a YAML that has no device block */
    void noDeviceBlock_clearsDeviceName()
    {
        YamlParser p;
        QList<WidgetDef> widgets;
        QString name = QStringLiteral("stale"), version = QStringLiteral("old");
        const char* yaml = "widgets:\n  a:\n    type: toggle\n";
        QVERIFY(p.parse(yaml, widgets, name, version));
        QVERIFY(name.isEmpty());
        QVERIFY(version.isEmpty());
    }

    /* Truly malformed YAML triggers a YAML::Exception which must be caught
     * and reported as a parse failure — not a crash. */
    void malformedYaml_returnsError()
    {
        YamlParser p;
        QList<WidgetDef> widgets;
        QString name, version;
        /* The colon with no value followed by a mapping key is a YAML error. */
        const char* bad = ":\n  - bad: [unclosed\n";
        QVERIFY(!p.parse(bad, widgets, name, version));
        QVERIFY(!p.errorString().isEmpty());
    }

    /* ── New widget types: section ────────────────────────────────── */

    /* A section header appears in the widget list with type Section and
     * widgetId=0 (no protocol exchange). Its children follow immediately. */
    void section_headerAndChildrenFlattened()
    {
        const char* yaml =
            "widgets:\n"
            "  controls:\n"
            "    type: section\n"
            "    label: Controls\n"
            "    widgets:\n"
            "      relay:\n"
            "        type: toggle\n"
            "        label: Relay\n"
            "      fan:\n"
            "        type: toggle\n"
            "        label: Fan\n";
        YamlParser p;
        QList<WidgetDef> widgets;
        QString name, version;
        QVERIFY(p.parse(yaml, widgets, name, version));
        /* Output: section header, then two children — 3 entries */
        QCOMPARE(widgets.size(), 3);
        QCOMPARE(widgets[0].type,     WidgetType::Section);
        QCOMPARE(widgets[0].widgetId, uint8_t(0));
        QCOMPARE(widgets[0].label,    QStringLiteral("Controls"));
        QCOMPARE(widgets[1].keyPath,  QStringLiteral("relay"));
        QCOMPARE(widgets[1].type,     WidgetType::Toggle);
        QCOMPARE(widgets[2].keyPath,  QStringLiteral("fan"));
        QCOMPARE(widgets[2].type,     WidgetType::Toggle);
    }

    /* Section children are transparent in ID assignment: children get IDs
     * as if they were top-level (sorted by plain key, not "section.key"). */
    void section_childrenGetTopLevelIds()
    {
        /* fan (0x10), relay (0x11) — alphabetical, no section prefix */
        const char* yaml =
            "widgets:\n"
            "  group:\n"
            "    type: section\n"
            "    widgets:\n"
            "      relay:\n"
            "        type: toggle\n"
            "      fan:\n"
            "        type: toggle\n";
        YamlParser p;
        QList<WidgetDef> widgets;
        QString name, version;
        QVERIFY(p.parse(yaml, widgets, name, version));
        /* fan < relay alphabetically → 0x10, relay → 0x11 */
        const WidgetDef* fan   = findByKey(widgets, "fan");
        const WidgetDef* relay = findByKey(widgets, "relay");
        QVERIFY(fan);
        QVERIFY(relay);
        QCOMPARE(fan->widgetId,   uint8_t(0x10));
        QCOMPARE(relay->widgetId, uint8_t(0x11));
    }

    /* ── New widget types: separator and label ────────────────────── */

    /* Separator and label are decorations: widgetId=0, skipped in ID assignment */
    void separator_widgetId_zero()
    {
        const char* yaml =
            "widgets:\n"
            "  divider:\n"
            "    type: separator\n";
        YamlParser p;
        QList<WidgetDef> widgets;
        QString name, version;
        QVERIFY(p.parse(yaml, widgets, name, version));
        QCOMPARE(widgets.size(), 1);
        QCOMPARE(widgets[0].type,     WidgetType::Separator);
        QCOMPARE(widgets[0].widgetId, uint8_t(0));
    }

    void label_widgetId_zero_and_props()
    {
        const char* yaml =
            "widgets:\n"
            "  title:\n"
            "    type: label\n"
            "    text: Hello world\n"
            "    style: heading\n";
        YamlParser p;
        QList<WidgetDef> widgets;
        QString name, version;
        QVERIFY(p.parse(yaml, widgets, name, version));
        QCOMPARE(widgets.size(), 1);
        QCOMPARE(widgets[0].type,       WidgetType::Label);
        QCOMPARE(widgets[0].widgetId,   uint8_t(0));
        QCOMPARE(widgets[0].labelText,  QStringLiteral("Hello world"));
        QCOMPARE(widgets[0].labelStyle, QStringLiteral("heading"));
    }

    /* Decorations are transparent to ID assignment: a toggle after a
     * separator still gets 0x10 (separator does not consume an ID). */
    void decorations_transparent_to_id_assignment()
    {
        const char* yaml =
            "widgets:\n"
            "  div:\n"
            "    type: separator\n"
            "  lbl:\n"
            "    type: label\n"
            "    text: x\n"
            "  relay:\n"
            "    type: toggle\n";
        YamlParser p;
        QList<WidgetDef> widgets;
        QString name, version;
        QVERIFY(p.parse(yaml, widgets, name, version));
        /* 3 entries; relay gets 0x10 (first and only ID consumer) */
        QCOMPARE(widgets.size(), 3);
        const WidgetDef* relay = findByKey(widgets, "relay");
        QVERIFY(relay);
        QCOMPARE(relay->widgetId, uint8_t(0x10));
    }

    /* ── New widget types: dropdown ───────────────────────────────── */

    void dropdown_items_populated()
    {
        const char* yaml =
            "widgets:\n"
            "  mode:\n"
            "    type: dropdown\n"
            "    label: Mode\n"
            "    items:\n"
            "      sta: Station\n"
            "      ap: Access Point\n";
        YamlParser p;
        QList<WidgetDef> widgets;
        QString name, version;
        QVERIFY(p.parse(yaml, widgets, name, version));
        QCOMPARE(widgets.size(), 1);
        const WidgetDef& w = widgets[0];
        QCOMPARE(w.type,            WidgetType::Dropdown);
        QCOMPARE(w.widgetId,        uint8_t(0x10));
        QCOMPARE(w.dropdownItems.size(), 2);
        QCOMPARE(w.dropdownItems[0].key,   QStringLiteral("sta"));
        QCOMPARE(w.dropdownItems[0].label, QStringLiteral("Station"));
        QCOMPARE(w.dropdownItems[1].key,   QStringLiteral("ap"));
        QCOMPARE(w.dropdownItems[1].label, QStringLiteral("Access Point"));
    }

    /* Dropdown items do NOT get their own widget IDs */
    void dropdown_items_have_no_ids()
    {
        /* Only the dropdown itself gets 0x10; items are value-only */
        const char* yaml =
            "widgets:\n"
            "  sel:\n"
            "    type: dropdown\n"
            "    items:\n"
            "      a: A\n"
            "      b: B\n"
            "      c: C\n";
        YamlParser p;
        QList<WidgetDef> widgets;
        QString name, version;
        QVERIFY(p.parse(yaml, widgets, name, version));
        /* Only 1 widget (the dropdown), not 4 */
        QCOMPARE(widgets.size(), 1);
        QCOMPARE(widgets[0].widgetId, uint8_t(0x10));
    }

    /* ── New widget types: row and grid ───────────────────────────── */

    void row_children_embedded()
    {
        const char* yaml =
            "widgets:\n"
            "  ctrl_row:\n"
            "    type: row\n"
            "    widgets:\n"
            "      relay:\n"
            "        type: toggle\n"
            "        label: Relay\n"
            "        flex: 1\n"
            "      fan:\n"
            "        type: toggle\n"
            "        label: Fan\n";
        YamlParser p;
        QList<WidgetDef> widgets;
        QString name, version;
        QVERIFY(p.parse(yaml, widgets, name, version));
        /* Row appears as a single top-level entry (children NOT in flat list) */
        QCOMPARE(widgets.size(), 1);
        QCOMPARE(widgets[0].type,     WidgetType::Row);
        QCOMPARE(widgets[0].widgetId, uint8_t(0));
        QCOMPARE(widgets[0].children.size(), 2);

        const WidgetDef& relay = widgets[0].children[0];
        QCOMPARE(relay.keyPath, QStringLiteral("relay"));
        QCOMPARE(relay.type,    WidgetType::Toggle);
        QCOMPARE(relay.flex,    1);

        const WidgetDef& fan = widgets[0].children[1];
        QCOMPARE(fan.keyPath, QStringLiteral("fan"));
        QCOMPARE(fan.flex,    0);
    }

    void row_flex_invalidValue_warnsAndClampsToOne()
    {
        const char* yaml =
            "widgets:\n"
            "  ctrl_row:\n"
            "    type: row\n"
            "    widgets:\n"
            "      relay:\n"
            "        type: toggle\n"
            "        flex: -3\n";
        YamlParser p;
        QList<WidgetDef> widgets;
        QString name, version;
        QVERIFY(p.parse(yaml, widgets, name, version));
        QCOMPARE(widgets[0].children[0].flex, 1);
        bool warned = false;
        for (const auto& d : p.diagnostics())
            if (d.field == "flex" && d.severity == YamlParser::Severity::Warning) warned = true;
        QVERIFY(warned);
    }

    void row_align_defaultsToLeft()
    {
        const char* yaml =
            "widgets:\n"
            "  ctrl_row:\n"
            "    type: row\n"
            "    widgets:\n"
            "      relay:\n"
            "        type: toggle\n";
        YamlParser p;
        QList<WidgetDef> widgets;
        QString name, version;
        QVERIFY(p.parse(yaml, widgets, name, version));
        QCOMPARE(widgets[0].align, QStringLiteral("left"));
        /* Child didn't declare its own align — always resolves to "left"
         * (its own default), never inherits the container's align. A
         * child's align is independent of its container's; there is no
         * tri-state "unspecified" sentinel. */
        QCOMPARE(widgets[0].children[0].align, QStringLiteral("left"));
    }

    void row_align_containerAndChildOverride()
    {
        const char* yaml =
            "widgets:\n"
            "  ctrl_row:\n"
            "    type: row\n"
            "    align: center\n"
            "    widgets:\n"
            "      relay:\n"
            "        type: toggle\n"
            "        align: right\n"
            "      fan:\n"
            "        type: toggle\n";
        YamlParser p;
        QList<WidgetDef> widgets;
        QString name, version;
        QVERIFY(p.parse(yaml, widgets, name, version));
        QCOMPARE(widgets[0].align, QStringLiteral("center"));
        QCOMPARE(widgets[0].children[0].align, QStringLiteral("right")); /* relay: own override */
        /* fan didn't declare its own align — resolves to "left", NOT the
         * container's "center". A child's align never inherits from its
         * container. */
        QCOMPARE(widgets[0].children[1].align, QStringLiteral("left"));
    }

    void row_align_invalidValue_warnsAndDefaultsToLeft()
    {
        const char* yaml =
            "widgets:\n"
            "  ctrl_row:\n"
            "    type: row\n"
            "    align: sideways\n"
            "    widgets:\n"
            "      relay:\n"
            "        type: toggle\n";
        YamlParser p;
        QList<WidgetDef> widgets;
        QString name, version;
        QVERIFY(p.parse(yaml, widgets, name, version));
        QCOMPARE(widgets[0].align, QStringLiteral("left"));
        bool warned = false;
        for (const auto& d : p.diagnostics())
            if (d.field == "align" && d.severity == YamlParser::Severity::Warning) warned = true;
        QVERIFY(warned);
    }

    void label_align_parsedAndDefaultsToLeft()
    {
        /* A label's own text alignment is parsed from textAlign: (align: is
         * reserved for row/grid position — see the eefe866 rename). */
        const char* yaml =
            "widgets:\n"
            "  a:\n"
            "    type: label\n"
            "    text: Hi\n"
            "    textAlign: justify\n"
            "  b:\n"
            "    type: label\n"
            "    text: Bye\n";
        YamlParser p;
        QList<WidgetDef> widgets;
        QString name, version;
        QVERIFY(p.parse(yaml, widgets, name, version));
        QCOMPARE(widgets[0].labelAlign, QStringLiteral("justify"));
        QCOMPARE(widgets[1].labelAlign, QStringLiteral("left"));
    }

    void label_align_insideRow_doesNotWarnOnJustify()
    {
        /* A label's own text alignment (4-value enum incl. justify) is
         * parsed from textAlign:, a key entirely separate from a row/grid
         * child's position align: (3-value, left/right/center) — so a
         * label using "justify" for its own text as a row child cannot
         * collide with the row-position align validation at all. */
        const char* yaml =
            "widgets:\n"
            "  ctrl_row:\n"
            "    type: row\n"
            "    widgets:\n"
            "      caption:\n"
            "        type: label\n"
            "        text: Hi\n"
            "        textAlign: justify\n";
        YamlParser p;
        QList<WidgetDef> widgets;
        QString name, version;
        QVERIFY(p.parse(yaml, widgets, name, version));
        QCOMPARE(widgets[0].children[0].labelAlign, QStringLiteral("justify"));
        for (const auto& d : p.diagnostics())
            QVERIFY2(!(d.field == "align" && d.severity == YamlParser::Severity::Warning),
                     "label's own justify textAlign must not warn as an invalid row-align value");
    }

    void row_childrenGetIds_transparentToContainer()
    {
        /* fan (0x10), relay (0x11) — sorted alphabetically as top-level */
        const char* yaml =
            "widgets:\n"
            "  r:\n"
            "    type: row\n"
            "    widgets:\n"
            "      relay:\n"
            "        type: toggle\n"
            "      fan:\n"
            "        type: toggle\n";
        YamlParser p;
        QList<WidgetDef> widgets;
        QString name, version;
        QVERIFY(p.parse(yaml, widgets, name, version));
        QCOMPARE(widgets.size(), 1);
        /* fan < relay → IDs 0x10, 0x11 */
        bool hasFan = false, hasRelay = false;
        for (const auto& child : widgets[0].children) {
            if (child.keyPath == "fan")   { QCOMPARE(child.widgetId, uint8_t(0x10)); hasFan = true; }
            if (child.keyPath == "relay") { QCOMPARE(child.widgetId, uint8_t(0x11)); hasRelay = true; }
        }
        QVERIFY(hasFan);
        QVERIFY(hasRelay);
    }

    void grid_columns_and_children()
    {
        const char* yaml =
            "widgets:\n"
            "  g:\n"
            "    type: grid\n"
            "    columns: 3\n"
            "    widgets:\n"
            "      a:\n"
            "        type: toggle\n"
            "      b:\n"
            "        type: toggle\n";
        YamlParser p;
        QList<WidgetDef> widgets;
        QString name, version;
        QVERIFY(p.parse(yaml, widgets, name, version));
        QCOMPARE(widgets.size(), 1);
        QCOMPARE(widgets[0].type,       WidgetType::Grid);
        QCOMPARE(widgets[0].gridColumns, 3);
        QCOMPARE(widgets[0].children.size(), 2);
    }

    /* Row nested inside row: outer row → inner row → leaf widgets.
     * Regression test for buildTopLevelWidget missing Row/Grid case —
     * nested rows parsed with empty children list (invisible in QML). */
    void row_inside_row_children_parsed()
    {
        const char* yaml =
            "widgets:\n"
            "  outer:\n"
            "    type: row\n"
            "    widgets:\n"
            "      inner:\n"
            "        type: row\n"
            "        widgets:\n"
            "          relay:\n"
            "            type: toggle\n"
            "            label: Relay\n"
            "            flex: 1\n"
            "      right:\n"
            "        type: display\n"
            "        label: Val\n"
            "        flex: 1\n";
        YamlParser p;
        QList<WidgetDef> widgets;
        QString name, version;
        QVERIFY(p.parse(yaml, widgets, name, version));

        QCOMPARE(widgets.size(), 1);
        QCOMPARE(widgets[0].type, WidgetType::Row);
        QCOMPARE(widgets[0].children.size(), 2);

        /* inner row — first child (YAML document order) */
        const WidgetDef& inner = widgets[0].children[0];
        QCOMPARE(inner.type, WidgetType::Row);
        QCOMPARE(inner.children.size(), 1);
        QCOMPARE(inner.children[0].type, WidgetType::Toggle);
        QCOMPARE(inner.children[0].flex, 1);

        /* right display — second child */
        const WidgetDef& right = widgets[0].children[1];
        QCOMPARE(right.type, WidgetType::Display);
        QCOMPARE(right.flex, 1);
    }

    /* Row nested inside grid (cross-type, depth-2). */
    void row_inside_grid_children_parsed()
    {
        const char* yaml =
            "widgets:\n"
            "  g:\n"
            "    type: grid\n"
            "    columns: 2\n"
            "    widgets:\n"
            "      inner_row:\n"
            "        type: row\n"
            "        widgets:\n"
            "          led1:\n"
            "            type: led\n"
            "            label: A\n"
            "      other:\n"
            "        type: toggle\n";
        YamlParser p;
        QList<WidgetDef> widgets;
        QString name, version;
        QVERIFY(p.parse(yaml, widgets, name, version));

        QCOMPARE(widgets.size(), 1);
        QCOMPARE(widgets[0].type, WidgetType::Grid);
        QCOMPARE(widgets[0].gridColumns, 2);
        QCOMPARE(widgets[0].children.size(), 2);

        const WidgetDef& inner_row = widgets[0].children[0];
        QCOMPARE(inner_row.type, WidgetType::Row);
        QCOMPARE(inner_row.children.size(), 1);
        QCOMPARE(inner_row.children[0].type, WidgetType::Led);
    }

    /* ── capabilities (v5+) ───────────────────────────────────────── */

    void capabilities_parsed()
    {
        const char* yaml =
            "device:\n"
            "  name: testdev\n"
            "  capabilities:\n"
            "    - ble\n"
            "    - wifi\n"
            "widgets:\n"
            "  a:\n"
            "    type: toggle\n";
        YamlParser p;
        QList<WidgetDef> widgets;
        QString name, version;
        QStringList caps;
        QVERIFY(p.parse(yaml, widgets, name, version, caps));
        QCOMPARE(caps.size(), 2);
        QVERIFY(caps.contains(QStringLiteral("ble")));
        QVERIFY(caps.contains(QStringLiteral("wifi")));
    }

    /* capabilities absent → empty list (device connects without being gated) */
    void capabilities_absent_yields_empty_list()
    {
        const char* yaml =
            "device:\n"
            "  name: testdev\n"
            "widgets:\n"
            "  a:\n"
            "    type: toggle\n";
        YamlParser p;
        QList<WidgetDef> widgets;
        QString name, version;
        QStringList caps;
        QVERIFY(p.parse(yaml, widgets, name, version, caps));
        QVERIFY(caps.isEmpty());
    }

    /* capabilities present → non-empty list (DeviceController will gate this) */
    void capabilities_present_returns_strings()
    {
        const char* yaml =
            "device:\n"
            "  name: testdev\n"
            "  capabilities:\n"
            "    - some_future_cap\n"
            "widgets:\n"
            "  a:\n"
            "    type: toggle\n";
        YamlParser p;
        QList<WidgetDef> widgets;
        QString name, version;
        QStringList caps;
        QVERIFY(p.parse(yaml, widgets, name, version, caps));
        QCOMPARE(caps.size(), 1);
        QCOMPARE(caps.at(0), QStringLiteral("some_future_cap"));
    }

    /* ── Collapsible sections ─────────────────────────────────────── */

    void section_collapsible_parsed()
    {
        const char* yaml =
            "widgets:\n"
            "  controls:\n"
            "    type: section\n"
            "    label: Controls\n"
            "    collapsible: true\n"
            "    widgets:\n"
            "      relay:\n"
            "        type: toggle\n";
        YamlParser p;
        QList<WidgetDef> widgets;
        QString name, version;
        QVERIFY(p.parse(yaml, widgets, name, version));
        QVERIFY(widgets[0].collapsible);
    }

    void section_children_get_sectionOwnerRow()
    {
        const char* yaml =
            "widgets:\n"
            "  controls:\n"
            "    type: section\n"
            "    label: Controls\n"
            "    collapsible: true\n"
            "    widgets:\n"
            "      relay:\n"
            "        type: toggle\n"
            "      fan:\n"
            "        type: toggle\n";
        YamlParser p;
        QList<WidgetDef> widgets;
        QString name, version;
        QVERIFY(p.parse(yaml, widgets, name, version));
        /* widgets[0] = section header at row 0 */
        QCOMPARE(widgets[0].sectionOwnerRow, -1);
        /* widgets[1] and [2] are children — should point back to row 0 */
        QCOMPARE(widgets[1].sectionOwnerRow, 0);
        QCOMPARE(widgets[2].sectionOwnerRow, 0);
    }

    void section_non_collapsible_children_have_no_sectionOwnerRow()
    {
        const char* yaml =
            "widgets:\n"
            "  info:\n"
            "    type: section\n"
            "    label: Info\n"
            "    widgets:\n"
            "      relay:\n"
            "        type: toggle\n";
        YamlParser p;
        QList<WidgetDef> widgets;
        QString name, version;
        QVERIFY(p.parse(yaml, widgets, name, version));
        /* Non-collapsible section: children keep sectionOwnerRow == -1 */
        QCOMPARE(widgets[0].collapsible, false);
        QCOMPARE(widgets[1].sectionOwnerRow, -1);
    }

    /* ── TODO-020: LED color property ────────────────────────────────── */

    void led_colorParsed()
    {
        const char* yaml =
            "widgets:\n"
            "  status:\n"
            "    type: led\n"
            "    label: Status\n"
            "    color: '#ff0000'\n";
        YamlParser p;
        QList<WidgetDef> widgets;
        QString name, version;
        QVERIFY(p.parse(yaml, widgets, name, version));
        QCOMPARE(widgets.size(), 1);
        QCOMPARE(widgets[0].type,  WidgetType::Led);
        QCOMPARE(widgets[0].color, QStringLiteral("#ff0000"));
    }

    void led_colorDefault()
    {
        const char* yaml =
            "widgets:\n"
            "  status:\n"
            "    type: led\n"
            "    label: Status\n";
        YamlParser p;
        QList<WidgetDef> widgets;
        QString name, version;
        QVERIFY(p.parse(yaml, widgets, name, version));
        QCOMPARE(widgets.size(), 1);
        QCOMPARE(widgets[0].color, QStringLiteral("#00d4aa"));
    }

    /* ── TODO-021: rgbled widget type ────────────────────────────────── */

    void rgbled_typeRecognized()
    {
        const char* yaml =
            "widgets:\n"
            "  rgb:\n"
            "    type: rgbled\n"
            "    label: RGB Status\n";
        YamlParser p;
        QList<WidgetDef> widgets;
        QString name, version;
        QVERIFY(p.parse(yaml, widgets, name, version));
        QCOMPARE(widgets.size(), 1);
        QCOMPARE(widgets[0].type,     WidgetType::RgbLed);
        QCOMPARE(widgets[0].widgetId, uint8_t(0x10));
        QCOMPARE(widgets[0].label,    QStringLiteral("RGB Status"));
    }
    /* ── Global stylesheet (TODO-025) ───────────────────────────────── */

    void style_absent_yields_default_token()
    {
        YamlParser p;
        QList<WidgetDef> widgets;
        QString name, version;
        QStringList caps;
        QMap<QString, StyleToken> styles;
        QVERIFY(p.parse(YAML_V1, widgets, name, version, caps, styles));
        QVERIFY(styles.contains(QStringLiteral("default")));
        QCOMPARE(styles[QStringLiteral("default")].accent, QStringLiteral("#00d4aa"));
    }

    void style_default_overrides_cpp_defaults()
    {
        const char* yaml =
            "device:\n"
            "  name: t\n"
            "style:\n"
            "  default:\n"
            "    accent: '#ff0000'\n"
            "widgets:\n"
            "  a:\n"
            "    type: toggle\n";
        YamlParser p;
        QList<WidgetDef> widgets;
        QString name, version;
        QStringList caps;
        QMap<QString, StyleToken> styles;
        QVERIFY(p.parse(yaml, widgets, name, version, caps, styles));
        QCOMPARE(styles[QStringLiteral("default")].accent, QStringLiteral("#ff0000"));
        /* unset tokens still have C++ default */
        QCOMPARE(styles[QStringLiteral("default")].background, QStringLiteral("#0d0d1a"));
    }

    void style_named_theme_inherits_from_default()
    {
        const char* yaml =
            "device:\n"
            "  name: t\n"
            "style:\n"
            "  default:\n"
            "    accent: '#ff0000'\n"
            "  dark:\n"
            "    background: '#000000'\n"
            "widgets:\n"
            "  a:\n"
            "    type: toggle\n";
        YamlParser p;
        QList<WidgetDef> widgets;
        QString name, version;
        QStringList caps;
        QMap<QString, StyleToken> styles;
        QVERIFY(p.parse(yaml, widgets, name, version, caps, styles));
        QVERIFY(styles.contains(QStringLiteral("dark")));
        /* dark inherits accent from default */
        QCOMPARE(styles[QStringLiteral("dark")].accent, QStringLiteral("#ff0000"));
        /* dark overrides background */
        QCOMPARE(styles[QStringLiteral("dark")].background, QStringLiteral("#000000"));
    }

    void style_invalid_color_ignored()
    {
        const char* yaml =
            "device:\n"
            "  name: t\n"
            "style:\n"
            "  default:\n"
            "    accent: 'notacolor'\n"
            "widgets:\n"
            "  a:\n"
            "    type: toggle\n";
        YamlParser p;
        QList<WidgetDef> widgets;
        QString name, version;
        QStringList caps;
        QMap<QString, StyleToken> styles;
        QVERIFY(p.parse(yaml, widgets, name, version, caps, styles));
        /* invalid color string must not overwrite C++ default */
        QCOMPARE(styles[QStringLiteral("default")].accent, QStringLiteral("#00d4aa"));
    }

    void style_all_16_tokens_parsed()
    {
        const char* yaml =
            "device:\n"
            "  name: t\n"
            "style:\n"
            "  default:\n"
            "    background: '#111111'\n"
            "    surface: '#222222'\n"
            "    text: '#333333'\n"
            "    text_muted: '#444444'\n"
            "    text_heading: '#555555'\n"
            "    border: '#666666'\n"
            "    line: '#777777'\n"
            "    accent: '#888888'\n"
            "    button: '#999999'\n"
            "    button_text: '#aaaaaa'\n"
            "    led_on: '#bbbbbb'\n"
            "    led_off: 'transparent'\n"
            "    led_border: '#cccccc'\n"
            "    success: '#dddddd'\n"
            "    warning: '#eeeeee'\n"
            "    error: '#ffffff'\n"
            "widgets:\n"
            "  a:\n"
            "    type: toggle\n";
        YamlParser p;
        QList<WidgetDef> widgets;
        QString name, version;
        QStringList caps;
        QMap<QString, StyleToken> styles;
        QVERIFY(p.parse(yaml, widgets, name, version, caps, styles));
        const StyleToken& t = styles[QStringLiteral("default")];
        QCOMPARE(t.background,   QStringLiteral("#111111"));
        QCOMPARE(t.surface,      QStringLiteral("#222222"));
        QCOMPARE(t.text,         QStringLiteral("#333333"));
        QCOMPARE(t.text_muted,   QStringLiteral("#444444"));
        QCOMPARE(t.text_heading, QStringLiteral("#555555"));
        QCOMPARE(t.border,       QStringLiteral("#666666"));
        QCOMPARE(t.line,         QStringLiteral("#777777"));
        QCOMPARE(t.accent,       QStringLiteral("#888888"));
        QCOMPARE(t.button,       QStringLiteral("#999999"));
        QCOMPARE(t.button_text,  QStringLiteral("#aaaaaa"));
        QCOMPARE(t.led_on,       QStringLiteral("#bbbbbb"));
        QCOMPARE(t.led_off,      QStringLiteral("transparent"));
        QCOMPARE(t.led_border,   QStringLiteral("#cccccc"));
        QCOMPARE(t.success,      QStringLiteral("#dddddd"));
        QCOMPARE(t.warning,      QStringLiteral("#eeeeee"));
        QCOMPARE(t.error,        QStringLiteral("#ffffff"));
    }

    void button_color_ignored_in_yaml()
    {
        const char* yaml =
            "widgets:\n"
            "  btn:\n"
            "    type: button\n"
            "    label: Test\n"
            "    color: '#ff0000'\n";
        YamlParser p;
        QList<WidgetDef> widgets;
        QString name, version;
        QVERIFY(p.parse(yaml, widgets, name, version));
        QVERIFY(widgets[0].color.isEmpty());
    }

    /* ── debug_state: design-mode preview values ─────────────────────── */

    void debugState_display_double()
    {
        const char* yaml =
            "widgets:\n"
            "  temp:\n"
            "    type: display\n"
            "    debug_state: 23.4\n";
        YamlParser p;
        QList<WidgetDef> widgets;
        QString name, version;
        QVERIFY(p.parse(yaml, widgets, name, version));
        QCOMPARE(widgets.size(), 1);
        QCOMPARE(widgets[0].debugValue, QVariant(23.4));
    }

    void debugState_led_bool()
    {
        const char* yaml =
            "widgets:\n"
            "  pump:\n"
            "    type: led\n"
            "    debug_state: true\n";
        YamlParser p;
        QList<WidgetDef> widgets;
        QString name, version;
        QVERIFY(p.parse(yaml, widgets, name, version));
        QCOMPARE(widgets.size(), 1);
        QCOMPARE(widgets[0].debugValue, QVariant(true));
    }

    void debugState_toggle_bool()
    {
        const char* yaml =
            "widgets:\n"
            "  relay:\n"
            "    type: toggle\n"
            "    debug_state: false\n";
        YamlParser p;
        QList<WidgetDef> widgets;
        QString name, version;
        QVERIFY(p.parse(yaml, widgets, name, version));
        QCOMPARE(widgets.size(), 1);
        QCOMPARE(widgets[0].debugValue, QVariant(false));
    }

    void debugState_slider_double()
    {
        const char* yaml =
            "widgets:\n"
            "  rate:\n"
            "    type: slider\n"
            "    min: 10\n"
            "    max: 80\n"
            "    debug_state: 42.0\n";
        YamlParser p;
        QList<WidgetDef> widgets;
        QString name, version;
        QVERIFY(p.parse(yaml, widgets, name, version));
        QCOMPARE(widgets.size(), 1);
        QCOMPARE(widgets[0].debugValue, QVariant(42.0));
    }

    void debugState_text_string()
    {
        const char* yaml =
            "widgets:\n"
            "  ssid:\n"
            "    type: text\n"
            "    debug_state: hello\n";
        YamlParser p;
        QList<WidgetDef> widgets;
        QString name, version;
        QVERIFY(p.parse(yaml, widgets, name, version));
        QCOMPARE(widgets.size(), 1);
        QCOMPARE(widgets[0].debugValue, QVariant(QStringLiteral("hello")));
    }

    void debugState_dropdown_string()
    {
        const char* yaml =
            "widgets:\n"
            "  mode:\n"
            "    type: dropdown\n"
            "    items:\n"
            "      auto: Automatic\n"
            "      manual: Manual\n"
            "    debug_state: manual\n";
        YamlParser p;
        QList<WidgetDef> widgets;
        QString name, version;
        QVERIFY(p.parse(yaml, widgets, name, version));
        QCOMPARE(widgets.size(), 1);
        QCOMPARE(widgets[0].debugValue, QVariant(QStringLiteral("manual")));
    }

    void debugState_rgbled_int()
    {
        const char* yaml =
            "widgets:\n"
            "  rgb:\n"
            "    type: rgbled\n"
            "    debug_state: 16711680\n";  /* 0xFF0000 */
        YamlParser p;
        QList<WidgetDef> widgets;
        QString name, version;
        QVERIFY(p.parse(yaml, widgets, name, version));
        QCOMPARE(widgets.size(), 1);
        QCOMPARE(widgets[0].debugValue, QVariant(16711680));
    }

    void debugState_buttonChild_bool()
    {
        const char* yaml =
            "widgets:\n"
            "  fire:\n"
            "    type: button\n"
            "    label: Fire\n"
            "    widgets:\n"
            "      arm:\n"
            "        type: led\n"
            "        label: Arm\n"
            "        debug_state: true\n";
        YamlParser p;
        QList<WidgetDef> widgets;
        QString name, version;
        QVERIFY(p.parse(yaml, widgets, name, version));
        QCOMPARE(widgets.size(), 1);
        QCOMPARE(widgets[0].children.size(), 1);
        QCOMPARE(widgets[0].children[0].debugValue, QVariant(true));
    }

    void debugState_absent_isNull()
    {
        const char* yaml =
            "widgets:\n"
            "  temp:\n"
            "    type: display\n"
            "    label: Temperature\n";
        YamlParser p;
        QList<WidgetDef> widgets;
        QString name, version;
        QVERIFY(p.parse(yaml, widgets, name, version));
        QCOMPARE(widgets.size(), 1);
        QVERIFY(widgets[0].debugValue.isNull());
    }

    /* ── text.mode normalization ─────────────────────────────────────────── */

    /* Schema enum value "ro" must be normalized to "readonly" at parse time.
     * QML TextWidget.qml checks for "readonly" and "rw" — "ro" left as-is
     * would make both display layers invisible. */
    void text_mode_ro_normalizedToReadonly()
    {
        const char* yaml =
            "widgets:\n"
            "  ssid:\n"
            "    type: text\n"
            "    label: SSID\n"
            "    mode: ro\n";
        YamlParser p;
        QList<WidgetDef> widgets;
        QString name, version;
        QVERIFY(p.parse(yaml, widgets, name, version));
        QCOMPARE(widgets.size(), 1);
        QCOMPARE(widgets[0].textMode,        QStringLiteral("readonly"));
        QCOMPARE(widgets[0].defaultTextMode, QStringLiteral("readonly"));
    }

    /* "rw" must pass through unchanged. */
    void text_mode_rw_unchanged()
    {
        const char* yaml =
            "widgets:\n"
            "  ssid:\n"
            "    type: text\n"
            "    label: SSID\n"
            "    mode: rw\n";
        YamlParser p;
        QList<WidgetDef> widgets;
        QString name, version;
        QVERIFY(p.parse(yaml, widgets, name, version));
        QCOMPARE(widgets.size(), 1);
        QCOMPARE(widgets[0].textMode, QStringLiteral("rw"));
    }

    /* Absent mode: defaults to "readonly". */
    void text_mode_absent_defaultsToReadonly()
    {
        const char* yaml =
            "widgets:\n"
            "  ssid:\n"
            "    type: text\n"
            "    label: SSID\n";
        YamlParser p;
        QList<WidgetDef> widgets;
        QString name, version;
        QVERIFY(p.parse(yaml, widgets, name, version));
        QCOMPARE(widgets.size(), 1);
        QCOMPARE(widgets[0].textMode, QStringLiteral("readonly"));
    }

    /* ── Strict validation: fatal errors (parse fails) ───────────────── */

    void unknownType_topLevel_fails()
    {
        YamlParser p;
        QList<WidgetDef> widgets;
        QString name, version;
        const char* yaml =
            "widgets:\n"
            "  a:\n"
            "    type: frobnitz\n";
        QVERIFY(!p.parse(yaml, widgets, name, version));
        QVERIFY(!p.errorString().isEmpty());
        QVERIFY(!p.diagnostics().isEmpty());
        QCOMPARE(p.diagnostics()[0].severity, YamlParser::Severity::Error);
        QCOMPARE(p.diagnostics()[0].field, QStringLiteral("type"));
    }

    void unknownType_inRowContainer_fails()
    {
        YamlParser p;
        QList<WidgetDef> widgets;
        QString name, version;
        const char* yaml =
            "widgets:\n"
            "  r:\n"
            "    type: row\n"
            "    widgets:\n"
            "      x:\n"
            "        type: nosuchtype\n";
        QVERIFY(!p.parse(yaml, widgets, name, version));
        QVERIFY(!p.errorString().isEmpty());
    }

    /* ── Strict validation: warnings (parse succeeds) ────────────────── */

    void buttonShape_invalid_failsParse()
    {
        const char* yaml =
            "widgets:\n"
            "  btn:\n"
            "    type: button\n"
            "    shape: oval\n";
        YamlParser p;
        QList<WidgetDef> widgets;
        QString name, version;
        QVERIFY(!p.parse(yaml, widgets, name, version));
        QVERIFY(!p.errorString().isEmpty());
        QVERIFY(!p.diagnostics().isEmpty());
        QCOMPARE(p.diagnostics()[0].severity, YamlParser::Severity::Error);
        QCOMPARE(p.diagnostics()[0].field,    QStringLiteral("shape"));
    }

    void buttonGroupLayout_invalid_failsParse()
    {
        const char* yaml =
            "widgets:\n"
            "  bg:\n"
            "    type: button-group\n"
            "    layout: circular\n"
            "    items:\n"
            "      a:\n"
            "        label: A\n"
            "      b:\n"
            "        label: B\n";
        YamlParser p;
        QList<WidgetDef> widgets;
        QString name, version;
        QVERIFY(!p.parse(yaml, widgets, name, version));
        QVERIFY(!p.errorString().isEmpty());
        bool hasError = false;
        for (const auto& d : p.diagnostics())
            if (d.field == QStringLiteral("layout") &&
                d.severity == YamlParser::Severity::Error) hasError = true;
        QVERIFY(hasError);
    }

    void textMode_invalid_failsParse()
    {
        const char* yaml =
            "widgets:\n"
            "  f:\n"
            "    type: text\n"
            "    mode: readwrite\n";
        YamlParser p;
        QList<WidgetDef> widgets;
        QString name, version;
        QVERIFY(!p.parse(yaml, widgets, name, version));
        QVERIFY(!p.errorString().isEmpty());
        bool hasError = false;
        for (const auto& d : p.diagnostics())
            if (d.field == QStringLiteral("mode") &&
                d.severity == YamlParser::Severity::Error) hasError = true;
        QVERIFY(hasError);
    }

    void displayStyle_invalid_failsParse()
    {
        const char* yaml =
            "widgets:\n"
            "  d:\n"
            "    type: display\n"
            "    style: hero\n";
        YamlParser p;
        QList<WidgetDef> widgets;
        QString name, version;
        QVERIFY(!p.parse(yaml, widgets, name, version));
        QVERIFY(!p.errorString().isEmpty());
        bool hasError = false;
        for (const auto& d : p.diagnostics())
            if (d.field == QStringLiteral("style") &&
                d.severity == YamlParser::Severity::Error) hasError = true;
        QVERIFY(hasError);
    }

    void labelStyle_invalid_failsParse()
    {
        const char* yaml =
            "widgets:\n"
            "  lbl:\n"
            "    type: label\n"
            "    text: Hello\n"
            "    style: giant\n";
        YamlParser p;
        QList<WidgetDef> widgets;
        QString name, version;
        QVERIFY(!p.parse(yaml, widgets, name, version));
        QVERIFY(!p.errorString().isEmpty());
        bool hasError = false;
        for (const auto& d : p.diagnostics())
            if (d.field == QStringLiteral("style") &&
                d.severity == YamlParser::Severity::Error) hasError = true;
        QVERIFY(hasError);
    }

    void sliderStep_zero_warns()
    {
        const char* yaml =
            "widgets:\n"
            "  s:\n"
            "    type: slider\n"
            "    min: 0\n"
            "    max: 100\n"
            "    step: 0\n";
        YamlParser p;
        QList<WidgetDef> widgets;
        QString name, version;
        QVERIFY(p.parse(yaml, widgets, name, version));
        QCOMPARE(widgets[0].sliderStep, 1.0);
        bool hasWarning = false;
        for (const auto& d : p.diagnostics())
            if (d.field == QStringLiteral("step")) hasWarning = true;
        QVERIFY(hasWarning);
    }

    void sliderStep_negative_warns()
    {
        const char* yaml =
            "widgets:\n"
            "  s:\n"
            "    type: slider\n"
            "    min: 0\n"
            "    max: 100\n"
            "    step: -5\n";
        YamlParser p;
        QList<WidgetDef> widgets;
        QString name, version;
        QVERIFY(p.parse(yaml, widgets, name, version));
        QCOMPARE(widgets[0].sliderStep, 1.0);
        bool hasWarning = false;
        for (const auto& d : p.diagnostics())
            if (d.field == QStringLiteral("step")) hasWarning = true;
        QVERIFY(hasWarning);
    }

    void sliderMaxBelowMin_warns()
    {
        const char* yaml =
            "widgets:\n"
            "  s:\n"
            "    type: slider\n"
            "    min: 100\n"
            "    max: 10\n";
        YamlParser p;
        QList<WidgetDef> widgets;
        QString name, version;
        QVERIFY(p.parse(yaml, widgets, name, version));
        bool hasWarning = false;
        for (const auto& d : p.diagnostics())
            if (d.field == QStringLiteral("max")) hasWarning = true;
        QVERIFY(hasWarning);
    }

    void textMaxlength_zero_warns()
    {
        const char* yaml =
            "widgets:\n"
            "  f:\n"
            "    type: text\n"
            "    maxlength: 0\n";
        YamlParser p;
        QList<WidgetDef> widgets;
        QString name, version;
        QVERIFY(p.parse(yaml, widgets, name, version));
        QCOMPARE(widgets[0].textMaxLength, 1);
        bool hasWarning = false;
        for (const auto& d : p.diagnostics())
            if (d.field == QStringLiteral("maxlength")) hasWarning = true;
        QVERIFY(hasWarning);
    }

    void textMaxlength_tooLarge_warns()
    {
        const char* yaml =
            "widgets:\n"
            "  f:\n"
            "    type: text\n"
            "    maxlength: 999\n";
        YamlParser p;
        QList<WidgetDef> widgets;
        QString name, version;
        QVERIFY(p.parse(yaml, widgets, name, version));
        QCOMPARE(widgets[0].textMaxLength, 255);
        bool hasWarning = false;
        for (const auto& d : p.diagnostics())
            if (d.field == QStringLiteral("maxlength")) hasWarning = true;
        QVERIFY(hasWarning);
    }

    void ledColor_invalid_warns()
    {
        const char* yaml =
            "widgets:\n"
            "  led:\n"
            "    type: led\n"
            "    color: 'red'\n";
        YamlParser p;
        QList<WidgetDef> widgets;
        QString name, version;
        QVERIFY(p.parse(yaml, widgets, name, version));
        QCOMPARE(widgets[0].color, QStringLiteral("#00d4aa"));
        bool hasWarning = false;
        for (const auto& d : p.diagnostics())
            if (d.field == QStringLiteral("color")) hasWarning = true;
        QVERIFY(hasWarning);
    }

    void ledColor_fourCharHex_warns()
    {
        /* #rgb (4-char) is valid for style tokens but not for LED color. */
        const char* yaml =
            "widgets:\n"
            "  led:\n"
            "    type: led\n"
            "    color: '#f00'\n";
        YamlParser p;
        QList<WidgetDef> widgets;
        QString name, version;
        QVERIFY(p.parse(yaml, widgets, name, version));
        QCOMPARE(widgets[0].color, QStringLiteral("#00d4aa"));
        bool hasWarning = false;
        for (const auto& d : p.diagnostics())
            if (d.field == QStringLiteral("color")) hasWarning = true;
        QVERIFY(hasWarning);
    }

    void buttonGroup_oneItem_warns()
    {
        const char* yaml =
            "widgets:\n"
            "  bg:\n"
            "    type: button-group\n"
            "    items:\n"
            "      a:\n"
            "        label: A\n";
        YamlParser p;
        QList<WidgetDef> widgets;
        QString name, version;
        QVERIFY(p.parse(yaml, widgets, name, version));
        bool hasWarning = false;
        for (const auto& d : p.diagnostics())
            if (d.field == QStringLiteral("items")) hasWarning = true;
        QVERIFY(hasWarning);
    }

    void button_twoChildren_noWarning()
    {
        /* Multiple button children are now valid — no "at most 1" warning. */
        const char* yaml =
            "widgets:\n"
            "  btn:\n"
            "    type: button\n"
            "    widgets:\n"
            "      a:\n"
            "        type: led\n"
            "      b:\n"
            "        type: led\n";
        YamlParser p;
        QList<WidgetDef> widgets;
        QString name, version;
        QVERIFY(p.parse(yaml, widgets, name, version));
        QVERIFY(p.diagnostics().isEmpty());
        QCOMPARE(widgets[0].children.size(), 2);
    }

    void gridColumns_one_acceptedAsColumnLayout()
    {
        /* columns: 1 is a deliberately supported degenerate case (single-
         * column "ColumnWidget" via grid) — GridWidget.qml's `props.columns
         * || 2` fallback only guards falsy values (0/null), so 1 flows
         * through unchanged and renders correctly. No warning expected. */
        const char* yaml =
            "widgets:\n"
            "  g:\n"
            "    type: grid\n"
            "    columns: 1\n"
            "    widgets:\n"
            "      a:\n"
            "        type: toggle\n";
        YamlParser p;
        QList<WidgetDef> widgets;
        QString name, version;
        QVERIFY(p.parse(yaml, widgets, name, version));
        QCOMPARE(widgets[0].gridColumns, 1);
        bool hasWarning = false;
        for (const auto& d : p.diagnostics())
            if (d.field == QStringLiteral("columns")) hasWarning = true;
        QVERIFY(!hasWarning);
    }

    void gridColumns_negative_warnsAndClampsToOne()
    {
        /* Adversarial-review finding: GridWidget.qml's `props.columns || 2`
         * only guards falsy values (0/null) — a negative value would flow
         * straight through to GridLayout.columns and negative-modulo array
         * indexing in the per-column flex-ratio math. The parser must
         * clamp, not just warn, so QML never sees an invalid value. */
        const char* yaml =
            "widgets:\n"
            "  g:\n"
            "    type: grid\n"
            "    columns: -3\n"
            "    widgets:\n"
            "      a:\n"
            "        type: toggle\n";
        YamlParser p;
        QList<WidgetDef> widgets;
        QString name, version;
        QVERIFY(p.parse(yaml, widgets, name, version));
        QCOMPARE(widgets[0].gridColumns, 1);
        bool hasWarning = false;
        for (const auto& d : p.diagnostics())
            if (d.field == QStringLiteral("columns")) hasWarning = true;
        QVERIFY(hasWarning);
    }

    void debugState_wrongType_display_warns()
    {
        /* debug_state: "notanumber" on a display widget → Warning, not crash. */
        const char* yaml =
            "widgets:\n"
            "  d:\n"
            "    type: display\n"
            "    debug_state: notanumber\n";
        YamlParser p;
        QList<WidgetDef> widgets;
        QString name, version;
        QVERIFY(p.parse(yaml, widgets, name, version));
        QVERIFY(widgets[0].debugValue.isNull());
        bool hasWarning = false;
        for (const auto& d : p.diagnostics())
            if (d.field == QStringLiteral("debug_state")) hasWarning = true;
        QVERIFY(hasWarning);
    }

    void debugState_wrongType_led_warns()
    {
        const char* yaml =
            "widgets:\n"
            "  l:\n"
            "    type: led\n"
            "    debug_state: notabool\n";
        YamlParser p;
        QList<WidgetDef> widgets;
        QString name, version;
        QVERIFY(p.parse(yaml, widgets, name, version));
        QVERIFY(widgets[0].debugValue.isNull());
        bool hasWarning = false;
        for (const auto& d : p.diagnostics())
            if (d.field == QStringLiteral("debug_state")) hasWarning = true;
        QVERIFY(hasWarning);
    }

    void debugState_wrongType_toggle_warns()
    {
        const char* yaml =
            "widgets:\n"
            "  t:\n"
            "    type: toggle\n"
            "    debug_state: '42'\n";
        YamlParser p;
        QList<WidgetDef> widgets;
        QString name, version;
        QVERIFY(p.parse(yaml, widgets, name, version));
        QVERIFY(widgets[0].debugValue.isNull());
        bool hasWarning = false;
        for (const auto& d : p.diagnostics())
            if (d.field == QStringLiteral("debug_state")) hasWarning = true;
        QVERIFY(hasWarning);
    }

    /* ── State isolation ──────────────────────────────────────────────── */

    void diagnostics_clearedBetweenParses()
    {
        YamlParser p;
        QList<WidgetDef> widgets;
        QString name, version;

        /* First parse: triggers a warning (LED with invalid color). */
        const char* warnYaml =
            "widgets:\n"
            "  led:\n"
            "    type: led\n"
            "    color: not_a_hex_color\n";
        QVERIFY(p.parse(warnYaml, widgets, name, version));
        QVERIFY(!p.diagnostics().isEmpty());

        /* Second parse: clean YAML — diagnostics must be cleared. */
        QVERIFY(p.parse(YAML_V1, widgets, name, version));
        QVERIFY(p.diagnostics().isEmpty());
    }

    /* ── Valid inputs produce no warnings ────────────────────────────── */

    void buttonShape_valid_noWarnings()
    {
        const char* yaml =
            "widgets:\n"
            "  btn:\n"
            "    type: button\n"
            "    shape: circle\n";
        YamlParser p;
        QList<WidgetDef> widgets;
        QString name, version;
        QVERIFY(p.parse(yaml, widgets, name, version));
        QCOMPARE(widgets[0].shape, QStringLiteral("circle"));
        QVERIFY(p.diagnostics().isEmpty());
    }

    void ledColor_valid_noWarnings()
    {
        const char* yaml =
            "widgets:\n"
            "  led:\n"
            "    type: led\n"
            "    color: '#ff0000'\n";
        YamlParser p;
        QList<WidgetDef> widgets;
        QString name, version;
        QVERIFY(p.parse(yaml, widgets, name, version));
        QCOMPARE(widgets[0].color, QStringLiteral("#ff0000"));
        QVERIFY(p.diagnostics().isEmpty());
    }

    void sliderStep_valid_noWarnings()
    {
        const char* yaml =
            "widgets:\n"
            "  s:\n"
            "    type: slider\n"
            "    min: 0\n"
            "    max: 10\n"
            "    step: 0.5\n";
        YamlParser p;
        QList<WidgetDef> widgets;
        QString name, version;
        QVERIFY(p.parse(yaml, widgets, name, version));
        QCOMPARE(widgets[0].sliderStep, 0.5);
        QVERIFY(p.diagnostics().isEmpty());
    }

    void buttonGroup_twoItems_noWarnings()
    {
        const char* yaml =
            "widgets:\n"
            "  bg:\n"
            "    type: button-group\n"
            "    items:\n"
            "      a:\n"
            "        label: A\n"
            "      b:\n"
            "        label: B\n";
        YamlParser p;
        QList<WidgetDef> widgets;
        QString name, version;
        QVERIFY(p.parse(yaml, widgets, name, version));
        QVERIFY(p.diagnostics().isEmpty());
    }

    void textMaxlength_valid_noWarnings()
    {
        const char* yaml =
            "widgets:\n"
            "  f:\n"
            "    type: text\n"
            "    maxlength: 64\n";
        YamlParser p;
        QList<WidgetDef> widgets;
        QString name, version;
        QVERIFY(p.parse(yaml, widgets, name, version));
        QCOMPARE(widgets[0].textMaxLength, 64);
        QVERIFY(p.diagnostics().isEmpty());
    }

    /* ── Button child type acceptance ────────────────────────────────── */

    /* Legacy `children:` key on a button warns and drops the face content —
     * must not silently lose data, since the client parses device-supplied
     * YAML directly with no schema validation at runtime. */
    void buttonChild_legacyChildrenKey_warnsAndDropsContent()
    {
        const char* yaml =
            "widgets:\n"
            "  btn:\n"
            "    type: button\n"
            "    children:\n"
            "      arm:\n"
            "        type: led\n"
            "        label: Armed\n";
        YamlParser p;
        QList<WidgetDef> widgets;
        QString name, version;
        QVERIFY(p.parse(yaml, widgets, name, version));
        QCOMPARE(widgets[0].children.size(), 0);
        bool hasWarning = false;
        for (const auto& d : p.diagnostics())
            if (d.field == QStringLiteral("children")) hasWarning = true;
        QVERIFY(hasWarning);
    }

    /* A label button child must consume ZERO widget-ID slots — matching
     * widget_ids.py's NO_ID_TYPES exclusion (collect_types()/_collect()).
     * Regression guard: collectPathsRecursive() previously assigned an ID to
     * every button child unconditionally, so a label child would shift every
     * alphabetically-later widget's ID by one relative to what the Python
     * codegen tool (which firmware is actually built against) computes. */
    void buttonChild_labelType_consumesNoIdSlot()
    {
        const char* yaml =
            "widgets:\n"
            "  btn:\n"
            "    type: button\n"
            "    widgets:\n"
            "      caption:\n"
            "        type: label\n"
            "        text: Hi\n"
            "      status:\n"
            "        type: led\n"
            "  zzz_toggle:\n"
            "    type: toggle\n";
        YamlParser p;
        QList<WidgetDef> widgets;
        QString name, version;
        QVERIFY(p.parse(yaml, widgets, name, version));

        const WidgetDef* btn = findByKey(widgets, "btn");
        QVERIFY(btn);
        QCOMPARE(btn->widgetId, uint8_t(0x10));
        QCOMPARE(btn->children.size(), 2);
        const WidgetDef* status = findByKey(btn->children, "btn.status");
        QVERIFY(status);
        /* btn=0x10, btn.status=0x11 (caption consumed no slot) */
        QCOMPARE(status->widgetId, uint8_t(0x11));

        const WidgetDef* zzz = findByKey(widgets, "zzz_toggle");
        QVERIFY(zzz);
        /* zzz_toggle must be 0x12, not 0x13 — proves caption never
         * occupied a slot that would otherwise shift this ID. */
        QCOMPARE(zzz->widgetId, uint8_t(0x12));
    }

    /* A button child with type: led is accepted — regression guard for demos. */
    void buttonChild_ledType_accepted()
    {
        const char* yaml =
            "widgets:\n"
            "  btn:\n"
            "    type: button\n"
            "    widgets:\n"
            "      arm:\n"
            "        type: led\n"
            "        label: Armed\n";
        YamlParser p;
        QList<WidgetDef> widgets;
        QString name, version;
        QVERIFY(p.parse(yaml, widgets, name, version));
        QVERIFY(p.diagnostics().isEmpty());
        QCOMPARE(widgets[0].children.size(), 1);
        QCOMPARE(widgets[0].children[0].type,  WidgetType::Led);
        QCOMPARE(widgets[0].children[0].label, QStringLiteral("Armed"));
    }

    /* A button child with type: rgbled parses correctly via real YAML text
     * (not just a hand-built WidgetDef) — the schema now widens to allow it. */
    void buttonChild_rgbledType_accepted()
    {
        const char* yaml =
            "widgets:\n"
            "  btn:\n"
            "    type: button\n"
            "    widgets:\n"
            "      status:\n"
            "        type: rgbled\n";
        YamlParser p;
        QList<WidgetDef> widgets;
        QString name, version;
        QVERIFY(p.parse(yaml, widgets, name, version));
        QVERIFY(p.diagnostics().isEmpty());
        QCOMPARE(widgets[0].children.size(), 1);
        QCOMPARE(widgets[0].children[0].type, WidgetType::RgbLed);
    }

    /* A button child with type: display parses correctly via real YAML text —
     * the schema now widens to allow it. */
    void buttonChild_displayType_accepted()
    {
        const char* yaml =
            "widgets:\n"
            "  btn:\n"
            "    type: button\n"
            "    widgets:\n"
            "      volt:\n"
            "        type: display\n"
            "        label: Volts\n";
        YamlParser p;
        QList<WidgetDef> widgets;
        QString name, version;
        QVERIFY(p.parse(yaml, widgets, name, version));
        QVERIFY(p.diagnostics().isEmpty());
        QCOMPARE(widgets[0].children.size(), 1);
        QCOMPARE(widgets[0].children[0].type,  WidgetType::Display);
        QCOMPARE(widgets[0].children[0].label, QStringLiteral("Volts"));
    }

    /* A button child with any widget type is now accepted (not just led). */
    void buttonChild_nonLedType_accepted()
    {
        const char* yaml =
            "widgets:\n"
            "  btn:\n"
            "    type: button\n"
            "    widgets:\n"
            "      ctrl:\n"
            "        type: toggle\n"
            "        label: Enable\n";
        YamlParser p;
        QList<WidgetDef> widgets;
        QString name, version;
        QVERIFY(p.parse(yaml, widgets, name, version));
        QCOMPARE(widgets[0].children.size(), 1);
        QCOMPARE(widgets[0].children[0].type,  WidgetType::Toggle);
        QCOMPARE(widgets[0].children[0].label, QStringLiteral("Enable"));
    }

    /* A button with no type field produces an Unknown-type child (not an Error). */
    void buttonChild_missingType_producesUnknownChild()
    {
        const char* yaml =
            "widgets:\n"
            "  btn:\n"
            "    type: button\n"
            "    widgets:\n"
            "      arm:\n"
            "        label: Arm\n";
        YamlParser p;
        QList<WidgetDef> widgets;
        QString name, version;
        p.parse(yaml, widgets, name, version);
        /* Parse may warn about unknown type but must not hard-fail the whole YAML */
        QCOMPARE(widgets[0].children.size(), 1);
        QCOMPARE(widgets[0].children[0].type, WidgetType::Unknown);
    }

    /* A button can have multiple children (no "at most 1" restriction). */
    void buttonChild_multipleChildren_accepted()
    {
        const char* yaml =
            "widgets:\n"
            "  btn:\n"
            "    type: button\n"
            "    widgets:\n"
            "      led1:\n"
            "        type: led\n"
            "      led2:\n"
            "        type: led\n";
        YamlParser p;
        QList<WidgetDef> widgets;
        QString name, version;
        QVERIFY(p.parse(yaml, widgets, name, version));
        QVERIFY(p.diagnostics().isEmpty());
        QCOMPARE(widgets[0].children.size(), 2);
    }

    /* debug_state on a button child is propagated to child.debugValue. */
    void buttonChild_debugValue_propagatesOnChild()
    {
        const char* yaml =
            "widgets:\n"
            "  btn:\n"
            "    type: button\n"
            "    widgets:\n"
            "      status:\n"
            "        type: led\n"
            "        debug_state: true\n";
        YamlParser p;
        QList<WidgetDef> widgets;
        QString name, version;
        QVERIFY(p.parse(yaml, widgets, name, version));
        QCOMPARE(widgets[0].children.size(), 1);
        QCOMPARE(widgets[0].children[0].debugValue, QVariant(true));
    }

    /* ── Increment 2: button face row/grid nesting ───────────────────── */

    /* A grid nested inside a button face is transparent to ID assignment,
     * matching top-level container semantics — the grid itself gets
     * widgetId 0 (no container name in the path), and its led grandchild
     * gets a real, non-zero ID prefixed by the BUTTON's own path (not the
     * grid's throwaway key). Regression guard for collectPathsRecursive's
     * button-child loop (previously flat, one level only) and
     * buildTopLevelWidget's Row/Grid idPrefix threading. */
    void button_gridChild_ledGrandchild_getsRealId()
    {
        const char* yaml =
            "widgets:\n"
            "  btn:\n"
            "    type: button\n"
            "    widgets:\n"
            "      face:\n"
            "        type: grid\n"
            "        columns: 1\n"
            "        widgets:\n"
            "          status:\n"
            "            type: led\n"
            "  zzz_toggle:\n"
            "    type: toggle\n";
        YamlParser p;
        QList<WidgetDef> widgets;
        QString name, version;
        QVERIFY(p.parse(yaml, widgets, name, version));

        const WidgetDef* btn = findByKey(widgets, "btn");
        QVERIFY(btn);
        QCOMPARE(btn->widgetId, uint8_t(0x10));
        QCOMPARE(btn->children.size(), 1);

        const WidgetDef& face = btn->children[0];
        QCOMPARE(face.type, WidgetType::Grid);
        QCOMPARE(face.widgetId, uint8_t(0));   /* container: transparent, no ID */
        QCOMPARE(face.children.size(), 1);

        const WidgetDef& status = face.children[0];
        QCOMPARE(status.type, WidgetType::Led);
        /* btn=0x10, btn.status=0x11 — "face" contributes no path segment */
        QCOMPARE(status.widgetId, uint8_t(0x11));

        const WidgetDef* zzz = findByKey(widgets, "zzz_toggle");
        QVERIFY(zzz);
        /* zzz_toggle must be 0x12 — proves "face" never consumed a real ID
         * slot that would otherwise shift this ID. */
        QCOMPARE(zzz->widgetId, uint8_t(0x12));
    }

    /* A row nested inside a button face is likewise transparent to ID
     * assignment (same mechanism as grid above, different container type). */
    void button_rowChild_ledGrandchild_getsRealId()
    {
        const char* yaml =
            "widgets:\n"
            "  btn:\n"
            "    type: button\n"
            "    widgets:\n"
            "      face:\n"
            "        type: row\n"
            "        widgets:\n"
            "          status:\n"
            "            type: rgbled\n";
        YamlParser p;
        QList<WidgetDef> widgets;
        QString name, version;
        QVERIFY(p.parse(yaml, widgets, name, version));

        const WidgetDef* btn = findByKey(widgets, "btn");
        QVERIFY(btn);
        QCOMPARE(btn->children.size(), 1);
        const WidgetDef& face = btn->children[0];
        QCOMPARE(face.type, WidgetType::Row);
        QCOMPARE(face.widgetId, uint8_t(0));
        QCOMPARE(face.children.size(), 1);
        QCOMPARE(face.children[0].type, WidgetType::RgbLed);
        QCOMPARE(face.children[0].widgetId, uint8_t(0x11));
    }

    /* A direct excluded interactive type (toggle) in a button face emits a
     * Warning — not a parse failure. The client stays permissive (defense
     * in depth for device-supplied YAML that bypasses `udisplay-gen
     * validate` entirely); the schema is the hard gate. */
    void buttonChild_excludedInteractiveType_warns()
    {
        const char* yaml =
            "widgets:\n"
            "  btn:\n"
            "    type: button\n"
            "    widgets:\n"
            "      ctrl:\n"
            "        type: toggle\n";
        YamlParser p;
        QList<WidgetDef> widgets;
        QString name, version;
        QVERIFY(p.parse(yaml, widgets, name, version));

        bool hasWarning = false;
        for (const auto& d : p.diagnostics())
            if (d.severity == YamlParser::Severity::Warning
                && d.widgetKey == QStringLiteral("btn")
                && d.message.contains(QStringLiteral("toggle")))
                hasWarning = true;
        QVERIFY(hasWarning);

        /* Still parses and would render — not stripped, just flagged. */
        QCOMPARE(widgets[0].children.size(), 1);
        QCOMPARE(widgets[0].children[0].type, WidgetType::Toggle);
    }

    /* The excluded-type warning walks the FULL recursive face subtree, not
     * just direct children — a slider two levels deep (button -> row ->
     * slider) must still warn. */
    void buttonChild_excludedType_warnsRecursivelyInNestedContainer()
    {
        const char* yaml =
            "widgets:\n"
            "  btn:\n"
            "    type: button\n"
            "    widgets:\n"
            "      face:\n"
            "        type: row\n"
            "        widgets:\n"
            "          bad:\n"
            "            type: slider\n"
            "            min: 0\n"
            "            max: 10\n";
        YamlParser p;
        QList<WidgetDef> widgets;
        QString name, version;
        QVERIFY(p.parse(yaml, widgets, name, version));

        bool hasWarning = false;
        for (const auto& d : p.diagnostics())
            if (d.severity == YamlParser::Severity::Warning
                && d.widgetKey == QStringLiteral("btn")
                && d.message.contains(QStringLiteral("slider")))
                hasWarning = true;
        QVERIFY(hasWarning);
    }

    /* The recursive warning walk must still fire two containers deep
     * (button -> row -> grid -> slider) — the exact depth the schema-side
     * test (test_button_row_grandchild_slider_rejected in
     * udisplay-gen/tests/test_validate.py) already covers, mirrored here so
     * the client-side warning and the schema enforcement are proven to
     * agree at the same depth, not just depth 1. */
    void buttonChild_excludedType_warnsTwoContainersDeep()
    {
        const char* yaml =
            "widgets:\n"
            "  btn:\n"
            "    type: button\n"
            "    widgets:\n"
            "      face:\n"
            "        type: row\n"
            "        widgets:\n"
            "          inner:\n"
            "            type: grid\n"
            "            columns: 1\n"
            "            widgets:\n"
            "              bad:\n"
            "                type: slider\n"
            "                min: 0\n"
            "                max: 10\n";
        YamlParser p;
        QList<WidgetDef> widgets;
        QString name, version;
        QVERIFY(p.parse(yaml, widgets, name, version));

        bool hasWarning = false;
        for (const auto& d : p.diagnostics())
            if (d.severity == YamlParser::Severity::Warning
                && d.widgetKey == QStringLiteral("btn")
                && d.message.contains(QStringLiteral("slider")))
                hasWarning = true;
        QVERIFY(hasWarning);
    }

    /* Allowed face types (led/rgbled/display/label/row/grid) never trigger
     * the excluded-type warning — regression guard against false positives.
     * Also verifies ID sequencing survives the combination of recursion
     * into a nested container AND a decoration sibling (label) that must
     * consume no ID slot — led/display get real, sequential IDs; label
     * gets none. */
    /* Two sibling containers inside ONE button face, each with a
     * same-named leaf, resolve to the SAME id path ("btn.x" in both
     * cases — containers don't contribute their own name to the path, and
     * both siblings share the button as their transparent-prefix
     * ancestor). Before Increment 2, a button's face was a single flat
     * map, so YAML's own duplicate-key rule made this structurally
     * impossible; nested containers remove that guarantee. Confirmed via
     * adversarial review to silently produce two widgets sharing one
     * protocol ID (STATE_UPDATE cross-application) before this guard;
     * must now be a hard parse failure, not a warning. */
    void duplicateIdPath_acrossSiblingContainers_failsParse()
    {
        const char* yaml =
            "widgets:\n"
            "  btn:\n"
            "    type: button\n"
            "    widgets:\n"
            "      left:\n"
            "        type: grid\n"
            "        columns: 1\n"
            "        widgets:\n"
            "          x:\n"
            "            type: led\n"
            "      right:\n"
            "        type: grid\n"
            "        columns: 1\n"
            "        widgets:\n"
            "          x:\n"
            "            type: led\n";
        YamlParser p;
        QList<WidgetDef> widgets;
        QString name, version;
        QVERIFY(!p.parse(yaml, widgets, name, version));
        QVERIFY(p.errorString().contains(QStringLiteral("btn.x")));
    }

    /* Same collision class at the top level (two sibling row/grid
     * containers, no button involved) — the root cause (container
     * transparency + shared prefix) is not button-specific, so the guard
     * must not be either. */
    void duplicateIdPath_acrossTopLevelSiblingContainers_failsParse()
    {
        const char* yaml =
            "widgets:\n"
            "  left:\n"
            "    type: row\n"
            "    widgets:\n"
            "      x:\n"
            "        type: toggle\n"
            "  right:\n"
            "    type: row\n"
            "    widgets:\n"
            "      x:\n"
            "        type: toggle\n";
        YamlParser p;
        QList<WidgetDef> widgets;
        QString name, version;
        QVERIFY(!p.parse(yaml, widgets, name, version));
        QVERIFY(p.errorString().contains(QStringLiteral("x")));
    }

    void buttonChild_allowedNestedTypes_noWarning()
    {
        const char* yaml =
            "widgets:\n"
            "  btn:\n"
            "    type: button\n"
            "    widgets:\n"
            "      face:\n"
            "        type: grid\n"
            "        columns: 2\n"
            "        widgets:\n"
            "          a:\n"
            "            type: led\n"
            "          b:\n"
            "            type: display\n"
            "          c:\n"
            "            type: label\n"
            "            text: Hi\n";
        YamlParser p;
        QList<WidgetDef> widgets;
        QString name, version;
        QVERIFY(p.parse(yaml, widgets, name, version));
        QVERIFY(p.diagnostics().isEmpty());

        const WidgetDef* btn = findByKey(widgets, "btn");
        QVERIFY(btn);
        QCOMPARE(btn->widgetId, uint8_t(0x10));
        QCOMPARE(btn->children.size(), 1);

        const WidgetDef& face = btn->children[0];
        QCOMPARE(face.children.size(), 3);

        const WidgetDef* a = findByKey(face.children, "a");
        const WidgetDef* b = findByKey(face.children, "b");
        const WidgetDef* c = findByKey(face.children, "c");
        QVERIFY(a); QVERIFY(b); QVERIFY(c);
        QCOMPARE(a->type, WidgetType::Led);
        QCOMPARE(a->widgetId, uint8_t(0x11));
        QCOMPARE(b->type, WidgetType::Display);
        QCOMPARE(b->widgetId, uint8_t(0x12));
        QCOMPARE(c->type, WidgetType::Label);
        QCOMPARE(c->widgetId, uint8_t(0));   /* decoration: no ID slot consumed */
    }

    /* ── Problem 3: non-map widget entry emits a warning ─────────────── */

    /* A scalar entry in the widgets map (e.g. "foo: bar") must emit a
     * Warning and continue — parse succeeds for any valid siblings. */
    void nonMapWidget_emitsWarning()
    {
        const char* yaml =
            "widgets:\n"
            "  somerandom_text: hello\n"
            "  relay:\n"
            "    type: toggle\n";
        YamlParser p;
        QList<WidgetDef> widgets;
        QString name, version;
        QVERIFY(p.parse(yaml, widgets, name, version));
        QCOMPARE(widgets.size(), 1);
        QCOMPARE(widgets[0].keyPath, QStringLiteral("relay"));
        bool hasWarning = false;
        for (const auto& d : p.diagnostics())
            if (d.widgetKey == QStringLiteral("somerandom_text") &&
                d.severity == YamlParser::Severity::Warning) hasWarning = true;
        QVERIFY(hasWarning);
    }
};

QTEST_MAIN(TestYamlParser)
#include "test_yaml_parser.moc"
