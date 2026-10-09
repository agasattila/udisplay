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


def _cpp_base_classes(variant: str) -> list:
    def handler(sig: str) -> str:
        name, _, rest = sig.partition("(")
        args = rest.rstrip(")")
        if variant == "safe":
            return f"    void (*{name})({args}) = nullptr;"
        if args and args != "void":
            arg_types = ", ".join(p.strip().rsplit(" ", 1)[0] for p in args.split(","))
        else:
            arg_types = ""
        return f"    std::function<void({arg_types})> {name};"

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
        "class DisplayWidget : public OutputWidget<float>    { public: DisplayWidget(udisplay_t* ctx, uint8_t id)    : OutputWidget(ctx, id) {} };",
        "class LedWidget     : public OutputWidget<bool>     { public: LedWidget(udisplay_t* ctx, uint8_t id)     : OutputWidget(ctx, id) {} };",
        "class RgbLedWidget  : public OutputWidget<uint32_t> { public: RgbLedWidget(udisplay_t* ctx, uint8_t id)  : OutputWidget(ctx, id) {} };",
        "",
        "class ToggleWidget : public OutputWidget<bool> {",
        "public:",
        "    ToggleWidget(udisplay_t* ctx, uint8_t id) : OutputWidget(ctx, id) {}",
        handler("on_change(bool state)"),
        "};",
        "",
        "class SliderWidget : public OutputWidget<float> {",
        "public:",
        "    SliderWidget(udisplay_t* ctx, uint8_t id) : OutputWidget(ctx, id) {}",
        handler("on_change(float value)"),
        "};",
        "",
        "class ButtonWidget : public Widget {",
        "public:",
        "    ButtonWidget(udisplay_t* ctx, uint8_t id) : Widget(ctx, id) {}",
        handler("on_press()"),
        handler("on_release()"),
        handler("on_click()"),
        "};",
        "",
        "class ButtonItem : public Widget {",
        "public:",
        "    ButtonItem(udisplay_t* ctx, uint8_t id) : Widget(ctx, id) {}",
        handler("on_press()"),
        handler("on_release()"),
        handler("on_click()"),
        "};",
        "",
        "class TextRwWidget : public Widget {",
        "public:",
        "    TextRwWidget(udisplay_t* ctx, uint8_t id) : Widget(ctx, id) {}",
        "    void set(const char* s, uint8_t n) { udisplay_send_string(_ctx, _id, s, n); }",
        handler("on_submit(const char* str, uint8_t len)"),
        "};",
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
    def handler(sig: str) -> str:
        name, _, rest = sig.partition("(")
        args = rest.rstrip(")")
        if variant == "safe":
            return f"    void (*{name})({args}) = nullptr;"
        if args and args != "void":
            arg_types = ", ".join(p.strip().rsplit(" ", 1)[0] for p in args.split(","))
        else:
            arg_types = ""
        return f"    std::function<void({arg_types})> {name};"

    lines: list = []

    def emit(node) -> None:
        nonlocal lines
        for child in node.children:
            emit(child)
        type_str = widget_types.get(node.path, "")
        cn = _cpp_class_name(node.name_path)

        if type_str == "dropdown":
            items = dropdown_items.get(node.path, [])
            lines += [
                f"class {cn} : public OutputWidget<uint8_t> {{",
                "    using OutputWidget::OutputWidget;",
                "public:",
                "    enum class Option : uint8_t {",
            ]
            for idx, (item_key, _) in enumerate(items):
                comma = "," if idx < len(items) - 1 else ""
                lines.append(f"        {item_key} = {idx}u{comma}")
            lines += [
                "    };",
                f"    void set(Option v) {{ OutputWidget<uint8_t>::set(static_cast<uint8_t>(v)); }}",
                handler("on_change(Option selection)"),
                "};",
                "",
            ]
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
            lines.append("    enum class Item : uint8_t {")
            for idx, child in enumerate(node.children):
                comma = "," if idx < len(node.children) - 1 else ""
                lines.append(f"        {child.key} = 0x{widget_ids[child.path]:02X}u{comma}")
            lines += [
                "    };",
                "    void set(Item v) { udisplay_send_uint8(_ctx, _id, static_cast<uint8_t>(v)); }",
                "    void clear()     { udisplay_send_uint8(_ctx, _id, 0u); }",
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
            cn = _cpp_class_name(path)
            lines += [
                f"    case 0x{wid:02X}u:",
                f"        if (self->{path}.on_change) self->{path}.on_change(static_cast<{cn}::Option>(ev->selection_index));",
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

    lines.append("")
    if variant == "safe":
        lines.append("    void (*on_client_ready)() = nullptr;")
        lines.append("    void (*on_comms_error)() = nullptr;")
    else:
        lines.append("    std::function<void()> on_client_ready;")
        lines.append("    std::function<void()> on_comms_error;")
    lines.append("")
    lines.append("    UDisplay()")
    for i, node in enumerate(top_members):
        prefix = "        : " if i == 0 else "        , "
        lines.append(f"{prefix}{node.key}(&_ctx, 0x{widget_ids[node.path]:02X}u)")
    lines += [
        "    {}",
        "",
        "    /* Non-copyable, non-movable: widgets above cache a raw pointer to _ctx's",
        "     * storage (taken at construction, see the member list above). Moving or",
        "     * copying this object would leave every widget's cached pointer dangling",
        "     * or aliased to the wrong instance -- so its address must stay fixed for",
        "     * its whole lifetime. Construct once (static, global, or in-place). */",
        "    UDisplay(const UDisplay&) = delete;",
        "    UDisplay& operator=(const UDisplay&) = delete;",
        "    UDisplay(UDisplay&&) = delete;",
        "    UDisplay& operator=(UDisplay&&) = delete;",
        "",
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

    dispatch = _cpp_dispatch_cases(walk(tree), widget_types, widget_ids)
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
