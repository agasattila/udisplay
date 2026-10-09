# SPDX-License-Identifier: MPL-2.0
# Copyright (c) 2026 Attila Agas

"""
Widget ID assignment and generated-name paths for uDisplay.

Every widget gets a widget ID (issue #43) — leaves, containers (section, row,
grid, dpad), decorations (label, separator), button face children and
button-group items alike — so SET_PROPERTY / RESET_PROPERTY can target any
node of the widget tree. Dropdown items are options, not widgets, and get no
ID.

A widget has two paths:

- Its **structural path** (`path`), the chain of YAML keys from the top
  level down to it: `settings.advanced.rate`. IDs are assigned
  alphabetically by structural path, starting at 0x10, identically in
  udisplay-client's YamlParser.cpp. This is the wire contract.
- Its **name path** (`name_path`), which the generated firmware APIs use
  for identifiers (`WIDGET_ID_*`, `ui.<member>`). A section, row or grid is
  transparent unless it sets `namespace: true`: its own key is not part of
  its descendants' name paths, so moving a widget between such containers
  changes no generated identifier. A flagged section/row/grid and every
  compound widget (dpad, button-group, a button with face children) is a
  namespace for its children. Name paths must be unique
  (name_path_collisions()).

    YAML                               structural path            name path
    main (section)                     main                       main
      row_temp (row)                   main.row_temp              row_temp
        temp_display                   main.row_temp.temp_display temp_display
    indoor (section, namespace: true)  indoor                     indoor
      temperature                      indoor.temperature         indoor.temperature

See docs/protocol.md § Widget ID Assignment.
"""
from __future__ import annotations

from typing import NamedTuple

ID_START = 0x10
ID_MAX = 0xFF
MAX_WIDGETS = ID_MAX - ID_START + 1  # 240

# Maximum widget nesting depth (issue #24, TODO-036): the number of keys in a
# widget's structural path, so a top-level widget is at depth 1 and a
# button-group item one deeper than its group. udisplay-client's
# YamlParser.cpp rejects the same documents (kMaxWidgetNestingDepth).
# MAX_WIDGETS bounds how many widgets a document has, not how deep they
# nest, and every walk over the tree recurses once per level.
MAX_NESTING_DEPTH = 10

# Layout containers. Single source of truth for validate.py and the
# backends (TODO-007).
CONTAINER_TYPES = frozenset({"section", "row", "grid", "dpad"})

# Containers that take `namespace: true|false` (default false = transparent
# to generated names). dpad, button-group and a button's face are always
# namespaces: their children reuse the same keys from one instance to the
# next (`up`, `off`).
NAMESPACE_FLAG_TYPES = frozenset({"section", "row", "grid"})


class WidgetNode(NamedTuple):
    """One widget of the YAML tree: its own key, its structural path (wire
    ID), its children (widget-map children and button-group items) in YAML
    declaration order, its type as collect_types() reports it, its name path
    (generated identifiers), whether it is transparent to its children's
    name paths, and its YAML mapping."""
    key: str
    path: str
    children: tuple
    type: str
    name_path: str
    transparent: bool
    widget: dict


def _type_str(widget: dict) -> str:
    wtype = widget.get("type", "")
    if wtype == "text":
        return f"text-{widget.get('mode', 'ro')}"
    return wtype


def nesting_depth_error(widgets: dict) -> str | None:
    """The error text for the first widget, in walk() order, nested deeper
    than MAX_NESTING_DEPTH, or None if none is. Walks the same edges as
    widget_tree() (every `widgets:` map, plus button-group items), but
    iteratively, so it is safe to run on input that would exhaust the stack
    of the recursive walks: run it before any of them."""
    def entries(children, prefix: str, depth: int) -> list:
        if not isinstance(children, dict):
            return []
        return [(f"{prefix}.{key}" if prefix else key, widget, depth)
                for key, widget in children.items() if isinstance(widget, dict)]

    # Reversed onto the stack, so entries pop in declaration order.
    stack = entries(widgets, "", 1)[::-1]
    while stack:
        path, widget, depth = stack.pop()
        if depth > MAX_NESTING_DEPTH:
            return (f"widgets.{path}: nested {depth} levels deep; the maximum "
                    f"widget nesting depth is {MAX_NESTING_DEPTH}")
        children = entries(widget.get("widgets"), path, depth + 1)
        items = widget.get("items")
        if widget.get("type") == "button-group" and isinstance(items, dict):
            # Items are widgets whatever their value (widget_tree()), and
            # leaves: an empty mapping stands in for each.
            children += [(f"{path}.{key}", {}, depth + 1) for key in items]
        stack.extend(reversed(children))
    return None


