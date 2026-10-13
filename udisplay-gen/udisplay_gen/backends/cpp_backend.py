# SPDX-License-Identifier: MPL-2.0
# Copyright (c) 2026 Attila Agas

"""C++ codegen backend: produces udisplay_ui.hpp + udisplay_ui.bin."""
from __future__ import annotations

import math
import re
from typing import List

from ..merkle import CHUNK_SIZE
from ..widget_ids import collect_dropdown_items, scope_members, walk, widget_tree
from . import BuildContext, OutputFile
from ._shared import (
    _hex_rows, _HEADER_COMMENT, widget_name_errors,
    _config_fields, _config_sequential_assignment,
    _ns_validate, _ns_fn,
)


def generate(ctx: BuildContext) -> List[OutputFile]:
    _ns_validate(ctx.namespace)
    _validate_cpp_identifiers(ctx)
    return [
        OutputFile("udisplay_ui.hpp", _generate_header_cpp(ctx)),
        OutputFile("udisplay_ui.bin", ctx.blob),
    ]


# ── helpers ───────────────────────────────────────────────────────────────────

def _cpp_class_name(key: str) -> str:
    """'wifi_mode' -> 'WifiModeWidget'"""
    parts = re.split(r"[^a-zA-Z0-9]+", key)
    return "".join(p.capitalize() for p in parts if p) + "Widget"



# UDisplay's own fixed members — a widget member with one of these names
# would not compile (or would silently shadow the method).
_UDISPLAY_RESERVED_NAMES = frozenset({
    "UDisplay", "_ctx", "init", "feed", "ble_set_mtu", "tcp_frame", "ctx",
    "on_client_ready", "on_comms_error", "_dispatch", "_on_ready", "_on_comms_error",
})

# Every generated widget class derives from Widget — a sub-member must not
# shadow its public API or protected state.
_WIDGET_RESERVED_NAMES = frozenset({"Widget", "_ctx", "_id", "id", "set_property", "reset_property"})
_BUTTON_RESERVED_NAMES = _WIDGET_RESERVED_NAMES | {"on_press", "on_release", "on_click"}

_CPP_KEYWORDS = frozenset("""
alignas alignof and and_eq asm auto bitand bitor bool break case catch char
char8_t char16_t char32_t class compl concept const consteval constexpr
constinit const_cast continue co_await co_return co_yield decltype default
delete do double dynamic_cast else enum explicit export extern false float
for friend goto if inline int long mutable namespace new noexcept not not_eq
nullptr operator or or_eq private protected public register reinterpret_cast
requires return short signed sizeof static static_assert static_cast struct
switch template this thread_local throw true try typedef typeid typename
union unsigned using virtual void volatile wchar_t while xor xor_eq
""".split())


def _cpp_child_reserved_names(type_str: str) -> frozenset:
    """Names a child member of a widget of this type must not take: the
    generated class derives from Widget (and ButtonWidget for a button)."""
    if type_str == "button":
        return _BUTTON_RESERVED_NAMES
    if type_str == "button-group":
        return _WIDGET_RESERVED_NAMES | {"Item", "set", "clear"}
    return _WIDGET_RESERVED_NAMES


def _validate_cpp_identifiers(ctx: BuildContext) -> None:
    """Reject widget names that would produce a non-compiling (or silently
    shadowing) C++ header: C++ keywords, names colliding with UDisplay's or
    the parent class's own members, and paths that normalize to the same
    generated class name (`a_b` and `a.b` are both `ABWidget`). Every widget
    is a member (issue #43) of its naming scope's class: UDisplay for the
    top-level scope, a namespace widget's generated class otherwise
    (scope_members()). Raises ValueError listing every violation."""
    errors: list = []

    def check(name: str, reserved: frozenset, where: str) -> None:
        if name in _CPP_KEYWORDS:
            errors.append(f"{where}: '{name}' is a C++ keyword")
        elif name in reserved:
            errors.append(
                f"{where}: '{name}' collides with a member of the generated C++ API"
            )

    tree = widget_tree(ctx.widgets_yaml or {})
    for node in scope_members(tree):
        check(node.key, _UDISPLAY_RESERVED_NAMES, f"widget '{node.path}'")
    for node in walk(tree):
        if node.transparent:
            continue
        reserved = _cpp_child_reserved_names(node.type)
        for child in scope_members(node.children):
            check(child.key, reserved, f"widget '{child.path}'")

    errors += widget_name_errors(ctx.widget_ids, ctx.widgets_yaml)

    if errors:
        raise ValueError(
            "Cannot generate valid C++ code from this YAML:\n"
            + "\n".join(f"  - {e}" for e in errors)
        )


