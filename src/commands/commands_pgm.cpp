#include <memory>
#include <furi.h>
#include <furi_hal_resources.h>

#include "src/app/app.hpp"
#include "src/utils/str_printf.hpp"
#include "src/swire/swire_bitbang.hpp"
#include "src/commands/commands_pgm.hpp"

#define _OK_RESPONSES           1
#define _TRANSACTION_CHUNK_SIZE 64

static SwireBitbang* cmd_swire_alloc(SwireApp* app);

static SwireBitbang* cmd_swire_alloc(SwireApp* app) {
    SwireBitbang* swire = swire_bitbang_alloc_with_sws((IoPins){
        .in = &gpio_ext_pa7,
        .out = &gpio_ext_pa7,
    });
    swire_bitbang_set_bitrate(swire, app->config->bitrate);
    return swire;
}

void cmd_pgm_init(SwireApp* app, const char* cargs) {
    if(app->usb == NULL) {
        FURI_LOG_E("swire", "cmd_pgm_transaction_start swire->usb not initialized");
        return;
    }
    if(cargs == NULL) {
        app_usb_printf_ln(app, "error no params");
        return;
    }
    int matched = sscanf(cargs, "%ld %ld", &app->config->addrsize, &app->config->bitrate);
    if(matched != 2) {
        app_usb_printf_ln(app, "error params %d", matched);
        return;
    }

    swire_bitbang_free(app->swire);
    app->swire = cmd_swire_alloc(app);

#if _OK_RESPONSES == 1
    swire_usb_writeline_cstr(app->usb, "ok");
#endif
}

void cmd_pgm_transaction_start(SwireApp* app, const char* cargs) {
    if(app->usb == NULL) {
        FURI_LOG_E("swire", "cmd_pgm_transaction_start swire->usb not initialized");
        return;
    }
    if(cargs == NULL) {
        swire_usb_printf_ln(app->usb, "error no params");
        return;
    }
    int32_t addr, wr, slave_id;
    int matched = sscanf(cargs, "%lx %lx %lx", &addr, &wr, &slave_id);
    if(matched != 3) {
        swire_usb_printf_ln(app->usb, "error params %d");
        return;
    }

    if(app->swire == NULL) {
        swire_usb_writeline_cstr(app->usb, "error swire not initialized");
        FURI_LOG_E("swire", "cmd_pgm_transaction_start swire->usb not initialized");
        return;
    }

    swire_bitbang_transaction_start(
        app->swire, addr, wr ? SwireBitbangRwRead : SwireBitbangRwWrite, slave_id);
    swire_bitbang_timer_join(app->swire);
#if _OK_RESPONSES == 1
    swire_usb_writeline_cstr(app->usb, "ok");
#endif
}

