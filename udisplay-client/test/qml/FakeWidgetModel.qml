// SPDX-License-Identifier: LGPL-3.0-only
// Copyright (c) 2026 Attila Agas

import QtQuick

/* Minimal stand-in for the real C++ WidgetModel, used only by these headless
 * QML layout tests. WidgetDelegate.qml unconditionally calls
 * `controller.widgetModel.childModel(model.row)` for every delegate it
 * creates (see its header comment) — a test file that instantiates
 * RowWidget/GridWidget/ButtonWidget/ButtonGroupWidget/DpadWidget (all of
 * which use WidgetDelegate internally for their own children) needs a
 * `controller.widgetModel` object that responds to childModel(key), even if
 * the test has no actual nested containers.
 *
 * Usage: give each container ListModel a unique string `key` and register it;
 * WidgetDelegate looks children up by whatever value the fixture data put in
 * that row's `row` field (a real WidgetModel uses a numeric flat-row index —
 * these tests use descriptive strings instead, since there's no real flat
 * list, just hand-built ListModel fixtures. Any value works as long as the
 * childModel() lookup key matches the item's own `row` field consistently). */
QtObject {
    property var _empty: ListModel {}
    property var _registry: ({})

    function register(key, model) {
        _registry[key] = model
    }

    function childModel(key) {
        return (key in _registry) ? _registry[key] : _empty
    }
}
