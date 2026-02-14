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

int tlsr_swire_demo_app(void* p) {
    UNUSED(p);

    global_debug_init();
    swire_bitbang_global_init();

    app = app_alloc();
    furi_delay_ms(1000); // ufbt freezes if we immediately hog the cli
    app_init(app);
    furi_event_loop_run(app->event_loop);
    app_deinit(app);

    // Typically when a pin is no longer in use, it is set to analog mode.
    furi_hal_gpio_init_simple(pin_sws, GpioModeAnalog);

    app_free(app);
    light_rgb_set(0);

    global_debug_deinit();
    return 0;
}
