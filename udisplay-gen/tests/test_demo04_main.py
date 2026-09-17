"""Tests for demos/demo04/main.py -- the MicroPython-backend demo TCP
server (docs/designs/micropython-backend.md).

This file had ZERO test coverage prior to this audit pass (no demo in this
repo has ever been unit tested, but demo04 is new production-shaped code
for this feature, not a throwaway script: it contains real branchy logic
-- CPython/MicroPython errno-shape detection in _recv_or_none, rate
clamping, and the per-tick simulation state machine -- that the
udisplay_runtime.py / python_backend.py test suites cannot exercise since
it lives entirely in this file.

Scope: the pure handler/helper logic (_recv_or_none, _make_ui's closures,
_temp_display_update) is unit-tested directly. The socket accept()/listen()
loop in run() and the blocking recv() polling loop in
_serve_one_connection() are integration-shaped (real sockets, real
timing) and are out of scope for this pass -- flagged as a remaining gap,
not silently skipped.
"""
import errno
import importlib.util
import pathlib
import sys
import uuid

import pytest
from click.testing import CliRunner

from udisplay_gen.cli import cli

REPO_ROOT = pathlib.Path(__file__).parent.parent.parent
DEMO04_DIR = REPO_ROOT / "demos" / "demo04"
DEMO04_YAML = DEMO04_DIR / "demo04.yaml"
DEMO04_MAIN = DEMO04_DIR / "main.py"


@pytest.fixture
def demo04_module(tmp_path):
    """Build demo04.yaml for real via the CLI (--lang micropython) so
    ui.py/udisplay_runtime.py exist on disk, then load main.py fresh under
    a unique module name so each test gets its own copy of main.py's
    module-level simulation globals (_base_rate_hz, _enabled, etc.) instead
    of leaking state between tests."""
    runner = CliRunner()
    result = runner.invoke(cli, ["build", str(DEMO04_YAML), "-o", str(tmp_path),
                                  "--lang", "micropython"])
    assert result.exit_code == 0, result.output

    sys.path.insert(0, str(tmp_path))
    mod_name = f"_demo04_main_{uuid.uuid4().hex}"
    spec = importlib.util.spec_from_file_location(mod_name, DEMO04_MAIN)
    module = importlib.util.module_from_spec(spec)
    sys.modules[mod_name] = module
    try:
        spec.loader.exec_module(module)
        yield module
    finally:
        sys.path.remove(str(tmp_path))
        sys.modules.pop(mod_name, None)
        for mod in ("ui", "udisplay_runtime"):
            sys.modules.pop(mod, None)


def _active_ui(module):
    """A UI wired to a fake connection, brought to the active state (past
    HANDSHAKE/CLIENT_READY) the same way _serve_one_connection does."""
    from udisplay_runtime import tcp_frame, MSG_CLIENT_READY
    sent = []

    def _fake_send(data):
        sent.append(data)
        return len(data)  # tcp_send_all requires the real socket.send contract

    conn = type("C", (), {"send": staticmethod(_fake_send)})()
    u = module._make_ui(conn, [False])
    u.on_connect()
    u.feed(tcp_frame(bytes([MSG_CLIENT_READY])))
    sent.clear()
    return u, sent


class TestCommsErrorAndSend:
    """Regression tests (adversarial review, 2026-09-11): demo04 previously
    never wired on_comms_error, so an unauthenticated peer that connects and
    never answers heartbeats permanently occupied the server's one listen()
    slot; and it passed conn.send directly instead of tcp_send_all(), so a
    partial socket write would silently corrupt the framed stream."""

    def test_on_comms_error_sets_the_dead_flag(self, demo04_module):
        comms_dead = [False]
        conn = type("C", (), {"send": staticmethod(lambda data: len(data))})()
        u = demo04_module._make_ui(conn, comms_dead)
        u.on_connect()
        for _ in range(3):  # UDISPLAY_HB_MISS_MAX
            u.heartbeat()
        assert comms_dead[0] is True

    def test_send_path_survives_a_partial_socket_write(self, demo04_module):
        """A conn.send() stand-in that only accepts a few bytes per call
        must not corrupt or drop the message -- tcp_send_all() must loop
        until every byte is written."""
        whole = bytearray()
        whole_conn = type("C", (), {"send": staticmethod(lambda d: (whole.extend(d), len(d))[1])})()
        demo04_module._make_ui(whole_conn, [False]).on_connect()

        partial = bytearray()

        def _partial_send(data):
            n = min(3, len(data))  # simulate a slow/congested socket
            partial.extend(data[:n])
            return n

        partial_conn = type("C", (), {"send": staticmethod(_partial_send)})()
        demo04_module._make_ui(partial_conn, [False]).on_connect()

        assert bytes(partial) == bytes(whole)


