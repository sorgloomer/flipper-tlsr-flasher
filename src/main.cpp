#include <furi.h>

#include "src/main.hpp"

#include "src/utils/global_debug.hpp"
#include "src/swire/swire_bitbang.hpp"
#include "src/app/app.hpp"
#include "src/utils/light_rgb.hpp"
#include "src/furi/delay.hpp"

using namespace std::chrono_literals;

void swire_main() {
    FURI_LOG_I("swire", "tlsr_swire_demo_app start");

    furi::delay_ms(200ms);

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
