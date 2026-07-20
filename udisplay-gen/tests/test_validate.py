"""Tests for YAML validation (schema + semantic)."""
import json
import pathlib

import pytest

import udisplay_gen.validate as validate_module
from udisplay_gen.validate import (
    load_schema, semantic_errors, schema_errors, validate,
    _yaml_line_map, parse_yaml,
)

SCHEMA = load_schema()

REPO_ROOT = pathlib.Path(__file__).parent.parent.parent

VALID_MINIMAL = {
    "device": {"name": "test"},
    "widgets": {"a": {"type": "toggle", "label": "A"}},
}


def test_valid_minimal():
    assert validate(VALID_MINIMAL, SCHEMA) == []


def test_valid_all_types(full_vocab_yaml):
    import yaml as pyyaml
    from udisplay_gen.validate import parse_yaml
    doc = parse_yaml(full_vocab_yaml)
    assert validate(doc, SCHEMA) == []


def test_unknown_widget_type():
    doc = {
        "device": {"name": "x"},
        "widgets": {"w": {"type": "stats"}},
    }
    errors = schema_errors(doc, SCHEMA)
    assert errors
    assert "stats" in errors[0]
    assert "Supported types" in errors[0]


def test_valid_type_unknown_property_gives_specific_error():
    """button is a valid type; color is not a valid button property."""
    doc = {
        "device": {"name": "x"},
        "widgets": {"power_btn": {"type": "button", "label": "Power", "color": "#e05555"}},
    }
    errors = schema_errors(doc, SCHEMA)
    assert errors
    # Must NOT say "unknown widget type button"
    assert not any("unknown widget type" in e for e in errors)
    # Must mention the bad property
    assert any("color" in e for e in errors)
    # Must mention additionalProperties or similar
    assert any("not allowed" in e or "Additional" in e for e in errors)


def test_valid_type_unknown_property_not_reported_as_type_error():
    """toggle with an unknown property should not say 'unknown widget type toggle'."""
    doc = {
        "device": {"name": "x"},
        "widgets": {"sw": {"type": "toggle", "label": "Switch", "color": "red"}},
    }
    errors = schema_errors(doc, SCHEMA)
    assert errors
    assert not any("unknown widget type" in e for e in errors)


def test_line_number_in_error():
    yaml_text = (
        "device:\n"
        "  name: x\n"
        "widgets:\n"
        "  power_btn:\n"
        "    type: button\n"
        "    label: Power\n"
        "    color: '#e05555'\n"
    )
    doc = {"device": {"name": "x"},
           "widgets": {"power_btn": {"type": "button", "label": "Power", "color": "#e05555"}}}
    errors = validate(doc, SCHEMA, yaml_text=yaml_text)
    assert errors
    assert any("line" in e for e in errors)


def test_line_number_unknown_type():
    yaml_text = (
        "device:\n"
        "  name: x\n"
        "widgets:\n"
        "  w:\n"
        "    type: stats\n"
    )
    doc = {"device": {"name": "x"}, "widgets": {"w": {"type": "stats"}}}
    errors = validate(doc, SCHEMA, yaml_text=yaml_text)
    assert errors
    assert any("line" in e for e in errors)
    assert any("stats" in e for e in errors)


def test_yaml_line_map_basic():
    yaml_text = (
        "device:\n"
        "  name: x\n"
        "widgets:\n"
        "  btn:\n"
        "    type: button\n"
    )
    lm = _yaml_line_map(yaml_text)
    assert lm["device"] == 1
    assert lm["device.name"] == 2
    assert lm["widgets"] == 3
    assert lm["widgets.btn"] == 4
    assert lm["widgets.btn.type"] == 5


def test_missing_device_name():
    doc = {"device": {}, "widgets": {"a": {"type": "toggle"}}}
    errors = schema_errors(doc, SCHEMA)
    assert errors


def test_slider_missing_min_max():
    doc = {"device": {"name": "x"}, "widgets": {"s": {"type": "slider"}}}
    errors = schema_errors(doc, SCHEMA)
    assert errors


