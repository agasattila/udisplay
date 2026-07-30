# SPDX-License-Identifier: MPL-2.0
# Copyright (c) 2026 Attila Agas

"""C codegen backend: produces udisplay_ui.h + udisplay_ui.c + udisplay_ui.bin."""
from __future__ import annotations

import math
from typing import List

from ..merkle import CHUNK_SIZE
from ..widget_ids import collect_dropdown_items
from . import BuildContext, OutputFile
from ._shared import (
    _macro_name, _fn_suffix,
    _setter_for_type, _handler_for_type,
    _hex_rows, _HEADER_COMMENT,
    _config_fields, _config_designated_initializer,
    _ns_validate, _ns_macro, _ns_fn,
)


def generate(ctx: BuildContext) -> List[OutputFile]:
    _ns_validate(ctx.namespace)
    return [
        OutputFile("udisplay_ui.h", _generate_header(ctx)),
        OutputFile("udisplay_ui.c", _generate_source(ctx)),
        OutputFile("udisplay_ui.bin", ctx.blob),
    ]


def _generate_header(ctx: BuildContext) -> str:
    _ns_validate(ctx.namespace)
    widget_ids   = ctx.widget_ids
    blob         = ctx.blob
    root         = ctx.root
    source       = ctx.source
    widget_types = ctx.widget_types
    widgets_yaml = ctx.widgets_yaml
    version      = ctx.version
    ns           = ctx.namespace

    n = math.ceil(len(blob) / CHUNK_SIZE)
    lines = [
        _HEADER_COMMENT.format(source=source, root_hex=root.hex(), version=version),
        "#pragma once",
        "#include <stdint.h>",
        "#include <stddef.h>",
    ]

    if widget_types is not None:
        lines.append('#include "udisplay.h"')

    lines += [
        "",
        "/* -- Widget IDs ------------------------------------------------------------ */",
    ]
    for path, wid in sorted(widget_ids.items(), key=lambda kv: kv[1]):
        macro = _ns_macro(ns, f"WIDGET_ID_{_macro_name(path)}")
        lines.append(f"#define {macro:<52} 0x{wid:02X}u")

    if widgets_yaml is not None:
        dropdown_items = collect_dropdown_items(widgets_yaml)
        if dropdown_items:
            lines += [
                "",
                "/* -- Dropdown item index constants ----------------------------------------- */",
            ]
            for dd_path in sorted(dropdown_items.keys()):
                for idx, (item_key, _label) in enumerate(dropdown_items[dd_path]):
                    macro = _ns_macro(ns, f"{_macro_name(dd_path)}_{_macro_name(item_key)}")
                    lines.append(f"#define {macro:<52} {idx}u")

    handlers_type = _ns_fn(ns, "udisplay_ui_handlers_t")
    init_fn       = _ns_fn(ns, "udisplay_ui_init")
    set_handlers_fn = _ns_fn(ns, "udisplay_ui_set_handlers")

    if widget_types is not None:
        setters = []
        for path, _wid in sorted(widget_ids.items(), key=lambda kv: kv[1]):
            type_str = widget_types.get(path)
            if not type_str:
                continue
            info = _setter_for_type(type_str)
            if info is None:
                continue
            arg_decl, send_fn = info
            fn = f"{ns}_set_{_fn_suffix(path)}" if ns else f"set_{_fn_suffix(path)}"
            macro = _ns_macro(ns, f"WIDGET_ID_{_macro_name(path)}")
            if type_str in ("text-rw", "text-ro"):
                body = f"{send_fn}(ctx, {macro}, s, n);"
            else:
                body = f"{send_fn}(ctx, {macro}, v);"
            setters.append(
                f"static inline void {fn}(udisplay_t* ctx, {arg_decl}) {{ {body} }}"
            )

        if setters:
            lines += [
                "",
                "/* -- Per-widget typed setters (static inline) ------------------------------ */",
            ]
            lines.extend(setters)

        handlers = []
        for path, _wid in sorted(widget_ids.items(), key=lambda kv: kv[1]):
            type_str = widget_types.get(path)
            if not type_str:
                continue
            info = _handler_for_type(type_str)
            if info is None:
                continue
            for event_suffix, handler_args, _ in info:
                fn = f"on_{_fn_suffix(path)}_{event_suffix}"
                handlers.append(f"    void (*{fn})({handler_args});")

        lines += [
            "",
            "/* -- Per-widget event handler struct --------------------------------------- */",
            "typedef struct {",
            "    void (*on_client_ready)(void);  /**< called once when client is fully bootstrapped */",
            "    void (*on_comms_error)(void);   /**< called when heartbeat watchdog fires */",
        ]
        if handlers:
            lines.extend(handlers)
        lines += [
            f"}} {handlers_type};",
            "",
            "/* -- Generated API -----------------------------------------------------------",
            " * One bind/init helper per namespace, plus handler registration. Everything",
            " * else (feed, heartbeat, ble_set_mtu, on_connect/on_disconnect, ...) is the",
            " * handle-taking core API declared in udisplay.h -- call it directly with the",
            " * same ctx passed here. There is deliberately no per-instance wrapper for the",
            " * full API surface (see design doc: generated wrappers duplicating the core",
            " * API were cut as redundant architecture).",
            " * ---------------------------------------------------------------------------- */",
            "#ifdef __cplusplus",
            'extern "C" {',
            "#endif",
            f"void {init_fn}(udisplay_t* ctx, udisplay_send_fn send, udisplay_transport_t transport);",
            f"void {set_handlers_fn}(const {handlers_type}* h);",
            "#ifdef __cplusplus",
            "}",
            "#endif",
        ]

    lines += [
        "",
        "/* -- Property IDs ---------------------------------------------------------- */",
        "#define UDISPLAY_PROP_ENABLED   0x01u",
        "#define UDISPLAY_PROP_VISIBLE   0x02u",
        "#define UDISPLAY_PROP_MODE      0x03u",
        "#define UDISPLAY_PROP_STYLE     0x04u",
        "",
        "/* -- Blob metadata --------------------------------------------------------- */",
        f"#define UDISPLAY_CHUNK_COUNT   {n}u",
        f"#define UDISPLAY_CHUNK_SIZE    {CHUNK_SIZE}u",
        f"#define UDISPLAY_BLOB_LEN      {len(blob)}u",
        "",
        "/* -- Merkle root (32 bytes) ------------------------------------------------- */",
        f"extern const uint8_t {_ns_macro(ns, 'UDISPLAY_MERKLE_ROOT')}[32];",
        "",
        "/* -- Blob chunks (raw compressed bytes; last chunk may be < CHUNK_SIZE) ----- */",
    ]

    for i in range(n):
        chunk_len = len(blob[i * CHUNK_SIZE : (i + 1) * CHUNK_SIZE])
        lines.append(f"extern const uint8_t {_ns_macro(ns, f'UDISPLAY_CHUNK_{i}')}[{chunk_len}];")

    lines += [
        "",
        f"extern const uint8_t* const {_ns_macro(ns, 'UDISPLAY_CHUNKS')}[{n}];",
        f"extern const uint16_t       {_ns_macro(ns, 'UDISPLAY_CHUNK_LENS')}[{n}];",
        "",
        "/* -- Per-chunk SHA-256 hashes (for CHUNK_HEADER_RESPONSE) ------------------ */",
        f"extern const uint8_t* const {_ns_macro(ns, 'UDISPLAY_CHUNK_HASHES')}[{n}];",
    ]

    return "\n".join(lines) + "\n"


