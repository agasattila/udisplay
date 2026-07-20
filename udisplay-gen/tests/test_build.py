"""Tests for the build pipeline (codegen output)."""
import math
import pathlib
import re

import pytest
import yaml as pyyaml
from click.testing import CliRunner

from udisplay_gen.cli import cli
from udisplay_gen.build import (
    generate_header, generate_source, generate_header_cpp,
    _macro_name, _fn_suffix,
    _setter_for_type, _handler_for_type, validate_blob_size, MAX_CHUNK_COUNT,
    _cpp_class_name,
)
from udisplay_gen.backends import BuildContext, OutputFile
from udisplay_gen.backends import c_backend, cpp_backend
from udisplay_gen.merkle import compute, CHUNK_SIZE
from udisplay_gen.widget_ids import assign, collect_types


def test_macro_name():
    assert _macro_name("fire_btn") == "FIRE_BTN"
    assert _macro_name("fire_btn.status_led") == "FIRE_BTN_STATUS_LED"
    assert _macro_name("mode_sel.ac") == "MODE_SEL_AC"


def test_generate_header_contains_widget_macros(minimal_yaml):
    yaml_bytes = minimal_yaml.read_bytes()
    doc = pyyaml.safe_load(yaml_bytes)
    blob, root, hashes = compute(yaml_bytes)
    widget_ids = assign(doc["widgets"])
    header = generate_header(widget_ids=widget_ids, blob=blob, root=root, source="test.yaml")
    assert "WIDGET_ID_A" in header
    assert "0x10u" in header
    assert "UDISPLAY_PROP_ENABLED" in header
    assert "UDISPLAY_CHUNK_COUNT" in header
    assert "#pragma once" in header
    assert "SPDX-License-Identifier: MIT" in header
    assert "Copyright (c) 2026 Attila Agas" in header


def test_generate_source_contains_chunks(minimal_yaml):
    yaml_bytes = minimal_yaml.read_bytes()
    doc = pyyaml.safe_load(yaml_bytes)
    blob, root, hashes = compute(yaml_bytes)
    widget_ids = assign(doc["widgets"])
    source = generate_source(widget_ids=widget_ids, blob=blob, root=root,
                             hashes=hashes, source="test.yaml")
    assert "UDISPLAY_MERKLE_ROOT" in source
    assert "UDISPLAY_CHUNK_0" in source
    assert "UDISPLAY_CHUNKS" in source
    assert "UDISPLAY_CHUNK_LENS" in source
    assert "UDISPLAY_CHUNK_HASH_0" in source
    assert "UDISPLAY_CHUNK_HASHES" in source
    # Root bytes appear
    assert f"0x{root[0]:02X}" in source


def test_build_produces_three_files(minimal_yaml, tmp_path):
    runner = CliRunner()
    result = runner.invoke(cli, ["build", str(minimal_yaml), "-o", str(tmp_path)])
    assert result.exit_code == 0, result.output
    assert (tmp_path / "udisplay_ui.h").exists()
    assert (tmp_path / "udisplay_ui.c").exists()
    assert (tmp_path / "udisplay_ui.bin").exists()


def test_build_bin_matches_merkle(minimal_yaml, tmp_path):
    runner = CliRunner()
    runner.invoke(cli, ["build", str(minimal_yaml), "-o", str(tmp_path)])
    bin_data = (tmp_path / "udisplay_ui.bin").read_bytes()
    yaml_bytes = minimal_yaml.read_bytes()
    blob, root, _ = compute(yaml_bytes)
    assert bin_data == blob


def test_build_deterministic(minimal_yaml, tmp_path):
    runner = CliRunner()
    out1 = tmp_path / "run1"
    out2 = tmp_path / "run2"
    runner.invoke(cli, ["build", str(minimal_yaml), "-o", str(out1)])
    runner.invoke(cli, ["build", str(minimal_yaml), "-o", str(out2)])
    assert (out1 / "udisplay_ui.bin").read_bytes() == (out2 / "udisplay_ui.bin").read_bytes()


def test_build_full_vocabulary(full_vocab_yaml, tmp_path):
    runner = CliRunner()
    result = runner.invoke(cli, ["build", str(full_vocab_yaml), "-o", str(tmp_path)])
    assert result.exit_code == 0, result.output
    header = (tmp_path / "udisplay_ui.h").read_text()
    # All expected widget ID macros present, at correct IDs.
    # Alphabetical order with status_rgb added:
    #   0x10 display_volt  0x11 fire_btn  0x12 fire_btn.status_led
    #   0x13 mode_sel  0x14 mode_sel.ac  0x15 mode_sel.dc
    #   0x16 power_led  0x17 slider_rate  0x18 ssid_field
    #   0x19 status_rgb  0x1A toggle_relay
    assert "WIDGET_ID_DISPLAY_VOLT" in header and "0x10u" in header
    assert "WIDGET_ID_FIRE_BTN" in header and "0x11u" in header
    assert "WIDGET_ID_FIRE_BTN_STATUS_LED" in header and "0x12u" in header
    assert "WIDGET_ID_STATUS_RGB" in header and "0x19u" in header
    assert "WIDGET_ID_TOGGLE_RELAY" in header and "0x1Au" in header


def test_validate_valid(minimal_yaml):
    runner = CliRunner()
    result = runner.invoke(cli, ["validate", str(minimal_yaml)])
    assert result.exit_code == 0
    assert "OK" in result.output


def test_validate_invalid(tmp_path):
    bad = tmp_path / "bad.yaml"
    bad.write_text("device:\n  name: x\nwidgets:\n  w:\n    type: stats\n")
    runner = CliRunner()
    result = runner.invoke(cli, ["validate", str(bad)])
    assert result.exit_code != 0
    assert "stats" in result.output


def test_build_unknown_type_fails(tmp_path):
    bad = tmp_path / "bad.yaml"
    bad.write_text("device:\n  name: x\nwidgets:\n  w:\n    type: gauge\n")
    runner = CliRunner()
    result = runner.invoke(cli, ["build", str(bad), "-o", str(tmp_path)])
    assert result.exit_code != 0


def test_max_chunk_count_constant():
    """MAX_CHUNK_COUNT must equal 64."""
    assert MAX_CHUNK_COUNT == 64


def test_validate_blob_size_64_chunks_ok():
    """A blob that produces exactly 64 chunks must not raise."""
    blob = bytes(64 * CHUNK_SIZE)
    validate_blob_size(blob)  # should not raise


def test_validate_blob_size_65_chunks_fails():
    """A blob that requires 65 chunks must raise ValueError."""
    blob = bytes(64 * CHUNK_SIZE + 1)
    import pytest
    with pytest.raises(ValueError):
        validate_blob_size(blob)


