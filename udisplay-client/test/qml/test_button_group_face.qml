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
 * Also covers container-level style cascading for button-group items
 * (docs/designs/container-style-cascading.md, Revision): each item resolves
 * its OWN effectiveStyleFor(model.row) now, computed once per delegate
 * (ButtonGroupWidget.qml's _itemStyle) — an item with its own style: must
 * render differently from a sibling that inherits the group's style, and a
 * live style change must re-render (proves the activeStyle/generation
 * dummy-dependency reads actually work, not just that the binding compiles).
 *
 * WidgetModel is a flat list now — a button-group's items come from
 * WidgetModel::childModel(), not a props.items array (see
 * ButtonGroupWidget.qml). This test builds a ListModel and feeds it via
 * childModel: instead — see FakeWidgetModel.qml's header comment for why a
 * `controller.widgetModel` stand-in is needed at all.
 *
 * Run headless: `qml -platform offscreen test_button_group_face.qml`.
 * Exits 0 on pass, 1 (with a console.error) on fail — CTest reads the exit code.
 */
Item {
    width: 500
    height: 400

    /* Records the last widgetId passed to each controller call -- lets
     * signal wiring be verified by invoking ButtonFace's signals directly
     * as functions (no live mouse simulation needed). */
    QtObject {
        id: controller
        property var defaultStyle: QtObject {
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
        /* Reassignable (not just an alias to defaultStyle) so the live
         * setActiveStyle() test below can swap it and prove _itemStyle
         * re-evaluates. */
        property var activeStyle: defaultStyle
        function setActiveStyle(s) { activeStyle = s }

        /* Own-style / group-style stand-ins for the cascading test below —
         * deliberately distinct from activeStyle's colors so a test failure
         * that silently falls back to activeStyle is unambiguous. */
        property var ownStyle: QtObject {
            property string border:      "#ff0000"
            property string button:      "#ff0000"
            property string button_text: "#ffffff"
        }
        property var groupStyle: QtObject {
            property string border:      "#00ff00"
            property string button:      "#00ff00"
            property string button_text: "#000000"
        }

        property var widgetModel: FakeWidgetModel {}
        property int lastPressId: -1
        property int lastReleaseId: -1
        property int lastClickId: -1
        function sendButtonPress(id) { lastPressId = id }
        function sendButtonRelease(id) { lastReleaseId = id }
        function sendButtonClick(id) { lastClickId = id }

        /* Mutable per-row override for row 5, used by the generation-
         * dependency test below: lets that test change what row 5 resolves
         * to WITHOUT touching activeStyle, isolating the
         * widgetModel.generation dummy-dependency read from the activeStyle
         * one (the live-restyle scenario above only proves the latter). */
        property var _row5Override: null

        /* effectiveStyleFor(row) stand-in: the two rows used by the
         * cascading test below resolve to their designated stand-in style;
         * row 5 resolves to _row5Override once set; every other row (the
         * fill/opacity/selection test's items, and the live-setActiveStyle
         * test's item) falls back to activeStyle, matching "outside any
         * styled container follows activeStyle". */
        function effectiveStyleFor(row) {
            if (row === 2) return ownStyle       // styled item
            if (row === 3) return groupStyle     // unstyled item, inherits group
            if (row === 5 && _row5Override) return _row5Override
            return activeStyle
        }
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

    /* Drill: root Rectangle -> Column "col" -> Flow -> Repeater's
     * instantiated ButtonFace delegates. Shared by every scenario below
     * (originally inlined per-scenario; factored out once a 2nd and 3rd
     * scenario needed the identical walk). */
    function facesOf(groupWidget) {
        var col = groupWidget.children[0]
        var flow = null
        for (var i = 0; i < col.children.length; i++)
            if (col.children[i].toString().indexOf("QQuickFlow") === 0) { flow = col.children[i]; break }
        if (!flow) return []
        return filterByType(toArray(flow.children), "ButtonFace_QML")
    }

    ListModel {
        id: groupItems
        property bool ready: false
        Component.onCompleted: {
            /* row: 0/1 fall through effectiveStyleFor()'s default case
             * (activeStyle) — matches this scenario's items being unstyled. */
            append({ widgetId: 0x10, type: "button", label: "Fast", enabled: true, widgetVisible: true, value: 0,
                     flex: 0, align: "", props: { position: 0 }, row: 0 })
            append({ widgetId: 0x11, type: "button", label: "Slow", enabled: true, widgetVisible: true, value: 0,
                     flex: 0, align: "", props: { position: 1 }, row: 1 })
            ready = true
        }
    }

    W.ButtonGroupWidget {
        id: group
        widgetId: 0x0f
        label: "Mode"
        enabled: true
        value: 0x10   /* first item "selected" */
        props: ({})
        childModel: groupItems.ready ? groupItems : null
        effectiveStyle: controller.activeStyle
    }

    W.ButtonGroupWidget {
        id: disabledGroup
        y: 100
        widgetId: 0x1f
        label: "Mode"
        enabled: false
        value: null
        props: ({})
        childModel: groupItems.ready ? groupItems : null
        effectiveStyle: controller.activeStyle
    }

    /* Cascading scenario: row 2 has its own style (ownStyle), row 3 has none
     * and inherits the group's style (groupStyle) — see
     * controller.effectiveStyleFor() above. Proves ButtonGroupWidget.qml's
     * Repeater resolves each item's OWN row, not the group's single
     * effectiveStyle uniformly. */
    ListModel {
        id: cascadeItems
        property bool ready: false
        Component.onCompleted: {
            append({ widgetId: 0x20, type: "button", label: "Own", enabled: true, widgetVisible: true, value: 0,
                     flex: 0, align: "", props: { position: 0 }, row: 2 })
            append({ widgetId: 0x21, type: "button", label: "Inherit", enabled: true, widgetVisible: true, value: 0,
                     flex: 0, align: "", props: { position: 1 }, row: 3 })
            ready = true
        }
    }

    W.ButtonGroupWidget {
        id: cascadeGroup
        y: 200
        widgetId: 0x2f
        label: "Cascade"
        enabled: true
        value: null
        props: ({})
        childModel: cascadeItems.ready ? cascadeItems : null
        effectiveStyle: controller.activeStyle
    }

    /* Live-restyle scenario: a single unstyled item (row 4, falls through
     * effectiveStyleFor()'s default case) whose rendered color must change
     * after controller.setActiveStyle() — proves the activeStyle dummy
     * dependency read in _itemStyle actually re-evaluates the binding, not
     * just that it compiles and renders once. */
    ListModel {
        id: liveStyleItems
        property bool ready: false
        Component.onCompleted: {
            append({ widgetId: 0x30, type: "button", label: "Live", enabled: true, widgetVisible: true, value: 0,
                     flex: 0, align: "", props: { position: 0 }, row: 4 })
            append({ widgetId: 0x31, type: "button", label: "Live2", enabled: true, widgetVisible: true, value: 0,
                     flex: 0, align: "", props: { position: 1 }, row: 5 })
            ready = true
        }
    }

    W.ButtonGroupWidget {
        id: liveGroup
        y: 300
        widgetId: 0x3f
        label: "Live"
        enabled: true
        value: null
        props: ({})
        childModel: liveStyleItems.ready ? liveStyleItems : null
        effectiveStyle: controller.activeStyle
    }

    Timer {
        interval: 300
        running: true
        onTriggered: {
            var faces = facesOf(group)
            if (faces.length !== 2) { fail("expected 2 ButtonFace items, got " + faces.length); return }

            var selected = null, unselected = null
            for (var j = 0; j < faces.length; j++) {
                if (faces[j].model.widgetId === 0x10) selected = faces[j]
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
            var dFaces = facesOf(disabledGroup)
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

            /* Cascading: item with its own style (row 2, ownStyle) renders
             * differently from a sibling with no style (row 3, inherits
             * groupStyle) — each resolves its OWN row, not the group's
             * single effectiveStyle uniformly. Checked via border.color and
             * label color, the same two channels the group-level style
             * already used (see the "selection shows only via border color"
             * assertions above), plus FILL: each item's effective style
             * also colors its own ButtonFace chrome (Revision 2). Both items
             * are unselected here (cascadeGroup.value is null), so
             * border.color reads _itemStyle.border for each — a fallback to
             * activeStyle.border here would mean the per-item lookup isn't
             * happening. */
            var cFaces = facesOf(cascadeGroup)
            if (cFaces.length !== 2) { fail("expected 2 ButtonFace items in cascade group, got " + cFaces.length); return }
            var ownFace = null, inheritFace = null
            for (var m = 0; m < cFaces.length; m++) {
                if (cFaces[m].model.widgetId === 0x20) ownFace = cFaces[m]
                else inheritFace = cFaces[m]
            }
            if (!ownFace || !inheritFace) { fail("could not identify own/inherit cascade items"); return }
            if (ownFace.border.color.toString() !== controller.ownStyle.border)
                { fail("item with own style: should render ownStyle.border, got " + ownFace.border.color); return }
            if (inheritFace.border.color.toString() !== controller.groupStyle.border)
                { fail("item with no style: should render groupStyle.border (inherited), got " + inheritFace.border.color); return }
            if (ownFace.border.color.toString() === inheritFace.border.color.toString())
                { fail("own-style and inherited-style items should render differently, both got " + ownFace.border.color); return }
            if (ownFace.border.color.toString() === controller.activeStyle.border ||
                inheritFace.border.color.toString() === controller.activeStyle.border)
                { fail("neither cascade item should fall back to activeStyle.border — per-item lookup isn't happening"); return }
            if (ownFace.color.toString() !== controller.ownStyle.button)
                { fail("item with own style: should fill with ownStyle.button, got " + ownFace.color); return }
            if (inheritFace.color.toString() !== controller.groupStyle.button)
                { fail("item with no style: should fill with groupStyle.button (inherited), got " + inheritFace.color); return }
            var ownLabel = labelOf(ownFace), inheritLabel = labelOf(inheritFace)
            if (!ownLabel || !inheritLabel) { fail("could not find cascade item labels"); return }
            if (ownLabel.color.toString() !== controller.ownStyle.button_text)
                { fail("item with own style: label should be ownStyle.button_text, got " + ownLabel.color); return }
            if (inheritLabel.color.toString() !== controller.groupStyle.button_text)
                { fail("item with no style: label should be groupStyle.button_text (inherited), got " + inheritLabel.color); return }

            /* Live restyle: both liveGroup items start on activeStyle
             * (default). After controller.setActiveStyle() swaps it, both
             * must re-render with the NEW border color — proves
             * _itemStyle's controller.activeStyle dummy dependency read
             * actually forces re-evaluation, not just that the binding
             * compiled once. Fill must follow too (own-chrome rule). */
            var lFacesBefore = facesOf(liveGroup)
            if (lFacesBefore.length !== 2) { fail("expected 2 ButtonFace items in live group, got " + lFacesBefore.length); return }
            if (lFacesBefore[0].border.color.toString() !== controller.defaultStyle.border)
                { fail("live group item should start on defaultStyle.border, got " + lFacesBefore[0].border.color); return }

            /* text_muted included even though this scenario doesn't assert
             * on it: controller.activeStyle is shared by every group's own
             * header Label (color: effectiveStyle.text_muted, all 4 groups
             * bind effectiveStyle: controller.activeStyle) — omitting it
             * produced a real "Unable to assign [undefined] to QColor"
             * warning on every group the moment setActiveStyle() swapped
             * the shared object, caught while writing this test. */
            var newStyle = Qt.createQmlObject(
                'import QtQuick; QtObject { property string border: "#123456"; property string button: "#123456"; property string button_text: "#ffffff"; property string text_muted: "#abcdef" }',
                liveGroup, "liveRestyle")
            controller.setActiveStyle(newStyle)
            var lFacesAfter = facesOf(liveGroup)
            if (lFacesAfter[0].border.color.toString() !== "#123456")
                { fail("live group item should re-render with the new activeStyle.border after setActiveStyle(), got " + lFacesAfter[0].border.color); return }
            if (lFacesAfter[0].color.toString() !== "#123456")
                { fail("live group item fill should re-render with the new activeStyle.button after setActiveStyle(), got " + lFacesAfter[0].color); return }

            /* Generation dependency: the scenario above only proves the
             * controller.activeStyle dummy dependency read works. This
             * proves the SECOND one, controller.widgetModel.generation —
             * row 5 (liveGroup's second item) changes what
             * effectiveStyleFor() returns for it WITHOUT touching
             * activeStyle, then bumps generation directly (mirrors how the
             * real WidgetModel's generation NOTIFY fires on setWidgets()/
             * clear() — see WidgetModel.h). If _itemStyle's binding didn't
             * read generation, this item would keep rendering its stale
             * pre-bump color. */
            var lRow5Before = null
            for (var n = 0; n < lFacesAfter.length; n++)
                if (lFacesAfter[n].model.widgetId === 0x31) lRow5Before = lFacesAfter[n]
            if (!lRow5Before) { fail("could not find live group's row-5 item"); return }
            if (lRow5Before.border.color.toString() !== "#123456")
                { fail("row 5 should still be on the post-restyle activeStyle.border before the generation bump, got " + lRow5Before.border.color); return }

            controller._row5Override = Qt.createQmlObject(
                'import QtQuick; QtObject { property string border: "#7788aa"; property string button: "#7788aa"; property string button_text: "#000000" }',
                liveGroup, "genOverrideStyle")
            controller.widgetModel.generation = controller.widgetModel.generation + 1
            var lFacesAfterBump = facesOf(liveGroup)
            var lRow5After = null
            for (var p = 0; p < lFacesAfterBump.length; p++)
                if (lFacesAfterBump[p].model.widgetId === 0x31) lRow5After = lFacesAfterBump[p]
            if (!lRow5After) { fail("could not find live group's row-5 item after bump"); return }
            if (lRow5After.border.color.toString() !== "#7788aa")
                { fail("row 5 should re-render with the override color after widgetModel.generation bumps, got " +
                       lRow5After.border.color + " — the generation dummy dependency read may be missing"); return }

            console.log("PASS: button-group grid items share button's fill/opacity; selection is border-only; signal wiring forwards correct widgetIds; per-item style cascading resolves independently (border, label, fill); live setActiveStyle() and widgetModel.generation bump both re-render")
            Qt.exit(0)
        }
    }
}
