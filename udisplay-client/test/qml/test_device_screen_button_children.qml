// SPDX-License-Identifier: LGPL-3.0-only
// Copyright (c) 2026 Attila Agas

import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import "../../qml" as Screens

/**
 * Regression test for a top-level `button` widget's face children being
 * dropped when the button is a direct top-level widget (not nested inside a
 * row/grid). The same button *does* render its children correctly when
 * nested inside a row, because that path goes through WidgetDelegate.qml's
 * buttonComp, which passes `childModel: root._childModel`.
 *
 * DeviceScreen.qml's own top-level Repeater has a second, parallel
 * type->component dispatch (added in e5679d7, the WidgetModel-flatten
 * refactor) that forgot to wire `childModel:` for buttonComp/buttonGroupComp
 * -- unlike rowComp/gridComp/dpadComp/sectionComp in that same dispatch,
 * which all correctly bind `childModel: { controller.widgetModel.generation;
 * return controller.widgetModel.childModel(row) }`. A top-level button's
 * `childModel` therefore stayed at its `null` default, so ButtonWidget.qml's
 * `hasChildren` was always false and its face fell back to showing just the
 * label -- silently dropping any led/rgbled/display/label children declared
 * under a top-level button in YAML.
 *
 * This test registers a top-level `button` row (key -1, the parentId every
 * top-level widget shares) plus that button's own face-children row (key 10,
 * its flat-list row index) in FakeWidgetModel, then asserts the rendered
 * ButtonWidget picked up a non-null childModel and grew to accommodate its
 * led child -- exactly as ButtonWidget.qml's own test_button_face_children.qml
 * already verifies when childModel is wired directly, but here going through
 * the real DeviceScreen.qml top-level dispatch that was actually broken.
 *
 * Run headless: `qml -platform offscreen test_device_screen_button_children.qml`.
 * Exits 0 on pass, 1 (with a console.error) on fail -- CTest reads the exit code.
 */
Item {
    width: 400
    height: 300

    QtObject {
        id: controller
        property string deviceName: "Sentinel Device"
        property string state: "running"
        property string designErrorString: ""
        property var parseWarnings: []
        property var widgetModel: FakeWidgetModel {}
        property var activeStyle: QtObject {
            property string background:   "#0d0d1a"
            property string surface:      "#1a1a2e"
            property string text_heading: "#e0e0e0"
            property string text:         "#c0c0c0"
            property string text_muted:   "#888888"
            property string border:       "#1e1e3a"
            property string line:         "#1e1e3a"
            property string accent:       "#00d4aa"
            property string button:       "#00d4aa"
            property string button_text:  "#0d0d1a"
        }
        function disconnectDevice() {}
        function sendButtonPress(id) {}
        function sendButtonRelease(id) {}
        function sendButtonClick(id) {}
    }

    function fail(msg) {
        console.error("FAIL: " + msg)
        Qt.exit(1)
    }

    /* Top-level widget list: one button (row=10), matching the reported
     * repro (a top-level `power_btn` with a `power_led` face child). */
    ListModel {
        id: topLevelItems
        property bool ready: false
        Component.onCompleted: {
            append({ widgetId: 1, type: "button", label: "Power", enabled: true, widgetVisible: true, value: 0,
                     row: 10, props: { shape: "rect" } })
            ready = true
        }
    }

    /* power_btn's own face children -- keyed by its flat row index (10). */
    ListModel {
        id: buttonFaceItems
        property bool ready: false
        Component.onCompleted: {
            append({ widgetId: 2, type: "led", label: "PWR", enabled: true, widgetVisible: true, value: 1,
                     flex: 0, align: "", props: { color: "#FF0000" } })
            ready = true
        }
    }

    Component.onCompleted: {
        controller.widgetModel.register(-1, topLevelItems)
        controller.widgetModel.register(10, buttonFaceItems)
    }

    Screens.DeviceScreen {
        id: deviceScreen
        anchors.fill: parent
    }

    Timer {
        interval: 300
        running: true
        onTriggered: {
            if (!topLevelItems.ready || !buttonFaceItems.ready)
                { fail("fixture ListModels never became ready"); return }

            var repeater = findByObjectName(deviceScreen, "topLevelRepeater")
            if (!repeater)
                { fail("could not find DeviceScreen's top-level Repeater (objectName 'topLevelRepeater')"); return }
            if (repeater.count !== 1)
                { fail("expected 1 top-level widget, got " + repeater.count); return }

            var delegateItem = repeater.itemAt(0)
            var buttonWidget = delegateItem && delegateItem.item ? delegateItem.item : null
            if (!buttonWidget)
                { fail("top-level button delegate produced no item"); return }

            if (buttonWidget.childModel === null || buttonWidget.childModel === undefined)
                { fail("top-level ButtonWidget.childModel was not wired (still null/undefined)"); return }
            if (!buttonWidget.hasChildren)
                { fail("top-level ButtonWidget.hasChildren is false -- face child (led) was dropped"); return }

            console.log("PASS: top-level button childModel wired, hasChildren=" + buttonWidget.hasChildren)
            Qt.exit(0)
        }
    }

    /* Exact-match search by objectName, not structural sniffing -- a
     * button's own face renders its led child through a *nested*
     * RowWidget/WidgetDelegate Repeater (see ButtonWidget.qml's
     * facesRowLoader), so a "first Repeater found" walk risks matching that
     * inner repeater instead of DeviceScreen's actual top-level one. */
    function findByObjectName(node, name) {
        if (node.objectName === name) return node
        if (!node.children) return null
        for (var i = 0; i < node.children.length; i++) {
            var found = findByObjectName(node.children[i], name)
            if (found) return found
        }
        return null
    }
}
