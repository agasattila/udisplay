# SPDX-License-Identifier: MPL-2.0
# Copyright (c) 2026 Attila Agas

"""
Merkle hash computation for uDisplay blobs.
Implements the algorithm from docs/merkle.md exactly.
Both udisplay-gen (this file) and the Qt client must produce identical roots.
"""
from __future__ import annotations

import hashlib
import math
import zlib

CHUNK_SIZE = 256
MAX_BLOB_SIZE = 65_536  # 64 KB hard limit


def compress(yaml_bytes: bytes) -> bytes:
    """zlib DEFLATE, level=6, fixed for determinism across runs and machines."""
    return zlib.compress(yaml_bytes, level=6)


def chunk_hashes(blob: bytes) -> list[bytes]:
    """
    SHA-256 of each 256-byte chunk.
    Last chunk is zero-padded to CHUNK_SIZE before hashing; padding bytes are
    never included in the transmitted chunk data.
    """
    n = math.ceil(len(blob) / CHUNK_SIZE)
    result = []
    for i in range(n):
        chunk = blob[i * CHUNK_SIZE : (i + 1) * CHUNK_SIZE]
        padded = chunk.ljust(CHUNK_SIZE, b"\x00")
        result.append(hashlib.sha256(padded).digest())
    return result


def root(hashes: list[bytes]) -> bytes:
    """Flat hash chain: SHA-256(hash_0 || hash_1 || ... || hash_{n-1})."""
    return hashlib.sha256(b"".join(hashes)).digest()


def compute(yaml_bytes: bytes) -> tuple[bytes, bytes, list[bytes]]:
    """
    Full pipeline: compress → chunk → hash → root.

    Returns (blob, root_32_bytes, chunk_hashes_list).
    Raises ValueError if compressed blob exceeds 64 KB.
    """
    blob = compress(yaml_bytes)
    if len(blob) > MAX_BLOB_SIZE:
        raise ValueError(
            f"Compressed blob is {len(blob):,} bytes — exceeds the 64 KB limit "
            f"({MAX_BLOB_SIZE:,} bytes). Reduce widget count or shorten string values."
        )
    hashes = chunk_hashes(blob)
    return blob, root(hashes), hashes
