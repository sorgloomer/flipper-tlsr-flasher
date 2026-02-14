#include "src/usb.h"
#include "src/global_debug.h"
#include "src/ringbuffer.h"

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

#define WORKER_ALL_RX_EVENTS \
    (WorkerEvtCfgChange | WorkerEvtLineCfgSet | WorkerEvtCtrlLineSet | WorkerEvtCdcTxComplete)
#define WORKER_ALL_TX_EVENTS       (WorkerEvtCdcRx)
#define FURI_EVENT_FLAG_VALID_BITS 0xffffff

typedef enum {
    CallbackEventRxAvailable = (1 << 1),
    CallbackEventStateChange = (1 << 2),
    CallbackEventAll = CallbackEventRxAvailable | CallbackEventStateChange
} CallbackEvent;

typedef enum {
    BlockingEventTxComplete = (1 << 0),
} BlockingEvent;

typedef struct {
    SwireUsbRxLineCallback callback;
    void* context;
} OnRxLineDelegate;

typedef struct {
    SwireUsbStateChangeCallback callback;
    void* context;
} OnStateChangeDelegate;

struct SwireUsb {
    FuriThread* thread;
    FuriEventLoop* event_loop;
    // FuriEventFlag* event_flag_rx;
    uint32_t thread_flag_rx;
    FuriEventFlag* event_flag_tx;

    volatile CdcState cdc_state;

    OnRxLineDelegate on_rx_line;
    OnStateChangeDelegate on_state_change;

    RingBuffer ringbuffer_rx;
    // FuriString* string_rx;
    FuriString* string_tx;
    bool auto_flush;
    volatile uint32_t debug_value;

    CliVcp* cli_vcp;
    uint8_t vcp_ch;
    uint32_t timeout_ms;
    uint32_t buffer_tx_size;
    uint8_t* buffer_tx_sending;
    uint8_t* buffer_tx_building;
    uint8_t buffer_tx_1[USB_CDC_PKT_LEN];
    uint8_t buffer_tx_2[USB_CDC_PKT_LEN];
    // uint8_t buffer_rx[USB_CDC_PKT_LEN + 1];
};

typedef enum {
    RxModeEvent,
    RxModeBlock,
} RxMode;

static void vcp_irq_on_cdc_tx_complete(void* context);
static void vcp_irq_on_cdc_rx(void* context);
static void vcp_irq_state_callback(void* context, CdcState state);
static void vcp_irq_on_cdc_control_line(void* context, CdcCtrlLine state);
static void vcp_irq_on_line_config(void* context, struct usb_cdc_line_coding* config);

static void swire_usb_vcp_init(SwireUsb* self, uint8_t vcp_ch);
static void swire_usb_vcp_deinit1(SwireUsb* self);
static void swire_usb_vcp_deinit2(SwireUsb* self);
// static void swire_usb_handle_event_flag(FuriEventLoopObject* object, void* context);
static void swire_usb_handle_thread_flag(void* context);

static FuriStatus swire_usb_rx_line_internal(SwireUsb* self, RxMode mode, FuriString* output_line);
static FuriStatus swire_usb_wait_swap_send(SwireUsb* self);

void swire_hal_usb_set_config(FuriHalUsbInterface* config);

#define STATUS_EXPECT_SUCCESS(status) \
    if((status) & FuriFlagError) {    \
        return (status);              \
    }

#define STATUS_EXPECT_OK(status)                                        \
    if((status) != FuriStatusOk) {                                      \
        return ((status) & FuriFlagError) ? (status) : FuriStatusError; \
    }
#define STATUS_DROP_VALUE(status) ((status) & FuriFlagError) ? (status) : FuriStatusOk;

static const CdcCallbacks cdc_cb = {
    vcp_irq_on_cdc_tx_complete,
    vcp_irq_on_cdc_rx,
    vcp_irq_state_callback,
    vcp_irq_on_cdc_control_line,
    vcp_irq_on_line_config,
};

