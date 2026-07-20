// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Attila Agas

#include <arpa/inet.h>
#include <netinet/in.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <time.h>
#include <unistd.h>

#include "udisplay.h"
#include "demo_tcp.h"

#define TICK_MS 100

static pthread_mutex_t        s_mutex = PTHREAD_MUTEX_INITIALIZER;
static int                    s_fd    = -1;
static const demo_tcp_hooks_t* s_hooks = NULL;

void demo_tcp_send(const uint8_t* msg, uint16_t len, void* ud)
{
    /*
     * libudisplay frames this message itself before calling this callback:
     * udisplay_ui_init()/UDisplay::init() is called with UDISPLAY_TRANSPORT_TCP,
     * so framed_send_raw() (udisplay.c) already prepends the u16_le length
     * header via udisplay_tcp_frame() prior to invoking cfg.send(). Framing
     * again here would double-frame every outbound message.
     */
    (void)ud;
    if (s_fd < 0) return;
    ssize_t w = write(s_fd, msg, len);
    (void)w;
}

static void* recv_thread(void* arg)
{
    (void)arg;
    uint8_t tmp[2048];
    while (1) {
        ssize_t n = recv(s_fd, tmp, sizeof(tmp), 0);
        if (n <= 0) break;
        pthread_mutex_lock(&s_mutex);
        if (s_hooks->on_rx) s_hooks->on_rx(tmp, (uint16_t)n);
        pthread_mutex_unlock(&s_mutex);
    }
    printf("[CONN] Client disconnected.\n");
    pthread_mutex_lock(&s_mutex);
    if (s_hooks->on_disconnect) s_hooks->on_disconnect();
    close(s_fd);
    s_fd = -1;
    pthread_mutex_unlock(&s_mutex);
    return NULL;
}

static void* timer_thread(void* arg)
{
    (void)arg;
    const double dt = TICK_MS / 1000.0;
    while (1) {
        struct timespec ts = { 0, (long)TICK_MS * 1000000L };
        nanosleep(&ts, NULL);
        pthread_mutex_lock(&s_mutex);
        if (s_fd >= 0 && s_hooks->on_tick) s_hooks->on_tick(dt);
        pthread_mutex_unlock(&s_mutex);
    }
    return NULL;
}

void demo_tcp_run(int port, const demo_tcp_hooks_t* hooks)
{
    s_hooks = hooks;

    int server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd < 0) { perror("socket"); return; }

    int opt = 1;
    setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family      = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port        = htons((uint16_t)port);

    if (bind(server_fd, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        perror("bind"); close(server_fd); return;
    }
    if (listen(server_fd, 2) < 0) {
        perror("listen"); close(server_fd); return;
    }

    pthread_t tid_timer;
    pthread_create(&tid_timer, NULL, timer_thread, NULL);
    pthread_detach(tid_timer);

    while (1) {
        struct sockaddr_in cli_addr;
        socklen_t          cli_len = sizeof(cli_addr);
        int fd = accept(server_fd, (struct sockaddr*)&cli_addr, &cli_len);
        if (fd < 0) { perror("accept"); continue; }

        char ip[INET_ADDRSTRLEN];
        inet_ntop(AF_INET, &cli_addr.sin_addr, ip, sizeof(ip));

        pthread_mutex_lock(&s_mutex);
        int already = (s_fd >= 0);
        pthread_mutex_unlock(&s_mutex);

        if (already) {
            printf("[CONN] %s rejected — only one client supported.\n", ip);
            close(fd);
            continue;
        }

        printf("[CONN] %s connected.\n", ip);
        pthread_mutex_lock(&s_mutex);
        s_fd = fd;
        if (s_hooks->on_connect) s_hooks->on_connect();
        pthread_mutex_unlock(&s_mutex);

        pthread_t tid_recv;
        pthread_create(&tid_recv, NULL, recv_thread, NULL);
        pthread_detach(tid_recv);
    }
}
