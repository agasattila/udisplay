"""Compile smoke test for generated C / C++ headers.

Every other codegen test greps the generated text; none compiled it, which
is how the C setter bodies shipped referencing an undeclared `v` for rgbled
(`int32_t rgb`) and dropdown (`uint8_t index`). Generates a YAML that uses
every setter-bearing type (including button-group's set/clear), inside
transparent and namespace containers (a row, `namespace: true` sections, two
dpads reusing button keys), and runs the host compiler in syntax-only mode
against libudisplay's public headers. The default C++ variant is compiled as
C++98, C++11, C++17 and C++20; --modern as C++11 and C++20.
Skipped when no compiler is on PATH."""
import pathlib
import shutil
import subprocess

import pytest
from click.testing import CliRunner

from udisplay_gen.cli import cli

REPO_ROOT = pathlib.Path(__file__).parent.parent.parent
LIB_INCLUDE = REPO_ROOT / "libudisplay" / "include"

YAML = """\
device:
  name: compile smoke
widgets:
  volt:
    type: display
  power_led:
    type: led
  status_rgb:
    type: rgbled
  relay:
    type: toggle
  rate:
    type: slider
    min: 0
    max: 10
  ssid:
    type: text
    mode: rw
  banner:
    type: text
  wifi_mode:
    type: dropdown
    items:
      sta: Station
      ap: Access Point
  controls:
    type: row
    widgets:
      mode_sel:
        type: button-group
        items:
          fast:
            label: Fast
          slow:
            label: Slow
  indoor:
    type: section
    namespace: true
    widgets:
      temp:
        type: display
  outdoor:
    type: section
    namespace: true
    widgets:
      temp:
        type: display
  nav_a:
    type: dpad
    widgets:
      up:
        type: button
        position: top
  nav_b:
    type: dpad
    widgets:
      up:
        type: button
        position: top
"""

C_USAGE = """\
#include "udisplay_ui.h"
void use(udisplay_t* ctx) {
    set_status_rgb(ctx, 0x00FF00);
    set_wifi_mode(ctx, WIFI_MODE_AP);
    set_mode_sel(ctx, WIDGET_ID_MODE_SEL_SLOW);
    clear_mode_sel(ctx);
    set_indoor_temp(ctx, 21.5f);
    set_outdoor_temp(ctx, 4.0f);
    udisplay_set_property(ctx, WIDGET_ID_CONTROLS, UDISPLAY_PROP_VISIBLE, 0);
    udisplay_set_property(ctx, WIDGET_ID_NAV_B_UP, UDISPLAY_PROP_ENABLED, 0);
}

static void on_up(void) {}
static const udisplay_ui_handlers_t handlers = {
    .on_nav_a_up_click = on_up,
    .on_nav_b_up_click = on_up,
};
const udisplay_ui_handlers_t* get_handlers(void) { return &handlers; }
"""

# Default variant: C++98, so free functions instead of lambdas. Exercises
# the spec of issue #48: handler assignment, set(X::Item::name), variables
# of the X::Item::Value / X::Option::Value types, switch, ==, set()/clear().
CPP98_USAGE = """\
#include "udisplay_ui.hpp"
using namespace udisplay_ui;

static ModeSelWidget::Item::Value g_mode = ModeSelWidget::Item::slow;
static int g_rate = 0;

static void select_mode(UDisplay& ui, ModeSelWidget::Item::Value item) {
    g_mode = item;
    ui.mode_sel.set(item);
}
static void on_relay(bool state) { (void)state; }
static void on_up() {}
static void on_ready() {}
static void on_wifi(WifiModeWidget::Option::Value selection) {
    switch (selection) {
    case WifiModeWidget::Option::sta: g_rate = 1; break;
    case WifiModeWidget::Option::ap:  g_rate = 2; break;
    }
}

void use(UDisplay& ui) {
    select_mode(ui, ModeSelWidget::Item::fast);
    ui.mode_sel.set(ModeSelWidget::Item::slow);
    ui.mode_sel.clear();
    if (g_mode == ModeSelWidget::Item::fast) g_rate = 3;
    ui.wifi_mode.set(WifiModeWidget::Option::ap);
    ui.wifi_mode.on_change = on_wifi;
    ui.relay.on_change = on_relay;
    ui.relay.set(true);
    ui.status_rgb.set(0x00FF00u);
    ui.ssid.set("net", 3);
    ui.controls.set_property(UDISPLAY_PROP_VISIBLE, 0);
    ui.indoor.temp.set(21.5f);
    ui.outdoor.temp.set(4.0f);
    ui.nav_a.up.on_click = on_up;
    ui.nav_b.up.on_click = on_up;
    ui.mode_sel.fast.on_press = on_up;
    ui.on_client_ready = on_ready;
    ui.on_comms_error = 0;
}

UDisplay g_ui;
"""

