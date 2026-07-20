// SPDX-License-Identifier: LGPL-3.0-only
// Copyright (c) 2026 Attila Agas

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

/* RGB LED indicator — dot + label, left-aligned, full-width row.
 * Value is a packed int32 (0x00RRGGBB) pushed by the device via VAL_INT32.
 * compact: true when renders on a button face. Historically it was rendered in small size.
 */
Rectangle {
    id: root
    required property int    widgetId
    required property string label
    required property bool   enabled
    required property var    value
    property bool compact: false

    implicitWidth: rgbLedLabel.implicitWidth + 12 + 12 + 32  /* 12 dot + 12 spacing + margins */
    implicitHeight: Math.max(rgbLedLabel.implicitHeight, 48)
    color: "transparent"
    opacity: enabled ? 1.0 : 0.4

    /* Extract R, G, B from packed int32. Use 0 (off) when value is null. */
    readonly property int _rgb: value !== null && value !== undefined ? value : 0
    readonly property int _r: (_rgb >> 16) & 0xFF
    readonly property int _g: (_rgb >>  8) & 0xFF
    readonly property int _b:  _rgb        & 0xFF
    readonly property color _dotColor: Qt.rgba(_r / 255.0, _g / 255.0, _b / 255.0, 1.0)
    /* Shared by both the compact and full-size dot below. */
    readonly property color _dotFillColor:   _rgb !== 0 ? _dotColor : "#2a2a4a"
    readonly property color _dotBorderColor: _rgb !== 0 ? _dotColor : "#444"


    RowLayout {
        anchors { fill: parent; leftMargin: 16; rightMargin: 16 }
        spacing: 12

        /* RGB LED dot */
        Rectangle {
            width: 12; height: 12
            radius: 6
            color: root._dotFillColor
            border.color: root._dotBorderColor
            border.width: 1
        }

        Label {
            id: rgbLedLabel
            text: root.label
            color: controller.activeStyle.text
            font.pixelSize: 14
            Layout.fillWidth: true
        }
    }
}
