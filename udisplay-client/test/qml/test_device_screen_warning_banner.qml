// SPDX-License-Identifier: LGPL-3.0-only
// Copyright (c) 2026 Attila Agas

import QtQuick
import QtQuick.Controls
import "../../qml" as Screens

/**
 * Regression test for the parse-warning banner (TODO-040) staying dismissed
 * across a later parse that also produced warnings.
 *
 * warningBanner.dismissed used to only reset via `onVisibleChanged: if
 * (visible) dismissed = false`. Once dismissed, visible goes false and stays
 * false as long as dismissed is true -- so if the *next* parse also has
 * warnings, `visible` never flips back to true and onVisibleChanged never
 * fires, leaving the banner permanently hidden even though it should have
 * re-armed for the new diagnostics.
 *
 * Fix: reset `dismissed` from a Connections handler on
 * controller.parseWarningsChanged directly, which fires on every parse
 * (DeviceController.h/.cpp), not just when `visible` happens to toggle.
 *
 * Run headless: `qml -platform offscreen test_device_screen_warning_banner.qml`.
 * Exits 0 on pass, 1 (with a console.error) on fail -- CTest reads the exit code.
 */
Item {
    width: 400
    height: 600

    QtObject {
        id: controller
        property string deviceName: "Sentinel Device"
        property string state: "running"
        property string designErrorString: ""
        property var parseWarnings: [{ severity: "warning", widgetKey: "w1", field: "", message: "first issue" }]
        property var widgetModel: FakeWidgetModel {}
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
        function disconnectDevice() {}
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

    Screens.DeviceScreen {
        id: deviceScreen
        anchors.fill: parent
    }

    Timer {
        interval: 200
        running: true
        onTriggered: {
            var banner = findByObjectName(deviceScreen, "warningBanner")
            if (!banner) {
                fail("could not find warningBanner in DeviceScreen's content tree")
                return
            }
            if (banner.visible !== true) {
                fail("banner should be visible with an unfixed parse warning present, got visible=" + banner.visible)
                return
            }

            banner.dismissed = true
            dismissThenReparse.start()
        }
    }

    Timer {
        id: dismissThenReparse
        interval: 200
        onTriggered: {
            var banner = findByObjectName(deviceScreen, "warningBanner")
            if (banner.visible !== false) {
                fail("banner should be hidden right after dismissal, got visible=" + banner.visible)
                return
            }

            // Simulate a second parse that also produces a (different) warning.
            controller.parseWarnings = [{ severity: "warning", widgetKey: "w2", field: "", message: "second issue" }]
            checkAfterReparse.start()
        }
    }

    Timer {
        id: checkAfterReparse
        interval: 200
        onTriggered: {
            var banner = findByObjectName(deviceScreen, "warningBanner")
            if (banner.visible !== true) {
                fail("banner should re-arm and become visible again for a new parse's warnings, got visible=" + banner.visible)
                return
            }

            console.log("PASS: warning banner re-arms on a subsequent parse after dismissal")
            Qt.exit(0)
        }
    }
}
