"""
End-to-end integration tests for the multi-instance --namespace flag.

Unlike test_build.py (string-content assertions on generated files), these
tests shell out to a real C/C++ compiler and prove the actual motivating
capability named in the design doc (prog-main-design-20260730-192038.md,
Success Criteria): two independently namespaced *generated* outputs compile,
link, and run together in one binary -- not just parser-level isolation on
hand-written fixtures, and not just string assertions on one header (which
can't catch a real duplicate-symbol linker collision).

Skipped entirely if no C/C++ compiler is available on PATH.
"""
from __future__ import annotations

import pathlib
import shutil
import subprocess

import pytest
from click.testing import CliRunner

from udisplay_gen.cli import cli

REPO_ROOT = pathlib.Path(__file__).parent.parent.parent
LIBUDISPLAY_SRC = REPO_ROOT / "libudisplay" / "src"
LIBUDISPLAY_INCLUDE = REPO_ROOT / "libudisplay" / "include"

CC = shutil.which("cc") or shutil.which("gcc") or shutil.which("clang")
CXX = shutil.which("c++") or shutil.which("g++") or shutil.which("clang++")

pytestmark = pytest.mark.skipif(
    CC is None or CXX is None,
    reason="no C/C++ compiler found on PATH -- skipping compile/link integration tests",
)


def _write_version_header(tmp_path: pathlib.Path) -> pathlib.Path:
    """Stub udisplay/version.h (normally produced by CMake configure_file())."""
    ver_dir = tmp_path / "generated" / "udisplay"
    ver_dir.mkdir(parents=True, exist_ok=True)
    (ver_dir / "version.h").write_text(
        '#pragma once\n#define UDISPLAY_VERSION_STRING "test"\n'
    )
    return tmp_path / "generated"


def _build_two_namespaces(tmp_path: pathlib.Path, lang: str) -> pathlib.Path:
    """Generate the SAME full-vocab YAML under two different --namespace
    values into one output directory, proving the namespace flag alone
    (not different widget catalogs) is what prevents collision."""
    yaml_path = tmp_path / "full.yaml"
    yaml_path.write_text(
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
    )
    out = tmp_path / "gen"
    out.mkdir()
    runner = CliRunner()
    for ns in ("ble", "wifi"):
        ns_out = out / ns
        ns_out.mkdir()
        result = runner.invoke(
            cli,
            ["build", str(yaml_path), "-o", str(ns_out), "--lang", lang, "--namespace", ns],
        )
        assert result.exit_code == 0, result.output
    return out


# ── C backend: two namespaced outputs compile + link together ────────────────

def test_c_two_namespaces_compile_link_and_run(tmp_path):
    out = _build_two_namespaces(tmp_path, "c")
    gen_include = _write_version_header(tmp_path)

    main_c = tmp_path / "main.c"
    main_c.write_text(
        """
#include <stdint.h>
#include <stdio.h>
#include "udisplay.h"
#include "ble/udisplay_ui.h"
#include "wifi/udisplay_ui.h"

static int g_ble_sent = 0;
static int g_wifi_sent = 0;

static void ble_send(const uint8_t* data, uint16_t len, void* ud) { (void)data; (void)len; (void)ud; g_ble_sent++; }
static void wifi_send(const uint8_t* data, uint16_t len, void* ud) { (void)data; (void)len; (void)ud; g_wifi_sent++; }

static udisplay_t g_ble_ctx;
static udisplay_t g_wifi_ctx;

int main(void) {
    udisplay_ble_ui_init(&g_ble_ctx, ble_send, UDISPLAY_TRANSPORT_NONE);
    udisplay_wifi_ui_init(&g_wifi_ctx, wifi_send, UDISPLAY_TRANSPORT_NONE);

    udisplay_on_connect(&g_ble_ctx);
    udisplay_on_connect(&g_wifi_ctx);
    if (g_ble_sent == 0)  { fprintf(stderr, "ble ctx never sent HANDSHAKE\\n"); return 1; }
    if (g_wifi_sent == 0) { fprintf(stderr, "wifi ctx never sent HANDSHAKE\\n"); return 1; }

    /* Prove independence: heartbeat on one must not touch the other's state. */
    int before_wifi = g_wifi_sent;
    udisplay_heartbeat(&g_ble_ctx);
    if (g_wifi_sent != before_wifi) { fprintf(stderr, "ble heartbeat leaked into wifi ctx\\n"); return 1; }

    printf("OK: ble_sent=%d wifi_sent=%d\\n", g_ble_sent, g_wifi_sent);
    return 0;
}
"""
    )

    binary = tmp_path / "test_bin"
    cmd = [
        CC, "-std=c11", "-Wall", "-Wextra",
        "-I", str(LIBUDISPLAY_INCLUDE),
        "-I", str(gen_include),
        "-I", str(out),
        str(main_c),
        str(out / "ble" / "udisplay_ui.c"),
        str(out / "wifi" / "udisplay_ui.c"),
        str(LIBUDISPLAY_SRC / "udisplay.c"),
        str(LIBUDISPLAY_SRC / "protocol.c"),
        str(LIBUDISPLAY_SRC / "chunk_server.c"),
        str(LIBUDISPLAY_SRC / "framing.c"),
        "-o", str(binary),
    ]
    result = subprocess.run(cmd, capture_output=True, text=True)
    assert result.returncode == 0, f"compile/link failed:\n{result.stdout}\n{result.stderr}"

    run_result = subprocess.run([str(binary)], capture_output=True, text=True)
    assert run_result.returncode == 0, f"binary failed at runtime:\n{run_result.stdout}\n{run_result.stderr}"
    assert "OK:" in run_result.stdout


