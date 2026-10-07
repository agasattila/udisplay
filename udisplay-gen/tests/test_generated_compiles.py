"""Compile smoke test for generated C / C++ headers.

Every other codegen test greps the generated text; none compiled it, which
is how the C setter bodies shipped referencing an undeclared `v` for rgbled
(`int32_t rgb`) and dropdown (`uint8_t index`). Generates a YAML that uses
every setter-bearing type (including button-group's set/clear), inside
transparent and namespace containers (a row, `namespace: true` sections, two
dpads reusing button keys), and runs the host compiler in syntax-only mode
against libudisplay's public headers.
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

CPP_USAGE = """\
#include "udisplay_ui.hpp"
using namespace udisplay_ui;
void use(UDisplay& ui) {
    ui.mode_sel.set(ModeSelWidget::Item::slow);
    ui.mode_sel.clear();
    ui.wifi_mode.set(WifiModeWidget::Option::ap);
    ui.controls.set_property(UDISPLAY_PROP_VISIBLE, 0);
    ui.indoor.temp.set(21.5f);
    ui.outdoor.temp.set(4.0f);
    ui.nav_a.up.on_click = []() {};
    ui.nav_b.up.on_click = []() {};
}
"""


def _build(tmp_path, lang):
    src = tmp_path / "smoke.yaml"
    src.write_text(YAML)
    out = tmp_path / lang
    result = CliRunner().invoke(cli, ["build", str(src), "--lang", lang, "-o", str(out)])
    assert result.exit_code == 0, result.output
    return out


def _compile(compiler, args, cwd):
    proc = subprocess.run([compiler, *args], cwd=cwd, capture_output=True, text=True)
    assert proc.returncode == 0, proc.stderr


@pytest.mark.skipif(shutil.which("cc") is None, reason="no C compiler")
def test_generated_c_header_compiles(tmp_path):
    out = _build(tmp_path, "c")
    (out / "use.c").write_text(C_USAGE)
    _compile("cc", ["-std=c99", "-Wall", "-Werror", "-fsyntax-only",
                    f"-I{LIB_INCLUDE}", "-I.", "use.c"], out)


@pytest.mark.skipif(shutil.which("c++") is None, reason="no C++ compiler")
def test_generated_cpp_header_compiles(tmp_path):
    out = _build(tmp_path, "cpp")
    (out / "use.cpp").write_text(CPP_USAGE)
    _compile("c++", ["-std=c++11", "-Wall", "-Werror", "-fsyntax-only",
                     f"-I{LIB_INCLUDE}", "-I.", "use.cpp"], out)
