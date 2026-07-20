// SPDX-License-Identifier: LGPL-3.0-only
// Copyright (c) 2026 Attila Agas

#include "YamlParser.h"
#include "udisplay_schema_enums.h"
#include <yaml-cpp/yaml.h>
#include <algorithm>
#include <array>
#include <QStringList>

using Diags    = QList<YamlParser::ParseDiagnostic>;
using Severity = YamlParser::Severity;

/* ── Helpers ────────────────────────────────────────────────────────────── */

static QString qs(const std::string& s) { return QString::fromStdString(s); }

static QString nodeStr(const YAML::Node& n, const char* key,
                       const QString& def = {})
{
    if (!n[key] || !n[key].IsScalar()) return def;
    return qs(n[key].as<std::string>());
}

static void diag(Diags& diags, Severity sev,
                 const std::string& key, const char* field, const QString& msg)
{
    diags.append({sev, qs(key), QString::fromLatin1(field), msg});
}

/* Returns true if v is a member of the constexpr array. */
template <typename Array>
static bool inEnum(const QString& v, const Array& allowed)
{
    std::string sv = v.toStdString();
    for (auto a : allowed)
        if (sv == a) return true;
    return false;
}

/* Row/grid child flex weight. Omitted (or non-scalar) -> 0, meaning
 * auto-width (the child sizes to its own implicitWidth and does not
 * stretch — see RowWidget.qml/GridWidget.qml's
 * max(implicitWidth, available*flex/totalFlex) layout formula, where
 * flex=0 naturally reduces to implicitWidth). An explicit value < 1 is
 * invalid (schema-side test_flex_zero_rejected mirrors this): warn and
 * clamp up to 1 rather than silently accepting 0 as "explicitly chosen
 * auto-width" — that distinction belongs to omission alone. */
static int parseFlex(const YAML::Node& node, const std::string& key, Diags& diags)
{
    if (!node["flex"] || !node["flex"].IsScalar())
        return 0;
    int flex = node["flex"].as<int>();
    if (flex < 1) {
        diag(diags, Severity::Warning, key, "flex",
             QStringLiteral("flex must be >= 1; got %1; using 1").arg(flex));
        flex = 1;
    }
    return flex;
}

/* Generic "align" enum parser shared by row/grid content alignment
 * (left/right/center) and label text alignment (left/right/center/justify —
 * see kLabelAligns below). Invalid or omitted values fall back to "left"
 * with a warning only when a value was actually present and unrecognised —
 * omission is silent (it's the documented default, not an error). */
template <typename Array>
static QString parseAlign(const YAML::Node& node, const std::string& key,
                          const char* field, const Array& allowed, Diags& diags)
{
    QString align = nodeStr(node, field, QStringLiteral("left"));
    if (!inEnum(align, allowed)) {
        QStringList valid;
        for (auto a : allowed) valid << QString::fromLatin1(a);
        diag(diags, Severity::Warning, key, field,
             QStringLiteral("unknown align '%1'; valid values: %2; using left")
                 .arg(align, valid.join(QStringLiteral(", "))));
        align = QStringLiteral("left");
    }
    return align;
}

static constexpr std::array<const char*, 3> kRowGridAligns = { "left", "right", "center" };
static constexpr std::array<const char*, 4> kLabelAligns   = { "left", "right", "center", "justify" };

/* Grid column count. Omitted (or non-scalar) -> 2 (WidgetDef.h's own
 * default). An explicit value < 1 is invalid (GridLayout needs at least 2
 * columns to mean anything as a grid, 1 technically equivalent with a ColumnLayout): warn and clamp up to 1 — clamped,
 * not just warned, so a negative/zero value never reaches GridWidget.qml,
 * where `props.columns || 2` only guards falsy values (0/null), not
 * negative ones (adversarial-review finding — a negative columns flowed
 * into GridLayout.columns and negative-modulo array indexing in the
 * per-column flex-ratio math before this was clamped here). */
