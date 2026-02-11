#include <stdlib.h>
#include <furi.h>
#include <furi_hal_resources.h>
#include <furi/core/log.h>
#include "swire_common.h"
#include "swire_clock.h"
#include "./swire_bitbang.h"

uint32_t _unit_ticks;
uint32_t _unit_ticks_backoff;

static void _swire_bitbang_init_with_sws(
    SwireBitbang* swire,
    const GpioPin* pin_sws_i,
    const GpioPin* pin_sws_o);

SWIRE_INLINE static bool _swire_bitbang_spinwait_until_pin_or_timeout(
    SwireBitbang* swire,
    const GpioPin* pin,
    bool value,
    uint32_t timeout_tick);
void _swire_init_with_sws(SwireBitbang* swire, const GpioPin* pin_sws_o, const GpioPin* pin_sws_i);

void swire_bitbang_global_init() {
    swire_bitbang_global_init_with_bitrate(75600);
}

void swire_bitbang_global_init_with_bitrate(uint32_t bitrate) {
    uint32_t unitrate = bitrate * 5;

    _unit_ticks = SystemCoreClock / unitrate;
    _unit_ticks_backoff = _unit_ticks * 10;
}

void swire_bitbang_global_log_params() {
    FURI_LOG_I("swire", "_unit_ticks=%lu", _unit_ticks);
    FURI_LOG_I("swire", "_unit_ticks_backoff=%lu", _unit_ticks_backoff);
    FURI_LOG_I("swire", "furi_kernel_get_tick_frequency()=%lu", furi_kernel_get_tick_frequency());
}

SwireBitbang* swire_bitbang_alloc_with_sws(const GpioPin* pin_sws_i, const GpioPin* pin_sws_o) {
    SwireBitbang* swire = malloc(sizeof(SwireBitbang));
    _swire_bitbang_init_with_sws(swire, pin_sws_i, pin_sws_o);
    return swire;
}

void swire_bitbang_free(SwireBitbang* swire) {
    free(swire);
}

static void _swire_bitbang_init_with_sws(
    SwireBitbang* swire,
    const GpioPin* pin_sws_i,
    const GpioPin* pin_sws_o) {
    swire->pin_sws_i = pin_sws_i;
    swire->pin_sws_o = pin_sws_o;

    swire->timeout_byte_ticks = _unit_ticks * 5 * 10 * 10;
    swire->error = SwireBitbangErrorNone;

    furi_hal_gpio_write(swire->pin_sws_i, true);
    furi_hal_gpio_init(swire->pin_sws_i, GpioModeInput, GpioPullUp, GpioSpeedVeryHigh);

    furi_hal_gpio_write(swire->pin_sws_o, true);
    furi_hal_gpio_init(swire->pin_sws_o, GpioModeOutputOpenDrain, GpioPullUp, GpioSpeedVeryHigh);

    swire_bitbang_timer_restart(swire);
}

void swire_bitbang_timer_restart(SwireBitbang* swire) {
    swire->next_unit_tick = swire_clock_get_real_tick();
}

void swire_bitbang_timer_continue(SwireBitbang* swire) {
    uint32_t next_unit_tick = swire->next_unit_tick;
    swire_clock_spinwait_until_tick(next_unit_tick);
    swire->next_unit_tick = swire_clock_get_real_tick();
}

void _swire_bitbang_write_bitsn(SwireBitbang* swire, uint32_t bits, int count) {
    const GpioPin* pin_sws_o = swire->pin_sws_o;
    swire_bitbang_timer_continue(swire);

    bits <<= 32 - count;
    uint32_t unit_1 = _unit_ticks;
    uint32_t unit_4 = _unit_ticks * 4;
    uint32_t unit_sw = unit_1 ^ unit_4;
    __disable_irq();

    furi_hal_gpio_write(pin_sws_o, false);
    uint32_t tick = swire_clock_get_real_tick();
    while(count > 0) {
        uint32_t bit = bits >> 31;
        bits <<= 1;
        SWIRE_CLOCK_PROGRESS_TICKS(tick, unit_1 ^ (bit * unit_sw));
        furi_hal_gpio_write(pin_sws_o, true);
        --count;
        SWIRE_CLOCK_PROGRESS_TICKS(tick, unit_4 ^ (bit * unit_sw));
        furi_hal_gpio_write(pin_sws_o, false);
    }
    SWIRE_CLOCK_PROGRESS_TICKS(tick, _unit_ticks);
    __enable_irq();

    swire->next_unit_tick = tick + _unit_ticks_backoff;
}

