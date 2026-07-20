// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Attila Agas

/*
 * Regression test for demo_tcp_send() (plan-eng-review, 2026-07-07):
 * previously demo_tcp_send() manually called udisplay_tcp_frame() before
 * writing to the socket. Once demo01/02/03 correctly pass
 * UDISPLAY_TRANSPORT_TCP to udisplay_ui_init()/UDisplay::init(),
 * libudisplay's own framed_send_raw() (udisplay.c) ALSO frames outbound
 * messages -- so demo_tcp_send() re-framing on top of that would double-
 * frame every outbound message and corrupt the wire.
 *
 * No test harness exists for demos/shared today (PC demos are run manually,
 * not under CI -- see TODOS.md TODO-006). This is a small, self-contained
 * regression test: build and run directly, e.g.
 *   cc -std=c11 -I../../libudisplay/include -o /tmp/test_demo_tcp \
 *       test_demo_tcp.c && /tmp/test_demo_tcp
 *
 * demo_tcp.c exposes no test seam for its internal client socket (s_fd is
 * file-static), so this test #includes demo_tcp.c directly to reach it --
 * a standard technique for testing static functions without adding
 * test-only API surface to production code.
 */

#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

#include "demo_tcp.c"

int main(void)
{
    int fds[2];
    assert(socketpair(AF_UNIX, SOCK_STREAM, 0, fds) == 0);
    s_fd = fds[0];   /* inject the test socket in place of a real accept()ed client */

    /*
     * Simulate what udisplay's TCP transport path (framed_send_raw(),
     * UDISPLAY_TRANSPORT_TCP case) already does before calling cfg.send():
     * a u16_le length prefix (len=1) followed by a 1-byte payload.
     */
    uint8_t already_framed[3] = { 0x01u, 0x00u, 0xABu };
    demo_tcp_send(already_framed, sizeof(already_framed), NULL);

    uint8_t got[16];
    ssize_t n = read(fds[1], got, sizeof(got));
    assert(n == (ssize_t)sizeof(already_framed));
    assert(memcmp(got, already_framed, sizeof(already_framed)) == 0);

    close(fds[0]);
    close(fds[1]);

    printf("OK: demo_tcp_send() writes bytes verbatim -- no double-framing\n");
    return 0;
}
