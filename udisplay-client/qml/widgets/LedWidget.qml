// SPDX-License-Identifier: LGPL-3.0-only
// Copyright (c) 2026 Attila Agas

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

/* Standalone LED indicator — dot + label, left-aligned, full-width row.
 * compact: true when renders on a button face. Historically it was rendered in small size.
 */
Rectangle {
    id: root
    required property int    widgetId
    required property string label
    required property bool   enabled
    required property var    value
    required property var    props
    property bool compact: false
    /* Resolved style tokens for this row (see WidgetDelegate.qml's
     * _effectiveStyle). Non-required, defaulting to {} — standalone QML
     * tests instantiate this widget directly with no controller/resolver in
     * scope, so color bindings below read undefined fields gracefully (no crash). */
    property var effectiveStyle: ({})

    implicitWidth: ledLabel.implicitWidth + 12 + 12 + 32  /* 12 dot + 12 spacing + margins */
    implicitHeight: Math.max(ledLabel.implicitHeight, 48)
    color: "transparent"
    opacity: enabled ? 1.0 : 0.4

    /* Border is intentionally brighter than fill for a subtle ring glow when on. */
    readonly property color _activeColor: props.color !== undefined ? props.color : effectiveStyle.accent
    readonly property color _dotFillColor:   value ? _activeColor : "#2a2a4a"
    readonly property color _dotBorderColor: value ? _activeColor : "#444"

    RowLayout {
        anchors { fill: parent; leftMargin: 16; rightMargin: 16 }
        spacing: 12

        Rectangle {
            width: 12; height: 12
            radius: 6
            color: root._dotFillColor
            border.color: root._dotBorderColor
            border.width: 1
        }

        Label {
            id: ledLabel
            text: root.label
            color: compact ? effectiveStyle.button_text : effectiveStyle.text
            font.pixelSize: 14
            Layout.fillWidth: true
        }
    }
}