void _swire_bitbang_write_bits9(SwireBitbang* swire, uint32_t bits) {
    const GpioPin* pin_sws_o = swire->pin_sws_o;
    uint32_t xor_a, xor_b;
    uint32_t ts_1 = _unit_ticks;
    uint32_t ts_4 = _unit_ticks * 4;
    uint32_t ts_sw = ts_1 ^ ts_4;

    xor_a = (bits >> 8) & 1;
    xor_a *= ts_sw;

    swire_bitbang_timer_continue(swire);
    __disable_irq();

    uint32_t tick = swire_clock_get_real_tick();
    furi_hal_gpio_write(pin_sws_o, false);
    xor_b = (bits >> 7) & 1;
    SWIRE_CLOCK_PROGRESS_TICKS(tick, ts_1 ^ xor_a);
    furi_hal_gpio_write(pin_sws_o, true);
    xor_b *= ts_sw;
    SWIRE_CLOCK_PROGRESS_TICKS(tick, ts_4 ^ xor_a);
    furi_hal_gpio_write(pin_sws_o, false);
    xor_a = (bits >> 6) & 1;
    SWIRE_CLOCK_PROGRESS_TICKS(tick, ts_1 ^ xor_b);
    furi_hal_gpio_write(pin_sws_o, true);
    xor_a *= ts_sw;
    SWIRE_CLOCK_PROGRESS_TICKS(tick, ts_4 ^ xor_b);
    furi_hal_gpio_write(pin_sws_o, false);
    xor_b = (bits >> 5) & 1;
    SWIRE_CLOCK_PROGRESS_TICKS(tick, ts_1 ^ xor_a);
    furi_hal_gpio_write(pin_sws_o, true);
    xor_b *= ts_sw;
    SWIRE_CLOCK_PROGRESS_TICKS(tick, ts_4 ^ xor_a);
    furi_hal_gpio_write(pin_sws_o, false);
    xor_a = (bits >> 4) & 1;
    SWIRE_CLOCK_PROGRESS_TICKS(tick, ts_1 ^ xor_b);
    furi_hal_gpio_write(pin_sws_o, true);
    xor_a *= ts_sw;
    SWIRE_CLOCK_PROGRESS_TICKS(tick, ts_4 ^ xor_b);
    furi_hal_gpio_write(pin_sws_o, false);
    xor_b = (bits >> 3) & 1;
    SWIRE_CLOCK_PROGRESS_TICKS(tick, ts_1 ^ xor_a);
    furi_hal_gpio_write(pin_sws_o, true);
    xor_b *= ts_sw;
    SWIRE_CLOCK_PROGRESS_TICKS(tick, ts_4 ^ xor_a);
    furi_hal_gpio_write(pin_sws_o, false);
    xor_a = (bits >> 2) & 1;
    SWIRE_CLOCK_PROGRESS_TICKS(tick, ts_1 ^ xor_b);
    furi_hal_gpio_write(pin_sws_o, true);
    xor_a *= ts_sw;
    SWIRE_CLOCK_PROGRESS_TICKS(tick, ts_4 ^ xor_b);
    furi_hal_gpio_write(pin_sws_o, false);
    xor_b = (bits >> 1) & 1;
    SWIRE_CLOCK_PROGRESS_TICKS(tick, ts_1 ^ xor_a);
    furi_hal_gpio_write(pin_sws_o, true);
    xor_b *= ts_sw;
    SWIRE_CLOCK_PROGRESS_TICKS(tick, ts_4 ^ xor_a);
    furi_hal_gpio_write(pin_sws_o, false);
    xor_a = (bits >> 0) & 1;
    SWIRE_CLOCK_PROGRESS_TICKS(tick, ts_1 ^ xor_b);
    furi_hal_gpio_write(pin_sws_o, true);
    xor_a *= ts_sw;
    SWIRE_CLOCK_PROGRESS_TICKS(tick, ts_4 ^ xor_b);
    furi_hal_gpio_write(pin_sws_o, false);
    SWIRE_CLOCK_PROGRESS_TICKS(tick, ts_1 ^ xor_a);
    furi_hal_gpio_write(pin_sws_o, true);
    SWIRE_CLOCK_PROGRESS_TICKS(tick, ts_4 ^ xor_a);
    furi_hal_gpio_write(pin_sws_o, false);
    SWIRE_CLOCK_PROGRESS_TICKS(tick, _unit_ticks);
    furi_hal_gpio_write(pin_sws_o, true);
    __enable_irq();

    swire->next_unit_tick = tick + _unit_ticks_backoff;
}