def test_slider_min_gte_max():
    doc = {
        "device": {"name": "x"},
        "widgets": {"s": {"type": "slider", "min": 10, "max": 5}},
    }
    errors = semantic_errors(doc)
    assert errors
    assert "min" in errors[0] and "max" in errors[0]


def test_slider_min_equals_max():
    doc = {
        "device": {"name": "x"},
        "widgets": {"s": {"type": "slider", "min": 5, "max": 5}},
    }
    errors = semantic_errors(doc)
    assert errors


def test_dpad_missing_position():
    doc = {
        "device": {"name": "x"},
        "widgets": {
            "g": {
                "type": "button-group",
                "layout": "dpad",
                "items": {
                    "up": {"label": "Up", "position": "top"},
                    "dn": {"label": "Down"},  # missing position
                },
            }
        },
    }
    errors = semantic_errors(doc)
    assert errors
    assert "dpad" in errors[0]
    assert "dn" in errors[0]


def test_button_widgets_key_led_accepted():
    doc = {
        "device": {"name": "x"},
        "widgets": {"b": {"type": "button", "widgets": {"c": {"type": "led"}}}},
    }
    assert validate(doc, SCHEMA) == []


def test_button_widgets_key_rgbled_accepted():
    doc = {
        "device": {"name": "x"},
        "widgets": {"b": {"type": "button", "widgets": {"c": {"type": "rgbled"}}}},
    }
    assert validate(doc, SCHEMA) == []


def test_button_widgets_key_display_accepted():
    doc = {
        "device": {"name": "x"},
        "widgets": {"b": {"type": "button", "widgets": {"c": {"type": "display"}}}},
    }
    assert validate(doc, SCHEMA) == []


def test_button_widgets_key_label_accepted():
    doc = {
        "device": {"name": "x"},
        "widgets": {"b": {"type": "button", "widgets": {"c": {"type": "label", "text": "Hi"}}}},
    }
    assert validate(doc, SCHEMA) == []


def test_button_widgets_key_multiple_children_accepted():
    doc = {
        "device": {"name": "x"},
        "widgets": {
            "b": {
                "type": "button",
                "widgets": {
                    "led1": {"type": "led"},
                    "led2": {"type": "led"},
                },
            }
        },
    }
    assert validate(doc, SCHEMA) == []


def test_button_toggle_child_rejected():
    """toggle has its own interactive control — excluded to avoid overlapping
    touch targets with the button's own tap handling."""
    doc = {
        "device": {"name": "x"},
        "widgets": {"b": {"type": "button", "widgets": {"c": {"type": "toggle"}}}},
    }
    errors = schema_errors(doc, SCHEMA)
    assert errors


def test_button_nested_button_rejected():
    doc = {
        "device": {"name": "x"},
        "widgets": {
            "b": {"type": "button", "widgets": {"c": {"type": "button", "label": "Inner"}}}
        },
    }
    errors = schema_errors(doc, SCHEMA)
    assert errors


# ── Increment 2: button face row/grid nesting ────────────────────────────

def test_button_widgets_key_row_accepted():
    doc = {
        "device": {"name": "x"},
        "widgets": {
            "b": {
                "type": "button",
                "widgets": {"face": {"type": "row", "widgets": {"c": {"type": "led"}}}},
            }
        },
    }
    assert validate(doc, SCHEMA) == []


def test_button_widgets_key_grid_accepted():
    doc = {
        "device": {"name": "x"},
        "widgets": {
            "b": {
                "type": "button",
                "widgets": {
                    "face": {
                        "type": "grid",
                        "columns": 1,
                        "widgets": {
                            "lbl": {"type": "label", "text": "Hi"},
                            "led": {"type": "led"},
                        },
                    }
                },
            }
        },
    }
    assert validate(doc, SCHEMA) == []


def test_button_grid_child_toggle_rejected():
    """The excluded-type restriction applies to a container's OWN children,
    not just the button's direct children."""
    doc = {
        "device": {"name": "x"},
        "widgets": {
            "b": {
                "type": "button",
                "widgets": {
                    "face": {"type": "grid", "columns": 1, "widgets": {"c": {"type": "toggle"}}}
                },
            }
        },
    }
    errors = schema_errors(doc, SCHEMA)
    assert errors


