// SPDX-License-Identifier: LGPL-3.0-only
// Copyright (c) 2026 Attila Agas

import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Layouts

/* Pushed onto DiscoveryScreen's local StackView (see DiscoveryScreen.qml's
 * header comment) — not a Page, so it has no header of its own; the title
 * shown in DiscoveryScreen's shared ToolBar comes from `pageTitle` below. */
Item {
    id: root
    readonly property string pageTitle: "About"

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 24
        spacing: 16

        Image {
            Layout.alignment: Qt.AlignHCenter
            source: "qrc:/icons/app.png"
            sourceSize.width: 96
            sourceSize.height: 96
            width: 96
            height: 96
        }

        Label {
            Layout.fillWidth: true
            Layout.alignment: Qt.AlignHCenter
            text: "uDisplay Client"
            font.pixelSize: 20
            font.bold: true
            color: "#e0e0e0"
            horizontalAlignment: Text.AlignHCenter
        }

        Label {
            Layout.fillWidth: true
            text: "An open-source framework for building remote user interfaces " +
                  "for microcontroller-based devices. This client renders and " +
                  "interacts with a device's declaratively-defined UI over a " +
                  "direct BLE or TCP connection, without requiring any cloud " +
                  "infrastructure."
            wrapMode: Text.WordWrap
            font.pixelSize: 14
            color: "#c0c0c0"
            horizontalAlignment: Text.AlignHCenter
        }

        RowLayout {
            Layout.fillWidth: true
            Layout.alignment: Qt.AlignHCenter
            spacing: 6
            Label {
                text: "Version:"
                font.pixelSize: 13
                color: "#888"
            }
            Label {
                objectName: "aboutVersionLabel"
                text: Qt.application.version
                font.pixelSize: 13
                font.bold: true
                color: "#00d4aa"
            }
        }

        Rectangle { Layout.fillWidth: true; height: 1; color: "#2a2a4a" }

        Button {
            id: githubButton
            objectName: "githubButton"
            Layout.fillWidth: true
            text: "View on GitHub"
            Material.background: "#16213e"
            Material.foreground: "#00d4aa"
            font.pixelSize: 14
            onClicked: {
                var opened = Qt.openUrlExternally("https://github.com/agasattila/udisplay")
                linkFeedback.visible = !opened
            }
        }

        Label {
            id: linkFeedback
            objectName: "linkFeedback"
            Layout.fillWidth: true
            visible: false
            text: "Couldn't open a browser. Visit github.com/agasattila/udisplay manually."
            wrapMode: Text.WordWrap
            font.pixelSize: 12
            color: "#e74c3c"
            horizontalAlignment: Text.AlignHCenter
        }

        Item { Layout.fillHeight: true }
    }
}
