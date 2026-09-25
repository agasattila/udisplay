# SPDX-License-Identifier: MPL-2.0
# Copyright (c) 2026 Attila Agas

"""
YAML validation for uDisplay definitions.
Schema validation (JSON Schema) + semantic checks not expressible in schema.
"""
from __future__ import annotations

import json
import pathlib

import jsonschema
import yaml

# Bundled schema — a symlink to the repo-root udisplay.schema.json (the canonical
# copy). Real installs (pip install .) get a dereferenced regular file; editable
# installs and the dev checkout read straight through the symlink.
_BUNDLED_SCHEMA = pathlib.Path(__file__).parent / "schema" / "udisplay.schema.json"

# Supported types listed in validation errors (kept in sync with schema oneOf)
SUPPORTED_TYPES = [
    "display", "led", "rgbled", "button", "button-group", "slider", "toggle", "text",
    "dropdown", "label", "separator",
    "section", "row", "grid", "dpad",
]

# Container types — have a `widgets:` sub-map, excluded from widget ID assignment
CONTAINER_TYPES = {"section", "row", "grid", "dpad"}

# Decoration types — no widget ID, no protocol exchange
DECORATION_TYPES = {"label", "separator"}

# button-group item keys that would collide with the generated group's own
# set()/clear() methods (C++ class members, Python attributes)
BUTTON_GROUP_RESERVED_ITEM_KEYS = {"set", "clear"}


def load_schema() -> dict:
    with _BUNDLED_SCHEMA.open() as f:
        try:
            return json.load(f)
        except json.JSONDecodeError as e:
            raise json.JSONDecodeError(
                f"{_BUNDLED_SCHEMA} did not parse as JSON ({e.msg}). If this file "
                "is a Git-for-Windows checkout, symlinks may have been checked out "
                "as plain text placeholders instead of real links — run "
                "`git config core.symlinks true` and re-clone/checkout.",
                e.doc, e.pos,
            ) from e


def parse_yaml(path: str | pathlib.Path) -> dict:
    with open(path) as f:
        return yaml.safe_load(f)


def parse_yaml_text(text: str) -> dict:
    return yaml.safe_load(text)


def _yaml_line_map(yaml_text: str) -> dict[str, int]:
    """Build a map from dot-paths to 1-based line numbers using YAML's CST."""
    def walk(node: yaml.Node, prefix: str) -> dict[str, int]:
        out: dict[str, int] = {}
        if isinstance(node, yaml.MappingNode):
            for key_node, value_node in node.value:
                key = key_node.value
                path = f"{prefix}.{key}" if prefix else key
                out[path] = key_node.start_mark.line + 1
                out.update(walk(value_node, path))
        elif isinstance(node, yaml.SequenceNode):
            for i, item_node in enumerate(node.value):
                path = f"{prefix}[{i}]"
                out[path] = item_node.start_mark.line + 1
                out.update(walk(item_node, path))
        return out

    root = yaml.compose(yaml_text)
    return walk(root, "") if root else {}


def _find_matching_branch_errors(oneOf_error: jsonschema.ValidationError,
                                 type_value: str) -> list[jsonschema.ValidationError]:
    """
    From a oneOf ValidationError, return sub-errors from the branch whose schema
    has properties.type.const == type_value. Returns [] if not found.
    """
    for ctx_err in oneOf_error.context:
        schema = ctx_err.schema
        type_const = (schema.get("properties") or {}).get("type", {}).get("const")
        if type_const == type_value:
            return list(ctx_err.context) if ctx_err.context else [ctx_err]
    return []


def schema_errors(doc: dict, schema: dict,
                  line_map: dict[str, int] | None = None) -> list[str]:
    """
    Run JSON Schema validation. Returns a list of human-readable error strings.
    Detects unknown widget types and unknown properties on valid widget types.
    When line_map is provided (built from raw YAML text), line numbers are included.
    """
    if line_map is None:
        line_map = {}
    errors: list[str] = []
    validator = jsonschema.Draft7Validator(schema)

    for error in sorted(validator.iter_errors(doc), key=lambda e: list(e.path)):
        path = ".".join(str(p) for p in error.path) if error.path else "(root)"
        line = line_map.get(path)
        line_sfx = f" (line {line})" if line else ""

        if (
            error.validator == "oneOf"
            and "widgets" in str(list(error.path))
            and isinstance(error.instance, dict)
            and "type" in error.instance
        ):
            bad_type = error.instance.get("type", "<unknown>")
            if bad_type in SUPPORTED_TYPES:
                # Valid type but bad properties — report the specific branch errors.
                branch_errs = _find_matching_branch_errors(error, bad_type)
                if branch_errs:
                    for be in branch_errs:
                        be_parts = list(be.path)
                        be_path = (
                            path + "." + ".".join(str(p) for p in be_parts)
                            if be_parts else path
                        )
                        be_line = line_map.get(be_path) or line
                        be_sfx = f" (line {be_line})" if be_line else ""
                        errors.append(f"  {be_path}{be_sfx}: {be.message}")
                else:
                    errors.append(
                        f"  {path}{line_sfx}: invalid `{bad_type}` widget definition"
                    )
            else:
                errors.append(
                    f"  {path}{line_sfx}: unknown widget type `{bad_type}`. "
                    f"Supported types: {', '.join(SUPPORTED_TYPES)}"
                )
        else:
            errors.append(f"  {path}{line_sfx}: {error.message}")

    return errors