static int parseGridColumns(const YAML::Node& node, const std::string& key, Diags& diags)
{
    if (!node["columns"] || !node["columns"].IsScalar())
        return 2;
    int columns = node["columns"].as<int>();
    if (columns < 1) {
        diag(diags, Severity::Warning, key, "columns",
             QStringLiteral("grid columns must be >= 1; got %1; clamping to 1").arg(columns));
        columns = 1;
    }
    return columns;
}

/* ══════════════════════════════════════════════════════════════════════════
 *  Widget ID assignment
 * ══════════════════════════════════════════════════════════════════════════ */

struct PathEntry {
    std::string path;
    bool        isChild;
    std::string parentPath;
    std::string childKey;
};

/* Container types: transparent to ID assignment (children inherit prefix). */
static bool isContainer(const std::string& type)
{
    return type == "section" || type == "row" || type == "grid";
}

/* Decoration types: no widget ID, no protocol exchange. */
static bool isDecoration(const std::string& type)
{
    return type == "label" || type == "separator";
}

static void collectPathsRecursive(const YAML::Node& widgets,
                                  const std::string& prefix,
                                  std::vector<PathEntry>& entries)
{
    for (auto it = widgets.begin(); it != widgets.end(); ++it) {
        std::string key = it->first.as<std::string>();
        YAML::Node  w   = it->second;
        if (!w.IsMap()) continue;

        std::string type;
        if (w["type"] && w["type"].IsScalar())
            type = w["type"].as<std::string>();

        if (isContainer(type)) {
            if (w["widgets"] && w["widgets"].IsMap())
                collectPathsRecursive(w["widgets"], prefix, entries);
            continue;
        }

        if (isDecoration(type)) continue;

        std::string path = prefix.empty() ? key : prefix + "." + key;
        entries.push_back({ path, !prefix.empty(), prefix, key });

        /* Recurse (not a flat loop) so a container-typed child (row/grid,
         * widget-model-redesign Increment 2) is transparent to ID assignment
         * just like a top-level container — its own grandchildren get IDs
         * prefixed by this widget's own path, not the container's throwaway
         * key. This is the exact same walk the top-level `widgets` map gets;
         * decoration children (label, separator) are skipped by the
         * isDecoration() check above on the recursive call, mirroring
         * widget_ids.py's NO_ID_TYPES exclusion so the client and the
         * offline codegen tool agree on ID numbering for every widget after
         * this one. */
        if (w["widgets"] && w["widgets"].IsMap())
            collectPathsRecursive(w["widgets"], path, entries);

        if (type == "button-group" && w["items"] && w["items"].IsMap()) {
            for (auto ii = w["items"].begin();
                 ii != w["items"].end(); ++ii) {
                std::string itemKey = ii->first.as<std::string>();
                entries.push_back({ path + "." + itemKey, true, path, itemKey });
            }
        }
    }
}

static std::vector<PathEntry> collectPaths(const YAML::Node& widgets)
{
    std::vector<PathEntry> entries;
    collectPathsRecursive(widgets, {}, entries);
    std::sort(entries.begin(), entries.end(),
              [](const PathEntry& a, const PathEntry& b) {
                  return a.path < b.path;
              });
    return entries;
}

/* ══════════════════════════════════════════════════════════════════════════
 *  Widget parsing
 * ══════════════════════════════════════════════════════════════════════════ */

static void appendRowGridChild(WidgetDef& parent, const std::string& key,
                               const YAML::Node& node, uint8_t widgetId,
                               const std::string& idPrefix,
                               const std::map<std::string, uint8_t>& idMap,
                               Diags& diags);

/* Emits a Warning diagnostic for any excluded interactive type
 * (toggle/slider/text/dropdown/button/button-group) found anywhere in a
 * button's face subtree — walks recursively into nested row/grid, not just
 * direct children. Defense in depth: the client parses device-supplied YAML
 * directly with no runtime schema validation, so this is the only guard for
 * YAML that bypasses `udisplay-gen validate` entirely. Not a parse failure —
 * the client stays permissive (schema-gates, client-warns, matching this
 * project's established pattern). */
