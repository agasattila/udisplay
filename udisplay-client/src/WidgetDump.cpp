#include "WidgetDump.h"
#include <QTextStream>
#include <QVariant>

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

void dumpWidget(QTextStream& out, const WidgetDef& w, int depth)
{
    const QString pad = indent(depth);
    out << pad << QStringLiteral("[0x%1] %2 \"%3\" keyPath=\"%4\" (enabled=%5 visible=%6)\n")
                     .arg(w.widgetId, 2, 16, QLatin1Char('0'))
                     .arg(widgetTypeName(w.type), w.label, w.keyPath)
                     .arg(w.enabled ? QStringLiteral("true") : QStringLiteral("false"))
                     .arg(w.visible ? QStringLiteral("true") : QStringLiteral("false"));

    out << pad << QStringLiteral("  value: %1\n").arg(formatValue(w.value));
    out << pad << QStringLiteral("  debugValue: %1\n").arg(formatValue(w.debugValue));

    switch (w.type) {
    case WidgetType::Display:
        out << pad << QStringLiteral("  unit: %1\n").arg(formatValue(w.unit));
        out << pad << QStringLiteral("  format: %1\n").arg(formatValue(w.format));
        out << pad << QStringLiteral("  style: %1\n").arg(formatValue(w.displayStyle));
        break;
    case WidgetType::Led:
        out << pad << QStringLiteral("  color: %1\n").arg(formatValue(w.color));
        break;
    case WidgetType::RgbLed:
        break;
    case WidgetType::Button:
        out << pad << QStringLiteral("  shape: %1\n").arg(formatValue(w.shape));
        out << pad << QStringLiteral("  color: %1\n").arg(formatValue(w.color));
        break;
    case WidgetType::ButtonGroup:
        out << pad << QStringLiteral("  layout: %1\n").arg(formatValue(w.groupLayout));
        out << pad << QStringLiteral("  items:\n");
        for (const auto& it : w.groupItems) {
            out << pad << QStringLiteral("    [0x%1] \"%2\" keyPath=\"%3\" position=%4\n")
                             .arg(it.widgetId, 2, 16, QLatin1Char('0'))
                             .arg(it.label, it.keyPath, formatValue(it.position));
        }
        break;
    case WidgetType::Slider:
        out << pad << QStringLiteral("  min: %1\n").arg(w.sliderMin);
        out << pad << QStringLiteral("  max: %1\n").arg(w.sliderMax);
        out << pad << QStringLiteral("  step: %1\n").arg(w.sliderStep);
        out << pad << QStringLiteral("  unit: %1\n").arg(formatValue(w.unit));
        break;
    case WidgetType::Text:
        out << pad << QStringLiteral("  mode: %1\n").arg(formatValue(w.textMode));
        out << pad << QStringLiteral("  defaultTextMode: %1\n").arg(formatValue(w.defaultTextMode));
        out << pad << QStringLiteral("  placeholder: %1\n").arg(formatValue(w.textPlaceholder));
        out << pad << QStringLiteral("  maxlength: %1\n").arg(w.textMaxLength);
        break;
    case WidgetType::Dropdown:
        out << pad << QStringLiteral("  items:\n");
        for (const auto& di : w.dropdownItems) {
            out << pad << QStringLiteral("    key=\"%1\" label=\"%2\"\n").arg(di.key, di.label);
        }
        break;
    case WidgetType::Label:
        out << pad << QStringLiteral("  text: %1\n").arg(formatValue(w.labelText));
        out << pad << QStringLiteral("  style: %1\n").arg(formatValue(w.labelStyle));
        out << pad << QStringLiteral("  textAlign: %1\n").arg(formatValue(w.labelAlign));
        break;
    case WidgetType::Separator:
        break;
    case WidgetType::Section:
        out << pad << QStringLiteral("  collapsible: %1\n")
                         .arg(w.collapsible ? QStringLiteral("true") : QStringLiteral("false"));
        out << pad << QStringLiteral("  sectionOwnerRow: %1\n").arg(w.sectionOwnerRow);
        break;
    case WidgetType::Row:
        out << pad << QStringLiteral("  flex: %1\n").arg(w.flex);
        out << pad << QStringLiteral("  align: %1\n").arg(formatValue(w.align));
        break;
    case WidgetType::Grid:
        out << pad << QStringLiteral("  flex: %1\n").arg(w.flex);
        out << pad << QStringLiteral("  columns: %1\n").arg(w.gridColumns);
        out << pad << QStringLiteral("  align: %1\n").arg(formatValue(w.align));
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
    if (w.type != WidgetType::Row && w.type != WidgetType::Grid) {
        out << pad << QStringLiteral("  flex: %1\n").arg(w.flex);
        out << pad << QStringLiteral("  align: %1\n").arg(formatValue(w.align));
    }

    if (!w.children.isEmpty()) {
        out << pad << QStringLiteral("  widgets:\n");
        for (const WidgetDef& child : w.children)
            dumpWidget(out, child, depth + 2);
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

    out << QStringLiteral("device: name=\"%1\" version=\"%2\" activeStyle=\"%3\"\n")
             .arg(deviceName, version, activeStyle);
    out << QStringLiteral("widgets: (%1 top-level)\n").arg(widgets.size());

    for (const WidgetDef& w : widgets)
        dumpWidget(out, w, 1);

    return result;
}
