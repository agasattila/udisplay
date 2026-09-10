// SPDX-License-Identifier: LGPL-3.0-only
// Copyright (c) 2026 Attila Agas

/**
 * Plain-data structs for uDisplay widget definitions parsed from YAML.
 *
 * YamlParser produces a QList<WidgetDef> — a FLAT list. Every widget,
 * regardless of nesting depth in the source YAML (button face children, row/
 * grid/section layout children, button-group items, dpad items), is its own
 * row in declaration order. `parentId` links a row to its container: -1 for
 * top-level widgets, otherwise the flat-list row index of the parent widget
 * (NOT the parent's widgetId — containers like row/grid/section always have
 * widgetId 0, so multiple sibling containers would collide if parentId were
 * widgetId-keyed; and 0 is itself a valid row index, so it cannot double as
 * the "no parent" sentinel either — that's why -1, not 0, means top-level).
 * parentId is only valid until the next
 * WidgetModel::setWidgets() call (row indices are reassigned on every reset).
 *
 * Every type-specific attribute (slider min/max/step, button shape/color,
 * dropdown items, section collapsible, ...) lives in `props`, a QVariantMap
 * built once by YamlParser at parse time — not projected on every QML read.
 * WidgetModel wraps the flat list for QML; a container's children are
 * obtained via WidgetModel::childModel(parentId), not by enumerating an
 * "items" key inside props.
 *
 * DeviceController owns both.
 */
#pragma once

#include <QList>
#include <QString>
#include <QVariant>
#include <QVariantMap>
#include <cstdint>

/* ── Widget type enum ───────────────────────────────────────────────────── */
enum class WidgetType {
    /* Interactive / data widgets */
    Display,
    Led,
    RgbLed,
    Button,
    ButtonGroup,
    Slider,
    Toggle,
    Text,
    Dropdown,
    /* Decoration types (no widget ID, no protocol exchange) */
    Label,
    Separator,
    /* Container types (transparent to ID assignment) */
    Section,
    Row,
    Grid,
    Dpad,
    Unknown,
};

QString widgetTypeName(WidgetType t);
WidgetType widgetTypeFromString(const QString& s);

/* ── Global style token set ─────────────────────────────────────────────── */
struct StyleToken {
    QString background   = QStringLiteral("#0d0d1a");
    QString surface      = QStringLiteral("#1a1a2e");
    QString text         = QStringLiteral("#c0c0c0");
    QString text_muted   = QStringLiteral("#888888");
    QString text_heading = QStringLiteral("#e0e0e0");
    QString border       = QStringLiteral("#1e1e3a");
    QString line         = QStringLiteral("#1e1e3a");
    QString accent       = QStringLiteral("#00d4aa");
    QString button       = QStringLiteral("#00d4aa");
    QString button_text  = QStringLiteral("#0d0d1a");
    QString led_on       = QStringLiteral("#ffffff");
    QString led_off      = QStringLiteral("transparent");
    QString led_border   = QStringLiteral("#ffffff");
    QString success      = QStringLiteral("#00d4aa");
    QString warning      = QStringLiteral("#f5a623");
    QString error        = QStringLiteral("#e05555");
};

/* ── Main widget definition ─────────────────────────────────────────────── */
struct WidgetDef {
    /* Common — every widget, regardless of type, has these. */
    QString    keyPath;     /* YAML key path, e.g. "fire_btn" or "mode_sel.ac" */
    uint8_t    widgetId;    /* 0x10–0xFF, assigned by YamlParser; 0 for containers/decorations */
    WidgetType type;
    QString    label;

    /* parentId: flat-list row index of the containing widget, or -1 for
     * top-level. See file header comment — NOT a widgetId, and NOT 0 for
     * "no parent" (0 is a valid row index). */
    int parentId = -1;

    /* Runtime properties (reset to these on reconnect) */
    bool enabled = true;
    bool visible = true;

    /* Current value (updated by DeviceController on STATE_UPDATE) */
    QVariant value;

    /* Design-mode preview value — populated by YamlParser from debug_state:,
     * applied by DeviceController::applyParsedYaml() in design mode only. */
    QVariant debugValue;

    /* Visual style variant, meaning depends on type (display: "default"|
     * "large"; label: "heading"|"body"|"caption"; empty for types that don't
     * have one). Also copied into props["style"] at parse time so existing
     * QML (DisplayWidget.qml, LabelWidget.qml) keeps reading props.style
     * unchanged. */
    QString style;

    /* Layout weight/alignment inside a row/grid container. flex=0 means
     * auto-width (no stretch). align is a null QString by default — see the
     * detailed inherit-vs-override note this used to carry in WidgetDef;
     * still true here: only a non-empty value overrides the parent
     * container's own resolved default. */
    int     flex  = 0;
    QString align;

    /* Every other, type-specific attribute (slider min/max/step, button
     * shape/color, dropdown items, section collapsible, dpad/button-group
     * item position, grid columns, button-group layout, text mode/
     * placeholder/maxlength, ...) — built once by YamlParser at parse time.
     * WidgetModel's PropsRole returns this directly; it does not rebuild it
     * per read. */
    QVariantMap props;
};

/* ── WidgetType helpers ─────────────────────────────────────────────────── */
inline QString widgetTypeName(WidgetType t)
{
    switch (t) {
    case WidgetType::Display:     return QStringLiteral("display");
    case WidgetType::Led:         return QStringLiteral("led");
    case WidgetType::RgbLed:      return QStringLiteral("rgbled");
    case WidgetType::Button:      return QStringLiteral("button");
    case WidgetType::ButtonGroup: return QStringLiteral("button-group");
    case WidgetType::Slider:      return QStringLiteral("slider");
    case WidgetType::Toggle:      return QStringLiteral("toggle");
    case WidgetType::Text:        return QStringLiteral("text");
    case WidgetType::Dropdown:    return QStringLiteral("dropdown");
    case WidgetType::Label:       return QStringLiteral("label");
    case WidgetType::Separator:   return QStringLiteral("separator");
    case WidgetType::Section:     return QStringLiteral("section");
    case WidgetType::Row:         return QStringLiteral("row");
    case WidgetType::Grid:        return QStringLiteral("grid");
    case WidgetType::Dpad:        return QStringLiteral("dpad");
    default:                      return QStringLiteral("unknown");
    }
}

inline WidgetType widgetTypeFromString(const QString& s)
{
    if (s == QLatin1String("display"))      return WidgetType::Display;
    if (s == QLatin1String("led"))          return WidgetType::Led;
    if (s == QLatin1String("rgbled"))       return WidgetType::RgbLed;
    if (s == QLatin1String("button"))       return WidgetType::Button;
    if (s == QLatin1String("button-group")) return WidgetType::ButtonGroup;
    if (s == QLatin1String("slider"))       return WidgetType::Slider;
    if (s == QLatin1String("toggle"))       return WidgetType::Toggle;
    if (s == QLatin1String("text"))         return WidgetType::Text;
    if (s == QLatin1String("dropdown"))     return WidgetType::Dropdown;
    if (s == QLatin1String("label"))        return WidgetType::Label;
    if (s == QLatin1String("separator"))    return WidgetType::Separator;
    if (s == QLatin1String("section"))      return WidgetType::Section;
    if (s == QLatin1String("row"))          return WidgetType::Row;
    if (s == QLatin1String("grid"))         return WidgetType::Grid;
    if (s == QLatin1String("dpad"))         return WidgetType::Dpad;
    return WidgetType::Unknown;
}