static constexpr std::array<const char*, 6> kButtonFaceExcludedTypes = {
    "toggle", "slider", "text", "dropdown", "button", "button-group"
};

static void warnExcludedButtonFaceTypes(const YAML::Node& widgets,
                                        const std::string& buttonKey,
                                        Diags& diags)
{
    for (auto it = widgets.begin(); it != widgets.end(); ++it) {
        std::string childKey = it->first.as<std::string>();
        YAML::Node  w = it->second;
        if (!w.IsMap()) continue;

        std::string type;
        if (w["type"] && w["type"].IsScalar())
            type = w["type"].as<std::string>();

        if (inEnum(qs(type), kButtonFaceExcludedTypes)) {
            diag(diags, Severity::Warning, buttonKey, "widgets",
                 QStringLiteral("'%1' (type '%2') is an interactive control and is "
                                "not allowed inside a button face; it may overlap "
                                "the button's own tap target")
                     .arg(qs(childKey), qs(type)));
        }

        if ((type == "row" || type == "grid") && w["widgets"] && w["widgets"].IsMap())
            warnExcludedButtonFaceTypes(w["widgets"], buttonKey, diags);
    }
}

/* idPrefix: the effective ID-path prefix to use when looking up this node's
 * OWN children in idMap, if this node turns out to be a container (row/grid)
 * — irrelevant otherwise, since every other widget type resolves its own
 * children's ID paths from `key` directly. Container types are transparent
 * to ID assignment (their own name is never a path segment — see
 * isContainer()), so idPrefix must be threaded explicitly rather than
 * derived from `key`: a row/grid reached via a button's face (key =
 * "btn_key.container_key") needs idPrefix = "btn_key" (skipping the
 * container's own throwaway key), while a row/grid reached via ordinary
 * top-level/nested-container recursion needs idPrefix = "" (unchanged
 * through any number of container hops — see collectPathsRecursive's
 * matching isContainer() recursion, which also keeps prefix unchanged). */
