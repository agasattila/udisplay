"""Tests for udisplay_gen.backends.python_backend (the MicroPython codegen
backend, docs/designs/micropython-backend.md)."""
import pathlib
import sys

import pytest
import yaml as pyyaml
from click.testing import CliRunner

from udisplay_gen.backends import BuildContext
from udisplay_gen.backends import c_backend, python_backend
from udisplay_gen.cli import cli
from udisplay_gen.merkle import compute
from udisplay_gen.widget_ids import assign, collect_types


def _load_types_and_ids(yaml_path):
    yaml_bytes = yaml_path.read_bytes()
    doc = pyyaml.safe_load(yaml_bytes)
    return collect_types(doc["widgets"]), assign(doc["widgets"])


def _make_ctx(yaml_path, **extra):
    yaml_bytes = yaml_path.read_bytes()
    blob, root, hashes = compute(yaml_bytes)
    wtypes, wids = _load_types_and_ids(yaml_path)
    doc = pyyaml.safe_load(yaml_bytes)
    return BuildContext(
        widget_ids=wids, blob=blob, root=root, hashes=hashes, source="test.yaml",
        widget_types=wtypes, widgets_yaml=doc["widgets"], **extra,
    )


class TestBackendContract:
    def test_generate_returns_two_files(self, minimal_yaml):
        ctx = _make_ctx(minimal_yaml)
        files = python_backend.generate(ctx)
        names = [f.name for f in files]
        assert names == ["ui.py", "udisplay_runtime.py"]

    def test_both_outputs_are_str(self, minimal_yaml):
        ctx = _make_ctx(minimal_yaml)
        for f in python_backend.generate(ctx):
            assert isinstance(f.content, str)

    def test_runtime_output_matches_source_file_verbatim(self, minimal_yaml):
        ctx = _make_ctx(minimal_yaml)
        files = python_backend.generate(ctx)
        runtime_file = next(f for f in files if f.name == "udisplay_runtime.py")
        source_path = (
            pathlib.Path(__file__).resolve().parents[1]
            / "udisplay_gen" / "runtime" / "udisplay_runtime.py"
        )
        assert runtime_file.content == source_path.read_text()

    def test_namespace_rejected(self, minimal_yaml):
        ctx = _make_ctx(minimal_yaml, namespace="ble")
        with pytest.raises(ValueError, match="--namespace"):
            python_backend.generate(ctx)

    def test_generated_ui_py_is_valid_python(self, full_vocab_yaml):
        ctx = _make_ctx(full_vocab_yaml)
        files = python_backend.generate(ctx)
        ui_file = next(f for f in files if f.name == "ui.py")
        compile(ui_file.content, "ui.py", "exec")  # raises SyntaxError if invalid

    def test_bin_bytes_identical_to_c_backend(self, full_vocab_yaml, tmp_path):
        """merkle.py's compute() is shared across all 3 backends -- the
        embedded blob content (just formatted differently per language)
        must be byte-identical."""
        ctx_py = _make_ctx(full_vocab_yaml)
        ctx_c = BuildContext(
            widget_ids=ctx_py.widget_ids, blob=ctx_py.blob, root=ctx_py.root,
            hashes=ctx_py.hashes, source="test.yaml",
        )
        c_bin = next(f for f in c_backend.generate(ctx_c) if f.name.endswith(".bin"))

        for f in python_backend.generate(ctx_py):
            (tmp_path / f.name).write_text(f.content)
        sys.path.insert(0, str(tmp_path))
        try:
            sys.modules.pop("ui", None)
            sys.modules.pop("udisplay_runtime", None)
            import ui as generated_ui
            reconstructed = b"".join(generated_ui.CHUNKS)
        finally:
            sys.path.remove(str(tmp_path))
            sys.modules.pop("ui", None)
            sys.modules.pop("udisplay_runtime", None)
        assert reconstructed == c_bin.content


