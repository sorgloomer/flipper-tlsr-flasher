#include <furi.h>
#include <gui/gui.h>

#include "swire.h"

// For list of pins see https://github.com/flipperdevices/flipperzero-firmware/blob/dev/firmware/targets/f7/furi_hal/furi_hal_resources.c
const GpioPin* const pin_sws = &gpio_ext_pa7;
const GpioPin* const pin_back = &gpio_button_back;
FuriEventLoop* event_loop;
bool running = true;
uint32_t last_tick;

void my_stop_loop();

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

    Swire* swire = swire_alloc_with_sws(&gpio_ext_pa7, &gpio_ext_pa6);

    swire_transaction_start(swire, 0x0602, RwWrite, 0);
    swire_byte_write(swire, 0x05);
    swire_transaction_end(swire);
    swire_transaction_start(swire, 0x00b2, RwWrite, 0);
    swire_byte_write(swire, 0x7f);
    swire_transaction_end(swire);
    swire_transaction_start(swire, 0x00b2, RwRead, 0);
    int32_t b1 = swire_byte_read(swire);
    swire_transaction_end(swire);

    MY_LOG_I("CHECKPOINT 1 %hx", (uint16_t)b1);

    furi_delay_ms(50);

    swire_transaction_start(swire, 0x0d, RwWrite, 0);
    swire_byte_write(swire, 0);
    swire_transaction_end(swire);

    swire_transaction_start(swire, 0x0c, RwWrite, 0);
    swire_byte_write(swire, 0x03);
    swire_transaction_end(swire);

    swire_transaction_start(swire, 0x0c, RwWrite, 0);
    swire_byte_write(swire, 0x00);
    swire_transaction_end(swire);

    swire_transaction_start(swire, 0x0c, RwWrite, 0);
    swire_byte_write(swire, 0x00);
    swire_transaction_end(swire);

    swire_transaction_start(swire, 0x0c, RwWrite, 0);
    swire_byte_write(swire, 0x00);
    swire_transaction_end(swire);

    swire_transaction_start(swire, 0x0c, RwWrite, 0);
    swire_byte_write(swire, 0x00);
    swire_byte_write(swire, 0x0a);
    swire_transaction_end(swire);

    swire_transaction_start(swire, 0xb3, RwWrite, 0);
    swire_byte_write(swire, 0x80);
    swire_transaction_end(swire);
    if(swire_has_error(swire)) goto exit;

    for(int j = 0; j < 16; j++) {
        swire_transaction_start(swire, 0x0c, RwRead, 0);
        for(int i = 0; i < 16; i++) {
            row[i] = swire_byte_read(swire);
        }
        swire_transaction_end(swire);

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
    swire_transaction_end_force(swire);
    swire_free(swire);
}

void do_read_top(uint32_t bitrate) {
    swire_global_init_with_bitrate(bitrate);
    swire_global_log_params();
    do_read();
    furi_delay_ms(500);
}

void loop_iteration(void* p) {
    UNUSED(p);
    if(!furi_hal_gpio_read(&gpio_button_back)) {
        furi_delay_ms(300);
        my_stop_loop();
    }
    if(!furi_hal_gpio_read(&gpio_button_down)) {
        do_read_top(75600);
    }
    if(!furi_hal_gpio_read(&gpio_button_left)) {
        do_read_top(120000);
    }
    if(!furi_hal_gpio_read(&gpio_button_right)) {
        do_read_top(180000);
    }
    if(!furi_hal_gpio_read(&gpio_button_up)) {
        do_read_top(962000);
    }
}

void my_stop_loop() {
    running = false;
    furi_event_loop_stop(event_loop);
}

int tlsr_swire_demo_app(void* p) {
    UNUSED(p);

    swire_global_init();

    // Show directions to user.
    Gui* gui = furi_record_open(RECORD_GUI);
    ViewPort* view_port = view_port_alloc();
    view_port_draw_callback_set(view_port, my_draw_callback, NULL);
    gui_add_view_port(gui, view_port, GuiLayerFullscreen);

    event_loop = furi_event_loop_alloc();
    FuriEventLoopTimer* timer = furi_event_loop_timer_alloc(
        event_loop, loop_iteration, FuriEventLoopTimerTypePeriodic, NULL);
    furi_event_loop_timer_start(timer, furi_ms_to_ticks(50));
    furi_event_loop_run(event_loop);
    furi_event_loop_timer_free(timer);
    furi_event_loop_free(event_loop);

    // Typically when a pin is no longer in use, it is set to analog mode.
    furi_hal_gpio_init_simple(pin_sws, GpioModeAnalog);

    // Remove the directions from the screen.
    gui_remove_view_port(gui, view_port);

    return 0;
}
