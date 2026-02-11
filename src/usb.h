#pragma once

#include <stdint.h>
#include <stdbool.h>
#include <furi.h>
#include <toolbox/api_lock.h>
#include <cli/cli_vcp.h>

typedef enum WorkerEventFlags WorkerEventFlags;

typedef struct SwireUsb SwireUsb;

typedef void (*SwireUsbRxLineCallback)(void* context, SwireUsb* sender, FuriString* line);
typedef struct {
    SwireUsbRxLineCallback callback;
    void* context;
} SwireUsbRxLineDelegate;

SwireUsb* swire_usb_alloc(FuriEventLoop* event_loop);
void swire_usb_free(SwireUsb* self);
void swire_usb_set_on_rx_line(SwireUsb* self, SwireUsbRxLineCallback callback, void* context);

FuriStatus swire_usb_printf(SwireUsb* self, const char* format, ...);
FuriStatus swire_usb_printf_line(SwireUsb* self, const char* format, ...);
FuriStatus swire_usb_write(SwireUsb* self, uint8_t* buffer, uint32_t buffer_size);
FuriStatus swire_usb_write_cstr(SwireUsb* self, const char* msg);
FuriStatus swire_usb_write_str(SwireUsb* self, FuriString* msg);
FuriStatus swire_usb_writeline_cstr(SwireUsb* self, const char* msg);
FuriStatus swire_usb_writeline_str(SwireUsb* self, FuriString* msg);
FuriStatus swire_usb_write_flush(SwireUsb* self);
bool swire_usb_set_flush_auto(SwireUsb* self, bool flush_auto);

FuriStatus swire_usb_read(SwireUsb* self, uint8_t* buffer, uint32_t buffer_size);
FuriStatus swire_usb_readline_str(SwireUsb* self, FuriString* output);
