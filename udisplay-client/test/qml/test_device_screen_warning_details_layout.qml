// SPDX-License-Identifier: LGPL-3.0-only
// Copyright (c) 2026 Attila Agas

import QtQuick
import QtQuick.Controls
import "../../qml" as Screens

/**
 * Layout of the diagnostics details popup with many entries on a small
 * display (PR #67 review, P4). With 30 entries in a 240x320 window:
 *
 *   - the popup fits inside the window;
 *   - the title and the Close button are inside the popup and on screen;
 *   - the list is clipped and scrollable (content taller than the view),
 *     and scrolling reaches the last entry;
 *   - Close closes the popup.
 *
 * Run headless: `qml -platform offscreen test_device_screen_warning_details_layout.qml`.
 * Exits 0 on pass, 1 (with a console.error) on fail -- CTest reads the exit code.
 */
Item {
    id: top
    width: 240
    height: 320

    readonly property int entryCount: 30

    function makeWarnings(n) {
        var out = []
        for (var i = 0; i < n; i++)
            out.push({
                severity: i % 5 === 0 ? "error" : "warning",
                widgetKey: "widget_" + i,
                field: "field_" + i,
                message: "diagnostic number " + i + " with a message long enough to wrap on a small display"
            })
        return out
    }

    QtObject {
        id: controller
        property string deviceName: "Small Device"
        property string state: "running"
        property string designErrorString: ""
        property var parseWarnings: top.makeWarnings(top.entryCount)
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
            if (title.text !== top.entryCount + " YAML issues") {
                fail("title should count every entry, got: " + title.text)
                return
            }

            if (list.count !== top.entryCount) {
                fail("list should hold every entry, got count=" + list.count)
                return
            }
            if (!list.clip) {
                fail("list must clip its content")
                return
            }
            if (list.height <= 0) {
                fail("list collapsed to height " + list.height)
                return
            }
            if (list.contentHeight <= list.height) {
                fail("30 entries should not fit a 320px display without scrolling: content="
                     + list.contentHeight + " view=" + list.height)
                return
            }
            /* The list must stay between the title and the Close button. */
            var listTop = list.mapToItem(bg, 0, 0).y
            var closeTop = close.mapToItem(bg, 0, 0).y
            var titleBottom = title.mapToItem(bg, 0, 0).y + title.height
            if (listTop < titleBottom - 0.5 || listTop + list.height > closeTop + 0.5) {
                fail("list overlaps title or Close: list " + listTop + "+" + list.height
                     + ", title bottom " + titleBottom + ", Close top " + closeTop)
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
            if (list.contentY <= 0) {
                fail("list should scroll, contentY=" + list.contentY)
                return
            }
            var last = list.itemAtIndex(top.entryCount - 1)
            if (!last) {
                fail("last entry should be instantiated after scrolling to the end")
                return
            }
            var y = last.mapToItem(list, 0, 0).y
            if (y < -0.5 || y + last.height > list.height + 0.5) {
                fail("last entry should be fully in view after scrolling, at y=" + y
                     + " h=" + last.height + " in " + list.height)
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
            console.log("PASS: details popup scrolls 30 entries on a 240x320 display, Close stays reachable")
            Qt.exit(0)
        }
    }
}
