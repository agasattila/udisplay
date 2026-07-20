import QtQuick
import "../../qml/widgets" as W

/**
 * Regression test for ButtonWidget.qml's facesRowLoader / RowWidget.qml
 * vertical centering.
 *
 * facesRowLoader is anchored top+bottom with topMargin/bottomMargin (see
 * ButtonWidget.qml), so on a button taller than its face content, the
 * Loader is stretched well beyond the loaded RowWidget's own implicit
 * height. RowWidget's internal RowLayout ("row") must vertically center
 * within that stretched space — without a verticalCenter anchor, it stays
 * pinned to row.y=0 (the top), leaving face content visually top-aligned
 * instead of centered.
 *
 * Run headless: `qml -platform offscreen test_button_face_vertical_center.qml`.
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

    /* Square shape forces a tall button (min 64px, see ButtonWidget.qml's
     * implicitHeight) well beyond a single compact LED+label row's own
     * implicit height — the exact condition that exposes top-pinning. */
    W.ButtonWidget {
        id: btn
        widgetId: 1
        label: "Power"
        enabled: true
        width: 160
        height: 160
        props: ({
            shape: "square",
            items: [
                { type: "led", widgetId: 2, label: "PWR", enabled: true, value: true, props: {} }
            ]
        })
    }

    Timer {
        interval: 300
        running: true
        onTriggered: {
            /* btn's structure (ButtonWidget.qml): Rectangle "root" ->
             * Rectangle "btn" (children[0]) -> [Text faceText, Loader
             * facesRowLoader, MouseArea] (children[1] is the Loader). */
            var innerBtn = btn.children[0]
            var loader = innerBtn.children[1]
            if (!loader || !loader.item) { fail("facesRowLoader not loaded"); return }

            var rowWidget = loader.item       // the loaded RowWidget instance
            var row = rowWidget.children[0]   // RowWidget.qml's internal RowLayout

            var expectedRowYWithinLoader = (loader.height - row.height) / 2
            var actualRowYWithinLoader = rowWidget.y + row.y
            var delta = Math.abs(actualRowYWithinLoader - expectedRowYWithinLoader)

            if (delta > 1.0) {
                fail("row not vertically centered in facesRowLoader: expected y=" +
                     expectedRowYWithinLoader + " got y=" + actualRowYWithinLoader +
                     " (loader.height=" + loader.height + " row.height=" + row.height + ")")
                return
            }

            console.log("PASS: row centered at y=" + actualRowYWithinLoader +
                        " within loader height " + loader.height)
            Qt.exit(0)
        }
    }
}