static WidgetDef buildTopLevelWidget(const std::string& key,
                                     const YAML::Node& node,
                                     uint8_t widgetId,
                                     const std::string& idPrefix,
                                     const std::map<std::string, uint8_t>& idMap,
                                     Diags& diags)
{
    WidgetDef w;
    w.keyPath  = qs(key);
    w.widgetId = widgetId;
    w.type     = widgetTypeFromString(nodeStr(node, "type"));
    w.label    = nodeStr(node, "label");

    if (w.type == WidgetType::Unknown) {
        diag(diags, Severity::Error, key, "type",
             QStringLiteral("unknown widget type: '%1'")
                 .arg(nodeStr(node, "type")));
        return w;
    }

    switch (w.type) {
    case WidgetType::Display: {
        w.unit   = nodeStr(node, "unit");
        w.format = nodeStr(node, "format", QStringLiteral("%.2f"));
        w.displayStyle = nodeStr(node, "style", QStringLiteral("default"));
        if (!inEnum(w.displayStyle, UDisplaySchema::kDisplayStyles)) {
            diag(diags, Severity::Error, key, "style",
                 QStringLiteral("unknown display style '%1'; valid values: default, large")
                     .arg(w.displayStyle));
        }
        break;
    }

    case WidgetType::Led: {
        QString rawColor = nodeStr(node, "color", QStringLiteral("#00d4aa"));
        /* LED color must be exactly 6-digit hex per schema. */
        bool valid = (rawColor.size() == 7 && rawColor[0] == '#');
        if (valid) {
            for (int i = 1; i < 7; ++i) {
                char c = rawColor[i].toLatin1();
                if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') ||
                      (c >= 'A' && c <= 'F'))) {
                    valid = false;
                    break;
                }
            }
        }
        if (!valid) {
            diag(diags, Severity::Warning, key, "color",
                 QStringLiteral("invalid LED color '%1'; must be 6-digit hex (e.g. #ff0000)")
                     .arg(rawColor));
            w.color = QStringLiteral("#00d4aa");
        } else {
            w.color = rawColor;
        }
        break;
    }

    case WidgetType::Button: {
        w.shape = nodeStr(node, "shape", QStringLiteral("rect"));
        if (!inEnum(w.shape, UDisplaySchema::kButtonShapes)) {
            diag(diags, Severity::Error, key, "shape",
                 QStringLiteral("unknown button shape '%1'; valid values: rect, circle, square")
                     .arg(w.shape));
        }
        if (node["widgets"] && node["widgets"].IsMap()) {
            warnExcludedButtonFaceTypes(node["widgets"], key, diags);
            for (auto ci = node["widgets"].begin();
                 ci != node["widgets"].end(); ++ci) {
                std::string ck = ci->first.as<std::string>();
                std::string cp = key + "." + ck;
                uint8_t cid = idMap.count(cp) ? idMap.at(cp) : 0;
                /* idPrefix = key (the button's own path): if this face
                 * child is itself a container (row/grid), ITS children's ID
                 * lookups must skip the container's own throwaway key `ck`
                 * and use the button's path instead — see buildTopLevelWidget's
                 * idPrefix doc comment. */
                WidgetDef child = buildTopLevelWidget(cp, ci->second, cid, key, idMap, diags);
                child.flex = parseFlex(ci->second, key, diags);
                child.align = parseAlign(ci->second, key, "align", kRowGridAligns, diags);
                w.children.append(child);
            }
        } else if (node["children"] && node["children"].IsMap()) {
            /* Legacy key, renamed to `widgets:` — warn instead of silently
             * dropping the button's face content. The client parses
             * device-supplied YAML directly (no schema validation at
             * runtime), so stale/hand-authored firmware YAML using the old
             * key must not lose data without a diagnostic. */
            diag(diags, Severity::Warning, key, "children",
                 QStringLiteral("'children:' is deprecated for button widgets; "
                                 "use 'widgets:' instead — this button's face content "
                                 "was not parsed"));
        }
        break;
    }

    case WidgetType::ButtonGroup: {
        w.groupLayout = nodeStr(node, "layout", QStringLiteral("grid"));
        if (!inEnum(w.groupLayout, UDisplaySchema::kButtonGroupLayouts)) {
            diag(diags, Severity::Error, key, "layout",
                 QStringLiteral("unknown button-group layout '%1'; valid values: grid, dpad")
                     .arg(w.groupLayout));
        }
        if (node["items"] && node["items"].IsMap()) {
            int itemCount = 0;
            for (auto ii = node["items"].begin();
                 ii != node["items"].end(); ++ii) {
                ++itemCount;
                std::string ik = ii->first.as<std::string>();
                std::string ip = key + "." + ik;
                ButtonGroupItem item;
                item.keyPath  = qs(ip);
                item.widgetId = idMap.count(ip) ? idMap.at(ip) : 0;
                item.label    = nodeStr(ii->second, "label");
                item.position = nodeStr(ii->second, "position");
                w.groupItems.append(item);
            }
            if (itemCount < 2) {
                diag(diags, Severity::Warning, key, "items",
                     QStringLiteral("button-group requires at least 2 items; found %1")
                         .arg(itemCount));
            }
        }
        break;
    }

    case WidgetType::Slider: {
        if (node["min"] && node["min"].IsScalar())
            w.sliderMin  = node["min"].as<double>();
        if (node["max"] && node["max"].IsScalar())
            w.sliderMax  = node["max"].as<double>();
        if (node["step"] && node["step"].IsScalar()) {
            w.sliderStep = node["step"].as<double>();
            if (w.sliderStep <= 0.0) {
                diag(diags, Severity::Warning, key, "step",
                     QStringLiteral("slider step must be > 0; got %1; using 1")
                         .arg(w.sliderStep));
                w.sliderStep = 1.0;
            }
        }
        if (w.sliderMax <= w.sliderMin) {
            diag(diags, Severity::Warning, key, "max",
                 QStringLiteral("slider max (%1) must be greater than min (%2)")
                     .arg(w.sliderMax).arg(w.sliderMin));
        }
        w.unit = nodeStr(node, "unit");
        break;
    }

    case WidgetType::Text: {
        QString rawMode = nodeStr(node, "mode", QStringLiteral("ro"));
        if (!inEnum(rawMode, UDisplaySchema::kTextModes)) {
            diag(diags, Severity::Error, key, "mode",
                 QStringLiteral("unknown text mode '%1'; valid values: ro, rw").arg(rawMode));
        }
        w.textMode        = (rawMode == u"ro") ? QStringLiteral("readonly") : rawMode;
        w.defaultTextMode = w.textMode;
        w.textPlaceholder = nodeStr(node, "placeholder");
        if (node["maxlength"] && node["maxlength"].IsScalar()) {
            int ml = node["maxlength"].as<int>();
            if (ml < 1 || ml > 255) {
                diag(diags, Severity::Warning, key, "maxlength",
                     QStringLiteral("maxlength %1 out of range [1, 255]").arg(ml));
                ml = qBound(1, ml, 255);
            }
            w.textMaxLength = ml;
        }
        break;
    }

    case WidgetType::Dropdown:
        if (node["items"] && node["items"].IsMap()) {
            for (auto ii = node["items"].begin();
                 ii != node["items"].end(); ++ii) {
                DropdownItem item;
                item.key   = qs(ii->first.as<std::string>());
                item.label = qs(ii->second.IsScalar()
                                ? ii->second.as<std::string>() : "");
                w.dropdownItems.append(item);
            }
        }
        break;

    case WidgetType::Label:
        w.labelText  = nodeStr(node, "text");
        w.labelStyle = nodeStr(node, "style", QStringLiteral("body"));
        if (!inEnum(w.labelStyle, UDisplaySchema::kLabelStyles)) {
            diag(diags, Severity::Error, key, "style",
                 QStringLiteral("unknown label style '%1'; valid values: heading, body, caption")
                     .arg(w.labelStyle));
        }
        w.labelAlign = parseAlign(node, key, "textAlign", kLabelAligns, diags);
        break;

    case WidgetType::Row:
    case WidgetType::Grid:
        /* Nested row/grid: recurse so depth-2+ layouts parse their children.
         * Mirrors the depth-1 handling in buildAndAppendWidgets. */
        if (w.type == WidgetType::Grid)
            w.gridColumns = parseGridColumns(node, key, diags);
        w.align = parseAlign(node, key, "align", kRowGridAligns, diags);
        if (node["widgets"] && node["widgets"].IsMap()) {
            for (auto ci = node["widgets"].begin();
                 ci != node["widgets"].end(); ++ci) {
                std::string ck = ci->first.as<std::string>();
                /* Container transparency: idPrefix carries through unchanged
                 * from whatever scope this row/grid was reached in — see
                 * buildTopLevelWidget's idPrefix doc comment. */
                std::string idPath = idPrefix.empty() ? ck : idPrefix + "." + ck;
                uint8_t cid = idMap.count(idPath) ? idMap.at(idPath) : 0;
                appendRowGridChild(w, ck, ci->second, cid, idPrefix, idMap, diags);
            }
        }
        break;

    default:
        break;
    }

    /* debug_state: optional design-mode preview value, per-type coercion.
     * Per-field try-catch so a type mismatch emits a Warning instead of
     * propagating a YAML::BadConversion up to the outer catch. */
    if (node["debug_state"] && node["debug_state"].IsScalar()) {
        switch (w.type) {
        case WidgetType::Display:
        case WidgetType::Slider:
            try { w.debugValue = node["debug_state"].as<double>(); }
            catch (const YAML::BadConversion&) {
                diag(diags, Severity::Warning, key, "debug_state",
                     QStringLiteral("expected number"));
            }
            break;
        case WidgetType::Led:
        case WidgetType::Toggle:
            try { w.debugValue = node["debug_state"].as<bool>(); }
            catch (const YAML::BadConversion&) {
                diag(diags, Severity::Warning, key, "debug_state",
                     QStringLiteral("expected boolean"));
            }
            break;
        case WidgetType::RgbLed:
            try { w.debugValue = node["debug_state"].as<int>(); }
            catch (const YAML::BadConversion&) {
                diag(diags, Severity::Warning, key, "debug_state",
                     QStringLiteral("expected integer"));
            }
            break;
        case WidgetType::Text:
        case WidgetType::Dropdown:
            try { w.debugValue = qs(node["debug_state"].as<std::string>()); }
            catch (const YAML::BadConversion&) {
                diag(diags, Severity::Warning, key, "debug_state",
                     QStringLiteral("expected string"));
            }
            break;
        default:
            break;
        }
    }

    return w;
}

