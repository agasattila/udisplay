// SPDX-License-Identifier: LGPL-3.0-only
// Copyright (c) 2026 Attila Agas

#include "WidgetModel.h"
#include "Protocol.h"
#include <QVariantMap>

/* ── WidgetModel ──────────────────────────────────────────────────────────── */

WidgetModel::WidgetModel(QObject* parent)
    : QAbstractListModel(parent)
{}

void WidgetModel::setWidgets(const QList<WidgetDef>& widgets)
{
    beginResetModel();
    clearChildModels();
    m_widgets = widgets;
    m_idToRow.clear();
    m_collapsedSections.clear();
    m_childrenByParent.clear();
    for (int i = 0; i < m_widgets.size(); ++i) {
        /* widgetId=0 means decoration/container — skip flat id lookup */
        if (m_widgets[i].widgetId != 0)
            m_idToRow[m_widgets[i].widgetId] = i;
        m_childrenByParent[m_widgets[i].parentId].append(i);
    }
    endResetModel();
    ++m_generation;
    emit generationChanged();
}

void WidgetModel::clear()
{
    beginResetModel();
    clearChildModels();
    m_widgets.clear();
    m_idToRow.clear();
    m_collapsedSections.clear();
    m_childrenByParent.clear();
    endResetModel();
    ++m_generation;
    emit generationChanged();
}

void WidgetModel::clearChildModels()
{
    qDeleteAll(m_childModels);
    m_childModels.clear();
}

void WidgetModel::setValue(uint8_t widgetId, const QVariant& value)
{
    int row = indexForWidgetId(widgetId);
    if (row < 0) return;
    m_widgets[row].value = value;
    QModelIndex idx = index(row);
    emit dataChanged(idx, idx, { ValueRole });
}

void WidgetModel::setProperty(uint8_t targetId, uint8_t propertyId, uint8_t value)
{
    int row = indexForWidgetId(targetId);
    if (row < 0) return;
    bool changed = false;
    QVector<int> roles;
    switch (propertyId) {
    case Proto::PROP_ENABLED:
        if (m_widgets[row].enabled != (value != 0)) {
            m_widgets[row].enabled = (value != 0);
            roles << EnabledRole;
            changed = true;
        }
        break;
    case Proto::PROP_VISIBLE:
        if (m_widgets[row].visible != (value != 0)) {
            m_widgets[row].visible = (value != 0);
            roles << VisibleRole;
            changed = true;
        }
        break;
    case Proto::PROP_MODE:
        if (m_widgets[row].type == WidgetType::Text) {
            QString newMode = (value == 0)
                ? QStringLiteral("readonly") : QStringLiteral("rw");
            if (m_widgets[row].props.value(QStringLiteral("mode")).toString() != newMode) {
                m_widgets[row].props[QStringLiteral("mode")] = newMode;
                roles << PropsRole;
                changed = true;
            }
        }
        break;
    default:
        break;
    }
    if (changed)
        emit dataChanged(index(row), index(row), roles);
}

void WidgetModel::resetProperty(uint8_t targetId, uint8_t propertyId)
{
    int row = indexForWidgetId(targetId);
    if (row < 0) return;
    bool changed = false;
    QVector<int> roles;
    switch (propertyId) {
    case Proto::PROP_ENABLED:
        if (!m_widgets[row].enabled) {
            m_widgets[row].enabled = true;
            roles << EnabledRole;
            changed = true;
        }
        break;
    case Proto::PROP_VISIBLE:
        if (!m_widgets[row].visible) {
            m_widgets[row].visible = true;
            roles << VisibleRole;
            changed = true;
        }
        break;
    case Proto::PROP_MODE:
        if (m_widgets[row].type == WidgetType::Text) {
            QString defaultMode = m_widgets[row].props.value(QStringLiteral("defaultMode")).toString();
            if (!defaultMode.isEmpty()
                && m_widgets[row].props.value(QStringLiteral("mode")).toString() != defaultMode) {
                m_widgets[row].props[QStringLiteral("mode")] = defaultMode;
                roles << PropsRole;
                changed = true;
            }
        }
        break;
    default:
        break;
    }
    if (changed)
        emit dataChanged(index(row), index(row), roles);
}

int WidgetModel::rowCount(const QModelIndex& parent) const
{
    if (parent.isValid()) return 0;
    return m_widgets.size();
}

