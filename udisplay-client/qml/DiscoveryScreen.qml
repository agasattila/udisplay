// SPDX-License-Identifier: LGPL-3.0-only
// Copyright (c) 2026 Attila Agas

import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Layouts

Page {
    id: root
    title: "Connect"
    background: Rectangle { color: "#0d0d1a" }

    /* Start/stop mDNS + BLE scanning with screen visibility */
    Component.onCompleted:  discoveryModel.startScan()
    Component.onDestruction: discoveryModel.stopScan()

    /* Stop scanning while a connection attempt is in progress */
    Connections {
        target: controller
        function onStateChanged() {
            if (controller.state === "connecting" ||
                controller.state === "bootstrapping")
                discoveryModel.stopScan()
            else if (controller.state === "disconnected" ||
                     controller.state === "error")
                discoveryModel.startScan()
        }
    }

    /* ── Header bar ─────────────────────────────────────────────── */
    header: ToolBar {
        Material.background: "#1a1a2e"
        Label {
            anchors.centerIn: parent
            text: "uDisplay"
            font.pixelSize: 18
            font.bold: true
            color: "#00d4aa"
        }
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 16
        spacing: 12

        /* ── Tagline ─────────────────────────────────────────────── */
        ColumnLayout {
            Layout.alignment: Qt.AlignHCenter
            spacing: 4
            Image {
                Layout.alignment: Qt.AlignHCenter
                source: "qrc:/icons/app.png"
                sourceSize.width: 128
                sourceSize.height: 128
                width: 128
                height: 128
            }
            Label {
                Layout.alignment: Qt.AlignHCenter
                text: "Connect to Device"
                font.pixelSize: 18
                font.bold: true
                color: "#e0e0e0"
            }
        }

        /* ── Discovered devices ──────────────────────────────────── */
        ColumnLayout {
            Layout.fillWidth: true
            spacing: 4
            visible: discoveryModel.count > 0 ||
                     discoveryModel.scanning ||
                     discoveryModel.scanError.length > 0

            RowLayout {
                Layout.fillWidth: true
                Label {
                    text: "Discovered devices"
                    font.pixelSize: 13
                    font.bold: true
                    color: "#888"
                }
                BusyIndicator {
                    running: discoveryModel.scanning
                    implicitWidth: 20
                    implicitHeight: 20
                    visible: discoveryModel.scanning
                    Material.accent: "#00d4aa"
                }
                Item { Layout.fillWidth: true }
            }

            /* scan error hint */
            Label {
                Layout.fillWidth: true
                text: discoveryModel.scanError
                visible: discoveryModel.scanError.length > 0
                wrapMode: Text.WordWrap
                font.pixelSize: 12
                color: "#888"
            }

            ListView {
                id: deviceList
                Layout.fillWidth: true
                implicitHeight: Math.min(contentHeight, 220)
                model: discoveryModel
                clip: true
                spacing: 4

                delegate: ItemDelegate {
                    required property int index
                    required property int transportType
                    required property string displayName
                    required property string sourceLabel
                    required property string address
                    required property int port
                    required property int rssi

                    width: deviceList.width
                    enabled: controller.state === "disconnected" ||
                             controller.state === "error"

                    background: Rectangle {
                        color: parent.hovered ? "#1e2a40" : "#16213e"
                        radius: 6
                        border.color: parent.hovered ? "#00d4aa" : "#2a2a4a"
                        border.width: 1
                    }

                    contentItem: RowLayout {
                        spacing: 10
                        /* transport type badge */
                        Rectangle {
                            width: 8
                            height: 8
                            radius: 4
                            color: transportType === 1 ? "#3b82f6" : "#00d4aa"
                        }
                        ColumnLayout {
                            spacing: 2
                            Label {
                                text: displayName
                                font.pixelSize: 14
                                color: "#e0e0e0"
                            }
                            Label {
                                text: sourceLabel + " · " + address +
                                      (transportType === 0 ? ":" + port : "") +
                                      (rssi > -1 ? " · " + rssi + " dBm" : "")
                                font.pixelSize: 11
                                color: "#666"
                            }
                        }
                        Item { Layout.fillWidth: true }
                    }

                    onClicked: controller.connectDiscovered(
                        discoveryModel.deviceAt(index))
                }
            }
        }

        /* ── Divider ─────────────────────────────────────────────── */
        RowLayout {
            Layout.fillWidth: true
            Rectangle { Layout.fillWidth: true; height: 1; color: "#2a2a4a" }
            Label {
                text: "or connect manually"
                font.pixelSize: 11
                color: "#555"
                leftPadding: 8
                rightPadding: 8
            }
            Rectangle { Layout.fillWidth: true; height: 1; color: "#2a2a4a" }
        }

        /* ── Manual TCP entry ────────────────────────────────────── */
        ColumnLayout {
            Layout.fillWidth: true
            spacing: 10

            RowLayout {
                Layout.fillWidth: true
                spacing: 6

                /* -- Host label -- */
                Label {
                    text: "Host:"
                    color: "#555"
                    font.pixelSize: 12
                    font.capitalization: Font.AllUppercase
                    font.letterSpacing: 1
                    leftPadding: 8
                    rightPadding: 8
                }

                /* -- Host input -- */
                TextField {
                    id: hostField
                    Layout.fillWidth: true
                    implicitHeight: 40
                    padding: 8
                    topPadding: 9
                    bottomPadding: 7
                    verticalAlignment: TextInput.AlignVCenter

                    placeholderText: text.length === 0 && !hostField.activeFocus
                                     ? "IP address (e.g. 192.168.1.42)"
                                     : ""

                    text: ""
                    inputMethodHints: Qt.ImhUrlCharactersOnly
                    Material.accent: "#00d4aa"
                    color: "#e0e0e0"
                    background: Rectangle {
                        color: "#16213e"
                        radius: 6
                        border.color: hostField.activeFocus ? "#00d4aa" : "#333"
                        border.width: 1
                    }
                }
            }

            RowLayout {
                Layout.fillWidth: true
                spacing: 6

                /* -- Port label -- */
                Label {
                    text: "Port:"
                    color: "#555"
                    font.pixelSize: 12
                    font.capitalization: Font.AllUppercase
                    font.letterSpacing: 1
                    leftPadding: 8
                    rightPadding: 8
                }

                /* -- Port input -- */
                TextField {
                    id: portField
                    Layout.fillWidth: true
                    implicitHeight: 40
                    padding: 8
                    topPadding: 9
                    bottomPadding: 7
                    verticalAlignment: TextInput.AlignVCenter

                    placeholderText: text.length === 0 && !portField.activeFocus
                                     ? "Port (e.g. 5555)"
                                     : ""

                    text: ""
                    inputMethodHints: Qt.ImhDigitsOnly
                    validator: IntValidator { bottom: 1; top: 65535 }
                    Material.accent: "#00d4aa"
                    color: "#e0e0e0"
                    background: Rectangle {
                        color: "#16213e"
                        radius: 6
                        border.color: portField.activeFocus ? "#00d4aa" : "#333"
                        border.width: 1
                    }
                }
            }

            Button {
                Layout.fillWidth: true
                text: controller.state === "connecting"    ? "Connecting…"
                    : controller.state === "bootstrapping" ? "Loading UI…"
                    : "Connect"
                enabled: controller.state === "disconnected" ||
                         controller.state === "error"
                Material.background: "#00d4aa"
                Material.foreground: "#0d0d1a"
                font.bold: true
                font.pixelSize: 16
                onClicked: controller.connectDevice(
                    0 /* TCP */, hostField.text, parseInt(portField.text) || 5555)
            }
        }

        /* ── Error banner ────────────────────────────────────────── */
        Rectangle {
            Layout.fillWidth: true
            height: errorLabel.implicitHeight + 16
            color: "#2d1a1a"
            radius: 6
            border.color: "#c0392b"
            border.width: 1
            visible: controller.state === "error"

            Label {
                id: errorLabel
                anchors { fill: parent; margins: 8 }
                text: controller.errorString
                wrapMode: Text.WordWrap
                color: "#e74c3c"
                font.pixelSize: 13
            }
        }

        Item { Layout.fillHeight: true }
    }
}