def _generate_source(ctx: BuildContext) -> str:
    _ns_validate(ctx.namespace)
    widget_ids   = ctx.widget_ids
    blob         = ctx.blob
    root         = ctx.root
    hashes       = ctx.hashes
    source       = ctx.source
    widget_types = ctx.widget_types
    version      = ctx.version
    header_name  = "udisplay_ui.h"
    ns           = ctx.namespace

    n = math.ceil(len(blob) / CHUNK_SIZE)

    merkle_root_sym  = _ns_macro(ns, "UDISPLAY_MERKLE_ROOT")
    chunks_sym       = _ns_macro(ns, "UDISPLAY_CHUNKS")
    chunk_lens_sym   = _ns_macro(ns, "UDISPLAY_CHUNK_LENS")
    chunk_hashes_sym = _ns_macro(ns, "UDISPLAY_CHUNK_HASHES")

    lines = [
        _HEADER_COMMENT.format(source=source, root_hex=root.hex(), version=version),
        f'#include "{header_name}"',
    ]
    lines += [
        "",
        f"const uint8_t {merkle_root_sym}[32] = {{",
        _hex_rows(root),
        "};",
        "",
    ]

    for i in range(n):
        chunk = blob[i * CHUNK_SIZE : (i + 1) * CHUNK_SIZE]
        chunk_sym = _ns_macro(ns, f"UDISPLAY_CHUNK_{i}")
        lines += [
            f"const uint8_t {chunk_sym}[{len(chunk)}] = {{",
            _hex_rows(chunk),
            "};",
            "",
        ]

    chunk_ptrs = ", ".join(_ns_macro(ns, f"UDISPLAY_CHUNK_{i}") for i in range(n))
    chunk_lens = ", ".join(
        str(len(blob[i * CHUNK_SIZE : (i + 1) * CHUNK_SIZE])) for i in range(n)
    )

    lines += [
        f"const uint8_t* const {chunks_sym}[{n}]    = {{ {chunk_ptrs} }};",
        f"const uint16_t       {chunk_lens_sym}[{n}] = {{ {chunk_lens} }};",
        "",
    ]

    for i, h in enumerate(hashes):
        lines += [
            f"static const uint8_t UDISPLAY_CHUNK_HASH_{i}[32] = {{",
            _hex_rows(h),
            "};",
            "",
        ]

    hash_ptrs = ", ".join(f"UDISPLAY_CHUNK_HASH_{i}" for i in range(n))
    lines.append(f"const uint8_t* const {chunk_hashes_sym}[{n}] = {{ {hash_ptrs} }};")

    if widget_types is not None:
        handlers_type   = _ns_fn(ns, "udisplay_ui_handlers_t")
        init_fn         = _ns_fn(ns, "udisplay_ui_init")
        set_handlers_fn = _ns_fn(ns, "udisplay_ui_set_handlers")
        # Internal statics: file-scope only, no linker-visible collision risk
        # across namespaces even when unprefixed -- but prefixed anyway so
        # grepping/debugging a linked multi-namespace binary stays unambiguous.
        handlers_var  = _ns_fn(ns, "_ui_handlers") if ns else "_ui_handlers"
        on_ready_fn   = _ns_fn(ns, "_udisplay_ui_on_ready") if ns else "_udisplay_ui_on_ready"
        on_error_fn   = _ns_fn(ns, "_udisplay_ui_on_comms_error") if ns else "_udisplay_ui_on_comms_error"
        dispatch_fn   = _ns_fn(ns, "_udisplay_ui_dispatch") if ns else "_udisplay_ui_dispatch"

        handler_paths = []
        for path, wid in sorted(widget_ids.items(), key=lambda kv: kv[1]):
            type_str = widget_types.get(path)
            if not type_str:
                continue
            info = _handler_for_type(type_str)
            if info is not None:
                handler_paths.append((path, wid, type_str, info))

        lines += [
            "",
            "/* -- Generated API implementation ------------------------------------------ */",
            "",
            f"static const {handlers_type}* {handlers_var} = NULL;",
            "",
            f"static void {on_ready_fn}(void* ud)",
            "{",
            "    (void)ud;",
            f"    if ({handlers_var} && {handlers_var}->on_client_ready) {handlers_var}->on_client_ready();",
            "}",
            "",
            f"static void {on_error_fn}(void* ud)",
            "{",
            "    (void)ud;",
            f"    if ({handlers_var} && {handlers_var}->on_comms_error) {handlers_var}->on_comms_error();",
            "}",
            "",
            f"static void {dispatch_fn}(const udisplay_event_t* ev, void* ud)",
            "{",
            "    (void)ud;",
            f"    if (!{handlers_var}) return;",
        ]

        # event_suffix → C event-type constant (for multi-handler dispatch)
        _EVT_CONST = {
            "press":   "UDISPLAY_EVENT_BUTTON_PRESS",
            "release": "UDISPLAY_EVENT_BUTTON_RELEASE",
            "click":   "UDISPLAY_EVENT_BUTTON_CLICK",
        }

        if handler_paths:
            lines.append("    switch (ev->widget_id) {")
            for path, _wid, _type_str, handlers_list in handler_paths:
                suffix = _fn_suffix(path)
                macro = _ns_macro(ns, f"WIDGET_ID_{_macro_name(path)}")
                lines.append(f"        case {macro}:")
                if len(handlers_list) == 1:
                    event_suffix, _handler_args, dispatch_args = handlers_list[0]
                    handler_field = f"on_{suffix}_{event_suffix}"
                    if dispatch_args:
                        call = f"{handlers_var}->{handler_field}({dispatch_args});"
                    else:
                        call = f"{handlers_var}->{handler_field}();"
                    lines.append(f"            if ({handlers_var}->{handler_field}) {call}")
                else:
                    lines.append("            switch (ev->event_type) {")
                    for event_suffix, _handler_args, dispatch_args in handlers_list:
                        evt_const = _EVT_CONST.get(event_suffix, event_suffix)
                        handler_field = f"on_{suffix}_{event_suffix}"
                        if dispatch_args:
                            call = f"{handlers_var}->{handler_field}({dispatch_args});"
                        else:
                            call = f"{handlers_var}->{handler_field}();"
                        lines += [
                            f"                case {evt_const}:",
                            f"                    if ({handlers_var}->{handler_field}) {call}",
                            "                    break;",
                        ]
                    lines.append("                default: break;")
                    lines.append("            }")
                lines.append("            break;")
            lines += [
                "        default: break;",
                "    }",
            ]

        lines += [
            "}",
            "",
            f"void {init_fn}(udisplay_t* ctx, udisplay_send_fn send, udisplay_transport_t transport)",
            "{",
        ]
        cfg_lines = _config_designated_initializer(
            "udisplay_config_t cfg",
            _config_fields(
                merkle_root_expr=merkle_root_sym,
                chunks_expr=chunks_sym,
                chunk_hashes_expr=chunk_hashes_sym,
                chunk_lens_expr=chunk_lens_sym,
                chunk_count_expr="UDISPLAY_CHUNK_COUNT",
                send_expr="send",
                on_event_expr=dispatch_fn,
                on_ready_expr=on_ready_fn,
                on_error_expr=on_error_fn,
                userdata_expr="NULL",
                transport_expr="transport",
            ),
            indent=8,
        )
        lines += ["    " + l for l in cfg_lines]
        lines += [
            "    udisplay_init(ctx, &cfg);",
            "}",
            "",
            f"void {set_handlers_fn}(const {handlers_type}* h)",
            "{",
            f"    {handlers_var} = h;",
            "}",
        ]

    return "\n".join(lines) + "\n"