# ── C++ backend: two namespaced outputs compile together in ONE translation unit ──

def test_cpp_two_namespaces_compile_in_one_tu_and_run(tmp_path):
    """C++ real namespaces mean both generated headers can be #included in
    the SAME .cpp file with zero manual symbol renaming -- this is the case
    that would fail with a duplicate 'class UDisplay' / ODR violation if the
    outer namespace weren't parameterized (see Design Decision, Next Steps #6)."""
    out = _build_two_namespaces(tmp_path, "cpp")
    gen_include = _write_version_header(tmp_path)

    main_cpp = tmp_path / "main.cpp"
    main_cpp.write_text(
        """
#include <cstdint>
#include <cstdio>
#include "udisplay.h"
#include "ble/udisplay_ui.hpp"
#include "wifi/udisplay_ui.hpp"

static int g_ble_sent = 0;
static int g_wifi_sent = 0;

static void ble_send(const uint8_t*, uint16_t, void*)  { g_ble_sent++; }
static void wifi_send(const uint8_t*, uint16_t, void*) { g_wifi_sent++; }

static udisplay_ble_ui::UDisplay g_ble_ui;
static udisplay_wifi_ui::UDisplay g_wifi_ui;

int main() {
    g_ble_ui.init(ble_send, UDISPLAY_TRANSPORT_NONE);
    g_wifi_ui.init(wifi_send, UDISPLAY_TRANSPORT_NONE);

    udisplay_on_connect(g_ble_ui.ctx());
    udisplay_on_connect(g_wifi_ui.ctx());
    if (g_ble_sent == 0)  { fprintf(stderr, "ble ui never sent HANDSHAKE\\n"); return 1; }
    if (g_wifi_sent == 0) { fprintf(stderr, "wifi ui never sent HANDSHAKE\\n"); return 1; }

    /* Smoke test: feed a message through .feed(), verify no crash and
     * independent instance state (ctx() returns each instance's own _ctx). */
    uint8_t client_ready = 0x02u;
    g_ble_ui.feed(&client_ready, 1u);

    printf("OK: ble_sent=%d wifi_sent=%d ctx_distinct=%d\\n",
           g_ble_sent, g_wifi_sent, g_ble_ui.ctx() != g_wifi_ui.ctx());
    return 0;
}
"""
    )

    # Compile the C library sources with the C compiler (not g++/clang++ --
    # mixed C/C++ projects always compile .c with the C frontend; CMake does
    # this automatically via per-file language detection, so mirror that here
    # rather than relying on g++'s "-x c++ for .c files" behavior).
    obj_dir = tmp_path / "obj"
    obj_dir.mkdir()
    c_objs = []
    for src_name in ("udisplay.c", "protocol.c", "chunk_server.c", "framing.c"):
        obj = obj_dir / (src_name + ".o")
        cc_cmd = [
            CC, "-std=c11", "-c",
            "-I", str(LIBUDISPLAY_INCLUDE),
            "-I", str(gen_include),
            str(LIBUDISPLAY_SRC / src_name),
            "-o", str(obj),
        ]
        cc_result = subprocess.run(cc_cmd, capture_output=True, text=True)
        assert cc_result.returncode == 0, f"C compile failed for {src_name}:\n{cc_result.stdout}\n{cc_result.stderr}"
        c_objs.append(str(obj))

    binary = tmp_path / "test_bin_cpp"
    cmd = [
        CXX, "-std=c++14", "-Wall", "-Wextra",
        "-I", str(LIBUDISPLAY_INCLUDE),
        "-I", str(gen_include),
        "-I", str(out),
        str(main_cpp),
        *c_objs,
        "-o", str(binary),
    ]
    result = subprocess.run(cmd, capture_output=True, text=True)
    assert result.returncode == 0, f"compile/link failed:\n{result.stdout}\n{result.stderr}"

    run_result = subprocess.run([str(binary)], capture_output=True, text=True)
    assert run_result.returncode == 0, f"binary failed at runtime:\n{run_result.stdout}\n{run_result.stderr}"
    assert "OK:" in run_result.stdout
    assert "ctx_distinct=1" in run_result.stdout