FuriStatus cmd_pgm_transaction_write(SwireApp* app, const char* cargs) {
    FuriStatus status;
    if(app->usb == NULL) {
        FURI_LOG_E("swire", "cmd_pgm_transaction_start swire->usb not initialized");
        return FuriStatusError;
    }
    if(cargs == NULL) {
        swire_usb_printf_ln(app->usb, "error no params");
        return FuriStatusErrorParameter;
    }
    int32_t addr, slave_id, bytecount;
    int matched = sscanf(cargs, "%lx %lx %lx", &addr, &slave_id, &bytecount);
    if(matched != 3) {
        swire_usb_printf_ln(app->usb, "error params %d");
        return FuriStatusErrorParameter;
    }

    if(bytecount < 0 || bytecount > 1024) {
        swire_usb_printf_ln(app->usb, "error params bytecount");
        return FuriStatusErrorParameter;
    }

    if(app->swire == NULL) {
        swire_usb_writeline_cstr(app->usb, "error swire not initialized");
        FURI_LOG_E("swire", "cmd_pgm_transaction_start swire->usb not initialized");
        return FuriStatusError;
    }

    swire_bitbang_transaction_start(app->swire, addr, SwireBitbangRwWrite, slave_id);

    uint8_t buffer[_TRANSACTION_CHUNK_SIZE];
    while(bytecount > 0) {
        int32_t chunk = MIN(bytecount, _TRANSACTION_CHUNK_SIZE);
        status = swire_usb_read(app->usb, (uint8_t*)&buffer, chunk);
        if(status != FuriStatusOk) {
            FURI_LOG_E(
                "swire",
                "error: could not read from usb, err: %lx, left: %ld, debug_rx: %ld",
                (uint32_t)status,
                (uint32_t)bytecount,
                (uint32_t)swire_usb_get_debug_rx(app->usb));
            swire_usb_printf_ln(
                app->usb,
                "error: could not read from usb, err: %lx, left: %ld, debug_rx: %ld",
                (uint32_t)status,
                (uint32_t)bytecount,
                (uint32_t)swire_usb_get_debug_rx(app->usb));
            return status;
        }
        for(int i = 0; i < chunk; i++) {
            swire_bitbang_byte_write(app->swire, buffer[i]);
        }
        bytecount -= chunk;
    }
    swire_bitbang_transaction_end(app->swire);

    swire_bitbang_timer_join(app->swire);
#if _OK_RESPONSES == 1
    swire_usb_writeline_cstr(app->usb, "ok");
#endif
    return FuriStatusOk;
}

FuriStatus cmd_pgm_transaction_read(SwireApp* app, const char* cargs) {
    FuriStatus status;
    if(app->usb == NULL) {
        FURI_LOG_E("swire", "cmd_pgm_transaction_start swire->usb not initialized");
        return FuriStatusError;
    }
    if(cargs == NULL) {
        swire_usb_printf_ln(app->usb, "error no params");
        return FuriStatusErrorParameter;
    }
    int32_t addr, slave_id, bytecount;
    int matched = sscanf(cargs, "%lx %lx %lx", &addr, &slave_id, &bytecount);
    if(matched != 3) {
        swire_usb_printf_ln(app->usb, "error params %d");
        return FuriStatusErrorParameter;
    }

    if(bytecount < 0 || bytecount > 1024) {
        swire_usb_printf_ln(app->usb, "error params bytecount");
        return FuriStatusErrorParameter;
    }

    if(app->swire == NULL) {
        swire_usb_writeline_cstr(app->usb, "error swire not initialized");
        FURI_LOG_E("swire", "cmd_pgm_transaction_start swire->usb not initialized");
        return FuriStatusError;
    }

    swire_bitbang_transaction_start(app->swire, addr, SwireBitbangRwRead, slave_id);

    uint8_t buffer[_TRANSACTION_CHUNK_SIZE];
    bool had_read_error = false;
    int32_t err_left;
    int32_t byte = 0xff;
    swire_usb_printf_ln(app->usb, "data %lx", (int32_t)bytecount);
    while(bytecount > 0) {
        int32_t chunk = MIN(bytecount, _TRANSACTION_CHUNK_SIZE);
        for(int i = 0; i < chunk; i++) {
            if(!had_read_error) byte = swire_bitbang_byte_read(app->swire);
            if(byte < 0) {
                err_left = bytecount;
                had_read_error = true;
                byte &= 0xff;
            }
            buffer[i] = byte;
        }
        status = swire_usb_write(app->usb, (uint8_t*)buffer, chunk);
        if(status != FuriStatusOk) {
            FURI_LOG_E(
                "swire",
                "error: could not write to usb, err: %lx left: %ld",
                (uint32_t)status,
                bytecount);
            swire_usb_printf_ln(
                app->usb, "error: could not write to usb, err: %lx left: %ld", status, bytecount);
            return status;
        }
        bytecount -= chunk;
    }
    swire_bitbang_transaction_end(app->swire);

    swire_bitbang_timer_join(app->swire);

    if(had_read_error) {
        swire_usb_printf_ln(app->usb, "error: could not read from swire, left: %ld", err_left);
        return FuriStatusError;
    } else {
        swire_usb_writeline_cstr(app->usb, "ok");
        return FuriStatusOk;
    }
}

