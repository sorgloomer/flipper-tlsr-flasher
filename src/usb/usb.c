#include "src/usb/usb.h"
#include "src/utils/global_debug.h"
#include "src/utils/ringbuffer.h"

#include "usb_cdc.h"

#include <string.h>
#include <toolbox/api_lock.h>
#include <cli/cli_vcp.h>
#include <furi_hal.h>
#include <furi_hal_usb_cdc.h>

#define USB_CDC_PKT_LEN      CDC_DATA_SZ
#define USB_UART_RX_BUF_SIZE (USB_CDC_PKT_LEN * 5)
#define USB_CDC_RX_BUF_SIZE  USB_CDC_PKT_LEN
// #define USB_CDC_RX_RINGBUF_SIZE 300
#define SW_LINE_BUFFER_SIZE  500
#define USB_CDC_BIT_DTR      (1 << 0)
#define USB_CDC_BIT_RTS      (1 << 1)

#define WORKER_ALL_RX_EVENTS \
    (WorkerEvtCfgChange | WorkerEvtLineCfgSet | WorkerEvtCtrlLineSet | WorkerEvtCdcTxComplete)
#define WORKER_ALL_TX_EVENTS       (WorkerEvtCdcRx)
#define FURI_EVENT_FLAG_VALID_BITS 0xffffff

/*
typedef enum {
    CallbackEventRxAvailable = (1 << 1),
    CallbackEventStateChange = (1 << 2),
    CallbackEventAll = CallbackEventRxAvailable | CallbackEventStateChange
} CallbackEvent;

typedef enum {
    BlockingEventTxComplete = (1 << 0),
} BlockingEvent;
*/

typedef struct {
    SwireUsbRxLineCallback callback;
    void* context;
} OnRxLineDelegate;

typedef struct {
    SwireUsbStateChangeCallback callback;
    void* context;
} OnStateChangeDelegate;

struct SwireUsb {
    // FuriEventLoop* event_loop;
    FuriEventFlag* event_flag_rx;
    FuriEventFlag* event_flag_tx;

#if SW_USB_USE_QUEUE == 1
    FuriMessageQueue* queue; // bug in firmware prevents EventFlag to notify EventLoop
#endif
#ifdef SW_USB_USE_THREAD
    FuriThread* thread; // bug in firmware prevents EventFlag to notify EventLoop, notifying manually
#endif
    // uint32_t queue_message_rx;

    volatile CdcState cdc_state;

    // OnRxLineDelegate on_rx_line;
    // OnStateChangeDelegate on_state_change;

    // RingBuffer ringbuffer_rx;
    // FuriString* string_rx;
    FuriString* string_tx;
    uint8_t* line_buffer;
    bool auto_flush;

