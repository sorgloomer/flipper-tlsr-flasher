#pragma once
#include <furi.h>
#include "swire_common.hpp"

#define SWIRE_SYSTEM_CLOCK_FREQ    (SystemCoreClock)
#define SWIRE_SYSTEM_CLOCK_CURRENT (DWT->CYCCNT)

SWIRE_INLINE uint32_t swire_clock_ticks_per_second() {
    return SystemCoreClock;
}

SWIRE_INLINE uint32_t swire_clock_get_cycclk() {
    return DWT->CYCCNT;
}

SWIRE_INLINE uint32_t swire_clock_ns_to_tick(uint32_t ns) {
    return ((uint64_t)ns) * swire_clock_ticks_per_second() / 1000000000;
}

SWIRE_INLINE uint32_t swire_clock_us_to_tick(uint32_t us) {
    return ((uint64_t)us) * swire_clock_ticks_per_second() / 1000000;
}

SWIRE_INLINE bool swire_clock_tick_elapsed(uint32_t tick) {
    // Important to balance the comparison to 0 to keep
    // integer overflows predictable
    return ((int32_t)(swire_clock_get_cycclk() - tick)) >= 0;
}

SWIRE_INLINE void swire_clock_spinwait_until_tick(uint32_t tick) {
    while(!swire_clock_tick_elapsed(tick))
        ;
}

SWIRE_INLINE int32_t cyc_min_i32(int32_t a, int32_t b) {
    // Important to balance the comparison to 0 to keep
    // integer overflows predictable
    return (a - b) < 0 ? a : b;
}
SWIRE_INLINE int32_t cyc_max_i32(int32_t a, int32_t b) {
    // Important to balance the comparison to 0 to keep
    // integer overflows predictable
    return (a - b) > 0 ? a : b;
}

#define SWIRE_CLOCK_PROGRESS_TICKS(acc, tick) swire_clock_spinwait_until_tick((acc) += (tick))
