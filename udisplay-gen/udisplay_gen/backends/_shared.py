# SPDX-License-Identifier: MPL-2.0
# Copyright (c) 2026 Attila Agas

"""Shared utilities used by all codegen backends."""
from __future__ import annotations

import math
import re
from typing import Optional

from ..merkle import CHUNK_SIZE

# Practical upper limit on blob chunk count.
MAX_CHUNK_COUNT = 64


def validate_blob_size(blob: bytes) -> None:
    """Raise ValueError if the blob requires more chunks than the protocol allows."""
    n = math.ceil(len(blob) / CHUNK_SIZE)
    if n > MAX_CHUNK_COUNT:
        raise ValueError(
            f"Blob requires {n} chunks but protocol maximum is {MAX_CHUNK_COUNT}. "
            f"Reduce the YAML to produce a smaller compressed blob."
        )


def _macro_name(key_path: str) -> str:
    """
    'fire_btn.status_led' -> 'FIRE_BTN_STATUS_LED'
    Dots and any non-alphanumeric characters become underscores.
    """
    return re.sub(r"[^a-zA-Z0-9]+", "_", key_path).upper()


def _fn_suffix(key_path: str) -> str:
    """
    'fire_btn.status_led' -> 'fire_btn_status_led'
    Dots and any non-alphanumeric characters become underscores.
    """
    return re.sub(r"[^a-zA-Z0-9]+", "_", key_path).lower()


# Maps type_str -> (setter_arg_decl, send_fn_name)
_SETTER_TYPES: dict[str, tuple[str, str]] = {
    "display":           ("float v",                  "udisplay_send_float"),
    "slider":            ("float v",                  "udisplay_send_float"),
    "led":               ("uint8_t v",                "udisplay_send_bool"),
    "rgbled":            ("int32_t rgb",              "udisplay_send_int"),
    "toggle":            ("uint8_t v",                "udisplay_send_bool"),
    "text-rw":           ("const char* s, uint8_t n", "udisplay_send_string"),
    "text-ro":           ("const char* s, uint8_t n", "udisplay_send_string"),
    "dropdown":          ("uint8_t index",             "udisplay_send_uint8"),
}

# Maps type_str -> list of (event_name_suffix, handler_arg_decl, dispatch_arg_expr)
# Button types emit three events; all others emit one.
_HANDLER_TYPES: dict[str, list[tuple[str, str, str]]] = {
    "button":            [
                             ("press",   "void", ""),
                             ("release", "void", ""),
                             ("click",   "void", ""),
                         ],
    "button-group-item": [
                             ("press",   "void", ""),
                             ("release", "void", ""),
                             ("click",   "void", ""),
                         ],
    "slider":            [("change", "float value",                  "ev->slider_value")],
    "toggle":            [("change", "uint8_t state",                "ev->toggle_state")],
    "text-rw":           [("submit", "const char* str, uint8_t len", "ev->text.str, ev->text.len")],
    "dropdown":          [("change", "uint8_t index",                "ev->selection_index")],
}


def _setter_for_type(type_str: str) -> Optional[tuple[str, str]]:
    """Return (arg_decl, send_fn) or None if this widget type has no setter."""
    return _SETTER_TYPES.get(type_str)


def _handler_for_type(type_str: str) -> Optional[list[tuple[str, str, str]]]:
    """Return list of (event_suffix, handler_args, dispatch_args) or None if no handler."""
    return _HANDLER_TYPES.get(type_str)


def _config_fields(
    *,
    merkle_root_expr: str,
    chunks_expr: str,
    chunk_hashes_expr: str,
    chunk_lens_expr: str,
    chunk_count_expr: str,
    send_expr: str,
    on_event_expr: str,
    on_ready_expr: str,
    on_error_expr: str,
    userdata_expr: str,
    transport_expr: str,
) -> list[tuple[str, str]]:
    """
    Canonical udisplay_config_t field -> value-expression list, shared by both
    codegen backends (c_backend.py, cpp_backend.py). Every field of
    udisplay_config_t (libudisplay/include/udisplay.h) MUST appear here --
    this is the single place to update when the struct grows a field, instead
    of the two backends drifting independently (the bug this list fixes:
    2026-07-07 plan-eng-review found transport/ble_mtu_payload/auth_* left
    unset -- and undefined -- in both backends' generated init).

    auth_algo/auth_check/fill_random are intentionally always UDISPLAY_AUTH_NONE
    /NULL/NULL: codegen has no story for auth yet (see TODOS.md TODO-033).
    ble_mtu_payload is intentionally always 0: udisplay_init() falls back to
    UDISPLAY_BLE_MTU_PAYLOAD_DEFAULT for any value < 7, so 0 is a correct
    default rather than an omission, and firmware updates it at runtime via
    udisplay_ble_set_mtu() after MTU negotiation.
    """
    return [
        ("merkle_root",     merkle_root_expr),
        ("chunks",          chunks_expr),
        ("chunk_hashes",    chunk_hashes_expr),
        ("chunk_lens",      chunk_lens_expr),
        ("chunk_count",     chunk_count_expr),
        ("send",            send_expr),
        ("on_event",        on_event_expr),
        ("on_client_ready", on_ready_expr),
        ("on_comms_error",  on_error_expr),
        ("userdata",        userdata_expr),
        ("auth_algo",       "UDISPLAY_AUTH_NONE"),
        ("auth_check",      "NULL"),
        ("fill_random",     "NULL"),
        ("transport",       transport_expr),
        ("ble_mtu_payload", "0"),
    ]


def _config_designated_initializer(
    var_decl: str, fields: list[tuple[str, str]], indent: int = 4
) -> list[str]:
    """
    Render fields as a C99 designated-initializer literal:
        {var_decl} = { .field = expr, ... };
    Every field is listed explicitly (never omitted) so that compiling the
    generated file with -Wmissing-field-initializers -Werror catches a future
    udisplay_config_t field that _config_fields() wasn't updated for.
    """
    pad = " " * indent
    lines = [f"{var_decl} = {{"]
    lines += [f"{pad}.{name} = {value}," for name, value in fields]
    lines.append("};")
    return lines


def _config_sequential_assignment(var_name: str, fields: list[tuple[str, str]]) -> list[str]:
    """
    Render fields as sequential assignment statements: `var.field = expr;`.
    Used by cpp_backend.py, which targets C++11 (demo02/demo03 pin
    CMAKE_CXX_STANDARD 11) -- C99 designated initializers aren't standard
    C++ until C++20, so this backend can't use _config_designated_initializer.
    """
    return [f"{var_name}.{name} = {value};" for name, value in fields]


def _hex_rows(data: bytes, per_line: int = 16, indent: int = 4) -> str:
    """Format bytes as hex rows for embedding in C arrays."""
    rows = []
    for start in range(0, len(data), per_line):
        row = data[start : start + per_line]
        rows.append(" " * indent + ", ".join(f"0x{b:02X}" for b in row))
    return ",\n".join(rows)


_HEADER_COMMENT = """\
/* SPDX-License-Identifier: MIT */
/* Copyright (c) 2026 Attila Agas */
/* Generated by udisplay-gen {version}. DO NOT EDIT.
 * Source:      {source}
 * Merkle root: {root_hex}
 */
"""
