#pragma once

#include "furi_hal_usb_cdc.h"
#include <stdint.h>
#include <stdbool.h>
#include <furi.h>
#include <toolbox/api_lock.h>
#include <cli/cli_vcp.h>

typedef enum WorkerEventFlags WorkerEventFlags;

typedef struct SwireUsb SwireUsb;

typedef void (*SwireUsbRxLineCallback)(void* context, SwireUsb* sender, FuriString* line);
typedef void (*SwireUsbStateChangeCallback)(void* context, SwireUsb* sender, CdcState state);

typedef enum {
    SwUsbEventRxAvailable = (1 << 0),
    SwUsbEventTxComplete = (1 << 1),
    SwUsbEventStateChange = (1 << 2),
    SwUsbEventAll = SwUsbEventRxAvailable | SwUsbEventStateChange | SwUsbEventTxComplete
} SwUsbEvent;

SwireUsb* swire_usb_alloc();
void swire_usb_free(SwireUsb* self);
// void swire_usb_set_on_rx_line(SwireUsb* self, SwireUsbRxLineCallback callback, void* context);
// void swire_usb_set_on_state_change(
//     SwireUsb* self,
//     SwireUsbStateChangeCallback callback,
//     void* context);

FuriStatus swire_usb_printf(SwireUsb* self, const char* format, ...);
FuriStatus swire_usb_printf_line(SwireUsb* self, const char* format, ...);
FuriStatus swire_usb_write(SwireUsb* self, uint8_t* buffer, uint32_t buffer_size);
FuriStatus swire_usb_write_cstr(SwireUsb* self, const char* msg);
FuriStatus swire_usb_write_str(SwireUsb* self, FuriString* msg);
FuriStatus swire_usb_writeline_cstr(SwireUsb* self, const char* msg);
FuriStatus swire_usb_writeline_str(SwireUsb* self, FuriString* msg);
FuriStatus swire_usb_write_flush(SwireUsb* self);
bool swire_usb_set_auto_flush(SwireUsb* self, bool flush_auto);

FuriStatus swire_usb_read(SwireUsb* self, uint8_t* buffer, uint32_t buffer_size);
FuriStatus swire_usb_readline_str(SwireUsb* self, FuriString* output);

uint32_t swire_usb_get_debug_value(SwireUsb* self);
CdcState swire_usb_get_cdc_state(SwireUsb* self);
void swire_usb_pull_debug_data(SwireUsb* self);
FuriEventFlag* swire_usb_get_event_flag(SwireUsb* self);
