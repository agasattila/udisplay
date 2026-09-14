#include "WidgetDump.h"
#include <QTextStream>
#include <QVariant>
#include <QVariantMap>

namespace {

QString formatValue(const QVariant& v)
{
    if (v.isNull() || !v.isValid())
        return QStringLiteral("(null)");
    if (v.typeId() == QMetaType::QString)
        return QStringLiteral("\"%1\"").arg(v.toString());
    if (v.typeId() == QMetaType::Bool)
        return v.toBool() ? QStringLiteral("true") : QStringLiteral("false");
    return v.toString();
}

QString indent(int depth)
{
    return QString(depth * 2, QLatin1Char(' '));
}

void dumpWidget(QTextStream& out, const QList<WidgetDef>& all, int row, int depth)
{
    const WidgetDef& w = all[row];
    const QString pad = indent(depth);
    out << pad << QStringLiteral("[0x%1] %2 \"%3\" keyPath=\"%4\" (enabled=%5 visible=%6)\n")
                     .arg(w.widgetId, 2, 16, QLatin1Char('0'))
                     .arg(widgetTypeName(w.type), w.label, w.keyPath)
                     .arg(w.enabled ? QStringLiteral("true") : QStringLiteral("false"))
                     .arg(w.visible ? QStringLiteral("true") : QStringLiteral("false"));

    out << pad << QStringLiteral("  parentId: %1\n").arg(w.parentId);
    out << pad << QStringLiteral("  value: %1\n").arg(formatValue(w.value));
    out << pad << QStringLiteral("  debugValue: %1\n").arg(formatValue(w.debugValue));

    switch (w.type) {
    case WidgetType::Display:
        out << pad << QStringLiteral("  unit: %1\n").arg(formatValue(w.props.value(QStringLiteral("unit"))));
        out << pad << QStringLiteral("  format: %1\n").arg(formatValue(w.props.value(QStringLiteral("format"))));
        out << pad << QStringLiteral("  style: %1\n").arg(formatValue(w.props.value(QStringLiteral("style"))));
        break;
    case WidgetType::Led:
        out << pad << QStringLiteral("  color: %1\n").arg(formatValue(w.props.value(QStringLiteral("color"))));
        break;
    case WidgetType::RgbLed:
        break;
    case WidgetType::Button:
        out << pad << QStringLiteral("  shape: %1\n").arg(formatValue(w.props.value(QStringLiteral("shape"))));
        out << pad << QStringLiteral("  position: %1\n").arg(formatValue(w.props.value(QStringLiteral("position"))));
        break;
    case WidgetType::ButtonGroup:
        out << pad << QStringLiteral("  layout: %1\n").arg(formatValue(w.props.value(QStringLiteral("layout"))));
        break;
    case WidgetType::Slider:
        out << pad << QStringLiteral("  min: %1\n").arg(w.props.value(QStringLiteral("min")).toDouble());
        out << pad << QStringLiteral("  max: %1\n").arg(w.props.value(QStringLiteral("max")).toDouble());
        out << pad << QStringLiteral("  step: %1\n").arg(w.props.value(QStringLiteral("step")).toDouble());
        out << pad << QStringLiteral("  unit: %1\n").arg(formatValue(w.props.value(QStringLiteral("unit"))));
        break;
    case WidgetType::Text:
        out << pad << QStringLiteral("  mode: %1\n").arg(formatValue(w.props.value(QStringLiteral("mode"))));
        out << pad << QStringLiteral("  defaultMode: %1\n").arg(formatValue(w.props.value(QStringLiteral("defaultMode"))));
        out << pad << QStringLiteral("  placeholder: %1\n").arg(formatValue(w.props.value(QStringLiteral("placeholder"))));
        out << pad << QStringLiteral("  maxlength: %1\n").arg(w.props.value(QStringLiteral("maxlength")).toInt());
        break;
    case WidgetType::Dropdown:
        out << pad << QStringLiteral("  items:\n");
        for (const auto& v : w.props.value(QStringLiteral("items")).toList()) {
            QVariantMap di = v.toMap();
            out << pad << QStringLiteral("    key=\"%1\" label=\"%2\"\n")
                             .arg(di.value(QStringLiteral("key")).toString(),
                                  di.value(QStringLiteral("label")).toString());
        }
        break;
    case WidgetType::Label:
        out << pad << QStringLiteral("  text: %1\n").arg(formatValue(w.props.value(QStringLiteral("text"))));
        out << pad << QStringLiteral("  style: %1\n").arg(formatValue(w.props.value(QStringLiteral("style"))));
        out << pad << QStringLiteral("  textAlign: %1\n").arg(formatValue(w.props.value(QStringLiteral("labelAlign"))));
        break;
    case WidgetType::Separator:
        break;
    case WidgetType::Section:
        out << pad << QStringLiteral("  collapsible: %1\n")
                         .arg(w.props.value(QStringLiteral("collapsible")).toBool()
                              ? QStringLiteral("true") : QStringLiteral("false"));
        break;
    case WidgetType::Row:
        out << pad << QStringLiteral("  flex: %1\n").arg(w.flex);
        out << pad << QStringLiteral("  align: %1\n").arg(formatValue(w.align));
        break;
    case WidgetType::Grid:
        out << pad << QStringLiteral("  flex: %1\n").arg(w.flex);
        out << pad << QStringLiteral("  columns: %1\n").arg(w.props.value(QStringLiteral("columns")).toInt());
        out << pad << QStringLiteral("  align: %1\n").arg(formatValue(w.align));
        break;
    case WidgetType::Dpad:
        out << pad << QStringLiteral("  flex: %1\n").arg(w.flex);
        out << pad << QStringLiteral("  align: %1\n").arg(formatValue(w.align));
        break;
    case WidgetType::Toggle:
        break;
    case WidgetType::Unknown:
        break;
    }

    /* flex/align apply to any widget used as a row/grid/button child,
     * regardless of its own type — print them once here for non-Row/Grid
     * types so they aren't duplicated by the switch above. align is a null
     * QString (WidgetDef.h's default member init) for a widget that
     * doesn't override its container's default — formatValue already
     * renders a null QVariant as "(null)", the same placeholder used for
     * value/debugValue above when absent. */
    if (w.type != WidgetType::Row && w.type != WidgetType::Grid && w.type != WidgetType::Dpad) {
        out << pad << QStringLiteral("  flex: %1\n").arg(w.flex);
        out << pad << QStringLiteral("  align: %1\n").arg(formatValue(w.align));
    }

    QList<int> childRows;
    for (int j = 0; j < all.size(); ++j)
        if (all[j].parentId == row)
            childRows.append(j);

    if (!childRows.isEmpty()) {
        out << pad << QStringLiteral("  widgets:\n");
        for (int j : childRows)
            dumpWidget(out, all, j, depth + 2);
    }
}

} // namespace

QString dumpWidgetTree(const QList<WidgetDef>& widgets,
                        const QString& deviceName,
                        const QString& version,
                        const QString& activeStyle)
{
    QString result;
    QTextStream out(&result);

    QList<int> topLevel;
    for (int i = 0; i < widgets.size(); ++i)
        if (widgets[i].parentId < 0)
            topLevel.append(i);

    out << QStringLiteral("device: name=\"%1\" version=\"%2\" activeStyle=\"%3\"\n")
             .arg(deviceName, version, activeStyle);
    out << QStringLiteral("widgets: (%1 top-level)\n").arg(topLevel.size());

    for (int i : topLevel)
        dumpWidget(out, widgets, i, 1);

    return result;
}
