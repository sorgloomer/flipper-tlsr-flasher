#pragma once

#include <furi.h>
#include <furi_hal.h>

#include "src/swire/swire_bitbang.h"

void cmd_bitbang_read();
void cmd_bitbang_read_top(uint32_t bitrate);
void cmd_bitbang_test_simple();
