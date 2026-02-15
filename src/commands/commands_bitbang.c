#include "src/commands/commands_bitbang.h"
#include "src/utils/light_rgb.h"

void cmd_bitbang_test_simple(SwireApp* app) {
    light_rgb_set(0xffff00);
    SwireBitbang* swire = swire_bitbang_alloc_with_sws((IoPins){
        .out = &gpio_ext_pa7,
        .in = &gpio_ext_pa6,
    });
    swire_bitbang_set_bitrate(swire, app->config->bitrate);
    swire_bitbang_transaction_start(swire, 0x0602, SwireBitbangRwWrite, 0);
    swire_bitbang_byte_write(swire, 0x05);
    swire_bitbang_transaction_end(swire);
    swire_bitbang_transaction_start(swire, 0x00b2, SwireBitbangRwWrite, 0);
    swire_bitbang_byte_write(swire, 0x7f);
    swire_bitbang_transaction_end(swire);
    swire_bitbang_free(swire);
    light_rgb_set(0x00ff00);
}

void cmd_bitbang_read() {
    int32_t row[16];

    SwireBitbang* swire = swire_bitbang_alloc_with_sws((IoPins){
        .out = &gpio_ext_pa7,
        .in = &gpio_ext_pa6,
    });

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
    swire_bitbang_free(swire);
}

void cmd_bitbang_read_top(uint32_t bitrate) {
    swire_bitbang_global_init_with_bitrate(bitrate);
    swire_bitbang_global_log_params();
    cmd_bitbang_read();
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
