#pragma once

#include <furi.h>
#include "src/swire/swire_common.h"

typedef enum {
    SwireBitbangRwWrite = 0,
    SwireBitbangRwRead = 1,
} SwireBitbangRw;

typedef enum {
    SwireBitbangErrorNone = 0,
    SwireBitbangErrorTimeout = 1,
    SwireBitbangErrorUnknown,
} SwireBitbangError;

#define _WAVEFORM_BUFFER_LENGTH 30
typedef struct {
    const GpioPin* pin_sws_i;
    const GpioPin* pin_sws_o;

    uint32_t bitrate;
    uint32_t clocks_per_second;

    uint32_t next_unit_tick;
    uint32_t timeout_byte_ticks;
    SwireBitbangError error;
    int32_t waveform_edges[_WAVEFORM_BUFFER_LENGTH];
} SwireBitbang;

static const uint32_t SwireBitbangGlobalTimeoutTicks = 64000;

void swire_bitbang_global_init();
void swire_bitbang_global_init_with_bitrate(uint32_t bitrate);
void swire_bitbang_global_log_params();

SwireBitbang* swire_bitbang_alloc_with_sws(const IoPins sws);
void swire_bitbang_free(SwireBitbang* swire);

uint32_t swire_bitbang_get_bitrage(SwireBitbang* swire);
void swire_bitbang_set_bitrate(SwireBitbang* swire, uint32_t bitrate);

void swire_bitbang_timer_restart(SwireBitbang* swire);
void swire_bitbang_timer_join(SwireBitbang* swire);
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

// export for disassembling
// void _swire_bitbang_write_bits9(SwireBitbang* self, uint32_t bits);
