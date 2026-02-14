#include <gui/view_dispatcher.h>
#include <gui/view_dispatcher.h>

#include "src/app/app.h"
#include "src/app/blinker.h"
#include "src/swire/swire_clock.h"
#include "src/global_debug.h"
#include "src/commands/commands.h"
#include "src/commands/commands_bitbang.h"
#include "src/commands/commands_uart.h"

#include "scenes/swire_scene.h"

static void handle_usb_event(FuriEventLoopObject* object, void* context);
static void loop_iteration(SwireApp* app);
static void app_handle_periodic_debug_info(SwireApp* app);
static void app_handle_cdc_state_changed(void* ctx, SwireUsb* sender, CdcState state);
static void app_handle_rx_one(SwireApp* app);
static void app_send_welcome(SwireApp* app);

const GpioPin* const pin_sws = &gpio_ext_pa7;
const GpioPin* const pin_back = &gpio_button_back;
SwireApp* app = NULL;

const SceneManagerHandlers app_scene_handlers = {

};

SwireApp* app_alloc() {
    SwireApp* self = malloc(sizeof(SwireApp));
    furi_check(self, "app_alloc");
    self->running = true;
    self->message = furi_string_alloc();
    self->message2 = furi_string_alloc();
    self->tmp_str1 = furi_string_alloc();
    self->tmp_str2 = furi_string_alloc();
    self->command = furi_string_alloc();
    self->last_tick = swire_clock_get_real_tick();
    //self->queue = furi_message_queue_alloc(_QUEUE_CAPACITY, sizeof(SwMessage));
    self->event_loop = furi_event_loop_alloc();
    self->timers = timerpool_alloc(self->event_loop);

    //self->timer_poll = furi_event_loop_timer_alloc(
    //    self->event_loop,
    //    (FuriEventLoopTimerCallback)loop_iteration,
    //    FuriEventLoopTimerTypePeriodic,
    //    self);
    //self->timer_debug = furi_event_loop_timer_alloc(
    //    self->event_loop,
    //    (FuriEventLoopTimerCallback)app_handle_periodic_debug_info,
    //    FuriEventLoopTimerTypePeriodic,
    //    self);
    self->usb = NULL;

    self->gui = furi_record_open(RECORD_GUI);
    self->view_dispatcher = view_dispatcher_alloc();
    self->scene_manager = scene_manager_alloc(&app_scene_handlers, self);
    self->widget = widget_alloc();
    self->notifications = furi_record_open(RECORD_NOTIFICATION);
    self->dialog = dialog_ex_alloc();

    view_dispatcher_attach_to_gui(self->view_dispatcher, self->gui, ViewDispatcherTypeFullscreen);

    self->var_item_list = variable_item_list_alloc();
    view_dispatcher_add_view(
        app->view_dispatcher,
        SwireAppViewVarItemList,
        variable_item_list_get_view(app->var_item_list));

    scene_manager_next_scene(app->scene_manager, SwireSceneStart);

    return self;
}

void app_init(SwireApp* self) {
    app_set_blinker_state(self, BlinkerStateIdle);
    if(self->usb != NULL) swire_usb_free(self->usb);
    self->usb = swire_usb_alloc();
    // swire_usb_set_on_rx_line(self->usb, handle_usb_rx_line, self);
    // swire_usb_set_on_state_change(self->usb, app_handle_cdc_state_changed, self);

    //furi_event_loop_timer_start(app->timer_poll, 50);
    //furi_event_loop_timer_start(app->timer_debug, 250);
    app_set_timer(self, 50, FuriEventLoopTimerTypePeriodic, loop_iteration);
    app_set_timer(self, 250, FuriEventLoopTimerTypePeriodic, app_handle_periodic_debug_info);

    //furi_event_loop_subscribe_message_queue(
    //    self->event_loop, self->queue, FuriEventLoopEventIn, handle_app_message, app);
    furi_event_loop_subscribe_event_flag(
        self->event_loop,
        swire_usb_get_event_flag_rx(self->usb),
        FuriEventLoopEventIn,
        handle_usb_event,
        app);
    // furi_event_loop_subscribe_message_queue(
    //     self->event_loop,
    //     swire_usb_get_queue(self->usb),
    //     FuriEventLoopEventIn,
    //     handle_usb_event,
    //     app);
}

void app_deinit(SwireApp* self) {
    variable_item_list_free(self->var_item_list);

    furi_event_loop_unsubscribe(self->event_loop, swire_usb_get_event_flag_rx(self->usb));
    // furi_event_loop_unsubscribe(self->event_loop, swire_usb_get_queue(self->usb));
    swire_usb_free(self->usb);
    self->usb = NULL;

    blinker_free(self->blinker);
    self->blinker = NULL;
    timerpool_free(self->timers);
    self->timers = NULL;
}

void app_free(SwireApp* self) {
    if(self == NULL) return;

    dialog_ex_free(self->dialog);
    furi_record_close(RECORD_NOTIFICATION);
    widget_free(self->widget);
    variable_item_list_free(self->var_item_list);
    scene_manager_free(self->scene_manager);
    view_dispatcher_free(self->view_dispatcher);
    furi_record_close(RECORD_GUI);

    blinker_free(self->blinker);
    //if(self->timer_poll != NULL) furi_event_loop_timer_free(self->timer_poll);
    //if(self->timer_debug != NULL) furi_event_loop_timer_free(self->timer_debug);
    timerpool_free(self->timers);
    swire_usb_free(self->usb);
    furi_event_loop_free(self->event_loop);
    furi_string_free(self->message);
    furi_string_free(self->message2);
    furi_string_free(self->tmp_str1);
    furi_string_free(self->tmp_str2);
    furi_string_free(self->command);
    // furi_event_loop_unsubscribe(self->event_loop, self->queue);
    // furi_message_queue_free(self->queue);

    free(self);
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
    SwUsbRxEvent events = furi_event_flag_clear(flag, SwUsbRxEventAll);
    // events result had the TxComplete flag even though it is not submitted to furi_event_flag_clear
    // clear seems to return all old flags, keep just the ones we care about
    global_debug()->evt_t++;

    if(events & FuriFlagError) {
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

void loop_iteration(SwireApp* app) {
    UNUSED(app);

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

#if SW_USB_USE_POLLING_WORKAROUND == 1
    if(app->usb) {
        FuriEventFlag* flag = swire_usb_get_event_flag_rx(app->usb);
        if(furi_event_flag_get(flag) & SwUsbRxEventAll) {
            furi_event_flag_clear(flag, SwUsbRxEventDummy);
            furi_event_flag_set(flag, SwUsbRxEventDummy);
        }
    }
#endif
}

void global_stop_loop() {
    app->running = false;
    furi_event_loop_stop(app->event_loop);
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
    furi_string_vprintf(app->message, format, args);
    va_end(args);
    //view_port_update(app->view_port); // TODO
}

static void app_handle_rx_one(SwireApp* app) {
    FuriEventFlag* flag = swire_usb_get_event_flag_rx(app->usb);

    global_debug()->evt_rx++;
    furi_event_flag_set(flag, SwUsbRxEventRxAvailable);
    FuriStatus status = swire_usb_readline_str(app->usb, app->command);
    furi_string_printf(app->message, "rls %08x", status);
    if(status & FuriFlagError) {
        global_debug()->err_loc = 31;
        global_debug()->err = status;
    } else {
        handle_command(app, app->command);
    }
}

static void app_send_welcome(SwireApp* app) {
    FuriStatus status = swire_usb_printf_line(app->usb, "swire_demo welcome v%s", APP_VERSION);
    if(status & FuriFlagError) {
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
