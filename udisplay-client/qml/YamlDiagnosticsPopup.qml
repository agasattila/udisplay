// SPDX-License-Identifier: LGPL-3.0-only
// Copyright (c) 2026 Attila Agas

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

/* Every entry of a parseWarnings list, with severity and location — the only
 * place a device's YAML issues are readable in full when no console is
 * attached (issue #25 / TODO-040). Lists whatever the controller reports, so
 * new diagnostics need no change here.
 *
 * Shared by DeviceScreen (warnings of a parse that succeeded, design-mode
 * failures) and DiscoveryScreen (the diagnostics of a bootstrap whose YAML
 * was rejected: that tears the connection down and returns to discovery,
 * so DeviceScreen is never shown for it). Colors are properties because
 * DeviceScreen follows the device's style and DiscoveryScreen has its own
 * fixed palette.
 *
 * Sized to the overlay: with many entries, or on a small display, the list
 * scrolls and the header and Close button stay on screen.
 *
 * Closes itself when the list becomes empty (a clean reparse, a new
 * connection): there is nothing left to show, whichever screen opened it. */
Popup {
    id: popup
    objectName: "warningDetails"

    property var diagnostics: []
    onDiagnosticsChanged: {
        if (!diagnostics || diagnostics.length === 0)
            close()
    }
    property color surfaceColor: "#1a1a2e"
    property color borderColor:  "#1e1e3a"
    property color headingColor: "#ffffff"
    property color textColor:    "#c0c0c0"
    property color mutedColor:   "#888888"

    /* "widgetKey.field" for a parseWarnings entry; either part may be empty. */
    function diagnosticLocation(d) {
        if (d.widgetKey && d.field)
            return d.widgetKey + "." + d.field
        return d.widgetKey || d.field || ""
    }

    parent: Overlay.overlay
    anchors.centerIn: parent
    width: parent ? Math.max(0, Math.min(parent.width - 32, 520)) : 0
    height: parent ? Math.max(0, Math.min(detailsLayout.implicitHeight + topPadding + bottomPadding,
                                          parent.height - 64)) : 0
    modal: true
    focus: true
    padding: 16
    background: Rectangle {
        color: popup.surfaceColor
        border.color: popup.borderColor
        radius: 8
    }

    ColumnLayout {
        id: detailsLayout
        anchors.fill: parent
        spacing: 12

        Label {
            objectName: "warningDetailsTitle"
            Layout.fillWidth: true
            text: popup.diagnostics.length === 1
                  ? "1 YAML issue" : popup.diagnostics.length + " YAML issues"
            font.pixelSize: 16
            font.bold: true
            color: popup.headingColor
        }

        ListView {
            id: detailsList
            objectName: "warningDetailsList"
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.preferredHeight: contentHeight
            /* Keep at least a row visible when the overlay is tiny, so the
             * list can't collapse to nothing and push Close off screen. */
            Layout.minimumHeight: Math.min(contentHeight, 40)
            clip: true
            spacing: 10
            model: popup.diagnostics
            boundsBehavior: Flickable.StopAtBounds
            ScrollIndicator.vertical: ScrollIndicator {}

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
                        text: popup.diagnosticLocation(modelData)
                        font.family: "monospace"
                        font.pixelSize: 12
                        color: popup.mutedColor
                        elide: Text.ElideMiddle
                    }
                }
                Label {
                    Layout.fillWidth: true
                    text: modelData.message
                    wrapMode: Text.Wrap
                    font.pixelSize: 13
                    color: popup.textColor
                }
            }
        }

        Button {
            objectName: "warningDetailsClose"
            Layout.alignment: Qt.AlignRight
            text: "Close"
            flat: true
            onClicked: popup.close()
        }
    }
}