def _cpp_handler(sig: str, variant: str) -> tuple:
    """(member declaration, constructor initializer or None) for an event
    handler member. The default ("safe") variant is C++98: a raw function
    pointer, null-initialized in the constructor (no in-class initializer,
    no nullptr). A std::function default-constructs empty."""
    name, _, rest = sig.partition("(")
    args = rest.rstrip(")")
    if variant == "safe":
        return f"    void (*{name})({args});", f"{name}(0)"
    if args and args != "void":
        arg_types = ", ".join(p.strip().rsplit(" ", 1)[0] for p in args.split(","))
    else:
        arg_types = ""
    return f"    std::function<void({arg_types})> {name};", None


def _cpp_class_with_handlers(cls: str, base: str, sigs: list, variant: str, body: list = ()) -> list:
    """A widget class whose constructor forwards to `base` and null-initializes
    its handler members (in declaration order, so -Wreorder stays quiet)."""
    handlers = [_cpp_handler(sig, variant) for sig in sigs]
    inits = [f"{base}(ctx, id)"] + [init for _, init in handlers if init]
    return [
        f"class {cls} : public {base} {{",
        "public:",
        f"    {cls}(udisplay_t* ctx, uint8_t id) : {', '.join(inits)} {{}}",
        *body,
        *(decl for decl, _ in handlers),
        "};",
    ]


def _cpp_enum_type(name: str, variant: str) -> str:
    """How generated code spells a widget's value enum type: an enum nested
    in a struct for the C++98 default variant (`Item::Value`), an
    `enum class` for --modern (`Item`). Enumerators are `Item::x` in both."""
    return f"{name}::Value" if variant == "safe" else name


def _cpp_enum(name: str, entries: list, variant: str) -> list:
    """Declare a widget's value enum; `entries` is [(enumerator, value)].
    C++98 has no scoped enums. The wrapping struct scopes the enumerators
    (`Item::fast`) so they can't collide with same-named members of the
    enclosing class (a button-group's item members are also called `fast`)."""
    body = [
        f"{key} = {value}" + ("," if i < len(entries) - 1 else "")
        for i, (key, value) in enumerate(entries)
    ]
    if variant == "safe":
        return (
            [f"    struct {name} {{", "        enum Value {"]
            + [f"            {line}" for line in body]
            + ["        };", "    };"]
        )
    return [f"    enum class {name} : uint8_t {{"] + [f"        {line}" for line in body] + ["    };"]


