#include <furi.h>

#include "swire.h"

uint32_t unit_ticks;
uint32_t unit_ticks_times_2;
uint32_t unit_ticks_times_2_5;
uint32_t unit_ticks_times_3;
uint32_t unit_ticks_times_4;
uint32_t unit_ticks_backoff;

void _SwireSwsSetListen(const Swire* swire);
void _SwireSwsSetDrive(const Swire* swire);

void swire_global_init() {
    unit_ticks = furi_ms_to_ticks(208) / 1000;
    unit_ticks_times_2 = 2 * unit_ticks;
    unit_ticks_times_3 = 3 * unit_ticks;
    unit_ticks_times_4 = 4 * unit_ticks;
    unit_ticks_times_2_5 = 5 * unit_ticks / 2;
    unit_ticks_backoff = unit_ticks * 20;
}

void SwireInit(Swire* swire, const GpioPin* pin_sws) {
    swire->pin_sws = pin_sws;
    swire->timeout_ticks = furi_ms_to_ticks(10);
    swire->error = SwireErrorNone;
    _SwireSwsSetListen(swire);
    SwireTimerRestart(swire);
}

void SwireTimerRestart(Swire* swire) {
    swire->next_unit_tick = furi_get_tick();
}

void SwireTimerContinue(Swire* swire) {
    uint32_t next_unit_tick = swire->next_unit_tick;
    int32_t delta = furi_get_tick() - next_unit_tick - 10;
    if(delta > 0) {
        furi_delay_until_tick(next_unit_tick);
    }
    swire->next_unit_tick = furi_get_tick();
}

void _SwireWriteBits(Swire* swire, uint32_t bits, int count) {
    SwireTimerContinue(swire);
    _SwireSwsSetDrive(swire);
    int32_t prev_lock = furi_kernel_lock();
    uint32_t tick = furi_get_tick();
    for(; count > 0; count--) {
        furi_hal_gpio_write(swire->pin_sws, false);
        uint32_t bit_mask = -(int32_t)((bits >> count) & 1);
        tick += unit_ticks + unit_ticks_times_3 & bit_mask;
        furi_delay_until_tick(tick);

        furi_hal_gpio_write(swire->pin_sws, true);
        tick += unit_ticks_times_4 - unit_ticks_times_3 & bit_mask;
        furi_delay_until_tick(tick);
    }
    furi_hal_gpio_write(swire->pin_sws, false);
    tick += unit_ticks;
    furi_delay_until_tick(tick);
    furi_hal_gpio_write(swire->pin_sws, true);
    _SwireSwsSetListen(swire);
    furi_kernel_restore_lock(prev_lock);
    swire->next_unit_tick = tick + unit_ticks_backoff;
}

void SwireTransactionStart(Swire* swire, uint32_t addr, Rw rw, uint32_t slave_id) {
    if(swire->error != SwireErrorNone) return;
    furi_hal_gpio_write(swire->pin_sws, true);
    furi_hal_gpio_init(swire->pin_sws, GpioModeOutputPushPull, GpioPullNo, GpioSpeedVeryHigh);

    int32_t rwid = (rw == RwRead ? 0x80 : 0x00) | (slave_id & 0x7f);
    _SwireWriteBits(swire, 0x15a, 9);
    _SwireWriteBits(swire, (addr >> 16) & 0xff, 9);
    _SwireWriteBits(swire, (addr >> 8) & 0xff, 9);
    _SwireWriteBits(swire, (addr >> 0) & 0xff, 9);
    _SwireWriteBits(swire, rwid, 9);
}

void SwireTransactionEnd(Swire* swire) {
    if(SwireHasError(swire)) return;
    _SwireWriteBits(swire, 0x1ff, 9);
}

void SwireByteWrite(Swire* swire, uint8_t data) {
    if(SwireHasError(swire)) return;
    _SwireWriteBits(swire, data, 9);
}

bool _WaitForPinOrTimeout(const GpioPin* pin, bool value, uint32_t timeout) {
    while(furi_hal_gpio_read(pin) != value) {
        if(((int32_t)(timeout - furi_get_tick())) < 0) {
            // signed comparison to handle tick overflow
            return true;
        }
    }
    return false;
}

int32_t SwireByteRead(Swire* swire) {
    if(SwireHasError(swire)) return -1;
    uint32_t timeout;
    uint32_t timer;
    const GpioPin* pin_sws = swire->pin_sws;
    SwireTimerContinue(swire);
    _SwireSwsSetDrive(swire);
    int32_t prev_lock = furi_kernel_lock();
    timeout = furi_get_tick() + swire->timeout_ticks;

    uint32_t tick = furi_get_tick();
    furi_hal_gpio_write(pin_sws, false);
    tick += unit_ticks;
    furi_delay_until_tick(tick);

    _SwireSwsSetListen(swire);

    int ibit = 0;
    uint32_t buffer = 0;
    for(ibit = 0; ibit < 8; ibit++) {
        if(_WaitForPinOrTimeout(pin_sws, false, timeout)) goto halt_abrupt;

        timer = furi_get_tick() + unit_ticks_times_2_5;
        buffer = buffer << 1;
        furi_delay_until_tick(timer);
        buffer |= furi_hal_gpio_read(pin_sws) ? 0 : 1;
        if(_WaitForPinOrTimeout(pin_sws, true, timeout)) goto halt_abrupt;
    }

    if(_WaitForPinOrTimeout(pin_sws, false, timeout)) goto halt_abrupt;

    timer = furi_get_tick() + unit_ticks_times_2;
    furi_kernel_restore_lock(prev_lock);

    furi_delay_until_tick(timer);
    swire->next_unit_tick = timer + unit_ticks_backoff;

    return buffer;
halt_abrupt:
    furi_kernel_restore_lock(prev_lock);
    swire->error = SwireErrorTimeout;
    return -1;
}

void _SwireSwsSetListen(const Swire* swire) {
    furi_hal_gpio_write(swire->pin_sws, true);
    furi_hal_gpio_init(swire->pin_sws, GpioModeInput, GpioPullUp, GpioSpeedVeryHigh);
}

void _SwireSwsSetDrive(const Swire* swire) {
    furi_hal_gpio_write(swire->pin_sws, true);
    furi_hal_gpio_init(swire->pin_sws, GpioModeOutputPushPull, GpioPullNo, GpioSpeedVeryHigh);
}

bool SwireHasError(Swire* swire) {
    return swire->error != SwireErrorNone;
}