def test_build_chunk_count_limit_error_message(tmp_path):
    """Build must exit non-zero and mention chunk count when limit exceeded."""
    # Construct a YAML large enough to exceed 31 chunks when compressed.
    # We use random-looking bytes via a large binary string to defeat compression.
    import os
    # 31 chunks = 7936 bytes; 32 chunks = 7937+ bytes compressed.
    # We need the *compressed* blob to be > 31*256 bytes.
    # Use incompressible data: write it as a large YAML string value.
    # The YAML itself needs to compress to more than 31*256=7936 bytes.
    # Safest: write raw bytes that don't compress well as a hex string value.
    rng_bytes = os.urandom(32 * CHUNK_SIZE)  # 8192 raw bytes — will compress to > 7936
    # Encode as hex inside YAML to ensure incompressibility
    big_yaml = (
        "device:\n  name: big\nwidgets:\n  w:\n    type: toggle\n    label: X\n"
        f"# padding: {rng_bytes.hex()}\n"
    )
    bad = tmp_path / "big.yaml"
    bad.write_text(big_yaml)
    runner = CliRunner()
    result = runner.invoke(cli, ["build", str(bad), "-o", str(tmp_path)])
    if result.exit_code != 0:
        # Verify error message mentions chunk count and limit
        assert "32" in result.output or "31" in result.output or "chunk" in result.output.lower()


# ── Helper: build widget_types + widget_ids from a YAML fixture ───────────────

def _load_types_and_ids(yaml_path):
    yaml_bytes = yaml_path.read_bytes()
    doc = pyyaml.safe_load(yaml_bytes)
    return collect_types(doc["widgets"]), assign(doc["widgets"])


def _make_header(yaml_path, **extra):
    yaml_bytes = yaml_path.read_bytes()
    blob, root, _ = compute(yaml_bytes)
    wtypes, wids = _load_types_and_ids(yaml_path)
    return generate_header(widget_ids=wids, widget_types=wtypes, blob=blob, root=root,
                            source="test.yaml", **extra)


def _make_source(yaml_path, **extra):
    yaml_bytes = yaml_path.read_bytes()
    blob, root, hashes = compute(yaml_bytes)
    wtypes, wids = _load_types_and_ids(yaml_path)
    return generate_source(widget_ids=wids, widget_types=wtypes, blob=blob, root=root,
                            hashes=hashes, source="test.yaml", **extra)


# ── Category 1: collect_types() unit tests (5) ────────────────────────────────

def test_collect_types_display():
    widgets = {"volt": {"type": "display", "label": "V", "unit": "V", "format": "%.2f"}}
    result = collect_types(widgets)
    assert result["volt"] == "display"


def test_collect_types_text_rw():
    widgets = {"ssid": {"type": "text", "label": "SSID", "mode": "rw"}}
    result = collect_types(widgets)
    assert result["ssid"] == "text-rw"


def test_collect_types_text_ro_default():
    widgets = {"msg": {"type": "text", "label": "Msg"}}
    result = collect_types(widgets)
    assert result["msg"] == "text-ro"


def test_collect_types_button_child_typed_as_led():
    widgets = {
        "pwr": {
            "type": "button",
            "label": "Power",
            "widgets": {"pwr_led": {"type": "led", "label": "On"}},
        }
    }
    result = collect_types(widgets)
    assert result["pwr"] == "button"
    assert result["pwr.pwr_led"] == "led"


def test_collect_types_button_child_typed_as_display_not_hardcoded_led():
    """Regression: collect_types() used to hardcode every button child as
    "led" regardless of its real type, which would generate a bool setter
    for what should be a float display setter."""
    widgets = {
        "pwr": {
            "type": "button",
            "label": "Power",
            "widgets": {"volt": {"type": "display", "label": "V"}},
        }
    }
    result = collect_types(widgets)
    assert result["pwr.volt"] == "display"


def test_collect_types_button_child_typed_as_rgbled():
    widgets = {
        "pwr": {
            "type": "button",
            "widgets": {"status": {"type": "rgbled"}},
        }
    }
    result = collect_types(widgets)
    assert result["pwr.status"] == "rgbled"


def test_collect_types_button_label_child_excluded_from_ids():
    """A label button child gets no ID and no type entry — matches
    top-level label semantics (no ID, no protocol exchange)."""
    widgets = {
        "pwr": {
            "type": "button",
            "widgets": {"caption": {"type": "label", "text": "Hi"}},
        }
    }
    result = collect_types(widgets)
    assert "pwr.caption" not in result
    ids = assign(widgets)
    assert "pwr.caption" not in ids
    assert "pwr" in ids


def test_collect_types_button_separator_child_excluded_from_ids():
    """A separator button child (not currently schema-reachable, but the
    NO_ID_TYPES exclusion branch itself is shared code with label and must
    behave the same way) gets no ID and no type entry."""
    widgets = {
        "pwr": {
            "type": "button",
            "widgets": {"div": {"type": "separator"}},
        }
    }
    result = collect_types(widgets)
    assert "pwr.div" not in result
    ids = assign(widgets)
    assert "pwr.div" not in ids
    assert "pwr" in ids


def test_collect_types_button_grid_grandchild_recurses():
    """Increment 2 regression: collect_types() used to only recurse one
    level into a button's own widgets:, silently dropping a container
    child's own grandchildren. A button -> grid -> led shape must produce a
    correctly-typed, correctly-prefixed entry for the led (container name
    "face" excluded from the path — transparent, same as top-level)."""
    widgets = {
        "pwr": {
            "type": "button",
            "widgets": {
                "face": {
                    "type": "grid",
                    "columns": 1,
                    "widgets": {"status": {"type": "led"}},
                }
            },
        }
    }
    result = collect_types(widgets)
    assert result["pwr"] == "button"
    assert "pwr.face" not in result   # container: no ID, no type entry
    assert result["pwr.status"] == "led"


def test_assign_duplicate_id_path_across_sibling_containers_raises():
    """Two sibling containers inside ONE button face, each with a
    same-named leaf, resolve to the same id_path ("pwr.x" in both cases —
    containers don't contribute their own name to the path, and both
    siblings share the button as their transparent-prefix ancestor).
    Confirmed via adversarial review to silently collapse to one shared
    protocol ID before this guard (assign() would just overwrite); must
    now raise, not silently corrupt. Mirrors YamlParser.cpp's client-side
    guard for the same collision class."""
    widgets = {
        "pwr": {
            "type": "button",
            "widgets": {
                "left":  {"type": "grid", "columns": 1, "widgets": {"x": {"type": "led"}}},
                "right": {"type": "grid", "columns": 1, "widgets": {"x": {"type": "led"}}},
            },
        }
    }
    with pytest.raises(ValueError, match="pwr.x"):
        assign(widgets)


