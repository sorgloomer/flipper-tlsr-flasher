#include "src/usb.h"

#include "usb_cdc.h"

#include <string.h>
#include <toolbox/api_lock.h>
#include <cli/cli_vcp.h>
#include <furi_hal.h>
#include <furi_hal_usb_cdc.h>

#define USB_CDC_PKT_LEN      CDC_DATA_SZ
#define USB_UART_RX_BUF_SIZE (USB_CDC_PKT_LEN * 5)

#define USB_CDC_BIT_DTR (1 << 0)
#define USB_CDC_BIT_RTS (1 << 1)

#define WORKER_ALL_RX_EVENTS                                                      \
    (WorkerEvtStop | WorkerEvtRxDone | WorkerEvtCfgChange | WorkerEvtLineCfgSet | \
     WorkerEvtCtrlLineSet | WorkerEvtCdcTxComplete)
#define WORKER_ALL_TX_EVENTS       (WorkerEvtTxStop | WorkerEvtCdcRx)
#define FURI_EVENT_FLAG_VALID_BITS 0xffffff

enum WorkerEventFlags {
    WorkerEvtStop = (1 << 0),
    WorkerEvtRxDone = (1 << 1),

    WorkerEvtTxStop = (1 << 2),
    WorkerEvtCdcRxAvailable = (1 << 3),
    WorkerEvtCdcTxComplete = (1 << 4),

    WorkerEvtCfgChange = (1 << 5),

    WorkerEvtLineCfgSet = (1 << 6),
    WorkerEvtCtrlLineSet = (1 << 7),
};

struct SwireUsb {
    FuriEventLoop* event_loop;
    FuriEventFlag* event_flag_rx;
    FuriEventFlag* event_flag_tx;

    SwireUsbRxLineDelegate on_rx_line;
    FuriString* string_rx;
    FuriString* string_tx;
    bool flush_auto;

    CliVcp* cli_vcp;
    uint8_t vcp_ch;
    uint32_t timeout_ms;
    uint32_t buffer_tx_size;
    uint8_t* buffer_tx_sending;
    uint8_t* buffer_tx_building;
    uint8_t buffer_tx_1[USB_CDC_PKT_LEN];
    uint8_t buffer_tx_2[USB_CDC_PKT_LEN];
    uint8_t buffer_rx[USB_CDC_PKT_LEN + 1];
};

typedef enum {
    RxModeEvent,
    RxModeBlock,
} RxMode;

static void vcp_on_cdc_tx_complete(void* context);
static void vcp_on_cdc_rx(void* context);
static void vcp_state_callback(void* context, CdcState state);
static void vcp_on_cdc_control_line(void* context, CdcCtrlLine state);
static void vcp_on_line_config(void* context, struct usb_cdc_line_coding* config);

static void swire_usb_vcp_init(SwireUsb* self, uint8_t vcp_ch);
static void swire_usb_vcp_deinit(SwireUsb* self, uint8_t vcp_ch);
static void swire_usb_handle_events(FuriEventLoopObject* object, void* context);

static FuriStatus swire_usb_rx_line_internal(SwireUsb* self, RxMode mode, FuriString* output_line);
static FuriStatus swire_usb_wait_swap_send(SwireUsb* self);

static const CdcCallbacks cdc_cb = {
    vcp_on_cdc_tx_complete,
    vcp_on_cdc_rx,
    vcp_state_callback,
    vcp_on_cdc_control_line,
    vcp_on_line_config,
};

SwireUsb* swire_usb_alloc(FuriEventLoop* event_loop) {
    SwireUsb* self = malloc(sizeof(SwireUsb));
    furi_check(self, "swire_usb_alloc");
    self->timeout_ms = 100;
    self->flush_auto = true;
    self->event_loop = event_loop;
    self->event_flag_rx = furi_event_flag_alloc();
    self->event_flag_tx = furi_event_flag_alloc();
    furi_event_flag_set(self->event_flag_tx, WorkerEvtCdcTxComplete);
    swire_usb_set_on_rx_line(self, NULL, NULL);
    self->string_rx = furi_string_alloc();
    self->string_tx = furi_string_alloc();
    self->cli_vcp = furi_record_open(RECORD_CLI_VCP);
    self->buffer_tx_sending = self->buffer_tx_1;
    self->buffer_tx_building = self->buffer_tx_2;

    furi_event_loop_subscribe_event_flag(
        event_loop,
        self->event_flag_rx,
        FuriEventLoopEventIn | FuriEventLoopEventFlagEdge,
        swire_usb_handle_events,
        self);
    swire_usb_vcp_init(self, 0);
    return self;
}

void swire_usb_free(SwireUsb* self) {
    if(self == NULL) return;
    swire_usb_vcp_deinit(self, self->vcp_ch);
    furi_hal_usb_unlock();
    furi_check(furi_hal_usb_set_config(&usb_cdc_single, NULL) == true, "swire_usb_free");
    furi_event_loop_unsubscribe(self->event_loop, self->event_flag_rx);
    furi_event_flag_free(self->event_flag_rx);
    furi_event_flag_free(self->event_flag_tx);
    furi_record_close(RECORD_CLI_VCP);
    furi_string_free(self->string_rx);
    furi_string_free(self->string_tx);
    cli_vcp_enable(self->cli_vcp);
    free(self);
}