QVariant WidgetModel::data(const QModelIndex& idx, int role) const
{
    if (!idx.isValid() || idx.row() >= m_widgets.size())
        return {};
    const WidgetDef& w = m_widgets[idx.row()];

    switch (role) {
    case WidgetIdRole: return static_cast<int>(w.widgetId);
    case TypeRole:     return widgetTypeName(w.type);
    case LabelRole:    return w.label;
    case EnabledRole:  return w.enabled;
    case VisibleRole: {
        if (!w.visible) return false;
        /* Walk the parentId chain to support nested collapsible sections —
         * any ancestor (direct or indirect) that is a collapsed collapsible
         * section hides this widget. */
        int ownerRow = w.parentId;
        while (ownerRow >= 0) {
            const WidgetDef& anc = m_widgets[ownerRow];
            if (anc.type == WidgetType::Section
                && anc.props.value(QStringLiteral("collapsible")).toBool()
                && m_collapsedSections.contains(ownerRow))
                return false;
            ownerRow = anc.parentId;
        }
        return true;
    }
    case ValueRole: return w.value;
    case PropsRole: {
        if (w.type == WidgetType::Section && w.props.value(QStringLiteral("collapsible")).toBool()) {
            QVariantMap props = w.props;
            props[QStringLiteral("collapsed")] = m_collapsedSections.contains(idx.row());
            return props;
        }
        return w.props;
    }
    case FlexRole:   return w.flex;
    case AlignRole:  return w.align;
    case ParentRole: return w.parentId;
    case RowRole:    return idx.row();
    default:         return {};
    }
}

QHash<int, QByteArray> WidgetModel::roleNames() const
{
    return {
        { WidgetIdRole, "widgetId"      },
        { TypeRole,     "type"          },
        { LabelRole,    "label"         },
        { EnabledRole,  "enabled"       },
        { VisibleRole,  "widgetVisible" },
        { ValueRole,    "value"         },
        { PropsRole,    "props"         },
        { FlexRole,     "flex"          },
        { AlignRole,    "align"         },
        { ParentRole,   "parentId"      },
        { RowRole,      "row"           },
    };
}

void WidgetModel::toggleSection(int row)
{
    if (row < 0 || row >= m_widgets.size()) return;
    const WidgetDef& section = m_widgets[row];
    if (section.type != WidgetType::Section
        || !section.props.value(QStringLiteral("collapsible")).toBool())
        return;

    if (m_collapsedSections.contains(row))
        m_collapsedSections.remove(row);
    else
        m_collapsedSections.insert(row);

    /* Update collapsed flag on section header (PropsRole) */
    QModelIndex sectionIdx = index(row);
    emit dataChanged(sectionIdx, sectionIdx, { PropsRole });

    /* Update visibility of all descendants (direct and nested) of this section */
    for (int i = row + 1; i < m_widgets.size(); ++i) {
        int ownerRow = m_widgets[i].parentId;
        while (ownerRow >= 0) {
            if (ownerRow == row) {
                QModelIndex childIdx = index(i);
                emit dataChanged(childIdx, childIdx, { VisibleRole });
                break;
            }
            ownerRow = m_widgets[ownerRow].parentId;
        }
    }
}

int WidgetModel::indexForWidgetId(uint8_t id) const
{
    auto it = m_idToRow.find(id);
    return (it != m_idToRow.end()) ? it.value() : -1;
}

QObject* WidgetModel::childModel(int parentId)
{
    auto it = m_childModels.find(parentId);
    if (it != m_childModels.end())
        return it.value();
    auto* cm = new ChildModel(this, parentId);
    m_childModels.insert(parentId, cm);
    return cm;
}

/* ── ChildModel ───────────────────────────────────────────────────────────── */

ChildModel::ChildModel(WidgetModel* source, int parentId)
    : QAbstractListModel(source)
    , m_source(source)
    , m_parentId(parentId)
{
    /* O(1) lookup via the index WidgetModel builds once in setWidgets() —
     * previously an O(N) scan of the full flat list per ChildModel, which
     * made vending a ChildModel for every one of O(N) containers O(N²)
     * overall. */
    m_rows = m_source->m_childrenByParent.value(m_parentId);

    /* Every QML container Repeater binds to a ChildModel, not to the source
     * WidgetModel directly (see childModel()'s header comment) — without
     * this forwarding, a live setValue()/setProperty()/toggleSection() call
     * (STATE_UPDATE from the device, or a design-mode edit) emits
     * dataChanged only on the source, which no bound Repeater is listening
     * to, so the change never reaches the screen after initial load. */
    connect(m_source, &QAbstractItemModel::dataChanged, this,
            [this](const QModelIndex& topLeft, const QModelIndex& bottomRight, const QList<int>& roles) {
        for (int srcRow = topLeft.row(); srcRow <= bottomRight.row(); ++srcRow) {
            int localRow = m_rows.indexOf(srcRow);
            if (localRow >= 0) {
                QModelIndex idx = index(localRow);
                emit dataChanged(idx, idx, roles);
            }
        }
    });
}

int ChildModel::rowCount(const QModelIndex& parent) const
{
    if (parent.isValid()) return 0;
    return m_rows.size();
}

QVariant ChildModel::data(const QModelIndex& idx, int role) const
{
    if (!idx.isValid() || idx.row() >= m_rows.size())
        return {};
    return m_source->data(m_source->index(m_rows[idx.row()]), role);
}

QHash<int, QByteArray> ChildModel::roleNames() const
{
    return m_source->roleNames();
}

QVariantMap ChildModel::get(int row) const
{
    QVariantMap m;
    if (row < 0 || row >= m_rows.size())
        return m;
    const auto roles = roleNames();
    for (auto it = roles.begin(); it != roles.end(); ++it)
        m[QString::fromUtf8(it.value())] = data(index(row, 0), it.key());
    return m;
}
