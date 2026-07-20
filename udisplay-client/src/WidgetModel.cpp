// SPDX-License-Identifier: LGPL-3.0-only
// Copyright (c) 2026 Attila Agas

#include "WidgetModel.h"
#include "Protocol.h"
#include <QVariantList>
#include <QVariantMap>

/* ── Shared child serialisation ─────────────────────────────────────────── */

static QVariantMap buildPropsMap(const WidgetDef& w);

static QVariantList serializeChildren(const QList<WidgetDef>& children)
{
    QVariantList items;
    for (const auto& child : children) {
        QVariantMap cm;
        cm[QStringLiteral("type")]     = widgetTypeName(child.type);
        cm[QStringLiteral("widgetId")] = static_cast<int>(child.widgetId);
        cm[QStringLiteral("label")]    = child.label;
        cm[QStringLiteral("enabled")]  = child.enabled;
        cm[QStringLiteral("visible")]  = child.visible;
        cm[QStringLiteral("value")]    = child.value;
        cm[QStringLiteral("flex")]     = child.flex;
        cm[QStringLiteral("align")]    = child.align;
        cm[QStringLiteral("props")]    = buildPropsMap(child);
        items.append(cm);
    }
    return items;
}

/* ── Props helper (shared by PropsRole and container child serialisation) ── */

static QVariantMap buildPropsMap(const WidgetDef& w)
{
    QVariantMap props;
    switch (w.type) {
    case WidgetType::Display:
        props[QStringLiteral("unit")]   = w.unit;
        props[QStringLiteral("format")] = w.format;
        props[QStringLiteral("style")]  = w.displayStyle;
        break;
    case WidgetType::Led:
        props[QStringLiteral("color")] = w.color;
        break;
    case WidgetType::RgbLed:
        break;
    case WidgetType::Button:
        props[QStringLiteral("shape")] = w.shape;
        if (!w.children.isEmpty())
            props[QStringLiteral("items")] = serializeChildren(w.children);
        break;
    case WidgetType::ButtonGroup: {
        props[QStringLiteral("layout")] = w.groupLayout;
        QVariantList items;
        for (const auto& it : w.groupItems) {
            QVariantMap m;
            m[QStringLiteral("widgetId")] = static_cast<int>(it.widgetId);
            m[QStringLiteral("label")]    = it.label;
            m[QStringLiteral("position")] = it.position;
            items.append(m);
        }
        props[QStringLiteral("items")] = items;
        break;
    }
    case WidgetType::Slider:
        props[QStringLiteral("min")]  = w.sliderMin;
        props[QStringLiteral("max")]  = w.sliderMax;
        props[QStringLiteral("step")] = w.sliderStep;
        props[QStringLiteral("unit")] = w.unit;
        break;
    case WidgetType::Text:
        props[QStringLiteral("mode")]        = w.textMode;
        props[QStringLiteral("placeholder")] = w.textPlaceholder;
        props[QStringLiteral("maxlength")]   = w.textMaxLength;
        break;
    case WidgetType::Dropdown: {
        QVariantList items;
        for (const auto& di : w.dropdownItems) {
            QVariantMap m;
            m[QStringLiteral("key")]   = di.key;
            m[QStringLiteral("label")] = di.label;
            items.append(m);
        }
        props[QStringLiteral("items")] = items;
        break;
    }
    case WidgetType::Label:
        props[QStringLiteral("text")]  = w.labelText;
        props[QStringLiteral("style")] = w.labelStyle;
        props[QStringLiteral("labelAlign")] = w.labelAlign;
        break;
    case WidgetType::Section:
        props[QStringLiteral("collapsible")] = w.collapsible;
        break;
    case WidgetType::Row:
    case WidgetType::Grid:
        if (w.type == WidgetType::Grid)
            props[QStringLiteral("columns")] = w.gridColumns;
        props[QStringLiteral("align")] = w.align;
        props[QStringLiteral("items")] = serializeChildren(w.children);
        break;
    default:
        break;
    }
    return props;
}

/* ── WidgetModel ──────────────────────────────────────────────────────────── */

WidgetModel::WidgetModel(QObject* parent)
    : QAbstractListModel(parent)
{}

/* Recursively index all descendants of a top-level widget into m_childPath.
 * children: the child list being walked at this recursion level.
 * path: the current path prefix (path[0] = top-level row, rest = child indices).
 * Appends each child's index, records the path for widgets with a non-zero ID,
 * then recurses into grandchildren. */
static void indexDescendants(
    const QList<WidgetDef>& children,
    QVector<int>& path,
    QHash<uint8_t, QVector<int>>& out)
{
    for (int j = 0; j < children.size(); ++j) {
        const auto& child = children[j];
        path.append(j);
        if (child.widgetId != 0)
            out[child.widgetId] = path;
        if (!child.children.isEmpty())
            indexDescendants(child.children, path, out);
        path.removeLast();
    }
}

void WidgetModel::setWidgets(const QList<WidgetDef>& widgets)
{
    beginResetModel();
    m_widgets = widgets;
    m_idToRow.clear();
    m_childPath.clear();
    m_collapsedSections.clear();
    for (int i = 0; i < m_widgets.size(); ++i) {
        /* widgetId=0 means decoration/container — skip flat id lookup */
        if (m_widgets[i].widgetId != 0)
            m_idToRow[m_widgets[i].widgetId] = i;
        /* Index all descendants at every nesting depth */
        QVector<int> path = { i };
        indexDescendants(m_widgets[i].children, path, m_childPath);
    }
    endResetModel();
}

