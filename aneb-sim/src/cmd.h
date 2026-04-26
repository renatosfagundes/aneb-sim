/*
 * cmd.h — apply a parsed command_t to the simulation state.
 *
 * The stdin reader thread parses JSON lines, then enqueues commands.
 * The main loop drains the queue between sim ticks and dispatches via
 * cmd_apply(). Queue is bounded; new commands are dropped (with a log
 * event) when full.
 */
#ifndef ANEB_CMD_H
#define ANEB_CMD_H

#include "proto.h"

#define CMD_QUEUE_CAP 256

/*
 * Initialize the command queue. Call once at startup.
 */
void cmd_queue_init(void);

/*
 * Push a command onto the queue. Takes ownership of cmd->data.
 * Returns 0 on success, -1 if full.
 */
int  cmd_queue_push(const cmd_t *cmd);

/*
 * Pop one command from the queue into *out. Returns 0 on success,
 * -1 if empty. Caller owns out->data and must call proto_free_command().
 */
int  cmd_queue_pop(cmd_t *out);

/*
 * Apply a command to the simulation state. Caller-owned cmd is consumed
 * by this call (data is freed on the caller's behalf).
 */
void cmd_apply(cmd_t *cmd);

#endif
