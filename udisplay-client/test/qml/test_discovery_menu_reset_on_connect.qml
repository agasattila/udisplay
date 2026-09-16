import QtQuick
import QtQuick.Controls

/**
 * Regression test for a bug found by adversarial review during /ship
 * (issue #14 follow-up): DiscoveryScreen's hamburger-menu local StackView
 * is never reset when a device connects mid-browse.
 *
 * main.qml's root StackView only ever pushes DeviceScreen OVER the single
 * DiscoveryScreen instance and later pops back to that SAME instance --
 * DiscoveryScreen is never destroyed/recreated. Without a reset, connecting
 * while the user is 2+ levels deep in DiscoveryScreen's local stack (About,
 * Licenses, LicenseDetail) leaves that state untouched underneath
 * DeviceScreen; on the next disconnect, DiscoveryScreen reappears exactly
 * as left -- stuck on a submenu with the hamburger button hidden, instead
 * of the device list the user expects to reconnect from.
 *
 * Fix: DiscoveryScreen.qml's existing `Connections { target: controller }`
 * block now also calls `menu.close()` + `localStack.pop(null)` whenever
 * `controller.state` becomes "running".
 *
 * Run headless: `qml -platform offscreen test_discovery_menu_reset_on_connect.qml`.
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
        property string errorString: ""
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
        function connectDevice() {}
        function connectDiscovered() {}
        function disconnectDevice() {}
    }

    QtObject {
        id: discoveryModel
        property int count: 0
        property bool scanning: false
        property string scanError: ""
        function startScan() {}
        function stopScan() {}
        function deviceAt(i) { return null }
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

    function findInMenu(anyWindowItem, name) {
        var overlay = anyWindowItem.Overlay ? anyWindowItem.Overlay.overlay : null
        if (!overlay)
            return null
        return findByObjectName(overlay, name)
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
        onTriggered: step0_openMenu()
    }

    function step0_openMenu() {
        var menuButton = findByObjectName(mainWindow.contentItem, "discoveryMenuButton")
        if (!menuButton) {
            fail("could not find discoveryMenuButton")
            return
        }
        menuButton.clicked()
        openMenuSettleTimer.start()
    }

    Timer {
        id: openMenuSettleTimer
        interval: 100
        onTriggered: step1_pushAboutThenLicenseDetail()
    }

    function step1_pushAboutThenLicenseDetail() {
        var aboutItem = findInMenu(mainWindow.contentItem, "discoveryMenuItemAbout")
        if (!aboutItem) {
            fail("could not find discoveryMenuItemAbout")
            return
        }
        aboutItem.triggered()
        Qt.callLater(step2_confirmDepthThenConnect)
    }

    function step2_confirmDepthThenConnect() {
        var localStack = findByObjectName(mainWindow.contentItem, "discoveryLocalStack")
        if (!localStack || localStack.depth !== 2) {
            fail("expected local stack depth 2 after pushing About, got " +
                 (localStack ? localStack.depth : "<no stack>"))
            return
        }

        // Simulate a device connecting while the user is mid-browse
        // (e.g. an auto-reconnecting saved device), not a manual tap.
        controller.state = "running"
        Qt.callLater(step3_checkResetWhileConnected)
    }

    function step3_checkResetWhileConnected() {
        var localStack = findByObjectName(mainWindow.contentItem, "discoveryLocalStack")
        if (!localStack) {
            fail("discoveryLocalStack should still exist (DiscoveryScreen instance " +
                 "is retained at the bottom of main.qml's root stack, never destroyed)")
            return
        }
        if (localStack.depth !== 1) {
            fail("expected discoveryLocalStack to reset to depth 1 once connected " +
                 "(controller.state=\"running\"), got " + localStack.depth +
                 " -- a connected user must not be stranded on About/Licenses/LicenseDetail")
            return
        }

        var menuButton = findByObjectName(mainWindow.contentItem, "discoveryMenuButton")
        if (!menuButton || menuButton.visible !== true) {
            fail("discoveryMenuButton should be visible again after the reset, got visible=" +
                 (menuButton ? menuButton.visible : "<not found>"))
            return
        }

        // Now disconnect and confirm DiscoveryScreen shows the device list,
        // not a stale submenu -- the actual user-visible symptom of the bug.
        controller.state = "disconnected"
        Qt.callLater(step4_confirmStillAtRootAfterDisconnect)
    }

    function step4_confirmStillAtRootAfterDisconnect() {
        var localStack = findByObjectName(mainWindow.contentItem, "discoveryLocalStack")
        if (!localStack || localStack.depth !== 1) {
            fail("expected discoveryLocalStack to remain at depth 1 after a disconnect, got " +
                 (localStack ? localStack.depth : "<no stack>"))
            return
        }

        console.log("PASS: connecting mid-browse resets the local menu stack; " +
                     "disconnecting afterward shows the device list, not a stale submenu")
        Qt.exit(0)
    }
}
