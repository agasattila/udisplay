// SPDX-License-Identifier: LGPL-3.0-only
// Copyright (c) 2026 Attila Agas

import QtQuick
import QtQuick.Controls.Material
import QtQuick.Layouts
import "./"

/* Grid layout container.
 * props.columns: number of columns (default 2).
 * props.items: [{type, widgetId, label, enabled, visible, value, props, flex, align}, ...]
 * props.align: "left"|"right"|"center" — default content alignment for
 * children that can't stretch (flex: 0/omitted); a child's own `align`
 * overrides this for that one child.
 * Items auto-flow left-to-right, top-to-bottom.
 * Each item renders via WidgetDelegate, which supports nested row and grid
 * (unlimited depth).
 *
 * Mirrors RowWidget.qml's Layout-driven sizing and manual flex-ratio stretch
 * (see that file's header comment for why: Qt 6.4 floor, no
 * Layout.horizontalStretchFactor, no QML preprocessor to gate it). Unlike a
 * row, a grid's columns are shared across every row that column intersects
 * (GridLayout sizes each column once, to the widest cell in it) — a flex
 * ratio computed against the full grid width would let one high-flex item
 * claim space that isn't actually available to it (it doesn't own the whole
 * grid, just one column). So the ratio is computed per virtual column
 * (items auto-flow left-to-right, top-to-bottom — `index % columns` is an
 * item's column): flex only competes against siblings in the SAME column,
 * against that column's fair share of the grid's width (grid.width /
 * columns). Two children in the same column but different rows with
 * different flex values will each pull their own row's cell width — since
 * GridLayout unifies column width to the widest cell, the practical effect
 * is the largest requested width in that column wins, same as any other
 * mismatched-preferredWidth column in GridLayout. */
Rectangle {
    id: root
    property string label: ""   /* optional; not rendered for layout containers */
    property var    props: ({})  /* { columns, items: [...] } — non-required so Loader.source can bind it */
    /* Compact rendering — non-required (same reason as props above: set via
     * live binding when loaded through a dynamic Loader.source, e.g.
     * WidgetDelegate.qml's gridComp). Forwarded to every child's own
     * WidgetDelegate below so compact threads to arbitrary depth. */
    property bool   compact: false

    /* NOT grid.implicitWidth. Same reasoning as RowWidget.qml's
     * root.implicitWidth */
    implicitWidth: contentImplicitWidth
    implicitHeight: grid.implicitHeight
    color: "transparent"

    property real contentImplicitWidth: {
        var items = props.items || []
        var cols = grid.columns
        var colWidths = []
        for (var c = 0; c < cols; c++) colWidths.push(0)
        for (var i = 0; i < items.length; i++) {
            var d = repeater.itemAt(i)
            /* See RowWidget.qml's contentImplicitWidth for why this drills
             * into d.item.implicitWidth rather than reading d.implicitWidth
             * directly (Loader's own auto-mirror is stuck at 0 for nested
             * row/grid children). */
            var w = (d && d.item) ? d.item.implicitWidth : 0
            var col = i % cols
            if (w > colWidths[col]) colWidths[col] = w
        }
        var sum = 0
        for (var j = 0; j < colWidths.length; j++) sum += colWidths[j]
        return sum + Math.max(0, cols - 1) * grid.columnSpacing
    }

    Layout.fillWidth: true

    GridLayout {
        id: grid
        anchors { left: parent.left; right: parent.right }
        columns: props.columns || 2
        rowSpacing: 0
        columnSpacing: 0

        /* Per-column sum of flex weights (index % columns groups items into
         * their virtual column). flex: 0/omitted items contribute 0. */
        property var columnFlexTotals: {
            var items = props.items || []
            var totals = []
            for (var c = 0; c < grid.columns; c++) totals.push(0)
            for (var i = 0; i < items.length; i++)
                totals[i % grid.columns] += (items[i].flex || 0)
            return totals
        }

        function alignFor(modelData) {
            var a = modelData.align || props.align || "left"
            return a === "right" ? Qt.AlignRight
                 : a === "center" ? Qt.AlignHCenter
                 : Qt.AlignLeft
        }

        Repeater {
            id: repeater
            model: props.items || []

            delegate: WidgetDelegate {
                required property var modelData
                required property int index
                readonly property int col: index % grid.columns
                readonly property real colTotalFlex: grid.columnFlexTotals[col] || 0
                compact: root.compact
                Layout.fillWidth: modelData.flex > 0
                /* item.implicitWidth, not bare implicitWidth — see
                 * RowWidget.qml's identical delegate comment: this binding
                 * overrides WidgetDelegate.qml's own internal
                 * `Layout.preferredWidth: item ? item.implicitWidth : 0` fix,
                 * so it must use the same drill-through or it silently
                 * reintroduces the nested row/grid width-0 collapse. */
                Layout.minimumWidth: item ? item.implicitWidth : 0
                Layout.preferredWidth: Math.max(item ? item.implicitWidth : 0,
                    colTotalFlex > 0
                        ? (grid.width / grid.columns) * modelData.flex / colTotalFlex
                        : 0)
                Layout.alignment: grid.alignFor(modelData)
            }
        }
    }
}
