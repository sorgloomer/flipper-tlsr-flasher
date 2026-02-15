#include <furi.h>

#include "src/main.h"
#include "src/utils/global_debug.h"
#include "src/swire/swire_bitbang.h"
#include "src/app/app.h"
#include "src/utils/light_rgb.h"

void swire_main() {
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
}
