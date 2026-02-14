#pragma once

#include <furi.h>

void hex(FuriString* output, FuriString* input, bool whitespace) {
    furi_string_reset(output);

    for(int32_t i = 0, len = furi_string_size(input); i < len; i++) {
        char c = furi_string_get_char(input, i);
        furi_string_cat_printf(
            output, (i == 0 || !whitespace) ? "%02x" : " %02x", (unsigned int)c);
    }
}
