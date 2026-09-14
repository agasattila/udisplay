import QtQuick
import QtQuick.Layouts
import "../../qml/widgets" as W

/**
 * Regression + coverage test for GridWidget's per-column flex-ratio math,
 * align (container default + child override), and the WidgetDelegate.qml
 * gridComp Layout.fillWidth fix (previously missing — a grid nested inside
 * another row/grid collapsed to width 0, the same bug class rowComp already
 * had two fixes for; see test_nested_row_layout.qml for the row side).
 *
 * WidgetModel is a flat list now — a grid's children come from
 * WidgetModel::childModel(), not a props.items array. These tests build a
 * ListModel per container and feed it via childModel: instead — see
 * FakeWidgetModel.qml's header comment for why a `controller.widgetModel`
 * stand-in is needed at all, and for how a NESTED container (Grid 2's inner
 * grid) is looked up by a registered key instead of props.items[N].props.items.
 *
 * Run headless: `qml -platform offscreen test_grid_flex_align.qml`.
 * Exits 0 on pass, 1 (with a console.error) on fail — CTest reads the exit code.
 */
Item {
    width: 600
    height: 500

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

    /* ── Grid 1: 2 columns x 2 rows, per-column flex ratio + align ───────
     * Layout.fillWidth is gated on model.flex > 0 (RowWidget.qml/
     * GridWidget.qml's Repeater delegates) — a flex:0 cell never stretches,
     * even when a flex>0 sibling shares its column and pulls that column
     * wider. So within column 0, A (flex:3) stretches to its flex-driven
     * width while C (flex:0) stays at its own natural content width —
     * they do NOT share one rendered width, unlike a GridLayout with no
     * fillWidth gating at all. Column 0 (driven by A's flex:3 demand)
     * still ends up wider than column 1 (B, D both flex:0, nothing pulls
     * it wider than its own content). */
    ListModel {
        id: gridItems
        property bool ready: false
        Component.onCompleted: {
            append({ widgetId: 0, type: "label", label: "", enabled: true, widgetVisible: true, value: "",
                     flex: 3, align: "", props: { text: "A", style: "body" } })
            append({ widgetId: 0, type: "label", label: "", enabled: true, widgetVisible: true, value: "",
                     flex: 0, align: "right", props: { text: "B", style: "body" } })
            append({ widgetId: 0, type: "label", label: "", enabled: true, widgetVisible: true, value: "",
                     flex: 0, align: "", props: { text: "C", style: "body" } })
            append({ widgetId: 0, type: "label", label: "", enabled: true, widgetVisible: true, value: "",
                     flex: 0, align: "", props: { text: "D", style: "body" } })
            ready = true
        }
    }
    W.GridWidget {
        id: myGrid
        anchors.left: parent.left
        anchors.right: parent.right
        props: ({ columns: 2, align: "center" })
        childModel: gridItems.ready ? gridItems : null
    }

    /* ── Grid 2: a grid nested inside a row (gridComp fillWidth fix) ────── */
    ListModel {
        id: innerGridItems
        property bool ready: false
        Component.onCompleted: {
            append({ widgetId: 0, type: "label", label: "", enabled: true, widgetVisible: true, value: "",
                     flex: 1, align: "", props: { text: "G1", style: "body" } })
            append({ widgetId: 0, type: "label", label: "", enabled: true, widgetVisible: true, value: "",
                     flex: 1, align: "", props: { text: "G2", style: "body" } })
            controller.widgetModel.register("innerGrid", innerGridItems)
            ready = true
        }
    }
    ListModel {
        id: outerRowItems
        property bool ready: false
        Component.onCompleted: {
            append({ widgetId: 0, type: "label", label: "", enabled: true, widgetVisible: true, value: "",
                     flex: 0, align: "", props: { text: "Before", style: "body" } })
            /* row: "innerGrid" — WidgetDelegate.qml looks this delegate's own
             * children up via controller.widgetModel.childModel(model.row);
             * innerGridItems was registered under that same key above. */
            append({ widgetId: 0, type: "grid", label: "", enabled: true, widgetVisible: true, value: "",
                     flex: 1, align: "", row: "innerGrid", props: { columns: 2 } })
            ready = true
        }
    }
    W.RowWidget {
        id: outerRow
        y: 150
        anchors.left: parent.left
        anchors.right: parent.right
        childModel: (outerRowItems.ready && innerGridItems.ready) ? outerRowItems : null
    }

    Timer {
        interval: 500
        running: true
        onTriggered: {
            /* ── Per-column flex ratio ───────────────────────────────── */
            var gridLayout = findByType(toArray(myGrid.children), "QQuickGridLayout")
            if (!gridLayout) { fail("could not find myGrid's internal GridLayout"); return }
            var delegates = filterByType(toArray(gridLayout.children), "WidgetDelegate")
            if (delegates.length !== 4) { fail("expected 4 delegates, got " + delegates.length); return }

            /* Auto-flow order: A=col0/row0, B=col1/row0, C=col0/row1, D=col1/row1 */
            var wA = delegates[0].width, wB = delegates[1].width
            var wC = delegates[2].width, wD = delegates[3].width
            if (wA <= 0 || wB <= 0 || wC <= 0 || wD <= 0) { fail("non-positive width: " + [wA, wB, wC, wD]); return }
            /* A (flex:3) stretches into column 0's flex-driven width; C
             * (flex:0) does not — flex:0 cells never fill their container
             * or shared column, by design. */
            if (!(wA > wC)) { fail("A (flex:3, width=" + wA + ") should be wider than C (flex:0, width=" + wC + ") in the same column"); return }
            /* B and D are both flex:0 with identical single-letter content
             * (same font/style) — approximately equal natural width, not
             * because GridLayout unifies the column (a small delta is real:
             * text layout metrics aren't pixel-identical across two
             * separate Text elements even with matching content/font). */
            if (Math.abs(wB - wD) > 2) { fail("column 1 (B=" + wB + ", D=" + wD + ") should have approximately equal natural width (same content)"); return }
            /* Column 0 has a flex:3 item (A) pulling extra width; column 1
             * has none (B, D both flex:0) — column 0 must end up wider. */
            if (!(wA > wB)) { fail("column 0 (flex:3 present, width=" + wA + ") should be wider than column 1 (all flex:0, width=" + wB + ")"); return }

            /* ── Align: container default (center) + child override (right) ── */
            if ((delegates[0].Layout.alignment & Qt.AlignHCenter) === 0) {
                fail("A should inherit grid's align:center, got " + delegates[0].Layout.alignment)
                return
            }
            if ((delegates[1].Layout.alignment & Qt.AlignRight) === 0) {
                fail("B should use its own align:right override, got " + delegates[1].Layout.alignment)
                return
            }

            /* ── Nested grid-in-row (gridComp Layout.fillWidth fix) ─────── */
            var outerLayout = findByType(toArray(outerRow.children), "QQuickRowLayout")
            if (!outerLayout) { fail("could not find outerRow's internal RowLayout"); return }
            var outerDelegates = filterByType(toArray(outerLayout.children), "WidgetDelegate")
            if (outerDelegates.length !== 2) { fail("expected 2 outer delegates, got " + outerDelegates.length); return }

            var gridDelegate = outerDelegates[1]
            if (gridDelegate.width <= 0) { fail("nested grid delegate has non-positive width: " + gridDelegate.width); return }

            var gridLoader = toArray(gridDelegate.children)[0]
            var innerGridWidget = gridLoader.item
            if (!innerGridWidget) { fail("nested GridWidget did not load"); return }
            if (innerGridWidget.width <= 0) {
                fail("nested GridWidget itself has non-positive width: " + innerGridWidget.width +
                     " (this is the gridComp Layout.fillWidth regression — grid nested in row collapsed to 0)")
                return
            }

            var innerGridLayout = findByType(toArray(innerGridWidget.children), "QQuickGridLayout")
            if (!innerGridLayout) { fail("could not find nested grid's internal GridLayout"); return }
            var innerDelegates = filterByType(toArray(innerGridLayout.children), "WidgetDelegate")
            if (innerDelegates.length !== 2) { fail("expected 2 inner grid delegates, got " + innerDelegates.length); return }
            if (innerDelegates[0].width <= 0 || innerDelegates[1].width <= 0) {
                fail("inner grid children have non-positive width: " + [innerDelegates[0].width, innerDelegates[1].width])
                return
            }

            console.log("PASS: grid column ratio wA=" + wA + " wB=" + wB + " wC=" + wC +
                        "; align + nested grid-in-row all correct")
            Qt.exit(0)
        }
    }
}
