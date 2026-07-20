import QtQuick
import "../../qml/widgets" as W

/**
 * Regression test for ButtonWidget.qml's multi-child face composition
 * (props.items: led/rgbled/display/label rendered inline via one embedded
 * compact RowWidget, loaded through facesRowLoader — see ButtonWidget.qml's
 * header comment. Per-type dispatch happens inside WidgetDelegate.qml/
 * RowWidget.qml, reused rather than a bespoke Repeater+Loader). Covers:
 *  - A face with all 4 supported leaf types renders without collapsing.
 *  - An unrecognized child type degrades safely (sourceComponent: null —
 *    no crash, no visible garbage), matching the client's documented
 *    permissive-parsing behavior (any type is accepted with zero
 *    diagnostics, per test_yaml_parser.cpp's buttonChild_nonLedType_accepted).
 *
 * Run headless: `qml -platform offscreen test_button_face_children.qml`.
 * Exits 0 on pass, 1 (with a console.error) on fail — CTest reads the exit code.
 */
Item {
    width: 400
    height: 200

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

    /* All 4 supported leaf types on one face. */
    W.ButtonWidget {
        id: fullFace
        widgetId: 1
        label: "Power"
        enabled: true
        props: ({
            shape: "rect",
            items: [
                { type: "led",     widgetId: 2, label: "",  enabled: true, value: true, props: {} },
                { type: "rgbled",  widgetId: 3, label: "",  enabled: true, value: 0x00ff00, props: {} },
                { type: "display", widgetId: 4, label: "V", enabled: true, value: 3.3, props: { unit: "V", format: "%.1f" } },
                { type: "label",   widgetId: 0, label: "",  enabled: true, value: null, props: { text: "Hi", style: "body" } }
            ]
        })
    }

    /* Unrecognized child type — must not crash, must render nothing for
     * that item (sourceComponent: null in ButtonWidget.qml's dispatch). */
    W.ButtonWidget {
        id: unknownChildFace
        y: 100
        widgetId: 5
        label: "Unknown"
        enabled: true
        props: ({
            shape: "rect",
            items: [
                { type: "some_future_type", widgetId: 6, label: "", enabled: true, value: null, props: {} }
            ]
        })
    }

    Timer {
        interval: 300
        running: true
        onTriggered: {
            if (fullFace.implicitWidth <= 0)
                { fail("button with 4 face children collapsed to zero width"); return }
            if (fullFace.implicitHeight <= 0)
                { fail("button with 4 face children collapsed to zero height"); return }

            if (unknownChildFace.implicitWidth <= 0)
                { fail("button with unrecognized-type child collapsed to zero width"); return }

            console.log("PASS: fullFace=" + fullFace.implicitWidth + "x" + fullFace.implicitHeight +
                        " unknownChildFace=" + unknownChildFace.implicitWidth + "x" + unknownChildFace.implicitHeight)
            Qt.exit(0)
        }
    }
}
