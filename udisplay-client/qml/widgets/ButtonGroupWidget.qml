// SPDX-License-Identifier: LGPL-3.0-only
// Copyright (c) 2026 Attila Agas

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Controls.Material
import "./"

/* Exclusive-select button group (grid layout). For a directional pad, use
 * the `dpad` container type (DpadWidget.qml) instead — dpad has no
 * selection/setter, so it doesn't belong here (see the dpad-split design
 * doc's Architecture Issue #2).
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

    /* Flow: items wrap, so there's no single "natural" width for an
     * arbitrary item count — use up to 3 columns worth (Flow's typical wrap
     * point) as a reasonable estimate. */
    implicitWidth: Math.min((props.items || []).length, 3) * (110 + 8) + 32
    implicitHeight: col.implicitHeight + 24
    color: "transparent"

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

            Repeater {
                model: props.items || []

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
    }
}
