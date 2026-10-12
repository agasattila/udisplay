"""Issue #43: every widget gets a widget ID — containers (section/row/grid/
dpad) and decorations (label/separator) included — so firmware can target any
node with SET_PROPERTY / RESET_PROPERTY. Container keys are segments of
their descendants' ID paths (`panel.power_btn.face_row.icon`), and the
generated object APIs nest the same way. Covers ID assignment, validation,
and every backend's generated API (docs/designs/widget-id-for-every-widget.md).
"""
import pathlib
import re
import shutil
import subprocess
import sys

import pytest
import yaml as pyyaml
from click.testing import CliRunner

from udisplay_gen.backends import BuildContext, cpp_backend, python_backend
from udisplay_gen.cli import cli
from udisplay_gen.merkle import compute
from udisplay_gen.validate import semantic_errors
from udisplay_gen.widget_ids import MAX_WIDGETS, assign, collect_types

LIBUDISPLAY_INCLUDE = pathlib.Path(__file__).resolve().parents[2] / "libudisplay" / "include"

# Every container type at the top level (dpad included — the pre-#43 C++
# backend silently dropped a top-level dpad's buttons), plus a button face
# holding a label and a row with its own children.
EVERY_WIDGET_YAML = """\
device:
  name: every widget
  capabilities: [label, layout-v2, dropdown]
widgets:
  panel:
    type: section
    label: Panel
    widgets:
      title:
        type: label
        text: Controls
      power_btn:
        type: button
        label: Power
        widgets:
          face_row:
            type: row
            widgets:
              icon:
                type: led
              caption:
                type: label
                text: PWR
          status:
            type: rgbled
  meters:
    type: row
    widgets:
      amps:
        type: display
      rate:
        type: slider
        min: 0
        max: 10
  zone:
    type: grid
    columns: 2
    widgets:
      relay:
        type: toggle
      sep:
        type: separator
      net:
        type: dropdown
        items:
          sta: Station
          ap: Access Point
      ssid:
        type: text
        mode: rw
  pad:
    type: dpad
    widgets:
      up_btn:
        type: button
        label: Up
        position: top
      down_btn:
        type: button
        label: Down
        position: bottom
  mode_sel:
    type: button-group
    items:
      dc:
        label: DC
      ac:
        label: AC
"""

# The same tree with `panel` flagged as a namespace.
NAMESPACED_PANEL_YAML = EVERY_WIDGET_YAML.replace(
    "  panel:\n    type: section\n", "  panel:\n    type: section\n    namespace: true\n")

EXPECTED_PATHS = {
    "panel", "panel.title", "panel.power_btn", "panel.power_btn.face_row",
    "panel.power_btn.face_row.icon", "panel.power_btn.face_row.caption",
    "panel.power_btn.status", "meters", "meters.amps", "meters.rate",
    "zone", "zone.relay", "zone.sep", "zone.net", "zone.ssid",
    "pad", "pad.up_btn", "pad.down_btn",
    "mode_sel", "mode_sel.dc", "mode_sel.ac",
}


def _widgets(yaml_text=EVERY_WIDGET_YAML):
    return pyyaml.safe_load(yaml_text)["widgets"]


def _ctx(yaml_text=EVERY_WIDGET_YAML, **extra):
    yaml_bytes = yaml_text.encode()
    blob, root, hashes = compute(yaml_bytes)
    widgets = pyyaml.safe_load(yaml_bytes)["widgets"]
    return BuildContext(
        widget_ids=assign(widgets), blob=blob, root=root, hashes=hashes,
        source="test.yaml", widget_types=collect_types(widgets),
        widgets_yaml=widgets, **extra,
    )


def _build(tmp_path, lang, yaml_text=EVERY_WIDGET_YAML, extra_args=()):
    src = tmp_path / "every.yaml"
    src.write_text(yaml_text)
    out = tmp_path / f"out_{lang}"
    result = CliRunner().invoke(
        cli, ["build", str(src), "-o", str(out), "--lang", lang, *extra_args])
    assert result.exit_code == 0, result.output
    return out


# ── ID assignment ────────────────────────────────────────────────────────────