def _cpp_base_classes(variant: str) -> list:
    lines = [
        "/* -- Widget base classes ---------------------------------------------------- */",
        "",
        "/* Every widget -- containers (section/row/grid/dpad) and decorations",
        " * (label/separator) included -- has a widget ID, so every widget can take",
        " * runtime properties (UDISPLAY_PROP_ENABLED, UDISPLAY_PROP_VISIBLE, ...). */",
        "class Widget {",
        "public:",
        "    Widget(udisplay_t* ctx, uint8_t id) : _ctx(ctx), _id(id) {}",
        "    uint8_t id() const { return _id; }",
        "    void set_property(uint8_t property_id, uint8_t value) { udisplay_set_property(_ctx, _id, property_id, value); }",
        "    void reset_property(uint8_t property_id) { udisplay_reset_property(_ctx, _id, property_id); }",
        "protected:",
        "    udisplay_t* _ctx;",
        "    uint8_t _id;",
        "    friend class UDisplay;",
        "};",
        "",
        "template<typename T>",
        "class OutputWidget : public Widget {",
        "public:",
        "    OutputWidget(udisplay_t* ctx, uint8_t id) : Widget(ctx, id) {}",
        "    void set(T v);",
        "};",
        "",
        "template<> inline void OutputWidget<bool>::set(bool v)         { udisplay_send_bool(_ctx, _id, v); }",
        "template<> inline void OutputWidget<float>::set(float v)       { udisplay_send_float(_ctx, _id, v); }",
        "template<> inline void OutputWidget<uint32_t>::set(uint32_t v) { udisplay_send_int(_ctx, _id, static_cast<int32_t>(v)); }",
        "template<> inline void OutputWidget<uint8_t>::set(uint8_t v)   { udisplay_send_uint8(_ctx, _id, v); }",
        "",
        "/* -- Standard concrete widget classes --------------------------------------- */",
        "",
        "class DisplayWidget : public OutputWidget<float>    { public: DisplayWidget(udisplay_t* ctx, uint8_t id)    : OutputWidget<float>(ctx, id) {} };",
        "class LedWidget     : public OutputWidget<bool>     { public: LedWidget(udisplay_t* ctx, uint8_t id)     : OutputWidget<bool>(ctx, id) {} };",
        "class RgbLedWidget  : public OutputWidget<uint32_t> { public: RgbLedWidget(udisplay_t* ctx, uint8_t id)  : OutputWidget<uint32_t>(ctx, id) {} };",
        "",
        *_cpp_class_with_handlers("ToggleWidget", "OutputWidget<bool>", ["on_change(bool state)"], variant),
        "",
        *_cpp_class_with_handlers("SliderWidget", "OutputWidget<float>", ["on_change(float value)"], variant),
        "",
        *_cpp_class_with_handlers("ButtonWidget", "Widget", ["on_press()", "on_release()", "on_click()"], variant),
        "",
        *_cpp_class_with_handlers("ButtonItem", "Widget", ["on_press()", "on_release()", "on_click()"], variant),
        "",
        *_cpp_class_with_handlers(
            "TextRwWidget", "Widget", ["on_submit(const char* str, uint8_t len)"], variant,
            ["    void set(const char* s, uint8_t n) { udisplay_send_string(_ctx, _id, s, n); }"],
        ),
        "",
        "class TextRoWidget : public Widget {",
        "public:",
        "    TextRoWidget(udisplay_t* ctx, uint8_t id) : Widget(ctx, id) {}",
        "    void set(const char* s, uint8_t n) { udisplay_send_string(_ctx, _id, s, n); }",
        "};",
    ]
    return lines


def _cpp_generated_classes(
    tree: list,
    widget_types: dict,
    widget_ids: dict,
    dropdown_items: dict,
    variant: str,
) -> list:
    """A generated class for every namespace widget with children (a
    flagged section/row/grid, a dpad, a button face, a button-group) and
    every dropdown, named after its name path. Its members are its naming
    scope (scope_members()), so access follows the name path
    (`ui.settings.rate`); a transparent container is a plain Widget whose
    children are members of the enclosing scope. Emitted children-first: a
    member's class must be complete before the class that holds it."""
    lines: list = []

    def emit(node) -> None:
        nonlocal lines
        for child in node.children:
            emit(child)
        type_str = widget_types.get(node.path, "")
        cn = _cpp_class_name(node.name_path)

        if type_str == "dropdown":
            items = dropdown_items.get(node.path, [])
            option = _cpp_enum_type("Option", variant)
            lines += _cpp_class_with_handlers(
                cn, "OutputWidget<uint8_t>", [f"on_change({option} selection)"], variant,
                _cpp_enum("Option", [(key, f"{idx}u") for idx, (key, _) in enumerate(items)], variant)
                + [f"    void set({option} v) {{ OutputWidget<uint8_t>::set(static_cast<uint8_t>(v)); }}"],
            ) + [""]
            return

        if not _cpp_has_class(node, type_str):
            return

        base = "ButtonWidget" if type_str == "button" else "Widget"
        lines += [f"class {cn} : public {base} {{", "public:"]
        if type_str == "button-group":
            # Exclusive selection is device-authoritative: an item press only
            # fires that item's handlers; firmware confirms via set(), which
            # pushes STATE_UPDATE(group, uint8 item widget ID). Item values
            # ARE the items' widget IDs; clear() sends 0 (reserved, no item).
            lines += _cpp_enum(
                "Item",
                [(child.key, f"0x{widget_ids[child.path]:02X}u") for child in node.children],
                variant,
            )
            lines += [
                f"    void set({_cpp_enum_type('Item', variant)} v) {{ udisplay_send_uint8(_ctx, _id, static_cast<uint8_t>(v)); }}",
                "    void clear() { udisplay_send_uint8(_ctx, _id, 0u); }",
            ]
        members = scope_members(node.children)
        for child in members:
            child_type = _cpp_member_type(child, widget_types.get(child.path, ""))
            lines.append(f"    {child_type} {child.key};")
        inits = [f"{base}(ctx, id)"] + [
            f"{child.key}(ctx, 0x{widget_ids[child.path]:02X}u)" for child in members
        ]
        lines += [
            f"    {cn}(udisplay_t* ctx, uint8_t id)",
            f"        : {', '.join(inits)} {{}}",
            "};",
            "",
        ]

    for node in tree:
        emit(node)
    if lines:
        lines = ["", "/* -- Generated per-widget derived classes ----------------------------------- */", ""] + lines
    return lines


