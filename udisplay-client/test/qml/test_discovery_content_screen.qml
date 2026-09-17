import QtQuick
import QtQuick.Controls

/**
 * Regression test for DiscoveryScreen's base "connect" content (device list,
 * manual host/port entry, Connect button, error banner) after the hamburger-
 * menu restructuring (issue #14).
 *
 * DiscoveryScreen.qml moved this entire block from being a direct child
 * ColumnLayout of the Page into `discoveryContentComponent`, the initialItem
 * of a new `discoveryLocalStack` StackView (added so About/Licenses/
 * LicenseDetail can be pushed on top of it). The moved content's bindings
 * and handlers were not meant to change -- but nothing on this branch
 * actually re-verified that the device list still renders, that typing a
 * host/port and tapping Connect still calls controller.connectDevice() with
 * the right arguments, or that the error banner still reacts to
 * controller.state, now that this content lives one level deeper inside a
 * StackView-managed Item instead of directly under the Page. All three
 * existing menu tests (test_discovery_menu_navigation.qml,
 * test_discovery_menu_reset_on_connect.qml,
 * test_license_detail_navigation.qml) only ever exercise the header/menu/
 * About/Licenses/LicenseDetail paths and never touch this base screen.
 *
 * Loads the real main.qml by URL with the same minimal controller/
 * discoveryModel mocks as the other discovery-menu tests, but with a
 * discoveryModel that reports one discovered device and a controller.
 * connectDevice() that records its call so the click handler can be
 * verified end-to-end, not just located.
 *
 * Run headless: `qml -platform offscreen test_discovery_content_screen.qml`.
 * Exits 0 on pass, 1 (with a console.error) on fail -- CTest reads the exit code.
 */
Item {
    width: 420
    height: 720

    property var mainWindow: null

    QtObject {
        id: controller
        property string deviceName: "Sentinel Device"
        property string state: "disconnected"
        property string designErrorString: ""
        property string errorString: "Connection refused"
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

        property int connectDeviceCallCount: 0
        property var lastConnectArgs: null

        function connectDevice(transport, host, port) {
            connectDeviceCallCount++
            lastConnectArgs = { transport: transport, host: host, port: port }
        }
        function connectDiscovered() {}
        function disconnectDevice() {}
    }

    QtObject {
        id: discoveryModel
        property int count: 1
        property bool scanning: false
        property string scanError: ""
        function startScan() {}
        function stopScan() {}
        function deviceAt(i) {
            return {
                index: i,
                transportType: 0,
                displayName: "Kitchen Display",
                sourceLabel: "mDNS",
                address: "192.168.1.42",
                port: 5555,
                rssi: -1
            }
        }
    }

    function fail(msg) {
        console.error("FAIL: " + msg)
        Qt.exit(1)
    }

    function findByObjectName(item, name) {
        if (!item)
            return null
        if (item.objectName === name)
            return item
        if (!item.children)
            return null
        for (var i = 0; i < item.children.length; i++) {
            var found = findByObjectName(item.children[i], name)
            if (found)
                return found
        }
        return null
    }

    Component.onCompleted: {
        var comp = Qt.createComponent(Qt.resolvedUrl("../../qml/main.qml"))
        if (comp.status === Component.Error) {
            fail("failed to load main.qml: " + comp.errorString())
            return
        }
        mainWindow = comp.createObject(null)
        if (!mainWindow) {
            fail("failed to instantiate main.qml")
            return
        }
    }

    Timer {
        interval: 300
        running: true
        onTriggered: step0_checkBaseScreenRenders()
    }

    function step0_checkBaseScreenRenders() {
        var localStack = findByObjectName(mainWindow.contentItem, "discoveryLocalStack")
        if (!localStack || localStack.depth !== 1) {
            fail("expected discoveryLocalStack at depth 1 on initial load, got " +
                 (localStack ? localStack.depth : "<no stack>"))
            return
        }

        var deviceList = findByObjectName(mainWindow.contentItem, "discoveryDeviceList")
        if (!deviceList || deviceList.count !== 1) {
            fail("expected discoveryDeviceList to reflect discoveryModel.count=1, got " +
                 (deviceList ? deviceList.count : "<not found>"))
            return
        }

        var hostField = findByObjectName(mainWindow.contentItem, "discoveryHostField")
        var portField = findByObjectName(mainWindow.contentItem, "discoveryPortField")
        var connectButton = findByObjectName(mainWindow.contentItem, "discoveryConnectButton")
        if (!hostField || !portField || !connectButton) {
            fail("could not find discoveryHostField/discoveryPortField/discoveryConnectButton -- " +
                 "the manual-connect UI should still render inside discoveryContentComponent")
            return
        }
        if (connectButton.text !== "Connect") {
            fail("expected Connect button text 'Connect' while disconnected, got " + connectButton.text)
            return
        }
        if (connectButton.enabled !== true) {
            fail("expected Connect button to be enabled while disconnected")
            return
        }

        var errorBanner = findByObjectName(mainWindow.contentItem, "discoveryErrorBanner")
        if (!errorBanner) {
            fail("could not find discoveryErrorBanner")
            return
        }
        if (errorBanner.visible !== false) {
            fail("expected error banner hidden while controller.state=\"disconnected\", got visible=" +
                 errorBanner.visible)
            return
        }

        hostField.text = "10.0.0.5"
        portField.text = "9000"
        connectButton.clicked()

        if (controller.connectDeviceCallCount !== 1) {
            fail("expected Connect button click to call controller.connectDevice() exactly once, got " +
                 controller.connectDeviceCallCount + " calls")
            return
        }
        if (!controller.lastConnectArgs ||
            controller.lastConnectArgs.transport !== 0 ||
            controller.lastConnectArgs.host !== "10.0.0.5" ||
            controller.lastConnectArgs.port !== 9000) {
            fail("expected connectDevice(0, \"10.0.0.5\", 9000), got " +
                 JSON.stringify(controller.lastConnectArgs))
            return
        }

        // Now flip to "error" and confirm the banner + button re-enable
        // wiring still reacts correctly to controller.state from inside the
        // StackView-managed content item.
        controller.state = "error"
        Qt.callLater(step1_checkErrorState)
    }

    function step1_checkErrorState() {
        var errorBanner = findByObjectName(mainWindow.contentItem, "discoveryErrorBanner")
        if (!errorBanner || errorBanner.visible !== true) {
            fail("expected error banner visible after controller.state=\"error\", got visible=" +
                 (errorBanner ? errorBanner.visible : "<not found>"))
            return
        }

        var connectButton = findByObjectName(mainWindow.contentItem, "discoveryConnectButton")
        if (!connectButton || connectButton.enabled !== true) {
            fail("expected Connect button to remain enabled in the \"error\" state " +
                 "(user should be able to retry), got enabled=" +
                 (connectButton ? connectButton.enabled : "<not found>"))
            return
        }

        // Sanity: the hamburger menu button should still be reachable too --
        // the base screen and the header live at the same local-stack depth.
        var menuButton = findByObjectName(mainWindow.contentItem, "discoveryMenuButton")
        if (!menuButton || menuButton.visible !== true) {
            fail("expected discoveryMenuButton visible at local stack depth 1, got " +
                 (menuButton ? menuButton.visible : "<not found>"))
            return
        }

        console.log("PASS: base discovery content (device list, manual connect, error banner) " +
                     "still renders and functions correctly inside discoveryContentComponent")
        Qt.exit(0)
    }
}
