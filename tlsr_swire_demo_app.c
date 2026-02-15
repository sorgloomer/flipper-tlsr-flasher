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

    FURI_LOG_I("swire", "tlsr_swire_demo_app start");
    furi_delay_ms(200);

    global_debug_init();
    swire_bitbang_global_init();

    SwireApp* app = app_alloc();
    global_app = app;
    app_run(app);
    global_app = NULL;
    app_free(app);

    furi_hal_gpio_init_simple(pin_sws, GpioModeAnalog);
    light_rgb_set(0);
    global_debug_deinit();
    return 0;
}
