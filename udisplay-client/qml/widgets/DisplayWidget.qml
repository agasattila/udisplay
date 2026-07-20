// SPDX-License-Identifier: LGPL-3.0-only
// Copyright (c) 2026 Attila Agas

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

/* Numeric read-only display. style="large" renders as a hero block.
 * compact: true renders a small inline reading (label + value + unit,
 * shrunk fonts/margins, style="large" ignored) — used on a button face,
 * where the standalone 64px (96px at style: large) row would overflow a
 * 40px button. */
Rectangle {
    id: root
    required property int    widgetId
    required property string label
    required property bool   enabled
    required property var    value
    required property var    props
    property bool compact: false

    /* Derived (compact, style) sizing — computed once here rather than
     * repeating the two-level ternary at each Label use site below. */
    readonly property bool  _isLarge:            !compact && props.style === "large"
    readonly property int   _boxHeight:           compact ? 24 : (props.style === "large" ? 96 : 64)
    readonly property int   _labelPixelSize:      compact ? 10 : (props.style === "large" ? 13 : 12)
    readonly property int   _valuePixelSize:      compact ? 14 : (props.style === "large" ? 36 : 22)
    readonly property int   _unitPixelSize:       compact ? 10 : (props.style === "large" ? 14 : 12)
    readonly property int   _unitBottomPadding:   compact ? 0  : (props.style === "large" ? 4  : 2)

    implicitWidth: contentRow.implicitWidth + (compact ? 16 : 32)  /* + left/right margins below */
    implicitHeight: _boxHeight
    color: "transparent"

    opacity: enabled ? 1.0 : 0.4

    RowLayout {
        id: contentRow
        anchors { fill: parent; leftMargin: root.compact ? 8 : 16; rightMargin: root.compact ? 8 : 16 }
        spacing: root.compact ? 4 : 8

        /* Label */
        Label {
            text: root.label
            color: controller.activeStyle.text_muted
            font.pixelSize: root._labelPixelSize
            font.capitalization: Font.AllUppercase
            font.letterSpacing: 1
            Layout.alignment: Qt.AlignVCenter
            visible: root.label.length > 0
        }

        Item { Layout.fillWidth: true }

        /* Value + unit */
        RowLayout {
            spacing: 4
            Layout.alignment: Qt.AlignVCenter

            Label {
                text: {
                    if (root.value === null || root.value === undefined)
                        return "—"
                    let fmt = props.format || "%.2f"
                    /* Simple format: just show the number with fixed decimals. */
                    let num = parseFloat(root.value)
                    if (isNaN(num)) return String(root.value)
                    /* Parse precision from fmt like "%.2f" or "%d" */
                    let dotIdx = fmt.indexOf(".")
                    let fIdx = fmt.lastIndexOf("f")
                    if (dotIdx >= 0 && fIdx > dotIdx)
                        return num.toFixed(parseInt(fmt.substring(dotIdx + 1, fIdx)))
                    if (fmt.indexOf("%d") >= 0) return Math.round(num).toString()
                    return num.toFixed(2)
                }
                color: controller.activeStyle.accent
                font.pixelSize: root._valuePixelSize
                font.bold: root._isLarge
                font.family: "monospace"
            }

            Label {
                text: props.unit || ""
                color: controller.activeStyle.text_muted
                font.pixelSize: root._unitPixelSize
                Layout.alignment: Qt.AlignBottom
                bottomPadding: root._unitBottomPadding
                visible: (props.unit || "").length > 0
            }
        }
    }
}
