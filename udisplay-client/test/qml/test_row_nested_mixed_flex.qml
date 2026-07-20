// SPDX-License-Identifier: LGPL-3.0-only
// Copyright (c) 2026 Attila Agas

import QtQuick
import QtQuick.Layouts
import "../../qml/widgets" as W

/**
 * Regression test for a RowWidget infinite QQuickItem::polish() loop.
 *
 * Trigger: a row with a mix of a flex>0 child and a flex:0/omitted
 * sibling, itself nested inside another row/grid.
 *
 * Root cause: the inner row's flex child computed its Layout.preferredWidth
 * "ask" as a share of the WHOLE row width, ignoring that the flex:0
 * sibling also needs its own space — the sum of both children's asks
 * could exceed the row's actual width. Since RowLayout computes its own
 * implicitWidth from children's Layout.preferredWidth, that inflated
 * (or, after a first attempted fix, exactly self-referential) ask fed
 * into the inner row's own implicitWidth. When the inner row is itself a
 * nested child, WidgetDelegate.qml's `Layout.preferredWidth:
 * item.implicitWidth` feeds that value back into how much width the
 * OUTER row gives the inner row — which becomes the inner row's new
 * width, which recomputes the same self-referential ask again. Qt's
 * layout engine detects this as "possible QQuickItem::polish() loop" and
 * never converges — confirmed via manual reproduction: this exact model
 * hung indefinitely (145k+ repeated warnings/sec, no settle) before the
 * fix in RowWidget.qml (nonFlexWidth-aware preferredWidth ask, plus a
 * purely content-based root.implicitWidth that never reads row.width or
 * any child's stretched preferredWidth).
 *
 * This is a real-world case, not a contrived edge case — reported via
 * /investigate (2026-07-13) from YAML shaped exactly like the model below
 * (a row containing a flex:1 button and a plain label).
 *
 * Run headless: `qml -platform offscreen test_row_nested_mixed_flex.qml`.
 * CTest's own TIMEOUT catches a real hang (this test does not need its
 * own watchdog timer — if the bug regresses, the process simply never
 * reaches Qt.exit() and CTest kills it after the configured timeout).
 * Exits 0 on pass, 1 (with a console.error) on fail.
 */
Item {
    width: 480
    height: 200

    QtObject {
        id: controller
        property var activeStyle: QtObject {
            property string text_heading: "#e0e0e0"
            property string text_muted:   "#888888"
            property string text:         "#c0c0c0"
            property string button:       "#00d4aa"
            property string button_text:  "#0d0d1a"
            property string line:         "#1e1e3a"
        }
        function sendButtonPress() {}
        function sendButtonRelease() {}
        function sendButtonClick() {}
    }

    /* outer row: [nested row (button1 flex:1 + label, no flex), sibling label] */
    property var outerProps: {
        "items": [
            { type: "row", widgetId: 0, label: "", enabled: true, visible: true, value: null, flex: 0, align: "",
              props: { items: [
                  { type: "button", widgetId: 0x10, label: "button1", enabled: true, visible: true, value: null, flex: 1, align: "",
                    props: { shape: "rect" } },
                  { type: "label", widgetId: 0, label: "", enabled: true, visible: true, value: null, flex: 0, align: "",
                    props: { text: "Hello, world!", style: "body" } }
              ] } },
            { type: "label", widgetId: 0, label: "", enabled: true, visible: true, value: null, flex: 0, align: "",
              props: { text: "sibling", style: "body" } }
        ]
    }

    W.RowWidget {
        id: outerRow
        anchors.left: parent.left
        anchors.right: parent.right
        props: outerProps
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

    Timer {
        interval: 500
        running: true
        onTriggered: {
            var outerLayout = findByType(toArray(outerRow.children), "QQuickRowLayout")
            if (!outerLayout) { fail("could not find outerRow's internal RowLayout"); return }
            var outerDelegates = filterByType(toArray(outerLayout.children), "WidgetDelegate")
            if (outerDelegates.length !== 2) { fail("expected 2 outer delegates, got " + outerDelegates.length); return }

            var nestedDelegate = outerDelegates[0], siblingDelegate = outerDelegates[1]
            if (nestedDelegate.width <= 0) { fail("nested row delegate has non-positive width: " + nestedDelegate.width); return }
            if (siblingDelegate.width <= 0) { fail("sibling label has non-positive width: " + siblingDelegate.width); return }

            /* The core regression: widths must sum to the outer row's
             * width, not exceed it (the historical bug had the nested
             * row's inflated implicitWidth pulling more than its fair
             * share, when it wasn't hanging outright). */
            var sum = nestedDelegate.width + siblingDelegate.width
            if (Math.abs(sum - outerRow.width) > 2) {
                fail("nested(" + nestedDelegate.width + ") + sibling(" + siblingDelegate.width +
                     ") = " + sum + " does not match outerRow.width=" + outerRow.width)
                return
            }

            var nestedLoader = toArray(nestedDelegate.children)[0]
            var nestedRowWidget = nestedLoader.item
            if (!nestedRowWidget) { fail("nested RowWidget did not load"); return }
            if (nestedRowWidget.width !== nestedDelegate.width) {
                fail("nested RowWidget width (" + nestedRowWidget.width +
                     ") does not match its delegate's allocated width (" + nestedDelegate.width + ")")
                return
            }

            var innerLayout = findByType(toArray(nestedRowWidget.children), "QQuickRowLayout")
            if (!innerLayout) { fail("could not find nested row's internal RowLayout"); return }
            var innerDelegates = filterByType(toArray(innerLayout.children), "WidgetDelegate")
            if (innerDelegates.length !== 2) { fail("expected 2 inner delegates, got " + innerDelegates.length); return }

            var buttonWidth = innerDelegates[0].width, labelWidth = innerDelegates[1].width
            if (buttonWidth <= 0 || labelWidth <= 0) {
                fail("inner children have non-positive width: button=" + buttonWidth + " label=" + labelWidth)
                return
            }
            var innerSum = buttonWidth + labelWidth
            if (Math.abs(innerSum - nestedRowWidget.width) > 2) {
                fail("inner button(" + buttonWidth + ") + label(" + labelWidth + ") = " + innerSum +
                     " does not match nested row width=" + nestedRowWidget.width)
                return
            }
            /* button1 (flex:1) must be wider than the plain label — it's
             * the only child claiming the flex pool, so it should absorb
             * essentially all the remaining space after the label's own
             * content width. */
            if (!(buttonWidth > labelWidth)) {
                fail("flex:1 button (" + buttonWidth + ") should be wider than the non-flex label (" + labelWidth + ")")
                return
            }

            console.log("PASS: no polish() loop, nested=" + nestedDelegate.width + " sibling=" + siblingDelegate.width +
                        "; inner button=" + buttonWidth + " inner label=" + labelWidth)
            Qt.exit(0)
        }
    }
}
