#include <stdlib.h>
#include <furi.h>
#include <furi_hal_resources.h>
#include <furi/core/log.h>
#include "./swire_clock.hpp"
#include "./swire_bitbang.hpp"

static constexpr uint32_t _ENOUGH_CYCLES_TO_TRANSLATE_SAMPLE_BUFFER_OF_20 =
    320; // determined empirically
#define _PRECALCULATE_SAMPLE_TRANSLATE 1

static void _swire_bitbang_init_with_sws(SwireBitbang* swire, const GpioPin* sws);

void _swire_init_with_sws(SwireBitbang* swire, const GpioPin* pin_sws_o, const GpioPin* pin_sws_i);

SwireBitbang* swire_bitbang_alloc_with_sws(const GpioPin* sws) {
    SwireBitbang* swire = (SwireBitbang*)malloc(sizeof(SwireBitbang));
    _swire_bitbang_init_with_sws(swire, sws);
    return swire;
}

void swire_bitbang_free(SwireBitbang* self) {
    if(self == NULL) return;

    furi_hal_gpio_init_simple(self->pin_sws, GpioModeAnalog);

    free(self);
}

uint32_t swire_bitbang_get_bitrage(SwireBitbang* self) {
    return self->bitrate;
}

void swire_bitbang_set_bitrate(SwireBitbang* self, uint32_t bitrate) {
    self->bitrate = bitrate;
    self->timeout_byte_ticks = static_cast<int32_t>(
        10 * 10 * static_cast<float_t>(self->clocks_per_second) / static_cast<float_t>(bitrate));
}

static void _swire_bitbang_init_with_sws(SwireBitbang* self, const GpioPin* sws) {
    self->pin_sws = sws;

    self->clocks_per_second = SWIRE_SYSTEM_CLOCK_FREQ;
    swire_bitbang_set_bitrate(self, 960000);
    self->error = SwireBitbangErrorNone;

    // Use GpioModeOutputPushPull, NOT OpenDrain, turns out
    // open drain with a pull up is SLOW to bring to logical high
    furi_hal_gpio_init(self->pin_sws, GpioModeInput, GpioPullNo, GpioSpeedVeryHigh);
    // set to true, so that when we set LL_GPIO_MODE_OUTPUT
    // for writing, it does not immediately pull the bus down and
    // we have better timing
    furi_hal_gpio_write(self->pin_sws, true);

    swire_bitbang_timer_restart(self);
}

void swire_bitbang_timer_restart(SwireBitbang* swire) {
    swire->next_unit_tick = swire_clock_get_cycclk();
}

void swire_bitbang_timer_join(SwireBitbang* self) {
    uint32_t next_unit_tick = self->next_unit_tick;
    swire_clock_spinwait_until_cycclk(next_unit_tick);
    self->next_unit_tick = swire_clock_get_cycclk();
}

void _swire_bitbang_write_bits9(SwireBitbang* self, uint32_t bits) {
    float bittimecyc = ((float)self->clocks_per_second) / (float)self->bitrate;
#define _BITCOUNT 10
    furi_assert(_BITCOUNT * 2 < SwireBitbang::WAVEFORM_BUFFER_LENGTH);
    for(int i = 0; i < _BITCOUNT; i++) {
        float bitstart = i * bittimecyc;
        bool bit = ((bits << 1 >> (9 - i)) & 1) != 0;
        self->waveform_edges[2 * i + 0] = (int32_t)(bitstart);
        self->waveform_edges[2 * i + 1] = (int32_t)(bitstart + bittimecyc * (bit ? 0.8f : 0.2f));
    }

    int32_t* sample = self->waveform_edges;
    uint32_t pin_set1 = self->pin_sws->pin;
    uint32_t pin_set0 = self->pin_sws->pin << GPIO_NUMBER;
    volatile uint32_t* bsrr = &self->pin_sws->port->BSRR;

#if _PRECALCULATE_SAMPLE_TRANSLATE == 1
#define _ONEBIT(idx)                                        \
    swire_clock_spinwait_until_cycclk(sample[idx * 2 + 0]); \
    *bsrr = pin_set0;                                       \
    swire_clock_spinwait_until_cycclk(sample[idx * 2 + 1]); \
    *bsrr = pin_set1;
#else
#define _ONEBIT(idx)                                                     \
    swire_clock_spinwait_until_cycclk(clk_offset + sample[idx * 2 + 0]); \
    *bsrr = pin_set0;                                                    \
    swire_clock_spinwait_until_cycclk(clk_offset + sample[idx * 2 + 1]); \
    *bsrr = pin_set1;
#endif
    swire_bitbang_timer_join(self);

    __disable_irq();

#if _PRECALCULATE_SAMPLE_TRANSLATE == 1
    {
        int32_t clk_translate = cyc_max_i32(
            self->next_unit_tick,
            swire_clock_get_cycclk() + _ENOUGH_CYCLES_TO_TRANSLATE_SAMPLE_BUFFER_OF_20);
        for(int i = 0; i < 20; i++)
            sample[i] += clk_translate;
    }
#endif

#if _PRECALCULATE_SAMPLE_TRANSLATE != 1
    int32_t clk_offset = swire_clock_get_cycclk() + 6;
#endif
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

    __enable_irq();

#if _PRECALCULATE_SAMPLE_TRANSLATE == 1
    self->next_unit_tick = sample[18] + (int32_t)(2 * bittimecyc);
#else
    self->next_unit_tick = sample[18] + clk_offset + (int32_t)(2 * bittimecyc);
#endif
}

