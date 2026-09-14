// SPDX-License-Identifier: LGPL-3.0-only
// Copyright (c) 2026 Attila Agas

/**
 * WidgetModel — QAbstractListModel wrapping a FLAT QList<WidgetDef>.
 *
 * Every widget, at any nesting depth in the source YAML, is its own row in
 * declaration order. A widget's container is found via its `parentId` (the
 * flat-list row index of its parent, -1 for top-level — see WidgetDef.h). A
 * container's children are obtained via childModel(parentId), which returns
 * a stable, cached ChildModel instance — a lightweight QAbstractListModel
 * that is just a filtered view (index array) onto this same list, with role
 * passthrough. QML containers (row/grid/button face/dpad/button-group) bind
 * their Repeater to that child model instead of enumerating an "items" key
 * inside props.
 *
 * Roles exposed to QML:
 *   widgetId    uint     unique 0x10–0xFF ID (0 for decorations/containers)
 *   type        string   widget type name
 *   label       string
 *   enabled     bool
 *   widgetVisible  bool
 *   value       variant  current device-pushed value (null until first STATE_UPDATE)
 *   props       map      type-specific properties, built once by YamlParser
 *                        at parse time (see WidgetDef.h) — this model does
 *                        not rebuild it per read, except to overlay the
 *                        dynamic "collapsed" flag on a collapsible section.
 *   flex        int      layout weight inside a row/grid container
 *   align       string   content alignment override ("" = inherit)
 *   parentId    int      flat-list row index of the containing widget, or
 *                        -1 for top-level (see WidgetDef.h)
 *   row         int      this widget's own flat-list row index — use this,
 *                        not a Repeater's local `index`, when calling back
 *                        into toggleSection()/etc (see RowRole)
 *
 * Top-level widgets themselves are obtained via childModel(-1) — the same
 * mechanism every other container uses for its own children, since -1 is
 * the parentId every top-level widget shares. DeviceScreen.qml binds its
 * top-level Repeater to `controller.widgetModel.childModel(-1)`, not to
 * `controller.widgetModel` directly (which is now the full flat list,
 * every widget at every depth — iterating it directly would render every
 * nested child a second time, outside its actual container).
 *
 * Because every widget is a real row, setValue/setProperty/resetProperty
 * mutate exactly the row named by widgetId and emit dataChanged targeted at
 * that row — there is no longer a distinction between "top-level" and
 * "descendant" widgets, and no whole-branch PropsRole rebuild for a single
 * leaf's value change.
 */
#pragma once

#include "WidgetDef.h"
#include <QAbstractListModel>
#include <QHash>
#include <QSet>
#include <QVariant>
#include <QVector>

class ChildModel;

class WidgetModel : public QAbstractListModel
{
    Q_OBJECT
    /* Bumped on every setWidgets()/clear() reset — the only NOTIFYing signal
     * tied to "every previously-vended ChildModel* is now deleted". childModel()
     * itself is a plain Q_INVOKABLE with no NOTIFY, so a QML binding that
     * calls it (e.g. `property var _childModel: widgetModel.childModel(row)`)
     * has no reactive dependency and never re-evaluates on reset — it would
     * keep referencing a since-deleted ChildModel forever. Every such binding
     * must read `generation` too (even though its value is unused) purely to
     * establish the dependency, e.g.:
     *   property var _childModel: { widgetModel.generation; return widgetModel.childModel(row) } */
    Q_PROPERTY(int generation READ generation NOTIFY generationChanged)

public:
    enum Roles {
        WidgetIdRole = Qt::UserRole + 1,
        TypeRole,
        LabelRole,
        EnabledRole,
        VisibleRole,
        ValueRole,
        PropsRole,
        FlexRole,
        AlignRole,
        ParentRole,
        RowRole, /* this widget's own row index in the flat list — use this,
                  * not a Repeater's local `index`, when calling back into
                  * toggleSection()/setValue()/etc: a Repeater bound to
                  * childModel(parentId) has its own 0-based local index,
                  * which is NOT the same as the flat row once other
                  * containers' rows are interspersed. RowRole is correct
                  * regardless of which model (WidgetModel itself or any
                  * ChildModel) the Repeater is bound to. */
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

    /* Returns a stable, cached child-model view onto every row whose
     * parentId == parentId. The same QObject* is returned for repeated
     * calls with the same parentId within one model generation; it is
     * invalidated (deleted) on the next setWidgets()/clear() reset. Return
     * type is QObject* (not ChildModel*) so QML can bind it directly as a
     * Repeater model without needing the concrete type registered. */
    Q_INVOKABLE QObject* childModel(int parentId);

    int generation() const { return m_generation; }

    /* ── QAbstractListModel ─────────────────────────────────────── */
    int      rowCount(const QModelIndex& parent = {}) const override;
    QVariant data(const QModelIndex& index, int role = Qt::DisplayRole) const override;
    QHash<int, QByteArray> roleNames() const override;

signals:
    void generationChanged();

private:
    friend class ChildModel;

    int indexForWidgetId(uint8_t id) const;
    void clearChildModels();

    QList<WidgetDef>            m_widgets;
    QHash<uint8_t, int>         m_idToRow;      /* widget_id → row index, every depth */
    QSet<int>                   m_collapsedSections; /* flat-model rows of collapsed sections */
    QHash<int, ChildModel*>     m_childModels;  /* parentId → cached child-model instance */
    QHash<int, QVector<int>>    m_childrenByParent; /* parentId → source rows, in declaration
                                                      * order — built once in setWidgets() so
                                                      * ChildModel construction is O(1) lookup
                                                      * instead of an O(N) scan per container. */
    int                          m_generation = 0;
};

/* Lightweight read-only view onto every WidgetModel row whose parentId
 * matches the one this instance was constructed for. Holds only an index
 * array into the shared WidgetModel — no data of its own. All role reads
 * and roleNames() delegate straight to the owning WidgetModel, so a
 * ChildModel row's data is always identical to what WidgetModel itself
 * would report for that same underlying row. */
class ChildModel : public QAbstractListModel
{
    Q_OBJECT

public:
    ChildModel(WidgetModel* source, int parentId);

    int      rowCount(const QModelIndex& parent = {}) const override;
    QVariant data(const QModelIndex& index, int role = Qt::DisplayRole) const override;
    QHash<int, QByteArray> roleNames() const override;

    /* QML convenience: returns every role for `row` as one QVariantMap, so
     * QML JS code can do childModel.get(i).flex the same way it used to do
     * props.items[i].flex against a plain array. */
    Q_INVOKABLE QVariantMap get(int row) const;

private:
    WidgetModel* m_source;
    int          m_parentId;
    QVector<int> m_rows; /* source-model row indices, in declaration order */
};
