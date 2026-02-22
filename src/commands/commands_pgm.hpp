#pragma once

#include <string>
#include <furi.h>
#include <furi_hal_resources.h>

#include "src/app/app.hpp"
#include "src/swire/swire_bitbang.hpp"

void cmd_pgm_init(SwireApp* app, const char* params);

void cmd_pgm_transaction_start(SwireApp* app, const char* params);
void cmd_pgm_transaction_end(SwireApp* app, const char* params);

FuriStatus cmd_pgm_bytes_write(SwireApp* app, const char* params);

FuriStatus cmd_pgm_bytes_read(SwireApp* app, const char* params);

FuriStatus cmd_pgm_reset(SwireApp* app, const char* params);

FuriStatus cmd_pgm_transaction_read(SwireApp* app, const char* cargs);
FuriStatus cmd_pgm_transaction_write(SwireApp* app, const char* cargs);

bool cmd_pgm(SwireApp* app, std::string& cmd);
