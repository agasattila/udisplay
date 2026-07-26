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

    /* Sparse dpad: only top/bottom populated — left/right/center are also
     * corner-style gaps here, exercising the same null-guard path as the
     * real 4-corner spacers (cell.btnItem === null). */
    property var dpadProps: ({
        layout: "dpad",
        items: [
            { widgetId: 0x20, label: "Up", position: "top" },
            { widgetId: 0x21, label: "Down", position: "bottom" }
        ]
    })

    W.ButtonGroupWidget {
        id: dpadGroup
        y: 250
        widgetId: 0x2f
        label: "Dir"
        enabled: true
        value: 0x20   /* "Up" selected */
        props: dpadProps
    }

    W.ButtonGroupWidget {
        id: disabledDpadGroup
        y: 400
        widgetId: 0x3f
        label: "Dir"
        enabled: false
        value: null
        props: dpadProps
    }

    /* Finds the visible dpad Grid among a ButtonGroupWidget's Column
     * children (sibling to the grid Flow, which is hidden for dpad). */
    function dpadGridOf(groupWidget) {
        var col = groupWidget.children[0]
        for (var i = 0; i < col.children.length; i++)
            if (col.children[i].toString().indexOf("QQuickGrid") === 0) return col.children[i]
        return null
    }

    /* A dpad cell is an Item wrapping one ButtonFace (visible only when
     * btnItem !== null). Returns the ButtonFace, or null if the cell has
     * no ButtonFace child (shouldn't happen — visible:false still keeps
     * the child instantiated) or the grid has fewer than 9 cells. */
    function dpadFaceAt(grid, index) {
        var cells = toArray(grid.children)
        if (index >= cells.length) return null
        var faces = filterByType(toArray(cells[index].children), "ButtonFace_QML")
        return faces.length > 0 ? faces[0] : null
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

            /* ── dpad delegate ────────────────────────────────────────
             * Model order: ["", "top", "", "left", "center", "right", "",
             * "bottom", ""] — index 1="top" (populated+selected), index
             * 7="bottom" (populated, unselected), all others are gaps
             * (corners AND the sparse left/center/right positions this
             * dpadProps doesn't define — same null-guard path). */
            var grid = dpadGridOf(dpadGroup)
            if (!grid) { fail("could not find dpad Grid"); return }

            var topFace = dpadFaceAt(grid, 1)
            if (!topFace) { fail("could not find dpad 'top' cell's ButtonFace"); return }
            if (topFace.visible !== true)
                { fail("populated dpad cell ('top') should be visible"); return }
            if (topFace.color.toString() !== controller.activeStyle.button)
                { fail("dpad cell fill should equal activeStyle.button, got " + topFace.color); return }
            if (topFace.border.color.toString() !== controller.activeStyle.button)
                { fail("selected dpad cell border should be activeStyle.button, got " + topFace.border.color); return }

            var bottomFace = dpadFaceAt(grid, 7)
            if (!bottomFace) { fail("could not find dpad 'bottom' cell's ButtonFace"); return }
            if (bottomFace.border.color.toString() !== controller.activeStyle.border)
                { fail("unselected dpad cell border should be activeStyle.border, got " + bottomFace.border.color); return }
            /* Fill matches top's regardless of selection — same unification as grid. */
            if (bottomFace.color.toString() !== topFace.color.toString())
                { fail("dpad selected/unselected fill should match, got " + topFace.color + " vs " + bottomFace.color); return }

            /* Radius matches ButtonFace's default "rect" formula (8), same
             * as the grid delegate — replacing the old hardcoded 6. */
            if (topFace.radius !== 8)
                { fail("dpad item radius: expected 8 (ButtonFace default), got " + topFace.radius); return }

            /* Label color is button_text unconditionally, same reasoning as
             * the grid delegate. */
            var topLabel = labelOf(topFace), bottomLabel = labelOf(bottomFace)
            if (!topLabel || !bottomLabel) { fail("could not find dpad cell labels"); return }
            if (topLabel.color.toString() !== controller.activeStyle.button_text)
                { fail("selected dpad label color should be button_text, got " + topLabel.color); return }
            if (bottomLabel.color.toString() !== controller.activeStyle.button_text)
                { fail("unselected dpad label color should be button_text (not activeStyle.text), got " + bottomLabel.color); return }
            if (topLabel.font.bold !== true)
                { fail("selected dpad label should be bold"); return }
            if (bottomLabel.font.bold !== false)
                { fail("unselected dpad label should not be bold"); return }

            /* Corner spacer (index 0) and a sparse non-corner gap (index 3,
             * "left", not in dpadProps.items) both hit the null-guard path
             * (cell.btnItem === null) and must render invisible. */
            var cornerFace = dpadFaceAt(grid, 0)
            if (!cornerFace) { fail("could not find corner cell's ButtonFace"); return }
            if (cornerFace.visible !== false)
                { fail("corner spacer cell should be invisible (btnItem===null)"); return }

            var leftFace = dpadFaceAt(grid, 3)
            if (!leftFace) { fail("could not find 'left' cell's ButtonFace"); return }
            if (leftFace.visible !== false)
                { fail("unpopulated 'left' dpad cell should be invisible (btnItem===null)"); return }

            /* Disabled dpad group: opacity 0.3 (unified with button/grid),
             * on a populated cell (the null-guard path has no press
             * handlers to disable, but ButtonFace.enabled still applies). */
            var disabledGrid = dpadGridOf(disabledDpadGroup)
            var disabledTopFace = dpadFaceAt(disabledGrid, 1)
            if (!disabledTopFace) { fail("could not find disabled dpad 'top' cell's ButtonFace"); return }
            if (Math.abs(disabledTopFace.opacity - 0.3) > 0.001)
                { fail("disabled dpad cell opacity: expected 0.3, got " + disabledTopFace.opacity); return }

            /* Signal wiring: invoking ButtonFace's signals as functions runs
             * the connected onButtonPressed/Released/Clicked handlers, same
             * as a real press would -- verifies each delegate forwards the
             * RIGHT widgetId (modelData.widgetId for grid, cell.btnItem.
             * widgetId for dpad), not just "some" id. */
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

            topFace.buttonPressed()
            if (controller.lastPressId !== 0x20)
                { fail("dpad 'top' cell press should forward widgetId 0x20, got " + controller.lastPressId); return }
            bottomFace.buttonReleased()
            if (controller.lastReleaseId !== 0x21)
                { fail("dpad 'bottom' cell release should forward widgetId 0x21, got " + controller.lastReleaseId); return }
            topFace.buttonClicked()
            if (controller.lastClickId !== 0x20)
                { fail("dpad 'top' cell click should forward widgetId 0x20, got " + controller.lastClickId); return }

            /* Null-guard: a corner spacer (btnItem===null) must NOT reach
             * the controller at all when its (invisible, but still
             * instantiated) ButtonFace signal fires -- this is exactly the
             * regression a dropped `if (cell.btnItem)` guard would cause.
             * Each of press/release/click has its OWN independent
             * `if (cell.btnItem)` guard in ButtonGroupWidget.qml, so all
             * three need checking -- a guard dropped from just one handler
             * would otherwise slip through untested. */
            var pressBefore = controller.lastPressId
            cornerFace.buttonPressed()
            if (controller.lastPressId !== pressBefore)
                { fail("corner spacer cell must not forward any press (null-guard) - lastPressId changed from " + pressBefore + " to " + controller.lastPressId); return }
            var releaseBefore = controller.lastReleaseId
            cornerFace.buttonReleased()
            if (controller.lastReleaseId !== releaseBefore)
                { fail("corner spacer cell must not forward any release (null-guard) - lastReleaseId changed from " + releaseBefore + " to " + controller.lastReleaseId); return }
            var clickBefore = controller.lastClickId
            cornerFace.buttonClicked()
            if (controller.lastClickId !== clickBefore)
                { fail("corner spacer cell must not forward any click (null-guard) - lastClickId changed from " + clickBefore + " to " + controller.lastClickId); return }

            console.log("PASS: button-group grid+dpad items share button's fill/opacity; selection is border-only; null-guarded gaps render invisible; signal wiring forwards correct widgetIds and respects the null-guard")
            Qt.exit(0)
        }
    }
}
