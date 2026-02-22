#include "src/app/config.hpp"
#include <gui/view_dispatcher.h>

#include <power/power_service/power.h>
#include <input/input.h>

#include "src/app/app.hpp"
#include "src/app/blinker.hpp"
#include "src/swire/swire_clock.hpp"
#include "src/utils/global_debug.hpp"
#include "src/utils/str_printf.hpp"
#include "src/commands/commands.hpp"

#include "src/scenes/swire_scene.hpp"
#include "src/furi/errors.hpp"

static void handle_usb_event(FuriEventLoopObject* object, void* context);
static void loop_iteration(SwireApp* app);
static void app_handle_periodic_debug_info(SwireApp* app);
static void app_handle_cdc_state_changed(void* ctx, SwireUsb* sender, CdcState state);
static void app_handle_rx_one(SwireApp* app);
static void app_send_welcome(SwireApp* app);

static bool app_custom_event_callback(void* context, uint32_t event);
static bool app_back_event_callback(void* context);
static void app_tick_event_callback(void* context);

static void app_do_event_flag_workaround_iteration(SwireApp* app);

const GpioPin* const pin_sws = &gpio_ext_pa7;
const GpioPin* const pin_back = &gpio_button_back;
SwireApp* global_app = NULL;

SwireApp* app_alloc() {
    SwireApp* self = (SwireApp*)malloc(sizeof(SwireApp));
    furi_check(self, "app_alloc");

    self->running = true;
    self->usb = NULL;
    self->swire = NULL;

    self->config = swire_config_alloc();

    self->last_tick = swire_clock_get_cycclk();
    self->view_dispatcher = view_dispatcher_alloc();
    self->event_loop = view_dispatcher_get_event_loop(self->view_dispatcher);
    self->timers = timerpool_alloc(self->event_loop);
    self->blinker = blinker_alloc(self->event_loop);

    self->gui = (Gui*)furi_record_open(RECORD_GUI);
    self->power = (Power*)furi_record_open(RECORD_POWER);

    FURI_LOG_T("swire", "app_alloc checkpoint 5");
    FURI_LOG_T("swire", "app_alloc checkpoint 6");

    self->scene_manager = scene_manager_alloc(&swire_app_scene_handlers, self);
    FURI_LOG_T("swire", "app_alloc checkpoint 7");
    self->widget = widget_alloc();
    self->notifications = (NotificationApp*)furi_record_open(RECORD_NOTIFICATION);
    self->dialog = dialog_ex_alloc();

    FURI_LOG_T("swire", "app_alloc checkpoint 8");
    view_dispatcher_set_event_callback_context(self->view_dispatcher, self);
    FURI_LOG_T("swire", "app_alloc checkpoint 8.1");
    view_dispatcher_set_custom_event_callback(self->view_dispatcher, app_custom_event_callback);
    FURI_LOG_T("swire", "app_alloc checkpoint 8.2");
    view_dispatcher_set_navigation_event_callback(self->view_dispatcher, app_back_event_callback);
    FURI_LOG_T("swire", "app_alloc checkpoint 8.3");
    view_dispatcher_set_tick_event_callback(self->view_dispatcher, app_tick_event_callback, 100);
    FURI_LOG_T("swire", "app_alloc checkpoint 8.4");
    view_dispatcher_attach_to_gui(self->view_dispatcher, self->gui, ViewDispatcherTypeFullscreen);

    FURI_LOG_T("swire", "app_alloc checkpoint 9");
    self->var_item_list = variable_item_list_alloc();
    view_dispatcher_add_view(
        self->view_dispatcher,
        SwireAppViewVarItemList,
        variable_item_list_get_view(self->var_item_list));

    FURI_LOG_T("swire", "app_alloc checkpoint 10");
    scene_manager_next_scene(self->scene_manager, SwireSceneStart);

    FURI_LOG_T("swire", "app_alloc checkpoint 11");
    app_set_timer(self, 50, FuriEventLoopTimerTypePeriodic, loop_iteration);
    app_set_timer(self, 250, FuriEventLoopTimerTypePeriodic, app_handle_periodic_debug_info);

    FURI_LOG_T("swire", "app_alloc checkpoint 12");
    app_set_blinker_state(self, BlinkerStateIdle);

    app_set_usb_enabled(self, true);
    FURI_LOG_T("swire", "app_alloc return");
    return self;
}

