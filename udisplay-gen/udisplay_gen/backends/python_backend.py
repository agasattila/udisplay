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
from ._shared import _macro_name, widget_id_macro_collisions
from ..widget_ids import CONTAINER_TYPES, walk, widget_tree

_RUNTIME_SOURCE = pathlib.Path(__file__).parent.parent / "runtime" / "udisplay_runtime.py"

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
# Top level: every top-level widget becomes `self.<key>` on the generated UI
# class (_generate_ui_py below) — cross-reference against every fixed
# attribute/method UI.__init__ and the class body assign there.
_UI_RESERVED_NAMES = {
    "_device", "on_client_ready", "on_comms_error",
    "set_property", "reset_property",
    "on_connect", "on_disconnect", "feed", "heartbeat",
    "_on_client_ready", "_on_comms_error", "_on_event",
    "__init__", "self",
}

# Every other widget becomes `<parent>.<key>` on its parent's wrapper
# (ContainerWidget, ButtonWidget, ButtonGroupWidget) — cross-reference
# against that class's own body in _BASE_CLASSES (no __slots__, so any
# attribute name is silently accepted, colliding or not).
_WIDGET_RESERVED_NAMES = {"_device", "_widget_id", "id", "set_property", "reset_property"}
_BUTTON_RESERVED_NAMES = _WIDGET_RESERVED_NAMES | {"on_press", "on_release", "on_click"}

# button-group items: cross-reference against ButtonGroupWidget's own body.
_BUTTON_GROUP_RESERVED_NAMES = _WIDGET_RESERVED_NAMES | {"_items", "set", "clear"}


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

    tree = widget_tree(widgets_yaml)
    for node in tree:
        check_segment(node.key, _UI_RESERVED_NAMES, f"widget '{node.path}'")
    for node in walk(tree):
        wtype = widget_types.get(node.path, "")
        sub_reserved = (
            _BUTTON_GROUP_RESERVED_NAMES if wtype == "button-group"
            else _BUTTON_RESERVED_NAMES if wtype == "button"
            else _WIDGET_RESERVED_NAMES
        )
        for child in node.children:
            check_segment(child.key, sub_reserved, f"widget '{child.path}'")

    # WIDGET_ID_* macro-name collisions (TODO-056) — every widget_ids key
    # (top-level AND nested, since widget_ids is keyed by full dotted path)
    # must normalize to a distinct constant name.
    errors += widget_id_macro_collisions(widget_ids)

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