def test_button_row_grandchild_slider_rejected():
    """The restriction is recursive, not depth-1 only — a slider two levels
    deep (button -> row -> grid -> slider) must still be rejected."""
    doc = {
        "device": {"name": "x"},
        "widgets": {
            "b": {
                "type": "button",
                "widgets": {
                    "face": {
                        "type": "row",
                        "widgets": {
                            "inner": {
                                "type": "grid",
                                "columns": 1,
                                "widgets": {
                                    "bad": {"type": "slider", "min": 0, "max": 10}
                                },
                            }
                        },
                    }
                },
            }
        },
    }
    errors = schema_errors(doc, SCHEMA)
    assert errors


def test_button_row_grandchild_led_accepted():
    """The recursive restriction accepts allowed types at every depth, not
    just depth 1 — a led three levels deep (button -> row -> grid -> led)
    must validate cleanly."""
    doc = {
        "device": {"name": "x"},
        "widgets": {
            "b": {
                "type": "button",
                "widgets": {
                    "face": {
                        "type": "row",
                        "widgets": {
                            "inner": {
                                "type": "grid",
                                "columns": 1,
                                "widgets": {"deep_led": {"type": "led"}},
                            }
                        },
                    }
                },
            }
        },
    }
    assert validate(doc, SCHEMA) == []


def test_button_old_children_key_rejected():
    """The legacy `children:` key is no longer valid — renamed to `widgets:`."""
    doc = {
        "device": {"name": "x"},
        "widgets": {"b": {"type": "button", "children": {"c": {"type": "led"}}}},
    }
    errors = schema_errors(doc, SCHEMA)
    assert errors


def test_camelcase_name_rejected():
    doc = {"device": {"name": "x"}, "widgets": {"MyWidget": {"type": "toggle"}}}
    errors = schema_errors(doc, SCHEMA)
    assert errors


def test_empty_widget_map_rejected():
    doc = {"device": {"name": "x"}, "widgets": {}}
    errors = schema_errors(doc, SCHEMA)
    assert errors


def test_button_group_single_item_rejected():
    doc = {
        "device": {"name": "x"},
        "widgets": {"g": {"type": "button-group", "items": {"a": {"label": "A"}}}},
    }
    errors = schema_errors(doc, SCHEMA)
    assert errors


def test_display_style_invalid():
    doc = {
        "device": {"name": "x"},
        "widgets": {"d": {"type": "display", "style": "xlarge"}},
    }
    errors = schema_errors(doc, SCHEMA)
    assert errors


# ── TODO-020: LED color property ──────────────────────────────────────────────

def test_led_color_valid():
    doc = {
        "device": {"name": "x"},
        "widgets": {"status": {"type": "led", "label": "Status", "color": "#ff0000"}},
    }
    assert validate(doc, SCHEMA) == []


def test_led_color_invalid_rejected():
    doc = {
        "device": {"name": "x"},
        "widgets": {"status": {"type": "led", "label": "Status", "color": "red"}},
    }
    errors = schema_errors(doc, SCHEMA)
    assert errors


def test_led_no_color_valid():
    doc = {
        "device": {"name": "x"},
        "widgets": {"status": {"type": "led", "label": "Status"}},
    }
    assert validate(doc, SCHEMA) == []


# ── TODO-021: rgbled widget ───────────────────────────────────────────────────

def test_rgbled_valid():
    doc = {
        "device": {"name": "x"},
        "widgets": {"rgb": {"type": "rgbled", "label": "Status"}},
    }
    assert validate(doc, SCHEMA) == []


def test_rgbled_no_label_valid():
    doc = {
        "device": {"name": "x"},
        "widgets": {"rgb": {"type": "rgbled"}},
    }
    assert validate(doc, SCHEMA) == []


# ── TODO-017: dropdown widget ─────────────────────────────────────────────────

def test_dropdown_valid(dropdown_yaml):
    from udisplay_gen.validate import parse_yaml
    doc = parse_yaml(dropdown_yaml)
    assert validate(doc, SCHEMA) == []


