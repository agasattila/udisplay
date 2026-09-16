// SPDX-License-Identifier: LGPL-3.0-only
// Copyright (c) 2026 Attila Agas

import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import "../../qml" as Screens

/**
 * Regression test for a top-level `section` widget's children AND its
 * collapse-toggle signal, now that DeviceScreen.qml's top-level Repeater
 * dispatches through WidgetDelegate.qml (this file's sectionComp) instead of
 * its own hand-rolled dispatch table (see WidgetDelegate.qml's header
 * comment and test_device_screen_button_children.qml for the childModel bug
 * that duplication caused).
 *
 * Three things changed shape in that consolidation, all covered here (the
 * latter two added after Codex's outside-voice review of this change flagged
 * them as gaps in an earlier version of this test):
 *  - SectionWidget.qml is now loaded dynamically via Qt.resolvedUrl (like
 *    rowComp/gridComp), not a static `SectionWidget { }` reference -- which
 *    required making its `label` property non-required (see
 *    SectionWidget.qml's header comment). A regression here would surface as
 *    "Required property label was not initialized" or the section rendering
 *    with no children at all (wrong dynamic-Loader wiring). Covered by both
 *    the childModel/label wiring checks AND the actual-rendered-child height
 *    check below (checking childModel.rowCount() alone would stay green even
 *    if SectionWidget's own internal Repeater never consumed it).
 *  - `onToggleClicked: controller.widgetModel.toggleSection(row)` (a
 *    declarative signal handler) became an imperative
 *    `item.toggleClicked.connect(...)` call inside sectionComp's onLoaded
 *    (WidgetDelegate.qml) -- a regression here would surface as the toggle
 *    button doing nothing.
 *  - `item.label = Qt.binding(function() { return root._label })` etc. in
 *    sectionComp's onLoaded must be a LIVE binding, not a one-time value copy
 *    -- verified below by mutating the source ListModel's label after initial
 *    load and confirming the already-constructed SectionWidget picks up the
 *    change without needing a full childModel()/generation reset.
 *
 * Run headless: `qml -platform offscreen test_device_screen_section_children.qml`.
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

    /* Top-level widget list: one section (row=5), matching how a real
     * `section:` YAML block flattens under the widget-model-flatten refactor. */
    ListModel {
        id: topLevelItems
        property bool ready: false
        Component.onCompleted: {
            append({ widgetId: 0, type: "section", label: "Controls", enabled: true, widgetVisible: true, value: 0,
                     row: 5, props: { collapsible: true, collapsed: false } })
            ready = true
        }
    }

    /* Controls section's own children -- keyed by its flat row index (5). */
    ListModel {
        id: sectionChildren
        property bool ready: false
        Component.onCompleted: {
            append({ widgetId: 0x10, type: "label", label: "", enabled: true, widgetVisible: true, value: 0,
                     flex: 0, align: "", props: { text: "Hello", style: "body" } })
            ready = true
        }
    }

    Component.onCompleted: {
        controller.widgetModel.register(-1, topLevelItems)
        controller.widgetModel.register(5, sectionChildren)
    }

    Screens.DeviceScreen {
        id: deviceScreen
        anchors.fill: parent
    }

    function findByObjectName(node, name) {
        if (node.objectName === name) return node
        if (!node.children) return null
        for (var i = 0; i < node.children.length; i++) {
            var found = findByObjectName(node.children[i], name)
            if (found) return found
        }
        return null
    }

    function findFirstOfType(node, needle) {
        if (node.toString && node.toString().indexOf(needle) === 0) return node
        if (!node.children) return null
        for (var i = 0; i < node.children.length; i++) {
            var found = findFirstOfType(node.children[i], needle)
            if (found) return found
        }
        return null
    }

    Timer {
        interval: 300
        running: true
        onTriggered: {
            if (!topLevelItems.ready || !sectionChildren.ready)
                { fail("fixture ListModels never became ready"); return }

            var repeater = findByObjectName(deviceScreen, "topLevelRepeater")
            if (!repeater)
                { fail("could not find DeviceScreen's top-level Repeater"); return }
            if (repeater.count !== 1)
                { fail("expected 1 top-level widget, got " + repeater.count); return }

            /* sectionComp (like rowComp/gridComp) wraps SectionWidget in its
             * own inner Loader{source:...} to avoid the WidgetDelegate<->
             * SectionWidget static-type cycle (see WidgetDelegate.qml's
             * header comment) -- so delegateItem.item is that inner Loader,
             * and delegateItem.item.item is the actual SectionWidget. */
            var delegateItem = repeater.itemAt(0)
            var innerLoader = delegateItem ? delegateItem.item : null
            var sectionWidget = innerLoader ? innerLoader.item : null
            if (!sectionWidget)
                { fail("top-level section delegate produced no item (dynamic Loader failed?)"); return }

            if (sectionWidget.childModel === null || sectionWidget.childModel === undefined)
                { fail("top-level SectionWidget.childModel was not wired"); return }
            if (sectionWidget.childModel.rowCount() !== 1)
                { fail("expected 1 section child, got " + sectionWidget.childModel.rowCount()); return }
            if (sectionWidget.label !== "Controls")
                { fail("top-level SectionWidget.label was not wired, got '" + sectionWidget.label + "'"); return }

            /* Not just "childModel has 1 row" -- prove SectionWidget's own
             * internal Repeater actually instantiated a delegate for it.
             * Same threshold test_section_widget.qml itself uses: a
             * header-only section (no children rendered) caps out at 36px. */
            if (sectionWidget.implicitHeight <= 36)
                { fail("top-level SectionWidget.implicitHeight=" + sectionWidget.implicitHeight +
                       " does not include its child -- childModel was wired but not actually rendered"); return }

            sectionWidget.toggleClicked()
            if (controller.widgetModel.lastToggledRow !== 5)
                { fail("toggleClicked did not call controller.widgetModel.toggleSection(5), got " + controller.widgetModel.lastToggledRow); return }

            /* Live-binding check: mutate the source row's label directly (no
             * generation bump, no re-register) and confirm the ALREADY-
             * CONSTRUCTED SectionWidget instance picks it up. If sectionComp's
             * onLoaded ever regressed from `item.label = Qt.binding(...)` to
             * a one-time `item.label = root._label`, this would still show
             * the stale "Controls" value. */
            topLevelItems.setProperty(0, "label", "Renamed")
            if (sectionWidget.label !== "Renamed")
                { fail("SectionWidget.label did not update live after the model row changed (got '" +
                       sectionWidget.label + "') -- sectionComp's binding may have regressed to a one-time assignment"); return }

            console.log("PASS: top-level section childModel=" + sectionWidget.childModel.rowCount() +
                        " rows (rendered height=" + sectionWidget.implicitHeight + "), toggleClicked -> toggleSection(" +
                        controller.widgetModel.lastToggledRow + "), label live-updates to '" + sectionWidget.label + "'")
            Qt.exit(0)
        }
    }
}
