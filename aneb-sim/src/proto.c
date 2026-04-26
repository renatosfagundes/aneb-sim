/*
 * proto.c — JSON-Lines wire protocol implementation.
 *
 * Output side hand-rolls JSON to avoid allocating per event (hot path).
 * Input side uses cJSON since commands are infrequent and the parser
 * complexity is not worth re-implementing.
 *
 * Output is serialized through a single mutex; concurrent emits from the
 * stdin reader thread and main loop are safe.
 */
#include "proto.h"

#include <ctype.h>
#include <pthread.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cJSON.h"
#include "mcp2515.h"

static pthread_mutex_t g_out_mutex = PTHREAD_MUTEX_INITIALIZER;

/* ----- JSON string escaping (engine output, hot path) ------------------ */

static void emit_escaped(FILE *f, const char *s, size_t len)
{
    for (size_t i = 0; i < len; i++) {
        unsigned char c = (unsigned char)s[i];
        switch (c) {
        case '"':  fputs("\\\"", f); break;
        case '\\': fputs("\\\\", f); break;
        case '\b': fputs("\\b",  f); break;
        case '\f': fputs("\\f",  f); break;
        case '\n': fputs("\\n",  f); break;
        case '\r': fputs("\\r",  f); break;
        case '\t': fputs("\\t",  f); break;
        default:
            if (c < 0x20) fprintf(f, "\\u%04x", c);
            else          fputc(c, f);
        }
    }
}

static void emit_str(FILE *f, const char *s)
{
    emit_escaped(f, s, strlen(s));
}

void proto_emit_pin(const char *chip, const char *pin_name, int val, uint64_t ts)
{
    pthread_mutex_lock(&g_out_mutex);
    fprintf(stdout, "{\"v\":%d,\"t\":\"pin\",\"chip\":\"%s\",\"pin\":\"%s\",\"val\":%d,\"ts\":%llu}\n",
            ANEB_PROTO_VERSION, chip, pin_name, val ? 1 : 0,
            (unsigned long long)ts);
    fflush(stdout);
    pthread_mutex_unlock(&g_out_mutex);
}

void proto_emit_pwm(const char *chip, const char *pin_name, double duty, uint64_t ts)
{
    pthread_mutex_lock(&g_out_mutex);
    fprintf(stdout, "{\"v\":%d,\"t\":\"pwm\",\"chip\":\"%s\",\"pin\":\"%s\",\"duty\":%.4f,\"ts\":%llu}\n",
            ANEB_PROTO_VERSION, chip, pin_name, duty,
            (unsigned long long)ts);
    fflush(stdout);
    pthread_mutex_unlock(&g_out_mutex);
}

void proto_emit_uart(const char *chip, const uint8_t *data, size_t len, uint64_t ts)
{
    pthread_mutex_lock(&g_out_mutex);
    fprintf(stdout, "{\"v\":%d,\"t\":\"uart\",\"chip\":\"%s\",\"data\":\"",
            ANEB_PROTO_VERSION, chip);
    emit_escaped(stdout, (const char *)data, len);
    fprintf(stdout, "\",\"ts\":%llu}\n", (unsigned long long)ts);
    fflush(stdout);
    pthread_mutex_unlock(&g_out_mutex);
}

void proto_emit_can_state(const char *chip, uint8_t tec, uint8_t rec,
                          const char *state, uint64_t ts)
{
    pthread_mutex_lock(&g_out_mutex);
    fprintf(stdout,
            "{\"v\":%d,\"t\":\"can_state\",\"chip\":\"%s\","
            "\"tec\":%u,\"rec\":%u,\"state\":\"%s\",\"ts\":%llu}\n",
            ANEB_PROTO_VERSION, chip,
            (unsigned)tec, (unsigned)rec, state,
            (unsigned long long)ts);
    fflush(stdout);
    pthread_mutex_unlock(&g_out_mutex);
}

void proto_emit_can_tx(const char *src_chip, const char *bus,
                       const struct mcp2515_frame *f, uint64_t ts)
{
    if (!f) return;
    pthread_mutex_lock(&g_out_mutex);
    fprintf(stdout,
            "{\"v\":%d,\"t\":\"can_tx\",\"bus\":\"%s\",\"src\":\"%s\","
            "\"id\":\"0x%X\",\"ext\":%s,\"rtr\":%s,\"dlc\":%u,\"data\":\"",
            ANEB_PROTO_VERSION, bus, src_chip,
            (unsigned)f->id,
            f->ext ? "true" : "false",
            f->rtr ? "true" : "false",
            (unsigned)f->dlc);
    /* Hex-encode the data bytes (DLC is bounded to 8). */
    int n = f->dlc;
    if (n > 8) n = 8;
    for (int i = 0; i < n; i++) {
        fprintf(stdout, "%02X", f->data[i]);
    }
    fprintf(stdout, "\",\"ts\":%llu}\n", (unsigned long long)ts);
    fflush(stdout);
    pthread_mutex_unlock(&g_out_mutex);
}

void proto_emit_log(const char *level, const char *fmt, ...)
{
    char buf[1024];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);

    pthread_mutex_lock(&g_out_mutex);
    fprintf(stdout, "{\"v\":%d,\"t\":\"log\",\"level\":\"%s\",\"msg\":\"",
            ANEB_PROTO_VERSION, level);
    emit_str(stdout, buf);
    fprintf(stdout, "\"}\n");
    fflush(stdout);
    pthread_mutex_unlock(&g_out_mutex);
}

/* ----- Command parsing -------------------------------------------------- */

