// SPDX-License-Identifier: LGPL-3.0-only
// Copyright (c) 2026 Attila Agas

import QtQuick
import QtQuick.Layouts
import "../../qml/widgets" as W

/**
 * Regression test for SectionWidget.qml rendering its own children.
 *
 * Pre-flattening, a section's children were flat top-level siblings in
 * WidgetModel's own list, tracked for collapse-visibility only via a
 * separate `sectionOwnerRow` field — completely independent of rendering
 * position. The flattening refactor folded both into one `parentId`
 * (children now structurally nest under their section's own row, matching
 * every other container's childModel scoping), which fixed
 * collapse-visibility but silently broke rendering: SectionWidget.qml had
 * no childModel/Repeater of its own, so any widgets nested inside a
 * `section:` block's `widgets:` disappeared from the UI entirely — while
 * every existing test (C++ parentId assertions, WidgetModel collapse
 * tests) kept passing, since none of them checked whether the children
 * were actually INSTANTIATED anywhere in the QML tree.
 *
 * Covers:
 *  - A section with real children renders them (not the pre-fix collapse
 *    to header-only height/width).
 *  - Each child's own `widgetVisible` role (which is what WidgetModel's
 *    collapse-ancestor-walk actually toggles — see WidgetModel.cpp's
 *    VisibleRole) is honored by the child's own WidgetDelegate, proving
 *    SectionWidget doesn't need its own filtering logic: instantiate every
 *    childModel row, let each row's own visibility binding do the hiding.
 *
 * Run headless: `qml -platform offscreen test_section_widget.qml`.
 * Exits 0 on pass, 1 (with a console.error) on fail — CTest reads the exit code.
 */
Item {
    width: 400
    height: 300

    QtObject {
        id: controller
        property var activeStyle: QtObject {
            property string surface:      "#1a1a2e"
            property string accent:       "#00d4aa"
            property string line:         "#1e1e3a"
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

    function filterByType(items, needle) {
        var out = []
        for (var i = 0; i < items.length; i++)
            if (items[i].toString().indexOf(needle) === 0)
                out.push(items[i])
        return out
    }

    ListModel {
        id: sectionChildren
        property bool ready: false
        Component.onCompleted: {
            append({ widgetId: 0x10, type: "label", label: "", enabled: true, widgetVisible: true, value: 0,
                     flex: 0, align: "", props: { text: "Visible child", style: "body" } })
            append({ widgetId: 0x11, type: "label", label: "", enabled: true, widgetVisible: false, value: 0,
                     flex: 0, align: "", props: { text: "Hidden child", style: "body" } })
            ready = true
        }
    }

    W.SectionWidget {
        id: section
        anchors.left: parent.left
        anchors.right: parent.right
        label: "Controls"
        props: ({ collapsible: true, collapsed: false })
        childModel: sectionChildren.ready ? sectionChildren : null
    }

    Timer {
        interval: 300
        running: true
        onTriggered: {
            var col = section.children[0]
            if (!col) { fail("could not find SectionWidget's internal ColumnLayout"); return }

            var repeater = null
            for (var i = 0; i < col.children.length; i++) {
                if (col.children[i].toString().indexOf("QQuickRepeater") === 0) { repeater = col.children[i]; break }
            }
            if (!repeater) { fail("could not find SectionWidget's own children Repeater"); return }

            var delegates = filterByType(toArray(repeater.parent.children), "WidgetDelegate")
            if (delegates.length !== 2) { fail("expected 2 child delegates rendered, got " + delegates.length); return }

            var visibleChild = delegates[0], hiddenChild = delegates[1]
            if (visibleChild.width <= 0 || visibleChild.height <= 0)
                { fail("visible child collapsed to zero size: " + visibleChild.width + "x" + visibleChild.height); return }
            if (visibleChild.visible !== true)
                { fail("child with widgetVisible:true should be visible"); return }
            if (hiddenChild.visible !== false)
                { fail("child with widgetVisible:false should be hidden (SectionWidget must not force its own children visible)"); return }

            /* The core regression: section's own height must include the
             * children's stacked height, not just the 36px header. */
            if (section.implicitHeight <= 36)
                { fail("SectionWidget.implicitHeight=" + section.implicitHeight + " does not include children — regression: children not rendered"); return }

            console.log("PASS: section renders " + delegates.length + " children, visible/hidden roles honored, implicitHeight=" + section.implicitHeight)
            Qt.exit(0)
        }
    }
}
