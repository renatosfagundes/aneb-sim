#include "uart_server.h"

#include <pthread.h>
#include <stdatomic.h>
#include <stdio.h>
#include <string.h>

#include <time.h>

#ifdef _WIN32
#  include <winsock2.h>
#  include <ws2tcpip.h>
typedef SOCKET      sock_t;
#  define SOCK_NONE     INVALID_SOCKET
#  define sock_close(s) closesocket(s)
#  define sock_err()    WSAGetLastError()
#  define EAGAIN_VAL    WSAEWOULDBLOCK
#else
#  include <sys/socket.h>
#  include <netinet/in.h>
#  include <arpa/inet.h>
#  include <unistd.h>
#  include <errno.h>
typedef int         sock_t;
#  define SOCK_NONE     (-1)
#  define sock_close(s) close(s)
#  define sock_err()    errno
#  define EAGAIN_VAL    EAGAIN
#endif

/* ---------- ring buffer --------------------------------------------------- */

#define BUF_CAP 4096

typedef struct {
    uint8_t         data[BUF_CAP];
    int             head, tail, count;
    pthread_mutex_t lock;
} ring_t;

static void ring_push(ring_t *r, uint8_t b)
{
    if (r->count < BUF_CAP) {
        r->data[r->tail] = b;
        r->tail = (r->tail + 1) % BUF_CAP;
        r->count++;
    }
    /* silently drop on overflow — burst that overwhelms a 4 KB buffer is
     * a protocol error on the client side */
}

static bool ring_pop(ring_t *r, uint8_t *out)
{
    if (r->count == 0) return false;
    *out = r->data[r->head];
    r->head = (r->head + 1) % BUF_CAP;
    r->count--;
    return true;
}

/* Drain up to `max` bytes into `dst`; returns actual count. */
static int ring_drain(ring_t *r, uint8_t *dst, int max)
{
    int n = 0;
    while (n < max && r->count > 0) {
        dst[n++] = r->data[r->head];
        r->head  = (r->head + 1) % BUF_CAP;
        r->count--;
    }
    return n;
}

/* ---------- per-chip state ------------------------------------------------ */

typedef struct {
    sock_t          listen_flash;   /* 8600+i — avrdude/flasher clients */
    sock_t          listen_bridge;  /* 8700+i — passive UART subscribers */
    sock_t          client_sock;
    pthread_mutex_t client_lock;
    ring_t          tx;
    ring_t          rx;
    int             flash_port;
    int             bridge_port;
    atomic_bool     new_connect;  /* set on flasher-port accept, cleared by pop_connect */
    atomic_bool     client_attached; /* true while a TCP client is connected */
    atomic_bool     flash_done;   /* set on flasher-client disconnect (avrdude
                                   * finished), cleared by pop_flash_done.
                                   * Triggers a soft reset (MCUSR=0) so the
                                   * chip drops out of Optiboot into the
                                   * just-flashed user app. */
    bool            client_is_flasher; /* whether the active client connected
                                   * via the flasher port (8600+i).  Read +
                                   * written only on this server thread. */
    pthread_t       thread;

    /* STK_GET_SYNC coalescing.  avrdude on Windows TCP can't drain the
     * recv buffer (tcflush/PurgeComm fail on a socket), so when its sync
     * handshake sends `30 20` three times in 1 ms before the bootloader's
     * first reply lands, the chip dutifully replies three times and the
     * 4-byte leftover desyncs every subsequent command.  We drop the
     * extra `30 20` recv batches that arrive within COALESCE_NS of the
     * previous one so the bootloader replies exactly once.
     *
     * We only match exact 2-byte `30 20` recv batches, never look for
     * the pattern inside larger payloads — flash programming pages are
     * raw firmware bytes that may contain 0x30,0x20 by coincidence. */
    int64_t         last_sync_ns;
} chip_uart_t;

static int64_t now_ns(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (int64_t)ts.tv_sec * 1000000000LL + ts.tv_nsec;
}

#define SYNC_COALESCE_NS  100000000LL   /* 100 ms */

#define MAX_CHIPS 5
static chip_uart_t  g_uart[MAX_CHIPS];
static int          g_nchips = 0;
static atomic_bool  g_stop   = false;

/* ---------- server thread ------------------------------------------------- */

/* Reset both rings + sync coalescer when a fresh client takes over.
 * Stale firmware UART output across a client switchover breaks the
 * next client's STK500 sync handshake, so we drain on accept. */
