#include <furi/core/kernel.h>
#include "delay.hpp"

namespace furi {

void delay_ms(milli duration) {
    furi_delay_ms(duration.count());
}
void delay_us(micro duration) {
    furi_delay_us(duration.count());
}

void delay_tick(uint32_t duration) {
    furi_delay_tick(duration);
}

}
