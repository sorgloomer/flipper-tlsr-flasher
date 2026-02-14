#include <furi.h>
#include <furi_hal.h>
#include <furi/core/string.h>
#include <gui/gui.h>

#include "src/swire/swire_clock.h"
#include "src/swire/swire_bitbang.h"
#include "src/swire/swire_uart.h"
#include "src/usb.h"
#include "src/timerpool.h"
#include "src/rgb.h"
#include "src/global_debug.h"

#define SW_LOG_I(format, ...) FURI_LOG_I("swire", format, ##__VA_ARGS__)
#define SW_IMAGE_LEN          144000 // 16
#define SW_QUEUE_CAPACITY     32

typedef enum {
    SwMessageNone,
    SwMessageRx,
} SwMessage;
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

void my_stop_loop();
static void handle_command(SwireApp* context, FuriString* cmd);
void app_set_blinker(SwireApp* app, uint32_t color, uint32_t interval_ms);
void app_set_blinker_state(SwireApp* app, BlinkerState state);
void app_set_message(SwireApp* app, const char* format, ...);
void app_set_timer(
    SwireApp* app,
    uint32_t interval_ms,
    FuriEventLoopTimerType type,
    SwireAppCallback callback);

uint32_t worker_callback(void* ctx);
void handle_app_message(FuriEventLoopObject* object, void* ctx);

const GpioPin* const pin_sws = &gpio_ext_pa7;
const GpioPin* const pin_back = &gpio_button_back;
SwireApp* app = NULL;
const char APP_VERSION[] = "0.3";

uint32_t app_get_all_event_flag_values(SwireApp* app) {
    if(app == NULL) return 0;
    SwireUsb* usb = app->usb;
    if(usb == NULL) return 0;
    uint32_t result = 0;
    {
        FuriEventFlag* flag = swire_usb_get_event_flag_rx(usb);
        if(flag != NULL) result |= furi_event_flag_get(flag);
    }
    {
        FuriEventFlag* flag = swire_usb_get_event_flag_tx(usb);
        if(flag != NULL) result |= furi_event_flag_get(flag);
    }
    return result;
}

void hex(FuriString* output, FuriString* input, bool whitespace) {
    furi_string_reset(output);

    for(int32_t i = 0, len = furi_string_size(input); i < len; i++) {
        char c = furi_string_get_char(input, i);
        furi_string_cat_printf(
            output, (i == 0 || !whitespace) ? "%02x" : " %02x", (unsigned int)c);
    }
}
static void my_draw_callback(Canvas* canvas, void* context) {
    SwireApp* app = (SwireApp*)context;
    UNUSED(app);
    int line = 0;
    canvas_set_font(canvas, FontPrimary);
    FuriString* str = app->tmp_str1;
    FuriString* str2 = app->tmp_str2;

    //canvas_draw_str(canvas, 5, 8, "TLSR SWIRE");
    furi_string_printf(
        str,
        "i: %04lx %04lx %04lx %ld",
        global_debug()->irq_rx_ts & 0xffff,
        global_debug()->irq_rx_before,
        global_debug()->irq_rx_after,
        global_debug()->irq_rx);
    canvas_draw_str(canvas, 2, 8 + 10 * line, furi_string_get_cstr(str));
    line++;

    furi_string_printf(
        str,
        "irq: %08lx flag: %04lx",
        global_debug()->irq_rx_status,
        app_get_all_event_flag_values(app));
    canvas_draw_str(canvas, 2, 8 + 10 * line, furi_string_get_cstr(str));
    line++;

    furi_string_printf(str, "err: l:%ld %08lx", global_debug()->err_loc, global_debug()->err);
    canvas_draw_str(canvas, 2, 8 + 10 * line, furi_string_get_cstr(str));
    line++;

    furi_string_printf(
        str,
        "evt: %ld %ld %ld / %ld",
        global_debug()->evt_0,
        global_debug()->evt_rx,
        global_debug()->evt_sc,
        global_debug()->evt_t);
    canvas_draw_str(canvas, 2, 8 + 10 * line, furi_string_get_cstr(str));
    line++;

    furi_string_printf(
        str, "m1: %s tc: %ld", furi_string_get_cstr(app->message), global_debug()->rx_trace);
    canvas_draw_str(canvas, 2, 8 + 10 * line, furi_string_get_cstr(str));
    line++;

    hex(str2, app->command, false);
    furi_string_printf(str, "chx: %s", furi_string_get_cstr(str2));
    canvas_draw_str(canvas, 2, 8 + 10 * line, furi_string_get_cstr(str));
    line++;
}

