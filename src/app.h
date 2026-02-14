#pragma once

#include <furi/core/string.h>

#include <gui/gui.h>
#include <gui/modules/variable_item_list.h>

#include "src/usb.h"
#include "src/timerpool.h"

const char APP_VERSION[] = "0.3";

typedef struct {
    SwireUsb* usb;
    FuriEventLoop* event_loop;
    // FuriMessageQueue* queue;
    bool running;
    uint32_t last_tick;
    TimerPool* timers;
    FuriEventLoopTimer* timer_led;

    bool led_state;
    uint32_t led_color;
    FuriString* message;
    FuriString* message2;
    FuriString* command;
    FuriString* tmp_str1;
    FuriString* tmp_str2;
    ViewPort* view_port;
    VariableItemList* var_item_list;

    uint32_t debug_value;
    uint32_t debug_value_to_show;
} SwireApp;

typedef enum {
    BlinkerStateOff,
    BlinkerStateIdle,
    BlinkerStateConnected,
    BlinkerStateTimeout,
    BlinkerStateError,
    BlinkerStateSolidWhite,
} BlinkerState;

typedef void (*SwireAppCallback)(SwireApp* app);
