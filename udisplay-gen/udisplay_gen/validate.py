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
    "section", "row", "grid",
]

# Container types — have a `widgets:` sub-map, excluded from widget ID assignment
CONTAINER_TYPES = {"section", "row", "grid"}

# Decoration types — no widget ID, no protocol exchange
DECORATION_TYPES = {"label", "separator"}


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


def _semantic_errors_in_map(widgets: dict, path_prefix: str,
                            seen_names: set[str]) -> list[str]:
    """
    Recursive semantic check for a widget map.
    - slider min < max
    - button-group dpad items must all have position
    - leaf widget names globally unique across all container scopes
    """
    errors: list[str] = []

    for key, widget in widgets.items():
        if not isinstance(widget, dict):
            continue
        wtype = widget.get("type")
        widget_path = f"{path_prefix}.{key}" if path_prefix else f"widgets.{key}"

        if wtype in CONTAINER_TYPES:
            sub_widgets = widget.get("widgets", {})
            errors.extend(_semantic_errors_in_map(sub_widgets, widget_path, seen_names))
            continue

        if wtype in DECORATION_TYPES:
            continue

        if wtype == "slider":
            mn, mx = widget.get("min"), widget.get("max")
            if mn is not None and mx is not None and mn >= mx:
                errors.append(
                    f"  {widget_path}: slider `min` ({mn}) must be less than `max` ({mx})"
                )

        if wtype == "button-group" and widget.get("layout") == "dpad":
            items = widget.get("items", {})
            missing = [k for k, v in items.items() if "position" not in v]
            if missing:
                errors.append(
                    f"  {widget_path}: dpad layout requires `position` on every item; "
                    f"missing: {', '.join(missing)}"
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
    - button-group dpad items must all have position
    - leaf names globally unique across all container scopes
    """
    seen: set[str] = set()
    return _semantic_errors_in_map(doc.get("widgets", {}), "", seen)


def validate(doc: dict, schema: dict | None = None,
             yaml_text: str | None = None) -> list[str]:
    """Combined schema + semantic validation. Returns all errors."""
    if schema is None:
        schema = load_schema()
    line_map = _yaml_line_map(yaml_text) if yaml_text else {}
    return schema_errors(doc, schema, line_map) + semantic_errors(doc)
