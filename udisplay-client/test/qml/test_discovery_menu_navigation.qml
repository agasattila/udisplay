import QtQuick
import QtQuick.Controls

/**
 * Regression / coverage test for the DiscoveryScreen hamburger menu
 * (issue #14: About + Licenses).
 *
 * DiscoveryScreen.qml pushes About/Licenses onto a StackView LOCAL to
 * DiscoveryScreen itself (objectName "discoveryLocalStack"), never onto
 * main.qml's root `stack` -- that root stack's Connections handler assumes
 * `stack.depth === 1` means "showing DiscoveryScreen" to decide when to
 * auto-push DeviceScreen on connect. This test verifies the full round
 * trip through the local stack: open menu -> About -> back -> open menu ->
 * Licenses -> back -> menu still opens -- not just independent screen
 * loads, which would miss a broken back-navigation wiring.
 *
 * Loads the real main.qml by URL (same technique as
 * test_version_label_visibility.qml) with minimal controller/discoveryModel
 * mocks -- just enough for main.qml, DiscoveryScreen.qml, AboutScreen.qml
 * and LicensesScreen.qml to construct without error.
 *
 * Run headless: `qml -platform offscreen test_discovery_menu_navigation.qml`.
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

    // Menu/MenuItem content is NOT part of the contentItem tree -- Qt Quick
    // Controls renders open Popups (which is what Menu is) via the window's
    // Overlay layer, reachable through the Overlay attached property on any
    // item inside the window. Only searches once the menu has actually been
    // opened at least once (Popup content is instantiated lazily).
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

    // QtQuick Controls Popup content (Menu's MenuItems) is instantiated
    // lazily on first open -- the MenuItem objects don't exist in the item
    // tree at all until menu.open() has actually run, so opening and
    // finding items are two separate steps, not one.
    function step0_openMenu() {
        var menuButton = findByObjectName(mainWindow.contentItem, "discoveryMenuButton")
        if (!menuButton) {
            fail("could not find discoveryMenuButton in DiscoveryScreen's header")
            return
        }
        if (menuButton.visible !== true) {
            fail("discoveryMenuButton should be visible at local stack depth 1, got visible=" + menuButton.visible)
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
            fail("could not find discoveryMenuItemAbout (menu content should be instantiated after open())")
            return
        }
        aboutItem.triggered()
        Qt.callLater(step2_checkAboutScreen)
    }

    function step2_checkAboutScreen() {
        var localStack = findByObjectName(mainWindow.contentItem, "discoveryLocalStack")
        if (!localStack) {
            fail("could not find discoveryLocalStack")
            return
        }
        if (localStack.depth !== 2) {
            fail("expected local stack depth 2 after pushing About, got " + localStack.depth)
            return
        }

        var title = findByObjectName(mainWindow.contentItem, "discoveryHeaderTitle")
        if (!title || title.text !== "About") {
            fail("expected header title 'About' after pushing About screen, got " +
                 (title ? title.text : "<no title label>"))
            return
        }

        var versionLabel = findByObjectName(mainWindow.contentItem, "aboutVersionLabel")
        if (!versionLabel || versionLabel.text !== Qt.application.version) {
            fail("AboutScreen's version label should show Qt.application.version, got " +
                 (versionLabel ? versionLabel.text : "<not found>"))
            return
        }

        var menuButton = findByObjectName(mainWindow.contentItem, "discoveryMenuButton")
        if (menuButton.visible !== false) {
            fail("discoveryMenuButton should be hidden while browsing About (depth > 1), got visible=" + menuButton.visible)
            return
        }

        var backButton = findByObjectName(mainWindow.contentItem, "discoveryBackButton")
        if (!backButton || backButton.visible !== true) {
            fail("discoveryBackButton should be visible while browsing About")
            return
        }
        backButton.clicked()
        Qt.callLater(step3_backAtRootThenOpenLicenses)
    }

    function step3_backAtRootThenOpenLicenses() {
        var localStack = findByObjectName(mainWindow.contentItem, "discoveryLocalStack")
        if (localStack.depth !== 1) {
            fail("expected local stack depth 1 after popping back from About, got " + localStack.depth)
            return
        }

        var menuButton = findByObjectName(mainWindow.contentItem, "discoveryMenuButton")
        if (menuButton.visible !== true) {
            fail("discoveryMenuButton should be visible again after popping back to Discovery")
            return
        }
        // Re-open explicitly (not just re-locate the item) -- this is the
        // "menu still opens" half of the round trip, not just a lookup.
        menuButton.clicked()
        reopenMenuSettleTimer.start()
    }

    Timer {
        id: reopenMenuSettleTimer
        interval: 100
        onTriggered: step3b_pushLicenses()
    }

    function step3b_pushLicenses() {
        var licensesItem = findInMenu(mainWindow.contentItem, "discoveryMenuItemLicenses")
        if (!licensesItem) {
            fail("could not find discoveryMenuItemLicenses on menu re-open")
            return
        }
        licensesItem.triggered()
        Qt.callLater(step4_checkLicensesScreen)
    }

    function step4_checkLicensesScreen() {
        var localStack = findByObjectName(mainWindow.contentItem, "discoveryLocalStack")
        if (localStack.depth !== 2) {
            fail("expected local stack depth 2 after pushing Licenses, got " + localStack.depth)
            return
        }

        var title = findByObjectName(mainWindow.contentItem, "discoveryHeaderTitle")
        if (!title || title.text !== "Licenses") {
            fail("expected header title 'Licenses' after pushing Licenses screen, got " +
                 (title ? title.text : "<no title label>"))
            return
        }

        var depList = findByObjectName(mainWindow.contentItem, "licensesDependencyList")
        if (!depList || depList.count !== 4) {
            fail("expected LicensesScreen's dependency list to have 4 entries, got " +
                 (depList ? depList.count : "<not found>"))
            return
        }

        var backButton = findByObjectName(mainWindow.contentItem, "discoveryBackButton")
        backButton.clicked()
        Qt.callLater(step5_backAtRootMenuStillOpens)
    }

    function step5_backAtRootMenuStillOpens() {
        var localStack = findByObjectName(mainWindow.contentItem, "discoveryLocalStack")
        if (localStack.depth !== 1) {
            fail("expected local stack depth 1 after popping back from Licenses, got " + localStack.depth)
            return
        }

        var menuButton = findByObjectName(mainWindow.contentItem, "discoveryMenuButton")
        if (menuButton.visible !== true) {
            fail("discoveryMenuButton should be visible again after popping back to Discovery")
            return
        }

        console.log("PASS: menu open -> About -> back -> menu open -> Licenses -> back -> menu still opens")
        Qt.exit(0)
    }
}
