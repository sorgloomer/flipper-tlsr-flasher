#pragma once
#include <string>

#include "src/buildconf.hpp"
#include "furi_hal_usb_cdc.h"
#include <stdint.h>
#include <stdbool.h>
#include <furi.h>
#include <toolbox/api_lock.h>
#include <cli/cli_vcp.h>

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

FuriStatus swire_usb_printf(SwireUsb* self, const char* format, ...);
FuriStatus swire_usb_printf_ln(SwireUsb* self, const char* format, ...);
FuriStatus swire_usb_write(SwireUsb* self, uint8_t* buffer, uint32_t buffer_size);
FuriStatus swire_usb_write_cstr(SwireUsb* self, const char* msg);
FuriStatus swire_usb_write_str(SwireUsb* self, const std::string& msg);
FuriStatus swire_usb_writeline_cstr(SwireUsb* self, const char* msg);
FuriStatus swire_usb_writeline_str(SwireUsb* self, const std::string& msg);
FuriStatus swire_usb_write_flush(SwireUsb* self);
bool swire_usb_set_auto_flush(SwireUsb* self, bool auto_flush);

FuriStatus swire_usb_read(SwireUsb* self, uint8_t* buffer, uint32_t buffer_size);
FuriStatus swire_usb_readline_str(SwireUsb* self, std::string& output);

CdcState swire_usb_get_cdc_state(SwireUsb* self);
void swire_usb_pull_debug_data(SwireUsb* self);
FuriEventFlag* swire_usb_get_event_flag_rx(SwireUsb* self);
FuriEventFlag* swire_usb_get_event_flag_tx(SwireUsb* self);
// FuriMessageQueue* swire_usb_get_queue(SwireUsb* self);

uint32_t swire_usb_get_debug_rx(SwireUsb* self);
