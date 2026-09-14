// SPDX-License-Identifier: LGPL-3.0-only
// Copyright (c) 2026 Attila Agas

import QtQuick

/* Horizontal rule — purely decorative, no protocol interaction.
 * No content to measure (genuinely fixed visual size, unlike the other
 * leaf widgets) — a small fallback implicitWidth is fine since this is
 * always meant to stretch via Layout.fillWidth in real usage. */
Rectangle {
    /* Resolved style tokens for this row (see WidgetDelegate.qml's
     * _effectiveStyle). Non-required, defaulting to {} — standalone QML
     * tests instantiate this widget directly with no controller/resolver in
     * scope, so color bindings below read undefined fields gracefully (no crash). */
    property var effectiveStyle: ({})

    implicitWidth: 40
    implicitHeight: 1
    color: effectiveStyle.line
}