static cmd_type_t parse_cmd_type(const char *s)
{
    if (!s) return CMD_UNKNOWN;
    if (!strcmp(s, "din"))    return CMD_DIN;
    if (!strcmp(s, "adc"))    return CMD_ADC;
    if (!strcmp(s, "uart"))   return CMD_UART;
    if (!strcmp(s, "load"))   return CMD_LOAD;
    if (!strcmp(s, "reset"))  return CMD_RESET;
    if (!strcmp(s, "speed"))  return CMD_SPEED;
    if (!strcmp(s, "pause"))  return CMD_PAUSE;
    if (!strcmp(s, "resume")) return CMD_RESUME;
    if (!strcmp(s, "step"))         return CMD_STEP;
    if (!strcmp(s, "can_inject"))   return CMD_CAN_INJECT;
    if (!strcmp(s, "force_busoff")) return CMD_FORCE_BUSOFF;
    if (!strcmp(s, "can_errors"))   return CMD_CAN_ERRORS;
    if (!strcmp(s, "can_recover"))  return CMD_CAN_RECOVER;
    return CMD_UNKNOWN;
}

static void copy_str_field(const cJSON *obj, const char *key, char *out, size_t cap)
{
    out[0] = '\0';
    const cJSON *node = cJSON_GetObjectItemCaseSensitive(obj, key);
    if (cJSON_IsString(node) && node->valuestring) {
        strncpy(out, node->valuestring, cap - 1);
        out[cap - 1] = '\0';
    }
}

int proto_parse_command(const char *line, cmd_t *out)
{
    if (!line || !out) return -1;
    memset(out, 0, sizeof(*out));

    cJSON *root = cJSON_Parse(line);
    if (!root) return -2;

    /* Optional version check — accept v=1 or absent. */
    const cJSON *v = cJSON_GetObjectItemCaseSensitive(root, "v");
    if (cJSON_IsNumber(v) && v->valueint != ANEB_PROTO_VERSION) {
        cJSON_Delete(root);
        return -3;
    }

    const cJSON *c = cJSON_GetObjectItemCaseSensitive(root, "c");
    if (!cJSON_IsString(c) || !c->valuestring) {
        cJSON_Delete(root);
        return -4;
    }
    out->type = parse_cmd_type(c->valuestring);
    if (out->type == CMD_UNKNOWN) {
        cJSON_Delete(root);
        return -5;
    }

    copy_str_field(root, "chip", out->chip, sizeof(out->chip));
    copy_str_field(root, "pin",  out->pin,  sizeof(out->pin));
    copy_str_field(root, "path", out->path, sizeof(out->path));
    copy_str_field(root, "bus",  out->bus,  sizeof(out->bus));

    const cJSON *val = cJSON_GetObjectItemCaseSensitive(root, "val");
    if (cJSON_IsNumber(val)) out->val = val->valueint;

    const cJSON *ch = cJSON_GetObjectItemCaseSensitive(root, "ch");
    if (cJSON_IsNumber(ch)) out->channel = ch->valueint;

    const cJSON *speed = cJSON_GetObjectItemCaseSensitive(root, "speed");
    if (cJSON_IsNumber(speed)) out->speed = speed->valuedouble;

    const cJSON *cycles = cJSON_GetObjectItemCaseSensitive(root, "cycles");
    if (cJSON_IsNumber(cycles)) out->cycles = (uint64_t)cycles->valuedouble;

    /* CAN-frame fields (CMD_CAN_INJECT). `id` may be either a number or a
     * "0x..." string for human-friendly hex. */
    const cJSON *can_id = cJSON_GetObjectItemCaseSensitive(root, "id");
    if (cJSON_IsNumber(can_id)) {
        out->can_id = (uint32_t)can_id->valuedouble;
    } else if (cJSON_IsString(can_id) && can_id->valuestring) {
        out->can_id = (uint32_t)strtoul(can_id->valuestring, NULL, 0);
    }
    const cJSON *can_ext = cJSON_GetObjectItemCaseSensitive(root, "ext");
    if (cJSON_IsBool(can_ext))   out->can_ext = cJSON_IsTrue(can_ext);
    const cJSON *can_rtr = cJSON_GetObjectItemCaseSensitive(root, "rtr");
    if (cJSON_IsBool(can_rtr))   out->can_rtr = cJSON_IsTrue(can_rtr);
    const cJSON *can_dlc = cJSON_GetObjectItemCaseSensitive(root, "dlc");
    if (cJSON_IsNumber(can_dlc)) out->can_dlc = (uint8_t)can_dlc->valueint;

    /* Error-injection fields (CMD_CAN_ERRORS). */
    const cJSON *etx = cJSON_GetObjectItemCaseSensitive(root, "tx");
    if (cJSON_IsNumber(etx)) out->err_tx = etx->valueint;
    const cJSON *erx = cJSON_GetObjectItemCaseSensitive(root, "rx");
    if (cJSON_IsNumber(erx)) out->err_rx = erx->valueint;

    const cJSON *data = cJSON_GetObjectItemCaseSensitive(root, "data");
    if (cJSON_IsString(data) && data->valuestring) {
        size_t n = strlen(data->valuestring);
        out->data = (char *)malloc(n);
        if (out->data) {
            memcpy(out->data, data->valuestring, n);
            out->data_len = n;
        }
    }

    cJSON_Delete(root);
    return 0;
}

void proto_free_command(cmd_t *cmd)
{
    if (cmd && cmd->data) {
        free(cmd->data);
        cmd->data = NULL;
        cmd->data_len = 0;
    }
}