void cmd_pgm_transaction_end(SwireApp* app, const char* cargs) {
    UNUSED(cargs);
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
#if _OK_RESPONSES == 1
    swire_usb_writeline_cstr(app->usb, "ok");
#endif
}

#define REG_MSPI_DATA                     0x000c
#define REG_MSPI_CONTROL                  0x000c
#define MSPI_FLASH_CMD_GET_STATUS         0x05
#define MSPI_FLASH_STATUS_FLAG_BUSY       0x01
#define MSPI_FLASH_CMD_INITIATE_READ      0x00
#define MSPI_FLASH_CONTROL_MASTER_SPI_RD  0x08 // read
#define MSPI_FLASH_CONTROL_MASTER_SPI_SDO 0x02 // auto
#define MSPI_FLASH_CONTROL_AUTOREAD \
    (MSPI_FLASH_CONTROL_MASTER_SPI_RD | MSPI_FLASH_CONTROL_MASTER_SPI_SDO)

FuriStatus cmd_pgm_wait_flash_ready(SwireApp* app, const char* cargs) {
    uint32_t slave_id = 0;
    int32_t timeout = 1000;
    sscanf(cargs, "%ld %lx", &slave_id, &timeout);
    if(timeout < 0) timeout = 1000;

    // swire_bitbang_transaction_start(app->swire, REG_MSPI_DATA, SwireBitbangRwWrite, slave_id);
    // swire_bitbang_byte_write(app->swire, MSPI_FLASH_CMD_INITIATE_READ);
    // swire_bitbang_transaction_end(app->swire);

    // swire_bitbang_transaction_start(app->swire, REG_MSPI_CONTROL, SwireBitbangRwWrite, slave_id);
    // swire_bitbang_byte_write(app->swire, MSPI_FLASH_CONTROL_AUTOREAD);
    // swire_bitbang_transaction_end(app->swire);

    uint32_t timeout_deadline = furi_get_tick() + timeout;
    for(;;) {
        swire_bitbang_transaction_start(app->swire, REG_MSPI_DATA, SwireBitbangRwWrite, slave_id);
        swire_bitbang_byte_write(app->swire, MSPI_FLASH_CMD_GET_STATUS);
        swire_bitbang_transaction_end(app->swire);

        swire_bitbang_transaction_start(app->swire, REG_MSPI_DATA, SwireBitbangRwRead, slave_id);
        int32_t data = swire_bitbang_byte_read(app->swire);
        swire_bitbang_transaction_end(app->swire);
        if((data & MSPI_FLASH_STATUS_FLAG_BUSY) == 0) {
#if _OK_RESPONSES == 1
            swire_usb_writeline_cstr(app->usb, "ok");
#endif
            return FuriStatusOk;
        }
        if((int32_t)(furi_get_tick() - timeout_deadline) > 0) {
            auto str = str_printf("error timeout flash_status=%ld", data);
            swire_usb_writeline_str(app->usb, str);
            return FuriStatusErrorTimeout;
        }
    }
}

