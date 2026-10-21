// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 Attila Agas

/**
 * @file tx_ring.h
 * @brief FIFO of variable-length outbound fragments in a caller-owned byte
 *        buffer. Each fragment is stored behind a u16_le length prefix.
 *
 * Used by transport backends whose link accepts one unit at a time (BLE
 * INDICATE: one unconfirmed indication per connection). The library's send
 * callback must accept every fragment without blocking, so the backend
 * queues here and drains one fragment per link confirmation.
 *
 * Not thread-safe: the caller serializes access.
 */
#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

typedef struct {
    uint8_t* buf;
    uint16_t cap;
    uint16_t head;   /**< next byte to read */
    uint16_t used;   /**< bytes queued, length prefixes included */
} tx_ring_t;

/** Bytes a fragment of @p len occupies in the ring. */
#define TX_RING_ENTRY_SIZE(len) ((uint32_t)(len) + 2u)

void tx_ring_init(tx_ring_t* r, uint8_t* storage, uint16_t cap);

/** Drop everything queued. */
void tx_ring_reset(tx_ring_t* r);

/** 1 if no fragment is queued. */
int tx_ring_empty(const tx_ring_t* r);

/**
 * Queue a copy of @p len bytes. @return 1 on success, 0 when @p len is 0 or
 * the fragment doesn't fit (nothing is queued then).
 */
int tx_ring_push(tx_ring_t* r, const uint8_t* data, uint16_t len);

/**
 * Dequeue the oldest fragment into @p out. @return its length, or 0 when the
 * ring is empty. A fragment longer than @p out_cap is discarded and 0 is
 * returned — size @p out for the largest fragment you push.
 */
uint16_t tx_ring_pop(tx_ring_t* r, uint8_t* out, uint16_t out_cap);

#ifdef __cplusplus
}
#endif
