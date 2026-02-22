#include "src/furi/duration.hpp"
#include "src/app/blinker.hpp"
#include "src/utils/light_rgb.hpp"

static void blinker_handle_timer(void* context);

Blinker::Blinker(FuriEventLoop* event_loop) {
    this->led_state = 0;
    this->led_color = 0x00ff00;
    this->event_loop = event_loop;
    this->timer = furi_event_loop_timer_alloc(
        this->event_loop, blinker_handle_timer, FuriEventLoopTimerTypePeriodic, this);
}

Blinker::~Blinker() {
    furi_event_loop_timer_free(this->timer);
}

void Blinker::set(uint32_t color, furi::u32ms interval) {
    if(interval.count() == 0) {
        furi_event_loop_timer_stop(this->timer);
        light_rgb_set(color);
    } else {
        furi_event_loop_timer_start(this->timer, interval.count() / 2);
    }
    this->led_color = color;
}

void Blinker::_handle_timer() {
    this->led_state = !this->led_state;

    light_rgb_set(this->led_state ? this->led_color : 0);
}
static void blinker_handle_timer(void* context) {
    Blinker* self = (Blinker*)context;
    self->_handle_timer();
}
