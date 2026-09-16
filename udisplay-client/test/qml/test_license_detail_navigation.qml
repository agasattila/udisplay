import QtQuick
import QtQuick.Controls
import "../../qml/LicensesData.js" as LicensesData

/**
 * Regression / coverage test for LicensesScreen -> LicenseDetailScreen
 * navigation (issue #14).
 *
 * test_discovery_menu_navigation.qml already covers menu -> About -> back
 * and menu -> Licenses -> back at the LIST level (it asserts the dependency
 * list has 4 entries, but never taps a row). This test picks up from there:
 * it taps the first dependency row, verifying the `pushDetail` callback
 * DiscoveryScreen.qml wires into LicensesScreen actually pushes
 * LicenseDetailScreen.qml onto the local stack (depth 3) with the right
 * dependency data, that the shared header title updates to the dependency
 * name, and that popping back lands on Licenses (depth 2) -- not Discovery
 * (depth 1) or a broken stack.
 *
 * Loads the real main.qml by URL with the same minimal controller/
 * discoveryModel mocks as test_discovery_menu_navigation.qml.
 *
 * Run headless: `qml -platform offscreen test_license_detail_navigation.qml`.
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

    // Menu/MenuItem content is rendered via the window's Overlay layer, not
    // the normal contentItem tree -- see test_discovery_menu_navigation.qml.
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
            fail("could not find discoveryMenuButton in DiscoveryScreen's header")
            return
        }
        menuButton.clicked()
        openMenuSettleTimer.start()
    }

    Timer {
        id: openMenuSettleTimer
        interval: 100
        onTriggered: step1_pushLicenses()
    }

    function step1_pushLicenses() {
        var licensesItem = findInMenu(mainWindow.contentItem, "discoveryMenuItemLicenses")
        if (!licensesItem) {
            fail("could not find discoveryMenuItemLicenses (menu content should be instantiated after open())")
            return
        }
        licensesItem.triggered()
        Qt.callLater(step2_tapFirstDependency)
    }

    function step2_tapFirstDependency() {
        var localStack = findByObjectName(mainWindow.contentItem, "discoveryLocalStack")
        if (!localStack || localStack.depth !== 2) {
            fail("expected local stack depth 2 after pushing Licenses, got " +
                 (localStack ? localStack.depth : "<no stack>"))
            return
        }

        var depList = findByObjectName(mainWindow.contentItem, "licensesDependencyList")
        if (!depList || depList.count !== 4) {
            fail("expected LicensesScreen's dependency list to have 4 entries, got " +
                 (depList ? depList.count : "<not found>"))
            return
        }

        var firstRow = depList.itemAtIndex(0)
        if (!firstRow) {
            fail("could not find delegate item at index 0 of licensesDependencyList")
            return
        }

        // ItemDelegate is an AbstractButton -- invoking clicked() directly
        // fires the same onClicked handler a real tap would, exercising the
        // pushDetail callback DiscoveryScreen.qml wires into LicensesScreen.
        firstRow.clicked()
        Qt.callLater(step3_checkDetailScreen)
    }

    function step3_checkDetailScreen() {
        var expected = LicensesData.dependencies[0]

        var localStack = findByObjectName(mainWindow.contentItem, "discoveryLocalStack")
        if (!localStack || localStack.depth !== 3) {
            fail("expected local stack depth 3 after tapping a dependency row, got " +
                 (localStack ? localStack.depth : "<no stack>"))
            return
        }

        var title = findByObjectName(mainWindow.contentItem, "discoveryHeaderTitle")
        if (!title || title.text !== expected.name) {
            fail("expected header title '" + expected.name + "' after pushing detail screen, got " +
                 (title ? title.text : "<no title label>"))
            return
        }

        var current = localStack.currentItem
        if (!current || !current.dependency) {
            fail("LicenseDetailScreen's pushed item should expose the tapped dependency")
            return
        }
        if (current.dependency.name !== expected.name ||
            current.dependency.licenseName !== expected.licenseName ||
            current.dependency.copyright !== expected.copyright ||
            current.dependency.licenseText !== expected.licenseText) {
            fail("LicenseDetailScreen's dependency data does not match the tapped row (name=" +
                 current.dependency.name + ")")
            return
        }

        var menuButton = findByObjectName(mainWindow.contentItem, "discoveryMenuButton")
        if (menuButton.visible !== false) {
            fail("discoveryMenuButton should stay hidden while browsing license detail (depth > 1), got visible=" + menuButton.visible)
            return
        }

        var backButton = findByObjectName(mainWindow.contentItem, "discoveryBackButton")
        if (!backButton || backButton.visible !== true) {
            fail("discoveryBackButton should be visible while browsing license detail")
            return
        }
        backButton.clicked()
        Qt.callLater(step4_backAtLicensesList)
    }

    function step4_backAtLicensesList() {
        var localStack = findByObjectName(mainWindow.contentItem, "discoveryLocalStack")
        if (!localStack || localStack.depth !== 2) {
            fail("expected local stack depth 2 after popping back from license detail, got " +
                 (localStack ? localStack.depth : "<no stack>"))
            return
        }

        var title = findByObjectName(mainWindow.contentItem, "discoveryHeaderTitle")
        if (!title || title.text !== "Licenses") {
            fail("expected header title 'Licenses' after popping back from detail screen, got " +
                 (title ? title.text : "<no title label>"))
            return
        }

        console.log("PASS: menu open -> Licenses -> tap dependency -> detail screen -> back -> Licenses list")
        Qt.exit(0)
    }
}
