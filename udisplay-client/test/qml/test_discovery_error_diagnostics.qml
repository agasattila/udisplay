// SPDX-License-Identifier: LGPL-3.0-only
// Copyright (c) 2026 Attila Agas

import QtQuick
import QtQuick.Controls

/**
 * A device whose YAML is rejected tears the connection down (issue #26) and
 * the app returns to DiscoveryScreen, so DeviceScreen's diagnostics popup is
 * never reachable for it (PR #67 review, P2). The discovery error banner
 * must offer the same list:
 *
 *   - no Details button for an error without diagnostics (refused, timeout);
 *   - a Details button once the error comes with diagnostics, opening a
 *     popup that lists every entry;
 *   - the popup closes when the error state ends (a new connection starts).
 *
 * Loads the real main.qml, as the other discovery tests do.
 *
 * Run headless: `qml -platform offscreen test_discovery_error_diagnostics.qml`.
 * Exits 0 on pass, 1 (with a console.error) on fail -- CTest reads the exit code.
 */
Item {
    width: 420
    height: 720

    property var mainWindow: null

    QtObject {
        id: controller
        property string deviceName: ""
        property string state: "disconnected"
        property string designErrorString: ""
        property string errorString: ""
        property var parseWarnings: []
        property var widgetModel: []
        property var activeStyle: QtObject {
            property string background:   "#0d0d1a"
            property string surface:      "#1a1a2e"
            property string text_heading: "#ffffff"
            property string text:         "#c0c0c0"
            property string text_muted:   "#888888"
            property string border:       "#1e1e3a"
            property string line:         "#1e1e3a"
            property string accent:       "#00d4aa"
            property string button:       "#00d4aa"
            property string button_text:  "#0d0d1a"
        }
        function connectDevice() {}
        function connectDiscovered() {}
        function disconnectDevice() {}
    }

    QtObject {
        id: discoveryModel
        property int count: 0
        property bool scanning: false
        property string scanError: ""
        function startScan() {}
        function stopScan() {}
        function deviceAt(i) { return null }
    }

    function fail(msg) {
        console.error("FAIL: " + msg)
        Qt.exit(1)
    }

    function findByObjectName(item, name) {
        if (!item)
            return null
        if (item.objectName === name)
            return item
        if (!item.children)
            return null
        for (var i = 0; i < item.children.length; i++) {
            var found = findByObjectName(item.children[i], name)
            if (found)
                return found
        }
        return null
    }

    function textsUnder(item) {
        var out = []
        if (!item)
            return out
        if (item.text !== undefined && typeof item.text === "string")
            out.push(item.text)
        for (var i = 0; i < item.children.length; i++)
            out = out.concat(textsUnder(item.children[i]))
        return out
    }

    Component.onCompleted: {
        var comp = Qt.createComponent(Qt.resolvedUrl("../../qml/main.qml"))
        if (comp.status === Component.Error) {
            fail("failed to load main.qml: " + comp.errorString())
            return
        }
        mainWindow = comp.createObject(null)
        if (!mainWindow) {
            fail("failed to instantiate main.qml")
            return
        }
        // An error that carries no diagnostics.
        controller.errorString = "Connection refused"
        controller.state = "error"
    }

    Timer {
        interval: 300
        running: true
        onTriggered: {
            var banner = findByObjectName(mainWindow.contentItem, "discoveryErrorBanner")
            if (!banner || !banner.visible) {
                fail("error banner should be visible in state \"error\"")
                return
            }
            var button = findByObjectName(banner, "discoveryErrorDetailsButton")
            if (!button) {
                fail("could not find discoveryErrorDetailsButton")
                return
            }
            if (button.visible) {
                fail("Details must be hidden for an error without diagnostics")
                return
            }

            // A YAML rejection: same error state, now with diagnostics.
            controller.errorString = "YAML parse failed: unknown widget type"
            controller.parseWarnings = [
                { severity: "warning", widgetKey: "led", field: "color", message: "invalid LED color" },
                { severity: "error",   widgetKey: "bad", field: "type",  message: "unknown widget type" }
            ]
            Qt.callLater(function() {
                if (!button.visible) {
                    fail("Details should be offered once the error has diagnostics")
                    return
                }
                button.clicked()
                checkPopup.start()
            })
        }
    }

    Timer {
        id: checkPopup
        interval: 300
        onTriggered: {
            var overlay = mainWindow.contentItem.Overlay.overlay
            var popupContent = findByObjectName(overlay, "warningDetailsList")
            if (!popupContent || !popupContent.visible) {
                fail("Details should open the diagnostics popup")
                return
            }
            if (popupContent.count !== 2) {
                fail("popup should list both diagnostics, got count=" + popupContent.count)
                return
            }
            var second = textsUnder(popupContent.itemAtIndex(1)).join(" | ")
            if (second.indexOf("ERROR") < 0 || second.indexOf("bad.type") < 0
                    || second.indexOf("unknown widget type") < 0) {
                fail("second row should show severity, location and message, got: " + second)
                return
            }

            // A new attempt starts: the popup goes away.
            controller.state = "connecting"
            checkClosed.start()
        }
    }

    Timer {
        id: checkClosed
        interval: 400
        onTriggered: {
            var overlay = mainWindow.contentItem.Overlay.overlay
            var popupContent = findByObjectName(overlay, "warningDetailsList")
            if (popupContent && popupContent.visible) {
                fail("popup should close when the error state ends")
                return
            }
            console.log("PASS: discovery error banner offers the rejected YAML's diagnostics")
            Qt.exit(0)
        }
    }
}