static void reset_streams(chip_uart_t *u)
{
    pthread_mutex_lock(&u->tx.lock);
    u->tx.head = u->tx.tail = u->tx.count = 0;
    pthread_mutex_unlock(&u->tx.lock);

    pthread_mutex_lock(&u->rx.lock);
    u->rx.head = u->rx.tail = u->rx.count = 0;
    pthread_mutex_unlock(&u->rx.lock);

    u->last_sync_ns = 0;
}

/* Accept a new client on the given listen socket and install it as the
 * single active client for the chip, evicting any previous client.
 * `is_flasher` distinguishes flasher port (8600+i) from bridge port
 * (8700+i) connections.  Returns true if a new client was accepted. */
static bool accept_new_client(chip_uart_t *u, int idx, sock_t listen_sock,
                              bool is_flasher)
{
    struct sockaddr_in sa;
    int salen = (int)sizeof(sa);
    sock_t newc = accept(listen_sock, (struct sockaddr *)&sa, &salen);
    if (newc == SOCK_NONE) return false;

    bool kicked;
    pthread_mutex_lock(&u->client_lock);
    kicked = (u->client_sock != SOCK_NONE);
    if (kicked) {
        sock_close(u->client_sock);
        fprintf(stderr,
                "[uart:%d] kicked previous client (new connect)\n", idx);
    }
    u->client_sock = newc;
    pthread_mutex_unlock(&u->client_lock);

    reset_streams(u);
    /* DTR-pulse emulation (chip_reset) is ONLY for flasher connects.
     * A bridge connect — even if it happens to kick a stale flasher —
     * should not reset the firmware that's running on the chip. */
    if (is_flasher) {
        atomic_store(&u->new_connect, true);
    }
    u->client_is_flasher = is_flasher;
    atomic_store(&u->client_attached, true);
    fprintf(stderr, "[uart:%d] %s client connected on port %d%s\n",
            idx, is_flasher ? "flasher" : "bridge",
            is_flasher ? u->flash_port : u->bridge_port,
            kicked ? " (kicked previous)" : "");
    return true;
}

static void *uart_server_thread(void *arg)
{
    int          idx = (int)(intptr_t)arg;
    chip_uart_t *u   = &g_uart[idx];

    /* Loop polls BOTH listen sockets (flasher + bridge) and the active
     * client socket so a new connection on either port preempts the
     * current client. */
    while (!atomic_load(&g_stop)) {
        fd_set fds;
        FD_ZERO(&fds);
        FD_SET(u->listen_flash,  &fds);
        FD_SET(u->listen_bridge, &fds);
        sock_t client = u->client_sock;        /* snapshot — only this thread mutates it */
        if (client != SOCK_NONE) FD_SET(client, &fds);

        sock_t maxfd = u->listen_flash;
        if (u->listen_bridge > maxfd) maxfd = u->listen_bridge;
        if (client != SOCK_NONE && client > maxfd) maxfd = client;

        struct timeval tv = {0, 100000};       /* 100 ms */
        int r = select((int)maxfd + 1, &fds, NULL, NULL, &tv);
        if (r <= 0) continue;                  /* timeout or error */

        /* Flasher-port accept (avrdude / remote_flasher TCP). */
        if (FD_ISSET(u->listen_flash, &fds)) {
            if (accept_new_client(u, idx, u->listen_flash, /*is_flasher=*/true)) {
                continue;  /* skip stale `client` snapshot this iteration */
            }
        }
        /* Bridge-port accept (passive UART subscriber). */
        if (FD_ISSET(u->listen_bridge, &fds)) {
            if (accept_new_client(u, idx, u->listen_bridge, /*is_flasher=*/false)) {
                continue;
            }
        }

        /* Client-side activity: drain bytes from the connected client. */
        if (client != SOCK_NONE && FD_ISSET(client, &fds)) {
            uint8_t buf[256];
            int n = recv(client, (char *)buf, (int)sizeof(buf), 0);
            if (n > 0) {
                /* STK_GET_SYNC coalescing.  Avrdude sends each sync as its
                 * own write() (each arriving as a separate 2-byte recv),
                 * so we only match exact 2-byte `30 20` batches and drop
                 * those that follow a previous sync within the window.
                 * Larger payloads — including PROG_PAGE flash data, which
                 * can legitimately contain 0x30 followed by 0x20 — pass
                 * through unchanged. */
                int drop = 0;
                if (n == 2 && buf[0] == 0x30 && buf[1] == 0x20) {
                    int64_t now = now_ns();
                    if (now - u->last_sync_ns < SYNC_COALESCE_NS) {
                        drop = 1;
                    } else {
                        u->last_sync_ns = now;
                    }
                }
                if (!drop) {
                    pthread_mutex_lock(&u->rx.lock);
                    for (int i = 0; i < n; i++) ring_push(&u->rx, buf[i]);
                    pthread_mutex_unlock(&u->rx.lock);
                }
            } else if (n == 0) {
                /* graceful close */
                fprintf(stderr, "[uart:%d] client disconnected%s\n",
                        idx, u->client_is_flasher ? " (was flasher)" : "");
                atomic_store(&u->client_attached, false);
                if (u->client_is_flasher) {
                    /* Avrdude (or another flasher) just hung up.  Tell the
                     * chip thread to soft-reset the AVR so Optiboot exits
                     * its sync-wait loop into the freshly programmed app
                     * — a chip_reset() with MCUSR=0 (no EXTRF). */
                    atomic_store(&u->flash_done, true);
                    u->client_is_flasher = false;
                }
                pthread_mutex_lock(&u->client_lock);
                if (u->client_sock != SOCK_NONE) {
                    sock_close(u->client_sock);
                    u->client_sock = SOCK_NONE;
                }
                pthread_mutex_unlock(&u->client_lock);
            } else {
                int e = sock_err();
                if (e == EAGAIN_VAL
#ifdef _WIN32
                    || e == WSAEINTR
#else
                    || e == EINTR
#endif
                ) {
                    /* transient — leave the socket alone */
                } else {
                    fprintf(stderr,
                            "[uart:%d] recv error %d, dropping client%s\n",
                            idx, e,
                            u->client_is_flasher ? " (was flasher)" : "");
                    atomic_store(&u->client_attached, false);
                    if (u->client_is_flasher) {
                        atomic_store(&u->flash_done, true);
                        u->client_is_flasher = false;
                    }
                    pthread_mutex_lock(&u->client_lock);
                    if (u->client_sock != SOCK_NONE) {
                        sock_close(u->client_sock);
                        u->client_sock = SOCK_NONE;
                    }
                    pthread_mutex_unlock(&u->client_lock);
                }
            }
        }
    }
    return NULL;
}

