// SPDX-License-Identifier: LGPL-3.0-only
// Copyright (c) 2026 Attila Agas

import QtQuick
import "../../qml/widgets" as W

/**
 * Regression test for ButtonGroupWidget.qml's grid items consuming
 * ButtonFace.qml (see ButtonGroupWidget.qml's header comment): items must
 * fill with the same accent color as a standalone button REGARDLESS of
 * selection state (selection is now a border + bold-label overlay only,
 * not a fill difference), and disabled opacity must match button's 0.3
 * (previously 0.35).
 *
 * Run headless: `qml -platform offscreen test_button_group_face.qml`.
 * Exits 0 on pass, 1 (with a console.error) on fail — CTest reads the exit code.
 */
Item {
    width: 500
    height: 200

    /* Records the last widgetId passed to each controller call -- lets
     * signal wiring be verified by invoking ButtonFace's signals directly
     * as functions (no live mouse simulation needed). */
    QtObject {
        id: controller
        property var activeStyle: QtObject {
            property string background:   "#0d0d1a"
            property string surface:      "#1a1a2e"
            property string text:         "#c0c0c0"
            property string text_muted:   "#888888"
            property string text_heading: "#e0e0e0"
            property string border:       "#1e1e3a"
            property string line:         "#1e1e3a"
            property string accent:       "#00d4aa"
            property string button:       "#00d4aa"
            property string button_text:  "#0d0d1a"
        }
        property int lastPressId: -1
        property int lastReleaseId: -1
        property int lastClickId: -1
        function sendButtonPress(id) { lastPressId = id }
        function sendButtonRelease(id) { lastReleaseId = id }
        function sendButtonClick(id) { lastClickId = id }
    }

    function fail(msg) {
        console.error("FAIL: " + msg)
        Qt.exit(1)
    }

    function toArray(qmlList) {
        var out = []
        for (var i = 0; i < qmlList.length; i++) out.push(qmlList[i])
        return out
    }

    function filterByType(items, needle) {
        var out = []
        for (var i = 0; i < items.length; i++)
            if (items[i].toString().indexOf(needle) === 0) out.push(items[i])
        return out
    }

    /* Finds the delegate's own overlay Label among a ButtonFace's children,
     * by TYPE NAME (not duck-typing on `.text`) — ButtonFace's own internal
     * label is a plain QtQuick `Text` (toString "QQuickText"), which also
     * has a `.text` property and would be indistinguishable from the
     * delegate's `QtQuick.Controls.Label` (toString "Label_QMLTYPE_N") if
     * matched by property presence alone. showLabel:false only hides
     * ButtonFace's internal Text, it doesn't remove it as a child. */
    function labelOf(face) {
        var labels = filterByType(toArray(face.children), "Label_QMLTYPE")
        return labels.length > 0 ? labels[0] : null
    }

    property var groupProps: ({
        layout: "grid",
        items: [
            { widgetId: 0x10, label: "Fast" },
            { widgetId: 0x11, label: "Slow" }
        ]
    })

    W.ButtonGroupWidget {
        id: group
        widgetId: 0x0f
        label: "Mode"
        enabled: true
        value: 0x10   /* first item "selected" */
        props: groupProps
    }

    W.ButtonGroupWidget {
        id: disabledGroup
        y: 100
        widgetId: 0x1f
        label: "Mode"
        enabled: false
        value: null
        props: groupProps
    }

    Timer {
        interval: 300
        running: true
        onTriggered: {
            /* Drill: root Rectangle -> Column "col" -> Flow -> Repeater's
             * instantiated ButtonFace delegates. */
            var col = group.children[0]
            var flow = null
            for (var i = 0; i < col.children.length; i++)
                if (col.children[i].toString().indexOf("QQuickFlow") === 0) { flow = col.children[i]; break }
            if (!flow) { fail("could not find grid Flow"); return }
            var faces = filterByType(toArray(flow.children), "ButtonFace_QML")
            if (faces.length !== 2) { fail("expected 2 ButtonFace items, got " + faces.length); return }

            var selected = null, unselected = null
            for (var j = 0; j < faces.length; j++) {
                if (faces[j].modelData.widgetId === 0x10) selected = faces[j]
                else unselected = faces[j]
            }
            if (!selected || !unselected) { fail("could not identify selected/unselected items"); return }

            /* Fill must be IDENTICAL for both — selection is a border/label
             * overlay only now, not a fill difference. */
            if (selected.color.toString() !== unselected.color.toString())
                { fail("selected/unselected fill should match (same as button's accent) — got " +
                       selected.color + " vs " + unselected.color); return }
            if (selected.color.toString() !== controller.activeStyle.button)
                { fail("item fill should equal activeStyle.button, got " + selected.color); return }

            /* Selection shows only via border color. */
            if (selected.border.color.toString() !== controller.activeStyle.button)
                { fail("selected item border should be activeStyle.button, got " + selected.border.color); return }
            if (unselected.border.color.toString() !== controller.activeStyle.border)
                { fail("unselected item border should be activeStyle.border, got " + unselected.border.color); return }

            /* Radius matches ButtonFace's default "rect" formula (8) — same
             * shared shape/radius model as button, replacing the old
             * hardcoded 6. */
            if (selected.radius !== 8)
                { fail("grid item radius: expected 8 (ButtonFace default), got " + selected.radius); return }

            /* Label color is button_text UNCONDITIONALLY now — fill is
             * always activeStyle.button (via ButtonFace) regardless of
             * selection, so activeStyle.text (meant for the old dark
             * "surface" fill) would be unreadable against it. Bold stays
             * the selection signal. */
            var selectedLabel = labelOf(selected), unselectedLabel = labelOf(unselected)
            if (!selectedLabel || !unselectedLabel) { fail("could not find grid item labels"); return }
            if (selectedLabel.color.toString() !== controller.activeStyle.button_text)
                { fail("selected label color should be button_text, got " + selectedLabel.color); return }
            if (unselectedLabel.color.toString() !== controller.activeStyle.button_text)
                { fail("unselected label color should be button_text (not activeStyle.text, unreadable on the accent fill), got " + unselectedLabel.color); return }
            if (selectedLabel.font.bold !== true)
                { fail("selected label should be bold"); return }
            if (unselectedLabel.font.bold !== false)
                { fail("unselected label should not be bold"); return }

            /* Disabled group: opacity 0.3 (unified with button), not the old 0.35. */
            var dCol = disabledGroup.children[0]
            var dFlow = null
            for (var k = 0; k < dCol.children.length; k++)
                if (dCol.children[k].toString().indexOf("QQuickFlow") === 0) { dFlow = dCol.children[k]; break }
            var dFaces = filterByType(toArray(dFlow.children), "ButtonFace_QML")
            if (dFaces.length !== 2) { fail("expected 2 ButtonFace items in disabled group, got " + dFaces.length); return }
            if (Math.abs(dFaces[0].opacity - 0.3) > 0.001)
                { fail("disabled button-group item opacity: expected 0.3, got " + dFaces[0].opacity); return }

            /* Signal wiring: invoking ButtonFace's signals as functions runs
             * the connected onButtonPressed/Released/Clicked handlers, same
             * as a real press would -- verifies each delegate forwards the
             * RIGHT widgetId (modelData.widgetId), not just "some" id. */
            selected.buttonPressed()
            if (controller.lastPressId !== 0x10)
                { fail("grid selected item press should forward widgetId 0x10, got " + controller.lastPressId); return }
            unselected.buttonPressed()
            if (controller.lastPressId !== 0x11)
                { fail("grid unselected item press should forward widgetId 0x11, got " + controller.lastPressId); return }
            unselected.buttonReleased()
            if (controller.lastReleaseId !== 0x11)
                { fail("grid item release should forward widgetId 0x11, got " + controller.lastReleaseId); return }
            unselected.buttonClicked()
            if (controller.lastClickId !== 0x11)
                { fail("grid item click should forward widgetId 0x11, got " + controller.lastClickId); return }

            console.log("PASS: button-group grid items share button's fill/opacity; selection is border-only; signal wiring forwards correct widgetIds")
            Qt.exit(0)
        }
    }
}
