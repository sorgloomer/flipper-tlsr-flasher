#pragma once

#include "src/app/app.h"
#include "src/global_debug.h"
#include "src/swire/swire_bitbang.h"

void handle_command(SwireApp* app, FuriString* cmd) {
    SwireUsb* usb = app->usb;

    swire_usb_printf_line(usb, " > %s", cmd);
    if(furi_string_equal(cmd, "ga7g4drb info") || furi_string_equal(cmd, "info")) {
        swire_usb_printf_line(usb, "flitswire info response start");
        swire_usb_printf_line(usb, "version=v%s", APP_VERSION);
        swire_usb_printf_line(usb, "bitrate=TODO");
        swire_usb_printf_line(usb, "trigger_delay=TODO");
        swire_usb_printf_line(usb, "end");
        return;
    }

    if(furi_string_equal(cmd, "ga7g4drb close") || furi_string_equal(cmd, "close")) {
        swire_usb_printf_line(usb, "ok");
        furi_event_loop_stop(app->event_loop);
        return;
    }

    if(furi_string_equal(cmd, "send hex")) {
        FuriStatus status = swire_usb_readline_str(usb, cmd);
        if(status & FuriFlagError) {
            global_debug()->err_loc = 31;
            global_debug()->err = status;
            return;
        }
        swire_usb_printf_line(usb, "send hex request received %d", furi_string_utf8_length(cmd));
        return;
    }

    if(furi_string_equal(cmd, "bbt")) {
        SwireBitbang* swire = swire_bitbang_alloc_with_sws(&gpio_ext_pa7, &gpio_ext_pa6);
        swire_bitbang_byte_write(swire, 0x05);
        swire_bitbang_free(swire);
    }
}