class TestGeneratedContent:
    def test_widget_id_constants_present(self, full_vocab_yaml):
        ctx = _make_ctx(full_vocab_yaml)
        ui_py = next(f for f in python_backend.generate(ctx) if f.name == "ui.py").content
        assert "WIDGET_ID_TOGGLE_RELAY" in ui_py
        assert "WIDGET_ID_FIRE_BTN" in ui_py
        assert "WIDGET_ID_FIRE_BTN_STATUS_LED" in ui_py

    def test_button_child_nested_as_attribute(self, full_vocab_yaml):
        ctx = _make_ctx(full_vocab_yaml)
        ui_py = next(f for f in python_backend.generate(ctx) if f.name == "ui.py").content
        assert "self.fire_btn.status_led = LedWidget" in ui_py

    def test_button_group_items_nested_as_attributes(self, full_vocab_yaml):
        ctx = _make_ctx(full_vocab_yaml)
        ui_py = next(f for f in python_backend.generate(ctx) if f.name == "ui.py").content
        assert "self.mode_sel.ac = ButtonItem" in ui_py
        assert "self.mode_sel.dc = ButtonItem" in ui_py

    def test_dispatch_routes_to_member_widgets(self, full_vocab_yaml):
        ctx = _make_ctx(full_vocab_yaml)
        ui_py = next(f for f in python_backend.generate(ctx) if f.name == "ui.py").content
        assert "self.toggle_relay.on_change" in ui_py
        assert "self.slider_rate.on_change" in ui_py
        assert "self.fire_btn.on_press" in ui_py

    def test_dispatch_button_group_items(self, full_vocab_yaml):
        ctx = _make_ctx(full_vocab_yaml)
        ui_py = next(f for f in python_backend.generate(ctx) if f.name == "ui.py").content
        assert "self.mode_sel.ac.on_press" in ui_py
        assert "self.mode_sel.dc.on_press" in ui_py

    def test_dropdown_option_constants(self, dropdown_yaml):
        yaml_bytes = dropdown_yaml.read_bytes()
        blob, root, hashes = compute(yaml_bytes)
        wtypes, wids = _load_types_and_ids(dropdown_yaml)
        doc = pyyaml.safe_load(yaml_bytes)
        ctx = BuildContext(widget_ids=wids, blob=blob, root=root, hashes=hashes,
                            source="test.yaml", widget_types=wtypes, widgets_yaml=doc["widgets"])
        ui_py = next(f for f in python_backend.generate(ctx) if f.name == "ui.py").content
        assert "WIFI_MODE_OPTION_STA = 0" in ui_py
        assert "WIFI_MODE_OPTION_AP = 1" in ui_py
        assert "WIFI_MODE_OPTION_DISABLED = 3" in ui_py

    def test_no_setter_for_button(self, full_vocab_yaml):
        """button has no .set() call generated for it (matches c/cpp: no
        setter exists for the widget type)."""
        ctx = _make_ctx(full_vocab_yaml)
        ui_py = next(f for f in python_backend.generate(ctx) if f.name == "ui.py").content
        assert "ButtonWidget(self._device, WIDGET_ID_FIRE_BTN)" in ui_py

    def test_output_ownership_comment_present(self, minimal_yaml):
        """Generated file must warn against hand-editing (output-ownership
        constraint from the eng review)."""
        ctx = _make_ctx(minimal_yaml)
        ui_py = next(f for f in python_backend.generate(ctx) if f.name == "ui.py").content
        assert "DO NOT EDIT" in ui_py
        assert "main.py" in ui_py


class TestWrapperClassSelection:
    """The _WRAPPER_CLASS mapping (display/led/rgbled/toggle/slider/
    text-rw/dropdown/button) was only ever exercised indirectly -- no
    existing test asserted which wrapper class gets instantiated for the
    non-button types. A wrong lookup here (e.g. rgbled silently falling
    back to ButtonWidget's default) would compile fine and only misbehave
    at runtime."""

    def test_each_leaf_type_gets_its_own_wrapper_class(self, full_vocab_yaml):
        ctx = _make_ctx(full_vocab_yaml)
        ui_py = next(f for f in python_backend.generate(ctx) if f.name == "ui.py").content
        assert "self.slider_rate = SliderWidget(self._device, WIDGET_ID_SLIDER_RATE)" in ui_py
        assert "self.toggle_relay = ToggleWidget(self._device, WIDGET_ID_TOGGLE_RELAY)" in ui_py
        assert "self.display_volt = DisplayWidget(self._device, WIDGET_ID_DISPLAY_VOLT)" in ui_py
        assert "self.power_led = LedWidget(self._device, WIDGET_ID_POWER_LED)" in ui_py
        assert "self.status_rgb = RgbLedWidget(self._device, WIDGET_ID_STATUS_RGB)" in ui_py
        assert "self.ssid_field = TextRwWidget(self._device, WIDGET_ID_SSID_FIELD)" in ui_py


