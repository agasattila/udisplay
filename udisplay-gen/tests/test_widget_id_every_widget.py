"""Issue #43: every widget gets a widget ID — containers (section/row/grid/
dpad) and decorations (label/separator) included — so firmware can target any
node with SET_PROPERTY / RESET_PROPERTY. Covers ID assignment, validation,
and every backend's generated API (docs/designs/widget-id-for-every-widget.md).
"""
import pathlib
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

EXPECTED_PATHS = {
    "panel", "title", "power_btn", "power_btn.face_row", "power_btn.icon",
    "power_btn.caption", "power_btn.status", "meters", "amps", "rate",
    "zone", "relay", "sep", "net", "ssid", "pad", "up_btn", "down_btn",
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

    def test_containers_transparent_to_children_paths(self):
        ids = assign(_widgets())
        assert not any(p.startswith(("panel.", "meters.", "zone.", "pad.")) for p in ids)
        assert "power_btn.icon" in ids and "power_btn.face_row.icon" not in ids

    def test_types_reported_for_containers_and_decorations(self):
        types = collect_types(_widgets())
        assert types["panel"] == "section"
        assert types["meters"] == "row"
        assert types["zone"] == "grid"
        assert types["pad"] == "dpad"
        assert types["title"] == "label"
        assert types["sep"] == "separator"
        assert types["power_btn.face_row"] == "row"
        assert types["power_btn.caption"] == "label"
        assert set(types) == EXPECTED_PATHS

    def test_dropdown_items_still_get_no_id(self):
        ids = assign({"dd": {"type": "dropdown", "items": {"a": "A", "b": "B"}}})
        assert ids == {"dd": 0x10}

    def test_container_name_colliding_with_leaf_raises(self):
        """A section named like a slider in another section resolves to the
        same path — must raise, not silently share one ID."""
        widgets = {
            "advanced": {"type": "section", "widgets": {"x": {"type": "toggle"}}},
            "basic": {"type": "section", "widgets": {"advanced": {"type": "slider"}}},
        }
        with pytest.raises(ValueError, match="'advanced'"):
            assign(widgets)

    def test_label_name_colliding_across_sections_raises(self):
        widgets = {
            "a": {"type": "section", "widgets": {"hdr": {"type": "label", "text": "A"}}},
            "b": {"type": "section", "widgets": {"hdr": {"type": "label", "text": "B"}}},
        }
        with pytest.raises(ValueError, match="'hdr'"):
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

    def test_container_name_colliding_with_leaf_rejected(self):
        doc = {"widgets": {
            "advanced": {"type": "section", "widgets": {"x": {"type": "toggle"}}},
            "basic": {"type": "section", "widgets": {"advanced": {"type": "slider",
                                                                  "min": 0, "max": 1}}},
        }}
        errs = semantic_errors(doc)
        assert any("duplicate widget name `advanced`" in e for e in errs), errs

    def test_label_name_colliding_with_leaf_rejected(self):
        doc = {"widgets": {
            "row1": {"type": "row", "widgets": {"volt": {"type": "label", "text": "V"}}},
            "volt": {"type": "display"},
        }}
        errs = semantic_errors(doc)
        assert any("duplicate widget name `volt`" in e for e in errs), errs

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
                      "WIDGET_ID_TITLE", "WIDGET_ID_SEP", "WIDGET_ID_POWER_BTN_FACE_ROW",
                      "WIDGET_ID_POWER_BTN_CAPTION"):
            assert f"#define {macro} " in header, macro

    def test_no_typed_api_for_containers_or_decorations(self, tmp_path):
        header = (_build(tmp_path, "c") / "udisplay_ui.h").read_text()
        for name in ("panel", "meters", "zone", "pad", "title", "sep"):
            assert f"set_{name}(" not in header
            assert f"on_{name}_" not in header


# ── C++ backend ──────────────────────────────────────────────────────────────

class TestCppBackend:
    def _header(self, tmp_path, extra_args=()):
        return (_build(tmp_path, "cpp", extra_args=extra_args) / "udisplay_ui.hpp").read_text()

    def test_containers_and_decorations_are_widget_members(self, tmp_path):
        header = self._header(tmp_path)
        for name in ("panel", "title", "meters", "zone", "sep", "pad"):
            assert f"    Widget {name};" in header, name

    def test_top_level_dpad_children_are_members(self, tmp_path):
        """Regression (A2): _cpp_ordered_toplevel's local container set
        lacked "dpad", so a top-level dpad's buttons were never members."""
        header = self._header(tmp_path)
        assert "    ButtonWidget up_btn;" in header
        assert "    ButtonWidget down_btn;" in header

    def test_face_sub_members_typed_by_their_own_type(self, tmp_path):
        """Regression (A3): every face child used to be emitted as LedWidget."""
        header = self._header(tmp_path)
        assert "    LedWidget icon;" in header
        assert "    RgbLedWidget status;" in header
        assert "    Widget caption;" in header
        assert "    Widget face_row;" in header
        assert "LedWidget caption;" not in header

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
            "    ui.power_btn.caption.reset_property(UDISPLAY_PROP_VISIBLE);\n"
            "    ui.up_btn.set_property(UDISPLAY_PROP_ENABLED, ui.pad.id());\n"
            "}\n")
        self._check("g++", tu, "-std=c++17")


# ── MicroPython backend ──────────────────────────────────────────────────────

class TestPythonBackend:
    def test_containers_and_decorations_are_widget_members(self, tmp_path):
        ui_py = (_build(tmp_path, "micropython") / "ui.py").read_text()
        for name in ("panel", "title", "meters", "zone", "sep", "pad"):
            assert f"self.{name} = Widget(self._device, WIDGET_ID_{name.upper()})" in ui_py
        assert "self.power_btn.caption = Widget(" in ui_py
        assert "self.power_btn.icon = LedWidget(" in ui_py
        assert "self.up_btn = ButtonWidget(" in ui_py

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
                bytes([0x33, generated_ui.WIDGET_ID_POWER_BTN_CAPTION, UDISPLAY_PROP_VISIBLE]),
                bytes([0x32, generated_ui.WIDGET_ID_RELAY, UDISPLAY_PROP_ENABLED, 0]),
            ]
            assert u.pad.id == generated_ui.WIDGET_ID_PAD
        finally:
            sys.path.remove(str(out))
            for mod in ("ui", "udisplay_runtime"):
                sys.modules.pop(mod, None)

    @pytest.mark.parametrize("name", ["set_property", "reset_property", "id"])
    def test_face_child_shadowing_widget_api_rejected(self, name):
        text = EVERY_WIDGET_YAML.replace("          status:\n", f"          {name}:\n")
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
        header = (root / "libudisplay" / "include" / "udisplay.h").read_text()
        m = re.search(r"#define UDISPLAY_PROTO_VERSION\s+0x([0-9A-Fa-f]+)u", header)
        assert m, "UDISPLAY_PROTO_VERSION not found in udisplay.h"
        vectors = json.loads((root / "tests" / "protocol_vectors.json").read_text())
        assert rt.UDISPLAY_PROTO_VERSION == int(m.group(1), 16) == 0x05
        assert vectors["messages"]["HANDSHAKE"]["input"]["proto_version"] == 0x05
