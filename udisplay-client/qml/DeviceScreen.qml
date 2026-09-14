// SPDX-License-Identifier: LGPL-3.0-only
// Copyright (c) 2026 Attila Agas

import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Layouts
import "widgets"

Page {
    id: root
    title: controller.deviceName
    background: Rectangle { color: controller.activeStyle.background }

    header: ToolBar {
        Material.background: controller.activeStyle.surface
        RowLayout {
            anchors { fill: parent; leftMargin: 8; rightMargin: 8 }
            ToolButton {
                text: "‹"
                font.pixelSize: 20
                onClicked: controller.disconnectDevice()
            }
            Label {
                Layout.fillWidth: true
                text: controller.deviceName
                font.pixelSize: 16
                font.bold: true
                color: controller.activeStyle.text_heading
                elide: Text.ElideRight
                horizontalAlignment: Text.AlignHCenter
            }
            /* status dot */
            Rectangle {
                width: 8; height: 8
                radius: 4
                color: controller.state === "running" ? "#00d4aa" : "#e74c3c"
            }
        }
    }

    /* Design Mode error overlay — shown in-place when YAML has a parse error.
     * State stays "running" so the navigation stack is unaffected; the developer
     * fixes the file and the overlay clears on the next successful reload. */
    Rectangle {
        anchors.fill: parent
        visible: controller.designErrorString !== ""
        color: controller.activeStyle.background
        z: 1

        ColumnLayout {
            anchors { fill: parent; margins: 24 }
            spacing: 16

            Label {
                text: "Parse Error"
                font.pixelSize: 16
                font.bold: true
                color: "#e74c3c"
            }
            Rectangle {
                Layout.fillWidth: true
                height: 1
                color: "#3d0000"
            }
            ScrollView {
                Layout.fillWidth: true
                Layout.fillHeight: true
                contentWidth: availableWidth
                clip: true
                Label {
                    width: parent.width
                    text: controller.designErrorString
                    color: "#ff6b6b"
                    font.family: "monospace"
                    font.pixelSize: 13
                    wrapMode: Text.WordWrap
                }
            }
            Label {
                text: "Fix the YAML and save — UI will reload automatically."
                color: "#666"
                font.pixelSize: 12
                font.italic: true
            }
        }
    }

    ScrollView {
        anchors.fill: parent
        visible: controller.designErrorString === ""
        contentWidth: availableWidth
        clip: true

        ColumnLayout {
            width: parent.width
            spacing: 0

            Repeater {
                objectName: "topLevelRepeater"
                /* Top-level widgets only. controller.widgetModel itself is
                 * now the full FLAT list (every widget, any nesting depth);
                 * childModel(-1) returns just the rows whose parentId is -1
                 * (top-level) — the same mechanism every other container
                 * uses for its own children, since -1 is the parentId every
                 * top-level widget shares. Binding directly to
                 * controller.widgetModel here would render every nested
                 * child a second time, outside its actual container.
                 *
                 * `controller.widgetModel.generation` is read but unused —
                 * see WidgetModel.h's `generation` doc: childModel() has no
                 * NOTIFY of its own, so without this dependency this binding
                 * would never re-evaluate after a live design-mode reload
                 * resets the model, leaving this Repeater pointing at a
                 * deleted ChildModel (blank screen after any live reload). */
                model: {
                    controller.widgetModel.generation
                    return controller.widgetModel.childModel(-1)
                }

                /* Top-level widgets are just this flat list's parentId:-1
                 * "container" — structurally no different from any other
                 * container's own children, so they're dispatched by the
                 * exact same WidgetDelegate.qml every other container
                 * (RowWidget/GridWidget/SectionWidget) already uses. This
                 * used to be a second, hand-rolled type->component dispatch
                 * table duplicating WidgetDelegate.qml's — see
                 * WidgetDelegate.qml's header comment for the bug that
                 * duplication caused (a childModel-wiring fix landing in
                 * one copy but not the other). */
                delegate: WidgetDelegate {
                    required property var model
                    Layout.fillWidth: true
                }
            }

            /* bottom padding */
            Item { Layout.fillWidth: true; height: 24 }
        }
    }
}
