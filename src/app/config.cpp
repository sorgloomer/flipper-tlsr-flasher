#include "src/app/config.hpp"
#include "src/app/bitrate_options.hpp"

SwireConfig* swire_config_alloc() {
    SwireConfig* self = (SwireConfig*)malloc(sizeof(SwireConfig));
    furi_check(self);

    self->bitrate = BitrateOptions__default;
    self->addrsize = 3;
    self->reset_duration_ms = 200;
    self->reset_delay_ms = 70;
    self->trigger_duration_us = 10;
    self->trigger_delay_us = 10;
    self->keep_powered_duration_ms = 200;

    return self;
}

void swire_config_free(SwireConfig* self) {
    if(self == NULL) return;
    free(self);
}
