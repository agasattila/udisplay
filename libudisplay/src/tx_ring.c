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

static void ring_write(tx_ring_t* r, const uint8_t* src, uint16_t n)
{
    uint16_t tail  = (uint16_t)(((uint32_t)r->head + r->used) % r->cap);
    uint16_t first = (uint16_t)(r->cap - tail);
    if (first > n) first = n;
    memcpy(&r->buf[tail], src, first);
    memcpy(r->buf, src + first, (size_t)(n - first));
    r->used = (uint16_t)(r->used + n);
}

/* @p dst may be NULL to skip bytes. */
static void ring_read(tx_ring_t* r, uint8_t* dst, uint16_t n)
{
    uint16_t first = (uint16_t)(r->cap - r->head);
    if (first > n) first = n;
    if (dst) {
        memcpy(dst, &r->buf[r->head], first);
        memcpy(dst + first, r->buf, (size_t)(n - first));
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
    ring_read(r, hdr, 2u);
    len = (uint16_t)(hdr[0] | (hdr[1] << 8));
    if (len > out_cap) {
        ring_read(r, NULL, len);
        return 0;
    }
    ring_read(r, out, len);
    return len;
}
