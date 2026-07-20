# SPDX-License-Identifier: MPL-2.0
# Copyright (c) 2026 Attila Agas

"""C++ codegen backend: produces udisplay_ui.hpp + udisplay_ui.bin."""
from __future__ import annotations

import math
import re
from typing import List

from ..merkle import CHUNK_SIZE
from ..widget_ids import collect_dropdown_items
from . import BuildContext, OutputFile
from ._shared import (
    _hex_rows, _HEADER_COMMENT,
    _config_fields, _config_sequential_assignment,
)


def generate(ctx: BuildContext) -> List[OutputFile]:
    return [
        OutputFile("udisplay_ui.hpp", _generate_header_cpp(ctx)),
        OutputFile("udisplay_ui.bin", ctx.blob),
    ]


# ── helpers ───────────────────────────────────────────────────────────────────

def _cpp_class_name(key: str) -> str:
    """'wifi_mode' -> 'WifiModeWidget'"""
    parts = re.split(r"[^a-zA-Z0-9]+", key)
    return "".join(p.capitalize() for p in parts if p) + "Widget"


def _cpp_ordered_toplevel(widgets_yaml: dict, widget_types: dict) -> list:
    """Return top-level widget paths in YAML declaration order (containers transparent)."""
    result: list = []
    container_types = {"section", "row", "grid"}
    no_id_types = {"label", "separator"}
    for key, widget in widgets_yaml.items():
        if not isinstance(widget, dict):
            continue
        wtype = widget.get("type", "")
        if wtype in no_id_types:
            continue
        if wtype in container_types:
            for child_path in _cpp_ordered_toplevel(widget.get("widgets", {}), widget_types):
                if child_path not in result:
                    result.append(child_path)
            continue
        if key in widget_types:
            result.append(key)
    return result


def _cpp_sub_members(path: str, widget_types: dict, widget_ids: dict) -> list:
    """Return [(sub_key, type_str, widget_id)] for sub-members, alphabetical order."""
    prefix = path + "."
    return [
        (sub_path[len(prefix):], widget_types[sub_path], widget_ids[sub_path])
        for sub_path in sorted(widget_ids)
        if sub_path.startswith(prefix)
    ]


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
        "class Widget {",
        "protected:",
        "    uint8_t _id;",
        "    explicit Widget(uint8_t id) : _id(id) {}",
        "    friend class UDisplay;",
        "};",
        "",
        "template<typename T>",
        "class OutputWidget : public Widget {",
        "public:",
        "    explicit OutputWidget(uint8_t id) : Widget(id) {}",
        "    void set(T v);",
        "};",
        "",
        "template<> inline void OutputWidget<bool>::set(bool v)         { udisplay_send_bool(_id, v); }",
        "template<> inline void OutputWidget<float>::set(float v)       { udisplay_send_float(_id, v); }",
        "template<> inline void OutputWidget<uint32_t>::set(uint32_t v) { udisplay_send_int(_id, static_cast<int32_t>(v)); }",
        "template<> inline void OutputWidget<uint8_t>::set(uint8_t v)   { udisplay_send_uint8(_id, v); }",
        "",
        "/* -- Standard concrete widget classes --------------------------------------- */",
        "",
        "class DisplayWidget : public OutputWidget<float>    { public: explicit DisplayWidget(uint8_t id) : OutputWidget(id) {} };",
        "class LedWidget     : public OutputWidget<bool>     { public: explicit LedWidget(uint8_t id)     : OutputWidget(id) {} };",
        "class RgbLedWidget  : public OutputWidget<uint32_t> { public: explicit RgbLedWidget(uint8_t id)  : OutputWidget(id) {} };",
        "",
        "class ToggleWidget : public OutputWidget<bool> {",
        "public:",
        "    explicit ToggleWidget(uint8_t id) : OutputWidget(id) {}",
        handler("on_change(bool state)"),
        "};",
        "",
        "class SliderWidget : public OutputWidget<float> {",
        "public:",
        "    explicit SliderWidget(uint8_t id) : OutputWidget(id) {}",
        handler("on_change(float value)"),
        "};",
        "",
        "class ButtonWidget : public Widget {",
        "public:",
        "    explicit ButtonWidget(uint8_t id) : Widget(id) {}",
        handler("on_press()"),
        handler("on_release()"),
        handler("on_click()"),
        "};",
        "",
        "class ButtonItem : public Widget {",
        "public:",
        "    explicit ButtonItem(uint8_t id) : Widget(id) {}",
        handler("on_press()"),
        handler("on_release()"),
        handler("on_click()"),
        "};",
        "",
        "class TextRwWidget : public Widget {",
        "public:",
        "    explicit TextRwWidget(uint8_t id) : Widget(id) {}",
        "    void set(const char* s, uint8_t n) { udisplay_send_string(_id, s, n); }",
        handler("on_submit(const char* str, uint8_t len)"),
        "};",
        "",
        "class TextRoWidget : public Widget {",
        "public:",
        "    explicit TextRoWidget(uint8_t id) : Widget(id) {}",
        "    void set(const char* s, uint8_t n) { udisplay_send_string(_id, s, n); }",
        "};",
    ]
    return lines