CPP_MODERN_USAGE = """\
#include "udisplay_ui.hpp"
using namespace udisplay_ui;
void use(UDisplay& ui) {
    ModeSelWidget::Item mode = ModeSelWidget::Item::slow;
    ui.mode_sel.set(mode);
    ui.mode_sel.clear();
    ui.wifi_mode.set(WifiModeWidget::Option::ap);
    ui.wifi_mode.on_change = [](WifiModeWidget::Option) {};
    ui.controls.set_property(UDISPLAY_PROP_VISIBLE, 0);
    ui.indoor.temp.set(21.5f);
    ui.outdoor.temp.set(4.0f);
    ui.nav_a.up.on_click = []() {};
    ui.nav_b.up.on_click = []() {};
}
"""

# Each must fail to compile against the default variant: the C++98 forms
# keep the guarantees of enum class / `= delete` that matter.
CPP98_MUST_NOT_COMPILE = {
    "copy": "void f(UDisplay& ui) { UDisplay copy(ui); (void)copy; }",
    "assign": "void f(UDisplay& a, UDisplay& b) { a = b; }",
    "raw_int": "void f(UDisplay& ui) { ui.mode_sel.set(1); }",
    "other_enum": "void f(UDisplay& ui) { ui.mode_sel.set(WifiModeWidget::Option::ap); }",
    "old_type_spelling": "void f(UDisplay& ui) { ModeSelWidget::Item m = ModeSelWidget::Item::fast; ui.mode_sel.set(m); }",
}

WARNINGS = ["-pedantic-errors", "-Wall", "-Wextra", "-Werror"]


def _build(tmp_path, lang, *extra):
    src = tmp_path / "smoke.yaml"
    src.write_text(YAML)
    out = tmp_path / lang
    result = CliRunner().invoke(cli, ["build", str(src), "--lang", lang, *extra, "-o", str(out)])
    assert result.exit_code == 0, result.output
    return out


def _run(compiler, args, cwd):
    return subprocess.run([compiler, *args], cwd=cwd, capture_output=True, text=True)


def _compile(compiler, args, cwd):
    proc = _run(compiler, args, cwd)
    assert proc.returncode == 0, proc.stderr


def _require_std(std):
    """Skip (not fail) for a language standard the host compiler lacks."""
    proc = subprocess.run(["c++", f"-std={std}", "-x", "c++", "-fsyntax-only", "-"],
                          input="", capture_output=True, text=True)
    if proc.returncode != 0:
        pytest.skip(f"c++ does not accept -std={std}")


def _cpp_args(std, src):
    return [f"-std={std}", *WARNINGS, "-fsyntax-only", f"-I{LIB_INCLUDE}", "-I.", src]


@pytest.mark.skipif(shutil.which("cc") is None, reason="no C compiler")
def test_generated_c_header_compiles(tmp_path):
    out = _build(tmp_path, "c")
    (out / "use.c").write_text(C_USAGE)
    _compile("cc", ["-std=c99", "-Wall", "-Werror", "-fsyntax-only",
                    f"-I{LIB_INCLUDE}", "-I.", "use.c"], out)


@pytest.mark.skipif(shutil.which("c++") is None, reason="no C++ compiler")
@pytest.mark.parametrize("std", ["c++98", "c++11", "c++17", "c++20"])
def test_generated_cpp_header_compiles(tmp_path, std):
    """The default variant is C++98 (issue #48) and must keep compiling
    unchanged under every later standard."""
    _require_std(std)
    out = _build(tmp_path, "cpp")
    (out / "use.cpp").write_text(CPP98_USAGE)
    _compile("c++", _cpp_args(std, "use.cpp"), out)


@pytest.mark.skipif(shutil.which("c++") is None, reason="no C++ compiler")
@pytest.mark.parametrize("std", ["c++11", "c++20"])
def test_generated_cpp_modern_header_compiles(tmp_path, std):
    _require_std(std)
    out = _build(tmp_path, "cpp", "--modern")
    (out / "use.cpp").write_text(CPP_MODERN_USAGE)
    _compile("c++", _cpp_args(std, "use.cpp"), out)


@pytest.mark.skipif(shutil.which("c++") is None, reason="no C++ compiler")
@pytest.mark.parametrize("case", sorted(CPP98_MUST_NOT_COMPILE))
def test_generated_cpp98_rejects_misuse(tmp_path, case):
    out = _build(tmp_path, "cpp")
    (out / "bad.cpp").write_text(
        '#include "udisplay_ui.hpp"\nusing namespace udisplay_ui;\n'
        + CPP98_MUST_NOT_COMPILE[case] + "\n"
    )
    proc = _run("c++", _cpp_args("c++98", "bad.cpp"), out)
    assert proc.returncode != 0, f"{case} compiled, but must not"
    # Fails on the misuse itself, not on some unrelated header error.
    assert "bad.cpp:3" in proc.stderr, proc.stderr
