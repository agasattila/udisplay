// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 Attila Agas

#include "chunk_server.h"
#include "protocol.h"

void chunk_server_init(chunk_server_t* cs,
                       const uint8_t* const* chunks,
                       const uint8_t* const* chunk_hashes,
                       const uint16_t*       chunk_lens,
                       uint16_t              chunk_count)
{
    cs->chunks       = chunks;
    cs->chunk_hashes = chunk_hashes;
    cs->chunk_lens   = chunk_lens;
    cs->chunk_count  = chunk_count;
}

uint16_t chunk_server_header_response(const chunk_server_t* cs,
                                       uint8_t* buf, uint16_t cap,
                                       uint16_t idx)
{
    if (idx >= cs->chunk_count) {
        return proto_err_invalid_chunk(buf, cap, idx);
    }
    /* len_byte: 0 = full 256-byte chunk; 1-255 = partial last chunk */
    uint8_t len_byte = (cs->chunk_lens[idx] == 256u) ? 0u
                                                      : (uint8_t)cs->chunk_lens[idx];
    return proto_chunk_header_response(buf, cap, cs->chunk_hashes[idx], len_byte);
}

uint16_t chunk_server_respond(const chunk_server_t* cs,
                               uint8_t* buf, uint16_t cap,
                               uint16_t idx)
{
    if (idx >= cs->chunk_count) {
        return proto_err_invalid_chunk(buf, cap, idx);
    }
    return proto_chunk_response(buf, cap, idx,
                                cs->chunks[idx], cs->chunk_lens[idx]);
}