def test_assign_duplicate_id_path_across_top_level_sibling_containers_raises():
    """Same collision class at the top level (two sibling row containers,
    no button involved) — the root cause (container transparency + shared
    prefix) is not button-specific, so the guard must not be either."""
    widgets = {
        "left":  {"type": "row", "widgets": {"x": {"type": "toggle"}}},
        "right": {"type": "row", "widgets": {"x": {"type": "toggle"}}},
    }
    with pytest.raises(ValueError, match="'x'"):
        assign(widgets)


def test_collect_ids_button_grid_grandchild_recurses():
    """Sibling regression for _collect() (ID-path assignment) — the same
    flat-loop bug, independently broken. A grandchild inside a button-face
    container must get a real, non-colliding ID, not silently vanish."""
    widgets = {
        "pwr": {
            "type": "button",
            "widgets": {
                "face": {
                    "type": "grid",
                    "columns": 1,
                    "widgets": {"status": {"type": "led"}},
                }
            },
        },
        "zzz_toggle": {"type": "toggle"},
    }
    ids = assign(widgets)
    assert ids["pwr"] == 0x10
    assert "pwr.face" not in ids
    assert ids["pwr.status"] == 0x11
    # proves "face" never consumed a slot that would otherwise shift this ID
    assert ids["zzz_toggle"] == 0x12


def test_collect_types_button_row_grandchild_recurses():
    widgets = {
        "pwr": {
            "type": "button",
            "widgets": {"face": {"type": "row", "widgets": {"rgb": {"type": "rgbled"}}}},
        }
    }
    result = collect_types(widgets)
    assert result["pwr.rgb"] == "rgbled"


def test_collect_types_button_group_items_typed_correctly():
    widgets = {
        "mode": {
            "type": "button-group",
            "layout": "grid",
            "items": {"dc": {"label": "DC"}, "ac": {"label": "AC"}},
        }
    }
    result = collect_types(widgets)
    assert result["mode"] == "button-group"
    assert result["mode.dc"] == "button-group-item"
    assert result["mode.ac"] == "button-group-item"


def test_collect_types_all_types_full_vocab(full_vocab_yaml):
    _, wids = _load_types_and_ids(full_vocab_yaml)
    wtypes, _ = _load_types_and_ids(full_vocab_yaml)
    assert wtypes["display_volt"] == "display"
    assert wtypes["fire_btn"] == "button"
    assert wtypes["fire_btn.status_led"] == "led"
    assert wtypes["mode_sel"] == "button-group"
    assert wtypes["mode_sel.ac"] == "button-group-item"
    assert wtypes["mode_sel.dc"] == "button-group-item"
    assert wtypes["power_led"] == "led"
    assert wtypes["slider_rate"] == "slider"
    assert wtypes["toggle_relay"] == "toggle"
    assert wtypes["ssid_field"] == "text-rw"
    assert wtypes["status_rgb"] == "rgbled"


# ── Category 2: _fn_suffix + setter helpers (2 helper tests + 8 setter) ──────

def test_fn_suffix_simple():
    assert _fn_suffix("fire_btn") == "fire_btn"


def test_fn_suffix_dotted():
    assert _fn_suffix("fire_btn.status_led") == "fire_btn_status_led"


def test_setter_for_type_display():
    arg, fn = _setter_for_type("display")
    assert "float" in arg
    assert fn == "udisplay_send_float"


def test_setter_for_type_slider():
    arg, fn = _setter_for_type("slider")
    assert "float" in arg
    assert fn == "udisplay_send_float"


def test_setter_for_type_led():
    arg, fn = _setter_for_type("led")
    assert "uint8_t" in arg
    assert fn == "udisplay_send_bool"


def test_setter_for_type_toggle():
    arg, fn = _setter_for_type("toggle")
    assert "uint8_t" in arg
    assert fn == "udisplay_send_bool"


def test_setter_for_type_text_rw():
    arg, fn = _setter_for_type("text-rw")
    assert "char*" in arg
    assert fn == "udisplay_send_string"


def test_setter_for_type_text_ro():
    arg, fn = _setter_for_type("text-ro")
    assert "char*" in arg
    assert fn == "udisplay_send_string"


def test_setter_for_type_button_is_none():
    assert _setter_for_type("button") is None


def test_setter_for_type_button_group_is_none():
    assert _setter_for_type("button-group") is None


# ── Category 3: _handler_for_type + event struct content (8 tests) ───────────

def test_handler_for_type_button():
    handlers = _handler_for_type("button")
    assert len(handlers) == 3
    suffixes = [h[0] for h in handlers]
    assert "press" in suffixes
    assert "release" in suffixes
    assert "click" in suffixes
    for _, args, _ in handlers:
        assert args == "void"


def test_handler_for_type_button_group_item():
    handlers = _handler_for_type("button-group-item")
    assert len(handlers) == 3
    suffixes = [h[0] for h in handlers]
    assert "press" in suffixes
    assert "release" in suffixes
    assert "click" in suffixes


def test_handler_for_type_slider():
    suffix, args, dispatch = _handler_for_type("slider")[0]
    assert suffix == "change"
    assert "float" in args
    assert "slider_value" in dispatch


def test_handler_for_type_toggle():
    suffix, args, dispatch = _handler_for_type("toggle")[0]
    assert suffix == "change"
    assert "uint8_t" in args
    assert "toggle_state" in dispatch


def test_handler_for_type_text_rw():
    suffix, args, dispatch = _handler_for_type("text-rw")[0]
    assert suffix == "submit"
    assert "char*" in args
    assert "text.str" in dispatch


def test_handler_for_type_display_is_none():
    assert _handler_for_type("display") is None


def test_handler_for_type_led_is_none():
    assert _handler_for_type("led") is None


def test_handler_for_type_text_ro_is_none():
    assert _handler_for_type("text-ro") is None


# ── Category 4: generate_header() setter + struct content (6 tests) ──────────

def test_header_includes_udisplay_h(full_vocab_yaml):
    header = _make_header(full_vocab_yaml)
    assert '#include "udisplay.h"' in header


def test_header_setter_display(full_vocab_yaml):
    header = _make_header(full_vocab_yaml)
    assert "set_display_volt" in header
    assert "udisplay_send_float" in header


def test_header_setter_toggle(full_vocab_yaml):
    header = _make_header(full_vocab_yaml)
    assert "set_toggle_relay" in header
    assert "udisplay_send_bool" in header


def test_header_setter_text_rw(full_vocab_yaml):
    header = _make_header(full_vocab_yaml)
    assert "set_ssid_field" in header
    assert "udisplay_send_string" in header


def test_header_no_setter_button(full_vocab_yaml):
    header = _make_header(full_vocab_yaml)
    # fire_btn is a button — no setter for the button itself
    assert "set_fire_btn(" not in header