void swire_bitbang_transaction_start(
    SwireBitbang* swire,
    uint32_t addr,
    SwireBitbangRw rw,
    uint32_t slave_id) {
    if(swire->error != SwireBitbangErrorNone) return;
    int32_t rwid = (rw == SwireBitbangRwRead ? 0x80 : 0x00) | (slave_id & 0x7f);
    swire_bitbang_timer_continue(swire);
    _swire_bitbang_write_bits9(swire, 0x15a);
    _swire_bitbang_write_bits9(swire, (addr >> 16) & 0xff);
    _swire_bitbang_write_bits9(swire, (addr >> 8) & 0xff);
    _swire_bitbang_write_bits9(swire, (addr >> 0) & 0xff);
    _swire_bitbang_write_bits9(swire, rwid);
}

void swire_bitbang_transaction_end(SwireBitbang* swire) {
    if(swire_bitbang_has_error(swire)) return;
    swire_bitbang_transaction_end_force(swire);
}

void swire_bitbang_transaction_end_force(SwireBitbang* swire) {
    _swire_bitbang_write_bits9(swire, 0x1ff);
}

void swire_bitbang_byte_write(SwireBitbang* swire, uint8_t data) {
    if(swire_bitbang_has_error(swire)) return;
    _swire_bitbang_write_bits9(swire, data);
}

SWIRE_INLINE static bool _swire_bitbang_spinwait_until_pin_or_timeout(
    SwireBitbang* swire,
    const GpioPin* pin,
    bool value,
    uint32_t timeout_tick) {
    for(;;) {
        if(furi_hal_gpio_read(pin) == value) {
            return false;
        }
        if(swire_clock_tick_elapsed(timeout_tick)) {
            swire->error = SwireBitbangErrorTimeout;
            return true;
        }
    }
}

#define _SWIRE_WAIT_EDGE(tick, value)                                 \
    for(;;) {                                                         \
        if(furi_hal_gpio_read(pin_sws_i) == (value)) break;           \
        if(furi_hal_gpio_read(pin_sws_i) == (value)) break;           \
        uint32_t __local_tick = swire_clock_get_real_tick();          \
        if(((int32_t)(__local_tick - timeout)) > 0) goto halt_abrupt; \
        if(furi_hal_gpio_read(pin_sws_i) == (value)) break;           \
        if(furi_hal_gpio_read(pin_sws_i) == (value)) break;           \
    }                                                                 \
    (tick) = swire_clock_get_real_tick();

#define _SWIRE_READ_STORE_BIT(tick1, tick2, tick3, value) \
    buffer |= (((tick3) - (tick2)) < ((tick2) - (tick1))) ? (value) : 0;

