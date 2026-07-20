// SPDX-License-Identifier: LGPL-3.0-only
// Copyright (c) 2026 Attila Agas

import QtQuick
import QtQuick.Controls
import "./"

/* Momentary push button. Sends button_press EVENT on tap.
 * If props.items is non-empty, child widgets are rendered on the button face
 * (label is hidden). If empty, the label text is shown as the face text.
 *
 * Face content is ONE embedded compact RowWidget bound to props.items —
 * unconditionally, no shape-sniffing, no face:/children: distinction.
 *
 * The face RowWidget is loaded via Qt.resolvedUrl (a dynamic Loader{source:},
 * NOT a static `RowWidget { ... }` instantiation) for the same reason
 * WidgetDelegate.qml's own rowComp/gridComp use that pattern: WidgetDelegate
 * statically references ButtonWidget (its buttonComp Component), and
 * RowWidget statically uses WidgetDelegate as its Repeater delegate — a
 * static `RowWidget { }` reference here would close a 3-hop cycle
 * (WidgetDelegate -> ButtonWidget -> RowWidget -> WidgetDelegate). Qt's
 * static-type cycle detector does not follow Qt.resolvedUrl's runtime string
 * argument, so the dynamic form breaks the cycle exactly as it already does
 * for row/grid nesting in WidgetDelegate.qml.
 *
 * Uses relative import "./" per Android qmlcachegen requirement — must NOT
 * import the module URI (would return empty type registry on Android).    */
Rectangle {
    id: root
    required property int    widgetId
    required property string label
    required property bool   enabled
    required property var    props

    implicitWidth: Math.max(btn.hasChildren && facesRowLoader.item ? facesRowLoader.item.implicitWidth + 12: faceText.implicitWidth + 12, 64)
    implicitHeight: Math.max(btn.hasChildren && facesRowLoader.item ? facesRowLoader.item.implicitHeight + 12: faceText.implicitHeight + 12 , btn.btnShape === "rect" ? 40 : 64)

    color:  "transparent"

    /* The button itself */
    Rectangle {
        id: btn

        property string btnShape:    props["shape"] !== undefined ? props["shape"] : "rect"
        property color  accentColor: controller.activeStyle.button
        property bool   btnPressed:  false
        property var    childItems:  props["items"] || []
        property bool   hasChildren: childItems.length > 0

        anchors.centerIn: parent

        width:  btnShape === "rect" ? parent.width - 12 : Math.min(parent.width,parent.height)
        height: btnShape === "rect" ? parent.height - 12 : Math.min(parent.width,parent.height)
        radius: btnShape === "circle" ? Math.min(width,height) / 2 : btnShape === "square" ? 4 : 8

        color:  btnPressed ? Qt.darker(accentColor, 1.3) : accentColor
        opacity: root.enabled ? 1.0 : 0.3

        /* Face label — shown only when there are no child widgets */
        Text {
            id: faceText
            anchors.centerIn: parent
            text:  root.label
            color: controller.activeStyle.button_text
            font.pixelSize: 14
            font.bold: true
            visible: !btn.hasChildren && root.label.length > 0
        }

        /* Child widgets on button face — one real, compact RowWidget,
         * loaded dynamically (see header comment for why). */
        Loader {
            id: facesRowLoader
            anchors { left: parent.left; right: parent.right;
                      top: parent.top; bottom: parent.bottom;
                      topMargin: 6; bottomMargin: 6 }
            active: btn.hasChildren
            source: Qt.resolvedUrl("RowWidget.qml")
            onLoaded: {
                item.compact = true
                item.props = Qt.binding(function() { return { "items": btn.childItems } })
            }
        }

        MouseArea {
            anchors.fill: parent
            enabled: root.enabled
            onPressed: {
                btn.btnPressed = true
                controller.sendButtonPress(root.widgetId)
            }
            onReleased: {
                btn.btnPressed = false
                controller.sendButtonRelease(root.widgetId)
                if (containsMouse)
                    controller.sendButtonClick(root.widgetId)
            }
            onCanceled: {
                btn.btnPressed = false
                controller.sendButtonRelease(root.widgetId)
            }
        }
    }
}
