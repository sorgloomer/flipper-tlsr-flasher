#include "src/app/config.h"

SwireConfig* swire_config_alloc() {
    SwireConfig* self = malloc(sizeof(SwireConfig));
    furi_check(self);
    self->bitrate = 480000;
    return self;
}

void swire_config_free(SwireConfig* self) {
    if(self == NULL) return;
    free(self);
}
