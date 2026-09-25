// SPDX-License-Identifier: LGPL-3.0-only
// Copyright (c) 2026 Attila Agas

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Controls.Material
import "./"

/* Exclusive-select button group (grid layout). For a directional pad, use
 * the `dpad` container type (DpadWidget.qml) instead — dpad has no
 * selection/setter, so it doesn't belong here (see the dpad-split design
 * doc's Architecture Issue #2).
 *
 * Item fill color, press-darken, shape/radius, and disabled opacity come
 * from the shared ButtonFace.qml component (see its header comment) so
 * items look and behave exactly like a standalone button. Selection is
 * layered on top as a border + bold-label overlay — independent of
 * ButtonFace's own fill/press styling.
 *
 * Selection is device-authoritative (docs/designs/
 * button-group-exclusive-select.md): a press only forwards the item's
 * press/release/click events; `value` changes solely when firmware calls
 * the generated set_<group>()/clear_<group>(), which sends
 * STATE_UPDATE(group, uint8 item widgetId). 0 = no selection. */
Rectangle {
    id: root
    required property int    widgetId
    required property string label
    required property bool   enabled
    required property var    value    /* selected item widgetId; 0 or null = none */
    required property var    props
    /* Every item in this group, as a real model (see
     * WidgetModel::childModel()) — supplied by WidgetDelegate.qml. */
    property var    childModel: null
    /* Resolved style tokens for this row (see WidgetDelegate.qml's
     * _effectiveStyle). Non-required, defaulting to {} — standalone QML
     * tests instantiate this widget directly with no controller/resolver in
     * scope, so color bindings below read undefined fields gracefully (no crash). */
    property var    effectiveStyle: ({})

    /* Flow: items wrap, so there's no single "natural" width for an
     * arbitrary item count — use up to 3 columns worth (Flow's typical wrap
     * point) as a reasonable estimate. */
    implicitWidth: Math.min(childModel ? childModel.rowCount() : 0, 3) * (110 + 8) + 32
    implicitHeight: col.implicitHeight + 24
    color: "transparent"

    Column {
        id: col
        anchors { left: parent.left; right: parent.right; top: parent.top;
                  leftMargin: 16; rightMargin: 16; topMargin: 12 }
        spacing: 8

        Label {
            text: root.label
            color: effectiveStyle.text_muted
            font.pixelSize: 12
            font.capitalization: Font.AllUppercase
            font.letterSpacing: 1
            visible: root.label.length > 0
        }

        /* ── Grid layout ───────────────────────────────────────────────── */
        Flow {
            width: parent.width
            spacing: 8

            Repeater {
                model: root.childModel

                delegate: ButtonFace {
                    required property var model

                    /* Container-level style cascading (docs/designs/
                     * container-style-cascading.md, Revision): this item's
                     * own resolved style — its own explicit style: if set,
                     * else the nearest styled ancestor (typically the
                     * group's own style:), else the app-wide active style.
                     * Computed ONCE here per delegate and threaded to the 3
                     * reads below, mirroring WidgetDelegate.qml's
                     * _effectiveStyle pattern exactly — including its two
                     * dummy dependency reads. effectiveStyleFor() is a
                     * Q_INVOKABLE call, so QML's binding engine tracks
                     * nothing inside it automatically; reading
                     * controller.activeStyle and
                     * controller.widgetModel.generation here forces this
                     * property to re-evaluate on setActiveStyle() and on a
                     * full YAML reload (row indices aren't stable across a
                     * reparse) — without them this would silently go stale,
                     * the same qml-invokable-no-notify class PR9's
                     * adversarial review already caught once for
                     * _childModel. model.row (not the Repeater's local
                     * index) is correct here regardless of which model the
                     * Repeater is bound to (WidgetModel.h's RowRole).
                     * Also colors this item's own ButtonFace fill (Revision
                     * 2: a widget's effective style drives its own chrome). */
                    property var _itemStyle: {
                        controller.activeStyle
                        controller.widgetModel.generation
                        return controller.effectiveStyleFor(model.row)
                    }

                    width:  110; height: 36
                    enabled: root.enabled
                    showLabel: false
                    effectiveStyle: _itemStyle

                    /* Selected ring uses button_text, the on-fill contrast
                     * token: fill is _itemStyle.button, so a button-colored
                     * ring would be invisible against it. */
                    border.color: root.value === model.widgetId
                                  ? _itemStyle.button_text : _itemStyle.border
                    border.width: 1

                    onButtonPressed:  controller.sendButtonPress(model.widgetId)
                    onButtonReleased: controller.sendButtonRelease(model.widgetId)
                    onButtonClicked:  controller.sendButtonClick(model.widgetId)

                    Label {
                        anchors.centerIn: parent
                        text: model.label
                        /* button_text unconditionally — fill is always
                         * _itemStyle.button (via ButtonFace's accentColor)
                         * regardless of selection, so _itemStyle.text (meant
                         * for the old dark "surface" fill) would be
                         * unreadable here. */
                        color: _itemStyle.button_text
                        font.pixelSize: 13
                        font.bold: root.value === model.widgetId
                    }
                }
            }
        }
    }
}