void app_free(SwireApp* self) {
    if(self == NULL) return;
    app_set_usb_enabled(self, false);
    swire_usb_free(self->usb);
    swire_bitbang_free(self->swire);

    view_dispatcher_remove_view(self->view_dispatcher, SwireAppViewVarItemList);

    dialog_ex_free(self->dialog);
    furi_record_close(RECORD_NOTIFICATION);
    widget_free(self->widget);
    variable_item_list_free(self->var_item_list);
    scene_manager_free(self->scene_manager);
    furi_record_close(RECORD_GUI);
    furi_record_close(RECORD_POWER);

    blinker_free(self->blinker);
    //if(self->timer_poll != NULL) furi_event_loop_timer_free(self->timer_poll);
    //if(self->timer_debug != NULL) furi_event_loop_timer_free(self->timer_debug);
    timerpool_free(self->timers);
    view_dispatcher_free(self->view_dispatcher);
    // furi_event_loop_free(self->event_loop); // owned and freed by view_dispatcher
    // furi_event_loop_unsubscribe(self->event_loop, self->queue);
    // furi_message_queue_free(self->queue);

    swire_config_free(self->config);

    free(self);
}

static bool app_custom_event_callback(void* context, uint32_t event) {
    furi_assert(context);
    SwireApp* app = (SwireApp*)context;
    return scene_manager_handle_custom_event(app->scene_manager, event);
}

static bool app_back_event_callback(void* context) {
    furi_assert(context);
    SwireApp* app = (SwireApp*)context;
    return scene_manager_handle_back_event(app->scene_manager);
}

static void app_tick_event_callback(void* context) {
    furi_assert(context);
    SwireApp* app = (SwireApp*)context;
    scene_manager_handle_tick_event(app->scene_manager);
}

void app_set_usb_enabled(SwireApp* self, bool value) {
    if(value) {
        if(self->usb == NULL) {
            self->usb = swire_usb_alloc();
            furi_event_loop_subscribe_event_flag(
                self->event_loop,
                swire_usb_get_event_flag_rx(self->usb),
                FuriEventLoopEventIn,
                handle_usb_event,
                self);
        }
    } else {
        if(self->usb != NULL) {
            furi_event_loop_unsubscribe(self->event_loop, swire_usb_get_event_flag_rx(self->usb));
            swire_usb_free(self->usb);
            self->usb = NULL;
        }
    }
}

void app_set_blinker(SwireApp* app, uint32_t color, uint32_t interval_ms) {
    blinker_set(app->blinker, color, interval_ms);
}

void app_set_blinker_state(SwireApp* app, BlinkerState state) {
    switch(state) {
    case BlinkerStateOff:
        app_set_blinker(app, 0, 0);
        break;
    case BlinkerStateIdle:
        app_set_blinker(app, 0x0000ff, 2000);
        break;
    case BlinkerStateConnected:
        app_set_blinker(app, 0x00ff00, 1000);
        break;
    case BlinkerStateTimeout:
        app_set_blinker(app, 0xff00ff, 1000);
        break;
    case BlinkerStateSolidWhite:
        app_set_blinker(app, 0xffffff, 0);
        break;
    case BlinkerStateError:
        app_set_blinker(app, 0xff0000, 500);
        break;
    default:
        app_set_blinker(app, 0xff0000, 0);
        break;
    }
}

void app_set_timer(
    SwireApp* app,
    uint32_t interval_ms,
    FuriEventLoopTimerType type,
    SwireAppCallback callback) {
    timerpool_submit(app->timers, interval_ms, type, (FuriEventLoopTimerCallback)callback, app);
}

static void handle_usb_event(FuriEventLoopObject* object, void* context) {
    UNUSED(object);
    SwireApp* app = (SwireApp*)context;
    FuriEventFlag* flag = swire_usb_get_event_flag_rx(app->usb);
    SwUsbRxEvent events = (SwUsbRxEvent)furi_event_flag_clear(flag, SwUsbRxEventAll);
    // events result had the TxComplete flag even though it is not submitted to furi_event_flag_clear
    // clear seems to return all old flags, keep just the ones we care about
    global_debug()->evt_t++;

    if((uint32_t)events & FuriFlagError) {
        global_debug()->err_loc = 41;
        global_debug()->err = events;
        return; // TODO
    }
    if(events == 0) {
        global_debug()->evt_0++;
        return;
    }
    if(events & SwUsbRxEventStateChange) {
        global_debug()->evt_sc++;
        app_handle_cdc_state_changed(app, app->usb, swire_usb_get_cdc_state(app->usb));
    }
    if(events & SwUsbRxEventRxAvailable) {
        app_handle_rx_one(app);
    }
}

