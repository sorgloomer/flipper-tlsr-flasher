#pragma once

#include <furi_hal.h>

#define SWIRE_INLINE __attribute__((always_inline)) inline

struct IoPins {
    const GpioPin* in;
    const GpioPin* out;
};
