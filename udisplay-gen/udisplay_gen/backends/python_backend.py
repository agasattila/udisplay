# SPDX-License-Identifier: MPL-2.0
# Copyright (c) 2026 Attila Agas

"""
MicroPython/CPython codegen backend: produces ui.py + udisplay_runtime.py.

v0 scope (Approach A, docs/designs/micropython-backend.md): shrunk mode
only. `ui.py` is thin, generated, and never hand-edited. `udisplay_runtime.py`
is the hand-written protocol library, copied verbatim (not templated) —
output-ownership boundary: a user's own code lives in a third, separate
file (main.py) this backend never writes, so re-running `build` never
destroys a user's callbacks/credentials/app logic.

Monolithic-merge and library-only output modes are deferred (Approach B).
BLE transport and HMAC auth are out of scope for v0 (matches --namespace's
absence here too — see below).
"""
from __future__ import annotations

import keyword
import pathlib
from typing import List

from . import BuildContext, OutputFile
from ._shared import _macro_name

_RUNTIME_SOURCE = pathlib.Path(__file__).parent.parent / "runtime" / "udisplay_runtime.py"

# Container types transparent to the member-ordering traversal below.
# Mirrors widget_ids.py's CONTAINER_TYPES. NOTE: cpp_backend.py's own local
# copy of this concept (_cpp_ordered_toplevel's `container_types` set) is
# missing "dpad" -- a pre-existing gap in cpp_backend.py, flagged separately
# to the user, not fixed here (out of scope for this backend's diff).
_CONTAINER_TYPES = {"section", "row", "grid", "dpad"}
_NO_ID_TYPES = {"label", "separator"}

_WRAPPER_CLASS = {
    "display": "DisplayWidget",
    "slider": "SliderWidget",
    "led": "LedWidget",
    "rgbled": "RgbLedWidget",
    "toggle": "ToggleWidget",
    "text-rw": "TextRwWidget",
    "text-ro": "TextRoWidget",
    "dropdown": "DropdownWidget",
    "button": "ButtonWidget",
    "button-group": "ButtonGroupWidget",
    "button-group-item": "ButtonItem",
}

# Reserved-name sets for TODO-056/TODO-057: a widget path segment that lands
# in one of these scopes as a Python attribute name (self.<name>) must not
# collide with a fixed name already used in that same scope, or the later
# definition silently overwrites the earlier one — see
# _validate_python_identifiers() below.
#
# Top level: every entry in _py_ordered_toplevel() becomes `self.<path>` on
# the generated UI class (_generate_ui_py below) — cross-reference against
# every fixed attribute/method UI.__init__ and the class body assign there.
_UI_RESERVED_NAMES = {
    "_device", "on_client_ready", "on_comms_error",
    "on_connect", "on_disconnect", "feed", "heartbeat",
    "_on_client_ready", "_on_comms_error", "_on_event",
    "__init__", "self",
}

# Button face children (_py_sub_members under a `button`) become
# `self.<path>.<sub_key>` on a ButtonWidget instance — cross-reference
# against ButtonWidget's own __init__ body above (no __slots__, so any
# attribute name is silently accepted, colliding or not).
_BUTTON_RESERVED_NAMES = {"_device", "_widget_id", "on_press", "on_release", "on_click"}

# button-group items become `self.<path>.<item_key>` on a ButtonGroupWidget
# instance — cross-reference against ButtonGroupWidget's own __init__ body.
_BUTTON_GROUP_RESERVED_NAMES = {"_device", "_widget_id", "set", "clear"}


