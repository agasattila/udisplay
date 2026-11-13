// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 Attila Agas

#include "tx_ring.h"

#include <stddef.h>
#include <string.h>

void tx_ring_init(tx_ring_t* r, uint8_t* storage, uint16_t cap)
{
    r->buf = storage;
    r->cap = cap;
    tx_ring_reset(r);
}

void tx_ring_reset(tx_ring_t* r)
{
    r->head = 0;
    r->used = 0;
}

int tx_ring_empty(const tx_ring_t* r)
{
    return r->used == 0u;
}

/*
 * Append @p n bytes at the tail, wrapping at the end of the buffer.
 *
 * Free space is NOT checked here: the caller must ensure
 * r->used + n <= r->cap. tx_ring_push() checks this once for the whole entry
 * (length prefix + payload) before either of its two writes, so an entry is
 * either queued completely or not at all.
 */
static void ring_write(tx_ring_t* r, const uint8_t* src, uint16_t n)
{
    uint16_t tail  = (uint16_t)(((uint32_t)r->head + r->used) % r->cap);
    uint16_t first = (uint16_t)(r->cap - tail);
    if (first > n) first = n;
    memcpy(&r->buf[tail], src, first);
    if (n > first) {
        /* wrapped: the rest goes to the start of the buffer */
        memcpy(r->buf, src + first, (size_t)(n - first));
    }
    r->used = (uint16_t)(r->used + n);
}

/*
 * Consume @p n bytes from the head, wrapping at the end of the buffer.
 * @p dst may be NULL to skip bytes.
 *
 * Available data is NOT checked here: the caller must ensure n <= r->used.
 * tx_ring_pop() is the only caller, and it validates both the length prefix
 * (r->used >= 2) and the payload length against r->used before reading, so
 * this helper stays a plain copy.
 */
static void ring_read(tx_ring_t* r, uint8_t* dst, uint16_t n)
{
    uint16_t first = (uint16_t)(r->cap - r->head);
    if (first > n) first = n;
    if (dst) {
        memcpy(dst, &r->buf[r->head], first);
        if (n > first) {
            /* wrapped: the rest comes from the start of the buffer */
            memcpy(dst + first, r->buf, (size_t)(n - first));
        }
    }
    r->head = (uint16_t)(((uint32_t)r->head + n) % r->cap);
    r->used = (uint16_t)(r->used - n);
}

int tx_ring_push(tx_ring_t* r, const uint8_t* data, uint16_t len)
{
    uint8_t hdr[2];
    if (len == 0u || (uint32_t)r->used + TX_RING_ENTRY_SIZE(len) > r->cap) {
        return 0;
    }
    hdr[0] = (uint8_t)(len & 0xFFu);
    hdr[1] = (uint8_t)(len >> 8);
    ring_write(r, hdr, 2u);
    ring_write(r, data, len);
    return 1;
}

uint16_t tx_ring_pop(tx_ring_t* r, uint8_t* out, uint16_t out_cap)
{
    uint8_t  hdr[2];
    uint16_t len;
    if (r->used == 0u) return 0;
    if (r->used < 2u) {
        /* Not even a length prefix. Can't happen through the public API
         * (push always writes prefix + payload together); treat the ring as
         * corrupt and drop it rather than read past the queued bytes. */
        tx_ring_reset(r);
        return 0;
    }
    ring_read(r, hdr, 2u);
    len = (uint16_t)(hdr[0] | (hdr[1] << 8));
    if (len == 0u || len > r->used) {
        /* Prefix inconsistent with the queued bytes (push never stores a
         * zero-length entry): corrupt, drop everything. */
        tx_ring_reset(r);
        return 0;
    }
    if (len > out_cap) {
        ring_read(r, NULL, len);
        return 0;
    }
    ring_read(r, out, len);
    return len;
}