def test_dropdown_valid_inline():
    doc = {
        "device": {"name": "x"},
        "widgets": {
            "mode": {
                "type": "dropdown",
                "label": "Mode",
                "items": {"a": "Alpha", "b": "Beta"},
            }
        },
    }
    assert validate(doc, SCHEMA) == []


def test_dropdown_missing_items_rejected():
    doc = {
        "device": {"name": "x"},
        "widgets": {"mode": {"type": "dropdown", "label": "Mode"}},
    }
    errors = schema_errors(doc, SCHEMA)
    assert errors


def test_dropdown_single_item_rejected():
    doc = {
        "device": {"name": "x"},
        "widgets": {
            "mode": {"type": "dropdown", "label": "Mode", "items": {"only": "One"}}
        },
    }
    errors = schema_errors(doc, SCHEMA)
    assert errors


# ── TODO-018: label and separator decoration types ────────────────────────────

def test_label_valid(decorations_yaml):
    from udisplay_gen.validate import parse_yaml
    doc = parse_yaml(decorations_yaml)
    assert validate(doc, SCHEMA) == []


def test_label_valid_inline():
    doc = {
        "device": {"name": "x"},
        "widgets": {
            "hdr": {"type": "label", "text": "Network", "style": "heading"},
        },
    }
    assert validate(doc, SCHEMA) == []


def test_label_missing_text_rejected():
    doc = {
        "device": {"name": "x"},
        "widgets": {"hdr": {"type": "label"}},
    }
    errors = schema_errors(doc, SCHEMA)
    assert errors


def test_label_invalid_style_rejected():
    doc = {
        "device": {"name": "x"},
        "widgets": {"hdr": {"type": "label", "text": "Hi", "style": "xlarge"}},
    }
    errors = schema_errors(doc, SCHEMA)
    assert errors


def test_separator_valid():
    doc = {
        "device": {"name": "x"},
        "widgets": {
            "div1": {"type": "separator"},
            "a": {"type": "toggle", "label": "A"},
        },
    }
    assert validate(doc, SCHEMA) == []


def test_separator_extra_fields_rejected():
    doc = {
        "device": {"name": "x"},
        "widgets": {"div": {"type": "separator", "label": "nope"}},
    }
    errors = schema_errors(doc, SCHEMA)
    assert errors


# ── TODO-009: layout containers (section, row, grid) ─────────────────────────

def test_section_valid(layout_yaml):
    from udisplay_gen.validate import parse_yaml
    doc = parse_yaml(layout_yaml)
    assert validate(doc, SCHEMA) == []


def test_section_valid_inline():
    doc = {
        "device": {"name": "x"},
        "widgets": {
            "grp": {
                "type": "section",
                "label": "Sensors",
                "collapsible": True,
                "widgets": {"volt": {"type": "display", "label": "V"}},
            }
        },
    }
    assert validate(doc, SCHEMA) == []


def test_section_missing_widgets_rejected():
    doc = {
        "device": {"name": "x"},
        "widgets": {"grp": {"type": "section"}},
    }
    errors = schema_errors(doc, SCHEMA)
    assert errors


def test_row_valid_inline():
    doc = {
        "device": {"name": "x"},
        "widgets": {
            "r": {
                "type": "row",
                "widgets": {
                    "relay": {"type": "toggle", "label": "Relay", "flex": 2},
                    "btn": {"type": "button", "label": "Reset"},
                },
            }
        },
    }
    assert validate(doc, SCHEMA) == []


def test_grid_valid_inline():
    doc = {
        "device": {"name": "x"},
        "widgets": {
            "g": {
                "type": "grid",
                "columns": 2,
                "widgets": {
                    "a": {"type": "toggle", "label": "A"},
                    "b": {"type": "toggle", "label": "B"},
                },
            }
        },
    }
    assert validate(doc, SCHEMA) == []


def test_grid_columns_one_valid():
    doc = {
        "device": {"name": "x"},
        "widgets": {
            "g": {
                "type": "grid",
                "columns": 1,
                "widgets": {"a": {"type": "toggle", "label": "A"}},
            }
        },
    }
    assert validate(doc, SCHEMA) == []


