#pragma once

#include "furi_hal_usb_cdc.h"
#include <stdint.h>
#include <stdbool.h>
#include <furi.h>
#include <toolbox/api_lock.h>
#include <cli/cli_vcp.h>

#define SW_USB_USE_QUEUE              1
#define SW_USB_USE_POLLING_WORKAROUND 1

typedef enum WorkerEventFlags WorkerEventFlags;

typedef struct SwireUsb SwireUsb;

typedef void (*SwireUsbRxLineCallback)(void* context, SwireUsb* sender, FuriString* line);
typedef void (*SwireUsbStateChangeCallback)(void* context, SwireUsb* sender, CdcState state);

typedef enum {
    SwUsbRxEventRxAvailable = (1 << 0),
    SwUsbRxEventStateChange = (1 << 1),
    SwUsbRxEventDummy = (1 << 15),
    SwUsbRxEventAll = SwUsbRxEventRxAvailable | SwUsbRxEventStateChange | SwUsbRxEventDummy,
} SwUsbRxEvent;

typedef enum {
    SwUsbTxEventTxComplete = (1 << 8),
} SwUsbTxEvent;

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

CdcState swire_usb_get_cdc_state(SwireUsb* self);
void swire_usb_pull_debug_data(SwireUsb* self);
FuriEventFlag* swire_usb_get_event_flag_rx(SwireUsb* self);
FuriEventFlag* swire_usb_get_event_flag_tx(SwireUsb* self);
// FuriMessageQueue* swire_usb_get_queue(SwireUsb* self);
