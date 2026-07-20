// SPDX-License-Identifier: LGPL-3.0-only
// Copyright (c) 2026 Attila Agas

import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Layouts

/* Exclusive-select dropdown. Sends EVT_SELECTION_CHANGE on pick. */
Rectangle {
    id: root
    required property int    widgetId
    required property string label
    required property bool   enabled
    required property var    value   /* selected index (int) or null */
    required property var    props   /* { items: [{key, label}, ...] } */

    implicitWidth: dropdownLabel.implicitWidth + 140 + 12 + 32  /* 140 = combo's own Layout.minimumWidth */
    implicitHeight: 72
    color: "transparent"

    RowLayout {
        anchors { fill: parent; leftMargin: 16; rightMargin: 16 }
        spacing: 12

        Label {
            id: dropdownLabel
            text: root.label
            color: controller.activeStyle.text_muted
            font.pixelSize: 12
            font.capitalization: Font.AllUppercase
            font.letterSpacing: 1
            Layout.fillWidth: true
            visible: root.label.length > 0
        }

        ComboBox {
            id: combo
            Layout.minimumWidth: 140
            Layout.minimumHeight: contentItem.implicitHeight+16
            enabled: root.enabled
            opacity: root.enabled ? 1.0 : 0.4

            model: (props.items || []).map(function(i) { return i.label })

            currentIndex: {
                let v = root.value
                if (v === null || v === undefined) return 0
                let idx = parseInt(v)
                return (idx >= 0 && idx < count) ? idx : 0
            }

            background: Rectangle {
                color: combo.pressed ? controller.activeStyle.surface : controller.activeStyle.background
                radius: 8
                border.color: controller.activeStyle.accent
                border.width: 1
            }

            contentItem: Label {
                leftPadding: 10
                rightPadding: 10
                text: combo.displayText
                color: controller.activeStyle.text_heading
                font.pixelSize: 13
                verticalAlignment: Text.AlignVCenter
            }

            delegate: ItemDelegate {
                required property string modelData
                required property int index
                width: combo.width
                highlighted: combo.highlightedIndex === index
                contentItem: Label {
                    text: modelData
                    color: highlighted ? controller.activeStyle.accent : controller.activeStyle.text
                    font.pixelSize: 13
                }
                background: Rectangle {
                    color: highlighted ? controller.activeStyle.surface : "transparent"
                }
            }

            onActivated: function(idx) {
                controller.sendSelectionChange(root.widgetId, idx)
            }
        }
    }
}
