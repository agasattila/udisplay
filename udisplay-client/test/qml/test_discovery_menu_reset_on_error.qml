import QtQuick
import QtQuick.Controls

/**
 * Regression test (ship adversarial review): a connection attempt that
 * fails while the user is browsing About/Licenses/LicenseDetail must also
 * reset DiscoveryScreen's hamburger-menu local stack, not just a
 * successful connect.
 *
 * The error banner (discoveryErrorBanner, bound to controller.errorString)
 * lives only in discoveryContentComponent at the bottom of localStack --
 * not in About/Licenses/LicenseDetail. Before this fix, DiscoveryScreen's
 * Connections{target: controller}.onStateChanged handler only reset
 * localStack on controller.state === "running", so a failed connection
 * attempt (controller.state becomes "error") left the user stranded on a
 * static About/Licenses screen with no visible indication anything
 * happened -- the error banner they'd need to see was several pops away.
 *
 * Run headless: `qml -platform offscreen test_discovery_menu_reset_on_error.qml`.
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
        onTriggered: step1_pushAbout()
    }

    function step1_pushAbout() {
        var aboutItem = findInMenu(mainWindow.contentItem, "discoveryMenuItemAbout")
        if (!aboutItem) {
            fail("could not find discoveryMenuItemAbout")
            return
        }
        aboutItem.triggered()
        Qt.callLater(step2_confirmDepthThenFailConnection)
    }

    function step2_confirmDepthThenFailConnection() {
        var localStack = findByObjectName(mainWindow.contentItem, "discoveryLocalStack")
        if (!localStack || localStack.depth !== 2) {
            fail("expected local stack depth 2 after pushing About, got " +
                 (localStack ? localStack.depth : "<no stack>"))
            return
        }

        // Simulate a connection attempt that fails while the user is
        // browsing About -- not a manual back-then-retry.
        controller.errorString = "Connection refused"
        controller.state = "error"
        Qt.callLater(step3_checkResetOnError)
    }

    function step3_checkResetOnError() {
        var localStack = findByObjectName(mainWindow.contentItem, "discoveryLocalStack")
        if (!localStack || localStack.depth !== 1) {
            fail("expected discoveryLocalStack to reset to depth 1 once the connection " +
                 "attempt failed (controller.state=\"error\"), got " +
                 (localStack ? localStack.depth : "<no stack>") +
                 " -- a failed connection must not strand the user on About/Licenses/" +
                 "LicenseDetail with no visible error")
            return
        }

        var errorBanner = findByObjectName(mainWindow.contentItem, "discoveryErrorBanner")
        if (!errorBanner || errorBanner.visible !== true) {
            fail("expected discoveryErrorBanner to be visible after the reset, got visible=" +
                 (errorBanner ? errorBanner.visible : "<not found>"))
            return
        }

        console.log("PASS: a failed connection attempt while browsing About resets the " +
                     "local menu stack and surfaces the error banner")
        Qt.exit(0)
    }
}
