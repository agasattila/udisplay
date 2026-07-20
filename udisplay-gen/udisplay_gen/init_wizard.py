# SPDX-License-Identifier: MPL-2.0
# Copyright (c) 2026 Attila Agas

"""
Interactive wizard for `udisplay-gen init`.
Guides the user through creating a valid udisplay.yaml.
"""
from __future__ import annotations

import pathlib
import sys

import click
import yaml

from .validate import SUPPORTED_TYPES

_WIDGET_PROMPTS: dict[str, list[tuple[str, str, object]]] = {
    # (prompt_text, yaml_key, default_or_None)
    "display": [
        ("Label (shown above the value)", "label", ""),
        ("Unit (e.g. V, °C, Hz — blank to skip)", "unit", ""),
        ("Format string (e.g. %.2f — blank for default)", "format", ""),
        ("Style [default/large]", "style", "default"),
    ],
    "led": [
        ("Label (shown beside the dot)", "label", ""),
    ],
    "button": [
        ("Label (shown on button face)", "label", ""),
        ("Shape [rect/circle/square]", "shape", "rect"),
        ("Color as #rrggbb (blank for theme default)", "color", ""),
    ],
    "button-group": [],  # handled specially
    "slider": [
        ("Label", "label", ""),
        ("Min value", "min", None),
        ("Max value", "max", None),
        ("Step (blank for 1)", "step", "1"),
        ("Unit (blank to skip)", "unit", ""),
    ],
    "toggle": [
        ("Label", "label", ""),
    ],
    "text": [
        ("Label", "label", ""),
        ("Mode [readonly/rw]", "mode", "readonly"),
        ("Placeholder (rw mode, blank to skip)", "placeholder", ""),
    ],
}


def _ask(prompt: str, default: object = None, choices: list[str] | None = None) -> str:
    kwargs: dict = {"default": default or ""}
    if choices:
        kwargs["type"] = click.Choice(choices)
    return click.prompt(f"  {prompt}", **kwargs).strip()


def _collect_widget(name: str) -> dict:
    wtype = click.prompt(
        f"  Type for '{name}'",
        type=click.Choice(SUPPORTED_TYPES),
    )
    widget: dict = {"type": wtype}

    if wtype == "button-group":
        layout = click.prompt("  Layout", type=click.Choice(["grid", "dpad"]), default="grid")
        widget["layout"] = layout
        items: dict = {}
        click.echo("  Add items (leave name blank to finish, minimum 2):")
        while True:
            item_name = click.prompt("    Item name (snake_case)", default="").strip()
            if not item_name:
                if len(items) < 2:
                    click.echo("    At least 2 items required.")
                    continue
                break
            item: dict = {"label": click.prompt(f"    Label for '{item_name}'").strip()}
            if layout == "dpad":
                item["position"] = click.prompt(
                    f"    Position for '{item_name}'",
                    type=click.Choice(["top", "right", "bottom", "left", "center"]),
                )
            items[item_name] = item
        widget["items"] = items
        return widget

    for prompt_text, key, default in _WIDGET_PROMPTS[wtype]:
        val = _ask(prompt_text, default)
        if val and val != str(default):
            if key in ("min", "max"):
                try:
                    widget[key] = float(val) if "." in val else int(val)
                except ValueError:
                    click.echo(f"    Invalid number: {val!r}. Using 0.")
                    widget[key] = 0
            elif key == "step":
                try:
                    widget[key] = float(val) if "." in val else int(val)
                except ValueError:
                    widget[key] = 1
            elif val:
                widget[key] = val

    if wtype == "button":
        if click.confirm("  Add an LED child to this button?", default=False):
            led_name = click.prompt("    LED child name (snake_case)", default="status").strip()
            led_label = click.prompt("    LED label (blank to skip)", default="").strip()
            led: dict = {"type": "led"}
            if led_label:
                led["label"] = led_label
            widget["widgets"] = {led_name: led}

    return widget


def run_wizard(output: str) -> None:
    out_path = pathlib.Path(output)
    if out_path.exists():
        if not click.confirm(f"{output} already exists. Overwrite?", default=False):
            click.echo("Aborted.")
            sys.exit(0)

    click.echo("=== uDisplay YAML wizard ===")
    click.echo("Press Enter to accept defaults.\n")

    device_name = click.prompt("Device name").strip()
    device_version = click.prompt("Version (blank to skip)", default="").strip()

    device: dict = {"name": device_name}
    if device_version:
        device["version"] = device_version

    widgets: dict = {}
    click.echo("\nAdd widgets (leave name blank to finish, minimum 1):")
    while True:
        widget_name = click.prompt("Widget name (snake_case)", default="").strip()
        if not widget_name:
            if not widgets:
                click.echo("At least one widget is required.")
                continue
            break
        if not widget_name.replace("_", "").isalnum() or not widget_name[0].isalpha():
            click.echo("  Name must be lowercase snake_case starting with a letter.")
            continue
        click.echo(f"Configure widget '{widget_name}':")
        widgets[widget_name] = _collect_widget(widget_name)
        click.echo()

    doc = {"device": device, "widgets": widgets}
    yaml_text = yaml.dump(doc, default_flow_style=False, sort_keys=False, allow_unicode=True)

    out_path.write_text(yaml_text)
    click.echo(f"\nWritten: {output}")
    click.echo(f"Run `udisplay-gen validate {output}` to verify.")
