// SPDX-License-Identifier: LGPL-3.0-only
// Copyright (c) 2026 Attila Agas

import QtQuick
import QtQuick.Controls
import "../../qml" as Screens

/**
 * Layout of the diagnostics details popup with ONE very long, multi-line
 * message on a small display (PR #67 review 3, P4). The popup's height
 * comes from its layout's implicit height and the list's contentHeight, so
 * a single entry taller than the screen must still:
 *
 *   - keep the popup inside the window;
 *   - keep the title and the Close button inside the popup and on screen,
 *     with the list between them;
 *   - wrap the message to the list's width (no horizontal overflow) and
 *     keep its explicit line breaks;
 *   - scroll, so the end of the message can be brought into view;
 *   - close on Close.
 *
 * Run headless: `qml -platform offscreen test_device_screen_warning_details_long_message.qml`.
 * Exits 0 on pass, 1 (with a console.error) on fail -- CTest reads the exit code.
 */
Item {
    id: top
    width: 240
    height: 320

    readonly property string longMessage: {
        var lines = []
        for (var i = 0; i < 12; i++)
            lines.push("line " + i + ": the value of this field does not match any of the "
                       + "accepted forms and was ignored while parsing the widget")
        lines.push("END_OF_MESSAGE")
        return lines.join("\n")
    }
    readonly property var longDiagnostic: ({
        severity: "error",
        widgetKey: "a_widget_with_a_rather_long_key_name",
        field: "some_field",
        message: longMessage
    })

    QtObject {
        id: controller
        property string deviceName: "Small Device"
        property string state: "running"
        property string designErrorString: ""
        property var parseWarnings: [top.longDiagnostic]
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

    /* True if item's whole rect lies within [0,0,w,h] of `inside`. */
    function within(item, inside, what) {
        var p = item.mapToItem(inside, 0, 0)
        var ok = p.x >= -0.5 && p.y >= -0.5
                 && p.x + item.width <= inside.width + 0.5
                 && p.y + item.height <= inside.height + 0.5
        if (!ok)
            fail(what + " not fully visible: at (" + p.x + "," + p.y + ") size "
                 + item.width + "x" + item.height + " in " + inside.width + "x" + inside.height)
        return ok
    }

    Screens.DeviceScreen {
        id: deviceScreen
        anchors.fill: parent
    }

    Timer {
        interval: 200
        running: true
        onTriggered: {
            findByObjectName(deviceScreen, "warningDetailsButton").clicked()
            checkLayout.start()
        }
    }

    Timer {
        id: checkLayout
        interval: 400
        onTriggered: {
            var popup = deviceScreen.warningDetails
            if (!popup.opened) {
                fail("Details should open the popup")
                return
            }
            var bg = popup.background
            var title = findByObjectName(popup.contentItem, "warningDetailsTitle")
            var list = findByObjectName(popup.contentItem, "warningDetailsList")
            var close = findByObjectName(popup.contentItem, "warningDetailsClose")
            if (!title || !list || !close) {
                fail("could not find title/list/Close in the popup")
                return
            }

            if (!within(bg, top, "popup")) return
            if (!within(title, bg, "title")) return
            if (!within(close, bg, "Close button")) return
            if (!close.visible || close.height <= 0) {
                fail("Close button must be visible, got height=" + close.height)
                return
            }
            if (title.text !== "1 YAML issue") {
                fail("title should say 1 YAML issue, got: " + title.text)
                return
            }
            if (list.count !== 1 || list.height <= 0) {
                fail("list should hold the one entry, count=" + list.count + " h=" + list.height)
                return
            }
            var listTop = list.mapToItem(bg, 0, 0).y
            var closeTop = close.mapToItem(bg, 0, 0).y
            var titleBottom = title.mapToItem(bg, 0, 0).y + title.height
            if (listTop < titleBottom - 0.5 || listTop + list.height > closeTop + 0.5) {
                fail("list overlaps title or Close: list " + listTop + "+" + list.height
                     + ", title bottom " + titleBottom + ", Close top " + closeTop)
                return
            }

            /* The message label: wrapped to the list width, line breaks kept. */
            var entry = list.itemAtIndex(0)
            var message = null
            var stack = [entry]
            while (stack.length && !message) {
                var it = stack.pop()
                if (it.text === top.longMessage)
                    message = it
                for (var i = 0; it.children && i < it.children.length; i++)
                    stack.push(it.children[i])
            }
            if (!message) {
                fail("could not find the message label")
                return
            }
            if (message.contentWidth > list.width + 0.5 || entry.width > list.width + 0.5) {
                fail("message overflows horizontally: contentWidth=" + message.contentWidth
                     + " entry=" + entry.width + " list=" + list.width)
                return
            }
            if (message.lineCount <= 13) {
                fail("13 source lines should wrap into more on a 240px display, lineCount="
                     + message.lineCount)
                return
            }
            if (message.height + 0.5 < message.contentHeight) {
                fail("message label is shorter than its text: h=" + message.height
                     + " contentHeight=" + message.contentHeight)
                return
            }
            if (list.contentHeight <= list.height) {
                fail("the message should not fit without scrolling: content="
                     + list.contentHeight + " view=" + list.height)
                return
            }

            list.positionViewAtEnd()
            checkScrolled.start()
        }
    }

    Timer {
        id: checkScrolled
        interval: 200
        onTriggered: {
            var popup = deviceScreen.warningDetails
            var list = findByObjectName(popup.contentItem, "warningDetailsList")
            if (list.contentY <= list.originY || !list.atYEnd) {
                fail("list should scroll to the end of the message, contentY=" + list.contentY
                     + " atYEnd=" + list.atYEnd)
                return
            }
            var entry = list.itemAtIndex(0)
            var bottom = entry.mapToItem(list, 0, entry.height).y
            if (bottom > list.height + 0.5 || bottom < 0) {
                fail("end of the message should be in view, entry bottom at " + bottom
                     + " in " + list.height)
                return
            }

            findByObjectName(popup.contentItem, "warningDetailsClose").clicked()
            checkClosed.start()
        }
    }

    Timer {
        id: checkClosed
        interval: 400
        onTriggered: {
            if (deviceScreen.warningDetails.visible) {
                fail("Close should close the popup")
                return
            }
            console.log("PASS: one long multi-line diagnostic wraps and scrolls, Close stays reachable")
            Qt.exit(0)
        }
    }
}