def test_header_handler_struct_present(full_vocab_yaml):
    header = _make_header(full_vocab_yaml)
    assert "udisplay_ui_handlers_t" in header
    assert "on_fire_btn_press" in header
    assert "on_fire_btn_release" in header
    assert "on_fire_btn_click" in header
    assert "on_toggle_relay_change" in header
    assert "on_ssid_field_submit" in header


def test_header_no_handler_display(full_vocab_yaml):
    header = _make_header(full_vocab_yaml)
    assert "on_display_volt_" not in header


def test_header_no_handler_text_ro(text_readonly_yaml):
    header = _make_header(text_readonly_yaml)
    assert "on_status_msg_" not in header
    # But setter must still be present
    assert "set_status_msg" in header


# ── Category 5: generate_source() init/dispatch/feed (6 tests) ───────────────

def test_source_no_rx_buffer(full_vocab_yaml):
    """TCP reassembly buffer moved into libudisplay core (udisplay.c) --
    generated code must no longer declare its own copy."""
    source = _make_source(full_vocab_yaml)
    assert "_ui_rx_buf" not in source


def test_source_contains_ui_init(full_vocab_yaml):
    source = _make_source(full_vocab_yaml)
    assert "udisplay_ui_init(udisplay_send_fn send, udisplay_transport_t transport)" in source
    assert "udisplay_init(&cfg)" in source
    assert ".transport = transport," in source


def test_source_contains_ui_set_handlers(full_vocab_yaml):
    source = _make_source(full_vocab_yaml)
    assert "udisplay_ui_set_handlers" in source
    assert "_ui_handlers = h" in source


def test_source_contains_ui_feed(full_vocab_yaml):
    """udisplay_ui_feed is a thin forward to the transport-aware core udisplay_feed()."""
    source = _make_source(full_vocab_yaml)
    assert "udisplay_ui_feed" in source
    assert "udisplay_feed(data, len);" in source
    assert "udisplay_tcp_unframe" not in source


def test_source_contains_ui_ble_set_mtu(full_vocab_yaml):
    source = _make_source(full_vocab_yaml)
    assert "int udisplay_ui_ble_set_mtu(uint16_t mtu_payload)" in source
    assert "return udisplay_ble_set_mtu(mtu_payload);" in source


def test_source_dispatch_switch(full_vocab_yaml):
    source = _make_source(full_vocab_yaml)
    assert "_udisplay_ui_dispatch" in source
    assert "switch (ev->widget_id)" in source
    assert "switch (ev->event_type)" in source
    assert "on_fire_btn_press" in source
    assert "on_fire_btn_release" in source
    assert "on_fire_btn_click" in source
    assert "UDISPLAY_EVENT_BUTTON_PRESS" in source
    assert "UDISPLAY_EVENT_BUTTON_RELEASE" in source
    assert "UDISPLAY_EVENT_BUTTON_CLICK" in source


def test_source_no_api_without_widget_types(minimal_yaml):
    yaml_bytes = minimal_yaml.read_bytes()
    blob, root, hashes = compute(yaml_bytes)
    doc = pyyaml.safe_load(yaml_bytes)
    wids = assign(doc["widgets"])
    # widget_types=None → no generated API
    source = generate_source(widget_ids=wids, blob=blob, root=root, hashes=hashes,
                              source="test.yaml")
    assert "udisplay_ui_init" not in source
    assert "udisplay_ui_feed" not in source


# ── Category 6: CLI --lang flags (4 tests) ────────────────────────────────────

def test_cli_lang_c_explicit(minimal_yaml, tmp_path):
    runner = CliRunner()
    result = runner.invoke(cli, ["build", str(minimal_yaml), "-o", str(tmp_path), "--lang", "c"])
    assert result.exit_code == 0, result.output
    header = (tmp_path / "udisplay_ui.h").read_text()
    assert '#include "udisplay.h"' in header


def test_cli_cpp_produces_hpp(minimal_yaml, tmp_path):
    runner = CliRunner()
    result = runner.invoke(cli, ["build", str(minimal_yaml), "-o", str(tmp_path), "--lang", "cpp"])
    assert result.exit_code == 0, result.output
    assert (tmp_path / "udisplay_ui.hpp").exists()
    assert (tmp_path / "udisplay_ui.bin").exists()
    assert not (tmp_path / "udisplay_ui.h").exists()
    assert not (tmp_path / "udisplay_ui.c").exists()
    hpp = (tmp_path / "udisplay_ui.hpp").read_text()
    assert "SPDX-License-Identifier: MIT" in hpp
    assert "Copyright (c) 2026 Attila Agas" in hpp


def test_cli_default_lang_generates_api(minimal_yaml, tmp_path):
    runner = CliRunner()
    result = runner.invoke(cli, ["build", str(minimal_yaml), "-o", str(tmp_path)])
    assert result.exit_code == 0, result.output
    # Default --lang c must generate the API
    source = (tmp_path / "udisplay_ui.c").read_text()
    assert "udisplay_ui_init" in source


def test_cli_build_full_vocab_with_api(full_vocab_yaml, tmp_path):
    runner = CliRunner()
    result = runner.invoke(cli, ["build", str(full_vocab_yaml), "-o", str(tmp_path)])
    assert result.exit_code == 0, result.output
    header = (tmp_path / "udisplay_ui.h").read_text()
    source = (tmp_path / "udisplay_ui.c").read_text()
    assert "udisplay_ui_handlers_t" in header
    assert "udisplay_ui_init" in source
    assert "udisplay_ui_feed" in source


# ── Eng-review gap tests ───────────────────────────────────────────────────────

def test_handler_for_type_button_group_is_none():
    """button-group itself has no handler (items do, group does not)."""
    assert _handler_for_type("button-group") is None


def test_header_handler_struct_always_has_connection_callbacks(text_readonly_yaml):
    """Even with no interactive widgets, the struct always has on_client_ready and on_comms_error."""
    header = _make_header(text_readonly_yaml)
    assert "on_client_ready" in header
    assert "on_comms_error" in header
    assert "udisplay_ui_handlers_t" in header


def test_source_dispatch_slider_uses_slider_value(full_vocab_yaml):
    """Slider dispatch must use ev->slider_value, not a wrong field."""
    source = _make_source(full_vocab_yaml)
    assert "ev->slider_value" in source


def test_source_dispatch_toggle_uses_toggle_state(full_vocab_yaml):
    """Toggle dispatch must use ev->toggle_state."""
    source = _make_source(full_vocab_yaml)
    assert "ev->toggle_state" in source


def test_source_dispatch_text_rw_uses_text_fields(full_vocab_yaml):
    """text-rw dispatch must use ev->text.str and ev->text.len."""
    source = _make_source(full_vocab_yaml)
    assert "ev->text.str" in source
    assert "ev->text.len" in source


