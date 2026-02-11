#pragma once

#include <furi.h>

typedef enum {
    SwireBitbangRwWrite = 0,
    SwireBitbangRwRead = 1,
} SwireBitbangRw;

typedef enum {
    SwireBitbangErrorNone = 0,
    SwireBitbangErrorTimeout = 1,
} SwireBitbangError;

typedef struct {
    const GpioPin* pin_sws_i;
    const GpioPin* pin_sws_o;
    uint32_t next_unit_tick;
    uint32_t timeout_byte_ticks;
    SwireBitbangError error;
} SwireBitbang;

static const uint32_t SwireBitbangGlobalTimeoutTicks = 64000;

void swire_bitbang_global_init();
void swire_bitbang_global_init_with_bitrate(uint32_t bitrate);
void swire_bitbang_global_log_params();

SwireBitbang* swire_bitbang_alloc_with_sws(const GpioPin* pin_sws_o, const GpioPin* pin_sws_i);
void swire_bitbang_free(SwireBitbang* swire);

void swire_bitbang_timer_restart(SwireBitbang* swire);
void swire_bitbang_timer_continue(SwireBitbang* swire);
bool swire_bitbang_has_error(SwireBitbang* swire);

void swire_bitbang_transaction_start(
    SwireBitbang* swire,
    uint32_t addr,
    SwireBitbangRw rw,
    uint32_t slave_id);
void swire_bitbang_transaction_end(SwireBitbang* swire);
void swire_bitbang_transaction_end_force(SwireBitbang* swire);

void swire_bitbang_byte_write(SwireBitbang* swire, uint8_t data);
int32_t swire_bitbang_byte_read(SwireBitbang* swire);
