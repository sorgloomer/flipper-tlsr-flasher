#include "src/rgb.h"
#include <furi.h>
#include <furi_hal.h>

void set_led_color(uint32_t color) {
    furi_hal_light_set(LightRed, (color >> 16) & 0xff);
    furi_hal_light_set(LightGreen, (color >> 8) & 0xff);
    furi_hal_light_set(LightBlue, (color >> 0) & 0xff);
}
