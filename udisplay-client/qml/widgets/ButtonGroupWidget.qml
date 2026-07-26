// SPDX-License-Identifier: LGPL-3.0-only
// Copyright (c) 2026 Attila Agas

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Controls.Material
import "./"

/* Exclusive-select button group (grid and dpad layouts).
 *
 * Item fill color, press-darken, shape/radius, and disabled opacity come
 * from the shared ButtonFace.qml component (see its header comment) so
 * items look and behave exactly like a standalone button. Selection is
 * layered on top as a border + bold-label overlay — independent of
 * ButtonFace's own fill/press styling — since `value` is currently
 * unimplemented on the firmware side (see the design doc); the overlay
 * stays inert until a real setter exists but the visuals are already
 * correct for when it does. */
Rectangle {
    id: root
    required property int    widgetId
    required property string label
    required property bool   enabled
    required property var    value    /* active item widgetId or null */
    required property var    props

    /* dpad: fixed 3x3 grid of 64px cells. grid (Flow): items wrap, so there's
     * no single "natural" width for an arbitrary item count — use up to 3
     * columns worth (Flow's typical wrap point) as a reasonable estimate. */
    implicitWidth: (props.layout === "dpad")
        ? (64 * 3 + 8 * 2 + 32)
        : (Math.min((props.items || []).length, 3) * (110 + 8) + 32)
    implicitHeight: col.implicitHeight + 24
    color: "transparent"

    function findDpadItem(position) {
        if (!position) return null;
        var items = props.items || [];
        for (var i = 0; i < items.length; i++) {
            if (items[i].position === position) return items[i];
        }
        return null;
    }

    Column {
        id: col
        anchors { left: parent.left; right: parent.right; top: parent.top;
                  leftMargin: 16; rightMargin: 16; topMargin: 12 }
        spacing: 8

        Label {
            text: root.label
            color: controller.activeStyle.text_muted
            font.pixelSize: 12
            font.capitalization: Font.AllUppercase
            font.letterSpacing: 1
            visible: root.label.length > 0
        }

        /* ── Grid layout ───────────────────────────────────────────────── */
        Flow {
            width: parent.width
            spacing: 8
            visible: props.layout !== "dpad"

            Repeater {
                model: props.layout !== "dpad" ? (props.items || []) : []

                delegate: ButtonFace {
                    required property var modelData

                    width:  110; height: 36
                    enabled: root.enabled
                    showLabel: false

                    border.color: root.value === modelData.widgetId
                                  ? controller.activeStyle.button : controller.activeStyle.border
                    border.width: 1

                    onButtonPressed:  controller.sendButtonPress(modelData.widgetId)
                    onButtonReleased: controller.sendButtonRelease(modelData.widgetId)
                    onButtonClicked:  controller.sendButtonClick(modelData.widgetId)

                    Label {
                        anchors.centerIn: parent
                        text: modelData.label
                        /* button_text unconditionally — fill is always
                         * activeStyle.button (via ButtonFace) now regardless
                         * of selection, so activeStyle.text (meant for the
                         * old dark "surface" fill) would be unreadable here. */
                        color: controller.activeStyle.button_text
                        font.pixelSize: 13
                        font.bold: root.value === modelData.widgetId
                    }
                }
            }
        }

        /* ── DPad layout ────────────────────────────────────────────────
         *  3×3 grid; corners are transparent spacers.
         *  Each cell looks up its item by position string.
         *  Layout:  [ ]   [top]    [ ]
         *           [left] [center] [right]
         *           [ ]   [bottom] [ ]
         * ─────────────────────────────────────────────────────────────── */
        Grid {
            columns: 3
            spacing: 8
            visible: props.layout === "dpad"
            anchors.horizontalCenter: parent.horizontalCenter

            Repeater {
                /* 9 cells: empty string = invisible corner spacer */
                model: ["", "top", "", "left", "center", "right", "", "bottom", ""]

                delegate: Item {
                    id: cell
                    width: 64; height: 64

                    required property string modelData
                    property var btnItem: root.findDpadItem(modelData)

                    ButtonFace {
                        anchors.fill: parent
                        visible: cell.btnItem !== null
                        enabled: root.enabled
                        showLabel: false

                        border.color: (cell.btnItem !== null && root.value === cell.btnItem.widgetId)
                                      ? controller.activeStyle.button : controller.activeStyle.border
                        border.width: 1

                        onButtonPressed:  { if (cell.btnItem) controller.sendButtonPress(cell.btnItem.widgetId) }
                        onButtonReleased: { if (cell.btnItem) controller.sendButtonRelease(cell.btnItem.widgetId) }
                        onButtonClicked:  { if (cell.btnItem) controller.sendButtonClick(cell.btnItem.widgetId) }

                        Label {
                            anchors.centerIn: parent
                            text: cell.btnItem ? cell.btnItem.label : ""
                            /* button_text unconditionally — same reasoning
                             * as the grid delegate's label above. */
                            color: controller.activeStyle.button_text
                            font.pixelSize: 16
                            font.bold: cell.btnItem !== null && root.value === cell.btnItem.widgetId
                        }
                    }
                }
            }
        }
    }
}
