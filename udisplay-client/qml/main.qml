// SPDX-License-Identifier: LGPL-3.0-only
// Copyright (c) 2026 Attila Agas

import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Layouts

ApplicationWindow {
    id: root
    visible: true
    width: 420
    height: 720
    title: controller.deviceName.length > 0
           ? controller.deviceName + " — uDisplay"
           : "uDisplay"

    Material.theme: Material.Dark
    Material.accent: "#00d4aa"
    Material.primary: "#1a1a2e"
    color: "#0d0d1a"

    StackView {
        id: stack
        anchors.fill: parent
        initialItem: controller.state === "running" ? deviceScreen : discoveryScreen
    }

    Component {
        id: discoveryScreen
        DiscoveryScreen {}
    }

    Component {
        id: deviceScreen
        DeviceScreen {}
    }

    Connections {
        target: controller
        function onStateChanged() {
            if (controller.state === "running" && stack.depth === 1)
                stack.push(deviceScreen)
            else if ((controller.state === "disconnected" ||
                      controller.state === "error") && stack.depth > 1)
                stack.pop(null)  // pop to root
        }
    }
}
