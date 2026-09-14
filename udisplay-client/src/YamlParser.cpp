// SPDX-License-Identifier: LGPL-3.0-only
// Copyright (c) 2026 Attila Agas

#include "YamlParser.h"
#include "udisplay_schema_enums.h"
#include <yaml-cpp/yaml.h>
#include <algorithm>
#include <array>
#include <QStringList>
#include <QVariantList>
#include <QVariantMap>

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

/* Row/grid/dpad child flex weight. Omitted (or non-scalar) -> 0, meaning
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
    return type == "section" || type == "row" || type == "grid" || type == "dpad";
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
 *  Widget parsing — flat model
 *
 *  buildWidget() appends exactly one WidgetDef row for `key`/`node` to `out`
 *  (parentId = `parentId`), then — if the widget is a container type with
 *  its own children (button face, row/grid/dpad members, button-group
 *  items) — recurses to append each child immediately after, with
 *  parentId set to the row index this call just appended. The tree is
 *  walked top-down and flattened as it goes; nothing is built bottom-up and
 *  attached to a parent's `.children` (there is no such field anymore).
 *  Returns the row index this call appended, so callers can post-process
 *  that specific row (flex/align/position overrides) after any nested
 *  children have already been appended after it.
 * ══════════════════════════════════════════════════════════════════════════ */

