#include <memory>
#include <furi.h>

#include "src/main.hpp"

#include "src/utils/global_debug.hpp"
#include "src/swire/swire_bitbang.hpp"
#include "src/app/app.hpp"
#include "src/utils/light_rgb.hpp"
#include "src/furi/delay.hpp"

using namespace std::chrono_literals;

#define _TRACE_CHECPOINT(...) FURI_LOG_T(TAG, "swire_main checkpoint " __VA_ARGS__)

void swire_main() {
    FURI_LOG_I("swire", "tlsr_swire_demo_app start");

    furi::delay_ms(200ms);

    global_debug_init();
    swire_bitbang_global_init();

    {
        auto app = std::make_unique<SwireApp>();
        global_app = app.get();
        {
            Implicits implicits{.timers = app->timers.get(), .app = app.get()};
            [[maybe_unused]] auto _ = main_async(&implicits);
            _TRACE_CHECPOINT("3.1");
            app->run();
            _TRACE_CHECPOINT("3.2");
        }
        global_app = nullptr;
    }

    furi_hal_gpio_init_simple(pin_sws, GpioModeAnalog);
    light_rgb_set(0);
    global_debug_deinit();
}