_BASE_CLASSES = '''
# ── Widget wrapper classes ──────────────────────────────────────────────────
# __slots__ on the value-only leaf wrappers (the bulk of widget instances,
# up to MAX_WIDGETS=240) keeps per-instance RAM down; ButtonWidget/
# ButtonGroupWidget/ContainerWidget skip __slots__ so generated __init__
# code below can attach named children (face children, button-group items,
# a container's widgets) as plain attributes: ui.settings.rate.

class Widget:
    # Every widget -- containers (section/row/grid/dpad) and decorations
    # (label/separator) included -- has a widget ID and takes runtime
    # properties (UDISPLAY_PROP_ENABLED, UDISPLAY_PROP_VISIBLE, ...).
    __slots__ = ("_device", "_widget_id")
    def __init__(self, device, widget_id):
        self._device = device
        self._widget_id = widget_id
    @property
    def id(self):
        return self._widget_id
    def set_property(self, property_id, value):
        self._device.set_property(self._widget_id, property_id, value)
    def reset_property(self, property_id):
        self._device.reset_property(self._widget_id, property_id)


class ContainerWidget(Widget):
    # section/row/grid/dpad: a namespace for its children, which UI.__init__
    # attaches as attributes (ui.settings.rate). No __slots__ for that.
    def __init__(self, device, widget_id):
        Widget.__init__(self, device, widget_id)


class DisplayWidget(Widget):
    __slots__ = ()
    def set(self, value):
        self._device.send_float(self._widget_id, value)


class LedWidget(Widget):
    __slots__ = ()
    def set(self, value):
        self._device.send_bool(self._widget_id, value)


class RgbLedWidget(Widget):
    __slots__ = ()
    def set(self, value):
        self._device.send_int(self._widget_id, value)


class ToggleWidget(Widget):
    __slots__ = ("on_change",)
    def __init__(self, device, widget_id):
        Widget.__init__(self, device, widget_id)
        self.on_change = None
    def set(self, value):
        self._device.send_bool(self._widget_id, value)


class SliderWidget(Widget):
    __slots__ = ("on_change",)
    def __init__(self, device, widget_id):
        Widget.__init__(self, device, widget_id)
        self.on_change = None
    def set(self, value):
        self._device.send_float(self._widget_id, value)


class TextRwWidget(Widget):
    __slots__ = ("on_submit",)
    def __init__(self, device, widget_id):
        Widget.__init__(self, device, widget_id)
        self.on_submit = None
    def set(self, value):
        self._device.send_string(self._widget_id, value)


class TextRoWidget(Widget):
    __slots__ = ()
    def set(self, value):
        self._device.send_string(self._widget_id, value)


class DropdownWidget(Widget):
    __slots__ = ("on_change",)
    def __init__(self, device, widget_id):
        Widget.__init__(self, device, widget_id)
        self.on_change = None
    def set(self, index):
        self._device.send_uint8(self._widget_id, index)


class ButtonWidget(Widget):
    def __init__(self, device, widget_id):
        Widget.__init__(self, device, widget_id)
        self.on_press = None
        self.on_release = None
        self.on_click = None


# button-group items have the identical shape to a top-level button (no
# setter, three press/release/click callbacks) -- ButtonItem is the same
# class under the name generated code uses for button-group children, not
# a second implementation to keep in sync (maintainability review, 2026-09-11).
ButtonItem = ButtonWidget


class ButtonGroupWidget(Widget):
    # Exclusive selection is device-authoritative: an item press only fires
    # that item's callbacks; firmware confirms with set(), which pushes
    # STATE_UPDATE(group, uint8 item widget ID). Items are ButtonItem
    # sub-attributes, assigned in UI.__init__ below along with _items, the
    # tuple of their widget IDs.
    def __init__(self, device, widget_id):
        Widget.__init__(self, device, widget_id)
        self._items = ()
    def set(self, item):
        # item: one of this group's ButtonItem attributes (ui.mode.fast) or
        # its WIDGET_ID_* constant. Anything else (another group's item, a
        # stray int) raises ValueError -- the runtime counterpart of the C++
        # per-group Item enum, instead of silently selecting nothing.
        if isinstance(item, ButtonItem):
            item = item._widget_id
        if item not in self._items:
            raise ValueError("not an item of this button-group")
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


def _py_instantiate(node, widget_types: dict) -> list:
    """UI.__init__ body lines constructing `self.<path>` and, recursively,
    its children (`self.<path>.<child>`), in YAML declaration order."""
    type_str = widget_types.get(node.path, "")
    if type_str in CONTAINER_TYPES:
        cls = "ContainerWidget"
    else:
        cls = _WRAPPER_CLASS.get(type_str, "Widget")
    const = f"WIDGET_ID_{_macro_name(node.path)}"
    lines = [f"        self.{node.path} = {cls}(self._device, {const})"]
    for child in node.children:
        lines.extend(_py_instantiate(child, widget_types))
    if type_str == "button-group":
        # Trailing comma keeps a one-item group a tuple.
        item_consts = [
            f"WIDGET_ID_{_macro_name(child.path)}" for child in node.children
            if widget_types.get(child.path) == "button-group-item"
        ]
        lines.append(f"        self.{node.path}._items = ({''.join(c + ', ' for c in item_consts).rstrip()})")
    return lines


def _py_dispatch_cases(nodes: list, widget_types: dict, widget_ids: dict) -> list:
    """`if widget_id == ...: ... elif widget_id == ...` body for
    UI._on_event, one branch per widget that raises events, at any depth.
    Mirrors cpp_backend.py's _cpp_dispatch_cases."""
    lines: list = []

    def kw() -> str:
        return "if" if not lines else "elif"

    for node in nodes:
        path = node.path
        type_str = widget_types.get(path, "")
        if path not in widget_ids:
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
        elif type_str in ("button", "button-group-item"):
            lines.extend([
                f"        {kw()} widget_id == {const}:",
                f"            if event_type == UDISPLAY_EVENT_BUTTON_PRESS and self.{path}.on_press:",
                f"                self.{path}.on_press()",
                f"            elif event_type == UDISPLAY_EVENT_BUTTON_RELEASE and self.{path}.on_release:",
                f"                self.{path}.on_release()",
                f"            elif event_type == UDISPLAY_EVENT_BUTTON_CLICK and self.{path}.on_click:",
                f"                self.{path}.on_click()",
            ])
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

    tree = widget_tree(widgets_yaml)

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

    for node in tree:
        lines.extend(_py_instantiate(node, widget_types))

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

    dispatch = _py_dispatch_cases(walk(tree), widget_types, widget_ids)
    if dispatch:
        lines.extend(dispatch)
    else:
        lines.append("        pass")

    return "\n".join(lines) + "\n"