static void swire_usb_vcp_init(SwireUsb* self, uint8_t vcp_ch) {
    furi_hal_usb_unlock();
    if(vcp_ch == 0) {
        cli_vcp_disable(self->cli_vcp);
        furi_check(furi_hal_usb_set_config(&usb_cdc_single, NULL) == true);
    } else {
        furi_check(furi_hal_usb_set_config(&usb_cdc_dual, NULL) == true);
        cli_vcp_enable(self->cli_vcp);
    }
    furi_hal_cdc_set_callbacks(vcp_ch, (CdcCallbacks*)&cdc_cb, self);
    self->vcp_ch = vcp_ch;
}

static void swire_usb_vcp_deinit(SwireUsb* self, uint8_t vcp_ch) {
    UNUSED(self);
    furi_hal_cdc_set_callbacks(vcp_ch, NULL, NULL);
    if(vcp_ch != 0) {
        cli_vcp_disable(self->cli_vcp);
    }
}

void swire_usb_set_on_rx_line(SwireUsb* self, SwireUsbRxLineCallback callback, void* context) {
    self->on_rx_line.callback = callback;
    self->on_rx_line.context = context;
}

FuriStatus swire_usb_printf(SwireUsb* self, const char* format, ...) {
    va_list args;
    va_start(args, format);
    int printed = furi_string_vprintf(self->string_tx, format, args);
    va_end(args);
    if(printed < 0) {
        return FuriStatusErrorParameter;
    }
    return swire_usb_write_str(self, self->string_tx);
}

FuriStatus swire_usb_printf_line(SwireUsb* self, const char* format, ...) {
    va_list args;
    va_start(args, format);
    int printed = furi_string_vprintf(self->string_tx, format, args);
    va_end(args);
    if(printed < 0) {
        return FuriStatusErrorParameter;
    }
    furi_string_cat(self->string_tx, "\r\n");
    return swire_usb_write_str(self, self->string_tx);
}

FuriStatus swire_usb_vprintf(SwireUsb* self, const char* format, va_list args) {
    int printed = furi_string_vprintf(self->string_tx, format, args);
    if(printed < 0) {
        return FuriStatusErrorParameter;
    }
    return swire_usb_write_str(self, self->string_tx);
}

FuriStatus swire_usb_write_cstr(SwireUsb* self, const char* msg) {
    return swire_usb_write(self, (uint8_t*)msg, strlen(msg));
}

FuriStatus swire_usb_write_str(SwireUsb* self, FuriString* msg) {
    return swire_usb_write_cstr(self, furi_string_get_cstr(msg));
}

FuriStatus swire_usb_writeline_cstr(SwireUsb* self, const char* msg) {
    FuriStatus status;
    bool old_flush = self->flush_auto;
    self->flush_auto = false;
    status = swire_usb_write_cstr(self, msg);
    self->flush_auto = old_flush;
    if(status != FuriStatusOk) {
        return status;
    }
    return swire_usb_write_cstr(self, "\r\n");
}
FuriStatus swire_usb_writeline_str(SwireUsb* self, FuriString* msg) {
    return swire_usb_writeline_cstr(self, furi_string_get_cstr(msg));
}

#define SWAP(a, b) _SWAP(a, b, _tmp_##__LINE__)

#define _SWAP(a, b, t) \
    typeof(a) t = (a); \
    (a) = (b);         \
    (b) = (t);

FuriStatus swire_usb_write(SwireUsb* self, uint8_t* buffer, uint32_t buffer_size) {
    FuriStatus status;
    while(buffer_size > 0) {
        uint32_t buffer_tx_left = USB_CDC_PKT_LEN - self->buffer_tx_size;
        uint32_t copy_size = MIN(buffer_size, buffer_tx_left);
        memcpy(self->buffer_tx_building + self->buffer_tx_size, buffer, copy_size);

        self->buffer_tx_size += copy_size;
        buffer_tx_left -= copy_size;
        buffer += copy_size;
        buffer_size -= copy_size;

        if(buffer_tx_left != 0) {
            break;
        }

        status = swire_usb_wait_swap_send(self);
        if(status != FuriStatusOk) {
            return status;
        }
    }

    if(self->flush_auto) {
        return swire_usb_write_flush(self);
    }
    return FuriStatusOk;
}

static FuriStatus swire_usb_wait_swap_send(SwireUsb* self) {
    FuriStatus status = furi_event_flag_wait(
        self->event_flag_tx, WorkerEvtCdcTxComplete, FuriFlagWaitAny, self->timeout_ms);
    if(status != FuriStatusOk) {
        return status;
    }
    SWAP(self->buffer_tx_building, self->buffer_tx_sending);

    if(status != FuriStatusOk) {
        return status;
    }
    if(self->buffer_tx_size > 0) {
        furi_hal_cdc_send(self->vcp_ch, self->buffer_tx_sending, self->buffer_tx_size);
    }
    self->buffer_tx_size = 0;
    return status;
}

