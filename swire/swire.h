#pragma once

#include <furi.h>

typedef enum {
    RwWrite = 0,
    RwRead = 1,
} Rw;

typedef enum {
    SwireErrorNone = 0,
    SwireErrorTimeout = 1,
} SwireError;

typedef struct {
    const GpioPin* pin_sws_i;
    const GpioPin* pin_sws_o;
    uint32_t next_unit_tick;
    uint32_t timeout_byte_ticks;
    SwireError error;
} Swire;

static const uint32_t SwireGlobalTimeoutTicks = 64000;

void swire_global_init();
void swire_global_init_with_bitrate(uint32_t bitrate);
void swire_global_log_params();

Swire* swire_alloc_with_sws(const GpioPin* pin_sws_o, const GpioPin* pin_sws_i);
void swire_free(Swire* swire);

void swire_timer_restart(Swire* swire);
void swire_timer_continue(Swire* swire);
bool swire_has_error(Swire* swire);

void swire_transaction_start(Swire* swire, uint32_t addr, Rw rw, uint32_t slave_id);
void swire_transaction_end(Swire* swire);
void swire_transaction_end_force(Swire* swire);

void swire_byte_write(Swire* swire, uint8_t data);
int32_t swire_byte_read(Swire* swire);
