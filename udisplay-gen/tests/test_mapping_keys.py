"""Mapping keys that aren't plain strings (PR #58 review).

PyYAML converts YAML 1.1 implicit keys (`yes:`, `0x10:`), applies `<<`
merge keys and keeps the last of two duplicate keys; yaml-cpp, in
udisplay-client, does none of that. validate rejects all of them (and
alias, collection and non-!!str tagged keys) so both tools derive the same
widget paths. The cross-tool cases live in tests/protocol_vectors.json
(mapping_key_fixtures, test_vectors.py)."""
import time

import pytest
from click.testing import CliRunner

from udisplay_gen.cli import cli
from udisplay_gen.validate import mapping_key_error


@pytest.mark.parametrize("key, kind", [
    ("yes", "a boolean"), ("No", "a boolean"), ("TRUE", "a boolean"),
    ("off", "a boolean"), ("On", "a boolean"),
    ("0", "an integer"), ("-12", "an integer"), ("0x1F", "an integer"),
    ("0b101", "an integer"), ("017", "an integer"), ("1_000", "an integer"),
    ("1:30", "an integer"),
    ("1.5", "a float"), ("-1.5e+3", "a float"), (".inf", "a float"), (".NaN", "a float"),
    ("2026-10-16", "a timestamp"), ("2026-10-16 12:00:00", "a timestamp"),
    ("=", "a value key"),
])
def test_implicit_non_string_keys_rejected(key, kind):
    assert mapping_key_error(f"{key}: x\n") == (
        f"line 1, column 1: mapping key '{key}' reads as {kind} in YAML 1.1, "
        f"not a string; quote it: \"{key}\"")


@pytest.mark.parametrize("key", [
    "lamp", "y", "n", "1e3", "-.5", "yes_no", "on_off", "x0x10", "1a", "v1.5", "null_mode",
    '"yes"', "'0x10'", "!!str off", "! on", '"<<"', '"~"',
])
def test_string_keys_accepted(key):
    assert mapping_key_error(f"{key}: x\n") is None


@pytest.mark.parametrize("key", ["~", "null", "Null", "NULL"])
def test_null_keys_rejected(key):
    assert "mapping key is null" in mapping_key_error(f"{key}: x\n")


def test_merge_key_rejected():
    text = "base: &b {a: 1}\nderived:\n  <<: *b\n"
    assert mapping_key_error(text) == (
        "line 3, column 3: YAML merge keys (<<) are not supported; write the keys out")


def test_duplicate_keys_rejected_per_mapping():
    assert mapping_key_error("a: {x: 1}\nb: {x: 1}\n") is None
    assert mapping_key_error("a: 1\nb: 2\na: 3\n") == (
        "line 3, column 1: duplicate mapping key 'a'")
    # A quoted and a plain spelling are the same string.
    assert "duplicate mapping key 'a'" in mapping_key_error("a: 1\n'a': 2\n")


def test_keys_in_sequences_checked():
    assert "'off'" in mapping_key_error("list:\n  - ok: 1\n  - off: 2\n")


def test_collection_alias_and_tagged_keys_rejected():
    assert "is a mapping or sequence" in mapping_key_error("? [a, b]\n: 1\n")
    assert "is a mapping or sequence" in mapping_key_error("? {a: 1}\n: 1\n")
    assert "is an alias" in mapping_key_error("a: &k b\n*k : 1\n")
    assert "explicit non-string tag" in mapping_key_error("!!int 1: x\n")


def test_scan_does_not_expand_aliases():
    """The scan reads events, so alias fan-out costs nothing (#62 tracks
    the line map, which still expands)."""
    lines = ["l0: &l0 [x]"]
    for level in range(1, 30):
        lines.append(f"l{level}: &l{level} [" + ", ".join([f"*l{level - 1}"] * 10) + "]")
    start = time.monotonic()
    assert mapping_key_error("\n".join(lines) + "\n") is None
    assert time.monotonic() - start < 5


@pytest.mark.parametrize("command", ["validate", "build"])
def test_cli_rejects_boolean_like_key(tmp_path, command):
    src = tmp_path / "keys.yaml"
    src.write_text("device:\n  name: d\nwidgets:\n  on:\n    type: led\n")
    args = [command, str(src)] + (["-o", str(tmp_path / "out")] if command == "build" else [])
    result = CliRunner().invoke(cli, args)
    assert result.exit_code == 1
    assert "line 4, column 3: mapping key 'on' reads as a boolean" in result.output


def test_cli_rejects_collection_key_cleanly(tmp_path):
    """PyYAML can't load an unhashable key at all; the CLI still exits 1
    without a traceback (the client reports mappingKeyError() instead)."""
    src = tmp_path / "keys.yaml"
    src.write_text("device:\n  name: d\nwidgets:\n  ? [a, b]\n  : {type: led}\n")
    result = CliRunner().invoke(cli, ["validate", str(src)])
    assert result.exit_code == 1
    assert "could not parse YAML" in result.output