def _validate_python_identifiers(ctx: BuildContext) -> None:
    """Reject schema-valid YAML that would produce invalid or ambiguously
    generated Python code (TODO-056, TODO-057). Detects, for every widget
    name that becomes a generated Python identifier:
      - Python keywords (produce a SyntaxError in the generated file);
      - collisions with names reserved by the generated UI/runtime API
        (silently overwrite a lifecycle method or callback slot);
      - collisions with each other after WIDGET_ID_* macro-name
        normalization (two differently-punctuated names silently alias
        onto one wire ID and one event-dispatch branch).
    Raises ValueError listing every violation found, not just the first,
    so one fix-and-rerun cycle catches everything."""
    widget_ids = ctx.widget_ids
    widget_types = ctx.widget_types or {}
    widgets_yaml = ctx.widgets_yaml or {}
    errors: List[str] = []

    def check_segment(name: str, reserved: set, where: str) -> None:
        if keyword.iskeyword(name):
            errors.append(
                f"{where}: '{name}' is a Python keyword — cannot be used as a "
                f"generated attribute name"
            )
        elif name in reserved:
            errors.append(
                f"{where}: '{name}' collides with a name reserved by the "
                f"generated UI/runtime API"
            )

    ordered = _py_ordered_toplevel(widgets_yaml, widget_types)

    for path in ordered:
        check_segment(path, _UI_RESERVED_NAMES, f"widget '{path}'")

        wtype = widget_types.get(path, "")
        sub_reserved = (
            _BUTTON_GROUP_RESERVED_NAMES if wtype == "button-group"
            else _BUTTON_RESERVED_NAMES
        )
        for sub_key, _sub_type, _sub_id in _py_sub_members(path, widget_types, widget_ids):
            check_segment(sub_key, sub_reserved, f"widget '{path}.{sub_key}'")

    # WIDGET_ID_* macro-name collisions (TODO-056) — every widget_ids key
    # (top-level AND nested, since widget_ids is keyed by full dotted path)
    # must normalize to a distinct constant name.
    by_macro: dict = {}
    for path in widget_ids:
        by_macro.setdefault(_macro_name(path), []).append(path)
    for macro, paths in sorted(by_macro.items()):
        if len(paths) > 1:
            errors.append(
                "widget name collision: "
                + ", ".join(repr(p) for p in sorted(paths))
                + f" all normalize to the same generated constant WIDGET_ID_{macro}"
            )

    # Same mechanism, scoped per dropdown, for the <NAME>_OPTION_<item> constants.
    from ..widget_ids import collect_dropdown_items
    for path, items in collect_dropdown_items(widgets_yaml).items():
        if path not in widget_ids:
            continue
        by_item_macro: dict = {}
        for item_key, _label in items:
            by_item_macro.setdefault(_macro_name(item_key), []).append(item_key)
        for macro, keys in sorted(by_item_macro.items()):
            if len(keys) > 1:
                errors.append(
                    f"dropdown '{path}' item collision: "
                    + ", ".join(repr(k) for k in sorted(keys))
                    + f" all normalize to the same generated constant "
                    f"{_macro_name(path)}_OPTION_{macro}"
                )

    if errors:
        raise ValueError(
            "Cannot generate valid Python code from this YAML:\n"
            + "\n".join(f"  - {e}" for e in errors)
        )


def generate(ctx: BuildContext) -> List[OutputFile]:
    if ctx.namespace:
        raise ValueError(
            "--namespace is not yet supported for --lang micropython "
            "(multi-instance output is Approach B scope, deferred — see "
            "docs/designs/micropython-backend.md)."
        )
    _validate_python_identifiers(ctx)
    return [
        OutputFile("ui.py", _generate_ui_py(ctx)),
        OutputFile("udisplay_runtime.py", _RUNTIME_SOURCE.read_text()),
    ]


# ── helpers ──────────────────────────────────────────────────────────────────

def _py_bytes_literal(data: bytes) -> str:
    """A portable bytes literal: only \\xHH escapes, valid in both CPython
    and MicroPython regardless of byte value."""
    return 'b"' + "".join(f"\\x{b:02x}" for b in data) + '"'


def _py_ordered_toplevel(widgets_yaml: dict, widget_types: dict) -> list:
    """Top-level widget paths in YAML declaration order (containers
    transparent). Mirrors cpp_backend.py's _cpp_ordered_toplevel."""
    result: list = []
    for key, widget in widgets_yaml.items():
        if not isinstance(widget, dict):
            continue
        wtype = widget.get("type", "")
        if wtype in _NO_ID_TYPES:
            continue
        if wtype in _CONTAINER_TYPES:
            for child_path in _py_ordered_toplevel(widget.get("widgets", {}), widget_types):
                if child_path not in result:
                    result.append(child_path)
            continue
        if key in widget_types:
            result.append(key)
    return result


def _py_sub_members(path: str, widget_types: dict, widget_ids: dict) -> list:
    """[(sub_key, type_str, widget_id)] for sub-members, alphabetical order.
    Mirrors cpp_backend.py's _cpp_sub_members."""
    prefix = path + "."
    return [
        (sub_path[len(prefix):], widget_types[sub_path], widget_ids[sub_path])
        for sub_path in sorted(widget_ids)
        if sub_path.startswith(prefix)
    ]


