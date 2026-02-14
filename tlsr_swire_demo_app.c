#include <furi.h>
#include <furi_hal.h>
#include <furi/core/string.h>

#include <gui/gui.h>
#include <gui/modules/variable_item_list.h>
#include <gui/view_dispatcher.h>

#include "src/app/app.h"
#include "src/swire/swire_bitbang.h"
#include "src/usb.h"
#include "src/global_debug.h"
#include "src/utils/hex.h"
#include "src/utils/light_rgb.h"

#define SW_LOG_I(format, ...) FURI_LOG_I("swire", format, ##__VA_ARGS__)
#define SW_QUEUE_CAPACITY     32

uint32_t worker_callback(void* ctx);
void handle_app_message(FuriEventLoopObject* object, void* ctx);
void app_handle_rx_one(SwireApp* app);

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
    light_rgb_set(0);

    global_debug_deinit();
    return 0;
}