class TestAssign:
    def test_every_widget_gets_an_id(self):
        ids = assign(_widgets())
        assert set(ids) == EXPECTED_PATHS

    def test_ids_are_dense_and_alphabetical(self):
        ids = assign(_widgets())
        assert [ids[p] for p in sorted(ids)] == list(range(0x10, 0x10 + len(ids)))

    def test_container_keys_are_path_segments(self):
        """Every ancestor's key is part of a widget's path — containers
        included, at any depth (a row on a button face too)."""
        ids = assign(_widgets())
        assert "panel.power_btn.face_row.icon" in ids
        assert "power_btn.icon" not in ids and "icon" not in ids
        assert "pad.up_btn" in ids and "up_btn" not in ids

    def test_types_reported_for_containers_and_decorations(self):
        types = collect_types(_widgets())
        assert types["panel"] == "section"
        assert types["meters"] == "row"
        assert types["zone"] == "grid"
        assert types["pad"] == "dpad"
        assert types["panel.title"] == "label"
        assert types["zone.sep"] == "separator"
        assert types["panel.power_btn.face_row"] == "row"
        assert types["panel.power_btn.face_row.caption"] == "label"
        assert set(types) == EXPECTED_PATHS

    def test_dropdown_items_still_get_no_id(self):
        ids = assign({"dd": {"type": "dropdown", "items": {"a": "A", "b": "B"}}})
        assert ids == {"dd": 0x10}

    def test_container_name_reused_by_leaf_in_another_section_allowed(self):
        """Keys are local to their parent: a section `advanced` and a slider
        `advanced` in another section are distinct paths."""
        widgets = {
            "advanced": {"type": "section", "widgets": {"x": {"type": "toggle"}}},
            "basic": {"type": "section", "widgets": {"advanced": {"type": "slider"}}},
        }
        ids = assign(widgets)
        assert {"advanced", "advanced.x", "basic", "basic.advanced"} == set(ids)

    def test_same_label_name_in_two_sections_allowed(self):
        widgets = {
            "a": {"type": "section", "widgets": {"hdr": {"type": "label", "text": "A"}}},
            "b": {"type": "section", "widgets": {"hdr": {"type": "label", "text": "B"}}},
        }
        ids = assign(widgets)
        assert ids["a.hdr"] != ids["b.hdr"]

    def test_dotted_key_shadowing_a_nested_path_raises(self):
        """Only a key containing '.' (rejected by the schema) can collide."""
        widgets = {
            "a": {"type": "section", "widgets": {"b": {"type": "led"}}},
            "a.b": {"type": "led"},
        }
        with pytest.raises(ValueError, match="'a.b'"):
            assign(widgets)

    def test_cap_counts_containers(self):
        """240 slots total — a container consumes one like any leaf."""
        leaves = {f"w{i:03d}": {"type": "led"} for i in range(MAX_WIDGETS - 1)}
        assert len(assign({"sec": {"type": "section", "widgets": leaves}})) == MAX_WIDGETS
        leaves[f"w{MAX_WIDGETS:03d}"] = {"type": "led"}
        with pytest.raises(ValueError, match="exceeds maximum"):
            assign({"sec": {"type": "section", "widgets": leaves}})


# ── validate ─────────────────────────────────────────────────────────────────

class TestValidate:
    def test_every_widget_fixture_is_valid(self):
        assert semantic_errors(pyyaml.safe_load(EVERY_WIDGET_YAML)) == []

    def test_container_name_reused_by_leaf_in_namespaced_section_valid(self):
        doc = {"widgets": {
            "advanced": {"type": "section", "widgets": {"x": {"type": "toggle"}}},
            "basic": {"type": "section", "namespace": True,
                      "widgets": {"advanced": {"type": "slider", "min": 0, "max": 1}}},
        }}
        assert semantic_errors(doc) == []

    def test_label_in_row_named_like_top_level_leaf_rejected(self):
        """A transparent row adds no name segment: its `volt` and the
        top-level `volt` would both be WIDGET_ID_VOLT / ui.volt."""
        doc = {"widgets": {
            "row1": {"type": "row", "widgets": {"volt": {"type": "label", "text": "V"}}},
            "volt": {"type": "display"},
        }}
        errs = semantic_errors(doc)
        assert len(errs) == 1
        assert "widgets.row1.volt, widgets.volt" in errs[0] and "'volt'" in errs[0]

    def test_same_face_child_name_in_two_buttons_still_allowed(self):
        doc = {"widgets": {
            "a": {"type": "button", "widgets": {"icon": {"type": "led"}}},
            "b": {"type": "button", "widgets": {"icon": {"type": "led"}}},
        }}
        assert semantic_errors(doc) == []


# ── C backend ────────────────────────────────────────────────────────────────