int32_t swire_bitbang_byte_read(SwireBitbang* swire) {
    if(swire_bitbang_has_error(swire)) return -1;
    const GpioPin* pin_sws_o = swire->pin_sws_o;
    const GpioPin* pin_sws_i = swire->pin_sws_i;
    swire_bitbang_timer_continue(swire);
    uint32_t tickss = swire_clock_get_real_tick();
    uint32_t timeout = tickss + swire->timeout_byte_ticks;
    uint32_t buffer = 0;
    uint32_t ticks[18] = {0};

    __disable_irq();

    furi_hal_gpio_write(pin_sws_o, false); // Write trigger
    uint32_t tickss1 = swire_clock_get_real_tick();
    SWIRE_CLOCK_PROGRESS_TICKS(tickss1, _unit_ticks);
    furi_hal_gpio_write(pin_sws_o, true); // Write trigger
    uint32_t tickss2 = swire_clock_get_real_tick();

    _SWIRE_WAIT_EDGE(ticks[0], false); // 7
    _SWIRE_WAIT_EDGE(ticks[1], true);
    _SWIRE_WAIT_EDGE(ticks[2], false); // 6
    _SWIRE_WAIT_EDGE(ticks[3], true);
    _SWIRE_WAIT_EDGE(ticks[4], false); // 5
    _SWIRE_WAIT_EDGE(ticks[5], true);
    _SWIRE_WAIT_EDGE(ticks[6], false); // 4
    _SWIRE_WAIT_EDGE(ticks[7], true);
    _SWIRE_WAIT_EDGE(ticks[8], false); // 3
    _SWIRE_WAIT_EDGE(ticks[9], true);
    _SWIRE_WAIT_EDGE(ticks[10], false); // 2
    _SWIRE_WAIT_EDGE(ticks[11], true);
    _SWIRE_WAIT_EDGE(ticks[12], false); // 1
    _SWIRE_WAIT_EDGE(ticks[13], true);
    _SWIRE_WAIT_EDGE(ticks[14], false); // 0
    _SWIRE_WAIT_EDGE(ticks[15], true);
    _SWIRE_WAIT_EDGE(ticks[16], false); // END
    _SWIRE_WAIT_EDGE(ticks[17], true);
    _SWIRE_READ_STORE_BIT(ticks[0], ticks[1], ticks[2], 0x80);
    _SWIRE_READ_STORE_BIT(ticks[2], ticks[3], ticks[4], 0x40);
    _SWIRE_READ_STORE_BIT(ticks[4], ticks[5], ticks[6], 0x20);
    _SWIRE_READ_STORE_BIT(ticks[6], ticks[7], ticks[8], 0x10);
    _SWIRE_READ_STORE_BIT(ticks[8], ticks[9], ticks[10], 0x08);
    _SWIRE_READ_STORE_BIT(ticks[10], ticks[11], ticks[12], 0x04);
    _SWIRE_READ_STORE_BIT(ticks[12], ticks[13], ticks[14], 0x02);
    _SWIRE_READ_STORE_BIT(ticks[14], ticks[15], ticks[16], 0x01);

    __enable_irq();

    swire->next_unit_tick = ticks[17] + _unit_ticks_backoff;
    swire_clock_spinwait_until_tick(swire->next_unit_tick);

    // FURI_LOG_I("swire", "CHECKPOINT read %lx", buffer);
    return buffer;
halt_abrupt:
    __enable_irq();
    swire->error = SwireBitbangErrorTimeout;
    FURI_LOG_I("swire", "CHECKPOINT read error %lx", buffer);
    FURI_LOG_I("swire", "  CHECKPOINT tickss    = %lu", tickss);
    FURI_LOG_I("swire", "  CHECKPOINT tickss    = %lu", tickss - tickss);
    FURI_LOG_I("swire", "  CHECKPOINT timeout   = %lu", timeout - tickss);
    FURI_LOG_I("swire", "  CHECKPOINT tickss1   = %lu", tickss1 - tickss);
    FURI_LOG_I("swire", "  CHECKPOINT tickss2   = %lu", tickss2 - tickss);
    FURI_LOG_I("swire", "  CHECKPOINT ticks[ 0] = %lu", ticks[0] - tickss);
    FURI_LOG_I("swire", "  CHECKPOINT ticks[ 1] = %lu", ticks[1] - tickss);
    FURI_LOG_I("swire", "  CHECKPOINT ticks[ 2] = %lu", ticks[2] - tickss);
    FURI_LOG_I("swire", "  CHECKPOINT ticks[ 3] = %lu", ticks[3] - tickss);
    FURI_LOG_I("swire", "  CHECKPOINT ticks[ 4] = %lu", ticks[4] - tickss);
    FURI_LOG_I("swire", "  CHECKPOINT ticks[ 5] = %lu", ticks[5] - tickss);
    FURI_LOG_I("swire", "  CHECKPOINT ticks[ 6] = %lu", ticks[6] - tickss);
    FURI_LOG_I("swire", "  CHECKPOINT ticks[ 7] = %lu", ticks[7] - tickss);
    FURI_LOG_I("swire", "  CHECKPOINT ticks[ 8] = %lu", ticks[8] - tickss);
    FURI_LOG_I("swire", "  CHECKPOINT ticks[ 9] = %lu", ticks[9] - tickss);
    FURI_LOG_I("swire", "  CHECKPOINT ticks[10] = %lu", ticks[10] - tickss);
    FURI_LOG_I("swire", "  CHECKPOINT ticks[11] = %lu", ticks[11] - tickss);
    FURI_LOG_I("swire", "  CHECKPOINT ticks[12] = %lu", ticks[12] - tickss);
    FURI_LOG_I("swire", "  CHECKPOINT ticks[13] = %lu", ticks[13] - tickss);
    FURI_LOG_I("swire", "  CHECKPOINT ticks[14] = %lu", ticks[14] - tickss);
    FURI_LOG_I("swire", "  CHECKPOINT ticks[15] = %lu", ticks[15] - tickss);
    FURI_LOG_I("swire", "  CHECKPOINT ticks[16] = %lu", ticks[16] - tickss);
    FURI_LOG_I("swire", "  CHECKPOINT ticks[17] = %lu", ticks[17] - tickss);
    return -1;
}

SWIRE_INLINE bool swire_bitbang_has_error(SwireBitbang* swire) {
    return swire->error != SwireBitbangErrorNone;
}
