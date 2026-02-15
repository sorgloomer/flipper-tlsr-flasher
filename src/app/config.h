#pragma once

#include <furi.h>
#include <stdint.h>

typedef struct SwireConfig {
    uint32_t bitrate;
} SwireConfig;

SwireConfig* swire_config_alloc();
void swire_config_free(SwireConfig* self);