class TestCBackend:
    def test_macro_for_every_widget(self, tmp_path):
        header = (_build(tmp_path, "c") / "udisplay_ui.h").read_text()
        for macro in ("WIDGET_ID_PANEL", "WIDGET_ID_METERS", "WIDGET_ID_ZONE", "WIDGET_ID_PAD",
                      "WIDGET_ID_TITLE", "WIDGET_ID_SEP",
                      "WIDGET_ID_POWER_BTN_FACE_ROW",
                      "WIDGET_ID_POWER_BTN_CAPTION",
                      "WIDGET_ID_RATE", "WIDGET_ID_PAD_UP_BTN",
                      "WIDGET_ID_MODE_SEL_AC"):
            assert f"#define {macro} " in header, macro

    def test_no_typed_api_for_containers_or_decorations(self, tmp_path):
        header = (_build(tmp_path, "c") / "udisplay_ui.h").read_text()
        for name in ("panel", "meters", "zone", "pad", "title", "sep"):
            assert f"set_{name}(" not in header
            assert not re.search(rf"on_{name}_(press|release|click|change|submit)\b",
                                 header), name


# ── C++ backend ──────────────────────────────────────────────────────────────

class TestCppBackend:
    def _header(self, tmp_path, extra_args=()):
        return (_build(tmp_path, "cpp", extra_args=extra_args) / "udisplay_ui.hpp").read_text()

    def test_namespaces_get_classes_transparent_containers_do_not(self, tmp_path):
        """A dpad is always a namespace: its own class, holding its children
        (ui.pad.up_btn). An unflagged section/row/grid is a plain Widget
        member whose children join the enclosing scope (ui.rate)."""
        header = self._header(tmp_path)
        assert "class PadWidget : public Widget {" in header
        assert "    PadWidget pad;" in header
        for name in ("panel", "meters", "zone"):
            assert f"    Widget {name};" in header, name
            assert f"class {name.capitalize()}Widget " not in header, name
        assert "    SliderWidget rate;" in header
        assert "    ToggleWidget relay;" in header

    def test_decorations_are_widget_members(self, tmp_path):
        header = self._header(tmp_path)
        assert "    Widget title;" in header
        assert "    Widget sep;" in header

    def test_dpad_children_are_members(self, tmp_path):
        """Regression (A2): the C++ member walk's old local container set
        lacked "dpad", so a top-level dpad's buttons were never members."""
        header = self._header(tmp_path)
        assert "    ButtonWidget up_btn;" in header
        assert "    ButtonWidget down_btn;" in header

    def test_face_sub_members_typed_by_their_own_type(self, tmp_path):
        """Regression (A3): every face child used to be emitted as LedWidget.
        A button face is a namespace; a row on it is transparent, so its
        children are members of the button (ui.power_btn.icon)."""
        header = self._header(tmp_path)
        assert "class PowerBtnWidget : public ButtonWidget {" in header
        assert "    Widget face_row;" in header
        assert "    LedWidget icon;" in header
        assert "    RgbLedWidget status;" in header
        assert "    Widget caption;" in header
        assert "LedWidget caption;" not in header

    def test_classes_emitted_before_their_users(self):
        text = NAMESPACED_PANEL_YAML
        header = cpp_backend._generate_header_cpp(_ctx(text))
        assert (header.index("class PanelPowerBtnWidget ")
                < header.index("class PanelWidget "))
        assert "    PanelPowerBtnWidget power_btn;" in header
        assert "self->panel.power_btn.icon" not in header  # led: no events
        assert "self->panel.power_btn.on_click()" in header

    def test_nested_dispatch_uses_name_path(self, tmp_path):
        header = self._header(tmp_path)
        assert "self->rate.on_change(ev->slider_value)" in header
        assert "self->pad.up_btn.on_click()" in header
        assert "self->mode_sel.ac.on_click()" in header

    def test_widget_base_exposes_property_api(self, tmp_path):
        header = self._header(tmp_path)
        assert "udisplay_set_property(_ctx, _id, property_id, value)" in header
        assert "udisplay_reset_property(_ctx, _id, property_id)" in header
        assert "uint8_t id() const" in header

    @pytest.mark.parametrize("name", ["init", "feed", "ctx", "on_client_ready"])
    def test_container_name_colliding_with_udisplay_member_rejected(self, name):
        text = EVERY_WIDGET_YAML.replace("  meters:\n", f"  {name}:\n")
        with pytest.raises(ValueError, match=f"'{name}' collides"):
            cpp_backend.generate(_ctx(text))

    def test_cpp_keyword_container_name_rejected(self):
        text = EVERY_WIDGET_YAML.replace("  zone:\n", "  default:\n")
        with pytest.raises(ValueError, match="C\\+\\+ keyword"):
            cpp_backend.generate(_ctx(text))

    @pytest.mark.parametrize("name", ["set_property", "id", "on_click"])
    def test_face_child_shadowing_widget_api_rejected(self, name):
        text = EVERY_WIDGET_YAML.replace("          status:\n", f"          {name}:\n")
        with pytest.raises(ValueError, match=f"'{name}' collides"):
            cpp_backend.generate(_ctx(text))

    @pytest.mark.parametrize("name", ["set_property", "id"])
    def test_namespace_child_shadowing_widget_api_rejected(self, name):
        text = EVERY_WIDGET_YAML.replace("      up_btn:\n", f"      {name}:\n")
        with pytest.raises(ValueError, match=f"'{name}' collides"):
            cpp_backend.generate(_ctx(text))

    @pytest.mark.parametrize("name", ["feed", "init"])
    def test_transparent_container_child_checked_against_udisplay(self, name):
        """A transparent row's children are UDisplay members (scope_members()),
        so they must not shadow UDisplay's own API."""
        text = EVERY_WIDGET_YAML.replace("      amps:\n", f"      {name}:\n")
        with pytest.raises(ValueError, match=f"'{name}' collides"):
            cpp_backend.generate(_ctx(text))

    def test_transparent_container_child_named_like_widget_api_accepted(self):
        """...but not against Widget's: `id` inside a transparent row is a
        UDisplay member, where it shadows nothing."""
        text = EVERY_WIDGET_YAML.replace("      amps:\n", "      id:\n")
        header = cpp_backend._generate_header_cpp(_ctx(text))
        assert "    DisplayWidget id;" in header

    def test_class_name_collision_rejected(self):
        """`pad_up_btn` and `pad.up_btn` would both be PadUpBtnWidget /
        WIDGET_ID_PAD_UP_BTN."""
        text = EVERY_WIDGET_YAML.replace(
            "  zone:\n", "  pad_up_btn:\n    type: led\n  zone:\n")
        with pytest.raises(ValueError, match="WIDGET_ID_PAD_UP_BTN"):
            cpp_backend.generate(_ctx(text))