void swire_bitbang_transaction_start(
    SwireBitbang* self,
    uint32_t addr,
    SwireBitbangRw rw,
    uint32_t slave_id) {
    if(self->error != SwireBitbangErrorNone) return;
    int32_t rwid = (rw == SwireBitbangRwRead ? 0x80 : 0x00) | (slave_id & 0x7f);
    swire_bitbang_timer_join(self);

    const GpioPin* pin_sws = self->pin_sws;
    LL_GPIO_SetPinMode(pin_sws->port, pin_sws->pin, LL_GPIO_MODE_OUTPUT);
    _swire_bitbang_write_bits9(self, 0x15a);
    _swire_bitbang_write_bits9(self, (addr >> 16) & 0xff);
    _swire_bitbang_write_bits9(self, (addr >> 8) & 0xff);
    _swire_bitbang_write_bits9(self, (addr >> 0) & 0xff);
    _swire_bitbang_write_bits9(self, rwid);
    LL_GPIO_SetPinMode(pin_sws->port, pin_sws->pin, LL_GPIO_MODE_INPUT);
}

void swire_bitbang_transaction_end(SwireBitbang* swire) {
    if(swire_bitbang_has_error(swire)) return;
    swire_bitbang_transaction_end_force(swire);
}

void swire_bitbang_transaction_end_force(SwireBitbang* self) {
    const GpioPin* pin_sws = self->pin_sws;
    LL_GPIO_SetPinMode(pin_sws->port, pin_sws->pin, LL_GPIO_MODE_OUTPUT);
    _swire_bitbang_write_bits9(self, 0x1ff);
    LL_GPIO_SetPinMode(pin_sws->port, pin_sws->pin, LL_GPIO_MODE_INPUT);
}

void swire_bitbang_byte_write(SwireBitbang* self, uint8_t data) {
    if(swire_bitbang_has_error(self)) return;
    const GpioPin* pin_sws = self->pin_sws;
    LL_GPIO_SetPinMode(pin_sws->port, pin_sws->pin, LL_GPIO_MODE_OUTPUT);
    _swire_bitbang_write_bits9(self, data);
    LL_GPIO_SetPinMode(pin_sws->port, pin_sws->pin, LL_GPIO_MODE_INPUT);
}

#define _SWIRE_WAIT_BIT(tick0, tick1)                                    \
    for(;;) {                                                            \
        (tick0) = swire_clock_get_cycclk();                              \
        if(((*idr) & pini_mask1) == 0) break;                            \
        if(((int32_t)((tick0) - timeout)) > 0) goto halt_abrupt_timeout; \
    }                                                                    \
    for(;;) {                                                            \
        (tick1) = swire_clock_get_cycclk();                              \
        if(((*idr) & pini_mask1) != 0) break;                            \
        if(((int32_t)((tick1) - timeout)) > 0) goto halt_abrupt_timeout; \
    }

int32_t swire_bitbang_byte_read(SwireBitbang* self) {
    if(swire_bitbang_has_error(self)) return -1;
    const GpioPin* pin_sws = self->pin_sws;
    uint32_t tickss = swire_clock_get_cycclk();
    uint32_t timeout = tickss + self->timeout_byte_ticks;
    float bittimecyc = ((float)self->clocks_per_second) / (float)self->bitrate;

    volatile uint32_t* pino_bsrr = &pin_sws->port->BSRR;
    volatile uint32_t* idr = &pin_sws->port->IDR;
    uint32_t pini_mask1 = pin_sws->pin;
    uint32_t pino_set1 = pin_sws->pin;
    uint32_t pino_set0 = pin_sws->pin << GPIO_NUMBER;
    uint32_t buffer = 0;
    int32_t sample1 = (int32_t)bittimecyc * 0.2;
    int32_t samples[2] = {0, sample1};
    uint32_t ticks[18];

    swire_bitbang_timer_join(self);
    __disable_irq();
    LL_GPIO_SetPinMode(pin_sws->port, pin_sws->pin, LL_GPIO_MODE_OUTPUT);
    int32_t clkstart = swire_clock_get_cycclk() + 6;
    swire_clock_spinwait_until_cycclk(samples[0] + clkstart);
    *pino_bsrr = pino_set0;
    swire_clock_spinwait_until_cycclk(samples[1] + clkstart);
    *pino_bsrr = pino_set1;
    LL_GPIO_SetPinMode(pin_sws->port, pin_sws->pin, LL_GPIO_MODE_INPUT);

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

    for(int i = 0; i < 8; i++) {
        int32_t len0 = ticks[2 * i + 2] - ticks[2 * i + 1];
        int32_t len1 = ticks[2 * i + 1] - ticks[2 * i + 0];
        uint32_t bit = 1 << (7 - i);
        buffer |= (len0 < len1) ? bit : 0;
    }
    self->next_unit_tick = ticks[17] + (int32_t)(1.8f * bittimecyc);

    return buffer;
halt_abrupt_timeout:
    __enable_irq();
    self->error = SwireBitbangErrorTimeout;
    FURI_LOG_I("swire", "CHECKPOINT read error %lx", buffer);
    FURI_LOG_I("swire", "  CHECKPOINT tickss    = %lu", tickss);
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

bool swire_bitbang_has_error(SwireBitbang* swire) {
    return swire->error != SwireBitbangErrorNone;
}
