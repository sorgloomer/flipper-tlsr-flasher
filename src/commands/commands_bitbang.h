#pragma once

#include <furi.h>
#include <furi_hal.h>

#include "src/swire/swire_bitbang.h"
#include "src/app/app.h"

void cmd_bitbang_read(SwireApp* app);
void cmd_bitbang_read_top(SwireApp* app, uint32_t bitrate);
void cmd_bitbang_test_simple(SwireApp* app);
void cmd_bitbang_test_switching_freq(SwireApp* app);
