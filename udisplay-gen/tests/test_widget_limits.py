"""Maximum widget nesting depth (issue #24, TODO-036) and widget count.

A widget's depth is the number of keys in its structural path: a top-level
widget is at depth 1, a button-group item one deeper than its group. Past
MAX_NESTING_DEPTH, or past MAX_WIDGETS widgets, validate reports that as its
only error, since every other check recurses once per level and visits
every widget a YAML alias expands to. The cross-tool boundary cases live in
tests/protocol_vectors.json (nesting_depth_fixtures, test_vectors.py)."""
import time

import pytest
from click.testing import CliRunner

from udisplay_gen.cli import cli
from udisplay_gen.validate import semantic_errors, validate
from udisplay_gen.widget_ids import (
    MAX_NESTING_DEPTH, MAX_WIDGETS, assign, widget_limit_error, widget_tree,
)


def _rows(depth: int, leaf: dict | None = None) -> dict:
    """A widget map whose innermost widget (`leaf`, an led by default) is at
    `depth`, below depth - 1 nested rows l1, l2, ..."""
    widget = leaf or {"type": "led"}
    key = "leaf"
    for level in range(depth - 1, 0, -1):
        widget = {"type": "row", "widgets": {key: widget}}
        key = f"l{level}"
    return {key: widget}


def _path(depth: int) -> str:
    return ".".join([f"l{i}" for i in range(1, depth)] + ["leaf"])


BUTTON_GROUP = {"type": "button-group",
                "items": {"a": {"label": "A"}, "b": {"label": "B"}}}


def test_max_depth_is_ten():
    assert MAX_NESTING_DEPTH == 10


def test_at_max_depth_accepted():
    widgets = _rows(MAX_NESTING_DEPTH)
    assert widget_limit_error(widgets) is None
    assert len(assign(widgets)) == MAX_NESTING_DEPTH
    assert validate({"device": {"name": "d"}, "widgets": widgets}) == []


def test_one_over_max_depth_rejected():
    widgets = _rows(MAX_NESTING_DEPTH + 1)
    expected = (f"widgets.{_path(MAX_NESTING_DEPTH + 1)}: nested 11 levels deep; "
                f"the maximum widget nesting depth is 10")
    assert widget_limit_error(widgets) == expected
    assert validate({"device": {"name": "d"}, "widgets": widgets}) == [f"  {expected}"]
    assert semantic_errors({"widgets": widgets}) == [f"  {expected}"]


def test_button_group_items_count_as_one_level():
    assert widget_limit_error(_rows(MAX_NESTING_DEPTH - 1, BUTTON_GROUP)) is None
    error = widget_limit_error(_rows(MAX_NESTING_DEPTH, BUTTON_GROUP))
    assert error.startswith(f"widgets.{_path(MAX_NESTING_DEPTH)}.a: nested 11 levels")


def test_button_face_and_dpad_children_count():
    """Every `widgets:` map is a level, not just section/row/grid."""
    face = {"type": "button", "widgets": {"face": {"type": "label", "text": "x"}}}
    assert widget_limit_error(_rows(MAX_NESTING_DEPTH - 1, face)) is None
    assert "nested 11 levels" in widget_limit_error(_rows(MAX_NESTING_DEPTH, face))
    dpad = {"type": "dpad", "widgets": {"up": {"type": "button", "position": "up"}}}
    assert widget_limit_error(_rows(MAX_NESTING_DEPTH - 1, dpad)) is None
    assert "nested 11 levels" in widget_limit_error(_rows(MAX_NESTING_DEPTH, dpad))


def test_reports_first_too_deep_widget_in_declaration_order():
    widgets = {"ok": {"type": "led"},
               "first": _rows(MAX_NESTING_DEPTH + 2)["l1"],
               "second": _rows(MAX_NESTING_DEPTH + 1)["l1"]}
    assert widget_limit_error(widgets).startswith(
        "widgets.first.l2.l3.l4.l5.l6.l7.l8.l9.l10.l11: nested 11 levels")


def test_far_too_deep_does_not_recurse():
    """Deep enough to blow Python's recursion limit if any walk recursed
    (and to make jsonschema's oneOf branches take forever)."""
    widgets = _rows(50_000)
    assert "nested 11 levels" in widget_limit_error(widgets)
    errors = validate({"device": {"name": "d"}, "widgets": widgets})
    assert len(errors) == 1 and "nested 11 levels" in errors[0]


def test_widget_tree_and_assign_raise_past_max_depth():
    """Callers that skip validate (the backends, assign()) still refuse."""
    with pytest.raises(ValueError, match="nested 11 levels deep"):
        widget_tree(_rows(MAX_NESTING_DEPTH + 1))
    with pytest.raises(ValueError, match=r"\.leaf\.a: nested 11 levels deep"):
        assign(_rows(MAX_NESTING_DEPTH, BUTTON_GROUP))


def _yaml(depth: int) -> str:
    text, indent = "device:\n  name: deep\nwidgets:\n", "  "
    for level in range(1, depth):
        text += f"{indent}l{level}:\n{indent}  type: row\n{indent}  widgets:\n"
        indent += "    "
    return text + f"{indent}leaf:\n{indent}  type: led\n"


@pytest.mark.parametrize("command", ["validate", "build"])
def test_cli_rejects_too_deep_yaml(tmp_path, command):
    src = tmp_path / "deep.yaml"
    src.write_text(_yaml(MAX_NESTING_DEPTH + 1))
    args = [command, str(src)] + (["-o", str(tmp_path / "out")] if command == "build" else [])
    result = CliRunner().invoke(cli, args)
    assert result.exit_code == 1
    assert "nested 11 levels deep; the maximum widget nesting depth is 10" in result.output