# Widget types with a fixed (non-generated) C++ class. A widget with children
# gets a generated class; anything else (a childless container, a
# decoration) is a plain `Widget`.
_CPP_LEAF_CLASS = {
    "display": "DisplayWidget",
    "led":     "LedWidget",
    "rgbled":  "RgbLedWidget",
    "toggle":  "ToggleWidget",
    "slider":  "SliderWidget",
    "text-rw": "TextRwWidget",
    "text-ro": "TextRoWidget",
    "button":  "ButtonWidget",
    "button-group-item": "ButtonItem",
}


def _cpp_has_class(node, type_str: str) -> bool:
    """Whether _cpp_generated_classes emits a class for this widget."""
    return (bool(node.children) and not node.transparent) or type_str in ("button-group", "dropdown")


def _cpp_member_type(node, type_str: str) -> str:
    if _cpp_has_class(node, type_str):
        return _cpp_class_name(node.name_path)
    return _CPP_LEAF_CLASS.get(type_str, "Widget")


def _cpp_dispatch_cases(
    nodes: list,
    widget_types: dict,
    widget_ids: dict,
    dropdown_items: dict,
    variant: str,
) -> list:
    """One `case` per widget that raises events, at any depth. The member
    expression is the name path itself (`self->settings.rate`)."""
    lines: list = []
    for node in nodes:
        path = node.name_path
        type_str = widget_types.get(node.path, "")
        wid = widget_ids.get(node.path)
        if wid is None:
            continue
        if type_str == "toggle":
            lines += [
                f"    case 0x{wid:02X}u:",
                f"        if (self->{path}.on_change) self->{path}.on_change(ev->toggle_state != 0);",
                "        break;",
            ]
        elif type_str == "slider":
            lines += [
                f"    case 0x{wid:02X}u:",
                f"        if (self->{path}.on_change) self->{path}.on_change(ev->slider_value);",
                "        break;",
            ]
        elif type_str in ("button", "button-group-item"):
            lines += [
                f"    case 0x{wid:02X}u:",
                "        switch (ev->event_type) {",
                f"            case UDISPLAY_EVENT_BUTTON_PRESS:   if (self->{path}.on_press)   self->{path}.on_press();   break;",
                f"            case UDISPLAY_EVENT_BUTTON_RELEASE: if (self->{path}.on_release) self->{path}.on_release(); break;",
                f"            case UDISPLAY_EVENT_BUTTON_CLICK:   if (self->{path}.on_click)   self->{path}.on_click();   break;",
                "            default: break;",
                "        }",
                "        break;",
            ]
        elif type_str == "text-rw":
            lines += [
                f"    case 0x{wid:02X}u:",
                f"        if (self->{path}.on_submit) self->{path}.on_submit(ev->text.str, ev->text.len);",
                "        break;",
            ]
        elif type_str == "dropdown":
            # libudisplay passes the selection byte through unvalidated, and
            # casting an out-of-range value to a C++98 enum is unspecified:
            # ignore it, like an unknown widget ID. Indices are 0..N-1 (YAML
            # order, see collect_dropdown_items()), so N bounds the range. With
            # 256 items every byte is valid, and `< 256u` on a uint8_t would
            # trip -Wtype-limits (-Wextra), so the check is left out.
            option = f"{_cpp_class_name(path)}::{_cpp_enum_type('Option', variant)}"
            count = len(dropdown_items.get(node.path, []))
            in_range = f"ev->selection_index < {count}u && " if count < 256 else ""
            lines += [
                f"    case 0x{wid:02X}u:",
                f"        if ({in_range}self->{path}.on_change)",
                f"            self->{path}.on_change(static_cast<{option}>(ev->selection_index));",
                "        break;",
            ]
    return lines


