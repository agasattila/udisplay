// SPDX-License-Identifier: LGPL-3.0-only
// Copyright (c) 2026 Attila Agas

import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Layouts
import "./"

/* Section header, followed by its own children stacked full-width below it.
 *
 * Pre-flattening, a section's children were genuinely flat top-level
 * siblings in WidgetModel's own list — a separate `sectionOwnerRow` field
 * (not the tree's own parent/child shape) tracked collapse-visibility
 * ownership independently of rendering position. This flattening refactor
 * folded both into one `parentId` (children now structurally nest under
 * their section's own row, matching every other container), which fixed
 * collapse-visibility but silently broke rendering: nothing walked
 * `childModel(sectionRow)` to actually display those children, since
 * SectionWidget previously had none. Fixed by giving SectionWidget the same
 * childModel + WidgetDelegate-Repeater shape RowWidget.qml uses, just
 * stacked vertically instead of horizontally, matching the original
 * full-width "normal item" appearance.
 *
 * Collapse itself needs no filtering here — each child's own `visible`
 * binding (WidgetDelegate.qml → `model.widgetVisible`) already reflects
 * WidgetModel's parentId-chain ancestor walk (WidgetModel.cpp's VisibleRole),
 * so simply instantiating every childModel row and letting its own
 * visibility binding do the hiding is correct.
 *
 * props.collapsible: bool — show chevron toggle
 * props.collapsed:   bool — current collapsed state (runtime, from model)
 * childModel: every child of this section, as a real model (see
 * WidgetModel::childModel()) — supplied by WidgetDelegate.qml's sectionComp.
 *
 * label: non-required (unlike an earlier version of this file) — WidgetDelegate.qml
 * loads this component via Qt.resolvedUrl (a dynamic Loader{source:}, not a
 * static `SectionWidget { }` instantiation, to avoid the same WidgetDelegate<->
 * container cycle rowComp/gridComp already sidestep this way — see
 * WidgetDelegate.qml's header comment). A Loader{source:}-created object gets
 * its properties set via a live binding in onLoaded, AFTER construction — a
 * required property has no way to be satisfied at that point (required
 * properties must be satisfied at creation), so label must default like every
 * other RowWidget.qml/GridWidget.qml container property that goes through this
 * same dynamic-Loader path. */
Rectangle {
    id: root
    property string label: ""
    property var props: ({})
    property var childModel: null
    /* Resolved style tokens for this row (see WidgetDelegate.qml's
     * _effectiveStyle). Non-required, defaulting to {} — matches props/
     * childModel above (set via live onLoaded binding, same dynamic-Loader
     * reasoning). Colors this section's OWN header/border chrome. Also
     * cascades to children now (docs/designs/container-style-cascading.md)
     * — but that happens entirely through the generic
     * WidgetDelegate.qml -> DeviceController::effectiveStyleFor(model.row)
     * path each child resolves for itself; this property here is only
     * this section's own chrome color, unrelated to that cascade. */
    property var effectiveStyle: ({})

    signal toggleClicked()

    implicitWidth: sectionLabel.implicitWidth + 40 + 32
    implicitHeight: col.implicitHeight
    color: "transparent"

    ColumnLayout {
        id: col
        anchors { left: parent.left; right: parent.right }
        spacing: 0

        Rectangle {
            Layout.fillWidth: true
            implicitHeight: 36
            color: effectiveStyle.surface

            RowLayout {
                anchors { fill: parent; leftMargin: 16; rightMargin: 16 }
                spacing: 8

                Label {
                    id: sectionLabel
                    text: root.label.toUpperCase()
                    color: effectiveStyle.accent
                    font.pixelSize: 11
                    font.letterSpacing: 1.5
                    font.bold: true
                }

                Rectangle {
                    Layout.fillWidth: true
                    height: 1
                    color: effectiveStyle.line
                }

                Label {
                    visible: root.props.collapsible === true
                    text: root.props.collapsed === true ? "▶" : "▼"
                    color: effectiveStyle.accent
                    font.pixelSize: 11
                }
            }

            MouseArea {
                anchors.fill: parent
                enabled: root.props.collapsible === true
                onClicked: root.toggleClicked()
            }
        }

        Repeater {
            id: repeater
            model: root.childModel

            delegate: WidgetDelegate {
                required property var model
                Layout.fillWidth: true
            }
        }
    }
}
