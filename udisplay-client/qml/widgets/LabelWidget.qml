// SPDX-License-Identifier: LGPL-3.0-only
// Copyright (c) 2026 Attila Agas

import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material

/* Static text label. props.style: "heading" | "body" | "caption".
 * props.labelAlign: "left" | "right" | "center" | "justify" — default "left".
 * compact: true Historically it was shrinked for use on a button face. Now only color is changed */
Rectangle {
    required property var props   /* { text, style, labelAlign } */
    property bool compact: false

    implicitWidth: lbl.implicitWidth + 32
    implicitHeight: lbl.implicitHeight + 12

    color: "transparent"

    Label {
        id: lbl
        anchors { left: parent.left; right: parent.right;
                  top: parent.top; leftMargin: 16; rightMargin: 16;
                  topMargin: 6 }
        text: props.text || ""
        wrapMode: Text.Wrap
        color: compact ? controller.activeStyle.button_text
             : props.style === "heading" ? controller.activeStyle.text_heading
             : props.style === "caption" ? controller.activeStyle.text_muted
             : controller.activeStyle.text
        font.pixelSize: props.style === "heading" ? 18
                      : props.style === "caption" ? 11
                      : 14
        font.bold: props.style === "heading"
        font.letterSpacing: props.style === "heading" ? 0.5 : 0
        horizontalAlignment: props.labelAlign === "right"   ? Text.AlignRight
                            : props.labelAlign === "center"  ? Text.AlignHCenter
                            : props.labelAlign === "justify" ? Text.AlignJustify
                            : Text.AlignLeft
        textFormat: Text.PlainText
    }
}
