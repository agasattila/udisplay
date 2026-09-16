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

    /* Parse-warning banner (TODO-040) — non-fatal YAML issues (unlike the
     * parse-error overlay above, which blocks the whole screen) shown in
     * BOTH bootstrap (real device) and design mode: `parseWarnings`
     * previously reached no QML listener anywhere in the app — the client
     * parses device-supplied YAML directly with no schema validation, so a
     * legacy/hand-authored device YAML's issues (e.g. the deprecated
     * button `children:` key) were only ever visible in a desktop stderr
     * log, never on the embedded touchscreen actually running it. Reads
     * controller.parseWarnings directly (not just a live Connections), so
     * this banner is also correct for a screen instantiated AFTER the
     * parse already happened — see DeviceController.h's parseWarnings doc. */
    Rectangle {
        id: warningBanner
        objectName: "warningBanner"
        anchors { top: parent.top; left: parent.left; right: parent.right }
        visible: controller.parseWarnings.length > 0 && !warningBanner.dismissed
        property bool dismissed: false
        /* Re-arm on every parse (not just when visible flips true) — dismissed
         * stayed true across a subsequent parse that also produced warnings,
         * since visible never toggled to trigger a reset. parseWarningsChanged
         * fires on every parse, including one that clears to empty, so this
         * also harmlessly resets dismissed when there's nothing to show. */
        Connections {
            target: controller
            function onParseWarningsChanged() { warningBanner.dismissed = false }
        }
        height: visible ? bannerContent.implicitHeight + 16 : 0
        color: "#3a2a00"
        z: 2

        RowLayout {
            id: bannerContent
            anchors { left: parent.left; right: parent.right; verticalCenter: parent.verticalCenter;
                      leftMargin: 12; rightMargin: 12 }
            spacing: 8

            Label {
                Layout.fillWidth: true
                text: controller.parseWarnings.length === 1
                      ? "⚠ 1 YAML issue: " + controller.parseWarnings[0].message
                      : "⚠ " + controller.parseWarnings.length + " YAML issues — see log for details"
                color: "#f5a623"
                font.pixelSize: 12
                elide: Text.ElideRight
                wrapMode: Text.NoWrap
            }
            ToolButton {
                text: "✕"
                font.pixelSize: 14
                implicitWidth: 28
                implicitHeight: 28
                onClicked: warningBanner.dismissed = true
            }
        }
    }

    ScrollView {
        anchors { top: warningBanner.bottom; left: parent.left; right: parent.right; bottom: parent.bottom }
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
