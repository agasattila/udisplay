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
     * fixes the file and the overlay clears on the next successful reload.
     * Starts below the warning banner, which also lists the failed parse's
     * diagnostics and would otherwise cover this overlay's title. */
    Rectangle {
        anchors { top: warningBanner.bottom; left: parent.left; right: parent.right; bottom: parent.bottom }
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

    /* "widgetKey.field" for a parseWarnings entry; either part may be empty. */
    function diagnosticLocation(d) {
        if (d.widgetKey && d.field)
            return d.widgetKey + "." + d.field
        return d.widgetKey || d.field || ""
    }

    function diagnosticCount(severity) {
        var n = 0
        for (var i = 0; i < controller.parseWarnings.length; i++)
            if (controller.parseWarnings[i].severity === severity)
                n++
        return n
    }

    /* Parse-diagnostic banner (TODO-040) — non-fatal YAML issues (unlike the
     * parse-error overlay above, which blocks the whole screen) shown in
     * BOTH bootstrap (real device) and design mode: `parseWarnings`
     * previously reached no QML listener anywhere in the app — the client
     * parses device-supplied YAML directly with no schema validation, so a
     * legacy/hand-authored device YAML's issues (e.g. the deprecated
     * button `children:` key) were only ever visible in a desktop stderr
     * log, never on the embedded touchscreen actually running it. Reads
     * controller.parseWarnings directly (not just a live Connections), so
     * this banner is also correct for a screen instantiated AFTER the
     * parse already happened — see DeviceController.h's parseWarnings doc.
     *
     * One line only; tapping it opens warningDetails with every entry,
     * whatever its severity or source. In design mode a failed parse's
     * entries show here too, above the error overlay. */
    Rectangle {
        id: warningBanner
        objectName: "warningBanner"
        anchors { top: parent.top; left: parent.left; right: parent.right }
        visible: controller.parseWarnings.length > 0 && !warningBanner.dismissed
        property bool dismissed: false
        readonly property int errorCount: root.diagnosticCount("error")
        /* Re-arm on every parse (not just when visible flips true) — dismissed
         * stayed true across a subsequent parse that also produced warnings,
         * since visible never toggled to trigger a reset. parseWarningsChanged
         * fires on every parse, including one that clears to empty, so this
         * also harmlessly resets dismissed when there's nothing to show. */
        Connections {
            target: controller
            function onParseWarningsChanged() {
                warningBanner.dismissed = false
                if (controller.parseWarnings.length === 0)
                    warningDetails.close()
            }
        }
        height: visible ? bannerContent.implicitHeight + 16 : 0
        color: errorCount > 0 ? "#3d0f0f" : "#3a2a00"
        z: 2

        MouseArea {
            anchors.fill: parent
            onClicked: warningDetails.open()
        }

        RowLayout {
            id: bannerContent
            anchors { left: parent.left; right: parent.right; verticalCenter: parent.verticalCenter;
                      leftMargin: 12; rightMargin: 12 }
            spacing: 8

            Label {
                objectName: "warningBannerText"
                Layout.fillWidth: true
                text: {
                    var n = controller.parseWarnings.length
                    if (n === 1) {
                        var d = controller.parseWarnings[0]
                        var loc = root.diagnosticLocation(d)
                        return "⚠ " + (loc ? loc + ": " : "") + d.message
                    }
                    return "⚠ " + n + " YAML issues"
                           + (warningBanner.errorCount > 0
                              ? " (" + warningBanner.errorCount + " "
                                + (warningBanner.errorCount === 1 ? "error" : "errors") + ")"
                              : "")
                }
                color: warningBanner.errorCount > 0 ? "#ff6b6b" : "#f5a623"
                font.pixelSize: 12
                elide: Text.ElideRight
                wrapMode: Text.NoWrap
            }
            ToolButton {
                objectName: "warningDetailsButton"
                text: "Details"
                font.pixelSize: 12
                implicitHeight: 28
                onClicked: warningDetails.open()
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

    /* Every entry of controller.parseWarnings, with severity and location —
     * the only place a device's YAML issues are readable in full when no
     * console is attached. Lists whatever the controller reports, so new
     * diagnostics need no change here. */
    readonly property alias warningDetails: warningDetails
    Popup {
        id: warningDetails
        objectName: "warningDetails"
        parent: Overlay.overlay
        anchors.centerIn: parent
        width: parent ? Math.min(parent.width - 32, 520) : 0
        height: parent ? Math.min(detailsLayout.implicitHeight + topPadding + bottomPadding,
                                  parent.height - 64) : 0
        modal: true
        focus: true
        padding: 16
        background: Rectangle {
            color: controller.activeStyle.surface
            border.color: controller.activeStyle.border
            radius: 8
        }

        ColumnLayout {
            id: detailsLayout
            anchors.fill: parent
            spacing: 12

            Label {
                Layout.fillWidth: true
                text: controller.parseWarnings.length === 1
                      ? "1 YAML issue" : controller.parseWarnings.length + " YAML issues"
                font.pixelSize: 16
                font.bold: true
                color: controller.activeStyle.text_heading
            }

            ListView {
                id: detailsList
                objectName: "warningDetailsList"
                Layout.fillWidth: true
                Layout.fillHeight: true
                Layout.preferredHeight: contentHeight
                clip: true
                spacing: 10
                model: controller.parseWarnings
                boundsBehavior: Flickable.StopAtBounds

                delegate: ColumnLayout {
                    required property var modelData
                    width: ListView.view.width
                    spacing: 2

                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 8
                        Label {
                            text: modelData.severity === "error" ? "ERROR" : "WARNING"
                            font.pixelSize: 11
                            font.bold: true
                            color: modelData.severity === "error" ? "#ff6b6b" : "#f5a623"
                        }
                        Label {
                            Layout.fillWidth: true
                            text: root.diagnosticLocation(modelData)
                            font.family: "monospace"
                            font.pixelSize: 12
                            color: controller.activeStyle.text_muted
                            elide: Text.ElideMiddle
                        }
                    }
                    Label {
                        Layout.fillWidth: true
                        text: modelData.message
                        wrapMode: Text.Wrap
                        font.pixelSize: 13
                        color: controller.activeStyle.text
                    }
                }
            }

            Button {
                Layout.alignment: Qt.AlignRight
                text: "Close"
                flat: true
                onClicked: warningDetails.close()
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
