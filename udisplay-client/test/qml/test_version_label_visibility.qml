import QtQuick
import QtQuick.Controls

/**
 * Regression test for the version label / "connected" status dot overlap.
 *
 * main.qml anchors its version label to the window's top-right corner,
 * z-stacked above the StackView so it survives Discovery <-> Device screen
 * transitions. DeviceScreen.qml's header ToolBar puts a "connected" status
 * dot in that same corner (the preceding title Label uses Layout.fillWidth,
 * pushing the dot to the far right) -- so once a device connects, the two
 * would render on top of each other.
 *
 * Fix: main.qml's version Label binds `visible: controller.state !== "running"`,
 * hiding it exactly when DeviceScreen (and its status dot) is on screen.
 *
 * Loads the real main.qml by URL (its filename is lowercase, so it can't be
 * imported as a type the way DeviceScreen/DiscoveryScreen are in the other
 * qml/ tests) with minimal controller/discoveryModel mocks -- just enough
 * for main.qml, DiscoveryScreen.qml and DeviceScreen.qml to construct
 * without error.
 *
 * Run headless: `qml -platform offscreen test_version_label_visibility.qml`.
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
        onTriggered: {
            var label = findByObjectName(mainWindow.contentItem, "versionLabel")
            if (!label) {
                fail("could not find versionLabel in main.qml's content tree")
                return
            }

            if (label.visible !== true) {
                fail("versionLabel should be visible on the discovery screen " +
                     "(controller.state=\"disconnected\"), got visible=" + label.visible)
                return
            }

            controller.state = "running"
            checkAfterConnect.start()
        }
    }

    Timer {
        id: checkAfterConnect
        interval: 300
        onTriggered: {
            var label = findByObjectName(mainWindow.contentItem, "versionLabel")
            if (label.visible !== false) {
                fail("versionLabel should be hidden once connected " +
                     "(controller.state=\"running\") to avoid overlapping " +
                     "DeviceScreen's status dot, got visible=" + label.visible)
                return
            }

            console.log("PASS: versionLabel visible on discovery screen, hidden on device screen")
            Qt.exit(0)
        }
    }
}
