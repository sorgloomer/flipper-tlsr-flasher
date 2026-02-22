#pragma once

#include <furi.h>

typedef struct Blinker {
    FuriEventLoop* event_loop;
    FuriEventLoopTimer* timer;

    bool led_state;
    uint32_t led_color;
} Blinker;

Blinker* blinker_alloc(FuriEventLoop* event_loop);
void blinker_free(Blinker* self);
void blinker_set(Blinker* blinker, uint32_t color, uint32_t interval_ms);
