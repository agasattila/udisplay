# SPDX-License-Identifier: MPL-2.0
# Copyright (c) 2026 Attila Agas

"""Allow ``python -m udisplay_gen`` to run the CLI straight from a source tree,
without a pip install putting ``udisplay-gen`` on PATH (the top-level CMake
build drives the demos' code generation this way)."""

from .cli import cli

cli(prog_name="udisplay-gen")