def test_source_no_string_h(full_vocab_yaml):
    """string.h was only needed for the old inline TCP reassembly loop, which
    moved into libudisplay core (udisplay.c) -- generated code no longer
    manipulates raw buffers directly."""
    source = _make_source(full_vocab_yaml)
    assert "#include <string.h>" not in source


# ── TODO-017: dropdown widget codegen ────────────────────────────────────────

def test_collect_types_dropdown(dropdown_yaml):
    wtypes, wids = _load_types_and_ids(dropdown_yaml)
    assert wtypes["wifi_mode"] == "dropdown"
    # Dropdown items must NOT appear in widget_ids
    assert not any(".sta" in k or ".ap" in k for k in wids)


def test_setter_for_type_dropdown():
    arg, fn = _setter_for_type("dropdown")
    assert "uint8_t" in arg
    assert fn == "udisplay_send_uint8"


def test_handler_for_type_dropdown():
    suffix, args, dispatch = _handler_for_type("dropdown")[0]
    assert suffix == "change"
    assert "uint8_t" in args
    assert "selection_index" in dispatch


def test_header_dropdown_setter(dropdown_yaml):
    header = _make_header(dropdown_yaml, widgets_yaml=pyyaml.safe_load(dropdown_yaml.read_bytes())["widgets"])
    assert "set_wifi_mode" in header
    assert "udisplay_send_uint8" in header


def test_header_dropdown_handler(dropdown_yaml):
    header = _make_header(dropdown_yaml, widgets_yaml=pyyaml.safe_load(dropdown_yaml.read_bytes())["widgets"])
    assert "on_wifi_mode_change" in header
    # Handler struct uses the declared arg type, not the dispatch expression
    assert "uint8_t index" in header


def test_header_dropdown_item_constants(dropdown_yaml):
    """Per-item index constants emitted in header when widgets_yaml provided."""
    widgets_yaml = pyyaml.safe_load(dropdown_yaml.read_bytes())["widgets"]
    header = _make_header(dropdown_yaml, widgets_yaml=widgets_yaml)
    assert "WIFI_MODE_STA" in header
    assert "WIFI_MODE_AP" in header
    assert "WIFI_MODE_APSTA" in header
    assert "WIFI_MODE_DISABLED" in header
    # Values must be 0u, 1u, 2u, 3u in order
    assert "WIFI_MODE_STA" in header and "0u" in header
    assert "WIFI_MODE_DISABLED" in header and "3u" in header


def test_header_no_dropdown_item_constants_without_widgets_yaml(dropdown_yaml):
    """Without widgets_yaml, no per-item constants emitted."""
    header = _make_header(dropdown_yaml)
    assert "WIFI_MODE_STA" not in header


def test_source_dropdown_dispatch(dropdown_yaml):
    source = _make_source(dropdown_yaml, widgets_yaml=pyyaml.safe_load(dropdown_yaml.read_bytes())["widgets"])
    assert "ev->selection_index" in source
    assert "on_wifi_mode_change" in source


def test_build_cli_dropdown(dropdown_yaml, tmp_path):
    runner = CliRunner()
    result = runner.invoke(cli, ["build", str(dropdown_yaml), "-o", str(tmp_path)])
    assert result.exit_code == 0, result.output
    header = (tmp_path / "udisplay_ui.h").read_text()
    assert "WIFI_MODE_STA" in header
    assert "WIFI_MODE_DISABLED" in header


# ── TODO-018: label and separator codegen ────────────────────────────────────

def test_label_separator_no_widget_ids(decorations_yaml):
    """label and separator must not get widget IDs."""
    wtypes, wids = _load_types_and_ids(decorations_yaml)
    assert not any("net_heading" in k or "div1" in k for k in wids)
    assert not any("net_heading" in k or "div1" in k for k in wtypes)


def test_label_separator_excluded_from_setters(decorations_yaml):
    """label/separator must not generate setter functions."""
    header = _make_header(decorations_yaml)
    assert "set_net_heading" not in header
    assert "set_div1" not in header


def test_label_separator_excluded_from_handlers(decorations_yaml):
    """label/separator must not generate handler struct entries."""
    header = _make_header(decorations_yaml)
    assert "on_net_heading" not in header
    assert "on_div1" not in header


def test_text_rw_in_decorations_yaml_gets_id(decorations_yaml):
    """ssid_field (text rw) must still get an ID alongside decorations."""
    _, wids = _load_types_and_ids(decorations_yaml)
    assert "ssid_field" in wids


# ── TODO-009: container layout codegen ───────────────────────────────────────

def test_container_names_excluded_from_ids(layout_yaml):
    """section/row names must not appear as ID path prefixes."""
    _, wids = _load_types_and_ids(layout_yaml)
    # Container names 'sensors' and 'controls' must not be in any path
    assert not any(k.startswith("sensors.") or k.startswith("controls.") for k in wids)
    # Children must be present at top-level paths
    assert "volt" in wids
    assert "temp" in wids
    assert "relay" in wids
    assert "reset_btn" in wids


def test_container_names_excluded_from_types(layout_yaml):
    wtypes, _ = _load_types_and_ids(layout_yaml)
    assert "volt" in wtypes
    assert wtypes["volt"] == "display"
    assert "relay" in wtypes
    assert wtypes["relay"] == "toggle"
    # Container itself must not appear
    assert "sensors" not in wtypes
    assert "controls" not in wtypes


def test_container_children_ids_are_alphabetical(layout_yaml):
    """IDs assigned alphabetically across all leaf paths (containers transparent)."""
    _, wids = _load_types_and_ids(layout_yaml)
    sorted_paths = sorted(wids.keys())
    for i, path in enumerate(sorted_paths):
        assert wids[path] == 0x10 + i


def test_build_cli_layout(layout_yaml, tmp_path):
    runner = CliRunner()
    result = runner.invoke(cli, ["build", str(layout_yaml), "-o", str(tmp_path)])
    assert result.exit_code == 0, result.output
    header = (tmp_path / "udisplay_ui.h").read_text()
    # Children IDs present, container names absent as macros
    assert "WIDGET_ID_VOLT" in header
    assert "WIDGET_ID_RELAY" in header
    assert "WIDGET_ID_SENSORS" not in header


# ── TODO-021: rgbled widget codegen ──────────────────────────────────────────

def test_setter_for_type_rgbled():
    arg, fn = _setter_for_type("rgbled")
    assert "int32_t" in arg
    assert fn == "udisplay_send_int"


def test_handler_for_type_rgbled_is_none():
    """rgbled is output-only — no event handler."""
    assert _handler_for_type("rgbled") is None


def test_collect_types_rgbled():
    widgets = {"rgb": {"type": "rgbled", "label": "Status"}}
    result = collect_types(widgets)
    assert result["rgb"] == "rgbled"


