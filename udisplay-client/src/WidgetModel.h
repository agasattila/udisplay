// SPDX-License-Identifier: LGPL-3.0-only
// Copyright (c) 2026 Attila Agas

/**
 * WidgetModel — QAbstractListModel wrapping a flat QList<WidgetDef>.
 *
 * Only top-level widgets are in the list (declaration order = render order).
 * Children and items are embedded inside their parent widget's data roles.
 *
 * Roles exposed to QML:
 *   widgetId    uint     unique 0x10–0xFF ID (0 for decorations/containers)
 *   type        string   widget type name
 *   label       string
 *   enabled     bool
 *   widgetVisible  bool
 *   value       variant  current device-pushed value (null until first STATE_UPDATE)
 *   props       map      type-specific properties (see below)
 *
 * Props map keys per type:
 *   display:      unit, format, style
 *   button:       shape, color, items (list of {type,widgetId,label,enabled,visible,value,props,flex} — see row/grid)
 *   button-group: layout, items (list of {widgetId, label, position})
 *   slider:       min, max, step, unit
 *   text:         mode, placeholder, maxlength
 *   dropdown:     items (list of {key, label})
 *   label:        text, style, labelAlign
 *   row:          align, items (list of {type,widgetId,label,enabled,visible,value,props,flex})
 *   grid:         columns, align, items (same as row)
 *
 * button/row/grid/section all share the same recursive WidgetDef.children
 * model (see WidgetDef.h) and the same serializeChildren() output; button
 * only emits the "items" props key when it has children (props["items"]
 * is omitted, not empty, for a plain label-only button).
 */
#pragma once

#include "WidgetDef.h"
#include <QAbstractListModel>
#include <QHash>
#include <QSet>
#include <QVariant>

class WidgetModel : public QAbstractListModel
{
    Q_OBJECT

public:
    enum Roles {
        WidgetIdRole = Qt::UserRole + 1,
        TypeRole,
        LabelRole,
        EnabledRole,
        VisibleRole,
        ValueRole,
        PropsRole,
        SectionOwnerRowRole,
    };

    explicit WidgetModel(QObject* parent = nullptr);

    /* Populate / replace the entire widget list. */
    void setWidgets(const QList<WidgetDef>& widgets);

    /* Clear all widgets. */
    void clear();

    /* Update the current value for a widget by its ID.
     * Called by DeviceController on STATE_UPDATE.              */
    void setValue(uint8_t widgetId, const QVariant& value);

    /* Apply a property override.
     * Called by DeviceController on PropertyCommand.           */
    void setProperty(uint8_t targetId, uint8_t propertyId, uint8_t value);

    /* Reset a property to its YAML default. */
    void resetProperty(uint8_t targetId, uint8_t propertyId);

    /* Toggle collapsed state of a collapsible section at flat-model row. */
    Q_INVOKABLE void toggleSection(int row);

    /* ── QAbstractListModel ─────────────────────────────────────── */
    int      rowCount(const QModelIndex& parent = {}) const override;
    QVariant data(const QModelIndex& index, int role = Qt::DisplayRole) const override;
    QHash<int, QByteArray> roleNames() const override;

private:
    int indexForWidgetId(uint8_t id) const;

    /* Traverse m_childPath to locate the WidgetDef for the given descendant ID.
     * Sets outParentRow to path[0] (the top-level m_widgets index).
     * Returns nullptr when id is not in m_childPath. */
    WidgetDef* findDescendant(uint8_t id, int& outParentRow);

    QList<WidgetDef>             m_widgets;
    QHash<uint8_t, int>          m_idToRow;   /* widget_id → row index (top-level only) */
    /* m_childPath maps every descendant widget ID to its traversal path.
     * path[0] = top-level m_widgets index; path[1..n] = child index at each level.
     * Example: toggle at m_widgets[2].children[1].children[0] → {2, 1, 0}.
     * Covers all nesting depths: button face, row/grid/section layout children. */
    QHash<uint8_t, QVector<int>> m_childPath;
    QSet<int>                    m_collapsedSections; /* flat-model rows of collapsed sections */
};
