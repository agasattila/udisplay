// SPDX-License-Identifier: LGPL-3.0-only
// Copyright (c) 2026 Attila Agas

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import "LicensesData.js" as LicensesData

/* Pushed onto DiscoveryScreen's local StackView. Lists third-party
 * components; tapping one pushes LicenseDetailScreen.qml (also on the same
 * local stack) showing that dependency's full license text. */
Item {
    id: root
    readonly property string pageTitle: "Licenses"
    property var pushDetail: null  // set by DiscoveryScreen: function(dependency)

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        Label {
            Layout.fillWidth: true
            Layout.margins: 16
            text: "Third-party components used by uDisplay Client:"
            wrapMode: Text.WordWrap
            font.pixelSize: 13
            color: "#888"
        }

        ListView {
            id: depList
            objectName: "licensesDependencyList"
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.leftMargin: 16
            Layout.rightMargin: 16
            clip: true
            spacing: 6
            model: LicensesData.dependencies

            delegate: ItemDelegate {
                id: delegateRoot
                required property var modelData
                width: depList.width

                background: Rectangle {
                    color: delegateRoot.hovered ? "#1e2a40" : "#16213e"
                    radius: 6
                    border.color: "#2a2a4a"
                    border.width: 1
                }

                contentItem: ColumnLayout {
                    spacing: 2
                    Label {
                        text: delegateRoot.modelData.name
                        font.pixelSize: 14
                        font.bold: true
                        color: "#e0e0e0"
                    }
                    Label {
                        text: delegateRoot.modelData.role
                        font.pixelSize: 11
                        color: "#888"
                        wrapMode: Text.WordWrap
                        Layout.fillWidth: true
                    }
                    Label {
                        text: delegateRoot.modelData.licenseName
                        font.pixelSize: 11
                        color: "#00d4aa"
                    }
                }

                onClicked: {
                    if (root.pushDetail)
                        root.pushDetail(delegateRoot.modelData)
                }
            }
        }

        Item { Layout.fillWidth: true; height: 16 }
    }
}
