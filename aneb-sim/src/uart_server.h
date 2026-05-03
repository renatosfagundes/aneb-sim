/*
 * uart_server.h — per-chip TCP UART server.
 *
 * Exposes each simulated chip's UART as TWO raw-byte TCP sockets on
 * localhost:
 *   8600..8604  — flasher port.  Connecting triggers chip_reset (Arduino
 *                 DTR-pulse emulation) so avrdude can sync with Optiboot.
 *                 The chip console suppresses its JSON UART events while
 *                 a flasher client is attached — STK500 traffic is binary
 *                 garbage that would flood the UI.
 *   8700..8704  — bridge port.  Passive subscriber: no reset, no JSON
 *                 suppression.  Used by the aneb-sim UI's UART bridge
 *                 to forward chip UART to virtual COM ports without
 *                 disturbing the running firmware on every reconnect.
 *
 * Both ports share the same single client slot per chip — a new connect
 * on either port preempts the previous client.
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

#define UART_SERVER_BASE_PORT          8600   /* flasher port */
#define UART_SERVER_BRIDGE_BASE_PORT   8700   /* bridge / passive port */

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
 * Returns true (once) if a "flasher" client (i.e. a client that kicked the
 * previous client when it connected — typically avrdude) just disconnected.
 * The chip thread uses this to trigger a soft reset with MCUSR=0 so Optiboot
 * exits its sync-wait loop and jumps into the freshly programmed user app.
 */
bool uart_server_pop_flash_done(int idx);

/*
 * Cheap atomic check: is a TCP client currently attached to chip `idx`?
 */
bool uart_server_has_client(int idx);

/*
 * Returns true only if the currently-attached client kicked a previous
 * client to grab the slot (i.e. avrdude or another flasher).  Passive
 * connects into an empty slot (typical bridge reconnect) return false.
 *
 * on_uart_byte uses this to suppress the JSON-Lines UART path during a
 * flash session — STK500 traffic is binary garbage and emitting one
 * event per byte floods the UI's serial console for ~10 K bytes back
 * to back.  When a passive bridge is the client, JSON emission stays
 * on so the user sees UART output in both the aneb-sim chip console
 * AND any external tool reading the bridge's COM port.
 */
bool uart_server_client_is_flasher(int idx);

/*
 * Return the TCP port number for chip `idx`.
 */
int  uart_server_port(int idx);

/*
 * Stop all server threads and release sockets.  Call in sim_loop_shutdown().
 */
void uart_server_shutdown(void);

#endif /* ANEB_UART_SERVER_H */