WidgetDef* WidgetModel::findDescendant(uint8_t id, int& outParentRow)
{
    auto it = m_childPath.find(id);
    if (it == m_childPath.end()) return nullptr;
    const QVector<int>& path = it.value();
    if (path.isEmpty() || path[0] >= m_widgets.size()) return nullptr;
    outParentRow = path[0];
    WidgetDef* cur = &m_widgets[path[0]];
    for (int k = 1; k < path.size(); ++k) {
        if (path[k] >= cur->children.size()) return nullptr;
        cur = &cur->children[path[k]];
    }
    return cur;
}

void WidgetModel::clear()
{
    beginResetModel();
    m_widgets.clear();
    m_idToRow.clear();
    m_childPath.clear();
    m_collapsedSections.clear();
    endResetModel();
}

void WidgetModel::setValue(uint8_t widgetId, const QVariant& value)
{
    /* Descendant lookup — covers all nesting depths */
    int parentRow = -1;
    if (auto* child = findDescendant(widgetId, parentRow)) {
        child->value = value;
        QModelIndex parentQIdx = index(parentRow);
        emit dataChanged(parentQIdx, parentQIdx, { PropsRole });
        return;
    }

    /* Top-level widget */
    int row = indexForWidgetId(widgetId);
    if (row < 0) return;
    m_widgets[row].value = value;
    QModelIndex idx = index(row);
    emit dataChanged(idx, idx, { ValueRole });
}

void WidgetModel::setProperty(uint8_t targetId, uint8_t propertyId, uint8_t value)
{
    /* Descendant lookup — covers all nesting depths */
    int parentRow = -1;
    if (auto* child = findDescendant(targetId, parentRow)) {
        bool changed = false;
        switch (propertyId) {
        case Proto::PROP_ENABLED:
            if (child->enabled != (value != 0)) {
                child->enabled = (value != 0); changed = true; }
            break;
        case Proto::PROP_VISIBLE:
            if (child->visible != (value != 0)) {
                child->visible = (value != 0); changed = true; }
            break;
        default: break;
        }
        if (changed)
            emit dataChanged(index(parentRow), index(parentRow), { PropsRole });
        return;
    }

    /* Top-level widget */
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
            if (m_widgets[row].textMode != newMode) {
                m_widgets[row].textMode = newMode;
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
    /* Descendant lookup — covers all nesting depths */
    int parentRow = -1;
    if (auto* child = findDescendant(targetId, parentRow)) {
        bool changed = false;
        switch (propertyId) {
        case Proto::PROP_ENABLED:
            if (!child->enabled) { child->enabled = true; changed = true; } break;
        case Proto::PROP_VISIBLE:
            if (!child->visible) { child->visible = true; changed = true; } break;
        default: break;
        }
        if (changed)
            emit dataChanged(index(parentRow), index(parentRow), { PropsRole });
        return;
    }

    /* Top-level widget */
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
        if (m_widgets[row].type == WidgetType::Text
            && !m_widgets[row].defaultTextMode.isEmpty()
            && m_widgets[row].textMode != m_widgets[row].defaultTextMode) {
            m_widgets[row].textMode = m_widgets[row].defaultTextMode;
            roles << PropsRole;
            changed = true;
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
        /* Walk the full section-ownership chain to support nested collapsible sections */
        int ownerRow = w.sectionOwnerRow;
        while (ownerRow >= 0) {
            if (m_collapsedSections.contains(ownerRow)) return false;
            ownerRow = m_widgets[ownerRow].sectionOwnerRow;
        }
        return true;
    }
    case ValueRole:    return w.value;
    case PropsRole: {
        QVariantMap props = buildPropsMap(w);
        if (w.type == WidgetType::Section && w.collapsible)
            props[QStringLiteral("collapsed")] = m_collapsedSections.contains(idx.row());
        return props;
    }
    case SectionOwnerRowRole: return w.sectionOwnerRow;
    default:           return {};
    }
}

QHash<int, QByteArray> WidgetModel::roleNames() const
{
    return {
        { WidgetIdRole,        "widgetId"       },
        { TypeRole,            "type"           },
        { LabelRole,           "label"          },
        { EnabledRole,         "enabled"        },
        { VisibleRole,         "widgetVisible"  },
        { ValueRole,           "value"          },
        { PropsRole,           "props"          },
        { SectionOwnerRowRole, "sectionOwnerRow"},
    };
}

void WidgetModel::toggleSection(int row)
{
    if (row < 0 || row >= m_widgets.size()) return;
    const WidgetDef& section = m_widgets[row];
    if (section.type != WidgetType::Section || !section.collapsible) return;

    if (m_collapsedSections.contains(row))
        m_collapsedSections.remove(row);
    else
        m_collapsedSections.insert(row);

    /* Update collapsed flag on section header (PropsRole) */
    QModelIndex sectionIdx = index(row);
    emit dataChanged(sectionIdx, sectionIdx, { PropsRole });

    /* Update visibility of all descendants (direct and nested) of this section */
    for (int i = row + 1; i < m_widgets.size(); ++i) {
        int ownerRow = m_widgets[i].sectionOwnerRow;
        while (ownerRow >= 0) {
            if (ownerRow == row) {
                QModelIndex childIdx = index(i);
                emit dataChanged(childIdx, childIdx, { VisibleRole });
                break;
            }
            ownerRow = m_widgets[ownerRow].sectionOwnerRow;
        }
    }
}

int WidgetModel::indexForWidgetId(uint8_t id) const
{
    auto it = m_idToRow.find(id);
    return (it != m_idToRow.end()) ? it.value() : -1;
}