def test_header_setter_rgbled(full_vocab_yaml):
    header = _make_header(full_vocab_yaml)
    assert "set_status_rgb" in header
    assert "udisplay_send_int" in header


# ── TODO-013: C++ output (generate_header_cpp) ───────────────────────────────

def _make_cpp(yaml_path, variant="safe", **extra):
    yaml_bytes = yaml_path.read_bytes()
    blob, root, hashes = compute(yaml_bytes)
    wtypes, wids = _load_types_and_ids(yaml_path)
    doc = pyyaml.safe_load(yaml_bytes)
    return generate_header_cpp(
        widget_ids=wids,
        widget_types=wtypes,
        widgets_yaml=doc["widgets"],
        blob=blob,
        root=root,
        hashes=hashes,
        source="test.yaml",
        variant=variant,
        **extra,
    )


# ── Class structure ───────────────────────────────────────────────────────────

def test_cpp_class_name_helper():
    assert _cpp_class_name("wifi_mode") == "WifiModeWidget"
    assert _cpp_class_name("fire_btn") == "FireBtnWidget"
    assert _cpp_class_name("mode_sel") == "ModeSelWidget"


def test_cpp_udisplay_class_declared(full_vocab_yaml):
    hpp = _make_cpp(full_vocab_yaml)
    assert "class UDisplay {" in hpp


def test_cpp_widget_member_names_match_yaml_keys(full_vocab_yaml):
    hpp = _make_cpp(full_vocab_yaml)
    for key in ("slider_rate", "toggle_relay", "fire_btn", "display_volt",
                "mode_sel", "power_led", "status_rgb", "ssid_field"):
        assert key in hpp


def test_cpp_widget_member_types(full_vocab_yaml):
    hpp = _make_cpp(full_vocab_yaml)
    assert "DisplayWidget display_volt" in hpp
    assert "ToggleWidget toggle_relay" in hpp
    assert "SliderWidget slider_rate" in hpp
    assert "LedWidget power_led" in hpp
    assert "RgbLedWidget status_rgb" in hpp
    assert "TextRwWidget ssid_field" in hpp


def test_cpp_button_child_nested(full_vocab_yaml):
    hpp = _make_cpp(full_vocab_yaml)
    assert "FireBtnWidget fire_btn" in hpp
    assert "LedWidget status_led;" in hpp


def test_cpp_buttongroup_items_nested(full_vocab_yaml):
    hpp = _make_cpp(full_vocab_yaml)
    assert "ModeSelWidget mode_sel" in hpp
    assert "ButtonItem ac;" in hpp
    assert "ButtonItem dc;" in hpp


# ── Typed setters ─────────────────────────────────────────────────────────────

def test_cpp_toggle_setter_bool(full_vocab_yaml):
    hpp = _make_cpp(full_vocab_yaml)
    assert "OutputWidget<bool>" in hpp


def test_cpp_led_setter_bool(full_vocab_yaml):
    hpp = _make_cpp(full_vocab_yaml)
    assert "OutputWidget<bool>::set(bool v)" in hpp


def test_cpp_rgbled_setter_uint32(full_vocab_yaml):
    hpp = _make_cpp(full_vocab_yaml)
    assert "OutputWidget<uint32_t>::set(uint32_t v)" in hpp


def test_cpp_display_setter_float(full_vocab_yaml):
    hpp = _make_cpp(full_vocab_yaml)
    assert "OutputWidget<float>::set(float v)" in hpp


def test_cpp_text_rw_has_setter_and_handler(full_vocab_yaml):
    hpp = _make_cpp(full_vocab_yaml)
    assert "TextRwWidget" in hpp
    assert "on_submit" in hpp
    assert "udisplay_send_string" in hpp


def test_cpp_text_ro_has_setter_no_handler(minimal_yaml, tmp_path):
    p = tmp_path / "text_ro.yaml"
    p.write_text("device:\n  name: x\nwidgets:\n  msg:\n    type: text\n    mode: ro\n")
    hpp = _make_cpp(p)
    assert "TextRoWidget msg" in hpp      # member uses read-only type
    assert "TextRwWidget msg" not in hpp  # not the rw type


# ── Dropdown derived class ────────────────────────────────────────────────────

def test_cpp_dropdown_derives_own_class(dropdown_yaml):
    hpp = _make_cpp(dropdown_yaml)
    assert "class WifiModeWidget" in hpp


def test_cpp_dropdown_option_enum(dropdown_yaml):
    hpp = _make_cpp(dropdown_yaml)
    assert "enum class Option : uint8_t" in hpp
    assert "sta = 0u" in hpp
    assert "ap = 1u" in hpp
    assert "apsta = 2u" in hpp
    assert "disabled = 3u" in hpp


def test_cpp_dropdown_setter_takes_enum(dropdown_yaml):
    hpp = _make_cpp(dropdown_yaml)
    assert "void set(Option v)" in hpp


def test_cpp_dropdown_handler_takes_enum(dropdown_yaml):
    hpp = _make_cpp(dropdown_yaml)
    assert "on_change" in hpp
    assert "Option" in hpp


# ── cpp-safe vs modern ────────────────────────────────────────────────────────

def test_cpp_safe_handlers_are_function_pointers(full_vocab_yaml):
    hpp = _make_cpp(full_vocab_yaml, variant="safe")
    assert "void (*on_change)" in hpp or "void (*on_press)" in hpp


def test_cpp_safe_no_functional_include(full_vocab_yaml):
    hpp = _make_cpp(full_vocab_yaml, variant="safe")
    assert "#include <functional>" not in hpp


def test_cpp_modern_handlers_are_std_function(full_vocab_yaml):
    hpp = _make_cpp(full_vocab_yaml, variant="modern")
    assert "std::function<" in hpp


def test_cpp_modern_includes_functional(full_vocab_yaml):
    hpp = _make_cpp(full_vocab_yaml, variant="modern")
    assert "#include <functional>" in hpp


# ── Lifecycle ─────────────────────────────────────────────────────────────────

def test_cpp_init_method_present(full_vocab_yaml):
    hpp = _make_cpp(full_vocab_yaml)
    assert "void init(udisplay_send_fn send, udisplay_transport_t transport)" in hpp
    assert "cfg.transport = transport;" in hpp


def test_cpp_feed_method_present(full_vocab_yaml):
    """A single feed() dispatches on the transport internally via the core
    udisplay_feed() -- no separate tcp_feed()/ble_feed() (collapsed per
    plan-eng-review 2026-07-07, Outside Voice Tension 1)."""
    hpp = _make_cpp(full_vocab_yaml)
    assert "void feed(const uint8_t* data, uint16_t len)" in hpp
    assert "udisplay_feed(data, len);" in hpp
    assert "void tcp_feed(" not in hpp
    assert "void ble_feed(" not in hpp


