// SPDX-License-Identifier: LGPL-3.0-only
// Copyright (c) 2026 Attila Agas

import QtQuick
import QtQuick.Layouts
import "../../qml/widgets" as W

/**
 * Regression test for widget-model-redesign Increment 2: a button face can
 * now contain a nested row/grid (not just a flat leaf map), rendered via one
 * embedded compact RowWidget in ButtonWidget.qml reusing WidgetDelegate's
 * existing recursive dispatch — the same machinery top-level row/grid
 * already use (see test_row_flex_align.qml / test_grid_flex_align.qml /
 * test_row_nested_mixed_flex.qml for that machinery's own regression
 * coverage; this file only covers what's NEW: reachability from inside a
 * button face, and the `compact` propagation contract every compact leaf
 * component must honor.
 *
 * `compact` no longer means "smaller" for every leaf — only DisplayWidget
 * still varies implicitWidth/implicitHeight by compact (see its own
 * _boxHeight/_labelPixelSize etc.). Led/RgbLed/Label intentionally dropped
 * compact-driven shrinking; `compact: true` only changes their label color
 * (button_text vs text) now — see each widget's own header comment.
 *
 * Covers:
 *  - `compact` still reaches every leaf and produces a sane, non-collapsed
 *    implicitWidth/implicitHeight — not just "renders visibly" (this size-
 *    propagation mechanism has silently collapsed nested layouts to 0 twice
 *    before in this codebase's history; see WidgetDelegate.qml's
 *    Layout.preferredHeight/Width comments). DisplayWidget additionally
 *    still shrinks under compact; Led/RgbLed/Label do not (color-only).
 *  - A button face whose single item is a `grid` (columns: 1) renders both
 *    grandchildren and stacks them vertically — proving the button ->
 *    WidgetDelegate -> gridComp -> GridWidget chain actually works when
 *    reached from a button face (schema-illegal before this increment).
 *  - `align` on a grid child inside a button face is honored (previously
 *    silently dropped — there was no rendering path to drop it FROM).
 *  - An excluded interactive type (toggle) directly in a button face still
 *    parses and renders — the client stays permissive; only
 *    `udisplay-gen validate` hard-rejects it (schema-gates, client-warns).
 *
 * Run headless: `qml -platform offscreen test_button_face_nested.qml`.
 * Exits 0 on pass, 1 (with a console.error) on fail — CTest reads the exit code.
 */
