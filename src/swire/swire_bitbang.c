#include <stdlib.h>
#include <furi.h>
#include <furi_hal_resources.h>
#include <furi/core/log.h>
#include "swire_common.h"
#include "swire_clock.h"
#include "./swire_bitbang.h"

uint32_t _unit_ticks;
uint32_t _unit_ticks_backoff;

static void _swire_bitbang_init_with_sws(SwireBitbang* swire, IoPins sws);

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

SwireBitbang* swire_bitbang_alloc_with_sws(const IoPins sws) {
    SwireBitbang* swire = malloc(sizeof(SwireBitbang));
    _swire_bitbang_init_with_sws(swire, sws);
    return swire;
}

void swire_bitbang_free(SwireBitbang* self) {
    if(self == NULL) return;

    furi_hal_gpio_init_simple(self->pin_sws_i, GpioModeAnalog);
    furi_hal_gpio_init_simple(self->pin_sws_o, GpioModeAnalog);

    free(self);
}

uint32_t swire_bitbang_get_bitrage(SwireBitbang* self) {
    return self->bitrate;
}
void swire_bitbang_set_bitrate(SwireBitbang* self, uint32_t bitrate) {
    self->bitrate = bitrate;
}

static void _swire_bitbang_init_with_sws(SwireBitbang* self, const IoPins sws) {
    self->pin_sws_i = sws.in;
    self->pin_sws_o = sws.out;

    self->clocks_per_second = SWIRE_SYSTEM_CLOCK_FREQ;
    self->bitrate = 960000;

    self->timeout_byte_ticks = _unit_ticks * 5 * 10 * 10;
    self->error = SwireBitbangErrorNone;

    furi_hal_gpio_init(self->pin_sws_i, GpioModeInput, GpioPullNo, GpioSpeedVeryHigh);
    // Use GpioModeOutputPushPull, NOT OpenDrain, turns out
    // open drain with a pull up is SLOW to bring to logical high
    furi_hal_gpio_init(self->pin_sws_o, GpioModeInput, GpioPullNo, GpioSpeedVeryHigh);
    furi_hal_gpio_write(self->pin_sws_i, true);
    furi_hal_gpio_write(self->pin_sws_o, true);

    swire_bitbang_timer_restart(self);
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
#define _CLK ((int32_t)SWIRE_SYSTEM_CLOCK_CURRENT)

void _swire_bitbang_write_bits9(SwireBitbang* self, uint32_t bits) {
    const GpioPin* pin_sws_o = self->pin_sws_o;

    swire_bitbang_timer_continue(self);

    float bittimecyc = ((float)self->clocks_per_second) / (float)self->bitrate;
#define _BITCOUNT 10
    furi_assert(_BITCOUNT * 2 < _WAVEFORM_BUFFER_LENGTH);
    for(int i = 0; i < _BITCOUNT; i++) {
        float bitstart = i * bittimecyc;
        bool bit = ((bits << 1 >> (9 - i)) & 1) != 0;
        self->waveform_edges[2 * i + 0] = (int32_t)(bitstart);
        self->waveform_edges[2 * i + 1] = (int32_t)(bitstart + bittimecyc * (bit ? 0.8f : 0.2f));
    }

    int32_t* sample = self->waveform_edges;
    uint32_t pin_action1 = pin_sws_o->pin;
    uint32_t pin_action0 = pin_action1 << GPIO_NUMBER;
    volatile uint32_t* bsrr = &pin_sws_o->port->BSRR;

#define _ONEBIT(idx)                                                 \
    swire_clock_spinwait_until_tick(sample[idx * 2 + 0] + clkstart); \
    *bsrr = pin_action0;                                             \
    swire_clock_spinwait_until_tick(sample[idx * 2 + 1] + clkstart); \
    *bsrr = pin_action1;

    __disable_irq();
    LL_GPIO_SetPinMode(pin_sws_o->port, pin_sws_o->pin, LL_GPIO_MODE_OUTPUT);
    int32_t clkstart = swire_clock_get_real_tick() + 6;
    _ONEBIT(0);
    _ONEBIT(1);
    _ONEBIT(2);
    _ONEBIT(3);
    _ONEBIT(4);
    _ONEBIT(5);
    _ONEBIT(6);
    _ONEBIT(7);
    _ONEBIT(8);
    _ONEBIT(9);
    LL_GPIO_SetPinMode(pin_sws_o->port, pin_sws_o->pin, LL_GPIO_MODE_INPUT);
    __enable_irq();

    self->next_unit_tick = _CLK + (int32_t)(1 * bittimecyc);
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

#define _SWIRE_WAIT_BIT(tick0, tick1)                                    \
    for(;;) {                                                            \
        (tick0) = swire_clock_get_real_tick();                           \
        if((*idr & pini_mask1) == 0) break;                              \
        if(((int32_t)((tick0) - timeout)) > 0) goto halt_abrupt_timeout; \
    }                                                                    \
    for(;;) {                                                            \
        (tick1) = swire_clock_get_real_tick();                           \
        if((*idr & pini_mask1) != 0) break;                              \
        if(((int32_t)((tick1) - timeout)) > 0) goto halt_abrupt_timeout; \
    }

int32_t swire_bitbang_byte_read(SwireBitbang* self) {
    if(swire_bitbang_has_error(self)) return -1;
    const GpioPin* pin_sws_o = self->pin_sws_o;
    const GpioPin* pin_sws_i = self->pin_sws_i;
    swire_bitbang_timer_continue(self);
    uint32_t tickss = swire_clock_get_real_tick();
    uint32_t timeout = tickss + self->timeout_byte_ticks;
    float bittimecyc = ((float)self->clocks_per_second) / (float)self->bitrate;

    volatile uint32_t* pino_bsrr = &pin_sws_o->port->BSRR;
    volatile uint32_t* idr = &pin_sws_i->port->IDR;
    uint32_t pini_mask1 = pin_sws_i->pin;
    uint32_t pino_action1 = pin_sws_o->pin;
    uint32_t pino_action0 = pin_sws_o->pin << GPIO_NUMBER;
    uint32_t buffer = 0;
    int32_t samples[2] = {0, (int32_t)bittimecyc * 0.2};
    uint32_t ticks[18];

    __disable_irq();
    LL_GPIO_SetPinMode(pin_sws_o->port, pin_sws_o->pin, LL_GPIO_MODE_OUTPUT);
    int32_t clkstart = swire_clock_get_real_tick() + 6;
    swire_clock_spinwait_until_tick(samples[0] + clkstart);
    *pino_bsrr = pino_action0;
    swire_clock_spinwait_until_tick(samples[1] + clkstart);
    *pino_bsrr = pino_action1;
    LL_GPIO_SetPinMode(pin_sws_o->port, pin_sws_o->pin, LL_GPIO_MODE_INPUT);

    _SWIRE_WAIT_BIT(ticks[0], ticks[1]);
    _SWIRE_WAIT_BIT(ticks[2], ticks[3]);
    _SWIRE_WAIT_BIT(ticks[4], ticks[5]);
    _SWIRE_WAIT_BIT(ticks[6], ticks[7]);
    _SWIRE_WAIT_BIT(ticks[8], ticks[9]);
    _SWIRE_WAIT_BIT(ticks[10], ticks[11]);
    _SWIRE_WAIT_BIT(ticks[12], ticks[13]);
    _SWIRE_WAIT_BIT(ticks[14], ticks[15]);
    _SWIRE_WAIT_BIT(ticks[16], ticks[17]);
    __enable_irq();

    self->next_unit_tick = ticks[17] + (int32_t)(2 * bittimecyc);

    for(int i = 0; i < 8; i++) {
        int32_t len0 = ticks[2 * i + 2] - ticks[2 * i + 1];
        int32_t len1 = ticks[2 * i + 1] - ticks[2 * i + 0];
        uint32_t bit = 1 << (7 - i);
        buffer |= (len0 < len1) ? bit : 0;
    }

    swire_clock_spinwait_until_tick(self->next_unit_tick);

    // FURI_LOG_I("swire", "CHECKPOINT read %lx", buffer);
    return buffer;
halt_abrupt_timeout:
    __enable_irq();
    self->error = SwireBitbangErrorTimeout;
    FURI_LOG_I("swire", "CHECKPOINT read error %lx", buffer);
    FURI_LOG_I("swire", "  CHECKPOINT tickss    = %lu", tickss);
    FURI_LOG_I("swire", "  CHECKPOINT tickss    = %lu", tickss - tickss);
    FURI_LOG_I("swire", "  CHECKPOINT timeout   = %lu", timeout - tickss);
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
