
#include "src/app/app.hpp"
#include "src/utils/global_debug.hpp"
#include "src/commands/commands_bitbang.hpp"
#include "src/commands/commands_pgm.hpp"
#include "src/commands/commands.hpp"

void handle_text_command(SwireApp* app, std::string& cmd) {
    SwireUsb* usb = app->usb;

    swire_usb_printf_ln(usb, "# > %s", cmd.c_str());
    if(cmd == "ga7g4drb info" || cmd == "info") {
        swire_usb_printf_ln(usb, "flitswire info response start");
        swire_usb_printf_ln(usb, "version=v%s", APP_VERSION);
        swire_usb_printf_ln(usb, "bitrate=TODO");
        swire_usb_printf_ln(usb, "trigger_delay=TODO");
        swire_usb_printf_ln(usb, "end");
        return;
    }

    if(cmd == "ping") {
        swire_usb_printf_ln(usb, "pong");
        return;
    }

    if(cmd == "close") {
        swire_usb_printf_ln(usb, "ok");
        furi_event_loop_stop(app->event_loop);
        return;
    }
    if(cmd == "ga7g4drb close") {
        swire_usb_printf_ln(usb, "ga7g4drb closing");
        furi_event_loop_stop(app->event_loop);
        return;
    }

    if(cmd == "send hex") {
        FuriStatus status = swire_usb_readline_str(usb, cmd);
        if((FuriFlag)status & FuriFlagError) {
            global_debug()->err_loc = 31;
            global_debug()->err = status;
            return;
        }
        swire_usb_printf_ln(usb, "send hex request received %d", cmd.size());
        return;
    }

    if(cmd_pgm(app, cmd)) {
        return;
    }

    if(cmd == "bbt") {
        cmd_bitbang_test_simple(app);
    }
}
