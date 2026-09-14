// SPDX-License-Identifier: LGPL-3.0-only
// Copyright (c) 2026 Attila Agas

import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material

/* Static text label. props.labelStyle: "heading" | "body" | "caption".
 * props.labelAlign: "left" | "right" | "center" | "justify" — default "left".
 * compact: true Historically it was shrinked for use on a button face. Now only color is changed */
Rectangle {
    required property var props   /* { text, labelStyle, labelAlign } */
    property bool compact: false
    /* Resolved style tokens for this row (see WidgetDelegate.qml's
     * _effectiveStyle). Non-required, defaulting to {} — standalone QML
     * tests instantiate this widget directly with no controller/resolver in
     * scope, so color bindings below read undefined fields gracefully (no crash). */
    property var effectiveStyle: ({})

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
        color: compact ? effectiveStyle.button_text
             : props.labelStyle === "heading" ? effectiveStyle.text_heading
             : props.labelStyle === "caption" ? effectiveStyle.text_muted
             : effectiveStyle.text
        font.pixelSize: props.labelStyle === "heading" ? 18
                      : props.labelStyle === "caption" ? 11
                      : 14
        font.bold: props.labelStyle === "heading"
        font.letterSpacing: props.labelStyle === "heading" ? 0.5 : 0
        horizontalAlignment: props.labelAlign === "right"   ? Text.AlignRight
                            : props.labelAlign === "center"  ? Text.AlignHCenter
                            : props.labelAlign === "justify" ? Text.AlignJustify
                            : Text.AlignLeft
        textFormat: Text.PlainText
    }
}
