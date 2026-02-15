#include "src/commands/commands_bitbang.h"
#include "src/swire/swire_clock.h"
#include "src/utils/light_rgb.h"

void cmd_bitbang_test_simple(SwireApp* app) {
    light_rgb_set(0xffff00);
    SwireBitbang* swire = swire_bitbang_alloc_with_sws((IoPins){
        .out = &gpio_ext_pa7,
        .in = &gpio_ext_pa6,
    });
    swire_bitbang_set_bitrate(swire, app->config->bitrate);

    const GpioPin* trigger = &gpio_ext_pc3;
    furi_hal_gpio_init_simple(trigger, GpioModeOutputPushPull);
    int32_t basecyc = swire_clock_get_cycclk();
    swire_clock_spinwait_until_tick(basecyc + 100);
    furi_hal_gpio_write(trigger, false);
    swire_clock_spinwait_until_tick(basecyc + 200);
    furi_hal_gpio_write(trigger, true);
    furi_hal_gpio_init_simple(trigger, GpioModeAnalog);

    swire_bitbang_transaction_start(swire, 0x0602, SwireBitbangRwWrite, 0);
    swire_bitbang_byte_write(swire, 0x05);
    swire_bitbang_transaction_end(swire);
    swire_bitbang_transaction_start(swire, 0x00b2, SwireBitbangRwWrite, 0);
    swire_bitbang_byte_write(swire, 0x7f);
    swire_bitbang_transaction_end(swire);
    swire_bitbang_free(swire);
    light_rgb_set(0x00ff00);
}

void cmd_bitbang_read(SwireApp* app) {
    int32_t row[16];

    const GpioPin* pin_power = &gpio_ext_pb2;
    const GpioPin* pin_trigger = &gpio_ext_pc3;
    const GpioPin* pin_sws = &gpio_ext_pa7;

    SwireBitbang* swire = swire_bitbang_alloc_with_sws((IoPins){.out = pin_sws, .in = pin_sws});
    swire_bitbang_set_bitrate(swire, app->config->bitrate);

    furi_hal_gpio_init_simple(pin_power, GpioModeOutputPushPull);
    furi_hal_gpio_write(pin_power, false);
    furi_delay_ms(app->config->reset_duration_ms);
    furi_hal_gpio_write(pin_power, true);
    furi_delay_ms(app->config->reset_delay_ms);

    furi_hal_gpio_init_simple(pin_trigger, GpioModeOutputPushPull);
    furi_hal_gpio_write(pin_trigger, false);
    furi_delay_us(app->config->trigger_duration_us);
    furi_hal_gpio_write(pin_trigger, true);
    furi_delay_us(app->config->trigger_delay_us);
    furi_hal_gpio_init_simple(pin_trigger, GpioModeAnalog);

    swire_bitbang_transaction_start(swire, 0x0602, SwireBitbangRwWrite, 0);
    swire_bitbang_byte_write(swire, 0x05);
    swire_bitbang_transaction_end(swire);
    swire_bitbang_transaction_start(swire, 0x00b2, SwireBitbangRwWrite, 0);
    swire_bitbang_byte_write(swire, 0x7f);
    swire_bitbang_transaction_end(swire);
    swire_bitbang_transaction_start(swire, 0x00b2, SwireBitbangRwRead, 0);
    int32_t b1 = swire_bitbang_byte_read(swire);
    UNUSED(b1); // sanity check
    swire_bitbang_transaction_end(swire);

    furi_delay_ms(50);

    swire_bitbang_transaction_start(swire, 0x0d, SwireBitbangRwWrite, 0);
    swire_bitbang_byte_write(swire, 0);
    swire_bitbang_transaction_end(swire);

    swire_bitbang_transaction_start(swire, 0x0c, SwireBitbangRwWrite, 0);
    swire_bitbang_byte_write(swire, 0x03);
    swire_bitbang_transaction_end(swire);

    swire_bitbang_transaction_start(swire, 0x0c, SwireBitbangRwWrite, 0);
    swire_bitbang_byte_write(swire, 0x00);
    swire_bitbang_transaction_end(swire);

    swire_bitbang_transaction_start(swire, 0x0c, SwireBitbangRwWrite, 0);
    swire_bitbang_byte_write(swire, 0x00);
    swire_bitbang_transaction_end(swire);

    swire_bitbang_transaction_start(swire, 0x0c, SwireBitbangRwWrite, 0);
    swire_bitbang_byte_write(swire, 0x00);
    swire_bitbang_transaction_end(swire);

    swire_bitbang_transaction_start(swire, 0x0c, SwireBitbangRwWrite, 0);
    swire_bitbang_byte_write(swire, 0x00);
    swire_bitbang_byte_write(swire, 0x0a);
    swire_bitbang_transaction_end(swire);

    swire_bitbang_transaction_start(swire, 0xb3, SwireBitbangRwWrite, 0);
    swire_bitbang_byte_write(swire, 0x80);
    swire_bitbang_transaction_end(swire);
    if(swire_bitbang_has_error(swire)) goto exit;

    for(int j = 0; j < 16; j++) {
        swire_bitbang_transaction_start(swire, 0x0c, SwireBitbangRwRead, 0);
        for(int i = 0; i < 16; i++) {
            row[i] = swire_bitbang_byte_read(swire);
        }
        swire_bitbang_transaction_end(swire);

        FURI_LOG_I(
            "swire",
            "%02x %02x %02x %02x %02x %02x %02x %02x  %02x %02x %02x %02x %02x %02x %02x %02x",
            (unsigned int)row[0],
            (unsigned int)row[1],
            (unsigned int)row[2],
            (unsigned int)row[3],
            (unsigned int)row[4],
            (unsigned int)row[5],
            (unsigned int)row[6],
            (unsigned int)row[7],
            (unsigned int)row[8],
            (unsigned int)row[9],
            (unsigned int)row[10],
            (unsigned int)row[11],
            (unsigned int)row[12],
            (unsigned int)row[13],
            (unsigned int)row[14],
            (unsigned int)row[15]);
        furi_delay_ms(1);
    }

exit:
    swire_bitbang_transaction_end_force(swire);

    furi_hal_gpio_init_simple(pin_power, GpioModeAnalog);

    furi_delay_ms(app->config->keep_powered_duration_ms);
    swire_bitbang_free(swire);
}

