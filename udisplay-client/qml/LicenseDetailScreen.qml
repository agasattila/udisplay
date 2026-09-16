// SPDX-License-Identifier: LGPL-3.0-only
// Copyright (c) 2026 Attila Agas

import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Layouts

/* Pushed onto DiscoveryScreen's local StackView from LicensesScreen.qml.
 * Shows one dependency's copyright notice and full, verbatim license text
 * (see LicensesData.js). */
Item {
    id: root
    required property var dependency
    readonly property string pageTitle: dependency.name

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 16
        spacing: 8

        Label {
            Layout.fillWidth: true
            text: root.dependency.licenseName
            font.pixelSize: 14
            font.bold: true
            color: "#00d4aa"
        }
        Label {
            Layout.fillWidth: true
            text: root.dependency.copyright
            font.pixelSize: 12
            color: "#888"
            wrapMode: Text.WordWrap
        }

        Button {
            objectName: "projectUrlButton"
            Layout.fillWidth: true
            text: "View project page"
            Material.background: "#16213e"
            Material.foreground: "#00d4aa"
            font.pixelSize: 13
            onClicked: {
                var opened = Qt.openUrlExternally(root.dependency.url)
                urlFeedback.visible = !opened
            }
        }
        Label {
            id: urlFeedback
            objectName: "urlFeedback"
            Layout.fillWidth: true
            visible: false
            text: "Couldn't open a browser. Visit " + root.dependency.url + " manually."
            wrapMode: Text.WordWrap
            font.pixelSize: 11
            color: "#e74c3c"
        }

        Rectangle { Layout.fillWidth: true; height: 1; color: "#2a2a4a" }

        ScrollView {
            Layout.fillWidth: true
            Layout.fillHeight: true
            contentWidth: availableWidth
            clip: true

            Label {
                width: parent.width
                text: root.dependency.licenseText
                color: "#c0c0c0"
                font.family: "monospace"
                font.pixelSize: 12
                wrapMode: Text.WordWrap
            }
        }
    }
}
