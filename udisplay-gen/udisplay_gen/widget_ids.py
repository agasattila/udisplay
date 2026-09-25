# SPDX-License-Identifier: MPL-2.0
# Copyright (c) 2026 Attila Agas

"""
Widget ID assignment for uDisplay.

Every widget gets a widget ID (issue #43) — leaves, containers (section, row,
grid, dpad), decorations (label, separator), button face children and
button-group items alike — so SET_PROPERTY / RESET_PROPERTY can target any
node of the widget tree. Dropdown items are options, not widgets, and get no
ID.

IDs are assigned alphabetically by ID path, starting at 0x10. Containers are
transparent to their CHILDREN's paths (a container's own key is never a
segment of a child's path), but the container itself gets an ID under its
own key at the position it occupies — exactly like a leaf would.

This is ID scheme v5 (PROTO_VERSION 0x05). The client keeps the older
leaf-only scheme for devices reporting PROTO_VERSION < 0x05 (see
YamlParser.cpp's IdScheme); codegen only ever emits v5.

See docs/protocol.md § Widget ID Assignment.
"""
from __future__ import annotations

ID_START = 0x10
ID_MAX = 0xFF
MAX_WIDGETS = ID_MAX - ID_START + 1  # 240

# Layout containers — transparent to their children's ID paths. Single source
# of truth for validate.py and the cpp/python backends (TODO-007).
CONTAINER_TYPES = frozenset({"section", "row", "grid", "dpad"})

# Static decorations — carry no value (no setter/handler), but still get an ID.
DECORATION_TYPES = frozenset({"label", "separator"})


def _collect(widgets: dict, prefix: str = "") -> list[str]:
    """
    Recursively collect every widget's ID path.

    - Every widget map entry gets a path: `<prefix>.<key>` (or `<key>` at the
      top level / under top-level containers).
    - Containers (section/row/grid/dpad) recurse with the SAME prefix — their
      own name is excluded from their children's paths.
    - Any other widget with `widgets:` (a button face) recurses with its own
      path as the prefix, so face children are `<button>.<child>`; a
      container on a face (`btn.face_row`) is again transparent to its own
      children (`btn.icon`).
    - button-group items get `<group>.<item>`; dropdown items get nothing.
    """
    paths: list[str] = []
    for key, widget in widgets.items():
        if not isinstance(widget, dict):
            continue
        wtype = widget.get("type", "")

        path = f"{prefix}.{key}" if prefix else key
        paths.append(path)

        child_prefix = prefix if wtype in CONTAINER_TYPES else path
        paths.extend(_collect(widget.get("widgets", {}), child_prefix))

        if wtype == "button-group":
            for item_key in widget.get("items", {}):
                paths.append(f"{path}.{item_key}")

    return paths


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

        child_prefix = prefix if wtype in CONTAINER_TYPES else path
        result.update(collect_types(widget.get("widgets", {}), child_prefix))

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

        if wtype in CONTAINER_TYPES:
            result.update(collect_dropdown_items(widget.get("widgets", {}), prefix))
            continue

        path = f"{prefix}.{key}" if prefix else key
        if wtype == "dropdown":
            items = widget.get("items", {})
            # str() guards against YAML 1.1 boolean/int key coercion (e.g. `off:` → False)
            result[path] = [(str(k), str(v)) for k, v in items.items()]

    return result


def assign(widgets: dict) -> dict[str, int]:
    """
    Return a mapping of id_path → widget_id for every widget.

    Raises ValueError if widget count exceeds 240 (containers and decorations
    count too), or if two widgets resolve to the same id_path. Containers
    don't contribute their own name to their children's paths, so two
    same-named widgets under different containers sharing a transparent-
    prefix ancestor collide — including a container whose own name equals a
    widget's name elsewhere in the same scope (e.g. a section `advanced` and
    a slider `advanced` in another section).
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
                f"protocol ID. Widget names (containers and labels included) must be "
                f"unique across sibling containers (section/row/grid/dpad, or a "
                f"button face's nested containers) that share a transparent-prefix "
                f"ancestor."
            )
        seen.add(path)
    return {path: ID_START + i for i, path in enumerate(paths)}
