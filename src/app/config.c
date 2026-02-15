#include "src/app/config.h"
#include "src/app/bitrate_options.h"

SwireConfig* swire_config_alloc() {
    SwireConfig* self = malloc(sizeof(SwireConfig));
    furi_check(self);
    self->bitrate = BitrateOptions__default;
    return self;
}

void swire_config_free(SwireConfig* self) {
    if(self == NULL) return;
    free(self);
}