def widget_tree(widgets: dict, prefix: str = "", name_prefix: str = "",
                depth: int = 1) -> list:
    """The widget tree as WidgetNodes, in YAML declaration order — every
    node that gets an ID. The single place both paths are derived; every
    other helper here, and every backend, walks this tree.

    Raises ValueError past MAX_NESTING_DEPTH (nesting_depth_error() reports
    the same documents without recursing; validate.py runs it first)."""
    nodes: list = []
    for key, widget in widgets.items():
        if not isinstance(widget, dict):
            continue
        path = f"{prefix}.{key}" if prefix else key
        if depth > MAX_NESTING_DEPTH:
            raise ValueError(
                f"Widget '{path}' is nested {depth} levels deep; the maximum "
                f"widget nesting depth is {MAX_NESTING_DEPTH}."
            )
        name_path = f"{name_prefix}.{key}" if name_prefix else key
        transparent = (widget.get("type", "") in NAMESPACE_FLAG_TYPES
                       and widget.get("namespace") is not True)
        children = widget_tree(widget.get("widgets", {}), path,
                               name_prefix if transparent else name_path,
                               depth + 1)
        if widget.get("type", "") == "button-group":
            if depth + 1 > MAX_NESTING_DEPTH and widget.get("items"):
                raise ValueError(
                    f"Widget '{path}.{next(iter(widget['items']))}' is nested "
                    f"{depth + 1} levels deep; the maximum widget nesting depth "
                    f"is {MAX_NESTING_DEPTH}."
                )
            children += [
                WidgetNode(item_key, f"{path}.{item_key}", (), "button-group-item",
                           f"{name_path}.{item_key}", False,
                           item if isinstance(item, dict) else {})
                for item_key, item in widget.get("items", {}).items()
            ]
        nodes.append(WidgetNode(key, path, tuple(children), _type_str(widget),
                                name_path, transparent, widget))
    return nodes


def walk(nodes) -> list:
    """Every node of a widget_tree(), parents before children."""
    result: list = []
    for node in nodes:
        result.append(node)
        result.extend(walk(node.children))
    return result


def scope_members(nodes) -> list:
    """The members of one naming scope, given its direct children (the
    top-level tree, or a namespace node's children): each child, plus the
    members a transparent child contributes in its place. These become the
    members of one generated class (`UDisplay`/`UI` or a namespace's
    class)."""
    result: list = []
    for node in nodes:
        result.append(node)
        if node.transparent:
            result.extend(scope_members(node.children))
    return result


def _collect(widgets: dict) -> list[str]:
    """Every widget's structural path: `<parent path>.<key>`, or `<key>` at
    the top level. button-group items get `<group>.<item>`; dropdown items
    are options, not widgets, and get nothing."""
    return [node.path for node in walk(widget_tree(widgets))]


def collect_types(widgets: dict) -> dict[str, str]:
    """
    Return a mapping of structural path → type_str for every widget (same
    paths as _collect()).

    Text mode is baked in: 'text-rw' or 'text-ro'.
    button-group items are typed as 'button-group-item'.
    Containers and decorations report their own type ('section', 'row',
    'label', ...); backends generate no typed setter/handler for them.
    """
    return {node.path: node.type for node in walk(widget_tree(widgets))}


def name_paths(widgets: dict) -> dict[str, str]:
    """Return a mapping of structural path → name path for every widget."""
    return {node.path: node.name_path for node in walk(widget_tree(widgets))}


def name_path_collisions(widgets: dict) -> list[tuple[str, list[str]]]:
    """(name path, [structural paths]) for every name path more than one
    widget resolves to: the same key twice in one naming scope, e.g. a
    `temp` in each of two transparent rows."""
    by_name: dict[str, list[str]] = {}
    for node in walk(widget_tree(widgets)):
        by_name.setdefault(node.name_path, []).append(node.path)
    return [(name, paths) for name, paths in by_name.items() if len(paths) > 1]


def describe_name_collision(name: str, paths: list[str]) -> str:
    """The error text for one name_path_collisions() entry, shared by
    validate.py and the backends."""
    return (
        ", ".join(f"widgets.{p}" for p in paths)
        + f": all named '{name}' in the generated firmware API; rename one, "
        f"or set `namespace: true` on a section/row/grid that separates them"
    )


def collect_dropdown_items(widgets: dict) -> dict[str, list[tuple[str, str]]]:
    """
    Return a mapping of dropdown structural path → [(item_key, item_label),
    ...] in declaration order. Used by the backends to emit per-item index
    constants.
    """
    # str() guards against YAML 1.1 boolean/int key coercion (e.g. `off:` → False)
    return {
        node.path: [(str(k), str(v)) for k, v in node.widget.get("items", {}).items()]
        for node in walk(widget_tree(widgets))
        if node.type == "dropdown"
    }


def assign(widgets: dict) -> dict[str, int]:
    """
    Return a mapping of id_path → widget_id for every widget.

    Raises ValueError if widget count exceeds 240 (containers and decorations
    count too), if a widget is nested deeper than MAX_NESTING_DEPTH, or if
    two widgets resolve to the same structural path. Valid
    YAML can't produce a duplicate (every parent's key is a path segment of
    its children, and the schema forbids '.' in keys); the check guards
    input that skipped schema validation.
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