SwireUsb* swire_usb_alloc(FuriEventLoop* event_loop, uint32_t thread_flag_rx) {
    SwireUsb* self = malloc(sizeof(SwireUsb));
    furi_check(self, "swire_usb_alloc");
    self->thread = furi_thread_get_current();
    self->thread_flag_rx = thread_flag_rx;
    self->timeout_ms = 100;
    self->auto_flush = true;
    self->event_loop = event_loop;
    //self->event_flag_rx = furi_event_flag_alloc();
    self->event_flag_tx = furi_event_flag_alloc();
    furi_event_flag_set(self->event_flag_tx, BlockingEventTxComplete);
    swire_usb_set_on_rx_line(self, NULL, NULL);
    swire_usb_set_on_state_change(self, NULL, NULL);
    ringbuffer_init(&self->ringbuffer_rx, 300);
    // self->string_rx = furi_string_alloc();
    self->string_tx = furi_string_alloc();
    self->cli_vcp = furi_record_open(RECORD_CLI_VCP);
    self->buffer_tx_sending = self->buffer_tx_1;
    self->buffer_tx_building = self->buffer_tx_2;

    furi_event_loop_subscribe_thread_flags(self->event_loop, swire_usb_handle_thread_flag, self);
    // furi_event_loop_subscribe_event_flag(
    //     event_loop, self->event_flag_rx, FuriEventLoopEventIn, swire_usb_handle_events, self);
    swire_usb_vcp_init(self, 0);
    return self;
}

void swire_usb_free(SwireUsb* self) {
    if(self == NULL) return;
    swire_usb_vcp_deinit1(self);
    furi_event_loop_unsubscribe_thread_flags(
        self->event_loop); // TODO: only option to unsubscribe from all?
    //furi_event_loop_unsubscribe(self->event_loop, self->event_flag_rx);
    //furi_event_flag_free(self->event_flag_rx);
    //self->event_flag_rx = NULL;
    furi_event_flag_free(self->event_flag_tx);
    self->event_flag_tx = NULL;
    furi_record_close(RECORD_CLI_VCP);
    ringbuffer_deinit(&self->ringbuffer_rx);
    // furi_string_free(self->string_rx);
    furi_string_free(self->string_tx);
    swire_usb_vcp_deinit2(self);
    free(self);
}
void swire_hal_usb_set_config(FuriHalUsbInterface* config) {
    // if(furi_hal_usb_get_config() == config) return;
    // if(furi_hal_usb_set_config(config, NULL)) return;
    // furi_hal_usb_unlock();
    furi_check(furi_hal_usb_set_config(config, NULL) == true, "swire_hal_usb_set_config");
}

static void swire_usb_vcp_init(SwireUsb* self, uint8_t vcp_ch) {
    furi_hal_usb_unlock();
    if(vcp_ch == 0) {
        cli_vcp_disable(self->cli_vcp);
        swire_hal_usb_set_config(&usb_cdc_single);
    } else {
        swire_hal_usb_set_config(&usb_cdc_dual);
        cli_vcp_enable(self->cli_vcp);
    }
    furi_hal_cdc_set_callbacks(vcp_ch, (CdcCallbacks*)&cdc_cb, self);
    self->vcp_ch = vcp_ch;
}

static void swire_usb_vcp_deinit1(SwireUsb* self) {
    furi_hal_usb_unlock();
    swire_hal_usb_set_config(&usb_cdc_single);
    furi_hal_cdc_set_callbacks(self->vcp_ch, NULL, NULL);
}
static void swire_usb_vcp_deinit2(SwireUsb* self) {
    cli_vcp_enable(self->cli_vcp);
}

void swire_usb_set_on_rx_line(SwireUsb* self, SwireUsbRxLineCallback callback, void* context) {
    self->on_rx_line.callback = callback;
    self->on_rx_line.context = context;
}

void swire_usb_set_on_state_change(
    SwireUsb* self,
    SwireUsbStateChangeCallback callback,
    void* context) {
    self->on_state_change.callback = callback;
    self->on_state_change.context = context;
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
    bool auto_flush_restore = swire_usb_set_auto_flush(self, false);
    status = swire_usb_write_cstr(self, msg);
    swire_usb_set_auto_flush(self, auto_flush_restore);
    STATUS_EXPECT_OK(status);
    return swire_usb_write_cstr(self, "\r\n");
}
FuriStatus swire_usb_writeline_str(SwireUsb* self, FuriString* msg) {
    return swire_usb_writeline_cstr(self, furi_string_get_cstr(msg));
}

uint32_t swire_usb_get_debug_value(SwireUsb* self) {
    return self->debug_value;
}
CdcState swire_usb_get_cdc_state(SwireUsb* self) {
    return self->cdc_state;
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
        STATUS_EXPECT_OK(status);
    }

    if(self->auto_flush) {
        return swire_usb_write_flush(self);
    }
    return FuriStatusOk;
}

