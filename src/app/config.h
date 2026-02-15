#pragma once

#include <furi.h>
#include <stdint.h>

typedef struct SwireConfig {
    uint32_t bitrate;
    uint32_t addrsize;
    uint32_t reset_duration_ms;
    uint32_t reset_delay_ms;
    uint32_t trigger_duration_us;
    uint32_t trigger_delay_us;
    uint32_t keep_powered_duration_ms;
} SwireConfig;

SwireConfig* swire_config_alloc();
void swire_config_free(SwireConfig* self);