static int buildWidget(const std::string& key,
                       const YAML::Node& node,
                       uint8_t widgetId,
                       const std::string& idPrefix,
                       const std::map<std::string, uint8_t>& idMap,
                       int parentId,
                       QList<WidgetDef>& out,
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
 * OWN children in idMap, if this node turns out to be a container (row/
 * grid/dpad) — irrelevant otherwise, since every other widget type resolves
 * its own children's ID paths from `key` directly. Container types are
 * transparent to ID assignment (their own name is never a path segment —
 * see isContainer()), so idPrefix must be threaded explicitly rather than
 * derived from `key`: a row/grid reached via a button's face (key =
 * "btn_key.container_key") needs idPrefix = "btn_key" (skipping the
 * container's own throwaway key), while a row/grid reached via ordinary
 * top-level/nested-container recursion needs idPrefix = "" (unchanged
 * through any number of container hops — see collectPathsRecursive's
 * matching isContainer() recursion, which also keeps prefix unchanged). */
static int buildWidget(const std::string& key,
                       const YAML::Node& node,
                       uint8_t widgetId,
                       const std::string& idPrefix,
                       const std::map<std::string, uint8_t>& idMap,
                       int parentId,
                       QList<WidgetDef>& out,
                       Diags& diags)
{
    WidgetDef w;
    w.keyPath  = qs(key);
    w.widgetId = widgetId;
    w.type     = widgetTypeFromString(nodeStr(node, "type"));
    w.label    = nodeStr(node, "label");
    w.parentId = parentId;

    if (w.type == WidgetType::Unknown) {
        diag(diags, Severity::Error, key, "type",
             QStringLiteral("unknown widget type: '%1'")
                 .arg(nodeStr(node, "type")));
        out.append(w);
        return out.size() - 1;
    }

    switch (w.type) {
    case WidgetType::Display: {
        w.props[QStringLiteral("unit")]   = nodeStr(node, "unit");
        w.props[QStringLiteral("format")] = nodeStr(node, "format", QStringLiteral("%.2f"));
        QString style = nodeStr(node, "style", QStringLiteral("default"));
        if (!inEnum(style, UDisplaySchema::kDisplayStyles)) {
            diag(diags, Severity::Error, key, "style",
                 QStringLiteral("unknown display style '%1'; valid values: default, large")
                     .arg(style));
        }
        w.props[QStringLiteral("style")] = style;
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
            rawColor = QStringLiteral("#00d4aa");
        }
        w.props[QStringLiteral("color")] = rawColor;
        break;
    }

    case WidgetType::RgbLed:
        break;

    case WidgetType::Button: {
        QString shape = nodeStr(node, "shape", QStringLiteral("rect"));
        if (!inEnum(shape, UDisplaySchema::kButtonShapes)) {
            diag(diags, Severity::Error, key, "shape",
                 QStringLiteral("unknown button shape '%1'; valid values: rect, circle, square")
                     .arg(shape));
        }
        w.props[QStringLiteral("shape")] = shape;
        break;
    }

    case WidgetType::ButtonGroup: {
        QString layout = nodeStr(node, "layout", QStringLiteral("grid"));
        if (!inEnum(layout, UDisplaySchema::kButtonGroupLayouts)) {
            diag(diags, Severity::Error, key, "layout",
                 QStringLiteral("unknown button-group layout '%1'; valid values: grid")
                     .arg(layout));
        }
        w.props[QStringLiteral("layout")] = layout;
        break;
    }

    case WidgetType::Dpad:
        break;

    case WidgetType::Slider: {
        double sliderMin = 0.0, sliderMax = 100.0, sliderStep = 1.0;
        if (node["min"] && node["min"].IsScalar())
            sliderMin = node["min"].as<double>();
        if (node["max"] && node["max"].IsScalar())
            sliderMax = node["max"].as<double>();
        if (node["step"] && node["step"].IsScalar()) {
            sliderStep = node["step"].as<double>();
            if (sliderStep <= 0.0) {
                diag(diags, Severity::Warning, key, "step",
                     QStringLiteral("slider step must be > 0; got %1; using 1")
                         .arg(sliderStep));
                sliderStep = 1.0;
            }
        }
        if (sliderMax <= sliderMin) {
            diag(diags, Severity::Warning, key, "max",
                 QStringLiteral("slider max (%1) must be greater than min (%2)")
                     .arg(sliderMax).arg(sliderMin));
        }
        w.props[QStringLiteral("min")]  = sliderMin;
        w.props[QStringLiteral("max")]  = sliderMax;
        w.props[QStringLiteral("step")] = sliderStep;
        w.props[QStringLiteral("unit")] = nodeStr(node, "unit");
        break;
    }

    case WidgetType::Text: {
        QString rawMode = nodeStr(node, "mode", QStringLiteral("ro"));
        if (!inEnum(rawMode, UDisplaySchema::kTextModes)) {
            diag(diags, Severity::Error, key, "mode",
                 QStringLiteral("unknown text mode '%1'; valid values: ro, rw").arg(rawMode));
        }
        QString textMode = (rawMode == u"ro") ? QStringLiteral("readonly") : rawMode;
        w.props[QStringLiteral("mode")]        = textMode;
        w.props[QStringLiteral("defaultMode")] = textMode;
        w.props[QStringLiteral("placeholder")] = nodeStr(node, "placeholder");
        int maxlen = 255;
        if (node["maxlength"] && node["maxlength"].IsScalar()) {
            int ml = node["maxlength"].as<int>();
            if (ml < 1 || ml > 255) {
                diag(diags, Severity::Warning, key, "maxlength",
                     QStringLiteral("maxlength %1 out of range [1, 255]").arg(ml));
                ml = qBound(1, ml, 255);
            }
            maxlen = ml;
        }
        w.props[QStringLiteral("maxlength")] = maxlen;
        break;
    }

    case WidgetType::Dropdown: {
        QVariantList items;
        if (node["items"] && node["items"].IsMap()) {
            for (auto ii = node["items"].begin();
                 ii != node["items"].end(); ++ii) {
                QVariantMap m;
                m[QStringLiteral("key")]   = qs(ii->first.as<std::string>());
                m[QStringLiteral("label")] = qs(ii->second.IsScalar()
                                                ? ii->second.as<std::string>() : "");
                items.append(m);
            }
        }
        w.props[QStringLiteral("items")] = items;
        break;
    }

    case WidgetType::Label: {
        w.props[QStringLiteral("text")] = nodeStr(node, "text");
        QString style = nodeStr(node, "style", QStringLiteral("body"));
        if (!inEnum(style, UDisplaySchema::kLabelStyles)) {
            diag(diags, Severity::Error, key, "style",
                 QStringLiteral("unknown label style '%1'; valid values: heading, body, caption")
                     .arg(style));
        }
        w.props[QStringLiteral("style")] = style;
        w.props[QStringLiteral("labelAlign")] = parseAlign(node, key, "textAlign", kLabelAligns, diags);
        break;
    }

    case WidgetType::Section: {
        bool collapsible = false;
        if (node["collapsible"] && node["collapsible"].IsScalar())
            collapsible = node["collapsible"].as<bool>();
        w.props[QStringLiteral("collapsible")] = collapsible;
        break;
    }

    case WidgetType::Row:
    case WidgetType::Grid:
        if (w.type == WidgetType::Grid)
            w.props[QStringLiteral("columns")] = parseGridColumns(node, key, diags);
        w.align = parseAlign(node, key, "align", kRowGridAligns, diags);
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

    out.append(w);
    const int myRow = out.size() - 1;

    /* ── Children (appended flat, immediately after this row) ──────────── */
    switch (w.type) {
    case WidgetType::Button: {
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
                 * and use the button's path instead. */
                int idx = buildWidget(cp, ci->second, cid, key, idMap, myRow, out, diags);
                out[idx].flex  = parseFlex(ci->second, key, diags);
                out[idx].align = parseAlign(ci->second, key, "align", kRowGridAligns, diags);
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
        if (node["items"] && node["items"].IsMap()) {
            int itemCount = 0;
            for (auto ii = node["items"].begin();
                 ii != node["items"].end(); ++ii) {
                ++itemCount;
                std::string ik = ii->first.as<std::string>();
                std::string ip = key + "." + ik;
                /* Button-group items have no `type:` field in YAML — they
                 * are implicitly button-shaped click targets (BUTTON_PRESS/
                 * RELEASE/CLICK), matching widget_ids.py's "button-group-item"
                 * pseudo-type. Built directly rather than through
                 * buildWidget(), which requires a `type:` key. */
                WidgetDef item;
                item.keyPath  = qs(ip);
                item.widgetId = idMap.count(ip) ? idMap.at(ip) : 0;
                item.type     = WidgetType::Button;
                item.label    = nodeStr(ii->second, "label");
                item.parentId = myRow;
                item.props[QStringLiteral("position")] = nodeStr(ii->second, "position");
                out.append(item);
            }
            if (itemCount < 2) {
                diag(diags, Severity::Warning, key, "items",
                     QStringLiteral("button-group requires at least 2 items; found %1")
                         .arg(itemCount));
            }
        }
        break;
    }

    case WidgetType::Dpad: {
        if (node["widgets"] && node["widgets"].IsMap()) {
            int itemCount = 0;
            for (auto ii = node["widgets"].begin();
                 ii != node["widgets"].end(); ++ii) {
                ++itemCount;
                std::string ik = ii->first.as<std::string>();
                /* Container transparency: dpad is in isContainer() /
                 * widget_ids.py's CONTAINER_TYPES, so its own key is never a
                 * path segment — idPrefix carries through unchanged, same as
                 * the Row/Grid case below. Dpad items now get full
                 * type-specific parsing via buildWidget() (they always carry
                 * an explicit `type:` in YAML, e.g. `type: button`). */
                std::string idPath = idPrefix.empty() ? ik : idPrefix + "." + ik;
                uint8_t cid = idMap.count(idPath) ? idMap.at(idPath) : 0;
                int idx = buildWidget(ik, ii->second, cid, idPrefix, idMap, myRow, out, diags);
                out[idx].props[QStringLiteral("position")] = nodeStr(ii->second, "position");
            }
            if (itemCount < 1) {
                diag(diags, Severity::Warning, key, "widgets",
                     QStringLiteral("dpad requires at least 1 item; found %1")
                         .arg(itemCount));
            }
        }
        break;
    }

    case WidgetType::Row:
    case WidgetType::Grid: {
        /* Nested row/grid: recurse so depth-2+ layouts parse their children.
         * Mirrors the depth-1 handling in buildAndAppendWidgets. */
        if (node["widgets"] && node["widgets"].IsMap()) {
            for (auto ci = node["widgets"].begin();
                 ci != node["widgets"].end(); ++ci) {
                std::string ck = ci->first.as<std::string>();
                /* Container transparency: idPrefix carries through unchanged
                 * from whatever scope this row/grid was reached in. */
                std::string idPath = idPrefix.empty() ? ck : idPrefix + "." + ck;
                uint8_t cid = idMap.count(idPath) ? idMap.at(idPath) : 0;
                int idx = buildWidget(ck, ci->second, cid, idPrefix, idMap, myRow, out, diags);
                out[idx].flex  = parseFlex(ci->second, ck, diags);
                out[idx].align = parseAlign(ci->second, ck, "align", kRowGridAligns, diags);
                out[idx].props[QStringLiteral("position")] = nodeStr(ci->second, "position");
            }
        }
        break;
    }

    default:
        break;
    }

    return myRow;
}

/* ══════════════════════════════════════════════════════════════════════════
 *  Top-level list builder
 * ══════════════════════════════════════════════════════════════════════════ */

static void buildAndAppendWidgets(const YAML::Node& widgets,
                                   const std::map<std::string, uint8_t>& idMap,
                                   int parentId,
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
            bool collapsible = false;
            if (node["collapsible"] && node["collapsible"].IsScalar())
                collapsible = node["collapsible"].as<bool>();
            s.props[QStringLiteral("collapsible")] = collapsible;
            s.parentId = parentId;
            out.append(s);
            int sectionRow = out.size() - 1;
            if (node["widgets"] && node["widgets"].IsMap())
                buildAndAppendWidgets(node["widgets"], idMap, sectionRow, out, diags);

        } else if (type == "row" || type == "grid") {
            /* Top-level row/grid: idPrefix stays empty for its children.
             * Its own flex is never used (nothing above a top-level widget
             * reads its flex weight) — matches original behavior, which
             * never parsed flex for a top-level row/grid either. */
            buildWidget(key, node, 0, /*idPrefix=*/{}, idMap, parentId, out, diags);

        } else {
            uint8_t wid = idMap.count(key) ? idMap.at(key) : 0;
            int idx = buildWidget(key, node, wid, /*idPrefix=*/{}, idMap, parentId, out, diags);
            out[idx].flex = parseFlex(node, key, diags);
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
    buildAndAppendWidgets(widgets, idMap, /*parentId=*/-1, widgetsOut, m_diagnostics);

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