void do_read() {
    int32_t row[16];

    SwireBitbang* swire = swire_bitbang_alloc_with_sws(&gpio_ext_pa7, &gpio_ext_pa6);

    swire_bitbang_transaction_start(swire, 0x0602, SwireBitbangRwWrite, 0);
    swire_bitbang_byte_write(swire, 0x05);
    swire_bitbang_transaction_end(swire);
    swire_bitbang_transaction_start(swire, 0x00b2, SwireBitbangRwWrite, 0);
    swire_bitbang_byte_write(swire, 0x7f);
    swire_bitbang_transaction_end(swire);
    swire_bitbang_transaction_start(swire, 0x00b2, SwireBitbangRwRead, 0);
    int32_t b1 = swire_bitbang_byte_read(swire);
    UNUSED(b1); // sanity check
    swire_bitbang_transaction_end(swire);

    furi_delay_ms(50);

    swire_bitbang_transaction_start(swire, 0x0d, SwireBitbangRwWrite, 0);
    swire_bitbang_byte_write(swire, 0);
    swire_bitbang_transaction_end(swire);

    swire_bitbang_transaction_start(swire, 0x0c, SwireBitbangRwWrite, 0);
    swire_bitbang_byte_write(swire, 0x03);
    swire_bitbang_transaction_end(swire);

    swire_bitbang_transaction_start(swire, 0x0c, SwireBitbangRwWrite, 0);
    swire_bitbang_byte_write(swire, 0x00);
    swire_bitbang_transaction_end(swire);

    swire_bitbang_transaction_start(swire, 0x0c, SwireBitbangRwWrite, 0);
    swire_bitbang_byte_write(swire, 0x00);
    swire_bitbang_transaction_end(swire);

    swire_bitbang_transaction_start(swire, 0x0c, SwireBitbangRwWrite, 0);
    swire_bitbang_byte_write(swire, 0x00);
    swire_bitbang_transaction_end(swire);

    swire_bitbang_transaction_start(swire, 0x0c, SwireBitbangRwWrite, 0);
    swire_bitbang_byte_write(swire, 0x00);
    swire_bitbang_byte_write(swire, 0x0a);
    swire_bitbang_transaction_end(swire);

    swire_bitbang_transaction_start(swire, 0xb3, SwireBitbangRwWrite, 0);
    swire_bitbang_byte_write(swire, 0x80);
    swire_bitbang_transaction_end(swire);
    if(swire_bitbang_has_error(swire)) goto exit;

    for(int j = 0; j < 16; j++) {
        swire_bitbang_transaction_start(swire, 0x0c, SwireBitbangRwRead, 0);
        for(int i = 0; i < 16; i++) {
            row[i] = swire_bitbang_byte_read(swire);
        }
        swire_bitbang_transaction_end(swire);

        FURI_LOG_I(
            "swire",
            "%02x %02x %02x %02x %02x %02x %02x %02x  %02x %02x %02x %02x %02x %02x %02x %02x",
            (unsigned int)row[0],
            (unsigned int)row[1],
            (unsigned int)row[2],
            (unsigned int)row[3],
            (unsigned int)row[4],
            (unsigned int)row[5],
            (unsigned int)row[6],
            (unsigned int)row[7],
            (unsigned int)row[8],
            (unsigned int)row[9],
            (unsigned int)row[10],
            (unsigned int)row[11],
            (unsigned int)row[12],
            (unsigned int)row[13],
            (unsigned int)row[14],
            (unsigned int)row[15]);
        furi_delay_ms(1);
    }

exit:
    swire_bitbang_transaction_end_force(swire);
    swire_bitbang_free(swire);
}

void do_read_top(uint32_t bitrate) {
    swire_bitbang_global_init_with_bitrate(bitrate);
    swire_bitbang_global_log_params();
    do_read();
    furi_delay_ms(500);
}

void do_by_uart_dumploop(SwireUart* swire) {
    FuriString* string = furi_string_alloc();
    uint8_t* buffer = malloc(256);
    furi_check(buffer);

    for(uint32_t addr = 0; addr < SW_IMAGE_LEN; addr += 16) {
        uint32_t baddr = addr & 0xff;
        if(baddr == 0) {
            int32_t rest_len = SW_IMAGE_LEN - addr;
            if(rest_len > 0x100) {
                rest_len = 0x100;
            }
            swire_uart_read(swire, 0x000c, 0, buffer, rest_len);
        }
        furi_string_printf(
            string,
            "DUMP %06lx:   %02lx %02lx %02lx %02lx",
            addr,
            (uint32_t)buffer[baddr + 0],
            (uint32_t)buffer[baddr + 1],
            (uint32_t)buffer[baddr + 2],
            (uint32_t)buffer[baddr + 3]);
        furi_string_cat_printf(
            string,
            " %02lx %02lx %02lx %02lx",
            (uint32_t)buffer[baddr + 4],
            (uint32_t)buffer[baddr + 5],
            (uint32_t)buffer[baddr + 6],
            (uint32_t)buffer[baddr + 7]);
        furi_string_cat_printf(
            string,
            "  %02lx %02lx %02lx %02lx",
            (uint32_t)buffer[baddr + 8],
            (uint32_t)buffer[baddr + 9],
            (uint32_t)buffer[baddr + 10],
            (uint32_t)buffer[baddr + 11]);
        furi_string_cat_printf(
            string,
            " %02lx %02lx %02lx %02lx\r\n",
            (uint32_t)buffer[baddr + 12],
            (uint32_t)buffer[baddr + 13],
            (uint32_t)buffer[baddr + 14],
            (uint32_t)buffer[baddr + 15]);
        furi_log_puts(furi_string_get_cstr(string));
    }
    furi_log_puts("DUMP FINISHED\r\n");
    free(buffer);
    furi_string_free(string);
}

