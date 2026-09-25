// SPDX-License-Identifier: LGPL-3.0-only
// Copyright (c) 2026 Attila Agas

import QtQuick
import "../../qml/widgets" as W

/**
 * Regression test for DpadWidget.qml: dpad has no group-level selection —
 * each cell is a regular ButtonWidget forwarding its OWN widgetId, unlike
 * button-group's old dpad delegate (see the dpad-split design doc's
 * Architecture Issue #2). Covers position lookup, the null-guard path for
 * unpopulated/corner cells (invisible + disabled, not a bespoke signal
 * guard — see DpadWidget.qml's header comment), and the 44px minimum
 * touch-target clamp (T9).
 *
 * WidgetModel is a flat list now — a dpad's items come from
 * WidgetModel::childModel(), not a props.items array (see DpadWidget.qml);
 * each item's cross-layout slot lives at props.position, not a bare
 * top-level `position` field. This test builds a ListModel and feeds it via
 * childModel: instead — see FakeWidgetModel.qml's header comment for why a
 * `controller.widgetModel` stand-in is needed at all.
 *
 * Run headless: `qml -platform offscreen test_dpad_widget.qml`.
 * Exits 0 on pass, 1 (with a console.error) on fail — CTest reads the exit code.
 */
Item {
    width: 500
    height: 300

    QtObject {
        id: controller
        property var baseStyle: QtObject {
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
        property var activeStyle: baseStyle
        property var widgetModel: FakeWidgetModel {}
        /* effectiveStyleFor(row) stand-in: row 1 (the "top" cell) carries
         * its own style; every other row falls back to activeStyle. */
        property var upStyle: QtObject {
            property string button:      "#ff8800"
            property string button_text: "#101010"
        }
        /* Opaque store for the fake resolver below. A real
         * DeviceController::effectiveStyleFor() is a C++ Q_INVOKABLE, so
         * QML's binding engine tracks nothing it reads. A JS function
         * reading notifying QML properties WOULD be tracked, which would
         * let the dummy-dependency tests pass even with the production
         * dummy reads deleted. Mutating a plain JS object's fields in
         * place emits no change signal, so reads from _opaque behave like
         * the real C++ boundary. */
        property var _opaque: ({})
        function effectiveStyleFor(row) {
            if (row === 1) return _opaque.up || upStyle
            return _opaque.active || baseStyle
        }
        function setActiveStyle(s) { _opaque.active = s; activeStyle = s }  // store first, then notify — matches C++ order
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

    /* Sparse dpad: only top/bottom populated — left/right/center are also
     * gaps here, exercising the same null-guard path as the real 4-corner
     * spacers (cell.btnItem === null). "Down" is disabled to verify
     * per-item (not group-level) enable/disable. */
    ListModel {
        id: dpadItems
        property bool ready: false
        Component.onCompleted: {
            append({ widgetId: 0x20, row: 1, type: "button", label: "Up",   enabled: true,  widgetVisible: true, value: 0,
                     flex: 0, align: "", props: { position: "top" } })
            append({ widgetId: 0x21, row: 2, type: "button", label: "Down", enabled: false, widgetVisible: true, value: 0,
                     flex: 0, align: "", props: { position: "bottom" } })
            ready = true
        }
    }

    W.DpadWidget {
        id: dpad
        childModel: dpadItems.ready ? dpadItems : null
    }

    /* Empty dpad: exercises the Math.max(..., 44) floor in isolation — with
     * no real buttons, grid.cellSize never gets bumped past its initial
     * value, so it must resolve to exactly 44 (not some other constant). */
    ListModel {
        id: emptyDpadItems
    }
    W.DpadWidget {
        id: emptyDpad
        y: 200
        childModel: emptyDpadItems
    }

    /* DpadWidget's root has exactly one child: the Grid. Its Repeater
     * instantiates exactly 9 Items (one per cross-layout cell, corners
     * included), each with exactly one child: a ButtonWidget. */
    function gridOf(dpadWidget) {
        return dpadWidget.children[0]
    }

    function cellAt(dpadWidget, index) {
        var cells = gridOf(dpadWidget).children
        return index < cells.length ? cells[index] : null
    }

    function buttonAt(dpadWidget, index) {
        var cell = cellAt(dpadWidget, index)
        return (cell && cell.children.length > 0) ? cell.children[0] : null
    }

    Timer {
        interval: 300
        running: true
        onTriggered: {
            /* Model order: ["", "top", "", "left", "center", "right", "",
             * "bottom", ""] — index 1="top" (populated, enabled), index
             * 7="bottom" (populated, disabled), all others are gaps
             * (corners AND the sparse left/center/right positions this
             * dpadProps doesn't define). */
            var topBtn = buttonAt(dpad, 1)
            if (!topBtn) { fail("could not find dpad 'top' cell's ButtonWidget"); return }
            if (topBtn.visible !== true)
                { fail("populated dpad cell ('top') should be visible"); return }
            if (topBtn.enabled !== true)
                { fail("'top' cell should be enabled (its own item.enabled is true)"); return }

            var bottomBtn = buttonAt(dpad, 7)
            if (!bottomBtn) { fail("could not find dpad 'bottom' cell's ButtonWidget"); return }
            if (bottomBtn.visible !== true)
                { fail("populated dpad cell ('bottom') should be visible"); return }
            if (bottomBtn.enabled !== false)
                { fail("'bottom' cell should be disabled (its own item.enabled is false) — dpad has no group-level enable"); return }

            /* Corner spacer (index 0) and a sparse non-corner gap (index 3,
             * "left", not in dpadProps.items): both must render an
             * invisible, disabled ButtonWidget (btnItem === null) — no
             * bespoke null-guard signal wiring needed since disabled +
             * invisible already makes them non-interactive in real use. */
            var cornerBtn = buttonAt(dpad, 0)
            if (!cornerBtn) { fail("could not find corner cell's ButtonWidget"); return }
            if (cornerBtn.visible !== false)
                { fail("corner spacer cell should be invisible (btnItem===null)"); return }
            if (cornerBtn.enabled !== false)
                { fail("corner spacer cell should be disabled (btnItem===null)"); return }

            var leftBtn = buttonAt(dpad, 3)
            if (!leftBtn) { fail("could not find 'left' cell's ButtonWidget"); return }
            if (leftBtn.visible !== false)
                { fail("unpopulated 'left' dpad cell should be invisible (btnItem===null)"); return }

            /* No special-case for "center" — same treatment as any other
             * unpopulated position. */
            var centerBtn = buttonAt(dpad, 4)
            if (!centerBtn) { fail("could not find 'center' cell's ButtonWidget"); return }
            if (centerBtn.visible !== false)
                { fail("unpopulated 'center' dpad cell should be invisible, same as any other gap"); return }

            /* Signal wiring: each cell is a real ButtonWidget forwarding
             * its OWN widgetId — drill into its ButtonFace (root.children[0])
             * and invoke its signals as functions, same technique
             * test_button_group_face.qml uses. */
            var topFace = topBtn.children[0]
            topFace.buttonPressed()
            if (controller.lastPressId !== 0x20)
                { fail("dpad 'top' cell press should forward widgetId 0x20, got " + controller.lastPressId); return }
            topFace.buttonReleased()
            if (controller.lastReleaseId !== 0x20)
                { fail("dpad 'top' cell release should forward widgetId 0x20, got " + controller.lastReleaseId); return }
            topFace.buttonClicked()
            if (controller.lastClickId !== 0x20)
                { fail("dpad 'top' cell click should forward widgetId 0x20, got " + controller.lastClickId); return }

            var bottomFace = bottomBtn.children[0]
            bottomFace.buttonPressed()
            if (controller.lastPressId !== 0x21)
                { fail("dpad 'bottom' cell press should forward widgetId 0x21, got " + controller.lastPressId); return }

            /* 44px minimum touch target (T9): the empty dpad's cellSize
             * never gets bumped by a real button's implicit size, so it
             * must resolve to exactly the floor constant, 44. */
            if (gridOf(emptyDpad).cellSize !== 44)
                { fail("empty dpad's cellSize should be exactly the 44px floor, got " + gridOf(emptyDpad).cellSize); return }

            /* The populated dpad's cells must never be smaller than 44,
             * whatever ButtonWidget's own natural size computes to. */
            if (gridOf(dpad).cellSize < 44)
                { fail("dpad cellSize must never be below the 44px touch-target floor, got " + gridOf(dpad).cellSize); return }
            if (cellAt(dpad, 1).width < 44 || cellAt(dpad, 1).height < 44)
                { fail("dpad cell dimensions must never be below 44px, got " +
                       cellAt(dpad, 1).width + "x" + cellAt(dpad, 1).height); return }

            /* Each cell's own effective style colors its own ButtonFace
             * (docs/designs/container-style-cascading.md, Revision 2): the
             * styled "top" cell fills with upStyle, "bottom" with activeStyle. */
            var topFace = topBtn.children[0], bottomFace = bottomBtn.children[0]
            if (topFace.color.toString() !== controller.upStyle.button)
                { fail("'top' cell should fill with its own effective style (" + controller.upStyle.button + "), got " + topFace.color); return }
            if (bottomFace.color.toString() !== controller.activeStyle.button)
                { fail("'bottom' cell should fill with activeStyle.button, got " + bottomFace.color); return }

            /* Live restyle: _cellStyle's controller.activeStyle dummy
             * dependency read must re-evaluate the unstyled "bottom" cell
             * (Q_INVOKABLE calls aren't tracked by the binding engine). */
            var nightStyle = Qt.createQmlObject(
                'import QtQuick; QtObject { property string button: "#123456"; property string button_text: "#ffffff" }',
                dpad, "dpadNight")
            controller.setActiveStyle(nightStyle)
            if (bottomFace.color.toString() !== "#123456")
                { fail("'bottom' cell should re-render with the new activeStyle.button after setActiveStyle(), got " + bottomFace.color); return }
            if (topFace.color.toString() !== controller.upStyle.button)
                { fail("'top' cell should keep its own style after setActiveStyle(), got " + topFace.color); return }

            /* Generation bump: row 1 changes what it resolves to without
             * touching activeStyle; only the widgetModel.generation dummy
             * read can make "top" pick that up. */
            controller._opaque.up = Qt.createQmlObject(
                'import QtQuick; QtObject { property string button: "#7788aa"; property string button_text: "#000000" }',
                dpad, "dpadUp2")
            controller.widgetModel.generation = controller.widgetModel.generation + 1
            if (topFace.color.toString() !== "#7788aa")
                { fail("'top' cell should re-render after widgetModel.generation bumps, got " + topFace.color); return }

            console.log("PASS: dpad cells forward their own widgetId (no group selection); unpopulated/corner cells are invisible+disabled; cellSize never drops below the 44px touch-target floor; per-cell effective style colors each cell's fill and re-evaluates on setActiveStyle()/generation bump")
            Qt.exit(0)
        }
    }
}
