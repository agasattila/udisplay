# SPDX-License-Identifier: MPL-2.0
# Copyright (c) 2026 Attila Agas

"""CLI entry point for udisplay-gen."""
from __future__ import annotations

import math
import pathlib
import sys

import click

from . import __version__
from .validate import load_schema, parse_yaml_text, validate
from .build import validate_blob_size
from .backends import BuildContext
from .backends import c_backend, cpp_backend
from .merkle import compute, CHUNK_SIZE
from .widget_ids import assign, collect_types
from .init_wizard import run_wizard

_BACKENDS = {"c": c_backend, "cpp": cpp_backend}


@click.group()
@click.version_option(__version__)
def cli() -> None:
    """udisplay-gen — uDisplay firmware UI codegen."""


@cli.command()
@click.argument("yaml_file", metavar="YAML_FILE", type=click.Path(exists=True, dir_okay=False))
def validate_cmd(yaml_file: str) -> None:
    """Validate a uDisplay YAML definition against the schema."""
    schema = load_schema()
    try:
        yaml_text = pathlib.Path(yaml_file).read_text(encoding="utf-8")
        doc = parse_yaml_text(yaml_text)
    except Exception as exc:
        click.echo(f"Error: could not parse YAML: {exc}", err=True)
        sys.exit(1)

    errors = validate(doc, schema, yaml_text=yaml_text)
    if errors:
        click.echo(f"Validation FAILED: {yaml_file}", err=True)
        for msg in errors:
            click.echo(msg, err=True)
        sys.exit(1)

    click.echo(f"OK: {yaml_file}")


# Register as 'validate' on the CLI
cli.add_command(validate_cmd, name="validate")


@cli.command()
@click.argument("yaml_file", metavar="YAML_FILE", type=click.Path(exists=True, dir_okay=False))
@click.option(
    "-o", "--output-dir",
    default=".",
    show_default=True,
    type=click.Path(file_okay=False),
    help="Output directory for generated files.",
)
@click.option(
    "--lang",
    default="c",
    show_default=True,
    type=click.Choice(["c", "cpp"]),
    help="Output language.",
)
@click.option(
    "--modern",
    is_flag=True,
    default=False,
    help="C++ only: use std::function for event handlers (requires C++11 with <functional>).",
)
def build(yaml_file: str, output_dir: str, lang: str, modern: bool) -> None:
    """
    Compile a uDisplay YAML definition to:
    \b
      --lang c  (default):
        udisplay_ui.h    — C header with widget ID macros and blob metadata
        udisplay_ui.c    — C source with blob chunks and Merkle root
        udisplay_ui.bin  — raw compressed YAML blob (flash to device ROM)

      --lang cpp:
        udisplay_ui.hpp  — header-only C++ class hierarchy
        udisplay_ui.bin  — raw compressed YAML blob (flash to device ROM)
    """
    if modern and lang != "cpp":
        click.echo("Error: --modern requires --lang cpp.", err=True)
        sys.exit(1)

    schema = load_schema()

    yaml_bytes = pathlib.Path(yaml_file).read_bytes()
    yaml_text = yaml_bytes.decode("utf-8")

    try:
        doc = parse_yaml_text(yaml_text)
    except Exception as exc:
        click.echo(f"Error: could not parse YAML: {exc}", err=True)
        sys.exit(1)

    errors = validate(doc, schema, yaml_text=yaml_text)
    if errors:
        click.echo(
            f"Error: YAML validation failed. Run `udisplay-gen validate {yaml_file}` for details.",
            err=True,
        )
        for msg in errors:
            click.echo(msg, err=True)
        sys.exit(1)

    try:
        blob, root, hashes = compute(yaml_bytes)
        validate_blob_size(blob)
    except ValueError as exc:
        click.echo(f"Error: {exc}", err=True)
        sys.exit(1)

    try:
        widget_ids = assign(doc["widgets"])
    except ValueError as exc:
        click.echo(f"Error: {exc}", err=True)
        sys.exit(1)

    widget_type_map = collect_types(doc["widgets"])

    out = pathlib.Path(output_dir)
    out.mkdir(parents=True, exist_ok=True)

    ctx = BuildContext(
        widget_ids=widget_ids,
        blob=blob,
        root=root,
        hashes=hashes,
        source=pathlib.Path(yaml_file).name,
        widget_types=widget_type_map,
        widgets_yaml=doc["widgets"],
        variant="modern" if modern else "safe",
    )

    n = math.ceil(len(ctx.blob) / CHUNK_SIZE)
    click.echo(
        f"Built: {len(ctx.blob):,} bytes compressed, {n} chunk(s), "
        f"root {ctx.root.hex()[:16]}..."
    )

    for f in _BACKENDS[lang].generate(ctx):
        path = out / f.name
        if isinstance(f.content, bytes):
            path.write_bytes(f.content)
        else:
            path.write_text(f.content)
        click.echo(f"  -> {path}")


@cli.command()
@click.option(
    "-o", "--output",
    default="udisplay.yaml",
    show_default=True,
    type=click.Path(dir_okay=False),
    help="Output YAML file path.",
)
def init(output: str) -> None:
    """Interactive wizard: create a new uDisplay YAML definition."""
    run_wizard(output)