void do_by_uart() {
    // SwireUart* swire = swire_uart_alloc(921600);
    SwireUart* swire = swire_uart_alloc(377804);
    swire->read_delay_per_byte_us = 35;
    uint8_t cmd[2];

    swire_uart_write1(swire, 0x0602, 0, 0x05); // CPU Stop
    swire_uart_write1(swire, 0x00b2, 0, 0x7f); // b0-b4 SWIRE
    int32_t sanity_check = swire_uart_read1(swire, 0x00b2, 0); // b0-b4 SWIRE
    FURI_LOG_W("swire", "Sanity test...");
    if(sanity_check != 0x7f) {
        FURI_LOG_W("swire", "Sanity test failed %02ld", sanity_check);
    }
    // MSPI = Memory SPI
    // CS = Chip Select
    swire_uart_write1(swire, 0x000d, 0, 0x00); // MSPI Control, CS bit active low

    // addr = Address
    swire_uart_write1(swire, 0x000c, 0, 0x03); // MSPI Data, 03 = read
    swire_uart_write1(swire, 0x000c, 0, 0x00); // MSPI read addr[2]
    swire_uart_write1(swire, 0x000c, 0, 0x00); // MSPI read addr[1]
    swire_uart_write1(swire, 0x000c, 0, 0x00); // MSPI read addr[0]

    cmd[0] = 0x00; // MSPI Data, 00 to drive MSPI Clock to initiate first read
    cmd[1] = 0x0a; // MSPI Control, auto read mode
    swire_uart_write(swire, 0x000c, 0, cmd, 2);

    swire_uart_write1(
        swire, 0x00b3, 0, 0x80); // swire mode, fifo, repeated reads from same address

    do_by_uart_dumploop(swire);

    swire_uart_write1(swire, 0x00b3, 0, 0x00); // swire mode reset to default
    swire_uart_write1(swire, 0x000d, 0, 0x01); // MSPI Control disable CS

    furi_delay_ms(500);
    SWIRE_UART_FREE(swire);
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

void app_send_welcome(SwireApp* app) {
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

void app_handle_cdc_state_changed(void* ctx, SwireUsb* sender, CdcState state) {
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
void app_handle_led_blinker(void* context) {
    SwireApp* app = (SwireApp*)context;
    app->led_state = !app->led_state;

    set_led_color(app->led_state ? app->led_color : 0);
}

void app_handle_periodic_debug_info(SwireApp* app) {
    view_port_update(app->view_port);
}

void app_set_message(SwireApp* app, const char* format, ...) {
    va_list args;
    va_start(args, format);
    furi_string_vprintf(app->message, format, args);
    va_end(args);
    view_port_update(app->view_port);
}

void app_set_blinker(SwireApp* app, uint32_t color, uint32_t interval_ms) {
    if(interval_ms == 0) {
        furi_event_loop_timer_stop(app->timer_led);
        set_led_color(color);
    } else {
        furi_event_loop_timer_start(app->timer_led, interval_ms / 2);
    }
    app->led_color = color;
}

void loop_iteration(SwireApp* app) {
    UNUSED(app);

    if(!furi_hal_gpio_read(&gpio_button_back)) {
        furi_string_set(app->command, "back button");
        furi_delay_ms(300);
        my_stop_loop();
    }
    if(!furi_hal_gpio_read(&gpio_button_down)) {
        do_read_top(75600);
    }
    if(!furi_hal_gpio_read(&gpio_button_left)) {
        // do_timer_test();
    }
    if(!furi_hal_gpio_read(&gpio_button_right)) {
        do_read_top(180000);
    }
    if(!furi_hal_gpio_read(&gpio_button_up)) {
        do_by_uart();
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

void my_stop_loop() {
    app->running = false;
    furi_event_loop_stop(app->event_loop);
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
}

SwireApp* app_alloc() {
    SwireApp* self = malloc(sizeof(SwireApp));
    furi_check(self, "app_alloc");
    self->running = true;
    self->led_state = false;
    self->message = furi_string_alloc();
    self->message2 = furi_string_alloc();
    self->tmp_str1 = furi_string_alloc();
    self->tmp_str2 = furi_string_alloc();
    self->command = furi_string_alloc();
    self->last_tick = swire_clock_get_real_tick();
    //self->queue = furi_message_queue_alloc(_QUEUE_CAPACITY, sizeof(SwMessage));
    self->event_loop = furi_event_loop_alloc();
    self->timers = timerpool_alloc(self->event_loop);
    self->timer_led = furi_event_loop_timer_alloc(
        self->event_loop, app_handle_led_blinker, FuriEventLoopTimerTypePeriodic, self);

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

    return self;
}

void app_init(SwireApp* self, ViewPort* view_port) {
    self->view_port = view_port;
    app_set_blinker_state(self, BlinkerStateIdle);
    if(self->usb != NULL) swire_usb_free(self->usb);
    self->usb = swire_usb_alloc();
    // swire_usb_set_on_rx_line(self->usb, handle_usb_rx_line, self);
    // swire_usb_set_on_state_change(self->usb, app_handle_cdc_state_changed, self);

    //furi_event_loop_timer_start(app->timer_poll, 50);
    //furi_event_loop_timer_start(app->timer_debug, 250);
    app_set_timer(app, 50, FuriEventLoopTimerTypePeriodic, loop_iteration);
    app_set_timer(app, 250, FuriEventLoopTimerTypePeriodic, app_handle_periodic_debug_info);

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
    furi_event_loop_unsubscribe(self->event_loop, swire_usb_get_event_flag_rx(self->usb));
    // furi_event_loop_unsubscribe(self->event_loop, swire_usb_get_queue(self->usb));
    swire_usb_free(self->usb);
    self->usb = NULL;
    if(self->timer_led != NULL) furi_event_loop_timer_free(self->timer_led);
    self->timer_led = NULL;
    timerpool_free(self->timers);
    self->timers = NULL;
}

void app_free(SwireApp* self) {
    if(self == NULL) return;

    if(self->timer_led != NULL) furi_event_loop_timer_free(self->timer_led);
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

void app_set_timer(
    SwireApp* app,
    uint32_t interval_ms,
    FuriEventLoopTimerType type,
    SwireAppCallback callback) {
    timerpool_submit(app->timers, interval_ms, type, (FuriEventLoopTimerCallback)callback, app);
}

void handle_command(SwireApp* app, FuriString* cmd) {
    SwireUsb* usb = app->usb;

    swire_usb_printf_line(usb, " > %s", cmd);
    if(furi_string_equal(cmd, "swire_demo info") || furi_string_equal(cmd, "info")) {
        swire_usb_printf_line(usb, "swire_demo info response start");
        swire_usb_printf_line(usb, "version=v%s", APP_VERSION);
        swire_usb_printf_line(usb, "bitrate=TODO");
        swire_usb_printf_line(usb, "trigger_delay=TODO");
        swire_usb_printf_line(usb, "end");
        return;
    }

    if(furi_string_equal(cmd, "close")) {
        swire_usb_printf_line(usb, "ok");
        furi_event_loop_stop(app->event_loop);
        return;
    }

    if(furi_string_equal(cmd, "send hex")) {
        FuriStatus status = swire_usb_readline_str(usb, cmd);
        if(status & FuriFlagError) {
            global_debug()->err_loc = 31;
            global_debug()->err = status;
            return;
        }
        swire_usb_printf_line(usb, "send hex request received %d", furi_string_utf8_length(cmd));
        return;
    }
}

int tlsr_swire_demo_app(void* p) {
    UNUSED(p);

    global_debug_init();
    swire_bitbang_global_init();

    app = app_alloc();
    // Show directions to user.
    Gui* gui = furi_record_open(RECORD_GUI);
    ViewPort* view_port = view_port_alloc();
    view_port_draw_callback_set(view_port, my_draw_callback, app);
    gui_add_view_port(gui, view_port, GuiLayerFullscreen);
    furi_delay_ms(1000); // ufbt freezes if we immediately hog the cli

    app_init(app, view_port);
    furi_event_loop_run(app->event_loop);
    app_deinit(app);

    // Typically when a pin is no longer in use, it is set to analog mode.
    furi_hal_gpio_init_simple(pin_sws, GpioModeAnalog);

    // Remove the directions from the screen.
    gui_remove_view_port(gui, view_port);
    app_free(app);
    set_led_color(0);

    global_debug_deinit();
    return 0;
}