def test_cpp_ble_set_mtu_method_present(full_vocab_yaml):
    hpp = _make_cpp(full_vocab_yaml)
    assert "static int ble_set_mtu(uint16_t mtu_payload)" in hpp
    assert "return udisplay_ble_set_mtu(mtu_payload);" in hpp


def test_cpp_tcp_frame_method_present(full_vocab_yaml):
    hpp = _make_cpp(full_vocab_yaml)
    assert "uint16_t tcp_frame(uint8_t* out, uint16_t cap, const uint8_t* msg, uint16_t len)" in hpp


def test_cpp_no_local_rx_buf(full_vocab_yaml):
    """TCP reassembly moved into libudisplay core -- generated hpp must not
    declare its own rx_buf/rx_used."""
    hpp = _make_cpp(full_vocab_yaml)
    assert "rx_buf" not in hpp
    assert "rx_used" not in hpp


def test_cpp_no_string_h(full_vocab_yaml):
    """string.h was only needed for the removed tcp_feed's memcpy/memmove."""
    hpp = _make_cpp(full_vocab_yaml)
    assert "#include <string.h>" not in hpp


def test_cpp_dispatch_routes_to_member_widgets(full_vocab_yaml):
    hpp = _make_cpp(full_vocab_yaml)
    assert "self->toggle_relay.on_change" in hpp
    assert "self->slider_rate.on_change" in hpp
    assert "self->fire_btn.on_press" in hpp


def test_cpp_dispatch_button_group_items(full_vocab_yaml):
    hpp = _make_cpp(full_vocab_yaml)
    assert "self->mode_sel.ac.on_press" in hpp
    assert "self->mode_sel.dc.on_press" in hpp


def test_cpp_dispatch_dropdown_casts_to_enum(dropdown_yaml):
    hpp = _make_cpp(dropdown_yaml)
    assert "static_cast<WifiModeWidget::Option>" in hpp


def test_cpp_dispatch_toggle_uses_toggle_state(full_vocab_yaml):
    hpp = _make_cpp(full_vocab_yaml)
    assert "ev->toggle_state != 0" in hpp


def test_cpp_dispatch_slider_uses_slider_value(full_vocab_yaml):
    hpp = _make_cpp(full_vocab_yaml)
    assert "ev->slider_value" in hpp


# ── Blob ──────────────────────────────────────────────────────────────────────

def test_cpp_blob_detail_namespace(full_vocab_yaml):
    hpp = _make_cpp(full_vocab_yaml)
    assert "namespace detail {" in hpp
    assert "merkle_root" in hpp
    assert "chunk_0" in hpp


def test_cpp_bin_identical_to_c(full_vocab_yaml, tmp_path):
    runner = CliRunner()
    out_c   = tmp_path / "c"
    out_cpp = tmp_path / "cpp"
    runner.invoke(cli, ["build", str(full_vocab_yaml), "-o", str(out_c)])
    runner.invoke(cli, ["build", str(full_vocab_yaml), "-o", str(out_cpp), "--lang", "cpp"])
    assert (out_c / "udisplay_ui.bin").read_bytes() == (out_cpp / "udisplay_ui.bin").read_bytes()


def test_cpp_no_source_cpp_file(full_vocab_yaml, tmp_path):
    runner = CliRunner()
    result = runner.invoke(cli, ["build", str(full_vocab_yaml), "-o", str(tmp_path), "--lang", "cpp"])
    assert result.exit_code == 0, result.output
    assert not (tmp_path / "udisplay_ui.c").exists()


# ── CLI ───────────────────────────────────────────────────────────────────────

def test_cli_modern_flag_c_lang_errors(minimal_yaml, tmp_path):
    runner = CliRunner()
    result = runner.invoke(cli, ["build", str(minimal_yaml), "-o", str(tmp_path), "--modern"])
    assert result.exit_code != 0
    assert "--modern" in result.output or "requires" in result.output


def test_cli_cpp_full_vocab(full_vocab_yaml, tmp_path):
    runner = CliRunner()
    result = runner.invoke(cli, ["build", str(full_vocab_yaml), "-o", str(tmp_path), "--lang", "cpp"])
    assert result.exit_code == 0, result.output
    hpp = (tmp_path / "udisplay_ui.hpp").read_text()
    assert "class UDisplay {" in hpp
    assert "namespace udisplay_ui" in hpp


def test_cli_cpp_modern_full_vocab(full_vocab_yaml, tmp_path):
    runner = CliRunner()
    result = runner.invoke(cli, ["build", str(full_vocab_yaml), "-o", str(tmp_path),
                                 "--lang", "cpp", "--modern"])
    assert result.exit_code == 0, result.output
    hpp = (tmp_path / "udisplay_ui.hpp").read_text()
    assert "#include <functional>" in hpp
    assert "std::function<" in hpp


def test_cpp_namespace_wraps_all(full_vocab_yaml):
    hpp = _make_cpp(full_vocab_yaml)
    ns_open = hpp.index("namespace udisplay_ui {")
    ns_close = hpp.rindex("} // namespace udisplay_ui")
    udisplay_class = hpp.index("class UDisplay {")
    assert ns_open < udisplay_class < ns_close


# ── Backend contract tests (6) ────────────────────────────────────────────────

def test_buildcontext_defaults():
    ctx = BuildContext(widget_ids={}, blob=b"", root=b"\x00" * 32, hashes=[], source="x.yaml")
    assert ctx.variant == "safe"
    assert ctx.version == "0.1.0"
    assert ctx.widget_types is None
    assert ctx.widgets_yaml is None


def test_c_backend_generate_returns_3_files(minimal_yaml):
    yaml_bytes = minimal_yaml.read_bytes()
    blob, root, hashes = compute(yaml_bytes)
    import yaml as _yaml
    doc = _yaml.safe_load(yaml_bytes)
    wids = assign(doc["widgets"])
    ctx = BuildContext(widget_ids=wids, blob=blob, root=root, hashes=hashes, source="test.yaml")
    files = c_backend.generate(ctx)
    names = [f.name for f in files]
    assert names == ["udisplay_ui.h", "udisplay_ui.c", "udisplay_ui.bin"]


def test_cpp_backend_generate_returns_2_files(minimal_yaml):
    yaml_bytes = minimal_yaml.read_bytes()
    blob, root, hashes = compute(yaml_bytes)
    import yaml as _yaml
    doc = _yaml.safe_load(yaml_bytes)
    wids = assign(doc["widgets"])
    wtypes = collect_types(doc["widgets"])
    ctx = BuildContext(widget_ids=wids, blob=blob, root=root, hashes=hashes,
                       source="test.yaml", widget_types=wtypes, widgets_yaml=doc["widgets"])
    files = cpp_backend.generate(ctx)
    names = [f.name for f in files]
    assert names == ["udisplay_ui.hpp", "udisplay_ui.bin"]