# ── Generated code compiles ──────────────────────────────────────────────────

@pytest.mark.skipif(shutil.which("g++") is None or shutil.which("gcc") is None,
                    reason="gcc/g++ not installed")
class TestGeneratedCompiles:
    """Text greps alone let non-compiling output through before (see the
    `codegen-tests-grep-not-compile` learning) — syntax-check for real."""

    def _check(self, compiler, path, *std):
        proc = subprocess.run(
            [compiler, *std, "-fsyntax-only", "-Wall", "-Werror",
             f"-I{LIBUDISPLAY_INCLUDE}", "-I", str(path.parent), str(path)],
            capture_output=True, text=True)
        assert proc.returncode == 0, proc.stderr

    def test_c_header_and_source(self, tmp_path):
        out = _build(tmp_path, "c")
        self._check("gcc", out / "udisplay_ui.c", "-std=c99")

    @pytest.mark.parametrize("variant", [(), ("--modern",)])
    def test_cpp_header(self, tmp_path, variant):
        out = _build(tmp_path, "cpp", extra_args=variant)
        tu = out / "tu.cpp"
        tu.write_text(
            '#include "udisplay_ui.hpp"\n'
            "static udisplay_ui::UDisplay ui;\n"
            "void use() {\n"
            "    ui.panel.set_property(UDISPLAY_PROP_VISIBLE, 0);\n"
            "    ui.power_btn.face_row.reset_property(UDISPLAY_PROP_VISIBLE);\n"
            "    ui.power_btn.caption.reset_property(UDISPLAY_PROP_VISIBLE);\n"
            "    ui.power_btn.status.set(0x00FF00u);\n"
            "    ui.pad.up_btn.set_property(UDISPLAY_PROP_ENABLED, ui.pad.id());\n"
            "    ui.meters.set_property(UDISPLAY_PROP_VISIBLE, 1);\n"
            "    ui.rate.set(1.0f);\n"
            "    ui.mode_sel.set(udisplay_ui::ModeSelWidget::Item::ac);\n"
            "}\n")
        self._check("g++", tu, "-std=c++17")


# ── MicroPython backend ──────────────────────────────────────────────────────