def _check_style_ref(style_ref: str | None, style_names: set[str],
                      widget_path: str) -> list[str]:
    """Shared "style: names a declared stylesheet" check — every widget type
    accepts style: now (button, the last rejected type, was lifted in
    docs/designs/container-style-cascading.md's Revision). Factored out
    since this same check now runs from 3 sites: the main per-widget loop
    below, the button-face recursion, and the button-group items loop."""
    if style_ref is None or style_ref in style_names:
        return []
    return [
        f"  {widget_path}: style '{style_ref}' is not declared in the "
        f"top-level style: block"
    ]


def _semantic_errors_in_map(widgets: dict, path_prefix: str,
                            seen_names: set[str], style_names: set[str]) -> list[str]:
    """
    Recursive semantic check for a widget map.
    - slider min < max
    - dpad's button children must all have position
    - leaf widget names globally unique across all container scopes
      (scoped to CONTAINER_TYPES's transparent-prefix subtree — see the
      `button` branch below for why a `button`'s own face children get a
      FRESH local scope instead of sharing this one)
    - style: names a declared stylesheet
    - button face children and button-group items — previously invisible to
      every check above (CONTAINER_TYPES never included button/button-group,
      so this function never recursed into them) — now get the style check
      too, closing a pre-existing gap independent of button's own style:
      acceptance
    """
    errors: list[str] = []

    for key, widget in widgets.items():
        if not isinstance(widget, dict):
            continue
        wtype = widget.get("type")
        widget_path = f"{path_prefix}.{key}" if path_prefix else f"widgets.{key}"

        errors.extend(_check_style_ref(widget.get("style"), style_names, widget_path))

        if wtype in CONTAINER_TYPES:
            sub_widgets = widget.get("widgets", {})
            if wtype == "dpad":
                missing = [
                    k for k, v in sub_widgets.items()
                    if isinstance(v, dict) and "position" not in v
                ]
                if missing:
                    errors.append(
                        f"  {widget_path}: dpad requires `position` on every child "
                        f"button; missing: {', '.join(missing)}"
                    )
            errors.extend(_semantic_errors_in_map(sub_widgets, widget_path, seen_names, style_names))
            continue

        if wtype == "button":
            # `button` is NOT a prefix-transparent container (widget_ids.py's
            # assign() and YamlParser.cpp's collectPathsRecursive() both key
            # identity on the full compound path, e.g. "button_a.icon" !=
            # "button_b.icon") — unlike CONTAINER_TYPES, where a short key IS
            # the full identity path. Recursing with the GLOBAL seen_names
            # set would reject valid YAML where two different buttons each
            # have a same-named face child (e.g. "icon") as a false
            # duplicate. A fresh, button-local set still correctly catches
            # the real collision case: two sibling containers *inside this
            # same button's face* reusing a name.
            errors.extend(_semantic_errors_in_map(
                widget.get("widgets", {}), widget_path, set(), style_names))
            # Falls through (no early `continue`) to the seen_names check
            # below for the button's OWN key, in the global/outer scope —
            # exactly like every other non-container widget.

        if wtype == "button-group":
            # Items are hand-built (no `type:` key, per buttonGroupItem's
            # schema) and have no sub-widgets of their own to recurse into —
            # just check each item's own style: ref. No name-uniqueness
            # check is needed here: YAML mapping keys are already unique
            # within one button-group, and different groups never share a
            # path prefix (each gets its own "group_key.item_key"), so
            # there's nothing for seen_names to usefully catch across items.
            for item_key, item in widget.get("items", {}).items():
                if not isinstance(item, dict):
                    continue
                item_path = f"{widget_path}.{item_key}"
                errors.extend(_check_style_ref(item.get("style"), style_names, item_path))
            for item_key in widget.get("items", {}):
                if item_key in BUTTON_GROUP_RESERVED_ITEM_KEYS:
                    errors.append(
                        f"  {widget_path}.{item_key}: `{item_key}` is reserved as a "
                        f"button-group item name (generated C++/Python groups have "
                        f"`{item_key}()` methods); rename this item"
                    )
            # Falls through to the seen_names check below for the group's
            # OWN key, same as the button branch above.

        if wtype in DECORATION_TYPES:
            continue

        if wtype == "slider":
            mn, mx = widget.get("min"), widget.get("max")
            if mn is not None and mx is not None and mn >= mx:
                errors.append(
                    f"  {widget_path}: slider `min` ({mn}) must be less than `max` ({mx})"
                )

        if key in seen_names:
            errors.append(
                f"  {widget_path}: duplicate leaf name `{key}` — "
                f"widget names must be globally unique across all container scopes"
            )
        else:
            seen_names.add(key)

    return errors


def semantic_errors(doc: dict) -> list[str]:
    """
    Semantic checks not expressible in JSON Schema:
    - slider min < max
    - dpad's button children must all have position
    - leaf names globally unique across all container scopes
    - style: names a declared stylesheet
    """
    seen: set[str] = set()
    # "default" is always implicitly valid, even with no style: block at all
    # — matches udisplay-client's YamlParser.cpp parseStyles(), which always
    # populates a "default" entry (from the style.default: block if present,
    # else hardcoded StyleToken C++ defaults).
    style_names = set(doc.get("style", {}).keys()) | {"default"}
    return _semantic_errors_in_map(doc.get("widgets", {}), "", seen, style_names)


def validate(doc: dict, schema: dict | None = None,
             yaml_text: str | None = None) -> list[str]:
    """Combined schema + semantic validation. Returns all errors."""
    if schema is None:
        schema = load_schema()
    line_map = _yaml_line_map(yaml_text) if yaml_text else {}
    return schema_errors(doc, schema, line_map) + semantic_errors(doc)