FuriStatus cmd_pgm_bytes_write(SwireApp* app, const char* cargs) {
    if(app->usb == NULL) {
        FURI_LOG_E("swire", "cmd_pgm_bytes_write swire->usb not initialized");
        return FuriStatusErrorResource;
    }
    FuriStatus status;
    if(cargs == NULL) {
        swire_usb_printf_ln(app->usb, "error no params");
        return FuriStatusErrorParameter;
    }
    int32_t bytecount;
    int matched = sscanf(cargs, "%lx", &bytecount);
    if(matched != 1) {
        swire_usb_printf_ln(app->usb, "error params matched %d", matched);
        return FuriStatusErrorParameter;
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

    uint8_t buffer[_TRANSACTION_CHUNK_SIZE];
    while(bytecount > 0) {
        int32_t chunksize = MIN(_TRANSACTION_CHUNK_SIZE, bytecount);
        status = swire_usb_read(app->usb, buffer, chunksize);
        if((FuriFlag)status & FuriFlagError) {
            app->swire->error = status == FuriStatusErrorTimeout ? SwireBitbangErrorTimeout :
                                                                   SwireBitbangErrorUnknown;
            return status;
        }

        for(int i = 0; i < chunksize; i++) {
            swire_bitbang_byte_write(app->swire, buffer[i]);
        }
        bytecount -= chunksize;
    }
    swire_bitbang_timer_join(app->swire);
#if _OK_RESPONSES == 1
    swire_usb_writeline_cstr(app->usb, "ok");
#endif
    return FuriStatusOk;
}

FuriStatus cmd_pgm_bytes_read(SwireApp* app, const char* cargs) {
    if(app->usb == NULL) {
        FURI_LOG_E("swire", "cmd_pgm_bytes_read swire->usb not initialized");
        return FuriStatusErrorResource;
    }
    FuriStatus status;
    if(cargs == NULL) {
        swire_usb_printf_ln(app->usb, "error no params");
        return FuriStatusErrorParameter;
    }
    int32_t bytecount;
    int matched = sscanf(cargs, "%lx", &bytecount);
    if(matched != 1) {
        swire_usb_printf_ln(app->usb, "error params matched %d", matched);
        return FuriStatusErrorParameter;
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

    std::unique_ptr<uint8_t[]> buffer(new uint8_t[bytecount]);
    furi_check(buffer);

    for(int i = 0; i < bytecount; i++) {
        buffer[i] = swire_bitbang_byte_read(app->swire);
    }

#if _OK_RESPONSES == 1
    swire_usb_writeline_cstr(app->usb, "ok");
#endif
    swire_usb_printf_ln(app->usb, "data %lx", (int32_t)bytecount);
    status = swire_usb_write(app->usb, buffer.get(), bytecount);
    if((FuriFlag)status & FuriFlagError) {
        return status;
    }

    swire_bitbang_timer_join(app->swire);
    return FuriStatusOk;
}

FuriStatus cmd_pgm_reset(SwireApp* app, const char* cargs) {
    if(app->usb == NULL) {
        FURI_LOG_E("swire", "cmd_pgm_reset swire->usb not initialized");
        return FuriStatusErrorResource;
    }
    int32_t reset_duration_ms = 0;
    int32_t reset_delay_ms = 0;
    sscanf(cargs, "%ld %ld", &reset_delay_ms, &reset_duration_ms);
    if(reset_duration_ms == 0) {
        reset_duration_ms = app->config->reset_duration_ms;
    }
    if(reset_delay_ms == 0) {
        reset_delay_ms = app->config->reset_delay_ms;
    }

    const GpioPin* pin_power = &gpio_ext_pb2;
    furi_hal_gpio_init_simple(pin_power, GpioModeOutputPushPull);
    furi_hal_gpio_write(pin_power, false);
    furi_delay_ms(reset_duration_ms);
    furi_hal_gpio_write(pin_power, true);
    furi_delay_ms(reset_delay_ms);
// swire_usb_printf_ln(app->usb, "# reset %ld %ld", reset_delay_ms, reset_duration_ms);
#if _OK_RESPONSES == 1
    swire_usb_writeline_cstr(app->usb, "ok");
#endif
    return FuriStatusOk;
}

void cmd_pgm_sleep_us(SwireApp* app, const char* cargs) {
    UNUSED(app);
    uint32_t us = 1;
    sscanf(cargs, "%ld", &us);
    furi_delay_us(us);
#if _OK_RESPONSES == 1
    swire_usb_writeline_cstr(app->usb, "ok");
#endif
}
