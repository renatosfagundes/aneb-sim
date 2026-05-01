/*
 * uart_server.h — per-chip TCP UART server.
 *
 * Exposes each simulated chip's UART as a raw-byte TCP socket on
 * localhost:8600, 8601, ... (one port per chip index).
 *
 * External tools (avrdude net:localhost:860x, PuTTY telnet, pyserial
 * socket://) can connect directly; a Python bridge can optionally
 * bridge those sockets to virtual COM ports (see uart_bridge.py).
 *
 * Locking contract:
 *   push_tx()  — called from on_uart_byte() inside avr_lock; safe to take
 *                its own tx.lock because the server thread never takes avr_lock.
 *   flush_tx() — called from chip thread AFTER releasing avr_lock; takes
 *                tx.lock then client_lock (always in that order).
 *   pop_rx()   — called from chip thread INSIDE avr_lock; takes rx.lock only.
 *   Server thread: takes client_lock (on connect), then rx.lock (on recv).
 *                  Never takes avr_lock — no deadlock possible.
 */
#ifndef ANEB_UART_SERVER_H
#define ANEB_UART_SERVER_H

#include <stdbool.h>
#include <stdint.h>

#define UART_SERVER_BASE_PORT  8600

/*
 * Initialize listening sockets for `nchips` chips.
 * Must be called before sim_loop_start().  Returns 0 on success.
 */
int  uart_server_init(int nchips);

/*
 * Spawn one accept/recv thread per chip.  Call after init().
 */
int  uart_server_start(void);

/*
 * Push one UART TX byte into the per-chip buffer.
 * Called from on_uart_byte() inside avr_lock.
 */
void uart_server_push_tx(int idx, uint8_t byte);

/*
 * Flush buffered TX bytes to the connected TCP client (if any).
 * Called from chip_thread_fn() AFTER releasing avr_lock.
 * No-op if no client is connected.
 */
void uart_server_flush_tx(int idx);

/*
 * Pop one byte from the per-chip TCP RX queue.
 * Called from chip_thread_fn() INSIDE avr_lock.
 * Returns true if a byte was available, false if the queue is empty.
 */
bool uart_server_pop_rx(int idx, uint8_t *out);

/*
 * Returns true (once) if a new TCP client connected since the last call.
 * The chip thread uses this to trigger a chip reset (Optiboot DTR emulation).
 */
bool uart_server_pop_connect(int idx);

/*
 * Cheap atomic check: is a TCP client currently attached to chip `idx`?
 * Used by on_uart_byte to skip the JSON-Lines emit when avrdude or another
 * raw-protocol client is consuming the UART — those bytes are binary
 * STK500 frames, not human-readable, and emitting one event per byte
 * floods the UI's serial console (~10K events during a flash+verify).
 */
bool uart_server_has_client(int idx);

/*
 * Return the TCP port number for chip `idx`.
 */
int  uart_server_port(int idx);

/*
 * Stop all server threads and release sockets.  Call in sim_loop_shutdown().
 */
void uart_server_shutdown(void);

#endif /* ANEB_UART_SERVER_H */
