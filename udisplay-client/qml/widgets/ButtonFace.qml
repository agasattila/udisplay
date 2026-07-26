// SPDX-License-Identifier: LGPL-3.0-only
// Copyright (c) 2026 Attila Agas

import QtQuick

/* Shared visual + interaction primitive for momentary button faces: fill
 * color, press-darken, shape-driven radius, and disabled opacity. Used by
 * both ButtonWidget.qml (standalone momentary button) and
 * ButtonGroupWidget.qml (grid/dpad items) so the two always look and behave
 * the same on tap — button-group items previously used selection-driven
 * colors with no press feedback at all, which is exactly the mismatch this
 * component exists to close.
 *
 * Purely visual + interaction: emits buttonPressed/Released/Clicked signals
 * rather than calling controller.sendButtonPress/Release/Click directly, so
 * each caller wires the signals to whichever widget_id is appropriate
 * (root.widgetId for a standalone button, modelData.widgetId for a
 * button-group item). */
Rectangle {
    id: face

    property string label:     ""
    property bool   enabled:   true
    property string shape:     "rect"  // rect | circle | square
    property bool   showLabel: true    // callers with their own label overlay (e.g. selection styling) set this false

    readonly property alias labelImplicitWidth:  labelText.implicitWidth
    readonly property alias labelImplicitHeight: labelText.implicitHeight

    signal buttonPressed()
    signal buttonReleased()
    signal buttonClicked()

    property color accentColor: controller.activeStyle.button

    radius:  shape === "circle" ? Math.min(width, height) / 2 : shape === "square" ? 4 : 8
    color:   mouseArea.pressed ? Qt.darker(accentColor, 1.3) : accentColor
    opacity: enabled ? 1.0 : 0.3

    Text {
        id: labelText
        anchors.centerIn: parent
        text:  face.label
        color: controller.activeStyle.button_text
        font.pixelSize: 14
        font.bold: true
        visible: face.showLabel && face.label.length > 0
    }

    MouseArea {
        id: mouseArea
        anchors.fill: parent
        enabled: face.enabled
        onPressed: face.buttonPressed()
        onReleased: {
            face.buttonReleased()
            if (containsMouse)
                face.buttonClicked()
        }
        onCanceled: face.buttonReleased()
    }
}