FuriStatus swire_usb_write_flush(SwireUsb* self) {
    FuriStatus status = swire_usb_wait_swap_send(self);
    if(status != FuriStatusOk) {
        return status;
    }
    return furi_event_flag_wait(
        self->event_flag_tx,
        WorkerEvtCdcTxComplete,
        FuriFlagWaitAny | FuriFlagNoClear,
        self->timeout_ms);
}

FuriStatus swire_usb_readline_str(SwireUsb* self, FuriString* output) {
    return swire_usb_rx_line_internal(self, RxModeBlock, output);
}

static void swire_usb_mark(SwireUsb* self, WorkerEventFlags flags) {
    furi_event_flag_set(self->event_flag_rx, flags);
}

FuriStatus swire_usb_read(SwireUsb* self, uint8_t* buffer, uint32_t buffer_size) {
    for(;;) {
        if(buffer_size == 0) break;
        FuriStatus status = furi_event_flag_wait(
            self->event_flag_rx, WorkerEvtCdcRxAvailable, FuriFlagWaitAny, self->timeout_ms);

        if(status != FuriStatusOk) {
            return status;
        }
        int32_t len = furi_hal_cdc_receive(self->vcp_ch, buffer, buffer_size);
        if(len <= 0 || ((uint32_t)len) > buffer_size) {
            return FuriStatusError;
        }
        buffer += len;
        buffer_size -= len;
    }
    return FuriStatusOk;
}

static FuriStatus
    swire_usb_rx_line_internal(SwireUsb* self, RxMode mode, FuriString* output_line) {
    char* buffer_rx = (char*)self->buffer_rx;
    for(;;) {
        if(mode == RxModeBlock) {
            FuriStatus status = furi_event_flag_wait(
                self->event_flag_rx, WorkerEvtCdcRxAvailable, FuriFlagWaitAny, self->timeout_ms);
            if(status != FuriStatusOk) {
                return status;
            }
        }
        int32_t len = furi_hal_cdc_receive(self->vcp_ch, (uint8_t*)buffer_rx, USB_CDC_PKT_LEN);
        if(len <= 0) {
            return (mode == RxModeEvent && len == 0) ? FuriStatusOk : FuriStatusError;
        }
        buffer_rx[len] = 0;

        char* p_newline = strchr(buffer_rx, '\n');
        if(p_newline == NULL) {
            furi_string_cat_str(self->string_rx, buffer_rx);
            continue;
        }

        char* p_endline = p_newline;
        if(p_endline > buffer_rx && p_endline[-1] == '\r') {
            p_endline--;
        }
        *p_endline = '\0';
        furi_string_cat_str(self->string_rx, buffer_rx);

        if(mode == RxModeEvent) {
            SwireUsbRxLineCallback callback = self->on_rx_line.callback;
            if(callback != NULL) callback(self->on_rx_line.context, self, self->string_rx);
        }

        if(mode == RxModeBlock && output_line != NULL) {
            furi_string_swap(output_line, self->string_rx);
        }

        furi_string_set_str(self->string_rx, p_newline + 1);
        if(mode == RxModeBlock) {
            return FuriStatusOk;
        }
    }
}

static void swire_usb_handle_cdc_rx(SwireUsb* self) {
    swire_usb_rx_line_internal(self, RxModeEvent, NULL);
}

static void swire_usb_handle_events(FuriEventLoopObject* object, void* context) {
    UNUSED(object);

    SwireUsb* self = (SwireUsb*)context;
    WorkerEventFlags events =
        furi_event_flag_clear(self->event_flag_rx, FURI_EVENT_FLAG_VALID_BITS);
    if(events & WorkerEvtCdcRxAvailable) {
        swire_usb_handle_cdc_rx(self);
    }
}

/* VCP callbacks */

static void vcp_on_cdc_tx_complete(void* context) {
    SwireUsb* self = (SwireUsb*)context;
    furi_event_flag_set(self->event_flag_tx, WorkerEvtCdcTxComplete);
}

static void vcp_on_cdc_rx(void* context) {
    SwireUsb* self = (SwireUsb*)context;
    swire_usb_mark(self, WorkerEvtCdcRxAvailable);
}

static void vcp_state_callback(void* context, CdcState state) {
    UNUSED(context);
    UNUSED(state);
}

static void vcp_on_cdc_control_line(void* context, CdcCtrlLine state) {
    UNUSED(state);
    SwireUsb* self = (SwireUsb*)context;
    swire_usb_mark(self, WorkerEvtCdcRxAvailable);
}

static void vcp_on_line_config(void* context, struct usb_cdc_line_coding* config) {
    UNUSED(config);
    SwireUsb* self = (SwireUsb*)context;
    swire_usb_mark(self, WorkerEvtLineCfgSet);
}
