// SPDX-License-Identifier: LGPL-3.0-only
// Copyright (c) 2026 Attila Agas

import QtQuick
import QtQuick.Controls

/**
 * The discovery error banner must always be tall enough for its wrapped
 * message (PR #67 review 3, P2). Reported repro: a YAML rejection with
 * diagnostics, open and close Details, reconnect, fail the same way — the
 * banner came back too short, its bottom border across the message text.
 *
 * Drives the mock controller the way DeviceController does it:
 *   startConnection(): parseWarnings cleared, state "connecting";
 *   rejected YAML:     parseWarnings published, then setError() sets
 *                      errorString and state "error".
 * After every failure it checks that the label's whole text height fits
 * inside the banner. Runs several rounds, with and without opening the
 * popup in between, and with an error that has no diagnostics (no Details
 * button, so a wider label).
 *
 * Run headless: `qml -platform offscreen test_discovery_error_banner_height.qml`.
 * Exits 0 on pass, 1 (with a console.error) on fail -- CTest reads the exit code.
 */
Item {
    width: 420
    height: 720

    property var mainWindow: null
    property int step: 0

    readonly property string yamlError:
        "YAML parse failed: widget 'bad' has unknown type 'sparkline'; supported types are "
        + "label, button, slider, switch, gauge, led, rgbled, dropdown, dpad and text"
    readonly property var yamlDiagnostics: [
        { severity: "warning", widgetKey: "led", field: "color", message: "invalid LED color" },
        { severity: "error",   widgetKey: "bad", field: "type",  message: "unknown widget type" }
    ]

    /* Each round: [errorString, parseWarnings, open and close Details first]. */
    property var rounds: [
        [yamlError, yamlDiagnostics, true],
        [yamlError, yamlDiagnostics, true],
        [yamlError, yamlDiagnostics, false],
        ["Connection refused", [], false],
        [yamlError, yamlDiagnostics, true],
        ["Connection refused", [], false]
    ]

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

    function popupList() {
        return findByObjectName(mainWindow.contentItem.Overlay.overlay, "warningDetailsList")
    }

    /* DeviceController::startConnection(), then a rejected YAML. */
    function failConnection(round) {
        controller.parseWarnings = []
        controller.state = "connecting"
        controller.parseWarnings = round[1]
        controller.errorString = round[0]
        controller.state = "error"
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
        failConnection(rounds[0])
        checkRound.start()
    }

    /* Checks the banner of the current round, then opens Details if the
     * round asks for it, or goes straight to the next failure. */
    Timer {
        id: checkRound
        interval: 250
        onTriggered: {
            var round = rounds[step]
            var banner = findByObjectName(mainWindow.contentItem, "discoveryErrorBanner")
            var label = findByObjectName(banner, "discoveryErrorLabel")
            if (!banner || !banner.visible || !label) {
                fail("round " + step + ": error banner and its label should be visible")
                return
            }
            if (label.text !== round[0]) {
                fail("round " + step + ": label shows \"" + label.text + "\"")
                return
            }
            if (label.lineCount < 2 && round[0] === yamlError) {
                fail("round " + step + ": test message should wrap, lineCount=" + label.lineCount)
                return
            }
            /* The label must be at least as tall as its text, and the
             * label must lie inside the banner (inside the 1 px border). */
            var bottom = label.mapToItem(banner, 0, label.height).y
            var top = label.mapToItem(banner, 0, 0).y
            if (label.height + 0.5 < label.contentHeight
                    || top < banner.border.width
                    || bottom > banner.height - banner.border.width) {
                fail("round " + step + ": message does not fit in the banner: label y="
                     + top + " h=" + label.height + " contentHeight=" + label.contentHeight
                     + " lines=" + label.lineCount + ", banner h=" + banner.height)
                return
            }
            if (round[2]) {
                var button = findByObjectName(banner, "discoveryErrorDetailsButton")
                if (!button || !button.visible) {
                    fail("round " + step + ": Details should be offered")
                    return
                }
                button.clicked()
                closePopup.start()
            } else {
                nextRound()
            }
        }
    }

    Timer {
        id: closePopup
        interval: 250
        onTriggered: {
            var list = popupList()
            if (!list || !list.visible) {
                fail("round " + step + ": Details should open the popup")
                return
            }
            var close = findByObjectName(mainWindow.contentItem.Overlay.overlay, "warningDetailsClose")
            close.clicked()
            reconnect.start()
        }
    }

    Timer {
        id: reconnect
        interval: 250
        onTriggered: nextRound()
    }

    function nextRound() {
        step++
        if (step >= rounds.length) {
            console.log("PASS: discovery error banner fits its message after repeated failures")
            Qt.exit(0)
            return
        }
        failConnection(rounds[step])
        checkRound.start()
    }
}