class TestRateClamping:
    """on_rate_change (wired to rate_slider.on_change) clamps to the
    documented 0.1-10.0 Hz band -- untested before this pass."""

    def test_clamps_to_valid_band(self, demo04_module):
        u, _ = _active_ui(demo04_module)
        u.rate_slider.on_change(0.01)
        assert demo04_module._base_rate_hz == 0.1  # below minimum

        u.rate_slider.on_change(50.0)
        assert demo04_module._base_rate_hz == 10.0  # above maximum

        u.rate_slider.on_change(5.0)
        assert demo04_module._base_rate_hz == 5.0  # in-range: passes through


class TestSimulationHandlers:
    def test_mode_buttons_set_the_expected_multiplier(self, demo04_module):
        u, _ = _active_ui(demo04_module)
        u.mode_sel.fast.on_press()
        assert demo04_module._multiplier == 2.0
        u.mode_sel.slow.on_press()
        assert demo04_module._multiplier == 1.0
        u.mode_sel.turbo.on_press()
        assert demo04_module._multiplier == 5.0

    def test_power_button_toggles_and_pushes_led_state(self, demo04_module):
        u, sent = _active_ui(demo04_module)
        assert demo04_module._power_on is False
        u.power_btn.on_press()
        assert demo04_module._power_on is True
        assert len(sent) == 1  # power_led.set() pushed over the wire
        u.power_btn.on_press()
        assert demo04_module._power_on is False

    def test_enable_toggle_updates_flag_and_echoes_state(self, demo04_module):
        u, sent = _active_ui(demo04_module)
        u.enable_toggle.on_change(0)
        assert demo04_module._enabled is False
        assert len(sent) == 1

    def test_text_submit_does_not_raise(self, demo04_module, capsys):
        u, _ = _active_ui(demo04_module)
        u.text_input.on_submit("hello world")  # smoke: must not raise
        assert "hello world" in capsys.readouterr().out


class TestTempDisplayUpdate:
    def test_sends_when_enabled(self, demo04_module):
        u, sent = _active_ui(demo04_module)
        demo04_module._enabled = True
        demo04_module._temp_display_update(u)
        assert len(sent) == 1

    def test_does_not_send_when_disabled(self, demo04_module):
        u, sent = _active_ui(demo04_module)
        demo04_module._enabled = False
        demo04_module._temp_display_update(u)
        assert sent == []


class TestRecvOrNone:
    """_recv_or_none's job is distinguishing "no data this tick" (a
    recv() timeout, in either CPython's or MicroPython's error shape) from
    a genuine socket error. Completely untested before this pass -- a
    regression here either busy-loops forever treating real errors as
    timeouts, or (the bug this function's own docstring says it already
    fixed once) treats every CPython timeout as a fatal disconnect."""

    class _FakeConn:
        def __init__(self, *, raises=None, returns=None):
            self._raises = raises
            self._returns = returns

        def recv(self, n):
            if self._raises is not None:
                raise self._raises
            return self._returns

    def test_timeout_shapes_return_none(self, demo04_module):
        """CPython's socket.timeout (args=("timed out",), no errno) and
        MicroPython's plain OSError(errno) must both be recognized as
        'no data this tick', not a fatal error."""
        for exc in (
            OSError("timed out"),                        # CPython-shaped
            OSError(errno.ETIMEDOUT, "timed out"),        # MicroPython-shaped
            OSError(errno.EAGAIN, "resource unavailable"),
        ):
            conn = self._FakeConn(raises=exc)
            assert demo04_module._recv_or_none(conn) is None

    def test_genuine_error_propagates(self, demo04_module):
        conn = self._FakeConn(raises=OSError(errno.ECONNRESET, "connection reset"))
        with pytest.raises(OSError):
            demo04_module._recv_or_none(conn)

    def test_peer_closed_raises(self, demo04_module):
        conn = self._FakeConn(returns=b"")
        with pytest.raises(OSError):
            demo04_module._recv_or_none(conn)

    def test_normal_data_returned_as_is(self, demo04_module):
        conn = self._FakeConn(returns=b"\x01\x02\x03")
        assert demo04_module._recv_or_none(conn) == b"\x01\x02\x03"
