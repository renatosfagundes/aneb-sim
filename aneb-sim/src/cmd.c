#include "cmd.h"

#include <pthread.h>
#include <stdlib.h>
#include <string.h>

#include "sim_avr.h"
#include "avr_ioport.h"
#include "avr_uart.h"
#include "avr_adc.h"

#include "can_bus.h"
#include "chip.h"
#include "mcp2515.h"
#include "pin_names.h"
#include "proto.h"
#include "sim_loop.h"

/* ----- bounded ring buffer for inbound commands ----------------------- */

static pthread_mutex_t g_q_mutex = PTHREAD_MUTEX_INITIALIZER;
static cmd_t           g_q[CMD_QUEUE_CAP];
static int             g_q_head, g_q_tail, g_q_count;

void cmd_queue_init(void)
{
    pthread_mutex_lock(&g_q_mutex);
    g_q_head = g_q_tail = g_q_count = 0;
    pthread_mutex_unlock(&g_q_mutex);
}

int cmd_queue_push(const cmd_t *cmd)
{
    pthread_mutex_lock(&g_q_mutex);
    if (g_q_count == CMD_QUEUE_CAP) {
        pthread_mutex_unlock(&g_q_mutex);
        return -1;
    }
    g_q[g_q_tail] = *cmd;
    g_q_tail = (g_q_tail + 1) % CMD_QUEUE_CAP;
    g_q_count++;
    pthread_mutex_unlock(&g_q_mutex);
    return 0;
}

int cmd_queue_pop(cmd_t *out)
{
    pthread_mutex_lock(&g_q_mutex);
    if (g_q_count == 0) {
        pthread_mutex_unlock(&g_q_mutex);
        return -1;
    }
    *out = g_q[g_q_head];
    g_q_head = (g_q_head + 1) % CMD_QUEUE_CAP;
    g_q_count--;
    pthread_mutex_unlock(&g_q_mutex);
    return 0;
}

/* ----- dispatch -------------------------------------------------------- */

static void apply_din(cmd_t *cmd)
{
    chip_t *c = sim_loop_find(cmd->chip);
    if (!c || !c->avr) {
        proto_emit_log("warn", "din: unknown chip '%s'", cmd->chip);
        return;
    }
    char port; int bit;
    if (pin_parse(cmd->pin, &port, &bit) != 0) {
        proto_emit_log("warn", "din: bad pin '%s' (use PB5, PD2, ...)", cmd->pin);
        return;
    }
    avr_irq_t *irq = avr_io_getirq(c->avr,
                                   AVR_IOCTL_IOPORT_GETIRQ(port), bit);
    if (!irq) {
        proto_emit_log("warn", "din: no IRQ for pin %s on %s", cmd->pin, c->id);
        return;
    }
    avr_raise_irq(irq, cmd->val ? 1 : 0);
}

static void apply_adc(cmd_t *cmd)
{
    chip_t *c = sim_loop_find(cmd->chip);
    if (!c || !c->avr) {
        proto_emit_log("warn", "adc: unknown chip '%s'", cmd->chip);
        return;
    }
    if (cmd->channel < 0 || cmd->channel > 7) {
        proto_emit_log("warn", "adc: channel %d out of range", cmd->channel);
        return;
    }
    avr_irq_t *irq = avr_io_getirq(c->avr,
                                   AVR_IOCTL_ADC_GETIRQ,
                                   ADC_IRQ_ADC0 + cmd->channel);
    if (!irq) {
        proto_emit_log("warn", "adc: no IRQ for ch%d on %s",
                       cmd->channel, c->id);
        return;
    }
    int v = cmd->val;
    if (v < 0)    v = 0;
    if (v > 1023) v = 1023;
    avr_raise_irq(irq, (uint32_t)v);
}

static void apply_uart(cmd_t *cmd)
{
    chip_t *c = sim_loop_find(cmd->chip);
    if (!c || !c->avr) {
        proto_emit_log("warn", "uart: unknown chip '%s'", cmd->chip);
        return;
    }
    if (!cmd->data || cmd->data_len == 0) return;
    avr_irq_t *irq = avr_io_getirq(c->avr,
                                   AVR_IOCTL_UART_GETIRQ('0'),
                                   UART_IRQ_INPUT);
    if (!irq) return;
    for (size_t i = 0; i < cmd->data_len; i++) {
        avr_raise_irq(irq, (uint8_t)cmd->data[i]);
    }
}

