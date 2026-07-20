import QtQuick
import QtQuick.Controls.Material
import QtQuick.Layouts
import "./"

/* Shared child widget dispatcher for RowWidget and GridWidget Repeaters.
 * Selects the correct widget component from modelData.type and forwards all
 * standard widget properties to the instantiated child.
 *
 * Uses import "./" (NOT the module URI) — Android qmlcachegen requirement.
 *
 * Supports unlimited nesting depth via Qt.resolvedUrl (string URL) for row and
 * grid. This avoids the static type-name cycle that would result from
 * Component { RowWidget { } } here: RowWidget references WidgetDelegate and
 * WidgetDelegate would reference RowWidget — Qt's cycle detector fires on
 * bilateral static type usage. Qt.resolvedUrl is a runtime string dependency
 * that the cycle detector does not follow.
 *
 * modelData is intentionally NOT declared on this base type. It is declared as
 * `required property var modelData` on the WidgetDelegate instance itself in
 * RowWidget.qml/GridWidget.qml's Repeater delegate. Redeclaring it here too
 * (even without `required`, even with no default value) shadows the
 * Repeater's injection — the local declaration silently wins and modelData
 * never receives the actual model item, so every row/grid child renders with
 * an empty type and nothing shows up. Verified empirically with a minimal
 * Repeater+delegate reproduction; do not re-add this property here.
 *
 * section children are not supported yet
 */
Loader {
    id: root

    property bool compact: false

    /* Propagate the loaded item's height to the parent Layout as preferredHeight.
     * Loader.implicitHeight is read-only in Qt 6; implicitHeight cannot be set.
     * Layout.preferredHeight is the correct hook: when WidgetDelegate is a direct
     * child of a RowLayout or GridLayout, the layout uses preferredHeight (which
     * takes precedence over implicitHeight) to compute its own implicitHeight.
     * Without this, leaf widgets with height:N but no implicitHeight contribute 0
     * to the layout's implicitHeight, collapsing RowWidget.height to 0. */
    Layout.preferredHeight: item ? item.implicitHeight : 0

    /* Same problem, same fix, for width — but this one only bites for nested
     * row/grid (rowComp/gridComp below), not flat leaf widgets. Loader.implicitWidth
     * is supposed to mirror the loaded item's implicitWidth, but empirically (Qt
     * 6.4.2, verified with a minimal Loader{source:...}-in-RowLayout reproduction)
     * that mirroring does not update once a `source:`-loaded item's own implicitWidth
     * settles asynchronously — Loader.implicitWidth stays stuck at 0 even though
     * item.implicitWidth correctly reads 200. For rowComp/gridComp, `item` here is
     * the inner Loader (see below); its own .implicitWidth DOES correctly track its
     * `source:`-loaded RowWidget/GridWidget instance, so reading `item.implicitWidth`
     * directly (bypassing this Loader's own broken mirroring) is the fix — same
     * pattern as Layout.preferredHeight above. Without this, a `row`/`grid` nested
     * inside another row/grid gets width 0, so RowLayout can't give its own children
     * any space and they all render at x=0 — exactly on top of each other. */
    Layout.preferredWidth: item ? item.implicitWidth : 0

    property string _type:     modelData.type      || ""
    property int    _widgetId: modelData.widgetId  || 0
    property string _label:    modelData.label     || ""
    property bool   _enabled:  modelData.enabled   !== false
    property var    _value:    modelData.value
    property var    _props:    modelData.props      || {}

    visible: modelData.visible !== false

    sourceComponent: _type === "display"      ? displayComp
                   : _type === "led"          ? ledComp
                   : _type === "rgbled"       ? rgbledComp
                   : _type === "button"       ? buttonComp
                   : _type === "button-group" ? buttonGroupComp
                   : _type === "slider"       ? sliderComp
                   : _type === "toggle"       ? toggleComp
                   : _type === "text"         ? textComp
                   : _type === "dropdown"     ? dropdownComp
                   : _type === "label"        ? labelComp
                   : _type === "separator"    ? separatorComp
                   : _type === "row"          ? rowComp
                   : _type === "grid"         ? gridComp
                   : null

    /* Leaf widget components — no cycle: none of these files reference WidgetDelegate */
    Component { id: displayComp;     DisplayWidget     { widgetId: _widgetId; label: _label; enabled: _enabled; value: _value; props: _props; compact: root.compact } }
    Component { id: ledComp;         LedWidget         { widgetId: _widgetId; label: _label; enabled: _enabled; value: _value; props: _props; compact: root.compact } }
    Component { id: rgbledComp;      RgbLedWidget      { widgetId: _widgetId; label: _label; enabled: _enabled; value: _value; compact: root.compact } }
    Component { id: buttonComp;      ButtonWidget      { widgetId: _widgetId; label: _label; enabled: _enabled; props: _props } }
    Component { id: buttonGroupComp; ButtonGroupWidget { widgetId: _widgetId; label: _label; enabled: _enabled; value: _value; props: _props } }
    Component { id: sliderComp;      SliderWidget      { widgetId: _widgetId; label: _label; enabled: _enabled; value: _value; props: _props } }
    Component { id: toggleComp;      ToggleWidget      { widgetId: _widgetId; label: _label; enabled: _enabled; value: _value } }
    Component { id: textComp;        TextWidget        { widgetId: _widgetId; label: _label; enabled: _enabled; value: _value; props: _props } }
    Component { id: dropdownComp;    DropdownWidget    { widgetId: _widgetId; label: _label; enabled: _enabled; value: _value; props: _props } }
    Component { id: labelComp;       LabelWidget       { props: _props; compact: root.compact } }
    Component { id: separatorComp;   SeparatorWidget   {} }

    /* Container components — dynamic URL loading breaks the bilateral cycle.
     * RowWidget/GridWidget use WidgetDelegate as their Repeater delegate (static
     * reference from their side), so WidgetDelegate must NOT reference them by
     * type name in return.  Using Qt.resolvedUrl produces a runtime string; Qt's
     * cycle detector does not follow string arguments.
     * Requires RowWidget.props and GridWidget.props to be non-required (set via
     * live binding in onLoaded after the item is created). */
    Component {
        id: rowComp
        Loader {
            anchors { left: parent.left; right: parent.right }
            source: Qt.resolvedUrl("RowWidget.qml")
            onLoaded: {
                item.label = Qt.binding(function() { return root._label })
                item.props = Qt.binding(function() { return root._props })
                item.compact = Qt.binding(function() { return root.compact })
            }
            Layout.fillWidth: true
        }
    }
    Component {
        id: gridComp
        Loader {
            anchors { left: parent.left; right: parent.right }
            source: Qt.resolvedUrl("GridWidget.qml")
            onLoaded: {
                item.label = Qt.binding(function() { return root._label })
                item.props = Qt.binding(function() { return root._props })
                item.compact = Qt.binding(function() { return root.compact })
            }
            /* Without this, a grid nested inside another row/grid gets
             * width 0 — same class of bug Layout.fillWidth above already
             * fixes for nested rows (see the long implicitWidth-propagation
             * comment on this file's Layout.preferredWidth above). */
            Layout.fillWidth: true
        }
    }
}