class TestGeneratedOutputEndToEndOtherEventTypes:
    """test_generated_output_actually_imports_and_runs (below) only drives a
    BUTTON_CLICK through the generated dispatch. The elif-chain in
    _py_dispatch_cases has separate branches per widget type (toggle,
    slider, text-rw, dropdown, ...) that a button-only round trip cannot
    catch a mis-ordering bug in."""

    def test_toggle_and_slider_events_reach_their_on_change_handlers(self, full_vocab_yaml, tmp_path):
        runner = CliRunner()
        result = runner.invoke(cli, ["build", str(full_vocab_yaml), "-o", str(tmp_path),
                                      "--lang", "micropython"])
        assert result.exit_code == 0, result.output

        sys.path.insert(0, str(tmp_path))
        try:
            for mod in ("ui", "udisplay_runtime"):
                sys.modules.pop(mod, None)
            import ui as generated_ui
            from udisplay_runtime import tcp_frame, MSG_CLIENT_READY, MSG_EVENT
            from udisplay_runtime import UDISPLAY_EVENT_TOGGLE_CHANGE, UDISPLAY_EVENT_SLIDER_CHANGE
            import struct

            sent = []
            u = generated_ui.UI(send=sent.append)
            u.on_connect()
            u.feed(tcp_frame(bytes([MSG_CLIENT_READY])))

            toggled = []
            u.toggle_relay.on_change = lambda v: toggled.append(v)
            u.feed(tcp_frame(bytes([MSG_EVENT, generated_ui.WIDGET_ID_TOGGLE_RELAY,
                                     UDISPLAY_EVENT_TOGGLE_CHANGE, 0x01])))
            assert toggled == [1]

            slid = []
            u.slider_rate.on_change = lambda v: slid.append(v)
            payload = (bytes([MSG_EVENT, generated_ui.WIDGET_ID_SLIDER_RATE, UDISPLAY_EVENT_SLIDER_CHANGE])
                       + struct.pack("<f", 42.0))
            u.feed(tcp_frame(payload))
            assert len(slid) == 1 and abs(slid[0] - 42.0) < 1e-4
        finally:
            sys.path.remove(str(tmp_path))
            for mod in ("ui", "udisplay_runtime"):
                sys.modules.pop(mod, None)


class TestCliIntegration:
    def test_cli_lang_micropython_produces_two_files(self, minimal_yaml, tmp_path):
        runner = CliRunner()
        result = runner.invoke(cli, ["build", str(minimal_yaml), "-o", str(tmp_path),
                                      "--lang", "micropython"])
        assert result.exit_code == 0, result.output
        assert (tmp_path / "ui.py").exists()
        assert (tmp_path / "udisplay_runtime.py").exists()
        assert not (tmp_path / "udisplay_ui.h").exists()

    def test_cli_lang_micropython_namespace_rejected(self, minimal_yaml, tmp_path):
        runner = CliRunner()
        result = runner.invoke(cli, ["build", str(minimal_yaml), "-o", str(tmp_path),
                                      "--lang", "micropython", "--namespace", "ble"])
        assert result.exit_code != 0
        assert "--namespace" in result.output

    def test_cli_full_vocab_micropython(self, full_vocab_yaml, tmp_path):
        runner = CliRunner()
        result = runner.invoke(cli, ["build", str(full_vocab_yaml), "-o", str(tmp_path),
                                      "--lang", "micropython"])
        assert result.exit_code == 0, result.output
        ui_py = (tmp_path / "ui.py").read_text()
        assert "class UI:" in ui_py

    def test_generated_output_actually_imports_and_runs(self, full_vocab_yaml, tmp_path):
        """End-to-end: generate real output, import it, drive a full
        connect -> active -> event -> state-push round trip."""
        runner = CliRunner()
        result = runner.invoke(cli, ["build", str(full_vocab_yaml), "-o", str(tmp_path),
                                      "--lang", "micropython"])
        assert result.exit_code == 0, result.output

        sys.path.insert(0, str(tmp_path))
        try:
            for mod in ("ui", "udisplay_runtime"):
                sys.modules.pop(mod, None)
            import ui as generated_ui
            from udisplay_runtime import tcp_frame, tcp_unframe, MSG_CLIENT_READY

            sent = []
            u = generated_ui.UI(send=sent.append)
            u.on_connect()
            assert len(sent) == 1
            payload, _ = tcp_unframe(sent[0])
            assert payload[0] == 0x00  # HANDSHAKE

            sent.clear()
            u.feed(tcp_frame(bytes([MSG_CLIENT_READY])))
            assert u._device.active is True

            clicks = []
            u.fire_btn.on_click = lambda: clicks.append(1)
            from udisplay_runtime import MSG_EVENT, UDISPLAY_EVENT_BUTTON_CLICK
            u.feed(tcp_frame(bytes([MSG_EVENT, generated_ui.WIDGET_ID_FIRE_BTN, UDISPLAY_EVENT_BUTTON_CLICK])))
            assert clicks == [1]
        finally:
            sys.path.remove(str(tmp_path))
            for mod in ("ui", "udisplay_runtime"):
                sys.modules.pop(mod, None)


