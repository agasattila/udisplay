// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 Attila Agas

/**
 * @file chunk_server.h
 * @brief Serves CHUNK_HEADER_RESPONSE and CHUNK_RESPONSE/ERR_INVALID_CHUNK messages.
 */
#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

typedef struct {
    const uint8_t* const* chunks;
    const uint8_t* const* chunk_hashes;
    const uint16_t*       chunk_lens;
    uint16_t              chunk_count;
} chunk_server_t;

void chunk_server_init(chunk_server_t* cs,
                       const uint8_t* const* chunks,
                       const uint8_t* const* chunk_hashes,
                       const uint16_t*       chunk_lens,
                       uint16_t              chunk_count);

/**
 * Encode a CHUNK_HEADER_RESPONSE for the requested index, or ERR_INVALID_CHUNK
 * if idx >= chunk_count.
 * Returns bytes written, 0 on buffer overflow.
 */
uint16_t chunk_server_header_response(const chunk_server_t* cs,
                                       uint8_t* buf, uint16_t cap,
                                       uint16_t idx);

/**
 * Encode a CHUNK_RESPONSE for the requested index, or ERR_INVALID_CHUNK
 * if idx >= chunk_count.
 * Returns bytes written, 0 on buffer overflow.
 */
uint16_t chunk_server_respond(const chunk_server_t* cs,
                               uint8_t* buf, uint16_t cap,
                               uint16_t idx);

#ifdef __cplusplus
}
#endif