def test_grid_columns_zero_rejected():
    doc = {
        "device": {"name": "x"},
        "widgets": {
            "g": {
                "type": "grid",
                "columns": 0,
                "widgets": {"a": {"type": "toggle", "label": "A"}},
            }
        },
    }
    errors = schema_errors(doc, SCHEMA)
    assert errors


def test_grid_missing_columns_rejected():
    doc = {
        "device": {"name": "x"},
        "widgets": {
            "g": {
                "type": "grid",
                "widgets": {"a": {"type": "toggle", "label": "A"}},
            }
        },
    }
    errors = schema_errors(doc, SCHEMA)
    assert errors


def test_flex_on_widget_valid():
    doc = {
        "device": {"name": "x"},
        "widgets": {"a": {"type": "toggle", "label": "A", "flex": 3}},
    }
    assert validate(doc, SCHEMA) == []


def test_flex_zero_rejected():
    doc = {
        "device": {"name": "x"},
        "widgets": {"a": {"type": "toggle", "label": "A", "flex": 0}},
    }
    errors = schema_errors(doc, SCHEMA)
    assert errors


def test_align_on_row_child_valid():
    doc = {
        "device": {"name": "x"},
        "widgets": {
            "r": {
                "type": "row",
                "align": "center",
                "widgets": {"a": {"type": "toggle", "label": "A", "align": "right"}},
            }
        },
    }
    assert validate(doc, SCHEMA) == []


def test_align_invalid_value_rejected():
    doc = {
        "device": {"name": "x"},
        "widgets": {
            "r": {
                "type": "row",
                "widgets": {"a": {"type": "toggle", "label": "A", "align": "sideways"}},
            }
        },
    }
    errors = schema_errors(doc, SCHEMA)
    assert errors


def test_label_text_align_accepts_justify():
    """label's textAlign accepts a 4th value (justify) that align does not."""
    doc = {
        "device": {"name": "x"},
        "widgets": {"a": {"type": "label", "text": "Hi", "textAlign": "justify"}},
    }
    assert validate(doc, SCHEMA) == []


def test_label_align_does_not_accept_justify():
    """label's align is the same 3-value row/grid-position enum as every other
    widget (left/right/center) — justify is textAlign-only (text alignment),
    not a valid row-position value."""
    doc = {
        "device": {"name": "x"},
        "widgets": {"a": {"type": "label", "text": "Hi", "align": "justify"}},
    }
    errors = schema_errors(doc, SCHEMA)
    assert errors


def test_row_align_does_not_accept_justify():
    """row/grid's align is the 3-value enum (left/right/center) — justify
    is textAlign-only (text alignment), not a valid row-position value."""
    doc = {
        "device": {"name": "x"},
        "widgets": {"r": {"type": "row", "align": "justify", "widgets": {"a": {"type": "toggle", "label": "A"}}}},
    }
    errors = schema_errors(doc, SCHEMA)
    assert errors


def test_label_align_and_text_align_together():
    """label can set both align (row/grid position) and textAlign (text
    alignment within the label) simultaneously — they are independent."""
    doc = {
        "device": {"name": "x"},
        "widgets": {
            "r": {
                "type": "row",
                "widgets": {
                    "a": {
                        "type": "label",
                        "text": "Hi",
                        "align": "right",
                        "textAlign": "justify",
                    }
                },
            }
        },
    }
    assert validate(doc, SCHEMA) == []


# ── Semantic: duplicate leaf names ────────────────────────────────────────────

def test_duplicate_leaf_name_in_same_scope():
    """Two widgets with the same key at the top level (impossible in YAML dict, caught by schema)."""
    # YAML dicts deduplicate keys, so test cross-container duplication instead
    doc = {
        "device": {"name": "x"},
        "widgets": {
            "relay": {"type": "toggle", "label": "Relay"},
            "grp": {
                "type": "section",
                "widgets": {"relay": {"type": "toggle", "label": "Relay2"}},
            },
        },
    }
    errors = semantic_errors(doc)
    assert errors
    assert any("relay" in e and "duplicate" in e for e in errors)


