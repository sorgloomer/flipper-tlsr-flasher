#include <furi.h>
#include <furi_hal.h>
#include <furi/core/string.h>
#include <gui/gui.h>

#include "src/swire/swire_clock.h"
#include "src/swire/swire_bitbang.h"
#include "src/swire/swire_uart.h"
#include "src/usb.h"

// For list of pins see https://github.com/flipperdevices/flipperzero-firmware/blob/dev/firmware/targets/f7/furi_hal/furi_hal_resources.c
const GpioPin* const pin_sws = &gpio_ext_pa7;
const GpioPin* const pin_back = &gpio_button_back;

typedef struct {
    SwireUsb* usb;
    FuriThread* thread;
    FuriEventLoop* event_loop;
    bool running;
    uint32_t last_tick;
    FuriEventLoopTimer* timer;
} SwireApp;
SwireApp* app = NULL;

void my_stop_loop();
void handle_usb_rx_line(void* context, SwireUsb* sender, FuriString* line);

static void my_draw_callback(Canvas* canvas, void* context) {
    UNUSED(context);
    canvas_set_font(canvas, FontPrimary);
    canvas_draw_str(canvas, 5, 8, "TLSR SWIRE");
    //FuriString* str_ticks = furi_string_alloc_vprintf("%d", last_tick);
    //canvas_draw_str(canvas, 5, 16, furi_string_get_cstr(str));
    //furi_string_free(str_ticks);
}

#define MY_LOG_I(format, ...) FURI_LOG_I("swire", format, ##__VA_ARGS__)

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

#define IMAGE_LEN 144000
    //#define IMAGE_LEN 16
    for(uint32_t addr = 0; addr < IMAGE_LEN; addr += 16) {
        uint32_t baddr = addr & 0xff;
        if(baddr == 0) {
            int32_t rest_len = IMAGE_LEN - addr;
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

void loop_iteration(void* ctx) {
    SwireApp* app = (SwireApp*)ctx;
    swire_usb_write_cstr(app->usb, "beat\r\n");

    if(!furi_hal_gpio_read(&gpio_button_back)) {
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
}

void my_stop_loop() {
    app->running = false;
    furi_event_loop_stop(app->event_loop);
}

SwireApp* app_alloc() {
    SwireApp* self = malloc(sizeof(SwireApp));
    furi_check(self, "app_alloc");
    self->running = true;
    self->last_tick = swire_clock_get_real_tick();
    self->event_loop = furi_event_loop_alloc();
    self->thread = furi_thread_get_current();
    self->timer = furi_event_loop_timer_alloc(
        self->event_loop, loop_iteration, FuriEventLoopTimerTypePeriodic, self);
    furi_event_loop_timer_start(self->timer, 1000);
    self->usb = NULL;
    self->usb = swire_usb_alloc(self->event_loop);
    swire_usb_set_on_rx_line(self->usb, handle_usb_rx_line, self);
    return self;
}

void app_free(SwireApp* self) {
    if(self == NULL) return;
    furi_event_loop_timer_free(self->timer);
    swire_usb_free(self->usb);
    furi_event_loop_free(self->event_loop);
}

void handle_usb_rx_line(void* context, SwireUsb* sender, FuriString* line) {
    UNUSED(sender);
    UNUSED(line);
    SwireApp* app = (SwireApp*)context;
    SwireUsb* usb = app->usb;

    swire_usb_printf_line(usb, " > %s", line);
    if(furi_string_equal(line, "swire_demo info")) {
        swire_usb_printf_line(usb, "swire_demo info response start");
        swire_usb_printf_line(usb, "version=v0.2");
        swire_usb_printf_line(usb, "bitrate=TODO");
        swire_usb_printf_line(usb, "trigger_delay=TODO");
        swire_usb_printf_line(usb, "end");
    }

    if(furi_string_equal(line, "send hex")) {
        swire_usb_readline_str(usb, line);
        swire_usb_printf_line(usb, "send hex request received %d", furi_string_utf8_length(line));
    }
}

int tlsr_swire_demo_app(void* p) {
    UNUSED(p);

    swire_bitbang_global_init();

    // Show directions to user.
    Gui* gui = furi_record_open(RECORD_GUI);
    ViewPort* view_port = view_port_alloc();
    view_port_draw_callback_set(view_port, my_draw_callback, app);
    gui_add_view_port(gui, view_port, GuiLayerFullscreen);
    furi_delay_ms(1000);

    app = app_alloc();
    furi_event_loop_run(app->event_loop);
    app_free(app);

    // Typically when a pin is no longer in use, it is set to analog mode.
    furi_hal_gpio_init_simple(pin_sws, GpioModeAnalog);

    // Remove the directions from the screen.
    gui_remove_view_port(gui, view_port);

    return 0;
}
