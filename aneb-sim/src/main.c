/*
 * aneb-sim — engine entry point (M1).
 *
 * Boots the canonical 5-chip ANEB roster, optionally pre-loads firmware
 * via CLI flags, then enters the main loop:
 *
 *   1. drain the inbound command queue (filled by the stdin reader thread)
 *   2. tick the simulation forward by one quantum
 *   3. emit any events generated during the tick
 *
 * CLI flags:
 *   --ecu1=PATH     load PATH into ecu1 at startup
 *   --ecu2=PATH     ... ecu2
 *   --ecu3=PATH
 *   --ecu4=PATH
 *   --mcu=PATH      load PATH into the MCU controller
 *   --no-mcu        skip MCU init (useful for tests that don't need it)
 *
 * Once started, the engine reads JSON-Lines commands from stdin and
 * writes JSON-Lines events to stdout. See docs/PROTOCOL.md.
 */
#include <pthread.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "cmd.h"
#include "proto.h"
#include "sim_loop.h"

/* ----- stdin reader thread -------------------------------------------- */

static void *stdin_reader(void *arg)
{
    (void)arg;
    char line[8192];
    while (fgets(line, sizeof(line), stdin)) {
        /* Strip trailing newline if present. */
        size_t n = strlen(line);
        while (n > 0 && (line[n - 1] == '\n' || line[n - 1] == '\r')) {
            line[--n] = '\0';
        }
        if (n == 0) continue;

        cmd_t cmd;
        int rc = proto_parse_command(line, &cmd);
        if (rc != 0) {
            proto_emit_log("warn", "parse error rc=%d on line: %s", rc, line);
            continue;
        }
        if (cmd_queue_push(&cmd) != 0) {
            proto_emit_log("warn", "command queue full, dropping cmd");
            proto_free_command(&cmd);
        }
    }
    sim_loop_request_stop();
    return NULL;
}

/* ----- signal handling ------------------------------------------------- */

static void on_sigint(int sig)
{
    (void)sig;
    sim_loop_request_stop();
}

/* ----- CLI arg handling ----------------------------------------------- */

static const char *flag_value(const char *arg, const char *flag)
{
    size_t n = strlen(flag);
    if (strncmp(arg, flag, n) != 0) return NULL;
    if (arg[n] != '=') return NULL;
    return arg + n + 1;
}

int main(int argc, char **argv)
{
    if (sim_loop_init() != 0) {
        fprintf(stderr, "fatal: sim_loop_init failed\n");
        return 1;
    }
    cmd_queue_init();

    /* Pre-load firmware specified on the command line. */
    for (int i = 1; i < argc; i++) {
        const char *v;
        if      ((v = flag_value(argv[i], "--ecu1"))) chip_load_hex(sim_loop_find("ecu1"), v);
        else if ((v = flag_value(argv[i], "--ecu2"))) chip_load_hex(sim_loop_find("ecu2"), v);
        else if ((v = flag_value(argv[i], "--ecu3"))) chip_load_hex(sim_loop_find("ecu3"), v);
        else if ((v = flag_value(argv[i], "--ecu4"))) chip_load_hex(sim_loop_find("ecu4"), v);
        else if ((v = flag_value(argv[i], "--mcu")))  chip_load_hex(sim_loop_find("mcu"),  v);
        else if (strcmp(argv[i], "--no-mcu") == 0) {
            /* leave mcu uninitialized; harmless because chip_step
             * returns immediately when running == false. */
        }
        else {
            fprintf(stderr, "warning: unknown arg '%s'\n", argv[i]);
        }
    }

    signal(SIGINT, on_sigint);

    pthread_t reader_tid;
    if (pthread_create(&reader_tid, NULL, stdin_reader, NULL) != 0) {
        fprintf(stderr, "fatal: failed to start stdin reader\n");
        return 1;
    }
    pthread_detach(reader_tid);

    proto_emit_log("info", "aneb-sim ready (proto v%d)", ANEB_PROTO_VERSION);

    while (!sim_loop_should_stop()) {
        cmd_t cmd;
        while (cmd_queue_pop(&cmd) == 0) {
            cmd_apply(&cmd);
        }
        if (!sim_loop_tick()) {
            /* No active chips. Don't busy-spin — wait briefly for a load
             * command from the UI, then re-check. */
            struct timespec ts = {.tv_sec = 0, .tv_nsec = 1000000};  /* 1 ms */
            nanosleep(&ts, NULL);
        }
    }

    proto_emit_log("info", "aneb-sim shutting down");
    sim_loop_shutdown();
    return 0;
}
