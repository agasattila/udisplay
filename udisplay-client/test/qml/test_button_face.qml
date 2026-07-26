// SPDX-License-Identifier: LGPL-3.0-only
// Copyright (c) 2026 Attila Agas

import QtQuick
import "../../qml/widgets" as W

/**
 * Regression test for ButtonFace.qml — the shared fill/press-darken/shape-
 * radius/disabled-opacity primitive extracted so ButtonWidget and
 * ButtonGroupWidget items always look and behave the same (see
 * ButtonFace.qml's header comment for why).
 *
 * This suite matches the project's existing QML test convention (static
 * structural/geometry assertions run once after layout settles) rather than
 * simulating live mouse press/release — no test file in this suite uses
 * QtTest's synthetic mouse helpers, and MouseArea.pressed is read-only
 * outside that module, so "press-darken" is covered here as: unpressed fill
 * equals accentColor exactly (the only state reachable without a live
 * press), which is what every other test in this file also verifies about
 * its own dimension.
 *
 * Run headless: `qml -platform offscreen test_button_face.qml`.
 * Exits 0 on pass, 1 (with a console.error) on fail — CTest reads the exit code.
 */
Item {
    width: 500
    height: 300

    /* Records the last widgetId passed to each controller call, so signal
     * wiring (ButtonFace.buttonPressed/Released/Clicked -> onButtonPressed:
     * controller.sendButtonPress(...)) can be verified without live mouse
     * simulation -- QML signals can be invoked directly as functions
     * (face.buttonPressed()), which runs the connected handler exactly as
     * a real press would. */
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

    /* Finds the MouseArea among a ButtonFace's children by duck-typing on
     * `pressed` — MouseArea is the only child type with that property. */
    function mouseAreaOf(face) {
        var kids = toArray(face.children)
        for (var i = 0; i < kids.length; i++)
            if (kids[i].pressed !== undefined) return kids[i]
        return null
    }

    /* Finds the label Text among a ButtonFace's children by duck-typing on
     * `text` — Text is the only child type with that property. */
    function labelTextOf(face) {
        var kids = toArray(face.children)
        for (var i = 0; i < kids.length; i++)
            if (kids[i].text !== undefined) return kids[i]
        return null
    }

    W.ButtonFace {
        id: rectFace
        x: 0; y: 0
        width: 100; height: 40
        shape: "rect"
        label: "Rect"
    }
    W.ButtonFace {
        id: circleFace
        x: 0; y: 60
        width: 80; height: 80
        shape: "circle"
        label: "C"
    }
    W.ButtonFace {
        id: squareFace
        x: 0; y: 160
        width: 60; height: 60
        shape: "square"
        label: "Sq"
    }
    W.ButtonFace {
        id: disabledFace
        x: 200; y: 0
        width: 100; height: 40
        enabled: false
        label: "Off"
    }
    W.ButtonFace {
        id: noLabelFace
        x: 200; y: 60
        width: 100; height: 40
        showLabel: false
        label: "Hidden"
    }
    W.ButtonFace {
        id: emptyLabelFace
        x: 200; y: 120
        width: 100; height: 40
        showLabel: true
        label: ""
    }

    /* Plain-label ButtonWidget (props.items empty/absent) — exercises the
     * hasChildren:false branch of ButtonWidget.qml's sizing calc and
     * showLabel:!hasChildren routing, which every OTHER ButtonWidget test
     * in this suite bypasses (they all pass non-empty props.items). */
    W.ButtonWidget {
        id: plainLabelButton
        x: 350; y: 0
        widgetId: 99
        label: "Plain"
        enabled: true
        props: ({})
    }

    Timer {
        interval: 300
        running: true
        onTriggered: {
            /* Shape-driven radius formula: rect=8, circle=min(w,h)/2, square=4. */
            if (rectFace.radius !== 8)
                { fail("rect shape radius: expected 8, got " + rectFace.radius); return }
            if (circleFace.radius !== Math.min(circleFace.width, circleFace.height) / 2)
                { fail("circle shape radius: expected " + (Math.min(circleFace.width, circleFace.height) / 2) + ", got " + circleFace.radius); return }
            if (squareFace.radius !== 4)
                { fail("square shape radius: expected 4, got " + squareFace.radius); return }

            /* Unpressed fill equals accentColor exactly. */
            if (rectFace.color.toString() !== rectFace.accentColor.toString())
                { fail("unpressed fill should equal accentColor, got " + rectFace.color + " vs " + rectFace.accentColor); return }

            /* Disabled: opacity 0.3, MouseArea itself disabled. */
            if (Math.abs(disabledFace.opacity - 0.3) > 0.001)
                { fail("disabled opacity: expected 0.3, got " + disabledFace.opacity); return }
            var disabledMouseArea = mouseAreaOf(disabledFace)
            if (!disabledMouseArea) { fail("could not find disabledFace's MouseArea"); return }
            if (disabledMouseArea.enabled !== false)
                { fail("disabled ButtonFace's MouseArea should be disabled, got enabled=" + disabledMouseArea.enabled); return }

            /* Enabled (default): full opacity, MouseArea itself enabled. */
            if (Math.abs(rectFace.opacity - 1.0) > 0.001)
                { fail("enabled opacity: expected 1.0, got " + rectFace.opacity); return }
            var rectMouseArea = mouseAreaOf(rectFace)
            if (!rectMouseArea) { fail("could not find rectFace's MouseArea"); return }
            if (rectMouseArea.enabled !== true)
                { fail("enabled ButtonFace's MouseArea should be enabled, got enabled=" + rectMouseArea.enabled); return }

            /* showLabel: false hides the label even when text is set. */
            var noLabelText = labelTextOf(noLabelFace)
            if (!noLabelText) { fail("could not find noLabelFace's label Text"); return }
            if (noLabelText.visible !== false)
                { fail("showLabel:false should hide the label, but it's visible"); return }

            /* showLabel: true + non-empty label -> label Text visible, and
             * colored button_text (the exact class of stale-color-reference
             * bug d4fa3b5 fixed for button-group's overlay Label -- assert
             * it here too for ButtonFace's own internal label). */
            var rectLabelText = labelTextOf(rectFace)
            if (!rectLabelText) { fail("could not find rectFace's label Text"); return }
            if (rectLabelText.visible !== true)
                { fail("showLabel:true with a non-empty label should show the label, but it's hidden"); return }
            if (rectLabelText.color.toString() !== controller.activeStyle.button_text)
                { fail("label color should be button_text, got " + rectLabelText.color); return }

            /* showLabel: true + empty label -> label Text still hidden. */
            var emptyLabelText = labelTextOf(emptyLabelFace)
            if (!emptyLabelText) { fail("could not find emptyLabelFace's label Text"); return }
            if (emptyLabelText.visible !== false)
                { fail("showLabel:true with an empty label should still hide the label, but it's visible"); return }

            /* labelImplicitWidth/Height aliases mirror the internal Text's
             * own implicit size — ButtonWidget.qml's sizing calc depends on
             * these being accurate, not just non-zero. */
            if (rectFace.labelImplicitWidth !== rectLabelText.implicitWidth)
                { fail("labelImplicitWidth alias mismatch: face=" + rectFace.labelImplicitWidth + " text=" + rectLabelText.implicitWidth); return }
            if (rectFace.labelImplicitHeight !== rectLabelText.implicitHeight)
                { fail("labelImplicitHeight alias mismatch: face=" + rectFace.labelImplicitHeight + " text=" + rectLabelText.implicitHeight); return }

            /* Plain-label ButtonWidget (hasChildren:false branch) — sizing
             * doesn't collapse to the empty floor, and its ButtonFace shows
             * the label (showLabel:!hasChildren -> true here). */
            if (plainLabelButton.implicitWidth <= 0 || plainLabelButton.implicitHeight <= 0)
                { fail("plain-label ButtonWidget collapsed: " + plainLabelButton.implicitWidth + "x" + plainLabelButton.implicitHeight); return }
            var plainFace = filterByType(toArray(plainLabelButton.children), "ButtonFace_QML")[0]
            if (!plainFace) { fail("could not find plainLabelButton's ButtonFace"); return }
            if (plainFace.showLabel !== true)
                { fail("ButtonWidget with no props.items should set showLabel:true on its ButtonFace"); return }
            var plainLabelText = labelTextOf(plainFace)
            if (!plainLabelText || plainLabelText.visible !== true)
                { fail("plain-label ButtonWidget's label should be visible"); return }
            if (plainLabelText.text !== "Plain")
                { fail("plain-label ButtonWidget's label text mismatch, got " + plainLabelText.text); return }

            /* Signal wiring: invoking ButtonFace's signals as functions runs
             * the connected onButtonPressed/Released/Clicked handlers, same
             * as a real press would -- verifies ButtonWidget forwards
             * root.widgetId (99) to the right controller method, not just
             * that "some" argument arrives. */
            plainFace.buttonPressed()
            if (controller.lastPressId !== 99)
                { fail("ButtonWidget's onButtonPressed should call sendButtonPress(99), got " + controller.lastPressId); return }
            plainFace.buttonReleased()
            if (controller.lastReleaseId !== 99)
                { fail("ButtonWidget's onButtonReleased should call sendButtonRelease(99), got " + controller.lastReleaseId); return }
            plainFace.buttonClicked()
            if (controller.lastClickId !== 99)
                { fail("ButtonWidget's onButtonClicked should call sendButtonClick(99), got " + controller.lastClickId); return }

            console.log("PASS: shape radius formula, disabled/enabled opacity+MouseArea, unpressed fill, showLabel (hidden/visible/empty), label size aliases, label color, plain-label ButtonWidget, signal wiring all correct")
            Qt.exit(0)
        }
    }
}
