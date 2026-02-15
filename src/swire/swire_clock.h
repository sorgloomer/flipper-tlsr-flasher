#pragma once
#include <furi.h>
#include "swire_common.h"

#define SWIRE_SYSTEM_CLOCK_FREQ    (SystemCoreClock)
#define SWIRE_SYSTEM_CLOCK_CURRENT (DWT->CYCCNT)

SWIRE_INLINE uint32_t swire_clock_ticks_per_second() {
    return SystemCoreClock;
}

SWIRE_INLINE uint32_t swire_clock_get_real_tick() {
    return DWT->CYCCNT;
}

SWIRE_INLINE uint32_t swire_clock_ns_to_tick(uint32_t ns) {
    return ((uint64_t)ns) * swire_clock_ticks_per_second() / 1000000000;
}

SWIRE_INLINE uint32_t swire_clock_us_to_tick(uint32_t us) {
    return ((uint64_t)us) * swire_clock_ticks_per_second() / 1000000;
}

SWIRE_INLINE bool swire_clock_tick_elapsed(uint32_t tick) {
    // signed comparison to handle tick overflow
    return ((int32_t)(swire_clock_get_real_tick() - tick)) >= 0;
}

SWIRE_INLINE void swire_clock_spinwait_until_tick(uint32_t tick) {
    while(!swire_clock_tick_elapsed(tick))
        ;
}

#define SWIRE_CLOCK_PROGRESS_TICKS(acc, tick) swire_clock_spinwait_until_tick((acc) += (tick))
