// SPDX-License-Identifier: LGPL-3.0-only
// Copyright (c) 2026 Attila Agas

import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Layouts

/* Section header — renders a labelled group divider.
 * Children appear as normal items in the flat Repeater after this widget.
 * props.collapsible: bool — show chevron toggle
 * props.collapsed:   bool — current collapsed state (runtime, from model) */
Rectangle {
    id: root
    required property string label
    property var props: ({})

    signal toggleClicked()

    /* The divider Rectangle below is a fillWidth spacer with no natural
     * size; 40 covers spacing + the optional chevron's width. Section isn't
     * currently supported as a row/grid child (TODO-032), so this mostly
     * matters for consistency with the rest of the widget set. */
    implicitWidth: sectionLabel.implicitWidth + 40 + 32
    implicitHeight: 36
    color: controller.activeStyle.surface

    RowLayout {
        anchors { fill: parent; leftMargin: 16; rightMargin: 16 }
        spacing: 8

        Label {
            id: sectionLabel
            text: label.toUpperCase()
            color: controller.activeStyle.accent
            font.pixelSize: 11
            font.letterSpacing: 1.5
            font.bold: true
        }

        Rectangle {
            Layout.fillWidth: true
            height: 1
            color: controller.activeStyle.line
        }

        Label {
            visible: props.collapsible === true
            text: props.collapsed === true ? "▶" : "▼"
            color: controller.activeStyle.accent
            font.pixelSize: 11
        }
    }

    MouseArea {
        anchors.fill: parent
        enabled: props.collapsible === true
        onClicked: root.toggleClicked()
    }
}
