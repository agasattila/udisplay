"""Tests for Merkle computation (docs/merkle.md)."""
import hashlib
import zlib

import pytest

from udisplay_gen.merkle import CHUNK_SIZE, MAX_BLOB_SIZE, chunk_hashes, compress, compute, root


def test_compress_is_deterministic():
    data = b"device:\n  name: t\nwidgets:\n  a:\n    type: toggle\n"
    assert compress(data) == compress(data)


def test_compress_level6():
    """zlib level=6 must be used — compare against explicit level=6 call."""
    data = b"hello world " * 100
    assert compress(data) == zlib.compress(data, level=6)


def test_single_chunk_padded():
    """Blob < 256 bytes: one chunk, padded to 256 before hashing."""
    blob = b"\x00" * 50
    hashes = chunk_hashes(blob)
    assert len(hashes) == 1
    padded = blob.ljust(CHUNK_SIZE, b"\x00")
    assert hashes[0] == hashlib.sha256(padded).digest()


def test_single_chunk_no_padding():
    """Blob exactly 256 bytes: one chunk, no padding needed."""
    blob = bytes(range(256))
    hashes = chunk_hashes(blob)
    assert len(hashes) == 1
    assert hashes[0] == hashlib.sha256(blob).digest()


def test_last_chunk_max_padding():
    """Blob 257 bytes: last chunk has 1 byte → 255 zero bytes appended."""
    blob = bytes(range(256)) + b"\xAB"
    hashes = chunk_hashes(blob)
    assert len(hashes) == 2
    padded_last = b"\xAB" + b"\x00" * 255
    assert hashes[1] == hashlib.sha256(padded_last).digest()


def test_root_single_chunk():
    """Root = SHA-256(SHA-256(padded_chunk)) for a single chunk."""
    blob = b"\x42" * 10
    hashes = chunk_hashes(blob)
    assert len(hashes) == 1
    expected = hashlib.sha256(hashes[0]).digest()
    assert root(hashes) == expected


def test_root_multi_chunk():
    """Root = SHA-256(hash_0 || hash_1) for two chunks."""
    blob = bytes(256) + bytes(256)
    hashes = chunk_hashes(blob)
    assert len(hashes) == 2
    expected = hashlib.sha256(hashes[0] + hashes[1]).digest()
    assert root(hashes) == expected


def test_compute_returns_triple():
    yaml_bytes = b"device:\n  name: t\nwidgets:\n  a:\n    type: toggle\n"
    blob, r, hashes = compute(yaml_bytes)
    assert isinstance(blob, bytes)
    assert len(r) == 32
    assert all(len(h) == 32 for h in hashes)
    # Root matches recomputed
    assert r == root(hashes)


def test_compute_blob_too_large(monkeypatch):
    """Build fails with ValueError when compressed blob > 64 KB."""
    import udisplay_gen.merkle as _merkle

    # Patch compress() to return a blob that exceeds the 64 KB limit
    oversized = b"\x00" * (MAX_BLOB_SIZE + 1)
    monkeypatch.setattr(_merkle, "compress", lambda _: oversized)
    with pytest.raises(ValueError, match="64 KB"):
        compute(b"anything")


def test_compute_deterministic(minimal_yaml):
    yaml_bytes = minimal_yaml.read_bytes()
    _, r1, _ = compute(yaml_bytes)
    _, r2, _ = compute(yaml_bytes)
    assert r1 == r2
