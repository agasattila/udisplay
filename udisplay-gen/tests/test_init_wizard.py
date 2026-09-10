"""Tests for the `udisplay-gen init` interactive wizard."""
import yaml
from click.testing import CliRunner

from udisplay_gen.cli import cli
from udisplay_gen.validate import load_schema, validate

SCHEMA = load_schema()


def test_wizard_dpad_and_button_group_produce_valid_yaml(tmp_path):
    """Regression test: the dpad-split removed layout:dpad from button-group
    and added `dpad` as its own top-level type — the wizard must offer a
    working flow for both (it previously KeyError'd on `dpad`, added to
    SUPPORTED_TYPES for the split without wizard support)."""
    out_path = tmp_path / "udisplay.yaml"
    wizard_input = "\n".join([
        "Test Device",   # device name
        "",              # version (skip)
        "dir_pad",       # widget name
        "dpad",          # type
        "up_btn",        # button name
        "Up",            # label
        "top",           # position
        "down_btn",      # button name
        "Down",          # label
        "bottom",        # position
        "",              # finish buttons (2 >= 1)
        "mode_sel",       # widget name
        "button-group",  # type
        "fast",          # item name
        "Fast",          # label
        "slow",          # item name
        "Slow",          # label
        "",              # finish items (2 >= 2)
        "",              # finish widgets
        "",
    ]) + "\n"

    runner = CliRunner()
    result = runner.invoke(cli, ["init", "-o", str(out_path)], input=wizard_input)

    assert result.exit_code == 0, result.output
    assert out_path.exists()

    doc = yaml.safe_load(out_path.read_text())

    dpad = doc["widgets"]["dir_pad"]
    assert dpad["type"] == "dpad"
    assert dpad["widgets"]["up_btn"] == {"type": "button", "label": "Up", "position": "top"}
    assert dpad["widgets"]["down_btn"] == {"type": "button", "label": "Down", "position": "bottom"}

    bg = doc["widgets"]["mode_sel"]
    assert bg["type"] == "button-group"
    assert "layout" not in bg  # wizard no longer offers dpad layout — omits the field, schema defaults to grid
    assert bg["items"]["fast"] == {"label": "Fast"}
    assert bg["items"]["slow"] == {"label": "Slow"}

    assert validate(doc, SCHEMA) == []
