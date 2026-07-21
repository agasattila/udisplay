import QtQuick
import QtQuick.Layouts
import "../../qml/widgets" as W

/**
 * Regression test for flex-weighted proportional stretch and alignment
 * (RowWidget/GridWidget/LabelWidget), covering the manual ratio-binding
 * layout formula added to replace Qt 6.5-only Layout.horizontalStretchFactor
 * (see RowWidget.qml's header comment for why): a Qt 6.4 floor means there
 * is no compile-time way to gate a QML attached property that may not exist
 * on the running Qt version, so RowWidget.qml/GridWidget.qml compute
 * Layout.preferredWidth manually as max(implicitWidth, available share by
 * flex ratio).
 *
 * Covers:
 *  - flex: 2 gets twice the extra width of flex: 1 in a row (D9's formula).
 *  - flex: 0/omitted sizes to implicitWidth, does not stretch (D10).
 *  - Row-level `align` default + a child's own `align` override (D6/D13).
 *  - Label `align` maps to the correct Text.AlignLeft/Right/HCenter/Justify.
 *
 * Run headless: `qml -platform offscreen test_row_flex_align.qml`.
 * Exits 0 on pass, 1 (with a console.error) on fail — CTest reads the exit code.
 */
Item {
    width: 600
    height: 400

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

    /* ── Row 1: flex-weighted stretch (flex:1, flex:2, flex:0) ─────────── */
    property var weightedRowProps: {
        "align": "left",
        "items": [
            { type: "label", widgetId: 0, label: "", enabled: true, visible: true, value: null, flex: 1, align: "",
              props: { text: "A", style: "body" } },
            { type: "label", widgetId: 0, label: "", enabled: true, visible: true, value: null, flex: 2, align: "",
              props: { text: "B", style: "body" } },
            { type: "label", widgetId: 0, label: "", enabled: true, visible: true, value: null, flex: 0, align: "",
              props: { text: "C", style: "body" } }
        ]
    }
    W.RowWidget {
        id: weightedRow
        anchors.left: parent.left
        anchors.right: parent.right
        props: weightedRowProps
    }

    /* ── Row 2: container align default + child override ───────────────── */
    property var alignRowProps: {
        "align": "center",
        "items": [
            { type: "label", widgetId: 0, label: "", enabled: true, visible: true, value: null, flex: 0, align: "",
              props: { text: "Inherits", style: "body" } },
            { type: "label", widgetId: 0, label: "", enabled: true, visible: true, value: null, flex: 0, align: "right",
              props: { text: "Overrides", style: "body" } }
        ]
    }
    W.RowWidget {
        id: alignRow
        y: 100
        anchors.left: parent.left
        anchors.right: parent.right
        props: alignRowProps
    }

    /* ── Standalone LabelWidget: align mapping ──────────────────────────── */
    W.LabelWidget {
        id: leftLabel
        y: 200
        props: { "text": "left", "style": "body", "labelAlign": "left" }
    }
    W.LabelWidget {
        id: rightLabel
        y: 230
        props: { "text": "right", "style": "body", "labelAlign": "right" }
    }
    W.LabelWidget {
        id: centerLabel
        y: 260
        props: { "text": "center", "style": "body", "labelAlign": "center" }
    }
    W.LabelWidget {
        id: justifyLabel
        y: 290
        props: { "text": "justify", "style": "body", "labelAlign": "justify" }
    }
    W.LabelWidget {
        id: defaultLabel
        y: 320
        props: { "text": "default", "style": "body" }
    }

    Timer {
        interval: 500
        running: true
        onTriggered: {
            /* ── Weighted stretch ────────────────────────────────────── */
            var rowLayout = findByType(toArray(weightedRow.children), "QQuickRowLayout")
            if (!rowLayout) { fail("could not find weightedRow's internal RowLayout"); return }
            var delegates = filterByType(toArray(rowLayout.children), "WidgetDelegate")
            if (delegates.length !== 3) { fail("expected 3 delegates, got " + delegates.length); return }

            var wA = delegates[0].width, wB = delegates[1].width, wC = delegates[2].width
            if (wA <= 0 || wB <= 0 || wC <= 0) { fail("non-positive width: " + [wA, wB, wC]); return }

            /* B (flex:2) should get roughly twice A's (flex:1) extra-space share.
             * Compare against C (flex:0, sized to implicitWidth only) as the
             * "no stretch" baseline — A and B must both exceed it. */
            if (!(wB > wA)) { fail("flex:2 (" + wB + ") should be wider than flex:1 (" + wA + ")"); return }
            if (!(wA > wC + 1)) { fail("flex:1 (" + wA + ") should be wider than flex:0 (" + wC + ")"); return }
            /* Ratio check with tolerance — extra space split ~2:1 between B:A. */
            var extraA = wA - wC, extraB = wB - wC
            var ratio = extraB / Math.max(extraA, 1)
            if (ratio < 1.5 || ratio > 2.5) {
                fail("flex:2/flex:1 extra-space ratio out of range: " + ratio + " (wA=" + wA + " wB=" + wB + " wC=" + wC + ")")
                return
            }

            /* ── Container align default + child override ──────────────── */
            var alignLayout = findByType(toArray(alignRow.children), "QQuickRowLayout")
            if (!alignLayout) { fail("could not find alignRow's internal RowLayout"); return }
            var alignDelegates = filterByType(toArray(alignLayout.children), "WidgetDelegate")
            if (alignDelegates.length !== 2) { fail("expected 2 align delegates, got " + alignDelegates.length); return }
            /* Qt.AlignHCenter = 0x0084, Qt.AlignRight = 0x0002 (Qt::Alignment flags) */
            if ((alignDelegates[0].Layout.alignment & Qt.AlignHCenter) === 0) {
                fail("first child should inherit row's align:center, got " + alignDelegates[0].Layout.alignment)
                return
            }
            if ((alignDelegates[1].Layout.alignment & Qt.AlignRight) === 0) {
                fail("second child should use its own align:right override, got " + alignDelegates[1].Layout.alignment)
                return
            }

            /* ── Label alignment mapping ────────────────────────────────── */
            function labelText(labelWidget) {
                /* QtQuick.Controls' Label control's toString() uses its
                 * registered QML type name, e.g. "Label_QMLTYPE_1(0x...)". */
                return findByType(toArray(labelWidget.children), "Label")
            }
            var lLeft = labelText(leftLabel), lRight = labelText(rightLabel)
            var lCenter = labelText(centerLabel), lJustify = labelText(justifyLabel)
            var lDefault = labelText(defaultLabel)
            if (!lLeft || lLeft.horizontalAlignment !== Text.AlignLeft) { fail("align:left did not map to Text.AlignLeft"); return }
            if (!lRight || lRight.horizontalAlignment !== Text.AlignRight) { fail("align:right did not map to Text.AlignRight"); return }
            if (!lCenter || lCenter.horizontalAlignment !== Text.AlignHCenter) { fail("align:center did not map to Text.AlignHCenter"); return }
            if (!lJustify || lJustify.horizontalAlignment !== Text.AlignJustify) { fail("align:justify did not map to Text.AlignJustify"); return }
            if (!lDefault || lDefault.horizontalAlignment !== Text.AlignLeft) { fail("omitted align did not default to Text.AlignLeft"); return }

            console.log("PASS: weighted stretch wA=" + wA + " wB=" + wB + " wC=" + wC + " ratio=" + ratio.toFixed(2) + "; align + label mapping all correct")
            Qt.exit(0)
        }
    }
}
