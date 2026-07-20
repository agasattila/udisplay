// SPDX-License-Identifier: LGPL-3.0-only
// Copyright (c) 2026 Attila Agas

import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Layouts

/* String widget.
 * readonly: displays device-pushed value.
 * rw: editable TextField; sends text_submit EVENT on commit. */
Rectangle {
    id: root
    required property int    widgetId
    required property string label
    required property bool   enabled
    required property var    value    /* string, null until first update */
    required property var    props

    /* rwField/value are input controls stretched to fill whatever width
     * they're given (width: parent.width below) — there's no "current text
     * width" worth measuring for an editable field. 160px is a reasonable
     * minimum input width; labelText can push it wider for a long label. */
    implicitWidth: Math.max(labelText.implicitWidth, 160) + 32
    implicitHeight: col.implicitHeight + 24
    color: "transparent"
    opacity: enabled ? 1.0 : 0.4

    Column {
        id: col
        anchors {
            left: parent.left; right: parent.right; top: parent.top
            leftMargin: 16; rightMargin: 16; topMargin: 12
        }
        spacing: 6

        Label {
            id: labelText
            text: root.label
            color: controller.activeStyle.text_muted
            font.pixelSize: 12
            font.capitalization: Font.AllUppercase
            font.letterSpacing: 1
            visible: root.label.length > 0
        }

        /* Read-only display */
        Label {
            width: parent.width
            text: root.value !== null && root.value !== undefined
                  ? String(root.value) : "—"
            color: controller.activeStyle.text_heading
            font.pixelSize: 15
            elide: Text.ElideRight
            visible: (props.mode || "readonly") === "readonly"
        }

        /* Read-write field */
        TextField {
            id: rwField
            width: parent.width
            placeholderText: props.placeholder || ""
            text: root.value !== null && root.value !== undefined
                  ? String(root.value) : ""
            maximumLength: props.maxlength || 255
            color: controller.activeStyle.text_heading
            Material.accent: controller.activeStyle.accent
            visible: (props.mode || "readonly") === "rw"
            enabled: root.enabled

            background: Rectangle {
                color: controller.activeStyle.surface
                radius: 6
                border.color: rwField.activeFocus
                              ? controller.activeStyle.accent
                              : controller.activeStyle.border
                border.width: 1
            }

            onAccepted: {
                controller.sendTextSubmit(root.widgetId, text)
                rwField.focus = false
            }
        }
    }
}