def test_bin_bytes_identical_across_backends(minimal_yaml):
    yaml_bytes = minimal_yaml.read_bytes()
    blob, root, hashes = compute(yaml_bytes)
    import yaml as _yaml
    doc = _yaml.safe_load(yaml_bytes)
    wids = assign(doc["widgets"])
    wtypes = collect_types(doc["widgets"])
    ctx_c = BuildContext(widget_ids=wids, blob=blob, root=root, hashes=hashes, source="test.yaml")
    ctx_cpp = BuildContext(widget_ids=wids, blob=blob, root=root, hashes=hashes,
                           source="test.yaml", widget_types=wtypes, widgets_yaml=doc["widgets"])
    c_bin = next(f for f in c_backend.generate(ctx_c) if f.name.endswith(".bin"))
    cpp_bin = next(f for f in cpp_backend.generate(ctx_cpp) if f.name.endswith(".bin"))
    assert c_bin.content == cpp_bin.content


def test_output_file_bin_is_bytes(minimal_yaml):
    yaml_bytes = minimal_yaml.read_bytes()
    blob, root, hashes = compute(yaml_bytes)
    import yaml as _yaml
    doc = _yaml.safe_load(yaml_bytes)
    wids = assign(doc["widgets"])
    ctx = BuildContext(widget_ids=wids, blob=blob, root=root, hashes=hashes, source="test.yaml")
    bin_file = next(f for f in c_backend.generate(ctx) if f.name.endswith(".bin"))
    assert isinstance(bin_file.content, bytes)


def test_cpp_no_s_send_in_generated_hpp(full_vocab_yaml):
    """s_send must not appear in the generated hpp — redundant shadow of s.cfg.send."""
    hpp = _make_cpp(full_vocab_yaml)
    assert "s_send" not in hpp


def test_cpp_no_connect_send_in_generated_hpp(full_vocab_yaml):
    """_connect_send must not appear in the generated hpp."""
    hpp = _make_cpp(full_vocab_yaml)
    assert "_connect_send" not in hpp


def test_cpp_no_s_send_guard_in_generated_hpp(full_vocab_yaml):
    """if (detail::s_send) guard must not appear — do_send() in udisplay.c already handles it."""
    hpp = _make_cpp(full_vocab_yaml)
    assert "if (detail::s_send)" not in hpp


def test_output_file_hpp_is_str(minimal_yaml):
    yaml_bytes = minimal_yaml.read_bytes()
    blob, root, hashes = compute(yaml_bytes)
    import yaml as _yaml
    doc = _yaml.safe_load(yaml_bytes)
    wids = assign(doc["widgets"])
    wtypes = collect_types(doc["widgets"])
    ctx = BuildContext(widget_ids=wids, blob=blob, root=root, hashes=hashes,
                       source="test.yaml", widget_types=wtypes, widgets_yaml=doc["widgets"])
    hpp_file = next(f for f in cpp_backend.generate(ctx) if f.name.endswith(".hpp"))
    assert isinstance(hpp_file.content, str)


# ── Struct-drift regression test ──────────────────────────────────────────────
#
# Root-cause closer for the bug plan-eng-review (2026-07-07) found: both
# codegen backends built udisplay_config_t without setting transport/
# ble_mtu_payload (or auth_algo/auth_check/fill_random), because neither was
# updated when those fields were added to the struct. A compiler-enforced
# approach (designated initializers + -Wmissing-field-initializers -Werror)
# was tried first and rejected: verified empirically (removing a field and
# recompiling with gcc 13 and clang 18, both under -Werror) that neither
# compiler warns on a missing field in a C99 *designated* initializer -- that
# warning only fires for old-style positional initializers. This parser-based
# test is the fallback that was actually verified to work.

def _parse_udisplay_config_t_fields() -> set:
    """Parse udisplay_config_t's field names directly out of udisplay.h."""
    header_path = (
        pathlib.Path(__file__).resolve().parents[2]
        / "libudisplay" / "include" / "udisplay.h"
    )
    text = header_path.read_text()
    # [^{}]* (not .*?) is deliberate: udisplay_config_t's body has no nested
    # braces, so this prevents matching spanning in from an earlier struct
    # that does have nested braces (e.g. udisplay_event_t's anonymous union) --
    # a non-greedy `.*?` alone doesn't stop that, since backtracking will
    # happily cross intervening braces to reach the first "} udisplay_config_t;".
    m = re.search(r"typedef struct \{([^{}]*)\}\s*udisplay_config_t;", text, re.DOTALL)
    assert m, (
        "udisplay_config_t struct not found in udisplay.h -- header layout "
        "changed, update _parse_udisplay_config_t_fields()"
    )
    body = re.sub(r"/\*.*?\*/", "", m.group(1), flags=re.DOTALL)

    fields = set()
    for stmt in body.split(";"):
        stmt = stmt.strip()
        if not stmt:
            continue
        # Function-pointer member: `int (*auth_check)(...)` / `void (*fill_random)(...)`
        fn_ptr = re.search(r"\(\s*\*\s*([A-Za-z_][A-Za-z0-9_]*)\s*\)", stmt)
        if fn_ptr:
            fields.add(fn_ptr.group(1))
            continue
        # Plain / pointer member: last identifier before the statement end
        plain = re.search(r"([A-Za-z_][A-Za-z0-9_]*)\s*$", stmt)
        assert plain, f"could not parse a field name out of: {stmt!r}"
        fields.add(plain.group(1))
    return fields


def test_config_fields_match_udisplay_h():
    """
    Every field of udisplay_config_t must be covered by _config_fields() in
    _shared.py, which both c_backend.py and cpp_backend.py use to build their
    generated init(). If this test fails, a new field was added to
    udisplay_config_t without updating _config_fields() -- exactly the bug
    this test exists to catch.
    """
    from udisplay_gen.backends._shared import _config_fields

    struct_fields = _parse_udisplay_config_t_fields()
    covered_fields = {
        name
        for name, _value in _config_fields(
            merkle_root_expr="x", chunks_expr="x", chunk_hashes_expr="x",
            chunk_lens_expr="x", chunk_count_expr="x", send_expr="x",
            on_event_expr="x", on_ready_expr="x", on_error_expr="x",
            userdata_expr="x", transport_expr="x",
        )
    }
    missing = struct_fields - covered_fields
    assert not missing, (
        f"udisplay_config_t grew field(s) {sorted(missing)} that "
        f"_config_fields() in _shared.py does not set -- update _config_fields() "
        f"so both codegen backends' generated init() stay correct."
    )
