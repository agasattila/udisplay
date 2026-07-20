// SPDX-License-Identifier: LGPL-3.0-only
// Copyright (c) 2026 Attila Agas

import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Layouts

/* Boolean toggle. Sends toggle_change EVENT on user tap.
 * No optimistic flip — stays in current state until device confirms. */
Rectangle {
    id: root
    required property int    widgetId
    required property string label
    required property bool   enabled
    required property var    value   /* uint8: 0=off, 1=on; null until first update */

    implicitWidth: toggleLabel.implicitWidth + track.width + 12 + 32
    implicitHeight: 56
    color: "transparent"
    opacity: enabled ? 1.0 : 0.4

    RowLayout {
        anchors { fill: parent; leftMargin: 16; rightMargin: 16 }
        spacing: 12

        Label {
            id: toggleLabel
            text: root.label
            color: controller.activeStyle.text
            font.pixelSize: 14
            Layout.fillWidth: true
        }

        /* Custom toggle track */
        Rectangle {
            id: track
            width: 46; height: 26
            radius: 13
            color: isOn ? controller.activeStyle.accent : controller.activeStyle.surface
            border.color: isOn ? controller.activeStyle.accent : controller.activeStyle.border
            border.width: 1
            property bool isOn: root.value === 1 || root.value === true

            Behavior on color { ColorAnimation { duration: 120 } }

            /* Thumb */
            Rectangle {
                id: thumb
                width: 20; height: 20
                radius: 10
                anchors.verticalCenter: parent.verticalCenter
                x: track.isOn ? track.width - width - 3 : 3
                color: track.isOn ? controller.activeStyle.background : controller.activeStyle.text_muted

                Behavior on x { NumberAnimation { duration: 120; easing.type: Easing.OutQuad } }
            }

            MouseArea {
                anchors.fill: parent
                enabled: root.enabled
                onClicked: {
                    /* Send the opposite of current state. */
                    let newState = track.isOn ? 0 : 1
                    controller.sendToggleChange(root.widgetId, newState === 1)
                }
            }
        }
    }
}