def _ctx_from_yaml_text(tmp_path, yaml_text: str) -> BuildContext:
    p = tmp_path / "gen.yaml"
    p.write_text(yaml_text)
    return _make_ctx(p)


class TestIdentifierValidation:
    """TODO-056/TODO-057, per PR10 review: a schema-valid YAML must not be
    able to produce invalid or ambiguously generated Python code."""

    def test_python_keyword_widget_name_rejected(self, tmp_path):
        ctx = _ctx_from_yaml_text(tmp_path, """
widgets:
  class:
    type: toggle
""")
        with pytest.raises(ValueError, match="keyword"):
            python_backend.generate(ctx)

    def test_reserved_ui_api_name_rejected(self, tmp_path):
        ctx = _ctx_from_yaml_text(tmp_path, """
widgets:
  feed:
    type: toggle
""")
        with pytest.raises(ValueError, match="reserved"):
            python_backend.generate(ctx)

    def test_reserved_ui_callback_slot_name_rejected(self, tmp_path):
        """on_client_ready is a settable callback attribute on UI, not just
        a method -- a widget of the same name silently replaces the slot
        itself, not just shadowing a method."""
        ctx = _ctx_from_yaml_text(tmp_path, """
widgets:
  on_client_ready:
    type: toggle
""")
        with pytest.raises(ValueError, match="reserved"):
            python_backend.generate(ctx)

    def test_button_face_child_reserved_name_rejected(self, tmp_path):
        """A button face child named on_press collides with ButtonWidget's
        own callback attribute -- ButtonWidget has no __slots__, so this
        assignment would otherwise succeed silently."""
        ctx = _ctx_from_yaml_text(tmp_path, """
widgets:
  fire_btn:
    type: button
    widgets:
      on_press:
        type: led
""")
        with pytest.raises(ValueError, match="reserved"):
            python_backend.generate(ctx)

    def test_button_group_item_reserved_name_rejected(self, tmp_path):
        ctx = _ctx_from_yaml_text(tmp_path, """
widgets:
  mode_sel:
    type: button-group
    items:
      _widget_id:
        label: Bad
      ok:
        label: OK
""")
        with pytest.raises(ValueError, match="reserved"):
            python_backend.generate(ctx)

    def test_macro_name_collision_rejected(self, tmp_path):
        """pump_rate and pump__rate both normalize to WIDGET_ID_PUMP_RATE —
        must be rejected, not silently let one definition win."""
        ctx = _ctx_from_yaml_text(tmp_path, """
widgets:
  pump_rate:
    type: toggle
  pump__rate:
    type: toggle
""")
        with pytest.raises(ValueError, match="WIDGET_ID_PUMP_RATE"):
            python_backend.generate(ctx)

    def test_dropdown_option_macro_collision_rejected(self, tmp_path):
        """s_ta and s__ta (single vs. double underscore) both normalize to
        the same OPTION_S_TA constant -- _macro_name() collapses any RUN of
        non-alphanumeric characters to one underscore."""
        ctx = _ctx_from_yaml_text(tmp_path, """
widgets:
  net_sel:
    type: dropdown
    items:
      s_ta: Station
      s__ta: Also Station
""")
        with pytest.raises(ValueError, match="item collision"):
            python_backend.generate(ctx)

    def test_ordinary_names_pass_validation(self, tmp_path):
        """Sanity check: normal, non-colliding names generate without error."""
        ctx = _ctx_from_yaml_text(tmp_path, """
widgets:
  pump_rate:
    type: toggle
  fire_btn:
    type: button
    widgets:
      status_led:
        type: led
""")
        files = python_backend.generate(ctx)
        assert [f.name for f in files] == ["ui.py", "udisplay_runtime.py"]

    def test_error_message_lists_multiple_violations(self, tmp_path):
        """Every violation should be reported in one pass, not just the
        first -- a single fix-and-rerun cycle should catch everything."""
        ctx = _ctx_from_yaml_text(tmp_path, """
widgets:
  class:
    type: toggle
  feed:
    type: toggle
""")
        with pytest.raises(ValueError) as exc_info:
            python_backend.generate(ctx)
        message = str(exc_info.value)
        assert "class" in message
        assert "feed" in message
