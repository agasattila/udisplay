// SPDX-License-Identifier: LGPL-3.0-only
// Copyright (c) 2026 Attila Agas

import QtQuick
import QtQuick.Controls.Material
import QtQuick.Layouts
import "./"

/* Horizontal layout container.
 * props.items: [{type, widgetId, label, enabled, visible, value, props, flex, align}, ...]
 * props.align: "left"|"right"|"center" — default content alignment for
 * children that can't stretch (flex: 0/omitted); a child's own `align`
 * overrides this for that one child.
 * Each item renders via WidgetDelegate, which supports all widget types including
 * nested row and grid (unlimited depth).
 *
 * flex weighting: QtQuick.Layouts' Layout.horizontalStretchFactor (the native
 * way to express "flex:2 gets 2x the extra space of flex:1") requires Qt 6.5+
 * and does not exist in Qt 6.4 (this project's floor — see docs/building.md,
 * CMakeLists.txt's `find_package(Qt6 6.4 REQUIRED ...)`). QML has no
 * preprocessor, so there is no compile-time way to conditionally reference an
 * attached property that may not exist on the target Qt version — the QML
 * engine rejects the unknown property reference outright when it doesn't
 * exist, regardless of any runtime guard around it. Instead, each child's
 * width is computed manually: max(implicitWidth, available share by flex
 * ratio). This is Qt-version-independent (works identically 6.4 through
 * 6.11+) and the max() naturally makes flex: 0/omitted children size to
 * their own content and not stretch — no special-casing needed. */
Rectangle {
    id: root
    property string label: ""   /* optional; not rendered for layout containers */
    property var    props: ({})  /* { items: [...] } — non-required so Loader.source can bind it */
    /* Compact rendering — non-required (same reason as props above: set via
     * live binding when loaded through a dynamic Loader.source, e.g.
     * WidgetDelegate.qml's rowComp). Forwarded to every child's own
     * WidgetDelegate below so compact threads to arbitrary depth. */
    property bool   compact: false

    /* NOT row.implicitWidth. RowLayout (a Qt Quick Layouts type) manages
     * its own implicitWidth internally in C++, recomputed from children's
     * Layout.preferredWidth on every layout pass. Below, a flex child's
     * preferredWidth is defined to "fill whatever's left of row.width" —
     * summed across all children, that always adds back up to exactly
     * row.width, by construction. If root's implicitWidth mirrored
     * row.implicitWidth, it would become a tautology that mirrors
     * whatever row.width currently is. When this row is nested inside
     * another row/grid, WidgetDelegate.qml's
     * `Layout.preferredWidth: item.implicitWidth` feeds that mirrored
     * value back into how much width THIS row gets from ITS parent —
     * which becomes the new row.width, which re-mirrors again: a feedback
     * loop with no fixed point. Qt's layout engine detects this as
     * "possible QQuickItem::polish() loop" and never converges.
     * The fix has to happen one level up: root.implicitWidth
     * (a plain Rectangle, not a Layout type with its own internal opinion)
     * is bound to contentImplicitWidth below instead — a purely
     * content-based sum of each child's OWN implicitWidth (unstretched
     * natural size — CSS calls this flex-basis), which never reads row.width
     * or any child's stretched Layout.preferredWidth,
     * so it cannot feed back into itself. */
    implicitWidth: contentImplicitWidth
    implicitHeight: row.implicitHeight
    color: "transparent"

    property real contentImplicitWidth: {
        var items = props.items || []
        var sum = 0
        for (var i = 0; i < items.length; i++) {
            var d = repeater.itemAt(i)
            /* d is a WidgetDelegate (itself a Loader). d.implicitWidth is
             * the Loader's own auto-mirror, which is reliable for leaf
             * children but stays stuck at 0 for row/grid children (Qt 6.4.2
             * Loader{source:...} async-mirroring gap — see
             * WidgetDelegate.qml's Layout.preferredWidth comment for the
             * full writeup). d.item.implicitWidth (drilling into the loaded
             * content directly, one level deeper) is reliable for both:
             * for a leaf, d.item IS the leaf widget itself (same value as
             * d.implicitWidth); for row/grid, d.item is the inner
             * source:-loaded Loader, whose own .implicitWidth DOES
             * correctly track its loaded RowWidget/GridWidget instance
             * (confirmed working — this is the exact mechanism
             * WidgetDelegate.qml's own Layout.preferredWidth already uses).
             * Purely content-based either way — does not read row.width or
             * any child's stretched Layout.preferredWidth, so it cannot
             * feed back into itself. */
            sum += (d && d.item) ? d.item.implicitWidth : 0
        }
        return sum + Math.max(0, items.length - 1) * row.spacing
    }

    Layout.fillWidth: true

    RowLayout {
        id: row
        anchors { left: parent.left; right: parent.right;
                  verticalCenter: parent.verticalCenter }
        spacing: 0

        /* Sum of every child's flex weight. flex: 0/omitted children
         * contribute 0 (no effect on the ratio given to their siblings). */
        property int totalFlex: {
            var items = props.items || []
            var sum = 0
            for (var i = 0; i < items.length; i++)
                sum += (items[i].flex || 0)
            return sum
        }

        /* Sum of implicitWidth for every child that does NOT stretch
         * (flex: 0/omitted) — those children keep their own content width
         * regardless, so flex-bearing siblings must divide what's left
         * over after that, not the row's full width. Without this, a flex
         * child's own preferredWidth "ask" would exceed what's actually
         * available once non-flex siblings are accounted for. */
        property real nonFlexWidth: {
            var items = props.items || []
            var sum = 0
            for (var i = 0; i < items.length; i++) {
                if (!(items[i].flex > 0)) {
                    var d = repeater.itemAt(i)
                    sum += d ? d.implicitWidth : 0
                }
            }
            return sum
        }

        function alignFor(modelData) {
            var a = modelData.align || props.align || "left"
            return a === "right" ? Qt.AlignRight
                 : a === "center" ? Qt.AlignHCenter
                 : Qt.AlignLeft
        }

        Repeater {
            id: repeater
            model: props.items || []

            delegate: WidgetDelegate {
                required property var modelData
                compact: root.compact
                Layout.fillWidth: modelData.flex > 0
                Layout.minimumWidth: implicitWidth
                Layout.preferredWidth: Math.max(implicitWidth,
                    row.totalFlex > 0
                        ? Math.max(0, row.width - row.nonFlexWidth) * modelData.flex / row.totalFlex
                        : 0)
                Layout.alignment: row.alignFor(modelData)
            }
        }
    }
}