/* ---------- public API ---------------------------------------------------- */

int uart_server_init(int nchips)
{
    if (nchips > MAX_CHIPS) nchips = MAX_CHIPS;
    g_nchips = nchips;
    atomic_store(&g_stop, false);

#ifdef _WIN32
    WSADATA wd;
    if (WSAStartup(MAKEWORD(2, 2), &wd) != 0) {
        fprintf(stderr, "[uart_server] WSAStartup failed\n");
        return -1;
    }
#endif

    for (int i = 0; i < nchips; i++) {
        chip_uart_t *u = &g_uart[i];
        memset(u, 0, sizeof(*u));
        atomic_store(&u->flash_done, false);
        u->client_is_flasher = false;
        u->listen_flash  = SOCK_NONE;
        u->listen_bridge = SOCK_NONE;
        u->client_sock   = SOCK_NONE;
        u->flash_port    = UART_SERVER_BASE_PORT        + i;
        u->bridge_port   = UART_SERVER_BRIDGE_BASE_PORT + i;
        atomic_store(&u->new_connect, false);
        atomic_store(&u->client_attached, false);
        pthread_mutex_init(&u->client_lock, NULL);
        pthread_mutex_init(&u->tx.lock,     NULL);
        pthread_mutex_init(&u->rx.lock,     NULL);

        struct {
            sock_t *sock;
            int     port;
            const char *name;
        } sockets[] = {
            { &u->listen_flash,  u->flash_port,  "flasher" },
            { &u->listen_bridge, u->bridge_port, "bridge"  },
        };

        for (size_t s = 0; s < sizeof(sockets)/sizeof(*sockets); s++) {
            *sockets[s].sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
            if (*sockets[s].sock == SOCK_NONE) {
                fprintf(stderr,
                        "[uart_server] socket() failed for chip %d (%s)\n",
                        i, sockets[s].name);
                return -1;
            }

            int opt = 1;
            setsockopt(*sockets[s].sock, SOL_SOCKET, SO_REUSEADDR,
                       (const char *)&opt, sizeof(opt));

            struct sockaddr_in sa;
            memset(&sa, 0, sizeof(sa));
            sa.sin_family      = AF_INET;
            sa.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
            sa.sin_port        = htons((uint16_t)sockets[s].port);

            if (bind(*sockets[s].sock, (struct sockaddr *)&sa, sizeof(sa)) != 0) {
                fprintf(stderr,
                        "[uart_server] bind() failed on port %d (%s)\n",
                        sockets[s].port, sockets[s].name);
                return -1;
            }
            if (listen(*sockets[s].sock, 1) != 0) {
                fprintf(stderr,
                        "[uart_server] listen() failed on port %d (%s)\n",
                        sockets[s].port, sockets[s].name);
                return -1;
            }
        }
        fprintf(stderr, "[uart_server] chip %d UART: flash=%d bridge=%d\n",
                i, u->flash_port, u->bridge_port);
    }
    return 0;
}

