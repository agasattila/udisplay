# SPDX-License-Identifier: MPL-2.0
# Copyright (c) 2026 Attila Agas

"""
Widget ID assignment for uDisplay.

Every widget gets a widget ID (issue #43) — leaves, containers (section, row,
grid, dpad), decorations (label, separator), button face children and
button-group items alike — so SET_PROPERTY / RESET_PROPERTY can target any
node of the widget tree. Dropdown items are options, not widgets, and get no
ID.

IDs are assigned alphabetically by ID path, starting at 0x10. A widget's ID
path is the chain of YAML keys from the top level down to it: every widget
with children (section, row, grid, dpad, a button face, a button-group's
items) is a namespace for them, so `settings.advanced.rate` is the `rate`
slider in the `advanced` row of the `settings` section. Widget keys only
need to be unique among their siblings.

See docs/protocol.md § Widget ID Assignment.
"""
from __future__ import annotations

from typing import NamedTuple

ID_START = 0x10
ID_MAX = 0xFF
MAX_WIDGETS = ID_MAX - ID_START + 1  # 240

# Layout containers. Single source of truth for validate.py and the
# backends (TODO-007).
CONTAINER_TYPES = frozenset({"section", "row", "grid", "dpad"})


class WidgetNode(NamedTuple):
    """One widget of the YAML tree: its own key, its full ID path, and its
    children (widget-map children and button-group items) in YAML
    declaration order."""
    key: str
    path: str
    children: tuple


def widget_tree(widgets: dict, prefix: str = "") -> list:
    """The widget tree as WidgetNodes, in YAML declaration order — every
    node that gets an ID in _collect(), shaped the way the generated
    object APIs (C++ `UDisplay`, MicroPython `UI`) nest their members."""
    nodes: list = []
    for key, widget in widgets.items():
        if not isinstance(widget, dict):
            continue
        path = f"{prefix}.{key}" if prefix else key
        children = widget_tree(widget.get("widgets", {}), path)
        if widget.get("type", "") == "button-group":
            children += [WidgetNode(item_key, f"{path}.{item_key}", ())
                         for item_key in widget.get("items", {})]
        nodes.append(WidgetNode(key, path, tuple(children)))
    return nodes


def walk(nodes) -> list:
    """Every node of a widget_tree(), parents before children."""
    result: list = []
    for node in nodes:
        result.append(node)
        result.extend(walk(node.children))
    return result


def _collect(widgets: dict) -> list[str]:
    """Every widget's ID path: `<parent path>.<key>`, or `<key>` at the top
    level. button-group items get `<group>.<item>`; dropdown items are
    options, not widgets, and get nothing."""
    return [node.path for node in walk(widget_tree(widgets))]


def collect_types(widgets: dict, prefix: str = "") -> dict[str, str]:
    """
    Return a mapping of id_path → type_str for every widget (same paths as
    _collect()).

    Text mode is baked in: 'text-rw' or 'text-ro'.
    button-group items are typed as 'button-group-item'.
    Containers and decorations report their own type ('section', 'row',
    'label', ...); backends generate no typed setter/handler for them.
    """
    result: dict[str, str] = {}
    for key, widget in widgets.items():
        if not isinstance(widget, dict):
            continue
        wtype = widget.get("type", "")

        path = f"{prefix}.{key}" if prefix else key

        if wtype == "text":
            mode = widget.get("mode", "ro")
            result[path] = f"text-{mode}"
        else:
            result[path] = wtype

        result.update(collect_types(widget.get("widgets", {}), path))

        if wtype == "button-group":
            for item_key in widget.get("items", {}):
                result[f"{path}.{item_key}"] = "button-group-item"

    return result


def collect_dropdown_items(widgets: dict, prefix: str = "") -> dict[str, list[tuple[str, str]]]:
    """
    Return a mapping of dropdown id_path → [(item_key, item_label), ...] in
    declaration order. Used by build.py to emit per-item index constants.
    """
    result: dict[str, list[tuple[str, str]]] = {}
    for key, widget in widgets.items():
        if not isinstance(widget, dict):
            continue
        wtype = widget.get("type", "")
        path = f"{prefix}.{key}" if prefix else key
        result.update(collect_dropdown_items(widget.get("widgets", {}), path))
        if wtype == "dropdown":
            items = widget.get("items", {})
            # str() guards against YAML 1.1 boolean/int key coercion (e.g. `off:` → False)
            result[path] = [(str(k), str(v)) for k, v in items.items()]

    return result


def assign(widgets: dict) -> dict[str, int]:
    """
    Return a mapping of id_path → widget_id for every widget.

    Raises ValueError if widget count exceeds 240 (containers and decorations
    count too), or if two widgets resolve to the same id_path. Valid YAML
    can't produce a duplicate (every parent's key is a path segment of its
    children, and the schema forbids '.' in keys); the check guards input
    that skipped schema validation.
    """
    paths = sorted(_collect(widgets))
    if len(paths) > MAX_WIDGETS:
        raise ValueError(
            f"Widget count {len(paths)} exceeds maximum of {MAX_WIDGETS} "
            f"(IDs 0x{ID_START:02X}–0x{ID_MAX:02X})."
        )
    seen: set[str] = set()
    for path in paths:
        if path in seen:
            raise ValueError(
                f"Duplicate widget ID path '{path}' — two widgets resolve to the same "
                f"protocol ID. Widget keys must not contain '.'."
            )
        seen.add(path)
    return {path: ID_START + i for i, path in enumerate(paths)}