    CliVcp* cli_vcp;
    uint8_t vcp_ch;
    uint32_t timeout_ms;
    uint32_t buffer_tx_size;
    uint8_t* buffer_tx_sending;
    uint8_t* buffer_tx_building;
    uint32_t buffer_rx_size;
    uint8_t buffer_rx[USB_CDC_RX_BUF_SIZE];
    uint8_t buffer_tx_1[USB_CDC_PKT_LEN];
    uint8_t buffer_tx_2[USB_CDC_PKT_LEN];
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
// static void swire_usb_handle_thread_flag(void* context);

static int32_t
    swire_usb_read_internal(SwireUsb* self, uint8_t* buffer, uint32_t buffer_size, int until);
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

SwireUsb* swire_usb_alloc() {
    SwireUsb* self = malloc(sizeof(SwireUsb));
    furi_check(self, "swire_usb_alloc");
    // self->queue = queue;
    // self->queue_message_rx = queue_message_rx;
    // self->thread_flag_rx = thread_flag_rx;

#ifdef SW_USB_USE_THREAD
    self->thread = furi_thread_current();
#endif
#if SW_USB_USE_QUEUE == 1
    self->queue = furi_message_queue_alloc(4, 4);
#endif

    self->timeout_ms = 500;
    self->auto_flush = true;
    // self->event_loop = event_loop;
    self->event_flag_rx = furi_event_flag_alloc();
    self->event_flag_tx = furi_event_flag_alloc();
    furi_event_flag_set(self->event_flag_tx, SwUsbTxEventTxComplete);
    // swire_usb_set_on_rx_line(self, NULL, NULL);
    // swire_usb_set_on_state_change(self, NULL, NULL);
    // ringbuffer_init(&self->ringbuffer_rx, USB_CDC_RX_RINGBUF_SIZE);
    // self->string_rx = furi_string_alloc();
    self->string_tx = furi_string_alloc();
    self->cli_vcp = furi_record_open(RECORD_CLI_VCP);
    self->buffer_tx_sending = self->buffer_tx_1;
    self->buffer_tx_building = self->buffer_tx_2;
    self->buffer_rx_size = 0;
    self->line_buffer = malloc(SW_LINE_BUFFER_SIZE);
    furi_check(self->line_buffer);

    // furi_event_loop_subscribe_thread_flags(self->event_loop, swire_usb_handle_thread_flag, self);
    // furi_event_loop_subscribe_event_flag(
    //     event_loop, self->event_flag_rx, FuriEventLoopEventIn, swire_usb_handle_events, self);
    swire_usb_vcp_init(self, 0);
    return self;
}

void swire_usb_free(SwireUsb* self) {
    if(self == NULL) return;
    swire_usb_vcp_deinit1(self);
    //furi_event_loop_unsubscribe_thread_flags(self->event_loop); // TODO: only option to unsubscribe from all?
    //furi_event_loop_unsubscribe(self->event_loop, self->event_flag_rx);
    furi_event_flag_free(self->event_flag_rx);
    self->event_flag_rx = NULL;
    furi_event_flag_free(self->event_flag_tx);
    self->event_flag_tx = NULL;
    furi_record_close(RECORD_CLI_VCP);
    // ringbuffer_deinit(&self->ringbuffer_rx);
    // furi_string_free(self->string_rx);
    furi_string_free(self->string_tx);
    free(self->line_buffer);
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

CdcState swire_usb_get_cdc_state(SwireUsb* self) {
    return self->cdc_state;
}

FuriEventFlag* swire_usb_get_event_flag_rx(SwireUsb* self) {
    return self->event_flag_rx;
}

#if SW_USB_USE_QUEUE == 1
FuriMessageQueue* swire_usb_get_queue(SwireUsb* self) {
    return self->queue;
}
#endif

FuriEventFlag* swire_usb_get_event_flag_tx(SwireUsb* self) {
    return self->event_flag_tx;
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
        self->event_flag_tx, SwUsbTxEventTxComplete, FuriFlagWaitAny, self->timeout_ms);
    if(status & FuriFlagError) {
        furi_event_flag_set(self->event_flag_tx, SwUsbTxEventTxComplete);
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
        SwUsbTxEventTxComplete,
        FuriFlagWaitAny | FuriFlagNoClear,
        self->timeout_ms);
    return STATUS_DROP_VALUE(status);
}

FuriStatus swire_usb_readline_str(SwireUsb* self, FuriString* output) {
    int32_t received = swire_usb_read_internal(self, self->line_buffer, SW_LINE_BUFFER_SIZE, '\n');
    if(received & FuriFlagError) {
        return received;
    }
    int32_t endindex = received - 1;
    if(endindex >= 0 && self->line_buffer[endindex] == '\n') {
        endindex--;
        if(endindex >= 0 && self->line_buffer[endindex] == '\r') {
            endindex--;
        }
    }
    furi_string_set_strn(output, (const char*)self->line_buffer, endindex + 1);
    return FuriStatusOk;
}

static int32_t
    swire_usb_read_internal(SwireUsb* self, uint8_t* buffer, uint32_t buffer_size, int until) {
    global_debug()->rx_trace = 0;
    uint8_t* buffer_rx = self->buffer_rx;

    uint8_t* original_buffer = buffer;
    uint32_t to_serve_from_leftover = MIN(buffer_size, self->buffer_rx_size);
    if(to_serve_from_leftover > 0) {
        uint32_t reading_count = until < 0 ? to_serve_from_leftover : ({
            uint8_t* find = memchr(self->buffer_rx, until, to_serve_from_leftover);
            if(find == NULL) {
                global_debug()->rx_trace = to_serve_from_leftover * 100 + 1;
                // not returning incomplete lines!
                return FuriStatusError;
            }
            (uint32_t)(find - self->buffer_rx + 1);
        });
        memcpy(buffer, buffer_rx, reading_count);
        buffer += reading_count;
        buffer_size -= reading_count;
        uint32_t new_leftover = self->buffer_rx_size - reading_count;
        self->buffer_rx_size = new_leftover;
        if(new_leftover > 0) {
            memcpy(buffer_rx, buffer_rx + reading_count, new_leftover);
            global_debug()->rx_trace = 2;
            goto exit_with_leftovers;
        }
    }

    uint32_t read_iteration_count = 0;
    while(buffer_size > 0) {
        FuriStatus status = furi_event_flag_wait(
            self->event_flag_rx, SwUsbRxEventRxAvailable, FuriFlagWaitAny, self->timeout_ms);
        if(status & FuriFlagError) {
            global_debug()->rx_trace = (int32_t)(buffer - original_buffer) * 10000 + 3;
            return status;
        }
        int32_t received = furi_hal_cdc_receive(
            self->vcp_ch, buffer, MIN(buffer_size, (uint32_t)USB_CDC_PKT_LEN));
        if(received < 0) {
            global_debug()->rx_trace = 4;
            return FuriStatusError;
        }
        if(received == 0 && read_iteration_count > 2) {
            // during the first iteration, the event might have been previously
            // set by leftovers instead of interrupt, so it is expected to receive
            // not bytes. subsequent reads however should return bytes. Return an
            // error to avoid a potential infinite loop
            global_debug()->rx_trace = 5;
            return FuriStatusError;
        }
        buffer += received;
        buffer_size -= received;

        if(until >= 0) {
            uint8_t* find = memchr(buffer - received, until, received);
            if(find != NULL) {
                uint32_t leftover = buffer - find - 1;
                buffer -= leftover;
                buffer_size += leftover;
                self->buffer_rx_size = leftover;
                if(leftover > 0) {
                    memcpy(self->buffer_rx, buffer, leftover);
                    global_debug()->rx_trace = 6;
                    goto exit_with_leftovers;
                }
                global_debug()->rx_trace = 7;
                goto exit_normal;
            }
        }

        read_iteration_count++;
    }

    global_debug()->rx_trace = 8;
    return buffer - original_buffer;
exit_normal:
    return buffer - original_buffer;
exit_with_leftovers:
    furi_event_flag_set(self->event_flag_rx, SwUsbRxEventRxAvailable);
    return buffer - original_buffer;
}

/* VCP callbacks */

static void vcp_irq_on_cdc_tx_complete(void* context) {
    global_debug()->irq_tx++;
    SwireUsb* self = (SwireUsb*)context;
    furi_event_flag_set(self->event_flag_tx, SwUsbTxEventTxComplete);
}

static void vcp_irq_on_cdc_rx(void* context) {
    global_debug()->irq_rx++;
    SwireUsb* self = (SwireUsb*)context;
    global_debug()->irq_rx_ts = furi_get_tick();
    uint32_t status = furi_event_flag_set(self->event_flag_rx, SwUsbRxEventRxAvailable);
    global_debug()->irq_rx_status = status;
    uint32_t msg = 0;
#if SW_USB_USE_QUEUE == 1
    furi_message_queue_put(self->queue, &msg, 0);
#endif
#if SW_USB_USE_IRQDUMMY_WORKAROUND == 1
    furi_event_flag_clear(self->event_flag_rx, SwUsbRxEventDummy);
    furi_event_flag_set(self->event_flag_rx, SwUsbRxEventDummy);
#endif
#if SW_USB_USE_DOUBLESET_WORKAROUND == 1
    furi_event_flag_clear(self->event_flag_rx, SwUsbRxEventRxAvailable);
    furi_event_flag_set(self->event_flag_rx, SwUsbRxEventRxAvailable);
#endif
}

static void vcp_irq_state_callback(void* context, CdcState state) {
    UNUSED(context);
    UNUSED(state);
    global_debug()->irq_sc++;
    SwireUsb* self = (SwireUsb*)context;
    self->cdc_state = state;
    furi_event_flag_set(self->event_flag_rx, SwUsbRxEventStateChange);
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
