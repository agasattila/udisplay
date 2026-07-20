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
     * GridLayout unifies width within a column (every cell in a column
     * shares the same actual rendered width) — so the ratio can only be
     * observed by comparing ACROSS columns, not within one. Column 0 (A,
     * flex:3 + C, flex:0) should end up wider than column 1 (B, flex:0 +
     * D, flex:0) because A's flex demand drives column 0's width; nothing
     * in column 1 asks for more than its own content. */
    property var gridProps: {
        "columns": 2,
        "align": "center",
        "items": [
            { type: "label", widgetId: 0, label: "", enabled: true, visible: true, value: null, flex: 3, align: "",
              props: { text: "A", style: "body" } },
            { type: "label", widgetId: 0, label: "", enabled: true, visible: true, value: null, flex: 0, align: "right",
              props: { text: "B", style: "body" } },
            { type: "label", widgetId: 0, label: "", enabled: true, visible: true, value: null, flex: 0, align: "",
              props: { text: "C", style: "body" } },
            { type: "label", widgetId: 0, label: "", enabled: true, visible: true, value: null, flex: 0, align: "",
              props: { text: "D", style: "body" } }
        ]
    }
    W.GridWidget {
        id: myGrid
        anchors.left: parent.left
        anchors.right: parent.right
        props: gridProps
    }

    /* ── Grid 2: a grid nested inside a row (gridComp fillWidth fix) ────── */
    property var outerRowProps: {
        "items": [
            { type: "label", widgetId: 0, label: "", enabled: true, visible: true, value: null, flex: 0, align: "",
              props: { text: "Before", style: "body" } },
            { type: "grid", widgetId: 0, label: "", enabled: true, visible: true, value: null, flex: 1, align: "",
              props: { columns: 2, items: [
                  { type: "label", widgetId: 0, label: "", enabled: true, visible: true, value: null, flex: 1, align: "",
                    props: { text: "G1", style: "body" } },
                  { type: "label", widgetId: 0, label: "", enabled: true, visible: true, value: null, flex: 1, align: "",
                    props: { text: "G2", style: "body" } }
              ] } }
        ]
    }
    W.RowWidget {
        id: outerRow
        y: 150
        anchors.left: parent.left
        anchors.right: parent.right
        props: outerRowProps
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
            /* Same column shares the same actual width (GridLayout invariant). */
            if (wA !== wC) { fail("column 0 (A=" + wA + ", C=" + wC + ") should share one column width"); return }
            if (wB !== wD) { fail("column 1 (B=" + wB + ", D=" + wD + ") should share one column width"); return }
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