def test_cli_accepts_max_depth_yaml(tmp_path):
    src = tmp_path / "deep.yaml"
    src.write_text(_yaml(MAX_NESTING_DEPTH))
    result = CliRunner().invoke(cli, ["validate", str(src)])
    assert result.exit_code == 0, result.output


def test_alias_cycle_rejected(tmp_path):
    """An alias to an enclosing anchor loads as a cyclic dict; without the
    cap, every recursive walk over it would never end."""
    src = tmp_path / "cycle.yaml"
    src.write_text("device:\n  name: d\nwidgets:\n"
                   "  loop: &loop\n    type: row\n    widgets:\n      again: *loop\n")
    result = CliRunner().invoke(cli, ["validate", str(src)])
    assert result.exit_code == 1
    assert ("widgets.loop" + ".again" * 10 + ": nested 11 levels deep") in result.output


@pytest.mark.parametrize("command", ["validate", "build"])
def test_cli_rejects_yaml_too_deep_to_load(tmp_path, command):
    """Past a few hundred levels PyYAML's own recursive loader gives up
    before the depth check sees the document; the CLI must still fail
    cleanly (exit 1, no traceback), not crash."""
    src = tmp_path / "deeper.yaml"
    src.write_text(_yaml(600))
    args = [command, str(src)] + (["-o", str(tmp_path / "out")] if command == "build" else [])
    result = CliRunner().invoke(cli, args)
    assert result.exit_code == 1
    assert not isinstance(result.exception, RecursionError)
    assert "could not parse YAML" in result.output


# ── Widget count ─────────────────────────────────────────────────────────────

COUNT_ERROR = "widget number 241; the maximum widget count is 240 (widget IDs 0x10-0xFF)"


def _leds(count: int) -> dict:
    return {f"w{i:03d}": {"type": "led"} for i in range(count)}


def test_max_widgets_accepted():
    widgets = _leds(MAX_WIDGETS)
    assert widget_limit_error(widgets) is None
    assert validate({"device": {"name": "d"}, "widgets": widgets}) == []


def test_one_over_max_widgets_rejected_by_validate():
    """validate used to accept this; only build (assign()) refused it."""
    widgets = _leds(MAX_WIDGETS + 1)
    expected = f"widgets.w240: {COUNT_ERROR}"
    assert widget_limit_error(widgets) == expected
    assert validate({"device": {"name": "d"}, "widgets": widgets}) == [f"  {expected}"]
    assert semantic_errors({"widgets": widgets}) == [f"  {expected}"]


def test_count_names_widget_241_in_declaration_order():
    """Parents count before their children, button-group items included."""
    group = {"type": "button-group",
             "items": {f"i{i:03d}": {"label": "x"} for i in range(MAX_WIDGETS)}}
    widgets = {"group": group, "after": {"type": "led"}}
    assert widget_limit_error(widgets) == f"widgets.group.i239: {COUNT_ERROR}"


def test_first_limit_reached_in_declaration_order_wins():
    deep_first = {"deep": _rows(MAX_NESTING_DEPTH + 1)["l1"], **_leds(MAX_WIDGETS)}
    assert "nested 11 levels deep" in widget_limit_error(deep_first)
    many_first = {**_leds(MAX_WIDGETS), "deep": _rows(MAX_NESTING_DEPTH + 1)["l1"]}
    assert widget_limit_error(many_first) == f"widgets.deep: {COUNT_ERROR}"


def test_alias_fan_out_stops_at_the_cap():
    """Aliases share one object per anchor, so a few lines of YAML load as
    a tree of 10^10 widgets ("billion laughs"). The walk stops at widget
    241 instead of visiting them all."""
    widgets: dict = {"type": "led"}
    for _ in range(MAX_NESTING_DEPTH - 1):
        widgets = {"type": "row", "widgets": {f"k{i}": widgets for i in range(10)}}
    widgets = {f"k{i}": widgets for i in range(10)}
    start = time.monotonic()
    error = widget_limit_error(widgets)
    assert error is not None and error.endswith(COUNT_ERROR)
    errors = validate({"device": {"name": "d"}, "widgets": widgets})
    assert len(errors) == 1 and COUNT_ERROR in errors[0]
    with pytest.raises(ValueError, match="widget number 241"):
        assign(widgets)
    assert time.monotonic() - start < 5


def test_cli_rejects_alias_fan_out(tmp_path):
    lines = ["device:", "  name: d", "anchors:", "  l0: &l0 {type: led}"]
    for level in range(1, MAX_NESTING_DEPTH):
        kids = ", ".join(f"k{i}: *l{level - 1}" for i in range(10))
        lines.append(f"  l{level}: &l{level} {{type: row, widgets: {{{kids}}}}}")
    kids = ", ".join(f"k{i}: *l{MAX_NESTING_DEPTH - 1}" for i in range(10))
    lines.append(f"widgets: {{{kids}}}")
    src = tmp_path / "laughs.yaml"
    src.write_text("\n".join(lines) + "\n")
    for command in ("validate", "build"):
        args = [command, str(src)] + (["-o", str(tmp_path / "out")] if command == "build" else [])
        result = CliRunner().invoke(cli, args)
        assert result.exit_code == 1, result.output
        assert COUNT_ERROR in result.output