static void app_do_event_flag_workaround_iteration(SwireApp* app) {
    if(app->usb == NULL) return;
    FuriEventFlag* flag = swire_usb_get_event_flag_rx(app->usb);
    if(flag == NULL) return;
    if(furi_event_flag_get(flag) & SwUsbRxEventAll) {
        furi_event_flag_clear(flag, SwUsbRxEventDummy);
        furi_event_flag_set(flag, SwUsbRxEventDummy);
    }
}

void loop_iteration(SwireApp* app) {
    UNUSED(app);

    /* TODO
    if(!furi_hal_gpio_read(&gpio_button_back)) {
        furi_string_set(app->command, "back button");
        furi_delay_ms(300);
        global_stop_loop();
    }
    if(!furi_hal_gpio_read(&gpio_button_down)) {
        cmd_bitbang_read_top(75600);
    }
    if(!furi_hal_gpio_read(&gpio_button_left)) {
        // do_timer_test();
    }
    if(!furi_hal_gpio_read(&gpio_button_right)) {
        cmd_bitbang_read_top(180000);
    }
    if(!furi_hal_gpio_read(&gpio_button_up)) {
        cmd_do_by_uart();
    }
    */
#if SW_USB_USE_POLLING_WORKAROUND == 1
    app_do_event_flag_workaround_iteration(app);
#endif
}

void global_stop_loop() {
    global_app->running = false;
    furi_event_loop_stop(global_app->event_loop);
}

static void app_handle_periodic_debug_info(SwireApp* app) {
    UNUSED(app);
    // TODO
}

static void app_handle_cdc_state_changed(void* ctx, SwireUsb* sender, CdcState state) {
    UNUSED(sender);
    SwireApp* app = (SwireApp*)ctx;
    switch(state) {
    case CdcStateConnected:
        app_set_blinker_state(app, BlinkerStateConnected);
        app_set_message(app, "cdc connected");
        app_set_timer(app, 1000, FuriEventLoopTimerTypeOnce, app_send_welcome);
        break;
    case CdcStateDisconnected:
        app_set_blinker_state(app, BlinkerStateIdle);
        app_set_message(app, "cdc disconnected");
        break;
    default:
        app_set_message(app, "unknown cdc state");
        app_set_blinker_state(app, BlinkerStateError);
        break;
    }
}

void app_set_message(SwireApp* app, const char* format, ...) {
    va_list args;
    va_start(args, format);
    str_vprintf(app->message, format, args);
    va_end(args);
    //view_port_update(app->view_port); // TODO
}

static void app_handle_rx_one(SwireApp* app) {
    FuriEventFlag* flag = swire_usb_get_event_flag_rx(app->usb);

    global_debug()->evt_rx++;
    // the event loop handler removes this flag atomically. we put that flag
    // back here because the callee will first attempt to wait for the rx
    // available flag
    furi_event_flag_set(flag, SwUsbRxEventRxAvailable);
    FuriStatus status = swire_usb_readline_str(app->usb, app->command);
    str_printf(app->message, "rls %08x", status);
    if(furi_status_is_error(status)) {
        global_debug()->err_loc = 31;
        global_debug()->err = status;
    } else {
        handle_text_command(app, app->command);
    }
}

static void app_send_welcome(SwireApp* app) {
    FuriStatus status = swire_usb_printf_ln(app->usb, "swire_demo welcome v%s", APP_VERSION);
    if(furi_status_is_error(status)) {
        switch(status) {
        case FuriStatusErrorTimeout:
            app_set_message(app, "tx timeout");
            app_set_blinker_state(app, BlinkerStateTimeout);
            break;
        default:
            app_set_message(app, "unknown err");
            app_set_blinker_state(app, BlinkerStateError);
            break;
        }
    }
}

void app_run(SwireApp* self) {
    view_dispatcher_run(self->view_dispatcher);
}

void app_usb_printf_ln(SwireApp* self, const char* format, ...) {
    furi_check(self->usb, "swire app->usb");
    va_list args;
    va_start(args, format);
    str_vprintf(self->tmp_str1, format, args);
    va_end(args);
    swire_usb_writeline_str(self->usb, self->tmp_str1);
}
