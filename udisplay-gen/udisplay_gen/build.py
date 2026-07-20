# SPDX-License-Identifier: MPL-2.0
# Copyright (c) 2026 Attila Agas

"""Backwards-compatibility shim. Import from .backends.* for new code."""
from __future__ import annotations

from typing import List, Optional

from .backends import BuildContext, OutputFile  # noqa: F401
from .backends._shared import (  # noqa: F401
    MAX_CHUNK_COUNT,
    validate_blob_size,
    _macro_name,
    _fn_suffix,
    _setter_for_type,
    _handler_for_type,
    _hex_rows,
)
from .backends.cpp_backend import _cpp_class_name  # noqa: F401
from .backends import c_backend as _c, cpp_backend as _cpp


def generate_header(
    *,
    widget_ids: dict,
    blob: bytes,
    root: bytes,
    source: str,
    widget_types: Optional[dict] = None,
    widgets_yaml: Optional[dict] = None,
    version: str = "0.1.0",
) -> str:
    ctx = BuildContext(
        widget_ids=widget_ids,
        blob=blob,
        root=root,
        hashes=[],
        source=source,
        widget_types=widget_types,
        widgets_yaml=widgets_yaml,
        version=version,
    )
    return _c._generate_header(ctx)


def generate_source(
    *,
    widget_ids: dict,
    blob: bytes,
    root: bytes,
    hashes: List[bytes],
    source: str,
    widget_types: Optional[dict] = None,
    widgets_yaml: Optional[dict] = None,
    header_name: str = "udisplay_ui.h",
    version: str = "0.1.0",
) -> str:
    ctx = BuildContext(
        widget_ids=widget_ids,
        blob=blob,
        root=root,
        hashes=hashes,
        source=source,
        widget_types=widget_types,
        widgets_yaml=widgets_yaml,
        version=version,
    )
    return _c._generate_source(ctx)


def generate_header_cpp(
    *,
    widget_ids: dict,
    blob: bytes,
    root: bytes,
    hashes: List[bytes],
    source: str,
    widget_types: dict,
    widgets_yaml: dict,
    variant: str = "safe",
    version: str = "0.1.0",
) -> str:
    ctx = BuildContext(
        widget_ids=widget_ids,
        blob=blob,
        root=root,
        hashes=hashes,
        source=source,
        widget_types=widget_types,
        widgets_yaml=widgets_yaml,
        variant=variant,
        version=version,
    )
    return _cpp._generate_header_cpp(ctx)