/* Parses one row/grid child (from `node`, keyed `key`) via
 * buildTopLevelWidget, then applies the child-level flex/align overrides.
 * Shared by both places a row/grid's `widgets:` map is walked: a top-level
 * row/grid in buildAndAppendWidgets, and a nested row/grid inside
 * buildTopLevelWidget's own Row/Grid case (depth-2+). Extracted after the
 * two call sites drifted out of sync once already (one had the Label
 * align guard, the other didn't) — single source of truth from here on. */
static void appendRowGridChild(WidgetDef& parent, const std::string& key,
                               const YAML::Node& node, uint8_t widgetId,
                               const std::string& idPrefix,
                               const std::map<std::string, uint8_t>& idMap,
                               Diags& diags)
{
    WidgetDef child = buildTopLevelWidget(key, node, widgetId, idPrefix, idMap, diags);
    child.flex = parseFlex(node, key, diags);
    child.align = parseAlign(node, key, "align", kRowGridAligns, diags);
    parent.children.append(child);
}

/* ══════════════════════════════════════════════════════════════════════════
 *  Recursive widget list builder
 * ══════════════════════════════════════════════════════════════════════════ */

static void buildAndAppendWidgets(const YAML::Node& widgets,
                                   const std::map<std::string, uint8_t>& idMap,
                                   QList<WidgetDef>& out,
                                   Diags& diags)
{
    for (auto it = widgets.begin(); it != widgets.end(); ++it) {
        std::string key  = it->first.as<std::string>();
        YAML::Node  node = it->second;
        if (!node.IsMap()) {
            diag(diags, Severity::Warning, key, "type",
                 QStringLiteral("widget entry is not a map; skipped"));
            continue;
        }

        std::string type;
        if (node["type"] && node["type"].IsScalar())
            type = node["type"].as<std::string>();

        if (type == "section") {
            WidgetDef s;
            s.keyPath  = qs(key);
            s.widgetId = 0;
            s.type     = WidgetType::Section;
            s.label    = nodeStr(node, "label");
            if (node["collapsible"] && node["collapsible"].IsScalar())
                s.collapsible = node["collapsible"].as<bool>();
            int sectionRow = static_cast<int>(out.size());
            out.append(s);
            int childrenStart = static_cast<int>(out.size());
            if (node["widgets"] && node["widgets"].IsMap())
                buildAndAppendWidgets(node["widgets"], idMap, out, diags);
            if (s.collapsible) {
                for (int i = childrenStart; i < static_cast<int>(out.size()); ++i)
                    if (out[i].sectionOwnerRow == -1)
                        out[i].sectionOwnerRow = sectionRow;
            }

        } else if (type == "row" || type == "grid") {
            WidgetDef w;
            w.keyPath  = qs(key);
            w.widgetId = 0;
            w.type     = (type == "row") ? WidgetType::Row : WidgetType::Grid;
            w.label    = nodeStr(node, "label");
            if (type == "grid")
                w.gridColumns = parseGridColumns(node, key, diags);
            w.align = parseAlign(node, key, "align", kRowGridAligns, diags);
            if (node["widgets"] && node["widgets"].IsMap()) {
                for (auto ci = node["widgets"].begin();
                     ci != node["widgets"].end(); ++ci) {
                    std::string ck = ci->first.as<std::string>();
                    uint8_t cid = idMap.count(ck) ? idMap.at(ck) : 0;
                    appendRowGridChild(w, ck, ci->second, cid, /*idPrefix=*/{}, idMap, diags);
                }
            }
            out.append(w);

        } else {
            uint8_t wid = idMap.count(key) ? idMap.at(key) : 0;
            WidgetDef w = buildTopLevelWidget(key, node, wid, /*idPrefix=*/{}, idMap, diags);
            w.flex = parseFlex(node, key, diags);
            out.append(w);
        }
    }
}

