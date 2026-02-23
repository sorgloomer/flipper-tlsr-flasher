#pragma once
class SwireApp;

#include <memory>
#include <string>

#include <gui/gui.h>
#include <gui/view_dispatcher.h>
#include <gui/scene_manager.h>
#include <gui/modules/dialog_ex.h>
#include <gui/modules/variable_item_list.h>
#include <gui/modules/widget.h>

#include <power/power_service/power.h>
#include <notification/notification_messages.h>

#include "src/usb/usb.hpp"
#include "src/utils/timerpool.hpp"
#include "src/app/blinker.hpp"
#include "src/app/config.hpp"
#include "src/swire/swire_bitbang.hpp"
#include "src/furi/event_loop.hpp"
#include "src/furi/record.hpp"
#include "src/main_async.hpp"

typedef enum {
    SwireAppViewVarItemList,
} SwireAppView;

class SwireApp {
public:
    bool running;
    SwireUsb* usb;
    SwireBitbang* swire;
    furi::Record<Gui> gui;
    furi::Record<Power> power;

    SwireConfig* config;
    uint32_t last_tick;
    std::unique_ptr<TimerPool> timers;
    std::unique_ptr<Blinker> blinker;

    std::string message;
    std::string message2;
    std::string command;
    std::string tmp_str1;
    std::string tmp_str2;

    uint32_t debug_value;
    uint32_t debug_value_to_show;

    furi::Record<NotificationApp> notifications;
    VariableItemList* var_item_list;
    ViewDispatcher* view_dispatcher;
    furi::EventLoop event_loop;
    SceneManager* scene_manager;
    Widget* widget;
    DialogEx* dialog;

public:
    SwireApp();
    ~SwireApp();
    void run();
};

enum BlinkerState {
    BlinkerStateOff,
    BlinkerStateIdle,
    BlinkerStateConnected,
    BlinkerStateTimeout,
    BlinkerStateError,
    BlinkerStateSolidWhite,
};

typedef void (*SwireAppCallback)(SwireApp* app);

void global_stop_loop();

void app_set_usb_enabled(SwireApp* self, bool value);

void app_set_blinker(SwireApp* app, uint32_t color, furi::u32ms interval);
void app_set_blinker_state(SwireApp* app, BlinkerState state);

void app_set_message(SwireApp* app, const char* format, ...);

template <typename F>
void app_set_timer(SwireApp* app, furi::u32ms interval, FuriEventLoopTimerType type, F&& callback) {
    app->timers->submit(interval, type, callback);
}

void app_usb_printf_ln(SwireApp* self, const char* format, ...);

extern const GpioPin* const pin_sws;
extern const GpioPin* const pin_back;
extern SwireApp* global_app;