static FuriStatus swire_usb_wait_swap_send(SwireUsb* self) {
    if(self->buffer_tx_size == 0) {
        return FuriStatusOk;
    }
    FuriStatus status = furi_event_flag_wait(
        self->event_flag_tx, BlockingEventTxComplete, FuriFlagWaitAny, self->timeout_ms);
    if(status & FuriFlagError) {
        furi_event_flag_set(self->event_flag_tx, BlockingEventTxComplete);
        return status;
    }
    SWAP(self->buffer_tx_building, self->buffer_tx_sending);
    furi_hal_cdc_send(self->vcp_ch, self->buffer_tx_sending, self->buffer_tx_size);
    self->buffer_tx_size = 0;
    return FuriStatusOk;
}

FuriStatus swire_usb_write_flush(SwireUsb* self) {
    FuriStatus status = swire_usb_wait_swap_send(self);
    STATUS_EXPECT_OK(status);
    status = furi_event_flag_wait(
        self->event_flag_tx,
        BlockingEventTxComplete,
        FuriFlagWaitAny | FuriFlagNoClear,
        self->timeout_ms);
    return STATUS_DROP_VALUE(status);
}

FuriStatus swire_usb_readline_str(SwireUsb* self, FuriString* output) {
    return swire_usb_rx_line_internal(self, RxModeBlock, output);
}

