#pragma once

#include <furi.h>

#include "src/furi/duration.hpp"

class Blinker {
    FuriEventLoop* event_loop;
    FuriEventLoopTimer* timer;

    bool led_state;
    uint32_t led_color;

public:
    Blinker(FuriEventLoop* event_loop);
    ~Blinker();
    void set(uint32_t color, furi::u32ms interval);
    void _handle_timer();
};
