
#include "src/app/app.hpp"
#include "src/commands/commands_pgm.hpp"
#include "src/commands/commands.hpp"

static bool cmd_matches(const std::string& input, const char* cmd);
static const char* cmd_get_params(const std::string& cmd);

void handle_text_command(SwireApp* app, std::string& cmd) {
    SwireUsb* usb = app->usb;
    const char* cargs = cmd_get_params(cmd);

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

    if(cmd == "close" || cmd == "exit") {
        swire_usb_printf_ln(usb, "ok");
        app->event_loop.stop();
        return;
    }
    if(cmd == "ga7g4drb close") {
        swire_usb_printf_ln(usb, "ga7g4drb closing");
        app->event_loop.stop();
        return;
    }

    if(cmd_matches(cmd, "swire_init")) {
        cmd_pgm_init(app, cargs);
        return;
    }
    if(cmd_matches(cmd, "trs")) {
        cmd_pgm_transaction_start(app, cargs);
        return;
    }
    if(cmd_matches(cmd, "tre")) {
        cmd_pgm_transaction_end(app, cargs);
        return;
    }
    if(cmd_matches(cmd, "bw")) {
        cmd_pgm_bytes_write(app, cargs);
        return;
    }
    if(cmd_matches(cmd, "br")) {
        cmd_pgm_bytes_read(app, cargs);
        return;
    }
    if(cmd_matches(cmd, "reset")) {
        cmd_pgm_reset(app, cargs);
        return;
    }

    if(cmd_matches(cmd, "trw")) {
        cmd_pgm_transaction_write(app, cargs);
        return;
    }
    if(cmd_matches(cmd, "trr")) {
        cmd_pgm_transaction_read(app, cargs);
        return;
    }
    if(cmd_matches(cmd, "wfr")) {
        cmd_pgm_wait_flash_ready(app, cargs);
        return;
    }
    if(cmd_matches(cmd, "wmspi")) {
        cmd_pgm_wait_mspi(app, cargs);
        return;
    }

    if(cmd_matches(cmd, "slus")) {
        cmd_pgm_sleep_us(app, cargs);
        return;
    }

    swire_usb_printf_ln(app->usb, "error unknown command: %s", cmd.c_str());
}

static bool cmd_matches(const std::string& input, const char* cmd) {
    if(!input.starts_with(cmd)) {
        return false;
    }
    unsigned int cmdlen = strlen(cmd);
    if(input.size() == cmdlen) {
        return true;
    }
    if(input.size() > cmdlen && input[cmdlen] == ' ') {
        return true;
    }
    return false;
}

static const char* cmd_get_params(const std::string& cmd) {
    const char* ccmd = cmd.c_str();
    const char* space = strchr(ccmd, ' ');
    return space != NULL ? space + 1 : ccmd + strlen(ccmd);
}
