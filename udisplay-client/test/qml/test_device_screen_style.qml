import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import "../../qml" as Screens

/**
 * Regression test for DeviceScreen.qml's page chrome (page background, header
 * toolbar background) being wired to the global stylesheet's `background` /
 * `surface` tokens instead of hardcoded hex.
 *
 * DeviceScreen.qml was not touched by the global-stylesheet migration (commit
 * 4493c60, feat/yaml-global-stylesheet) -- that commit migrated the 14 widget
 * QML files under qml/widgets/ but left the surrounding page chrome (page
 * background Rectangle, header ToolBar, Design Mode error overlay backdrop)
 * hardcoded to "#0d0d1a" / "#1a1a2e". A device YAML that declares a custom
 * `style:` theme therefore re-colors every widget's internals but the page
 * canvas and header bar behind them never change -- which is what a user
 * would see as "background and surface don't work", even though the tokens
 * themselves are correctly defined and consumed elsewhere.
 *
 * Uses sentinel colors that do NOT match any built-in default, so the test
 * fails if DeviceScreen falls back to a hardcoded literal instead of reading
 * controller.activeStyle.
 *
 * Run headless: `qml -platform offscreen test_device_screen_style.qml`.
 * Exits 0 on pass, 1 (with a console.error) on fail -- CTest reads the exit code.
 */
Item {
    width: 400
    height: 600

    QtObject {
        id: controller
        property string deviceName: "Sentinel Device"
        property string state: "running"
        property string designErrorString: ""
        property var widgetModel: []
        property var activeStyle: QtObject {
            property string background:   "#123456"
            property string surface:      "#abcdef"
            property string text_heading: "#ff00ff"
            property string text:         "#c0c0c0"
            property string text_muted:   "#888888"
            property string border:       "#1e1e3a"
            property string line:         "#1e1e3a"
            property string accent:       "#00d4aa"
            property string button:       "#00d4aa"
            property string button_text:  "#0d0d1a"
        }
        function disconnectDevice() {}
    }

    function fail(msg) {
        console.error("FAIL: " + msg)
        Qt.exit(1)
    }

    Screens.DeviceScreen {
        id: deviceScreen
        anchors.fill: parent
    }

    Timer {
        interval: 300
        running: true
        onTriggered: {
            var bg = deviceScreen.background ? deviceScreen.background.color : undefined
            if (String(bg) !== "#123456") {
                fail("page background did not follow controller.activeStyle.background, got " + bg)
                return
            }

            var headerBg = deviceScreen.header ? deviceScreen.header.Material.background : undefined
            if (String(headerBg) !== "#abcdef") {
                fail("header ToolBar background did not follow controller.activeStyle.surface, got " + headerBg)
                return
            }

            console.log("PASS: DeviceScreen page background=" + bg + " header background=" + headerBg + " both follow controller.activeStyle")
            Qt.exit(0)
        }
    }
}
