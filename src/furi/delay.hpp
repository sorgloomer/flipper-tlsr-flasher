#pragma once

#include "src/furi/duration.hpp"

namespace furi {

void delay_ms(u32ms duration);
void delay_us(u32us duration);
void delay_tick(uint32_t duration);

}