int uart_server_start(void)
{
    for (int i = 0; i < g_nchips; i++) {
        if (pthread_create(&g_uart[i].thread, NULL, uart_server_thread,
                           (void *)(intptr_t)i) != 0) {
            fprintf(stderr, "[uart_server] pthread_create failed for chip %d\n", i);
            return -1;
        }
    }
    return 0;
}

void uart_server_push_tx(int idx, uint8_t byte)
{
    if (idx < 0 || idx >= g_nchips) return;
    chip_uart_t *u = &g_uart[idx];
    pthread_mutex_lock(&u->tx.lock);
    ring_push(&u->tx, byte);
    pthread_mutex_unlock(&u->tx.lock);
}

void uart_server_flush_tx(int idx)
{
    if (idx < 0 || idx >= g_nchips) return;
    chip_uart_t *u = &g_uart[idx];

    pthread_mutex_lock(&u->tx.lock);
    if (u->tx.count == 0) {
        pthread_mutex_unlock(&u->tx.lock);
        return;
    }
    uint8_t buf[BUF_CAP];
    int n = ring_drain(&u->tx, buf, BUF_CAP);
    pthread_mutex_unlock(&u->tx.lock);

    pthread_mutex_lock(&u->client_lock);
    sock_t client = u->client_sock;
    if (client != SOCK_NONE) {
        /* best-effort; ignore partial sends */
        send(client, (const char *)buf, n, 0);
    }
    pthread_mutex_unlock(&u->client_lock);
}

bool uart_server_pop_rx(int idx, uint8_t *out)
{
    if (idx < 0 || idx >= g_nchips) return false;
    chip_uart_t *u = &g_uart[idx];
    pthread_mutex_lock(&u->rx.lock);
    bool ok = ring_pop(&u->rx, out);
    pthread_mutex_unlock(&u->rx.lock);
    return ok;
}

bool uart_server_pop_connect(int idx)
{
    if (idx < 0 || idx >= g_nchips) return false;
    return atomic_exchange(&g_uart[idx].new_connect, false);
}

bool uart_server_pop_flash_done(int idx)
{
    if (idx < 0 || idx >= g_nchips) return false;
    return atomic_exchange(&g_uart[idx].flash_done, false);
}

bool uart_server_has_client(int idx)
{
    if (idx < 0 || idx >= g_nchips) return false;
    return atomic_load(&g_uart[idx].client_attached);
}

bool uart_server_client_is_flasher(int idx)
{
    if (idx < 0 || idx >= g_nchips) return false;
    /* client_is_flasher is only mutated on the server thread between
     * accept() and disconnect; reading it from the chip thread here is
     * a benign data race — worst case we see one byte's worth of stale
     * value during the millisecond around a connect/disconnect. */
    return atomic_load(&g_uart[idx].client_attached)
           && g_uart[idx].client_is_flasher;
}

int uart_server_port(int idx)
{
    if (idx < 0 || idx >= g_nchips) return -1;
    return g_uart[idx].flash_port;
}

void uart_server_shutdown(void)
{
    atomic_store(&g_stop, true);

    /* closing the listen sockets wakes select() in each thread */
    for (int i = 0; i < g_nchips; i++) {
        chip_uart_t *u = &g_uart[i];
        if (u->listen_flash != SOCK_NONE) {
            sock_close(u->listen_flash);
            u->listen_flash = SOCK_NONE;
        }
        if (u->listen_bridge != SOCK_NONE) {
            sock_close(u->listen_bridge);
            u->listen_bridge = SOCK_NONE;
        }
        pthread_mutex_lock(&u->client_lock);
        if (u->client_sock != SOCK_NONE) {
            sock_close(u->client_sock);
            u->client_sock = SOCK_NONE;
        }
        pthread_mutex_unlock(&u->client_lock);
    }

    for (int i = 0; i < g_nchips; i++) {
        pthread_join(g_uart[i].thread, NULL);
        pthread_mutex_destroy(&g_uart[i].client_lock);
        pthread_mutex_destroy(&g_uart[i].tx.lock);
        pthread_mutex_destroy(&g_uart[i].rx.lock);
    }

#ifdef _WIN32
    WSACleanup();
#endif
}