def _generate_header_cpp(ctx: BuildContext) -> str:
    _ns_validate(ctx.namespace)
    widget_ids    = ctx.widget_ids
    blob          = ctx.blob
    root          = ctx.root
    hashes        = ctx.hashes
    source        = ctx.source
    widget_types  = ctx.widget_types
    widgets_yaml  = ctx.widgets_yaml
    variant       = ctx.variant
    version       = ctx.version
    ns_name       = _ns_fn(ctx.namespace, "udisplay_ui")

    n = math.ceil(len(blob) / CHUNK_SIZE)
    dropdown_items = collect_dropdown_items(widgets_yaml)
    tree = widget_tree(widgets_yaml)

    lines = [
        _HEADER_COMMENT.format(source=source, root_hex=root.hex(), version=version),
        "#pragma once",
        "#include <stdint.h>",
        "#include <stddef.h>",
        '#include "libudisplay/udisplay.h"',
    ]
    if variant == "modern":
        lines.append("#include <functional>")

    lines += [
        "",
        f"namespace {ns_name} {{",
        "",
        "/* -- Blob data + runtime state ---------------------------------------------- */",
        "namespace detail {",
        f"static const uint32_t CHUNK_COUNT = {n}u;",
        "",
        "static const uint8_t merkle_root[32] = {",
        _hex_rows(root),
        "};",
        "",
    ]

    for i in range(n):
        chunk = blob[i * CHUNK_SIZE : (i + 1) * CHUNK_SIZE]
        lines += [
            f"static const uint8_t chunk_{i}[{len(chunk)}] = {{",
            _hex_rows(chunk),
            "};",
            "",
        ]

    chunk_ptrs = ", ".join(f"chunk_{i}" for i in range(n))
    chunk_lens_str = ", ".join(
        str(len(blob[i * CHUNK_SIZE : (i + 1) * CHUNK_SIZE])) for i in range(n)
    )
    lines += [
        f"static const uint8_t* const chunks[{n}] = {{ {chunk_ptrs} }};",
        f"static const uint16_t chunk_lens[{n}] = {{ {chunk_lens_str} }};",
        "",
    ]

    for i, h in enumerate(hashes):
        lines += [
            f"static const uint8_t chunk_hash_{i}[32] = {{",
            _hex_rows(h),
            "};",
            "",
        ]
    hash_ptrs = ", ".join(f"chunk_hash_{i}" for i in range(n))
    lines += [
        f"static const uint8_t* const chunk_hashes[{n}] = {{ {hash_ptrs} }};",
        "} // namespace detail",
        "",
    ]

    lines.extend(_cpp_base_classes(variant))
    lines.extend(_cpp_generated_classes(tree, widget_types, widget_ids, dropdown_items, variant))

    lines += [
        "",
        "/* -- UDisplay: top-level aggregate ----------------------------------------- */",
        "",
        "class UDisplay {",
        "private:",
        "    /* Declared first (before the public widget members below) so it is",
        "     * constructed first -- C++ initializes members in DECLARATION order,",
        "     * not initializer-list order, and every widget member below takes",
        "     * &_ctx in its own constructor. */",
        "    udisplay_t _ctx;",
        "",
        "public:",
    ]

    top_members = scope_members(tree)
    for node in top_members:
        cpp_type = _cpp_member_type(node, widget_types.get(node.path, ""))
        lines.append(f"    {cpp_type} {node.key};")

    ready_decl, ready_init = _cpp_handler("on_client_ready()", variant)
    error_decl, error_init = _cpp_handler("on_comms_error()", variant)
    lines += ["", ready_decl, error_decl, "", "    UDisplay()"]
    inits = [f"{node.key}(&_ctx, 0x{widget_ids[node.path]:02X}u)" for node in top_members]
    inits += [init for init in (ready_init, error_init) if init]
    for i, init in enumerate(inits):
        lines.append(("        : " if i == 0 else "        , ") + init)
    lines += ["    {}", ""]

    no_copy_comment = [
        "    /* Non-copyable, non-movable: widgets above cache a raw pointer to _ctx's",
        "     * storage (taken at construction, see the member list above). Moving or",
        "     * copying this object would leave every widget's cached pointer dangling",
        "     * or aliased to the wrong instance -- so its address must stay fixed for",
        "     * its whole lifetime. Construct once (static, global, or in-place). */",
    ]
    if variant == "modern":
        lines += no_copy_comment + [
            "    UDisplay(const UDisplay&) = delete;",
            "    UDisplay& operator=(const UDisplay&) = delete;",
            "    UDisplay(UDisplay&&) = delete;",
            "    UDisplay& operator=(UDisplay&&) = delete;",
            "",
        ]
        no_copy_private = []
    else:
        # C++98 idiom: declared private, never defined. A user-declared copy
        # constructor also suppresses the implicit move operations (C++11+).
        no_copy_private = [""] + no_copy_comment + [
            "    UDisplay(const UDisplay&);",
            "    UDisplay& operator=(const UDisplay&);",
        ]

    lines += [
        "    void init(udisplay_send_fn send, udisplay_transport_t transport);",
        "    void feed(const uint8_t* data, uint16_t len);",
        "    int ble_set_mtu(uint16_t mtu_payload);",
        "    static uint16_t tcp_frame(uint8_t* out, uint16_t cap, const uint8_t* msg, uint16_t len);",
        "",
        "    /* Raw handle accessor -- call the ctx-taking core API in udisplay.h",
        "     * directly for anything this class doesn't wrap (heartbeat,",
        "     * on_connect/on_disconnect, set_property/reset_property, ...). */",
        "    udisplay_t* ctx() { return &_ctx; }",
        "",
        "private:",
        "    static void _dispatch(const udisplay_event_t* ev, void* ud);",
        "    static void _on_ready(void* ud);",
        "    static void _on_comms_error(void* ud);",
    ] + no_copy_private + [
        "};",
        "",
        "inline void UDisplay::init(udisplay_send_fn send, udisplay_transport_t transport)",
        "{",
        "    udisplay_config_t cfg;",
    ] + [
        "    " + l for l in _config_sequential_assignment(
            "cfg",
            _config_fields(
                merkle_root_expr="detail::merkle_root",
                chunks_expr="detail::chunks",
                chunk_hashes_expr="detail::chunk_hashes",
                chunk_lens_expr="detail::chunk_lens",
                chunk_count_expr="detail::CHUNK_COUNT",
                send_expr="send",
                on_event_expr="UDisplay::_dispatch",
                on_ready_expr="UDisplay::_on_ready",
                on_error_expr="UDisplay::_on_comms_error",
                userdata_expr="this",
                transport_expr="transport",
            ),
        )
    ] + [
        "    udisplay_init(&_ctx, &cfg);",
        "}",
        "",
        "inline void UDisplay::feed(const uint8_t* data, uint16_t len)",
        "{",
        "    udisplay_feed(&_ctx, data, len);",
        "}",
        "",
        "inline int UDisplay::ble_set_mtu(uint16_t mtu_payload)",
        "{",
        "    return udisplay_ble_set_mtu(&_ctx, mtu_payload);",
        "}",
        "",
        "inline uint16_t UDisplay::tcp_frame(uint8_t* out, uint16_t cap, const uint8_t* msg, uint16_t len)",
        "{",
        "    return udisplay_tcp_frame(out, cap, msg, len);",
        "}",
        "",
        "inline void UDisplay::_dispatch(const udisplay_event_t* ev, void* ud)",
        "{",
        "    UDisplay* self = static_cast<UDisplay*>(ud);",
    ]

    dispatch = _cpp_dispatch_cases(walk(tree), widget_types, widget_ids, dropdown_items, variant)
    if dispatch:
        lines.append("    switch (ev->widget_id) {")
        lines.extend(dispatch)
        lines += ["    default: break;", "    }"]

    lines += [
        "}",
        "",
        "inline void UDisplay::_on_ready(void* ud)",
        "{",
        "    UDisplay* self = static_cast<UDisplay*>(ud);",
        "    if (self->on_client_ready) self->on_client_ready();",
        "}",
        "",
        "inline void UDisplay::_on_comms_error(void* ud)",
        "{",
        "    UDisplay* self = static_cast<UDisplay*>(ud);",
        "    if (self->on_comms_error) self->on_comms_error();",
        "}",
        "",
        f"}} // namespace {ns_name}",
    ]

    return "\n".join(lines) + "\n"
