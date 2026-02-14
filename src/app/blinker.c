#include "src/app/blinker.h"
#include "src/utils/light_rgb.h"

static void blinker_handle_timer(void* context);

Blinker* blinker_alloc(FuriEventLoop* event_loop) {
    Blinker* self = malloc(sizeof(Blinker));
    self->led_state = 0;
    self->led_color = 0x00ff00;
    self->event_loop = event_loop;
    self->timer = furi_event_loop_timer_alloc(
        self->event_loop, blinker_handle_timer, FuriEventLoopTimerTypePeriodic, self);
    return self;
}

void blinker_free(Blinker* self) {
    if(self == NULL) return;
    furi_event_loop_timer_free(self->timer);
}

void blinker_set(Blinker* self, uint32_t color, uint32_t interval_ms) {
    if(interval_ms == 0) {
        furi_event_loop_timer_stop(self->timer);
        light_rgb_set(color);
    } else {
        furi_event_loop_timer_start(self->timer, interval_ms / 2);
    }
    self->led_color = color;
}

static void blinker_handle_timer(void* context) {
    Blinker* self = (Blinker*)context;
    self->led_state = !self->led_state;

    light_rgb_set(self->led_state ? self->led_color : 0);
}