_BASE_CLASSES = '''
# ── Widget wrapper classes ──────────────────────────────────────────────────
# __slots__ on the value-only leaf wrappers (the bulk of widget instances,
# up to MAX_WIDGETS=240) keeps per-instance RAM down; ButtonWidget/
# ButtonGroupWidget skip __slots__ so generated __init__ code below can
# attach named sub-widgets (LED children, button-group items) as plain
# attributes.

class DisplayWidget:
    __slots__ = ("_device", "_widget_id")
    def __init__(self, device, widget_id):
        self._device = device
        self._widget_id = widget_id
    def set(self, value):
        self._device.send_float(self._widget_id, value)


class LedWidget:
    __slots__ = ("_device", "_widget_id")
    def __init__(self, device, widget_id):
        self._device = device
        self._widget_id = widget_id
    def set(self, value):
        self._device.send_bool(self._widget_id, value)


class RgbLedWidget:
    __slots__ = ("_device", "_widget_id")
    def __init__(self, device, widget_id):
        self._device = device
        self._widget_id = widget_id
    def set(self, value):
        self._device.send_int(self._widget_id, value)


class ToggleWidget:
    __slots__ = ("_device", "_widget_id", "on_change")
    def __init__(self, device, widget_id):
        self._device = device
        self._widget_id = widget_id
        self.on_change = None
    def set(self, value):
        self._device.send_bool(self._widget_id, value)


class SliderWidget:
    __slots__ = ("_device", "_widget_id", "on_change")
    def __init__(self, device, widget_id):
        self._device = device
        self._widget_id = widget_id
        self.on_change = None
    def set(self, value):
        self._device.send_float(self._widget_id, value)


class TextRwWidget:
    __slots__ = ("_device", "_widget_id", "on_submit")
    def __init__(self, device, widget_id):
        self._device = device
        self._widget_id = widget_id
        self.on_submit = None
    def set(self, value):
        self._device.send_string(self._widget_id, value)


class TextRoWidget:
    __slots__ = ("_device", "_widget_id")
    def __init__(self, device, widget_id):
        self._device = device
        self._widget_id = widget_id
    def set(self, value):
        self._device.send_string(self._widget_id, value)


class DropdownWidget:
    __slots__ = ("_device", "_widget_id", "on_change")
    def __init__(self, device, widget_id):
        self._device = device
        self._widget_id = widget_id
        self.on_change = None
    def set(self, index):
        self._device.send_uint8(self._widget_id, index)


class ButtonWidget:
    def __init__(self, device, widget_id):
        self._device = device
        self._widget_id = widget_id
        self.on_press = None
        self.on_release = None
        self.on_click = None


# button-group items have the identical shape to a top-level button (no
# setter, three press/release/click callbacks) -- ButtonItem is the same
# class under the name generated code uses for button-group children, not
# a second implementation to keep in sync (maintainability review, 2026-09-11).
ButtonItem = ButtonWidget


class ButtonGroupWidget:
    # Exclusive selection is device-authoritative: an item press only fires
    # that item's callbacks; firmware confirms with set(), which pushes
    # STATE_UPDATE(group, uint8 item widget ID). Items are ButtonItem
    # sub-attributes, assigned in UI.__init__ below.
    def __init__(self, device, widget_id):
        self._device = device
        self._widget_id = widget_id
    def set(self, item):
        # item: one of this group's ButtonItem attributes (ui.mode.fast) or
        # its WIDGET_ID_* constant.
        if isinstance(item, ButtonItem):
            item = item._widget_id
        self._device.send_uint8(self._widget_id, item)
    def clear(self):
        # 0 is a reserved widget ID, never a real item: no item selected.
        self._device.send_uint8(self._widget_id, 0)
'''.lstrip("\n")


def _py_dropdown_options(widgets_yaml: dict, widget_ids: dict) -> list:
    """Module-level '<DROPDOWN_NAME>_OPTION_<ITEM_KEY> = idx' constants for
    every dropdown's items, in declaration order. Scoped per-dropdown (like
    cpp_backend's nested enum) so two dropdowns can reuse the same item key."""
    from ..widget_ids import collect_dropdown_items

    lines: list = []
    dropdown_items = collect_dropdown_items(widgets_yaml)
    for path, items in dropdown_items.items():
        if path not in widget_ids:
            continue
        const_prefix = _macro_name(path)
        for idx, (item_key, _label) in enumerate(items):
            lines.append(f"{const_prefix}_OPTION_{_macro_name(item_key)} = {idx}")
    return lines


