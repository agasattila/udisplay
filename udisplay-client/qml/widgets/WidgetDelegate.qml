import QtQuick
import QtQuick.Controls.Material
import QtQuick.Layouts
import "./"

/* Shared widget dispatcher — used as the Repeater delegate by RowWidget,
 * GridWidget, SectionWidget, AND DeviceScreen.qml's top-level Repeater (the
 * flat list's own parentId:-1 "container"). Selects the correct widget
 * component from the current row's `type` role and forwards all standard
 * widget properties to the instantiated child.
 *
 * DeviceScreen.qml previously hand-rolled its own second copy of this exact
 * type->component dispatch for top-level widgets instead of reusing this
 * file. That duplication is what let a childModel-wiring fix land here
 * (buttonComp/buttonGroupComp) without the equivalent fix landing in
 * DeviceScreen.qml's copy — a top-level button's face children were
 * silently dropped while the identical button nested in a row worked fine.
 * DeviceScreen.qml now uses this file directly as its Repeater's delegate;
 * do not reintroduce a second dispatch table there.
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
 * `model` is intentionally NOT declared on this base type. It is declared as
 * `required property var model` on the WidgetDelegate instance itself in
 * RowWidget.qml/GridWidget.qml's Repeater delegate — Qt Quick's own
 * aggregating context property for a QAbstractItemModel-backed delegate,
 * giving `model.widgetId`, `model.type`, etc. for every role in
 * WidgetModel::roleNames() (WidgetModel is now a flat list — every widget,
 * any nesting depth, is a real row with real roles; there is no more
 * separate "props.items" JS-array shape to consume). Redeclaring `model`
 * here too (even without `required`, even with no default value) shadows
 * the Repeater's injection — the local declaration silently wins and model
 * never receives the actual row data, so every row/grid child renders with
 * an empty type and nothing shows up. Verified empirically with a minimal
 * Repeater+delegate reproduction (the same failure mode the old `modelData`
 * version of this file already documented) — do not re-add this property
 * here.
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

    property string _type:     model.type          || ""
    property int    _widgetId: model.widgetId      || 0
    property string _label:    model.label         || ""
    property bool   _enabled:  model.enabled       !== false
    property var    _value:    model.value
    property var    _props:    model.props          || {}
    /* Only container types actually consume a childModel below (buttonComp/
     * buttonGroupComp/rowComp/gridComp/dpadComp) — every leaf type
     * (display/led/rgbled/slider/toggle/text/dropdown/label/separator)
     * ignores it. Gating construction on _isContainer matters: each
     * uncached childModel() call does a full O(N) scan of WidgetModel's flat
     * list to build its row index (WidgetModel.cpp's ChildModel
     * constructor), so evaluating this unconditionally for every leaf row
     * too — as an earlier version of this binding did — made a full
     * setWidgets() reset (e.g. every live design-mode reload) cost O(N²)
     * for N flat widgets, the overwhelming majority of which are leaves
     * whose throwaway ChildModel is never read. */
    property bool   _isContainer: _type === "row" || _type === "grid" || _type === "button"
                                 || _type === "button-group" || _type === "dpad" || _type === "section"

    /* Every widget's own children, scoped by its FLAT ROW index — NOT
     * widgetId: row/grid/section/dpad containers all have widgetId 0 (only
     * ID-bearing leaf/button-ish widgets get a real id), so keying on
     * widgetId would collide every top-level container sharing id 0 into
     * one shared child model. `model.row` (WidgetModel::RowRole) is this
     * row's own unique flat-list index, always distinct. This is the one
     * place a QML container reaches the global WidgetModel singleton —
     * every container component itself receives childModel as a plain
     * property instead, preserving their existing standalone-testability
     * (see test/qml/*.qml, which instantiate containers directly with no
     * `controller` in scope). */
    /* controller.widgetModel.generation is read but unused — it's a NOTIFYing
     * property that only exists to give this binding a reactive dependency.
     * childModel() itself is a plain Q_INVOKABLE with no NOTIFY, so without
     * this the binding would evaluate once and never again: every previously
     * vended ChildModel is deleted on the next setWidgets()/clear() reset
     * (e.g. a live design-mode file reload), leaving `_childModel` pointing
     * at a destroyed object forever. See WidgetModel.h's `generation` doc. */
    property var    _childModel: {
        if (!_isContainer) return null
        controller.widgetModel.generation
        return controller.widgetModel.childModel(model.row)
    }

    visible: model.widgetVisible !== false

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
                   : _type === "dpad"         ? dpadComp
                   : _type === "section"      ? sectionComp
                   : null

    /* Leaf widget components — no cycle: none of these files reference WidgetDelegate */
    Component { id: displayComp;     DisplayWidget     { widgetId: _widgetId; label: _label; enabled: _enabled; value: _value; props: _props; compact: root.compact } }
    Component { id: ledComp;         LedWidget         { widgetId: _widgetId; label: _label; enabled: _enabled; value: _value; props: _props; compact: root.compact } }
    Component { id: rgbledComp;      RgbLedWidget      { widgetId: _widgetId; label: _label; enabled: _enabled; value: _value; compact: root.compact } }
    Component { id: buttonComp;      ButtonWidget      { widgetId: _widgetId; label: _label; enabled: _enabled; props: _props; childModel: root._childModel } }
    Component { id: buttonGroupComp; ButtonGroupWidget { widgetId: _widgetId; label: _label; enabled: _enabled; value: _value; props: _props; childModel: root._childModel } }
    Component { id: sliderComp;      SliderWidget      { widgetId: _widgetId; label: _label; enabled: _enabled; value: _value; props: _props } }
    Component { id: toggleComp;      ToggleWidget      { widgetId: _widgetId; label: _label; enabled: _enabled; value: _value } }
    Component { id: textComp;        TextWidget        { widgetId: _widgetId; label: _label; enabled: _enabled; value: _value; props: _props } }
    Component { id: dropdownComp;    DropdownWidget    { widgetId: _widgetId; label: _label; enabled: _enabled; value: _value; props: _props } }
    Component { id: labelComp;       LabelWidget       { props: _props; compact: root.compact } }
    Component { id: dpadComp;        DpadWidget        { label: _label; props: _props; childModel: root._childModel } }
    Component { id: separatorComp;   SeparatorWidget   {} }

    /* Container components — dynamic URL loading breaks the bilateral cycle.
     * RowWidget/GridWidget use WidgetDelegate as their Repeater delegate (static
     * reference from their side), so WidgetDelegate must NOT reference them by
     * type name in return.  Using Qt.resolvedUrl produces a runtime string; Qt's
     * cycle detector does not follow string arguments.
     * Requires RowWidget.props/childModel and GridWidget.props/childModel to be
     * non-required (set via live binding in onLoaded after the item is
     * created). */
    Component {
        id: rowComp
        Loader {
            anchors { left: parent.left; right: parent.right }
            source: Qt.resolvedUrl("RowWidget.qml")
            onLoaded: {
                item.label = Qt.binding(function() { return root._label })
                item.props = Qt.binding(function() { return root._props })
                item.childModel = Qt.binding(function() { return root._childModel })
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
                item.childModel = Qt.binding(function() { return root._childModel })
                item.compact = Qt.binding(function() { return root.compact })
            }
            /* Without this, a grid nested inside another row/grid gets
             * width 0 — same class of bug Layout.fillWidth above already
             * fixes for nested rows (see the long implicitWidth-propagation
             * comment on this file's Layout.preferredWidth above). */
            Layout.fillWidth: true
        }
    }
    Component {
        id: sectionComp
        Loader {
            anchors { left: parent.left; right: parent.right }
            source: Qt.resolvedUrl("SectionWidget.qml")
            onLoaded: {
                item.label = Qt.binding(function() { return root._label })
                item.props = Qt.binding(function() { return root._props })
                item.childModel = Qt.binding(function() { return root._childModel })
                /* toggleSection() takes the flat-model row this section
                 * itself occupies (model.row), not widgetId — sections
                 * always have widgetId 0 (see this file's own _childModel
                 * comment on why row, not widgetId, keys container lookups). */
                item.toggleClicked.connect(function() { controller.widgetModel.toggleSection(model.row) })
            }
            Layout.fillWidth: true
        }
    }
}
