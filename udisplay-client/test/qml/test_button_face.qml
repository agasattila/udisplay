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

    /* Finds the MouseArea among a ButtonFace's children by duck-typing on
     * `pressed` — MouseArea is the only child type with that property. */
    function mouseAreaOf(face) {
        var kids = toArray(face.children)
        for (var i = 0; i < kids.length; i++)
            if (kids[i].pressed !== undefined) return kids[i]
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

            /* Enabled (default): full opacity. */
            if (Math.abs(rectFace.opacity - 1.0) > 0.001)
                { fail("enabled opacity: expected 1.0, got " + rectFace.opacity); return }

            /* showLabel: false hides the label even when text is set. */
            var noLabelText = null
            var kids = toArray(noLabelFace.children)
            for (var i = 0; i < kids.length; i++)
                if (kids[i].text !== undefined) noLabelText = kids[i]
            if (!noLabelText) { fail("could not find noLabelFace's label Text"); return }
            if (noLabelText.visible !== false)
                { fail("showLabel:false should hide the label, but it's visible"); return }

            console.log("PASS: shape radius formula, disabled opacity/MouseArea, unpressed fill, showLabel all correct")
            Qt.exit(0)
        }
    }
}
