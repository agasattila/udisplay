// SPDX-License-Identifier: LGPL-3.0-only
// Copyright (c) 2026 Attila Agas

import QtQuick
import QtQuick.Controls
import "./"

/* Momentary push button. Sends button_press EVENT on tap.
 * If props.items is non-empty, child widgets are rendered on the button face
 * (label is hidden). If empty, the label text is shown as the face text.
 *
 * Fill color, press-darken, shape-driven radius, and disabled opacity live in
 * the shared ButtonFace.qml component so button-group items can look and
 * behave identically — see ButtonFace.qml's header comment.
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

    property string btnShape:    props["shape"] !== undefined ? props["shape"] : "rect"
    property var    childItems:  props["items"] || []
    property bool   hasChildren: childItems.length > 0

    implicitWidth: Math.max(root.hasChildren && facesRowLoader.item ? facesRowLoader.item.implicitWidth + 12: face.labelImplicitWidth + 12, 64)
    implicitHeight: Math.max(root.hasChildren && facesRowLoader.item ? facesRowLoader.item.implicitHeight + 12: face.labelImplicitHeight + 12 , root.btnShape === "rect" ? 40 : 64)

    color:  "transparent"

    /* The button itself */
    ButtonFace {
        id: face

        anchors.centerIn: parent

        width:  root.btnShape === "rect" ? parent.width - 12 : Math.min(parent.width,parent.height)
        height: root.btnShape === "rect" ? parent.height - 12 : Math.min(parent.width,parent.height)

        shape:     root.btnShape
        label:     root.label
        enabled:   root.enabled
        showLabel: !root.hasChildren

        onButtonPressed:  controller.sendButtonPress(root.widgetId)
        onButtonReleased: controller.sendButtonRelease(root.widgetId)
        onButtonClicked:  controller.sendButtonClick(root.widgetId)

        /* Child widgets on button face — one real, compact RowWidget,
         * loaded dynamically (see header comment for why). */
        Loader {
            id: facesRowLoader
            anchors { left: parent.left; right: parent.right;
                      top: parent.top; bottom: parent.bottom;
                      topMargin: 6; bottomMargin: 6 }
            active: root.hasChildren
            source: Qt.resolvedUrl("RowWidget.qml")
            onLoaded: {
                item.compact = true
                item.props = Qt.binding(function() { return { "items": root.childItems } })
            }
        }
    }
}
