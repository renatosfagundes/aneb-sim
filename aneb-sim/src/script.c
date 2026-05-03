#include "script.h"

#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "cJSON.h"
#include "cmd.h"
#include "proto.h"
#include "sim_loop.h"

typedef struct {
    char path[260];
} script_args_t;

static long elapsed_ms_since(const struct timespec *start)
{
    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);
    return (long)(now.tv_sec  - start->tv_sec ) * 1000L
         + (long)(now.tv_nsec - start->tv_nsec) / 1000000L;
}

static void sleep_ms(long ms)
{
    if (ms <= 0) return;
    struct timespec ts;
    ts.tv_sec  = ms / 1000;
    ts.tv_nsec = (ms % 1000) * 1000000L;
    while (nanosleep(&ts, &ts) == -1) {
        /* interrupted — finish the remaining sleep */
    }
}

static void *script_thread(void *arg)
{
    script_args_t *sa = (script_args_t *)arg;
    FILE *f = fopen(sa->path, "r");
    if (!f) {
        proto_emit_log("error", "script: cannot open '%s'", sa->path);
        free(sa);
        return NULL;
    }

    struct timespec start;
    clock_gettime(CLOCK_MONOTONIC, &start);
    proto_emit_log("info", "script: started '%s'", sa->path);

    int line_no = 0;
    int events  = 0;
    char line[8192];
    while (fgets(line, sizeof(line), f) && !sim_loop_should_stop()) {
        line_no++;
        /* strip trailing newline */
        size_t n = strlen(line);
        while (n > 0 && (line[n - 1] == '\n' || line[n - 1] == '\r')) {
            line[--n] = '\0';
        }
        /* skip blanks and comments */
        if (n == 0)                  continue;
        if (line[0] == '#')          continue;
        if (n >= 2 && line[0] == '/' && line[1] == '/') continue;

        /* Pull at_ms out of the JSON without copying the line — we hand
         * the same string to proto_parse_command afterwards because its
         * parser ignores unknown fields like "at_ms". */
        cJSON *root = cJSON_Parse(line);
        if (!root) {
            proto_emit_log("warn", "script: line %d parse error", line_no);
            continue;
        }
        cJSON *at = cJSON_GetObjectItem(root, "at_ms");
        long at_ms = (at && cJSON_IsNumber(at)) ? (long)at->valuedouble : 0;
        cJSON_Delete(root);

        /* Sleep until at_ms past the script's start time.  If the
         * scheduled time has already passed (the previous command took
         * longer than expected) we run immediately. */
        long wait_ms = at_ms - elapsed_ms_since(&start);
        if (wait_ms > 0) sleep_ms(wait_ms);
        if (sim_loop_should_stop()) break;

        cmd_t cmd;
        int rc = proto_parse_command(line, &cmd);
        if (rc != 0) {
            proto_emit_log("warn",
                           "script: line %d cmd parse error rc=%d", line_no, rc);
            continue;
        }
        if (cmd_queue_push(&cmd) != 0) {
            proto_emit_log("warn",
                           "script: line %d dropped (queue full)", line_no);
            proto_free_command(&cmd);
            continue;
        }
        events++;
    }
    fclose(f);
    proto_emit_log("info",
                   "script: finished '%s' (%d events from %d lines)",
                   sa->path, events, line_no);
    free(sa);
    return NULL;
}

int script_run_async(const char *path)
{
    if (!path || !*path) return -1;
    script_args_t *sa = (script_args_t *)malloc(sizeof(*sa));
    if (!sa) return -1;
    snprintf(sa->path, sizeof(sa->path), "%s", path);

    pthread_t tid;
    if (pthread_create(&tid, NULL, script_thread, sa) != 0) {
        free(sa);
        return -1;
    }
    pthread_detach(tid);
    return 0;
}
