#pragma once

#include <stdint.h>
#include <furi.h>

typedef struct {
    int32_t irq_tx;
    int32_t irq_rx;
    int32_t irq_rx_ts;
    int32_t irq_rx_before;
    int32_t irq_rx_after;
    int32_t irq_rx_status;
    int32_t irq_sc;
    int32_t evt_rx;
    int32_t evt_sc;
    int32_t evt_t;
    int32_t evt_0;
    int32_t err_loc;
    int32_t err;
    int32_t rx_trace;
    FuriString** logs;
    uint32_t log_capacity;
} GlobalDebugInfo;
GlobalDebugInfo* global_debug();
void global_debug_init();
void global_debug_deinit();

void global_debug_log(const char* format, ...);
