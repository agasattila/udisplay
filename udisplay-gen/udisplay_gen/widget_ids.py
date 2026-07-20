# SPDX-License-Identifier: MPL-2.0
# Copyright (c) 2026 Attila Agas

"""
Widget ID assignment for uDisplay.

IDs are assigned alphabetically by leaf key name (not full path), starting at 0x10.
Container types (section, row, grid) are transparent — their names are excluded from
the ID path. Decoration types (label, separator) and dropdown items are excluded from
ID assignment entirely.

See docs/merkle.md § Widget ID assignment.
"""
from __future__ import annotations

ID_START = 0x10
ID_MAX = 0xFF
MAX_WIDGETS = ID_MAX - ID_START + 1  # 240

# Container types — transparent to ID assignment (children get IDs, not the container)
CONTAINER_TYPES = {"section", "row", "grid"}

# Types that never receive a widget ID
NO_ID_TYPES = {"label", "separator"}


def _collect(widgets: dict, prefix: str = "") -> list[str]:
    """
    Recursively collect all ID-bearing leaf paths in alphabetical order per level.

    - Containers (section/row/grid) are transparent: their children get IDs as if
      the container name were absent from the path.
    - Decoration types (label, separator) are skipped.
    - dropdown items are NOT collected (only the dropdown itself gets an ID).
    - button children and button-group items keep their parent-prefixed paths.
    """
    paths: list[str] = []
    for key, widget in widgets.items():
        if not isinstance(widget, dict):
            continue
        wtype = widget.get("type", "")

        if wtype in CONTAINER_TYPES:
            # Transparent: recurse with the SAME prefix (container name excluded)
            paths.extend(_collect(widget.get("widgets", {}), prefix))
            continue

        if wtype in NO_ID_TYPES:
            continue

        path = f"{prefix}.{key}" if prefix else key
        paths.append(path)

        # button children get IDs, except decoration types (label, separator),
        # which never receive one — matches top-level label/separator semantics.
        # Recurse (not a flat loop) so a container-typed child (row/grid,
        # widget-model-redesign Increment 2) is transparent to ID assignment
        # just like a top-level container — its own grandchildren get IDs
        # prefixed by this widget's own path, not the container's throwaway
        # key. Mirrors the equivalent fix in YamlParser.cpp's
        # collectPathsRecursive().
        paths.extend(_collect(widget.get("widgets", {}), path))

        # button-group items get IDs
        for item_key in widget.get("items", {}):
            if wtype == "button-group":
                paths.append(f"{path}.{item_key}")
            # dropdown items: no IDs (intentional — already handled by skipping)

    return paths


def collect_types(widgets: dict, prefix: str = "") -> dict[str, str]:
    """
    Return a mapping of id_path → type_str for every ID-bearing widget.

    Text mode is baked in: 'text-rw' or 'text-ro'.
    button children are typed per their own `type:` field (led, rgbled, display —
    label is excluded from the result, matching top-level label semantics: no ID,
    no setter).
    button-group items are typed as 'button-group-item'.
    Container names are excluded from paths (same logic as _collect).
    label, separator, and dropdown items are excluded.
    """
    result: dict[str, str] = {}
    for key, widget in widgets.items():
        if not isinstance(widget, dict):
            continue
        wtype = widget.get("type", "")

        if wtype in CONTAINER_TYPES:
            result.update(collect_types(widget.get("widgets", {}), prefix))
            continue

        if wtype in NO_ID_TYPES:
            continue

        path = f"{prefix}.{key}" if prefix else key

        if wtype == "text":
            mode = widget.get("mode", "ro")
            result[path] = f"text-{mode}"
        elif wtype == "dropdown":
            result[path] = "dropdown"
        else:
            result[path] = wtype

        # Recurse (not a flat loop) — same container-transparency fix as
        # _collect() above, so a nested row/grid button-face child's own
        # children get correctly-prefixed, correctly-typed entries instead
        # of being silently dropped.
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

        if wtype in CONTAINER_TYPES:
            result.update(collect_dropdown_items(widget.get("widgets", {}), prefix))
            continue

        if wtype in NO_ID_TYPES:
            continue

        path = f"{prefix}.{key}" if prefix else key
        if wtype == "dropdown":
            items = widget.get("items", {})
            # str() guards against YAML 1.1 boolean/int key coercion (e.g. `off:` → False)
            result[path] = [(str(k), str(v)) for k, v in items.items()]

    return result


def assign(widgets: dict) -> dict[str, int]:
    """
    Return a mapping of id_path → widget_id for every ID-bearing widget.

    Raises ValueError if widget count exceeds 240, or if two widgets resolve
    to the same id_path (duplicate leaf name under sibling containers that
    share a transparent-prefix ancestor — e.g. two same-named leaves in two
    different row/grid children of the same button face; containers don't
    contribute their own name to the path, so this collides silently
    instead of erroring at parse time otherwise).
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
                f"protocol ID. Check for same-named leaves under different sibling "
                f"containers (row/grid/section, or a button face's nested containers) "
                f"that share a transparent-prefix ancestor."
            )
        seen.add(path)
    return {path: ID_START + i for i, path in enumerate(paths)}
