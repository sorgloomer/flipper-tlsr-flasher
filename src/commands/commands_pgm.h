#pragma once

#include <string.h>
#include <furi.h>
#include <furi_hal_resources.h>

#include "src/app/app.h"
#include "src/swire/swire_bitbang.h"

void cmd_pgm_init(SwireApp* app, const char* params);

void cmd_pgm_transaction_start(SwireApp* app, const char* params);
void cmd_pgm_transaction_end(SwireApp* app);

FuriStatus cmd_pgm_bytes_write(SwireApp* app, const char* params);

FuriStatus cmd_pgm_bytes_read(SwireApp* app, char* params);

FuriStatus cmd_pgm_reset(SwireApp* app);

bool cmd_pgm(SwireApp* app, FuriString* cmd);
