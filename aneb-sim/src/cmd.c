#include "cmd.h"

#include <pthread.h>
#include <stdlib.h>
#include <string.h>

#include "sim_avr.h"
#include "avr_ioport.h"
#include "avr_uart.h"
#include "avr_adc.h"

#include "chip.h"
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

void cmd_apply(cmd_t *cmd)
{
    switch (cmd->type) {
    case CMD_DIN:    apply_din(cmd);    break;
    case CMD_ADC:    apply_adc(cmd);    break;
    case CMD_UART:   apply_uart(cmd);   break;
    case CMD_LOAD:   apply_load(cmd);   break;
    case CMD_RESET:  apply_reset(cmd);  break;
    case CMD_SPEED:  sim_loop_set_speed(cmd->speed); break;
    case CMD_PAUSE:  sim_loop_pause_all();  break;
    case CMD_RESUME: sim_loop_resume_all(); break;
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
