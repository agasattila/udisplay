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
 * WidgetModel is a flat list now — a button's face children come from
 * WidgetModel::childModel(), not a props.items array (see ButtonWidget.qml).
 * This test builds a ListModel per face and feeds it via childModel: instead
 * — see FakeWidgetModel.qml's header comment for why a
 * `controller.widgetModel` stand-in is needed at all.
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
        property var widgetModel: FakeWidgetModel {}
        function sendButtonPress(id) {}
        function sendButtonRelease(id) {}
        function sendButtonClick(id) {}
    }

    function fail(msg) {
        console.error("FAIL: " + msg)
        Qt.exit(1)
    }

    ListModel {
        id: fullFaceItems
        property bool ready: false
        Component.onCompleted: {
            /* value role must stay a consistent type across every append()
             * in this ListModel — QML ListModel infers a role's type from
             * its first row and silently rejects later appends of a
             * different type for that same role. led's "on" state is
             * truthy-checked (see LedWidget.qml's `value ? ... : ...`), so
             * 1/0 works identically to true/false here. */
            append({ widgetId: 2, type: "led",     label: "",  enabled: true, widgetVisible: true, value: 1,
                     flex: 0, align: "", props: {} })
            append({ widgetId: 3, type: "rgbled",  label: "",  enabled: true, widgetVisible: true, value: 0x00ff00,
                     flex: 0, align: "", props: {} })
            append({ widgetId: 4, type: "display", label: "V", enabled: true, widgetVisible: true, value: 3.3,
                     flex: 0, align: "", props: { unit: "V", format: "%.1f" } })
            append({ widgetId: 0, type: "label",   label: "",  enabled: true, widgetVisible: true, value: 0,
                     flex: 0, align: "", props: { text: "Hi", style: "body" } })
            ready = true
        }
    }
    /* All 4 supported leaf types on one face. */
    W.ButtonWidget {
        id: fullFace
        widgetId: 1
        label: "Power"
        enabled: true
        props: ({ shape: "rect" })
        childModel: fullFaceItems.ready ? fullFaceItems : null
    }

    ListModel {
        id: unknownChildItems
        property bool ready: false
        Component.onCompleted: {
            append({ widgetId: 6, type: "some_future_type", label: "", enabled: true, widgetVisible: true, value: "",
                     flex: 0, align: "", props: {} })
            ready = true
        }
    }
    /* Unrecognized child type — must not crash, must render nothing for
     * that item (sourceComponent: null in ButtonWidget.qml's dispatch). */
    W.ButtonWidget {
        id: unknownChildFace
        y: 100
        widgetId: 5
        label: "Unknown"
        enabled: true
        props: ({ shape: "rect" })
        childModel: unknownChildItems.ready ? unknownChildItems : null
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
