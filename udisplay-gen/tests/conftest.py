"""Shared fixtures for udisplay-gen tests."""
import json
import pathlib

import pytest

# Repo root is two levels above the udisplay-gen/ package
REPO_ROOT = pathlib.Path(__file__).parent.parent.parent
VECTORS_FILE = REPO_ROOT / "tests" / "protocol_vectors.json"


@pytest.fixture(scope="session")
def vectors() -> dict:
    with VECTORS_FILE.open() as f:
        return json.load(f)


@pytest.fixture
def minimal_yaml(tmp_path) -> pathlib.Path:
    p = tmp_path / "udisplay.yaml"
    p.write_text(
        "device:\n  name: test\nwidgets:\n  a:\n    type: toggle\n    label: A\n"
    )
    return p


@pytest.fixture
def text_readonly_yaml(tmp_path) -> pathlib.Path:
    """A YAML with a single text widget in readonly mode — setter yes, handler no."""
    p = tmp_path / "text_ro.yaml"
    p.write_text(
        "device:\n  name: test\nwidgets:\n"
        "  status_msg:\n    type: text\n    label: Status\n    mode: ro\n"
    )
    return p


@pytest.fixture
def dropdown_yaml(tmp_path) -> pathlib.Path:
    """A YAML with a dropdown widget."""
    p = tmp_path / "dropdown.yaml"
    p.write_text(
        "device:\n"
        "  name: test\n"
        "widgets:\n"
        "  wifi_mode:\n"
        "    type: dropdown\n"
        "    label: Wi-Fi Mode\n"
        "    items:\n"
        "      sta: Station\n"
        "      ap: Access Point\n"
        "      apsta: AP + Station\n"
        "      disabled: Disabled\n"
    )
    return p


@pytest.fixture
def decorations_yaml(tmp_path) -> pathlib.Path:
    """A YAML with label and separator decoration types."""
    p = tmp_path / "decorations.yaml"
    p.write_text(
        "device:\n"
        "  name: test\n"
        "widgets:\n"
        "  net_heading:\n"
        "    type: label\n"
        "    text: Network Settings\n"
        "    style: heading\n"
        "  div1:\n"
        "    type: separator\n"
        "  ssid_field:\n"
        "    type: text\n"
        "    label: SSID\n"
        "    mode: rw\n"
    )
    return p


@pytest.fixture
def layout_yaml(tmp_path) -> pathlib.Path:
    """A YAML with section/row/grid containers."""
    p = tmp_path / "layout.yaml"
    p.write_text(
        "device:\n"
        "  name: test\n"
        "widgets:\n"
        "  sensors:\n"
        "    type: section\n"
        "    label: Sensors\n"
        "    collapsible: true\n"
        "    widgets:\n"
        "      volt:\n"
        "        type: display\n"
        "        label: Voltage\n"
        "      temp:\n"
        "        type: display\n"
        "        label: Temp\n"
        "  controls:\n"
        "    type: row\n"
        "    widgets:\n"
        "      relay:\n"
        "        type: toggle\n"
        "        label: Relay\n"
        "        flex: 2\n"
        "      reset_btn:\n"
        "        type: button\n"
        "        label: Reset\n"
    )
    return p


@pytest.fixture
def full_vocab_yaml(tmp_path) -> pathlib.Path:
    """All 7 widget types, non-alphabetical YAML order."""
    p = tmp_path / "full.yaml"
    p.write_text(
        "device:\n"
        "  name: full vocab test\n"
        "widgets:\n"
        "  slider_rate:\n"
        "    type: slider\n"
        "    label: Rate\n"
        "    min: 1\n"
        "    max: 100\n"
        "    step: 1\n"
        "    unit: Hz\n"
        "  toggle_relay:\n"
        "    type: toggle\n"
        "    label: Relay\n"
        "  fire_btn:\n"
        "    type: button\n"
        "    label: Fire\n"
        "    shape: circle\n"
        "    widgets:\n"
        "      status_led:\n"
        "        type: led\n"
        "        label: Active\n"
        "  display_volt:\n"
        "    type: display\n"
        "    label: Voltage\n"
        "    unit: V\n"
        "    format: '%.3f'\n"
        "    style: large\n"
        "  mode_sel:\n"
        "    type: button-group\n"
        "    layout: grid\n"
        "    items:\n"
        "      dc:\n"
        "        label: DCV\n"
        "      ac:\n"
        "        label: ACV\n"
        "  power_led:\n"
        "    type: led\n"
        "    label: Power\n"
        "    color: '#ff0000'\n"
        "  status_rgb:\n"
        "    type: rgbled\n"
        "    label: Status\n"
        "  ssid_field:\n"
        "    type: text\n"
        "    label: SSID\n"
        "    mode: rw\n"
    )
    return p
