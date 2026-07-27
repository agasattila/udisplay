// SPDX-License-Identifier: LGPL-3.0-only
// Copyright (c) 2026 Attila Agas

import QtQuick
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
 * props.items: [{type, widgetId, label, enabled, visible, value, props, position}, ...]
 *
 * Uses relative import "./" per Android qmlcachegen requirement — must NOT
 * import the module URI.
 */
Rectangle {
    id: root
    property var props: ({})  /* { items: [...] } — non-required so Loader.source can bind it, matching RowWidget/GridWidget */

    function findByPosition(position) {
        var items = root.props.items || []
        for (var i = 0; i < items.length; i++) {
            if (items[i].position === position) return items[i]
        }
        return null
    }

    implicitWidth: grid.implicitWidth
    implicitHeight: grid.implicitHeight
    color: "transparent"

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
                if (d && d.btnItem)
                    size = Math.max(size, d.buttonImplicitWidth, d.buttonImplicitHeight)
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

                readonly property real buttonImplicitWidth:  btn.implicitWidth
                readonly property real buttonImplicitHeight: btn.implicitHeight

                width:  grid.cellSize
                height: grid.cellSize

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