/* ══════════════════════════════════════════════════════════════════════════
 *  Style parsing
 * ══════════════════════════════════════════════════════════════════════════ */

/* Returns true if s is a valid CSS hex color (#rgb, #rrggbb, #rrggbbaa)
 * or the keyword "transparent". Invalid strings are silently ignored. */
static bool isValidColor(const std::string& s)
{
    if (s == "transparent") return true;
    if (s.empty() || s[0] != '#') return false;
    size_t len = s.size();
    if (len != 4 && len != 7 && len != 9) return false;
    for (size_t i = 1; i < len; ++i) {
        char c = s[i];
        if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F')))
            return false;
    }
    return true;
}

static void parseStyles(const YAML::Node& styleNode,
                        QMap<QString, StyleToken>& stylesOut)
{
    StyleToken defaults;

    if (styleNode["default"] && styleNode["default"].IsMap()) {
        const YAML::Node& d = styleNode["default"];
#define APPLY_TOKEN(field) \
        if (d[#field] && d[#field].IsScalar()) { \
            auto sv = d[#field].as<std::string>(); \
            if (isValidColor(sv)) defaults.field = qs(sv); \
        }
        APPLY_TOKEN(background)
        APPLY_TOKEN(surface)
        APPLY_TOKEN(text)
        APPLY_TOKEN(text_muted)
        APPLY_TOKEN(text_heading)
        APPLY_TOKEN(border)
        APPLY_TOKEN(line)
        APPLY_TOKEN(accent)
        APPLY_TOKEN(button)
        APPLY_TOKEN(button_text)
        APPLY_TOKEN(led_on)
        APPLY_TOKEN(led_off)
        APPLY_TOKEN(led_border)
        APPLY_TOKEN(success)
        APPLY_TOKEN(warning)
        APPLY_TOKEN(error)
#undef APPLY_TOKEN
    }
    stylesOut[QStringLiteral("default")] = defaults;

    for (auto it = styleNode.begin(); it != styleNode.end(); ++it) {
        std::string name = it->first.as<std::string>();
        if (name == "default") continue;
        if (!it->second.IsMap()) continue;

        StyleToken t = defaults;
        const YAML::Node& n = it->second;
#define APPLY_TOKEN(field) \
        if (n[#field] && n[#field].IsScalar()) { \
            auto sv = n[#field].as<std::string>(); \
            if (isValidColor(sv)) t.field = qs(sv); \
        }
        APPLY_TOKEN(background)
        APPLY_TOKEN(surface)
        APPLY_TOKEN(text)
        APPLY_TOKEN(text_muted)
        APPLY_TOKEN(text_heading)
        APPLY_TOKEN(border)
        APPLY_TOKEN(line)
        APPLY_TOKEN(accent)
        APPLY_TOKEN(button)
        APPLY_TOKEN(button_text)
        APPLY_TOKEN(led_on)
        APPLY_TOKEN(led_off)
        APPLY_TOKEN(led_border)
        APPLY_TOKEN(success)
        APPLY_TOKEN(warning)
        APPLY_TOKEN(error)
#undef APPLY_TOKEN
        stylesOut[qs(name)] = t;
    }
}

/* ══════════════════════════════════════════════════════════════════════════
 *  Public API
 * ══════════════════════════════════════════════════════════════════════════ */

bool YamlParser::parse(const QByteArray& yamlBytes,
                       QList<WidgetDef>& widgetsOut,
                       QString& deviceNameOut,
                       QString& deviceVersionOut,
                       QStringList& capabilitiesOut,
                       QMap<QString, StyleToken>& stylesOut)
{
    m_error.clear();
    m_diagnostics.clear();
    deviceNameOut.clear();
    deviceVersionOut.clear();
    capabilitiesOut.clear();
    stylesOut.clear();
    YAML::Node doc;
    try {
        doc = YAML::Load(std::string(yamlBytes.constData(), yamlBytes.size()));

        if (!doc.IsMap()) {
            m_error = QStringLiteral("YAML root is not a mapping");
            return false;
        }
    } catch (const YAML::Exception& e) {
        m_error = QStringLiteral("YAML parse error: %1").arg(qs(e.what()));
        return false;
    }

    try {
    if (doc["device"] && doc["device"].IsMap()) {
        deviceNameOut    = nodeStr(doc["device"], "name");
        deviceVersionOut = nodeStr(doc["device"], "version");

        if (doc["device"]["capabilities"] && doc["device"]["capabilities"].IsSequence()) {
            for (auto cap : doc["device"]["capabilities"])
                capabilitiesOut.append(qs(cap.as<std::string>()));
        }
    }

    if (doc["style"] && doc["style"].IsMap())
        parseStyles(doc["style"], stylesOut);
    else
        stylesOut[QStringLiteral("default")] = StyleToken{};

    if (!doc["widgets"] || !doc["widgets"].IsMap()) {
        m_error = QStringLiteral("Missing or invalid 'widgets' map");
        return false;
    }
    const YAML::Node& widgets = doc["widgets"];

    auto entries = collectPaths(widgets);
    if (entries.size() > 240) {
        m_error = QStringLiteral("Too many widget paths (%1); maximum is 240")
                      .arg(static_cast<int>(entries.size()));
        return false;
    }
    /* Duplicate id-paths: two widgets resolving to the same protocol ID.
     * Containers (section/row/grid) don't contribute their own name to the
     * path — two same-named leaves under different sibling containers that
     * share a transparent-prefix ancestor (e.g. two same-named leaves in
     * two different row/grid children of the same button face) collide
     * silently here otherwise: idMap[e.path] = nextId++ below would just
     * overwrite, both ending up with the LAST-written ID, one real ID slot
     * wasted, and STATE_UPDATE messages cross-applying between two
     * semantically unrelated widgets. entries is sorted by path (see
     * collectPaths()), so adjacent duplicates catch every collision in one
     * pass. */
    for (size_t i = 1; i < entries.size(); ++i) {
        if (entries[i].path == entries[i - 1].path) {
            m_error = QStringLiteral(
                "Duplicate widget ID path '%1' — two widgets resolve to the same "
                "protocol ID. Check for same-named leaves under different sibling "
                "containers (row/grid/section, or a button face's nested containers) "
                "that share a transparent-prefix ancestor.")
                          .arg(qs(entries[i].path));
            return false;
        }
    }
    std::map<std::string, uint8_t> idMap;
    uint8_t nextId = 0x10;
    for (auto& e : entries)
        idMap[e.path] = nextId++;

    widgetsOut.clear();
    buildAndAppendWidgets(widgets, idMap, widgetsOut, m_diagnostics);

    /* Any Error diagnostic is fatal — report the first one. */
    for (const auto& d : m_diagnostics) {
        if (d.severity == Severity::Error) {
            m_error = QStringLiteral("[%1.%2] %3")
                          .arg(d.widgetKey, d.field, d.message);
            return false;
        }
    }

    return true;
    } catch (const YAML::Exception& e) {
        m_error = QStringLiteral("YAML error: %1").arg(qs(e.what()));
        return false;
    }
}
