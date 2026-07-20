# uDisplay Merkle Specification

**Version:** 1.0
**Status:** Normative — both `udisplay-gen` (Python) and the Qt client MUST implement from this document.

---

## Purpose

This document is the single shared specification for the uDisplay Merkle hash scheme.
Any discrepancy between this document and either implementation is a **bug in the
implementation**, not in this document.

The Merkle hash gives the Qt client a stable, content-addressable identity for each
device's UI definition blob. The client uses this hash to:

1. Detect when a device's firmware has been updated (root changed → re-download).
2. Skip the download entirely when the cached root matches (warm connect fast path).
3. Verify each downloaded chunk before accepting it (tamper / corruption detection).

---

## Input: the compressed blob

The input to the Merkle scheme is the **compressed UI definition blob** produced by
`udisplay-gen build`:

```
blob = zlib_compress(yaml_binary, level=6)
```

- Compression algorithm: **zlib DEFLATE**, `wbits = 15` (standard zlib header + trailer).
- Compression level: **6** (fixed — must be identical across runs for determinism).
- The blob is the compressed bytes only. No length prefix, no framing.

---

## Chunking

```
CHUNK_SIZE = 256  (bytes)
N_CHUNKS   = ceil(len(blob) / CHUNK_SIZE)
```

Chunks are numbered `0 … N_CHUNKS - 1`.

### Last-chunk padding

If `len(blob)` is not an exact multiple of 256, the final chunk is padded with **zero
bytes (`0x00`)** to exactly 256 bytes before hashing.

```
chunk[i] = blob[i*256 : (i+1)*256]          # may be < 256 bytes for last chunk
if len(chunk[N_CHUNKS-1]) < CHUNK_SIZE:
    chunk[N_CHUNKS-1] = chunk[N_CHUNKS-1].ljust(CHUNK_SIZE, b'\x00')
```

The padding bytes are **only added for hashing**. They are never transmitted over the
wire. The chunk server sends the raw (unpadded) bytes for the final chunk; the client
pads to 256 bytes locally before verifying the hash.

---

## Chunk hashes

Each padded chunk is hashed with **SHA-256**:

```
chunk_hash[i] = SHA-256(chunk[i])   # chunk[i] is always exactly 256 bytes (padded if last)
```

Result: an ordered list of 32-byte hashes, one per chunk.

---

## Root hash (flat hash chain)

The Merkle root is computed by concatenating all chunk hashes in order and hashing the
result once with SHA-256:

```
root = SHA-256(chunk_hash[0] ‖ chunk_hash[1] ‖ … ‖ chunk_hash[N_CHUNKS-1])
```

Where `‖` denotes byte concatenation.

This is a **flat hash chain**, not a tree. There is no intermediate layer. The root is
always exactly 32 bytes regardless of blob size.

### Rationale for flat chain over tree

uDisplay blobs are ≤ 64 KB → max 256 chunks. A tree would add implementation complexity
with no practical benefit: a client that downloads all chunks sequentially has nothing
to gain from tree-level partial verification. Flat chain keeps both implementations
simple and the spec testable with a single known-answer test vector.

---

## Single-chunk edge case

If the entire blob fits in one chunk (`N_CHUNKS = 1`):

```
root = SHA-256(SHA-256(padded_chunk[0]))
```

The root is the SHA-256 of the single chunk hash, not the chunk itself. This is a
deliberate consistency choice: the root computation formula is identical regardless of
chunk count.

---

## Reference computation (pseudocode)

```python
import zlib, hashlib, math

CHUNK_SIZE = 256

def compute_merkle_root(yaml_bytes: bytes) -> bytes:
    blob = zlib.compress(yaml_bytes, level=6)
    n_chunks = math.ceil(len(blob) / CHUNK_SIZE)

    chunk_hashes = []
    for i in range(n_chunks):
        chunk = blob[i * CHUNK_SIZE : (i + 1) * CHUNK_SIZE]
        # Pad last chunk to CHUNK_SIZE with zero bytes
        if len(chunk) < CHUNK_SIZE:
            chunk = chunk.ljust(CHUNK_SIZE, b'\x00')
        chunk_hashes.append(hashlib.sha256(chunk).digest())

    root = hashlib.sha256(b''.join(chunk_hashes)).digest()
    return root  # 32 bytes
```

---

## Wire protocol integration

The root hash appears in the **HANDSHAKE** message sent by the device on connect
(see `docs/protocol.md` § HANDSHAKE (0x00)):

```
[u8   0x00]         msg_type = HANDSHAKE
[u8   0x01]         proto_version
[32×  u8  root]     Merkle root (SHA-256 of concatenated chunk hashes)
[u16  chunk_count]  little-endian, total number of chunks
[u16  chunk_size ]  little-endian, always 256 in v1
```

Total: 38 bytes.

The client compares `root` against its local cache (`sqlite: yaml_blob_hash`). On a
cache hit, it skips HASH_REQUEST / CHUNK_REQUEST entirely and proceeds to widget
rendering.

---

## Chunk download and verification

During bootstrap the client downloads chunks by index:

1. Client sends **CHUNK_REQUEST** with the desired chunk index.
2. Device sends **CHUNK_DATA** with the raw chunk bytes (not padded for last chunk).
3. Client pads the received bytes to 256 bytes with `0x00` if it is the last chunk
   (`index == N_CHUNKS - 1`).
4. Client computes `SHA-256(padded_chunk)` and compares against the expected
   `chunk_hash[index]`.

The client accumulates `chunk_hash[i]` values as each chunk is verified. After all
chunks arrive it computes the root and **must** compare it against the root from
`HANDSHAKE_RESPONSE`. A mismatch → reject the device (user sees "definition corrupted").

---

## Test vectors

The file `tests/protocol_vectors.json` contains canonical test vectors that BOTH
`udisplay-gen` (pytest) and the Qt client (gtest) MUST validate against. Each vector
includes:

| Field           | Description                                      |
|-----------------|--------------------------------------------------|
| `yaml_hex`      | hex-encoded YAML input bytes                     |
| `blob_hex`      | hex-encoded zlib-compressed output               |
| `n_chunks`      | expected chunk count                             |
| `chunk_hashes`  | array of hex-encoded SHA-256 chunk hashes        |
| `root_hex`      | hex-encoded Merkle root (32 bytes)               |
| `widget_ids`    | map of YAML key path → assigned widget ID (hex)  |

Minimum required vectors:

1. **Tiny blob** — compresses to < 256 bytes (single chunk, padded).
2. **Exact boundary** — compresses to exactly 256 bytes (single chunk, no padding).
3. **Multi-chunk** — compresses to 257–512 bytes (two chunks, last chunk padded).
4. **Full vocabulary** — YAML containing all 7 widget types in non-alphabetical order
   (verifies sort + ID assignment).
5. **Max blob** — YAML that produces exactly 64 KB compressed (boundary, must pass
   `udisplay-gen build`).

---

## Constraints and limits

| Parameter            | Value         | Notes                                      |
|----------------------|---------------|--------------------------------------------|
| `CHUNK_SIZE`         | 256 bytes     | Fixed. Not configurable.                   |
| Max blob size        | 65 536 bytes  | `udisplay-gen build` hard-fails above this |
| Max chunks           | 256           | = 65 536 / 256                             |
| Hash algorithm       | SHA-256       | Output: 32 bytes                           |
| Compression          | zlib level 6  | Fixed level for determinism                |
| Widget ID space      | 0x10–0xFF     | 240 user slots                             |
| System ID space      | 0x00–0x0F     | Reserved (see `docs/protocol.md`)          |

