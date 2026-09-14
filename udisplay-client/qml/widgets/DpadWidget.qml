// SPDX-License-Identifier: LGPL-3.0-only
// Copyright (c) 2026 Attila Agas

import QtQuick
import QtQuick.Layouts
import "./"

/* Directional pad container. Places its `button` children into a 3x3 cross
 * layout by each child's `position` (top/right/bottom/left/center) — corner
 * cells are always empty spacers. No group-level state: unlike button-group,
 * dpad has no selection/setter — each cell is an ordinary ButtonWidget that
 * sends its own button_press/release/click events by its own widgetId (see
 * the dpad-split design doc's Architecture Issue #2). All 5 cells render
 * identically regardless of position — no special-case for "center".
 *
 * Position-lookup pattern (9-cell Grid, corners as invisible spacers) reused
 * from ButtonGroupWidget.qml's now-removed dpad delegate.
 *
 * childModel: every item in this dpad, as a real model (see
 * WidgetModel::childModel()) — each row's own `props.position` holds its
 * cross-layout slot (top/right/bottom/left/center). Dpad is the one
 * container that needs lookup BY a property (position) rather than
 * sequential Repeater access, since it renders a fixed 9-cell cross, not a
 * list — findByPosition() below does the same linear scan it always did,
 * just reading rows out of childModel (via its get() convenience method)
 * instead of a plain props.items array.
 *
 * Uses relative import "./" per Android qmlcachegen requirement — must NOT
 * import the module URI.
 */
Rectangle {
    id: root
    property string label: ""   /* optional; not rendered */
    property var props: ({})  /* unused by dpad itself today — kept for shape parity with other containers */
    /* Non-required so Loader.source can bind it (WidgetDelegate.qml's
     * dpadComp), matching props above. */
    property var childModel: null

    function findByPosition(position) {
        var n = root.childModel ? root.childModel.rowCount() : 0
        for (var i = 0; i < n; i++) {
            var item = root.childModel.get(i)
            if ((item.props && item.props.position) === position) return item
        }
        return null
    }

    color: "transparent"
    implicitWidth: grid.implicitWidth
    implicitHeight: grid.implicitHeight
    Layout.fillWidth: true


    Grid {
        id: grid
        columns: 3
        spacing: 8
        anchors.horizontalCenter: parent.horizontalCenter

        /* Uniform cell size for the whole cross, derived from the largest
         * real button's own natural size, floored at a 44px touch target —
         * NOT a hardcoded pixel constant. Keeps the classic dpad's arms
         * visually symmetric while never letting a cell shrink below the
         * accessibility minimum on small embedded touchscreens. Same
         * repeater.itemAt(i)-drilling pattern RowWidget.qml/GridWidget.qml
         * use for their own content-based implicit sizing. */
        property real cellSize: {
            var size = 44
            for (var i = 0; i < repeater.count; i++) {
                var d = repeater.itemAt(i)
                if (d && d.btnItem !== null)
                    size = Math.max(size, d.naturalSize)
            }
            return size
        }

        Repeater {
            id: repeater
            /* 9 cells: empty string = invisible corner spacer. */
            model: ["", "top", "", "left", "center", "right", "", "bottom", ""]

            delegate: Item {
                id: cell
                required property string modelData
                property var btnItem: root.findByPosition(modelData)

                readonly property real naturalSize: Math.max(btn.implicitWidth, btn.implicitHeight)

                implicitWidth:  grid.cellSize
                implicitHeight: grid.cellSize

                ButtonWidget {
                    id: btn
                    anchors.fill: parent
                    visible:  cell.btnItem !== null
                    widgetId: cell.btnItem ? cell.btnItem.widgetId : 0
                    label:    cell.btnItem ? cell.btnItem.label : ""
                    enabled:  cell.btnItem ? cell.btnItem.enabled !== false : false
                    props:    cell.btnItem ? cell.btnItem.props : ({})
                }
            }
        }
    }
}
