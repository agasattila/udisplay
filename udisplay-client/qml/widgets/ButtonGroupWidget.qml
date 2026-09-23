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
 * ButtonFace's own fill/press styling — since `value` is currently
 * unimplemented on the firmware side (see the design doc); the overlay
 * stays inert until a real setter exists but the visuals are already
 * correct for when it does. */
Rectangle {
    id: root
    required property int    widgetId
    required property string label
    required property bool   enabled
    required property var    value    /* active item widgetId or null */
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
                     * Repeater is bound to (WidgetModel.h's RowRole). */
                    property var _itemStyle: {
                        controller.activeStyle
                        controller.widgetModel.generation
                        return controller.effectiveStyleFor(model.row)
                    }

                    width:  110; height: 36
                    enabled: root.enabled
                    showLabel: false

                    border.color: root.value === model.widgetId
                                  ? _itemStyle.button : _itemStyle.border
                    border.width: 1

                    onButtonPressed:  controller.sendButtonPress(model.widgetId)
                    onButtonReleased: controller.sendButtonRelease(model.widgetId)
                    onButtonClicked:  controller.sendButtonClick(model.widgetId)

                    Label {
                        anchors.centerIn: parent
                        text: model.label
                        /* button_text unconditionally — fill is always
                         * activeStyle.button (via ButtonFace's hardwired
                         * accentColor, unaffected by _itemStyle — see the D1
                         * invariant) regardless of selection or per-item
                         * style, so activeStyle.text (meant for the old dark
                         * "surface" fill) would be unreadable here. Only the
                         * label text color and the selection border read
                         * _itemStyle. */
                        color: _itemStyle.button_text
                        font.pixelSize: 13
                        font.bold: root.value === model.widgetId
                    }
                }
            }
        }
    }
}