class TestPythonBackend:
    def test_containers_and_decorations_are_widget_members(self, tmp_path):
        ui_py = (_build(tmp_path, "micropython") / "ui.py").read_text()
        for name in ("panel", "meters", "zone", "pad"):
            assert (f"self.{name} = ContainerWidget(self._device, "
                    f"WIDGET_ID_{name.upper()})") in ui_py
        assert "self.title = Widget(self._device, WIDGET_ID_TITLE)" in ui_py
        assert "self.sep = Widget(self._device, WIDGET_ID_SEP)" in ui_py
        assert "self.power_btn = ButtonWidget(" in ui_py
        assert "self.power_btn.face_row = ContainerWidget(" in ui_py
        assert "self.power_btn.caption = Widget(" in ui_py
        assert "self.power_btn.icon = LedWidget(" in ui_py
        assert "self.pad.up_btn = ButtonWidget(" in ui_py

    def test_set_property_on_container_sends_set_property(self, tmp_path):
        out = _build(tmp_path, "micropython")
        sys.path.insert(0, str(out))
        try:
            for mod in ("ui", "udisplay_runtime"):
                sys.modules.pop(mod, None)
            import ui as generated_ui
            from udisplay_runtime import (MSG_CLIENT_READY, UDISPLAY_PROP_ENABLED,
                                          UDISPLAY_PROP_VISIBLE, tcp_frame, tcp_unframe)

            sent = []
            u = generated_ui.UI(send=sent.append)
            u.on_connect()
            u.feed(tcp_frame(bytes([MSG_CLIENT_READY])))
            sent.clear()

            u.panel.set_property(UDISPLAY_PROP_VISIBLE, 0)
            u.power_btn.caption.reset_property(UDISPLAY_PROP_VISIBLE)
            u.relay.set_property(UDISPLAY_PROP_ENABLED, 0)
            payloads = [tcp_unframe(m)[0] for m in sent]
            assert payloads == [
                bytes([0x32, generated_ui.WIDGET_ID_PANEL, UDISPLAY_PROP_VISIBLE, 0]),
                bytes([0x33, generated_ui.WIDGET_ID_POWER_BTN_CAPTION,
                       UDISPLAY_PROP_VISIBLE]),
                bytes([0x32, generated_ui.WIDGET_ID_RELAY, UDISPLAY_PROP_ENABLED, 0]),
            ]
            assert u.pad.id == generated_ui.WIDGET_ID_PAD

            # Events reach namespace members and members inlined from a
            # transparent row.
            clicks, rates = [], []
            u.pad.up_btn.on_click = lambda: clicks.append("up")
            u.rate.on_change = rates.append
            u._on_event(generated_ui.WIDGET_ID_PAD_UP_BTN,
                        generated_ui.UDISPLAY_EVENT_BUTTON_CLICK, None)
            u._on_event(generated_ui.WIDGET_ID_RATE,
                        generated_ui.UDISPLAY_EVENT_SLIDER_CHANGE, 2.5)
            assert clicks == ["up"] and rates == [2.5]
        finally:
            sys.path.remove(str(out))
            for mod in ("ui", "udisplay_runtime"):
                sys.modules.pop(mod, None)

    @pytest.mark.parametrize("name", ["set_property", "reset_property", "id"])
    def test_face_child_shadowing_widget_api_rejected(self, name):
        text = EVERY_WIDGET_YAML.replace("          status:\n", f"          {name}:\n")
        with pytest.raises(ValueError, match="reserved"):
            python_backend.generate(_ctx(text))

    def test_container_child_shadowing_widget_api_rejected(self):
        text = EVERY_WIDGET_YAML.replace("      amps:\n", "      set_property:\n")
        with pytest.raises(ValueError, match="reserved"):
            python_backend.generate(_ctx(text))

    def test_container_named_like_ui_method_rejected(self):
        text = EVERY_WIDGET_YAML.replace("  meters:\n", "  heartbeat:\n")
        with pytest.raises(ValueError, match="reserved"):
            python_backend.generate(_ctx(text))


# ── Coverage-audit additions ─────────────────────────────────────────────────