def _py_instantiate(path: str, type_str: str, widget_types: dict, widget_ids: dict) -> list:
    """UI.__init__ body lines constructing `self.<path>` and any sub-members."""
    cls = _WRAPPER_CLASS.get(type_str, "ButtonWidget")
    const = f"WIDGET_ID_{_macro_name(path)}"
    lines = [f"        self.{path} = {cls}(self._device, {const})"]
    if type_str in ("button", "button-group"):
        for sub_key, sub_type, sub_wid in _py_sub_members(path, widget_types, widget_ids):
            sub_cls = _WRAPPER_CLASS.get(sub_type, "LedWidget")
            sub_const = f"WIDGET_ID_{_macro_name(path + '.' + sub_key)}"
            lines.append(f"        self.{path}.{sub_key} = {sub_cls}(self._device, {sub_const})")
    return lines


def _py_dispatch_cases(ordered: list, widget_types: dict, widget_ids: dict) -> list:
    """`if widget_id == ...: ... elif widget_id == ...` body for
    UI._on_event. Mirrors cpp_backend.py's _cpp_dispatch_cases."""
    lines: list = []

    def kw() -> str:
        return "if" if not lines else "elif"

    def button_branch(const: str, attr: str) -> None:
        lines.extend([
            f"        {kw()} widget_id == {const}:",
            f"            if event_type == UDISPLAY_EVENT_BUTTON_PRESS and self.{attr}.on_press:",
            f"                self.{attr}.on_press()",
            f"            elif event_type == UDISPLAY_EVENT_BUTTON_RELEASE and self.{attr}.on_release:",
            f"                self.{attr}.on_release()",
            f"            elif event_type == UDISPLAY_EVENT_BUTTON_CLICK and self.{attr}.on_click:",
            f"                self.{attr}.on_click()",
        ])

    for path in ordered:
        type_str = widget_types.get(path, "")
        wid = widget_ids.get(path)
        if wid is None:
            continue
        const = f"WIDGET_ID_{_macro_name(path)}"

        if type_str == "toggle":
            lines.extend([
                f"        {kw()} widget_id == {const}:",
                f"            if event_type == UDISPLAY_EVENT_TOGGLE_CHANGE and self.{path}.on_change:",
                f"                self.{path}.on_change(value)",
            ])
        elif type_str == "slider":
            lines.extend([
                f"        {kw()} widget_id == {const}:",
                f"            if event_type == UDISPLAY_EVENT_SLIDER_CHANGE and self.{path}.on_change:",
                f"                self.{path}.on_change(value)",
            ])
        elif type_str == "button":
            button_branch(const, path)
        elif type_str == "text-rw":
            lines.extend([
                f"        {kw()} widget_id == {const}:",
                f"            if event_type == UDISPLAY_EVENT_TEXT_SUBMIT and self.{path}.on_submit:",
                f"                self.{path}.on_submit(value)",
            ])
        elif type_str == "dropdown":
            lines.extend([
                f"        {kw()} widget_id == {const}:",
                f"            if event_type == UDISPLAY_EVENT_SELECTION_CHANGE and self.{path}.on_change:",
                f"                self.{path}.on_change(value)",
            ])

        for sub_key, sub_type, sub_wid in _py_sub_members(path, widget_types, widget_ids):
            if sub_type != "button-group-item":
                continue
            sub_const = f"WIDGET_ID_{_macro_name(path + '.' + sub_key)}"
            button_branch(sub_const, f"{path}.{sub_key}")

    return lines


# ── main generator ───────────────────────────────────────────────────────────

