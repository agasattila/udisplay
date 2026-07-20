// SPDX-License-Identifier: LGPL-3.0-only
// Copyright (c) 2026 Attila Agas

import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Layouts

/* Numeric slider. Sends slider_change EVENT on release. */
Rectangle {
    id: root
    required property int    widgetId
    required property string label
    required property bool   enabled
    required property var    value   /* float, null until first STATE_UPDATE */
    required property var    props

    /* The Slider control itself stretches to fill (Layout.fillWidth below) —
     * there's no natural "track width" worth measuring. 180px is a
     * reasonable minimum usable slider width; the label/value row can push
     * it wider for a long label. */
    implicitWidth: Math.max(sliderLabel.implicitWidth + valueLabel.implicitWidth + 40, 180) + 32
    implicitHeight: 80
    color: "transparent"

    ColumnLayout {
        anchors { fill: parent; leftMargin: 16; rightMargin: 16;
                  topMargin: 10; bottomMargin: 10 }
        spacing: 4

        RowLayout {
            Layout.fillWidth: true
            Label {
                id: sliderLabel
                text: root.label
                color: controller.activeStyle.text_muted
                font.pixelSize: 12
                font.capitalization: Font.AllUppercase
                font.letterSpacing: 1
                visible: root.label.length > 0
            }
            Item { Layout.fillWidth: true }
            Label {
                id: valueLabel
                text: {
                    let v = slider.value
                    let step = props.step || 1
                    let decimals = step < 1
                        ? (String(step).split('.')[1] ? String(step).split('.')[1].length : 1)
                        : 0
                    return v.toFixed(decimals) + (props.unit ? " " + props.unit : "")
                }
                color: controller.activeStyle.accent
                font.pixelSize: 14
                font.family: "monospace"
            }
        }

        Slider {
            id: slider
            Layout.fillWidth: true
            from:       props.min  !== undefined ? props.min  : 0
            to:         props.max  !== undefined ? props.max  : 100
            stepSize:   props.step !== undefined ? props.step : 1
            value:      root.value !== null && root.value !== undefined
                        ? parseFloat(root.value) : props.min || 0
            enabled:    root.enabled
            opacity:    root.enabled ? 1.0 : 0.4

            Material.accent: controller.activeStyle.accent
            background: Rectangle {
                x: slider.leftPadding
                y: slider.topPadding + slider.availableHeight / 2 - height / 2
                width: slider.availableWidth
                height: 4
                radius: 2
                color: controller.activeStyle.surface
                Rectangle {
                    width: slider.visualPosition * parent.width
                    height: parent.height
                    color: controller.activeStyle.accent
                    radius: 2
                }
            }
            handle: Rectangle {
                x: slider.leftPadding + slider.visualPosition * (slider.availableWidth - width)
                y: slider.topPadding + slider.availableHeight / 2 - height / 2
                width: 18; height: 18
                radius: 9
                color: slider.pressed
                       ? Qt.lighter(controller.activeStyle.accent, 1.2)
                       : controller.activeStyle.accent
                border.color: controller.activeStyle.background
                border.width: 2
            }

            onPressedChanged: {
                if (!pressed)
                    controller.sendSliderChange(root.widgetId, value)
            }
        }
    }
}