def _cpp_generated_classes(
    widget_types: dict,
    widget_ids: dict,
    dropdown_items: dict,
    variant: str,
) -> list:
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
    header_emitted = False

    for path in sorted(widget_types):
        if "." in path:
            continue
        type_str = widget_types[path]

        if type_str == "button":
            sub = _cpp_sub_members(path, widget_types, widget_ids)
            if not sub:
                continue
            if not header_emitted:
                lines += ["", "/* -- Generated per-widget derived classes ----------------------------------- */", ""]
                header_emitted = True
            cn = _cpp_class_name(path)
            lines += [f"class {cn} : public Widget {{", "public:",
                      handler("on_press()"), handler("on_release()"), handler("on_click()")]
            for sub_key, _, _ in sub:
                lines.append(f"    LedWidget {sub_key};")
            params = ["uint8_t id"] + [f"uint8_t {sk}_id" for sk, _, _ in sub]
            inits = ["Widget(id)"] + [f"{sk}({sk}_id)" for sk, _, _ in sub]
            lines += [
                f"    explicit {cn}({', '.join(params)})",
                f"        : {', '.join(inits)} {{}}",
                "};",
                "",
            ]

        elif type_str == "button-group":
            sub = _cpp_sub_members(path, widget_types, widget_ids)
            if not header_emitted:
                lines += ["", "/* -- Generated per-widget derived classes ----------------------------------- */", ""]
                header_emitted = True
            cn = _cpp_class_name(path)
            lines += [f"class {cn} : public Widget {{", "public:"]
            for sub_key, _, _ in sub:
                lines.append(f"    ButtonItem {sub_key};")
            params = ["uint8_t group_id"] + [f"uint8_t {sk}_id" for sk, _, _ in sub]
            inits = ["Widget(group_id)"] + [f"{sk}({sk}_id)" for sk, _, _ in sub]
            lines += [
                f"    explicit {cn}({', '.join(params)})",
                f"        : {', '.join(inits)} {{}}",
                "};",
                "",
            ]

        elif type_str == "dropdown":
            items = dropdown_items.get(path, [])
            if not header_emitted:
                lines += ["", "/* -- Generated per-widget derived classes ----------------------------------- */", ""]
                header_emitted = True
            cn = _cpp_class_name(path)
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

    return lines


def _cpp_member_type(path: str, type_str: str, widget_types: dict, widget_ids: dict) -> str:
    type_map = {
        "display": "DisplayWidget",
        "led":     "LedWidget",
        "rgbled":  "RgbLedWidget",
        "toggle":  "ToggleWidget",
        "slider":  "SliderWidget",
        "text-rw": "TextRwWidget",
        "text-ro": "TextRoWidget",
    }
    if type_str in type_map:
        return type_map[type_str]
    if type_str == "button":
        if _cpp_sub_members(path, widget_types, widget_ids):
            return _cpp_class_name(path)
        return "ButtonWidget"
    if type_str in ("button-group", "dropdown"):
        return _cpp_class_name(path)
    return "Widget"


def _cpp_ctor_args(path: str, type_str: str, widget_ids: dict, widget_types: dict) -> str:
    wid = widget_ids[path]
    if type_str in ("button", "button-group"):
        sub = _cpp_sub_members(path, widget_types, widget_ids)
        sub_ids = "".join(f", 0x{sw:02X}u" for _, _, sw in sub)
        return f"0x{wid:02X}u{sub_ids}"
    return f"0x{wid:02X}u"