def _generate_ui_py(ctx: BuildContext) -> str:
    widget_ids = ctx.widget_ids
    blob = ctx.blob
    root = ctx.root
    hashes = ctx.hashes
    source = ctx.source
    widget_types = ctx.widget_types or {}
    widgets_yaml = ctx.widgets_yaml or {}
    version = ctx.version

    from ..merkle import CHUNK_SIZE
    import math
    n = math.ceil(len(blob) / CHUNK_SIZE)
    chunks = [blob[i * CHUNK_SIZE:(i + 1) * CHUNK_SIZE] for i in range(n)]
    chunk_lens = [len(c) for c in chunks]

    ordered = _py_ordered_toplevel(widgets_yaml, widget_types)

    lines = [
        "# SPDX-License-Identifier: MIT",
        "# Copyright (c) 2026 Attila Agas",
        f"# Generated by udisplay-gen {version}. DO NOT EDIT.",
        f"# Source:      {source}",
        f"# Merkle root: {root.hex()}",
        "#",
        "# This file is pure generated glue -- widget IDs, embedded blob data,",
        "# and wiring. Application logic (callbacks, credentials, the socket",
        "# event loop) belongs in your own separate main.py, which imports",
        "# this module. Re-running `udisplay-gen build` overwrites this file",
        "# completely; it never touches main.py.",
        "from udisplay_runtime import UDisplayDevice, UDISPLAY_EVENT_BUTTON_CLICK",
        "from udisplay_runtime import UDISPLAY_EVENT_BUTTON_PRESS, UDISPLAY_EVENT_BUTTON_RELEASE",
        "from udisplay_runtime import UDISPLAY_EVENT_SLIDER_CHANGE, UDISPLAY_EVENT_TOGGLE_CHANGE",
        "from udisplay_runtime import UDISPLAY_EVENT_TEXT_SUBMIT, UDISPLAY_EVENT_SELECTION_CHANGE",
        "",
        "# ── Widget IDs ───────────────────────────────────────────────────────────",
    ]

    for path in sorted(widget_ids):
        lines.append(f"WIDGET_ID_{_macro_name(path)} = 0x{widget_ids[path]:02X}")

    dropdown_opts = _py_dropdown_options(widgets_yaml, widget_ids)
    if dropdown_opts:
        lines.append("")
        lines.append("# ── Dropdown item indices ───────────────────────────────────────────────")
        lines.extend(dropdown_opts)

    lines += [
        "",
        "# ── Blob data (embedded at build time; the device never hashes this) ────",
        f"MERKLE_ROOT = {_py_bytes_literal(root)}",
        f"CHUNK_COUNT = {n}",
        f"CHUNKS = [",
    ]
    for c in chunks:
        lines.append(f"    {_py_bytes_literal(c)},")
    lines.append("]")
    lines.append("CHUNK_LENS = [" + ", ".join(str(cl) for cl in chunk_lens) + "]")
    lines.append("CHUNK_HASHES = [")
    for h in hashes:
        lines.append(f"    {_py_bytes_literal(h)},")
    lines.append("]")
    lines.append("")

    lines.append(_BASE_CLASSES.rstrip("\n"))
    lines.append("")

    lines += [
        "# ── UI: generated aggregate ─────────────────────────────────────────────",
        "",
        "class UI:",
        "    def __init__(self, send):",
        "        self._device = UDisplayDevice(",
        "            merkle_root=MERKLE_ROOT,",
        "            chunks=CHUNKS,",
        "            chunk_hashes=CHUNK_HASHES,",
        "            chunk_lens=CHUNK_LENS,",
        "            send=send,",
        "            on_event=self._on_event,",
        "        )",
        "        self.on_client_ready = None",
        "        self.on_comms_error = None",
        "        self._device.on_client_ready = self._on_client_ready",
        "        self._device.on_comms_error = self._on_comms_error",
        "",
    ]

    for path in ordered:
        type_str = widget_types.get(path, "")
        lines.extend(_py_instantiate(path, type_str, widget_types, widget_ids))

    lines += [
        "",
        "    def on_connect(self):",
        "        self._device.on_connect()",
        "",
        "    def on_disconnect(self):",
        "        self._device.on_disconnect()",
        "",
        "    def feed(self, data):",
        "        self._device.feed(data)",
        "",
        "    def heartbeat(self):",
        "        self._device.heartbeat()",
        "",
        "    def _on_client_ready(self):",
        "        if self.on_client_ready:",
        "            self.on_client_ready()",
        "",
        "    def _on_comms_error(self):",
        "        if self.on_comms_error:",
        "            self.on_comms_error()",
        "",
        "    def _on_event(self, widget_id, event_type, value):",
    ]

    dispatch = _py_dispatch_cases(ordered, widget_types, widget_ids)
    if dispatch:
        lines.extend(dispatch)
    else:
        lines.append("        pass")

    return "\n".join(lines) + "\n"
