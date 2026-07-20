# SPDX-License-Identifier: MPL-2.0
# Copyright (c) 2026 Attila Agas

"""Backend contract types for udisplay-gen codegen."""
from __future__ import annotations

from dataclasses import dataclass
from typing import Dict, List, Optional, Union


@dataclass
class BuildContext:
    """All inputs needed by any codegen backend."""
    widget_ids:   Dict[str, int]
    blob:         bytes
    root:         bytes
    hashes:       List[bytes]
    source:       str                            # YAML source filename (informational)
    widget_types: Optional[Dict[str, str]] = None  # None -> suppress generated API
    widgets_yaml: Optional[dict] = None
    variant:      str = "safe"                  # C++ only: "safe" | "modern"
    version:      str = "0.1.0"


@dataclass
class OutputFile:
    """A single file emitted by a backend."""
    name:    str
    content: Union[str, bytes]      # str -> write_text, bytes -> write_bytes