def _cpp_dispatch_cases(
    ordered: list,
    widget_types: dict,
    widget_ids: dict,
    dropdown_items: dict,
) -> list:
    lines: list = []
    for path in ordered:
        type_str = widget_types.get(path, "")
        wid = widget_ids.get(path)
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
        elif type_str == "button":
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
        for sub_path in sorted(widget_ids):
            if not sub_path.startswith(path + "."):
                continue
            sub_key = sub_path[len(path) + 1:]
            if widget_types.get(sub_path) == "button-group-item":
                sw = widget_ids[sub_path]
                lines += [
                    f"    case 0x{sw:02X}u:",
                    "        switch (ev->event_type) {",
                    f"            case UDISPLAY_EVENT_BUTTON_PRESS:   if (self->{path}.{sub_key}.on_press)   self->{path}.{sub_key}.on_press();   break;",
                    f"            case UDISPLAY_EVENT_BUTTON_RELEASE: if (self->{path}.{sub_key}.on_release) self->{path}.{sub_key}.on_release(); break;",
                    f"            case UDISPLAY_EVENT_BUTTON_CLICK:   if (self->{path}.{sub_key}.on_click)   self->{path}.{sub_key}.on_click();   break;",
                    "            default: break;",
                    "        }",
                    "        break;",
                ]
    return lines


def _generate_header_cpp(ctx: BuildContext) -> str:
    widget_ids    = ctx.widget_ids
    blob          = ctx.blob
    root          = ctx.root
    hashes        = ctx.hashes
    source        = ctx.source
    widget_types  = ctx.widget_types
    widgets_yaml  = ctx.widgets_yaml
    variant       = ctx.variant
    version       = ctx.version

    n = math.ceil(len(blob) / CHUNK_SIZE)
    dropdown_items = collect_dropdown_items(widgets_yaml)
    ordered = _cpp_ordered_toplevel(widgets_yaml, widget_types)

    lines = [
        _HEADER_COMMENT.format(source=source, root_hex=root.hex(), version=version),
        "#pragma once",
        "#include <stdint.h>",
        "#include <stddef.h>",
        '#include "udisplay.h"',
    ]
    if variant == "modern":
        lines.append("#include <functional>")

    lines += [
        "",
        "namespace udisplay_ui {",
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
    lines.extend(_cpp_generated_classes(widget_types, widget_ids, dropdown_items, variant))

    lines += [
        "",
        "/* -- UDisplay: top-level aggregate ----------------------------------------- */",
        "",
        "class UDisplay {",
        "public:",
    ]

    for path in ordered:
        type_str = widget_types.get(path, "")
        cpp_type = _cpp_member_type(path, type_str, widget_types, widget_ids)
        lines.append(f"    {cpp_type} {path};")

    lines.append("")
    if variant == "safe":
        lines.append("    void (*on_client_ready)() = nullptr;")
        lines.append("    void (*on_comms_error)() = nullptr;")
    else:
        lines.append("    std::function<void()> on_client_ready;")
        lines.append("    std::function<void()> on_comms_error;")
    lines.append("")
    lines.append("    UDisplay()")
    for i, path in enumerate(ordered):
        type_str = widget_types.get(path, "")
        args = _cpp_ctor_args(path, type_str, widget_ids, widget_types)
        prefix = "        : " if i == 0 else "        , "
        lines.append(f"{prefix}{path}({args})")
    lines += [
        "    {}",
        "",
        "    void init(udisplay_send_fn send, udisplay_transport_t transport);",
        "    void feed(const uint8_t* data, uint16_t len);",
        "    static int ble_set_mtu(uint16_t mtu_payload);",
        "    static uint16_t tcp_frame(uint8_t* out, uint16_t cap, const uint8_t* msg, uint16_t len);",
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
        "    udisplay_init(&cfg);",
        "}",
        "",
        "inline void UDisplay::feed(const uint8_t* data, uint16_t len)",
        "{",
        "    udisplay_feed(data, len);",
        "}",
        "",
        "inline int UDisplay::ble_set_mtu(uint16_t mtu_payload)",
        "{",
        "    return udisplay_ble_set_mtu(mtu_payload);",
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

    dispatch = _cpp_dispatch_cases(ordered, widget_types, widget_ids, dropdown_items)
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
        "} // namespace udisplay_ui",
    ]

    return "\n".join(lines) + "\n"
