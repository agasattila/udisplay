// SPDX-License-Identifier: LGPL-3.0-only
// Copyright (c) 2026 Attila Agas

/**
 * Plain-data structs for uDisplay widget definitions parsed from YAML.
 *
 * YamlParser produces a QList<WidgetDef>.
 * WidgetModel wraps that list for QML.
 * DeviceController owns both.
 */
#pragma once

#include <QList>
#include <QString>
#include <QVariant>
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

/* ── Dropdown item ────────────────────────────────────────────────────────── */
struct DropdownItem {
    QString key;    /* YAML key, e.g. "sta" */
    QString label;  /* Display label, e.g. "Station" */
};

/* ── ButtonGroup item ────────────────────────────────────────────────────── */
/* NOTE: ButtonGroupItems intentionally do NOT participate in the unified
 * WidgetDef.children model. button-group items are events-only (BUTTON_PRESS)
 * with no state pushed from the device — they have no value model and do not
 * need recursive child rendering. WidgetDef.children is for stateful children. */
struct ButtonGroupItem {
    QString  keyPath;   /* e.g. "mode_sel.ac" */
    uint8_t  widgetId;
    QString  label;
    QString  position;  /* "top"|"right"|"bottom"|"left"|"center" — dpad only */
};

/* ── Main widget definition ─────────────────────────────────────────────── */
struct WidgetDef {
    /* Common */
    QString    keyPath;     /* YAML key path, e.g. "fire_btn" or "mode_sel.ac" */
    uint8_t    widgetId;    /* 0x10–0xFF, assigned by YamlParser */
    WidgetType type;
    QString    label;

    /* Runtime properties (reset to these on reconnect) */
    bool enabled = true;
    bool visible = true;

    /* Current value (updated by DeviceController on STATE_UPDATE) */
    QVariant value;

    /* Design-mode preview value — populated by YamlParser from debug_state:,
     * applied by DeviceController::applyParsedYaml() in design mode only. */
    QVariant debugValue;

    /* ── Type-specific fields ─────────────────────────────────────── */

    /* display */
    QString unit;
    QString format;        /* printf-style, default "%.2f" */
    QString displayStyle;  /* "default" | "large" */

    /* button */
    QString          shape;   /* "rect" | "circle" | "square" */
    QString          color;   /* "#rrggbb" */

    /* button-group */
    QString               groupLayout; /* "grid" | "dpad" */
    QList<ButtonGroupItem> groupItems;

    /* slider */
    double  sliderMin  = 0.0;
    double  sliderMax  = 100.0;
    double  sliderStep = 1.0;
    /* unit field is shared with display */

    /* text */
    QString textMode;        /* "readonly" | "rw" */
    QString defaultTextMode; /* YAML-declared default, used by resetProperty */
    QString textPlaceholder;
    int     textMaxLength = 255;

    /* dropdown */
    QList<DropdownItem> dropdownItems;

    /* label */
    QString labelText;   /* display text */
    QString labelStyle;  /* "heading" | "body" | "caption" */
    QString labelAlign  = QStringLiteral("left"); /* "left"|"right"|"center"|"justify" */

    /* layout */
    int flex        = 0; /* layout weight inside row/grid (0 = auto-width, no stretch) */
    int gridColumns = 2; /* column count for grid containers */

    /* Content alignment ("left"|"right"|"center"). Dual purpose, same
     * convention as `flex` above: on a row/grid WidgetDef, the resolved
     * default applied to children that don't declare their own (always a
     * concrete value — YamlParser fills "left" when the row/grid's own
     * YAML omits align:). On a WidgetDef that IS a row/grid child, an
     * empty string means "no override, inherit the parent's align" — only
     * a non-empty value here (from the child's own align: key) overrides
     * the container's default for that one child. Do NOT default this to
     * "left": that would make every child indistinguishable from one that
     * explicitly opted into left-alignment, breaking the inherit-by-default
     * behavior. */
    QString align;

    /* section collapse */
    bool collapsible    = false; /* section only: can the user collapse it */
    int  sectionOwnerRow = -1;   /* flat-model row of parent collapsible section, or -1 */

    /* children — unified child list for button (face widgets) and row/grid/section
     * (layout children). Depth-1 for PR 1; PR 2 extends QML rendering to
     * unlimited depth. See TODO-031. */
    QList<WidgetDef> children;
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
    return WidgetType::Unknown;
}
