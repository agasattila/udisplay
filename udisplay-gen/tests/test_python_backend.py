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
