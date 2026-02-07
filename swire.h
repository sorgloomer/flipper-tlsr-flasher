#pragma once

#include <furi.h>

typedef enum {
    RwWrite = 0,
    RwRead = 1,
} Rw;

typedef enum {
    SwireErrorNone = 0,
    SwireErrorTimeout = 1,
} SwireError;

typedef struct {
    const GpioPin* pin_sws;
    uint32_t next_unit_tick;
    uint32_t timeout_ticks;
    SwireError error;
} Swire;

void swire_global_init();
void SwireInit(Swire* swire, const GpioPin* pin_sws);
void SwireTimerRestart(Swire* swire);
void SwireTimerContinue(Swire* swire);
bool SwireHasError(Swire* swire);

void SwireTransactionStart(Swire* swire, uint32_t addr, Rw rw, uint32_t slave_id);
void SwireTransactionEnd(Swire* swire);

void SwireByteWrite(Swire* swire, uint8_t data);
int32_t SwireByteRead(Swire* swire);
