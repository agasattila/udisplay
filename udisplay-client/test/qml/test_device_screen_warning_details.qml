// SPDX-License-Identifier: LGPL-3.0-only
// Copyright (c) 2026 Attila Agas

import QtQuick
import QtQuick.Controls
import "../../qml" as Screens

/**
 * The parse-diagnostic banner's details popup (issue #25 / TODO-040).
 *
 * With more than one diagnostic the banner used to say "see log for
 * details", which a touchscreen device with no console can't do. Checks
 * that the banner summarises (and counts errors), that Details opens a
 * popup listing every entry with its severity and location, that a single
 * entry is shown inline with its location, and that the popup closes once
 * a later parse comes back clean.
 *
 * Run headless: `qml -platform offscreen test_device_screen_warning_details.qml`.
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
        property var parseWarnings: [
            { severity: "warning", widgetKey: "led", field: "color", message: "invalid LED color" },
            { severity: "error",   widgetKey: "bad", field: "type",  message: "unknown widget type" }
        ]
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

    /* Every Label/Text string under item, in tree order. */
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

    Screens.DeviceScreen {
        id: deviceScreen
        anchors.fill: parent
    }

    Timer {
        interval: 200
        running: true
        onTriggered: {
            var text = findByObjectName(deviceScreen, "warningBannerText")
            if (!text) {
                fail("could not find warningBannerText")
                return
            }
            if (text.text.indexOf("2 YAML issues (1 error)") < 0) {
                fail("banner should summarise 2 issues incl. 1 error, got: " + text.text)
                return
            }
            if (text.text.indexOf("see log") >= 0) {
                fail("banner must not point at a log the device may not have: " + text.text)
                return
            }
            if (deviceScreen.warningDetails.opened) {
                fail("details popup must start closed")
                return
            }

            findByObjectName(deviceScreen, "warningDetailsButton").clicked()
            checkPopup.start()
        }
    }

    Timer {
        id: checkPopup
        interval: 300
        onTriggered: {
            var popup = deviceScreen.warningDetails
            if (!popup.opened) {
                fail("Details should open the popup")
                return
            }
            var list = findByObjectName(popup.contentItem, "warningDetailsList")
            if (!list) {
                fail("could not find warningDetailsList in the popup")
                return
            }
            if (list.count !== 2) {
                fail("popup should list both diagnostics, got count=" + list.count)
                return
            }
            var first = textsUnder(list.itemAtIndex(0)).join(" | ")
            var second = textsUnder(list.itemAtIndex(1)).join(" | ")
            if (first.indexOf("WARNING") < 0 || first.indexOf("led.color") < 0
                    || first.indexOf("invalid LED color") < 0) {
                fail("first row should show severity, location and message, got: " + first)
                return
            }
            if (second.indexOf("ERROR") < 0 || second.indexOf("bad.type") < 0
                    || second.indexOf("unknown widget type") < 0) {
                fail("second row should show severity, location and message, got: " + second)
                return
            }

            // A later parse with one warning: shown inline, with its location.
            controller.parseWarnings = [
                { severity: "warning", widgetKey: "led", field: "color", message: "invalid LED color" }
            ]
            checkSingle.start()
        }
    }

    Timer {
        id: checkSingle
        interval: 200
        onTriggered: {
            var text = findByObjectName(deviceScreen, "warningBannerText")
            if (text.text !== "⚠ led.color: invalid LED color") {
                fail("a single issue should be shown inline with its location, got: " + text.text)
                return
            }
            var list = findByObjectName(deviceScreen.warningDetails.contentItem, "warningDetailsList")
            if (list.count !== 1) {
                fail("open popup should follow the new list, got count=" + list.count)
                return
            }

            // A clean parse: nothing left to show, the popup closes.
            controller.parseWarnings = []
            checkClean.start()
        }
    }

    Timer {
        id: checkClean
        interval: 400
        onTriggered: {
            if (deviceScreen.warningDetails.visible) {
                fail("popup should close once a parse comes back clean")
                return
            }
            if (findByObjectName(deviceScreen, "warningBanner").visible) {
                fail("banner should hide once a parse comes back clean")
                return
            }
            console.log("PASS: diagnostics details popup lists every entry and follows reparses")
            Qt.exit(0)
        }
    }
}
