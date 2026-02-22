#pragma once

#include "src/furi/duration.hpp"

namespace furi {

void delay_ms(milli duration);
void delay_us(micro duration);
void delay_tick(uint32_t duration);

}
