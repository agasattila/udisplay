import QtQuick
import QtQuick.Layouts
import "../../qml/widgets" as W

/**
 * Regression test for the nested row/grid width-collapse bug.
 *
 * WidgetDelegate.qml wraps a nested `row`/`grid` in a Loader-of-Loader chain
 * (rowComp/gridComp, using Qt.resolvedUrl to dodge a compile-time type cycle
 * with RowWidget/GridWidget). Loader.implicitWidth is supposed to mirror the
 * loaded item's implicitWidth, but empirically (Qt 6.4.2) that mirroring
 * never updates once the inner `source:`-loaded item settles asynchronously
 * — the outer Loader's implicitWidth stays stuck at 0 even though the inner
 * item's implicitWidth correctly resolves to 200. Without a fix, a row/grid
 * nested inside another row/grid gets width 0, so its own children collapse
 * to width 0 too and render on top of each other at x=0.
 *
 * Fix: WidgetDelegate.qml reads `item.implicitWidth` directly via an explicit
 * Layout.preferredWidth binding, bypassing Loader's broken internal mirroring
 * — same pattern already used for height (Layout.preferredHeight: item.height).
 *
 * WidgetModel is a flat list now — a row's children come from
 * WidgetModel::childModel(), not a props.items array. This test builds a
 * ListModel per container and registers the inner row under a key that the
 * outer row's own fixture data references via its `row` field — see
 * FakeWidgetModel.qml's header comment for why a `controller.widgetModel`
 * stand-in is needed at all.
 *
 * Model below mirrors the reported bug's YAML: a row containing a label and a
 * nested row of 3 labels. Run headless: `qml -platform offscreen
 * test_nested_row_layout.qml`. Exits 0 on pass, 1 (with a console.error) on
 * fail — CTest reads the exit code.
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
        }
        property var widgetModel: FakeWidgetModel {}
    }

    function fail(msg) {
        console.error("FAIL: " + msg)
        Qt.exit(1)
    }

    function toArray(qmlList) {
        var out = []
        for (var i = 0; i < qmlList.length; i++)
            out.push(qmlList[i])
        return out
    }

    function findByType(items, needle) {
        for (var i = 0; i < items.length; i++)
            if (items[i].toString().indexOf(needle) === 0)
                return items[i]
        return null
    }

    function filterByType(items, needle) {
        var out = []
        for (var i = 0; i < items.length; i++)
            if (items[i].toString().indexOf(needle) === 0)
                out.push(items[i])
        return out
    }

    ListModel {
        id: innerRowItems
        property bool ready: false
        Component.onCompleted: {
            append({ widgetId: 0, type: "label", label: "", enabled: true, widgetVisible: true, value: "",
                     flex: 0, align: "", props: { text: "Label1", style: "body" } })
            append({ widgetId: 0, type: "label", label: "", enabled: true, widgetVisible: true, value: "",
                     flex: 0, align: "", props: { text: "Label2", style: "body" } })
            append({ widgetId: 0, type: "label", label: "", enabled: true, widgetVisible: true, value: "",
                     flex: 0, align: "", props: { text: "Label3", style: "body" } })
            controller.widgetModel.register("innerRow", innerRowItems)
            ready = true
        }
    }
    ListModel {
        id: outerRowItems
        property bool ready: false
        Component.onCompleted: {
            append({ widgetId: 0, type: "label", label: "", enabled: true, widgetVisible: true, value: "",
                     flex: 0, align: "", props: { text: "Hello", style: "body" } })
            append({ widgetId: 0, type: "row", label: "", enabled: true, widgetVisible: true, value: "",
                     flex: 0, align: "", row: "innerRow", props: {} })
            ready = true
        }
    }
    W.RowWidget {
        id: myrow
        anchors.left: parent.left
        anchors.right: parent.right
        childModel: (outerRowItems.ready && innerRowItems.ready) ? outerRowItems : null
    }

    Timer {
        interval: 500
        running: true
        /* Qt.exit() schedules a quit event, it does NOT halt JS execution —
         * every fail() call below MUST be followed by an explicit `return`,
         * or a later check can overwrite an earlier failure with Qt.exit(0). */
        onTriggered: {
            // myrow's RowLayout -> [outerlabel delegate, innerrow delegate]
            var myrowChildren = toArray(myrow.children)
            var outerRow = findByType(myrowChildren, "QQuickRowLayout")
            if (!outerRow) { fail("could not find myrow's internal RowLayout"); return }

            var delegates = filterByType(toArray(outerRow.children), "WidgetDelegate")
            if (delegates.length !== 2) { fail("expected 2 top-level delegates (label + row), got " + delegates.length); return }

            var innerRowDelegate = delegates[1]
            if (innerRowDelegate.width <= 0) { fail("innerrow delegate has non-positive width: " + innerRowDelegate.width); return }

            // Drill down: WidgetDelegate -> Loader -> RowWidget -> RowLayout -> 3 label delegates
            var innerLoader = toArray(innerRowDelegate.children)[0]
            var innerRowWidget = innerLoader.item
            if (!innerRowWidget) { fail("nested RowWidget did not load"); return }

            var innerLayout = findByType(toArray(innerRowWidget.children), "QQuickRowLayout")
            if (!innerLayout) { fail("could not find innerrow's internal RowLayout"); return }

            var innerDelegates = filterByType(toArray(innerLayout.children), "WidgetDelegate")
            if (innerDelegates.length !== 3) { fail("expected 3 inner label delegates, got " + innerDelegates.length); return }

            var xs = []
            var widths = []
            for (var k = 0; k < innerDelegates.length; k++) {
                xs.push(innerDelegates[k].x)
                widths.push(innerDelegates[k].width)
            }

            for (var i = 0; i < widths.length; i++) {
                if (widths[i] <= 0) { fail("inner label " + i + " has non-positive width: " + widths[i]); return }
            }
            // The core regression: all 3 must NOT sit at the same x (that's the
            // "exactly on top of each other" bug). Each must start where the
            // previous one ended.
            for (var j = 1; j < xs.length; j++) {
                if (xs[j] <= xs[j - 1]) {
                    fail("inner labels overlap: label " + (j-1) + " x=" + xs[j-1]
                        + " label " + j + " x=" + xs[j] + " (expected strictly increasing)")
                    return
                }
            }

            console.log("PASS: inner labels laid out at x=" + xs.join(","))
            Qt.exit(0)
        }
    }
}