void cmd_bitbang_read_top(SwireApp* app, uint32_t bitrate) {
    swire_bitbang_global_init_with_bitrate(bitrate);
    swire_bitbang_global_log_params();
    cmd_bitbang_read(app);
    furi_delay_ms(500);
}

void cmd_bitbang_test_switching_freq(SwireApp* app) {
    UNUSED(app);

    const GpioPin* pin = &gpio_ext_pa7;
    volatile uint32_t* odr = &pin->port->ODR;
    uint32_t bits1 = pin->pin;
    uint32_t bits0 = ~bits1;
    volatile int dummy_acc = 0;

    furi_hal_gpio_init(pin, GpioModeOutputPushPull, GpioPullUp, GpioSpeedVeryHigh);
    __disable_irq();
    for(volatile int i = 0; i < 10000;) {
        *odr &= bits0;
        i++;
        asm("nop");
        *odr |= bits1;
    }
    __enable_irq();

    FURI_LOG_D("swire", "log accumulator to prevent optimization %d", dummy_acc);

    furi_hal_gpio_init_simple(pin, GpioModeAnalog);
}

/*
void cmd_dump_by_bitbang() {
    // SwireUart* swire = swire_uart_alloc(921600);
    SwireBitbang* swire = swire_bitbang_alloc_with_sws(377804);
    swire->read_delay_per_byte_us = 35;
    uint8_t cmd[2];

    swire_uart_write1(swire, 0x0602, 0, 0x05); // CPU Stop
    swire_uart_write1(swire, 0x00b2, 0, 0x7f); // b0-b4 SWIRE
    int32_t sanity_check = swire_uart_read1(swire, 0x00b2, 0); // b0-b4 SWIRE
    FURI_LOG_W("swire", "Sanity test...");
    if(sanity_check != 0x7f) {
        FURI_LOG_W("swire", "Sanity test failed %02ld", sanity_check);
    }
    // MSPI = Memory SPI
    // CS = Chip Select
    swire_uart_write1(swire, 0x000d, 0, 0x00); // MSPI Control, CS bit active low

    // addr = Address
    swire_uart_write1(swire, 0x000c, 0, 0x03); // MSPI Data, 03 = read
    swire_uart_write1(swire, 0x000c, 0, 0x00); // MSPI read addr[2]
    swire_uart_write1(swire, 0x000c, 0, 0x00); // MSPI read addr[1]
    swire_uart_write1(swire, 0x000c, 0, 0x00); // MSPI read addr[0]

    cmd[0] = 0x00; // MSPI Data, 00 to drive MSPI Clock to initiate first read
    cmd[1] = 0x0a; // MSPI Control, auto read mode
    swire_uart_write(swire, 0x000c, 0, cmd, 2);

    swire_uart_write1(
        swire, 0x00b3, 0, 0x80); // swire mode, fifo, repeated reads from same address

    do_by_uart_dumploop(swire);

    swire_uart_write1(swire, 0x00b3, 0, 0x00); // swire mode reset to default
    swire_uart_write1(swire, 0x000d, 0, 0x01); // MSPI Control disable CS

    furi_delay_ms(500);
    SWIRE_UART_FREE(swire);
}
*/
