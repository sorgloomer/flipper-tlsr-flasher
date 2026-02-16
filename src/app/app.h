#pragma once

#include <furi/core/string.h>

#include <gui/gui.h>
#include <gui/view_dispatcher.h>
#include <gui/scene_manager.h>
#include <gui/modules/dialog_ex.h>
#include <gui/modules/variable_item_list.h>
#include <gui/modules/widget.h>

#include <power/power_service/power.h>
#include <notification/notification_messages.h>

#include "src/usb/usb.h"
#include "src/utils/timerpool.h"
#include "src/app/blinker.h"
#include "src/app/config.h"
#include "src/swire/swire_bitbang.h"

typedef enum {
    SwireAppViewVarItemList,
} SwireAppView;

typedef struct SwireApp {
    SwireUsb* usb;
    SwireBitbang* swire;

    SwireConfig* config;
    FuriEventLoop* event_loop;
    bool running;
    uint32_t last_tick;
    TimerPool* timers;
    Blinker* blinker;

    FuriString* message;
    FuriString* message2;
    FuriString* command;
    FuriString* logs;
    FuriString* tmp_str1;
    FuriString* tmp_str2;

    uint32_t debug_value;
    uint32_t debug_value_to_show;

    Gui* gui;
    Power* power;
    NotificationApp* notifications;
    VariableItemList* var_item_list;
    ViewDispatcher* view_dispatcher;
    SceneManager* scene_manager;
    Widget* widget;
    DialogEx* dialog;
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

void global_stop_loop();

SwireApp* app_alloc();
void app_free(SwireApp* self);
void app_run(SwireApp* self);

void app_set_usb_enabled(SwireApp* self, bool value);

void app_set_blinker(SwireApp* app, uint32_t color, uint32_t interval_ms);
void app_set_blinker_state(SwireApp* app, BlinkerState state);

void app_set_message(SwireApp* app, const char* format, ...);
void app_set_timer(
    SwireApp* app,
    uint32_t interval_ms,
    FuriEventLoopTimerType type,
    SwireAppCallback callback);

FuriString* app_get_logs(SwireApp* self);
void app_log_append(SwireApp* self, const char* format, ...);

extern const GpioPin* const pin_sws;
extern const GpioPin* const pin_back;
extern SwireApp* global_app;
