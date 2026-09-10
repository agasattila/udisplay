// SPDX-License-Identifier: LGPL-3.0-only
// Copyright (c) 2026 Attila Agas

import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Layouts
import "widgets"

Page {
    id: root
    title: controller.deviceName
    background: Rectangle { color: controller.activeStyle.background }

    header: ToolBar {
        Material.background: controller.activeStyle.surface
        RowLayout {
            anchors { fill: parent; leftMargin: 8; rightMargin: 8 }
            ToolButton {
                text: "‹"
                font.pixelSize: 20
                onClicked: controller.disconnectDevice()
            }
            Label {
                Layout.fillWidth: true
                text: controller.deviceName
                font.pixelSize: 16
                font.bold: true
                color: controller.activeStyle.text_heading
                elide: Text.ElideRight
                horizontalAlignment: Text.AlignHCenter
            }
            /* status dot */
            Rectangle {
                width: 8; height: 8
                radius: 4
                color: controller.state === "running" ? "#00d4aa" : "#e74c3c"
            }
        }
    }

    /* Design Mode error overlay — shown in-place when YAML has a parse error.
     * State stays "running" so the navigation stack is unaffected; the developer
     * fixes the file and the overlay clears on the next successful reload. */
    Rectangle {
        anchors.fill: parent
        visible: controller.designErrorString !== ""
        color: controller.activeStyle.background
        z: 1

        ColumnLayout {
            anchors { fill: parent; margins: 24 }
            spacing: 16

            Label {
                text: "Parse Error"
                font.pixelSize: 16
                font.bold: true
                color: "#e74c3c"
            }
            Rectangle {
                Layout.fillWidth: true
                height: 1
                color: "#3d0000"
            }
            ScrollView {
                Layout.fillWidth: true
                Layout.fillHeight: true
                contentWidth: availableWidth
                clip: true
                Label {
                    width: parent.width
                    text: controller.designErrorString
                    color: "#ff6b6b"
                    font.family: "monospace"
                    font.pixelSize: 13
                    wrapMode: Text.WordWrap
                }
            }
            Label {
                text: "Fix the YAML and save — UI will reload automatically."
                color: "#666"
                font.pixelSize: 12
                font.italic: true
            }
        }
    }

    ScrollView {
        anchors.fill: parent
        visible: controller.designErrorString === ""
        contentWidth: availableWidth
        clip: true

        ColumnLayout {
            width: parent.width
            spacing: 0

            Repeater {
                objectName: "topLevelRepeater"
                /* Top-level widgets only. controller.widgetModel itself is
                 * now the full FLAT list (every widget, any nesting depth);
                 * childModel(-1) returns just the rows whose parentId is -1
                 * (top-level) — the same mechanism every other container
                 * uses for its own children, since -1 is the parentId every
                 * top-level widget shares. Binding directly to
                 * controller.widgetModel here would render every nested
                 * child a second time, outside its actual container.
                 *
                 * `controller.widgetModel.generation` is read but unused —
                 * see WidgetModel.h's `generation` doc: childModel() has no
                 * NOTIFY of its own, so without this dependency this binding
                 * would never re-evaluate after a live design-mode reload
                 * resets the model, leaving this Repeater pointing at a
                 * deleted ChildModel (blank screen after any live reload). */
                model: {
                    controller.widgetModel.generation
                    return controller.widgetModel.childModel(-1)
                }

                delegate: Loader {
                    required property int     widgetId
                    required property string  type
                    required property string  label
                    required property bool    enabled
                    required property bool    widgetVisible
                    required property var     value
                    required property var     props
                    /* This delegate's own row in the FLAT WidgetModel — NOT
                     * the same as this Repeater's local `index` (which is
                     * this row's position among top-level widgets only).
                     * toggleSection() takes a flat-model row, so it must
                     * use `row`, not `index`. */
                    required property int     row

                    Layout.fillWidth: true
                    visible: widgetVisible

                    sourceComponent: type === "display"      ? displayComp
                                   : type === "led"          ? ledComp
                                   : type === "rgbled"       ? rgbledComp
                                   : type === "button"       ? buttonComp
                                   : type === "button-group" ? buttonGroupComp
                                   : type === "slider"       ? sliderComp
                                   : type === "toggle"       ? toggleComp
                                   : type === "text"         ? textComp
                                   : type === "dropdown"     ? dropdownComp
                                   : type === "label"        ? labelComp
                                   : type === "separator"    ? separatorComp
                                   : type === "section"      ? sectionComp
                                   : type === "row"          ? rowComp
                                   : type === "grid"         ? gridComp
                                   : type === "dpad"         ? dpadComp

                                   : unknownComp

                    property int     _widgetId: widgetId
                    property string  _label:    label
                    property bool    _enabled:  enabled
                    property var     _value:    value
                    property var     _props:    props

                    Component { id: displayComp;     DisplayWidget     { widgetId: _widgetId; label: _label; enabled: _enabled; value: _value; props: _props } }
                    Component { id: ledComp;         LedWidget         { widgetId: _widgetId; label: _label; enabled: _enabled; value: _value; props: _props } }
                    Component { id: rgbledComp;      RgbLedWidget      { widgetId: _widgetId; label: _label; enabled: _enabled; value: _value } }
                    Component { id: buttonComp;      ButtonWidget      { widgetId: _widgetId; label: _label; enabled: _enabled; props: _props; childModel: { controller.widgetModel.generation; return controller.widgetModel.childModel(row) } } }
                    Component { id: buttonGroupComp; ButtonGroupWidget { widgetId: _widgetId; label: _label; enabled: _enabled; value: _value; props: _props; childModel: { controller.widgetModel.generation; return controller.widgetModel.childModel(row) } } }
                    Component { id: sliderComp;      SliderWidget      { widgetId: _widgetId; label: _label; enabled: _enabled; value: _value; props: _props } }
                    Component { id: toggleComp;      ToggleWidget      { widgetId: _widgetId; label: _label; enabled: _enabled; value: _value } }
                    Component { id: textComp;        TextWidget        { widgetId: _widgetId; label: _label; enabled: _enabled; value: _value; props: _props } }
                    Component { id: dropdownComp;    DropdownWidget    { widgetId: _widgetId; label: _label; enabled: _enabled; value: _value; props: _props } }
                    Component { id: labelComp;       LabelWidget       { props: _props } }
                    Component { id: separatorComp;   SeparatorWidget   {} }
                    /* childModel keyed by `row` (this widget's own flat-list
                     * index), NOT widgetId — row/grid/section/dpad
                     * containers all share widgetId 0, so widgetId-keyed
                     * lookup would collide every top-level container into
                     * one shared child model.
                     *
                     * `controller.widgetModel.generation` is read but unused
                     * in each binding below — see WidgetModel.h's `generation`
                     * doc: childModel() has no NOTIFY of its own, so without
                     * this dependency these bindings would never re-evaluate
                     * after a live design-mode reload resets the model,
                     * leaving each container pointing at a deleted ChildModel. */
                    Component {
                        id: sectionComp
                        SectionWidget {
                            label: _label
                            props: _props
                            onToggleClicked: controller.widgetModel.toggleSection(row)
                            childModel: {
                                controller.widgetModel.generation
                                return controller.widgetModel.childModel(row)
                            }
                        }
                    }
                    Component { id: rowComp;         RowWidget         { label: _label; props: _props; childModel: { controller.widgetModel.generation; return controller.widgetModel.childModel(row) } } }
                    Component { id: gridComp;        GridWidget        { label: _label; props: _props; childModel: { controller.widgetModel.generation; return controller.widgetModel.childModel(row) } } }
                    Component { id: dpadComp;        DpadWidget        { label: _label; props: _props; childModel: { controller.widgetModel.generation; return controller.widgetModel.childModel(row) } } }
                    Component { id: unknownComp;     Item { height: 0 } }
                }
            }

            /* bottom padding */
            Item { Layout.fillWidth: true; height: 24 }
        }
    }
}
