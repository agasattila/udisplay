// SPDX-License-Identifier: LGPL-3.0-only
// Copyright (c) 2026 Attila Agas

import QtQuick
import QtQuick.Layouts
import "../../qml/widgets" as W

/**
 * Acceptance test for docs/designs/unify-widget-style-handling.md's
 * per-widget style: resolution — the scenario from Cross-Model Perspective,
 * adopted into Success Criteria: a widget pinned to an explicit `style:`
 * stays pinned when the app-wide active style changes; an unpinned widget
 * follows the active style live.
 *
 * Mocks DeviceController::effectiveStyleFor(row) as a small per-row lookup
 * table, matching every other standalone QML test's controller-mock pattern
 * (see FakeWidgetModel.qml's header comment for why a real C++
 * DeviceController is never used here) — plus a `styles` registry and a
 * setActiveStyle() mock that reassigns the `activeStyle` property, which
 * QML's own binding system then propagates reactively to
 * WidgetDelegate.qml's `_effectiveStyle` (no manual signal needed, exactly
 * like the real Q_PROPERTY/NOTIFY pair).
 *
 * Three widgets: two pinned to distinct named stylesheets, one unpinned.
 * Asserts each WidgetDelegate-hosted DisplayWidget's own `effectiveStyle`
 * property directly (the threading pipeline under test), not its rendered
 * pixels — DisplayWidget's own use of that property is covered elsewhere.
 *
 * Run headless: `qml -platform offscreen test_style_resolution.qml`.
 * Exits 0 on pass, 1 (with a console.error) on fail — CTest reads the exit code.
 */
Item {
    width: 600
    height: 200

    QtObject {
        id: controller
        property var styles: ({
            "default": { accent: "#00d4aa", text_muted: "#888888", button: "#00d4aa", button_text: "#0d0d1a" },
            "alarm":   { accent: "#e05555", text_muted: "#888888", button: "#e05555", button_text: "#ffffff" },
            "warning": { accent: "#f5a623", text_muted: "#888888", button: "#f5a623", button_text: "#000000" },
            "night":   { accent: "#111111", text_muted: "#888888", button: "#111111", button_text: "#eeeeee" }
        })
        property var activeStyle: styles["default"]
        property var widgetModel: FakeWidgetModel {}

        /* Mirrors DeviceController::effectiveStyleFor(row): the row's own
         * pinned style name if set, else the current activeStyle. Rows 0/1
         * are pinned; row 2 is deliberately absent from this table (no
         * explicit style:), matching a real parse where props.style is
         * simply unset for that widget. */
        property var _rowStyleNames: ({ 0: "alarm", 1: "warning", 3: "alarm" })
        function effectiveStyleFor(row) {
            var name = _rowStyleNames[row]
            return (name !== undefined && name in styles) ? styles[name] : activeStyle
        }

        function setActiveStyle(name) {
            if (name in styles) activeStyle = styles[name]
        }
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

    ListModel {
        id: items
        property bool ready: false
        Component.onCompleted: {
            /* row 0: pinned "alarm" */
            append({ row: 0, widgetId: 0x10, type: "display", label: "", enabled: true, widgetVisible: true, value: 1,
                     flex: 0, align: "", props: { format: "%.0f" } })
            /* row 1: pinned "warning" */
            append({ row: 1, widgetId: 0x11, type: "display", label: "", enabled: true, widgetVisible: true, value: 2,
                     flex: 0, align: "", props: { format: "%.0f" } })
            /* row 2: unpinned — follows controller.activeStyle */
            append({ row: 2, widgetId: 0x12, type: "display", label: "", enabled: true, widgetVisible: true, value: 3,
                     flex: 0, align: "", props: { format: "%.0f" } })
            /* row 3: button pinned "alarm" — WidgetDelegate's buttonComp
             * must thread _effectiveStyle into ButtonWidget so the
             * button's own face fills with alarm's button token. */
            append({ row: 3, widgetId: 0x13, type: "button", label: "Go", enabled: true, widgetVisible: true, value: 0,
                     flex: 0, align: "", props: { style: "alarm" } })
            ready = true
        }
    }
    W.RowWidget {
        id: row
        anchors.left: parent.left
        anchors.right: parent.right
        props: ({ align: "left" })
        childModel: items.ready ? items : null
    }

    function delegates() {
        var rowLayout = null
        var children = toArray(row.children)
        for (var i = 0; i < children.length; i++)
            if (children[i].toString().indexOf("QQuickRowLayout") === 0) { rowLayout = children[i]; break }
        if (!rowLayout) return []
        return filterByType(toArray(rowLayout.children), "WidgetDelegate")
    }

    function accentOf(delegateItem) {
        return delegateItem.item ? String(delegateItem.item.effectiveStyle.accent) : undefined
    }

    Timer {
        interval: 500
        running: true
        onTriggered: {
            var ds = delegates()
            if (ds.length !== 4) { fail("expected 4 delegates, got " + ds.length); return }

            /* ── Initial state ───────────────────────────────────────── */
            if (accentOf(ds[0]) !== "#e05555")
                { fail("row 0 (pinned alarm) initial accent should be #e05555, got " + accentOf(ds[0])); return }
            if (accentOf(ds[1]) !== "#f5a623")
                { fail("row 1 (pinned warning) initial accent should be #f5a623, got " + accentOf(ds[1])); return }
            if (accentOf(ds[2]) !== "#00d4aa")
                { fail("row 2 (unpinned) initial accent should follow default (#00d4aa), got " + accentOf(ds[2])); return }

            /* Button (row 3): WidgetDelegate threads the resolved style
             * through ButtonWidget into its ButtonFace fill. */
            var btnFace = ds[3].item ? filterByType(toArray(ds[3].item.children), "ButtonFace_QML")[0] : null
            if (!btnFace) { fail("could not find row 3 button's ButtonFace"); return }
            if (btnFace.color.toString() !== "#e05555")
                { fail("row 3 (button pinned alarm) face should fill with alarm.button #e05555, got " + btnFace.color); return }

            /* ── Switch the app-wide active style ────────────────────── */
            controller.setActiveStyle("night")

            if (accentOf(ds[0]) !== "#e05555")
                { fail("row 0 (pinned alarm) must stay pinned after setActiveStyle, got " + accentOf(ds[0])); return }
            if (accentOf(ds[1]) !== "#f5a623")
                { fail("row 1 (pinned warning) must stay pinned after setActiveStyle, got " + accentOf(ds[1])); return }
            if (accentOf(ds[2]) !== "#111111")
                { fail("row 2 (unpinned) must follow setActiveStyle(\"night\"), got " + accentOf(ds[2])); return }

            if (btnFace.color.toString() !== "#e05555")
                { fail("row 3 (button pinned alarm) must stay pinned after setActiveStyle, got " + btnFace.color); return }

            console.log("PASS: pinned widgets (alarm, warning, alarm button face) stayed pinned; unpinned widget followed setActiveStyle")
            Qt.exit(0)
        }
    }
}