class TestCSetterParamFix:
    def test_setters_pass_their_own_declared_parameter(self, tmp_path):
        """Regression: non-`v` setters (rgbled `rgb`, dropdown `index`) used
        to forward an undeclared `v`. Text check so it fails fast even where
        the gcc compile test is skipped."""
        header = (_build(tmp_path, "c") / "udisplay_ui.h").read_text()
        assert ("set_power_btn_status(udisplay_t* ctx, int32_t rgb) "
                "{ udisplay_send_int(ctx, WIDGET_ID_POWER_BTN_STATUS, rgb); }") in header
        assert ("set_net(udisplay_t* ctx, uint8_t index) "
                "{ udisplay_send_uint8(ctx, WIDGET_ID_NET, index); }") in header
        assert "udisplay_send_float(ctx, WIDGET_ID_AMPS, v);" in header


class TestReservedNamesExtra:
    @pytest.mark.parametrize("name", ["id", "set_property"])
    def test_cpp_button_group_item_shadowing_widget_api_rejected(self, name):
        text = EVERY_WIDGET_YAML.replace("      ac:\n", f"      {name}:\n")
        with pytest.raises(ValueError, match=f"'{name}' collides"):
            cpp_backend.generate(_ctx(text))

    @pytest.mark.parametrize("name", ["id", "reset_property"])
    def test_python_button_group_item_shadowing_widget_api_rejected(self, name):
        text = EVERY_WIDGET_YAML.replace("      ac:\n", f"      {name}:\n")
        with pytest.raises(ValueError, match="reserved"):
            python_backend.generate(_ctx(text))

    @pytest.mark.parametrize("name", ["set_property", "reset_property"])
    def test_python_container_named_like_ui_property_api_rejected(self, name):
        """UI itself exposes set_property/reset_property (issue #43), so a
        container member of that name would shadow them."""
        text = EVERY_WIDGET_YAML.replace("  meters:\n", f"  {name}:\n")
        with pytest.raises(ValueError, match="reserved"):
            python_backend.generate(_ctx(text))


class TestProtoVersionInSync:
    def test_runtime_header_and_vectors_agree_on_proto_version(self):
        """PROTO_VERSION 0x05 = ID scheme v5 — the MicroPython runtime, the
        C header and the golden HANDSHAKE vector must all agree."""
        import json
        import re
        from udisplay_gen.runtime import udisplay_runtime as rt
        root = pathlib.Path(__file__).resolve().parents[2]
        header = (root / "libudisplay" / "include" / "libudisplay" / "udisplay.h").read_text()
        m = re.search(r"#define UDISPLAY_PROTO_VERSION\s+0x([0-9A-Fa-f]+)u", header)
        assert m, "UDISPLAY_PROTO_VERSION not found in udisplay.h"
        vectors = json.loads((root / "tests" / "protocol_vectors.json").read_text())
        assert rt.UDISPLAY_PROTO_VERSION == int(m.group(1), 16) == 0x05
        assert vectors["messages"]["HANDSHAKE"]["input"]["proto_version"] == 0x05


# ── Ship coverage-audit additions ────────────────────────────────────────────

class TestShipAuditGaps:
    @pytest.mark.parametrize("old,new", [
        ("  meters:\n", "  class:\n"),   # container named like a keyword
        ("      sep:\n", "      pass:\n"),  # decoration named like a keyword
    ])
    def test_python_keyword_container_or_decoration_name_rejected(self, old, new):
        """Containers and decorations are UI members now (issue #43), so a
        keyword name would emit `self.class = ...` — a SyntaxError."""
        text = EVERY_WIDGET_YAML.replace(old, new)
        with pytest.raises(ValueError, match="Python keyword"):
            python_backend.generate(_ctx(text))

    def test_same_container_name_in_two_namespaced_sections_allowed(self):
        """Two containers sharing a name in different namespaced sections
        are distinct, down to their own children."""
        doc = {"widgets": {
            "a": {"type": "section", "namespace": True, "widgets": {
                "strip": {"type": "row", "widgets": {"x": {"type": "led"}}}}},
            "b": {"type": "section", "namespace": True, "widgets": {
                "strip": {"type": "grid", "columns": 2,
                          "widgets": {"x": {"type": "led"}}}}},
        }}
        assert semantic_errors(doc) == []
        ids = assign(doc["widgets"])
        assert {"a.strip", "a.strip.x", "b.strip", "b.strip.x"} <= set(ids)

    def test_cap_counts_decorations(self):
        """Decorations consume an ID slot too — 240 leds + 1 label overflow."""
        widgets = {f"w{i:03d}": {"type": "led"} for i in range(MAX_WIDGETS)}
        assert len(assign(widgets)) == MAX_WIDGETS
        widgets["zz_note"] = {"type": "label", "text": "x"}
        with pytest.raises(ValueError, match="exceeds maximum"):
            assign(widgets)
