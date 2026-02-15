#include <string.h>
#include <furi.h>
#include <furi_hal_resources.h>

#include "src/app/app.h"
#include "src/swire/swire_bitbang.h"

static SwireBitbang* cmd_swire_alloc(SwireApp* app) {
    SwireBitbang* swire = swire_bitbang_alloc_with_sws((IoPins){
        .in = &gpio_ext_pa7,
        .out = &gpio_ext_pa7,
    });
    swire_bitbang_set_bitrate(swire, app->config->bitrate);
    return swire;
}

void cmd_pgm_init(SwireApp* app, const char* params) {
    const char* rest = strchr(params, ' ');
    if(rest == NULL) return;
    int matched = sscanf(rest, "%ld %ld", &app->config->addrsize, &app->config->bitrate);
    if(matched != 2) return;

    swire_bitbang_free(app->swire);
    app->swire = cmd_swire_alloc(app);

    swire_usb_writeline_cstr(app->usb, "ok");
}

void cmd_pgm_transaction_start(SwireApp* app, const char* params) {
    const char* rest = strchr(params, ' ');
    if(rest == NULL) return;
    int32_t addr, wr, slaveid;
    int matched = sscanf(rest, "%lx %lx %lx", &addr, &wr, &slaveid);
    if(matched != 3) return;

    if(app->usb == NULL) {
        FURI_LOG_E("swire", "cmd_pgm_transaction_start swire->usb not initialized");
        return;
    }

    if(app->swire == NULL) {
        swire_usb_writeline_cstr(app->usb, "error swire not initialized");
        FURI_LOG_E("swire", "cmd_pgm_transaction_start swire->usb not initialized");
        return;
    }

    swire_bitbang_transaction_start(
        app->swire, addr, wr ? SwireBitbangRwRead : SwireBitbangRwWrite, slaveid);
    swire_bitbang_timer_join(app->swire);
}

void cmd_pgm_transaction_end(SwireApp* app) {
    if(app->usb == NULL) {
        FURI_LOG_E("swire", "cmd_pgm_transaction_end swire->usb not initialized");
        return;
    }

    if(app->swire == NULL) {
        swire_usb_writeline_cstr(app->usb, "error swire not initialized");
        FURI_LOG_E("swire", "cmd_pgm_transaction_end swire not initialized");
        return;
    }
    swire_bitbang_transaction_end(app->swire);
    swire_bitbang_timer_join(app->swire);
}

FuriStatus cmd_pgm_bytes_write(SwireApp* app, const char* params) {
    FuriStatus status;
    const char* rest = strchr(params, ' ');
    if(rest == NULL) return FuriStatusErrorParameter;
    int32_t bytecount;
    int matched = sscanf(rest, "%lx", &bytecount);
    if(matched != 1) return FuriStatusErrorParameter;

    if(app->usb == NULL) {
        FURI_LOG_E("swire", "cmd_pgm_bytes_write swire->usb not initialized");
        return FuriStatusErrorResource;
    }

    if(app->swire == NULL) {
        swire_usb_writeline_cstr(app->usb, "error swire not initialized");
        FURI_LOG_E("swire", "cmd_pgm_bytes_write swire not initialized");
        return FuriStatusErrorResource;
    }

    if(bytecount < 0) {
        swire_usb_writeline_cstr(app->usb, "error buffer too small");
        FURI_LOG_E("swire", "cmd_pgm_bytes_write buffer too small");
        return FuriStatusErrorParameter;
    }
    if(bytecount > 1024) {
        swire_usb_writeline_cstr(app->usb, "error buffer too big");
        FURI_LOG_E("swire", "cmd_pgm_bytes_write buffer too big");
        return FuriStatusErrorParameter;
    }

    uint8_t* buffer = malloc(bytecount);
    furi_check(buffer);

    status = swire_usb_read(app->usb, buffer, bytecount);
    if(status & FuriFlagError) {
        return status;
    }

    for(int i = 0; i < bytecount; i++) {
        swire_bitbang_byte_write(app->swire, buffer[i]);
    }
    swire_bitbang_timer_join(app->swire);
    swire_usb_writeline_cstr(app->usb, "ok");
    free(buffer);
    return FuriStatusOk;
}

FuriStatus cmd_pgm_bytes_read(SwireApp* app, const char* params) {
    FuriStatus status;
    const char* rest = strchr(params, ' ');
    if(rest == NULL) return FuriStatusErrorParameter;
    int32_t bytecount;
    int matched = sscanf(rest, "%lx", &bytecount);
    if(matched != 1) return FuriStatusErrorParameter;

    if(app->usb == NULL) {
        FURI_LOG_E("swire", "cmd_pgm_bytes_read swire->usb not initialized");
        return FuriStatusErrorResource;
    }

    if(app->swire == NULL) {
        swire_usb_writeline_cstr(app->usb, "error swire not initialized");
        FURI_LOG_E("swire", "cmd_pgm_bytes_read swire not initialized");
        return FuriStatusErrorResource;
    }

    if(bytecount < 0) {
        swire_usb_writeline_cstr(app->usb, "error buffer too small");
        FURI_LOG_E("swire", "cmd_pgm_bytes_read buffer too small");
        return FuriStatusErrorParameter;
    }
    if(bytecount > 1024) {
        swire_usb_writeline_cstr(app->usb, "error buffer too big");
        FURI_LOG_E("swire", "cmd_pgm_bytes_read buffer too big");
        return FuriStatusErrorParameter;
    }

    uint8_t* buffer = malloc(bytecount);
    furi_check(buffer);

    for(int i = 0; i < bytecount; i++) {
        buffer[i] = swire_bitbang_byte_read(app->swire);
    }

    swire_usb_writeline_cstr(app->usb, "ok");
    status = swire_usb_write(app->usb, buffer, bytecount);
    if(status & FuriFlagError) {
        return status;
    }

    swire_bitbang_timer_join(app->swire);
    free(buffer);
    return FuriStatusOk;
}

FuriStatus cmd_pgm_reset(SwireApp* app) {
    const GpioPin* pin_power = &gpio_ext_pb2;
    furi_hal_gpio_init_simple(pin_power, GpioModeOutputPushPull);
    furi_hal_gpio_write(pin_power, false);
    furi_delay_ms(app->config->reset_duration_ms);
    furi_hal_gpio_write(pin_power, true);
    furi_delay_ms(app->config->reset_delay_ms);
    return FuriStatusOk;
}

bool cmd_pgm(SwireApp* app, FuriString* cmd) {
    if(furi_string_start_with(cmd, "swire_init ")) {
        cmd_pgm_init(app, furi_string_get_cstr(cmd));
        return true;
    }

    if(furi_string_start_with(cmd, "trs ")) {
        cmd_pgm_transaction_start(app, furi_string_get_cstr(cmd));
        return true;
    }
    if(furi_string_equal(cmd, "tre")) {
        cmd_pgm_transaction_end(app);
        return true;
    }
    if(furi_string_start_with(cmd, "bw ")) {
        cmd_pgm_bytes_write(app, furi_string_get_cstr(cmd));
        return true;
    }
    if(furi_string_start_with(cmd, "br ")) {
        cmd_pgm_bytes_read(app, furi_string_get_cstr(cmd));
        return true;
    }
    if(furi_string_equal(cmd, "reset")) {
        cmd_pgm_reset(app);
        return true;
    }
    return false;
}