static void apply_load(cmd_t *cmd)
{
    chip_t *c = sim_loop_find(cmd->chip);
    if (!c) {
        proto_emit_log("warn", "load: unknown chip '%s'", cmd->chip);
        return;
    }
    chip_load_hex(c, cmd->path);
}

static void apply_reset(cmd_t *cmd)
{
    chip_t *c = sim_loop_find(cmd->chip);
    if (!c) {
        proto_emit_log("warn", "reset: unknown chip '%s'", cmd->chip);
        return;
    }
    chip_reset(c);
}

/* Decode an ASCII hex string into raw bytes. Returns count written, or
 * -1 on a malformed input (odd length, non-hex digit). Caller-supplied
 * `dst` must hold at least cap bytes; excess input is truncated. */
static int hex_decode(const char *src, size_t src_len, uint8_t *dst, size_t cap)
{
    if (src_len % 2 != 0) return -1;
    size_t out = 0;
    for (size_t i = 0; i < src_len && out < cap; i += 2) {
        unsigned hi, lo;
        char c1 = src[i], c2 = src[i + 1];
        if      (c1 >= '0' && c1 <= '9') hi = c1 - '0';
        else if (c1 >= 'a' && c1 <= 'f') hi = c1 - 'a' + 10;
        else if (c1 >= 'A' && c1 <= 'F') hi = c1 - 'A' + 10;
        else return -1;
        if      (c2 >= '0' && c2 <= '9') lo = c2 - '0';
        else if (c2 >= 'a' && c2 <= 'f') lo = c2 - 'a' + 10;
        else if (c2 >= 'A' && c2 <= 'F') lo = c2 - 'A' + 10;
        else return -1;
        dst[out++] = (uint8_t)((hi << 4) | lo);
    }
    return (int)out;
}

static void apply_can_inject(cmd_t *cmd)
{
    can_bus_t *bus = sim_loop_bus();
    if (!bus) {
        proto_emit_log("warn", "can_inject: bus not initialized");
        return;
    }
    /* The bus name is optional but if present must match. */
    if (cmd->bus[0] && strcmp(cmd->bus, bus->name) != 0) {
        proto_emit_log("warn", "can_inject: unknown bus '%s'", cmd->bus);
        return;
    }

    mcp2515_frame_t f = {0};
    f.id  = cmd->can_id & (cmd->can_ext ? 0x1FFFFFFFu : 0x7FFu);
    f.ext = cmd->can_ext;
    f.rtr = cmd->can_rtr;
    f.dlc = (cmd->can_dlc > 8) ? 8 : cmd->can_dlc;
    if (cmd->data && cmd->data_len > 0) {
        int n = hex_decode(cmd->data, cmd->data_len, f.data, 8);
        if (n < 0) {
            proto_emit_log("warn", "can_inject: malformed hex data");
            return;
        }
        /* If DLC wasn't explicit, infer from decoded length. */
        if (cmd->can_dlc == 0) f.dlc = (uint8_t)n;
    }

    can_bus_inject(bus, &f);
}

void cmd_apply(cmd_t *cmd)
{
    switch (cmd->type) {
    case CMD_DIN:    apply_din(cmd);    break;
    case CMD_ADC:    apply_adc(cmd);    break;
    case CMD_UART:   apply_uart(cmd);   break;
    case CMD_LOAD:   apply_load(cmd);   break;
    case CMD_RESET:      apply_reset(cmd);      break;
    case CMD_CAN_INJECT: apply_can_inject(cmd); break;
    case CMD_SPEED:      sim_loop_set_speed(cmd->speed); break;
    case CMD_PAUSE:      sim_loop_pause_all();  break;
    case CMD_RESUME:     sim_loop_resume_all(); break;
    case CMD_STEP:
        /* M1: step is a stub — does a resume-pause sandwich for now.
         * Full single-stepping by cycles lands when wallclock pacing
         * arrives in M5 alongside the speed factor. */
        sim_loop_resume_all();
        for (uint64_t i = 0; i < cmd->cycles / SIM_CYCLES_PER_TICK; i++) {
            sim_loop_tick();
        }
        sim_loop_pause_all();
        break;
    default:
        proto_emit_log("warn", "cmd: unknown type %d", cmd->type);
        break;
    }
    proto_free_command(cmd);
}