Item {
    width: 500
    height: 300

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
        function sendButtonPress(id) {}
        function sendButtonRelease(id) {}
        function sendButtonClick(id) {}
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
    function findByType(items, needle) {
        for (var i = 0; i < items.length; i++)
            if (items[i].toString().indexOf(needle) === 0) return items[i]
        return null
    }
    function filterByType(items, needle) {
        var out = []
        for (var i = 0; i < items.length; i++)
            if (items[i].toString().indexOf(needle) === 0) out.push(items[i])
        return out
    }
    /* Drills from a ButtonWidget instance down to its loaded facesRow
     * (the RowWidget behind btn.children[1], the Loader). Returns null
     * (does not fail the test itself) if any hop is missing, so callers
     * can produce a specific fail() message. */
    function facesRowOf(button) {
        var btnRect = toArray(button.children)[0]
        if (!btnRect) return null
        var loader = toArray(btnRect.children)[1]
        if (!loader || loader.source === undefined) return null
        return loader.item
    }

    /* ── Section A: compact leaf sizing contract ─────────────────────── */

    W.LedWidget {
        id: ledCompact
        compact: true
        widgetId: 1; label: ""; enabled: true; value: true; props: ({})
    }
    W.LedWidget {
        id: ledNormal
        y: 60
        compact: false
        widgetId: 2; label: "Power"; enabled: true; value: true; props: ({})
    }

    W.RgbLedWidget {
        id: rgbCompact
        y: 120
        compact: true
        widgetId: 3; label: ""; enabled: true; value: 0x00ff00
    }
    W.RgbLedWidget {
        id: rgbNormal
        y: 180
        compact: false
        widgetId: 4; label: "Status"; enabled: true; value: 0x00ff00
    }

    W.DisplayWidget {
        id: displayCompact
        y: 240
        compact: true
        widgetId: 5; label: "V"; enabled: true; value: 3.3; props: ({ unit: "V", format: "%.1f" })
    }
    W.DisplayWidget {
        id: displayNormal
        y: 300
        compact: false
        widgetId: 6; label: "V"; enabled: true; value: 3.3; props: ({ unit: "V", format: "%.1f" })
    }

    W.LabelWidget {
        id: labelCompact
        y: 360
        compact: true
        props: ({ text: "Hi", style: "body" })
    }
    W.LabelWidget {
        id: labelNormal
        y: 400
        compact: false
        props: ({ text: "Hi", style: "body" })
    }

    /* ── Section C: nested grid (columns: 1) face — label + LED stacked ── */

    property var nestedGridProps: ({
        shape: "rect",
        items: [
            {
                type: "grid", widgetId: 0, label: "", enabled: true, visible: true, value: null, flex: 0, align: "",
                props: {
                    columns: 1,
                    align: "left",
                    items: [
                        { type: "label", widgetId: 0, label: "", enabled: true, visible: true, value: null, flex: 0, align: "center",
                          props: { text: "PWR", style: "caption" } },
                        { type: "led", widgetId: 0x20, label: "", enabled: true, visible: true, value: true, flex: 0, align: "",
                          props: { color: "#ff0000" } }
                    ]
                }
            }
        ]
    })
    W.ButtonWidget {
        id: nestedGridButton
        x: 300
        widgetId: 0x10
        label: "Power"
        enabled: true
        props: nestedGridProps
    }

    /* ── Section E: compact propagates through 2 Loader{source:} hops ─── */
    /* button -> row -> grid -> led. compact threading crosses TWO dynamic
     * Qt.resolvedUrl Loader boundaries here (rowComp's inner loader, then
     * gridComp's inner loader) — this is exactly the depth at which
     * WidgetDelegate.qml's own comments describe prior real bugs
     * (Loader.implicitWidth mirroring gaps), so it's worth its own
     * assertion rather than trusting 1-level coverage (Section C) to imply
     * 2-level correctness. */

    property var doubleNestedProps: ({
        shape: "rect",
        items: [
            {
                type: "row", widgetId: 0, label: "", enabled: true, visible: true, value: null, flex: 0, align: "",
                props: {
                    items: [
                        {
                            type: "grid", widgetId: 0, label: "", enabled: true, visible: true, value: null, flex: 0, align: "",
                            props: {
                                columns: 1,
                                items: [
                                    { type: "led", widgetId: 0x22, label: "", enabled: true, visible: true, value: true, flex: 0, align: "",
                                      props: { color: "#00ff00" } }
                                ]
                            }
                        }
                    ]
                }
            }
        ]
    })
    W.ButtonWidget {
        id: doubleNestedButton
        x: 300
        y: 200
        widgetId: 0x12
        label: "Deep"
        enabled: true
        props: doubleNestedProps
    }

    /* ── Section D: excluded interactive type stays permissive ──────── */

    property var toggleFaceProps: ({
        shape: "rect",
        items: [
            { type: "toggle", widgetId: 0x21, label: "", enabled: true, visible: true, value: false, flex: 0, align: "",
              props: {} }
        ]
    })
    W.ButtonWidget {
        id: toggleFaceButton
        x: 300
        y: 100
        widgetId: 0x11
        label: "Ctrl"
        enabled: true
        props: toggleFaceProps
    }

    Timer {
        interval: 500
        running: true
        onTriggered: {
            /* ── Section A ────────────────────────────────────────── */
            /* Led/RgbLed: compact no longer shrinks height (both floor to
             * Math.max(label.implicitHeight, 48) regardless of compact) —
             * only the label color differs. Still assert non-collapse and
             * that the shared height floor actually held for both. */
            if (ledCompact.implicitWidth <= 0 || ledCompact.implicitHeight <= 0)
                { fail("compact LedWidget collapsed: " + ledCompact.implicitWidth + "x" + ledCompact.implicitHeight); return }
            if (ledCompact.implicitHeight !== ledNormal.implicitHeight)
                { fail("compact LedWidget height (" + ledCompact.implicitHeight + ") should equal normal (" + ledNormal.implicitHeight + ") - compact no longer shrinks height"); return }

            if (rgbCompact.implicitWidth <= 0 || rgbCompact.implicitHeight <= 0)
                { fail("compact RgbLedWidget collapsed: " + rgbCompact.implicitWidth + "x" + rgbCompact.implicitHeight); return }
            if (rgbCompact.implicitHeight !== rgbNormal.implicitHeight)
                { fail("compact RgbLedWidget height (" + rgbCompact.implicitHeight + ") should equal normal (" + rgbNormal.implicitHeight + ") - compact no longer shrinks height"); return }

            /* DisplayWidget is the one leaf that still varies size by
             * compact (see its own _boxHeight/_labelPixelSize etc.) — this
             * assertion is unchanged. */
            if (displayCompact.implicitWidth <= 0 || displayCompact.implicitHeight <= 0)
                { fail("compact DisplayWidget collapsed: " + displayCompact.implicitWidth + "x" + displayCompact.implicitHeight); return }
            if (!(displayCompact.implicitHeight < displayNormal.implicitHeight))
                { fail("compact DisplayWidget (" + displayCompact.implicitHeight + ") should be shorter than normal (" + displayNormal.implicitHeight + ")"); return }

            /* LabelWidget: same text, same font size regardless of compact
             * ("Now only color is changed" — LabelWidget.qml's own header
             * comment) — width and height should match exactly. */
            if (labelCompact.implicitWidth <= 0 || labelCompact.implicitHeight <= 0)
                { fail("compact LabelWidget collapsed: " + labelCompact.implicitWidth + "x" + labelCompact.implicitHeight); return }
            if (labelCompact.implicitHeight !== labelNormal.implicitHeight || labelCompact.implicitWidth !== labelNormal.implicitWidth)
                { fail("compact LabelWidget (" + labelCompact.implicitWidth + "x" + labelCompact.implicitHeight +
                        ") should be identical to normal (" + labelNormal.implicitWidth + "x" + labelNormal.implicitHeight +
                        ") - compact only changes color") ; return }

            /* ── Section C ────────────────────────────────────────── */
            if (nestedGridButton.implicitWidth <= 64)
                { fail("nested grid face collapsed to the empty floor: " + nestedGridButton.implicitWidth); return }

            var btnRect = toArray(nestedGridButton.children)[0]
            if (!btnRect) { fail("could not find button's inner Rectangle"); return }
            /* ButtonWidget.qml declares btn's children in order: faceText
             * (Text), facesRowLoader (Loader), MouseArea — index 1 is the
             * face content loader. */
            var facesRowLoader = toArray(btnRect.children)[1]
            if (!facesRowLoader || facesRowLoader.source === undefined)
                { fail("expected facesRowLoader (Loader) at btn.children[1], got " + facesRowLoader); return }
            var facesRow = facesRowLoader.item
            if (!facesRow) { fail("facesRow (RowWidget) did not load"); return }
            var outerRow = toArray(facesRow.children)[0]
            if (!outerRow) { fail("could not find facesRow's internal RowLayout"); return }
            var outerDelegates = filterByType(toArray(outerRow.children), "WidgetDelegate")
            if (outerDelegates.length !== 1) { fail("expected 1 outer delegate (the grid), got " + outerDelegates.length); return }

            var gridLoader = toArray(outerDelegates[0].children)[0]
            var gridWidget = gridLoader ? gridLoader.item : null
            if (!gridWidget) { fail("nested GridWidget did not load"); return }

            var grid = toArray(gridWidget.children)[0]
            if (!grid) { fail("could not find grid's internal GridLayout"); return }
            var gridDelegates = filterByType(toArray(grid.children), "WidgetDelegate")
            if (gridDelegates.length !== 2) { fail("expected 2 grid grandchildren (label + led), got " + gridDelegates.length); return }

            var labelDelegate = gridDelegates[0], ledDelegate = gridDelegates[1]
            if (labelDelegate.width <= 0 || labelDelegate.height <= 0)
                { fail("grid label grandchild collapsed: " + labelDelegate.width + "x" + labelDelegate.height); return }
            if (ledDelegate.width <= 0 || ledDelegate.height <= 0)
                { fail("grid led grandchild collapsed: " + ledDelegate.width + "x" + ledDelegate.height); return }

            /* columns: 1 must stack vertically, not side by side. */
            var stackedHeight = labelDelegate.height + ledDelegate.height
            if (Math.abs(grid.height - stackedHeight) > 2) {
                fail("grid.height=" + grid.height + " does not match stacked height=" + stackedHeight +
                     " (label=" + labelDelegate.height + " led=" + ledDelegate.height + ") — columns:1 did not stack vertically")
                return
            }

            /* align: center on the label grandchild is honored. */
            if ((labelDelegate.Layout.alignment & Qt.AlignHCenter) === 0) {
                fail("grid label grandchild should honor align:center, got " + labelDelegate.Layout.alignment)
                return
            }

            /* ── Section E ────────────────────────────────────────── */
            var outerFace = facesRowOf(doubleNestedButton)
            if (!outerFace) { fail("doubleNestedButton's facesRow did not load"); return }
            var outerFaceRow = toArray(outerFace.children)[0]
            var outerFaceDelegates = filterByType(toArray(outerFaceRow.children), "WidgetDelegate")
            if (outerFaceDelegates.length !== 1) { fail("expected 1 delegate (the row), got " + outerFaceDelegates.length); return }

            var innerRowLoader = toArray(outerFaceDelegates[0].children)[0]
            var innerRow = innerRowLoader ? innerRowLoader.item : null
            if (!innerRow) { fail("inner row (1st Loader{source:} hop) did not load"); return }

            var innerRowLayout = toArray(innerRow.children)[0]
            var innerRowDelegates = filterByType(toArray(innerRowLayout.children), "WidgetDelegate")
            if (innerRowDelegates.length !== 1) { fail("expected 1 delegate (the grid), got " + innerRowDelegates.length); return }

            var gridLoader2 = toArray(innerRowDelegates[0].children)[0]
            var innerGrid = gridLoader2 ? gridLoader2.item : null
            if (!innerGrid) { fail("inner grid (2nd Loader{source:} hop) did not load"); return }

            var innerGridLayout = toArray(innerGrid.children)[0]
            var innerGridDelegates = filterByType(toArray(innerGridLayout.children), "WidgetDelegate")
            if (innerGridDelegates.length !== 1) { fail("expected 1 grid delegate (the led), got " + innerGridDelegates.length); return }

            var deepLed = innerGridDelegates[0].item
            if (!deepLed) { fail("deeply-nested led did not load"); return }
            if (!deepLed.compact) { fail("compact did not propagate through 2 Loader{source:} hops"); return }
            /* LedWidget no longer has a distinct compact size (see Section A
             * above) - compact propagation itself is what this section
             * covers, so just confirm it didn't collapse to 0 across 2
             * dynamic Loader{source:} hops, matching ledCompact's height
             * from Section A (Math.max(label.implicitHeight, 48)). */
            if (deepLed.implicitWidth <= 0 || deepLed.implicitHeight !== 48) {
                fail("led 2 levels deep has unexpected size, got " +
                     deepLed.implicitWidth + "x" + deepLed.implicitHeight)
                return
            }

            /* ── Section D ────────────────────────────────────────── */
            if (toggleFaceButton.implicitWidth <= 64)
                { fail("button face with excluded type (toggle) collapsed: " + toggleFaceButton.implicitWidth); return }

            console.log("PASS: compact leaf sizing, nested grid stacking, align, and excluded-type permissiveness all correct")
            Qt.exit(0)
        }
    }
}