FuriStatus swire_usb_read(SwireUsb* self, uint8_t* buffer, uint32_t buffer_size) {
    for(;;) {
        if(buffer_size == 0) break;
        //FuriStatus status = furi_event_flag_wait(
        //    self->event_flag_rx, CallbackEventRxAvailable, FuriFlagWaitAny, self->timeout_ms);
        FuriStatus status =
            furi_thread_flags_wait(CallbackEventRxAvailable, FuriFlagWaitAny, self->timeout_ms);
        STATUS_EXPECT_SUCCESS(status);
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
    self->debug_value = 0;
    // char* buffer_rx = (char*)self->buffer_rx;
    RingBuffer* ring = &self->ringbuffer_rx;
    Buffer buffer_rx;
    for(;;) {
        self->debug_value = 1;
        if(mode == RxModeBlock) {
            self->debug_value = 2;
            //FuriStatus status = furi_event_flag_wait(
            //    self->event_flag_rx, CallbackEventRxAvailable, FuriFlagWaitAny, self->timeout_ms);
            FuriStatus status = furi_thread_flags_wait(
                CallbackEventRxAvailable, FuriFlagWaitAny, self->timeout_ms);
            self->debug_value = 3;
            STATUS_EXPECT_SUCCESS(status);
            self->debug_value = 4;
        }
        self->debug_value = 5;

        if(ringbuffer_get_empty_space(ring) == 0) {
            return FuriStatusError; // TODO
        }

        ringbuffer_get_continuous_write_buffer(ring, &buffer_rx);
        int32_t received = furi_hal_cdc_receive(self->vcp_ch, buffer_rx.ptr, buffer_rx.size);
        ringbuffer_advance_write_tail(ring, received);

        if(received <= 0) {
            return (mode == RxModeEvent && received == 0) ? FuriStatusOk : FuriStatusError;
        }

        self->debug_value = 8;
        uint8_t* p_newline = memchr(buffer_rx.ptr, '\n', received);
        if(p_newline == NULL) {
            self->debug_value = 9;
            continue;
        }
        self->debug_value = 10;

        uint8_t* p_endline = p_newline;
        if(p_endline > buffer_rx.ptr && p_endline[-1] == '\r') {
            self->debug_value = 11;
            p_endline--;
        }
        *p_endline = '\0';
        self->debug_value |= 0x0080;
        furi_string_cat_str(self->string_rx, buffer_rx);

        if(mode == RxModeEvent) {
            self->debug_value |= 0x0100;
            SwireUsbRxLineCallback callback = self->on_rx_line.callback;
            if(callback != NULL) callback(self->on_rx_line.context, self, self->string_rx);
        }
        self->debug_value |= 0x0200;

        if(mode == RxModeBlock && output_line != NULL) {
            self->debug_value |= 0x0400;
            furi_string_swap(output_line, self->string_rx);
        }

        self->debug_value |= 0x0800;
        furi_string_set_str(self->string_rx, p_newline + 1);
        if(mode == RxModeBlock) {
            self->debug_value |= 0x1000;
            return FuriStatusOk;
        }
        self->debug_value |= 0x2000;
    }
}

static void swire_usb_handle_cdc_rx(SwireUsb* self) {
    swire_usb_rx_line_internal(self, RxModeEvent, NULL);
}

static void swire_usb_handle_thread_flag(void* context) {
    SwireUsb* self = (SwireUsb*)context;
    global_debug()->evt_t++;
    //uint32_t before = furi_event_flag_get(self->event_flag_rx);
    uint32_t before = furi_thread_flags_get();
    // CallbackEvent events = furi_event_flag_clear(self->event_flag_rx, CallbackEventAll);
    CallbackEvent events = furi_thread_flags_clear(CallbackEventAll);
    //uint32_t after = furi_event_flag_get(self->event_flag_rx);
    uint32_t after = furi_thread_flags_get();

    uint32_t ts = furi_get_tick();
    global_debug_log("  %08lx %08lx", events, after);
    global_debug_log("hevt %04lx %08lx", ts & 0xffff, before);

    global_debug()->err_loc = 20;
    global_debug()->err = events;
    if(events & FuriFlagError) {
        global_debug()->err_loc = 21;
        FURI_LOG_E(
            "swire", "failed getting flags in swire_usb_handle_events %08lx", (uint32_t)events);
        return;
    }
    if(events & CallbackEventRxAvailable) {
        global_debug()->evt_rx++;
        swire_usb_handle_cdc_rx(self);
    }
    if(events & CallbackEventStateChange) {
        global_debug()->evt_sc++;
        if(self->on_state_change.callback != NULL) {
            self->on_state_change.callback(self->on_state_change.context, self, self->cdc_state);
        }
    }
}

// static void swire_usb_handle_event_flag(FuriEventLoopObject* object, void* context) {
//     UNUSED(object);
//     swire_usb_handle_thread_flag(context);
// }

/* VCP callbacks */

static void vcp_irq_on_cdc_tx_complete(void* context) {
    global_debug()->irq_tx++;
    SwireUsb* self = (SwireUsb*)context;
    furi_event_flag_set(self->event_flag_tx, BlockingEventTxComplete);
}

static void vcp_irq_on_cdc_rx(void* context) {
    global_debug()->irq_rx++;

    SwireUsb* self = (SwireUsb*)context;

    global_debug()->irq_rx_ts = furi_get_tick();
    // global_debug()->irq_rx_before = furi_event_flag_get(self->event_flag_rx);
    //global_debug()->irq_rx_before = furi_thread_flags_get();
    //uint32_t status = furi_event_flag_set(self->event_flag_rx, CallbackEventRxAvailable);
    uint32_t status = furi_thread_flags_set(self->thread, CallbackEventRxAvailable);
    global_debug()->irq_rx_status = status;
    //global_debug()->irq_rx_after = furi_event_flag_get(self->event_flag_rx);
}

static void vcp_irq_state_callback(void* context, CdcState state) {
    UNUSED(context);
    UNUSED(state);
    global_debug()->irq_sc++;
    SwireUsb* self = (SwireUsb*)context;
    self->cdc_state = state;
    // furi_event_flag_set(self->event_flag_rx, CallbackEventStateChange);
    furi_thread_flags_set(self->thread, CallbackEventStateChange);
}

static void vcp_irq_on_cdc_control_line(void* context, CdcCtrlLine state) {
    UNUSED(state);
    SwireUsb* self = (SwireUsb*)context;
    UNUSED(self);
}

static void vcp_irq_on_line_config(void* context, struct usb_cdc_line_coding* config) {
    UNUSED(config);
    SwireUsb* self = (SwireUsb*)context;
    UNUSED(self);
}

void swire_usb_pull_debug_data(SwireUsb* self) {
    UNUSED(self);
    //global_debug()->err = furi_event_flag_get(self->event_flag_rx);
    global_debug()->err = furi_thread_flags_get();
    global_debug()->err_loc = 57;

    // CHECKPOINT

    // all working workarounds from nonIRQ

    //uint32_t r = furi_event_flag_clear(self->event_flag_rx, CallbackEventAll);
    //furi_event_flag_set(self->event_flag_rx, r | CallbackEventRxAvailable);

    // furi_event_flag_set(self->event_flag_rx, CallbackEventRxAvailable);

    // furi_event_flag_set(self->event_flag_rx, 0);

    // swire_usb_handle_cdc_rx(self);
}