def test_duplicate_leaf_name_across_nested_containers():
    doc = {
        "device": {"name": "x"},
        "widgets": {
            "sec_a": {
                "type": "section",
                "widgets": {"volt": {"type": "display", "label": "V"}},
            },
            "sec_b": {
                "type": "section",
                "widgets": {"volt": {"type": "display", "label": "V2"}},
            },
        },
    }
    errors = semantic_errors(doc)
    assert errors
    assert any("volt" in e for e in errors)


def test_unique_leaf_names_across_containers_valid():
    doc = {
        "device": {"name": "x"},
        "widgets": {
            "sec_a": {
                "type": "section",
                "widgets": {"volt": {"type": "display", "label": "V"}},
            },
            "sec_b": {
                "type": "section",
                "widgets": {"temp": {"type": "display", "label": "T"}},
            },
        },
    }
    assert semantic_errors(doc) == []


# ── TODO-026: global style: block ─────────────────────────────────────────────

def test_style_block_valid_all_tokens():
    """A named theme with all 16 known tokens must pass schema validation."""
    doc = {
        "device": {"name": "x"},
        "widgets": {"a": {"type": "toggle", "label": "A"}},
        "style": {
            "default": {
                "background": "#0d0d1a",
                "surface": "#1a1a2e",
                "text": "#c0c0c0",
                "text_muted": "#888888",
                "text_heading": "#e0e0e0",
                "border": "#1e1e3a",
                "line": "#1e1e3a",
                "accent": "#00d4aa",
                "button": "#00d4aa",
                "button_text": "#0d0d1a",
                "led_on": "#ffffff",
                "led_off": "transparent",
                "led_border": "#ffffff",
                "success": "#00d4aa",
                "warning": "#f5a623",
                "error": "#e05555",
            }
        },
    }
    assert validate(doc, SCHEMA) == []


def test_style_block_unknown_token_rejected():
    """An unknown token name inside a named theme must be rejected (additionalProperties: false)."""
    doc = {
        "device": {"name": "x"},
        "widgets": {"a": {"type": "toggle", "label": "A"}},
        "style": {
            "default": {"backgrnd": "#0d0d1a"},  # typo: backgrnd instead of background
        },
    }
    errors = schema_errors(doc, SCHEMA)
    assert errors


# --- Schema file location (TODO-035: root udisplay.schema.json is canonical, the
# bundled copy under udisplay_gen/schema/ is a symlink to it, not a hand copy) ---

def test_schema_is_symlink_to_root():
    """Regression guard: if a future change replaces the symlink with a hand copy
    again, this must fail loudly instead of letting the two files silently drift."""
    assert validate_module._BUNDLED_SCHEMA.is_symlink()
    assert (
        validate_module._BUNDLED_SCHEMA.resolve()
        == (REPO_ROOT / "udisplay.schema.json").resolve()
    )


def test_load_schema_happy_path():
    schema = load_schema()
    assert schema["$id"] == "udisplay.schema.json"
    assert "displayWidget" in schema["$defs"]


def test_load_schema_friendly_error_on_corrupt_file(tmp_path, monkeypatch):
    """On Windows, a Git checkout without core.symlinks=true replaces the symlink
    with a plain-text placeholder file. Simulate that and check the error names
    the actual cause instead of a bare JSONDecodeError."""
    placeholder = tmp_path / "udisplay.schema.json"
    placeholder.write_text("../../../udisplay.schema.json")
    monkeypatch.setattr(validate_module, "_BUNDLED_SCHEMA", placeholder)

    with pytest.raises(json.JSONDecodeError) as exc_info:
        load_schema()
    assert "core.symlinks" in str(exc_info.value)


def test_debug_yaml_validates_against_root_schema():
    """Regression test for TODO-035's content merge: debug_state (used by
    udisplay-client's design-mode preview) must validate against the schema
    SCHEMA now resolves to (the root file, via the symlink)."""
    debug_yaml = REPO_ROOT / "udisplay-client" / "debug.yaml"
    doc = parse_yaml(debug_yaml)
    assert schema_errors(doc, SCHEMA) == []
