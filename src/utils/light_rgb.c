#include <furi.h>
#include <furi_hal.h>

#include "src/utils/light_rgb.h"

void light_rgb_set(uint32_t color) {
    furi_hal_light_set(LightRed, (color >> 16) & 0xff);
    furi_hal_light_set(LightGreen, (color >> 8) & 0xff);
    furi_hal_light_set(LightBlue, (color >> 0) & 0xff);
}
