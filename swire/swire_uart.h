#pragma once

#include <furi.h>
#include <furi_hal.h>
#include "swire_common.h"

#define SWIRE_UART_TX_BUFFER_SIZE 8
#define SWIRE_UART_RX_BUFFER_SIZE 8

typedef struct {
    FuriHalSerialHandle* serial_handle;
    uint32_t read_delay_per_byte_us;
    volatile FuriHalSerialRxEvent rx_event;
    volatile uint32_t rx_buffer_idx;
    uint8_t tx_buffer[SWIRE_UART_TX_BUFFER_SIZE];
    uint8_t rx_buffer[SWIRE_UART_RX_BUFFER_SIZE];
} SwireUart;

SwireUart* swire_uart_alloc(uint32_t baudrate) {
    SwireUart* self = malloc(sizeof(SwireUart));

    self->read_delay_per_byte_us = 0;
    self->serial_handle = furi_hal_serial_control_acquire(FuriHalSerialIdUsart);
    furi_check(self->serial_handle);
    furi_hal_serial_init(self->serial_handle, baudrate);
    furi_hal_serial_configure_framing(
        self->serial_handle,
        FuriHalSerialDataBits8,
        FuriHalSerialParityNone,
        FuriHalSerialStopBits1);

    self->rx_buffer_idx = 0;
    return self;
}

void swire_uart_free(SwireUart* self) {
    furi_hal_serial_tx_wait_complete(self->serial_handle);
    furi_hal_serial_deinit(self->serial_handle);
    furi_hal_serial_control_release(self->serial_handle);
    free(self);
}

#define SWIRE_UART_ENSURE(ref) \
    swire_uart_free(ref);      \
    (ref) = swire_uart_alloc();
#define SWIRE_UART_FREE(ref) \
    swire_uart_free(ref);    \
    (ref) = NULL;

void swire_uart_encode_9bit_block(uint32_t value, uint8_t* buffer) {
    // UART  |         0         |         1         |         2         |         3         |         4         |
    //        0 x x x x x x x x 1 0 x x x x x x x x 1 0 x x x x x x x x 1 0 x x x x x x x x 1 0 x x x x x x x x 1
    //          0 1 2 3 4 5 6 7     0 1 2 3 4 5 6 7     0 1 2 3 4 5 6 7     0 1 2 3 4 5 6 7     0 1 2 3 4 5 6 7
    // SWIRE |    8    |    7    |    6    |    5    |    4    |    3    |    2    |    1    |    0    |    E    |
    //        0 s s s 1 0 s s s 1 0 s s s 1 0 s s s 1 0 s s s 1 0 s s s 1 0 s s s 1 0 s s s 1 0 s s s 1 0 1 1 1 1
    //        sss01sss                                                                        11101sss
    //            0         1
    //       |_|^^^^^^^|_______|^|

    buffer[0] = 0x08;
    buffer[1] = 0x08;
    buffer[2] = 0x08;
    buffer[3] = 0x08;
    buffer[4] = 0xe8;
    buffer[0] |= (((value >> 8) & 1) - 1) & 0x07;
    buffer[0] |= (((value >> 7) & 1) - 1) & 0xe0;
    buffer[1] |= (((value >> 6) & 1) - 1) & 0x07;
    buffer[1] |= (((value >> 5) & 1) - 1) & 0xe0;
    buffer[2] |= (((value >> 4) & 1) - 1) & 0x07;
    buffer[2] |= (((value >> 3) & 1) - 1) & 0xe0;
    buffer[3] |= (((value >> 2) & 1) - 1) & 0x07;
    buffer[3] |= (((value >> 1) & 1) - 1) & 0xe0;
    buffer[4] |= (((value >> 0) & 1) - 1) & 0x07;
}

void swire_uart_write_9bit(SwireUart* self, uint32_t value) {
    uint8_t* tx_buffer = self->tx_buffer;
    swire_uart_encode_9bit_block(value, tx_buffer);
    __disable_irq();
    furi_hal_serial_tx(self->serial_handle, tx_buffer, 5);
    __enable_irq();
}

static void swire_uart_echo_on_irq_cb(
    FuriHalSerialHandle* handle,
    FuriHalSerialRxEvent event,
    void* context) {
    UNUSED(handle);

    SwireUart* self = context;
    self->rx_event |= event;
    if(event & FuriHalSerialRxEventData) {
        uint8_t data = furi_hal_serial_async_rx(handle);
        size_t idx = self->rx_buffer_idx;
        if(idx < SWIRE_UART_RX_BUFFER_SIZE) {
            self->rx_buffer[idx] = data;
            idx++;
            self->rx_buffer_idx = idx;
        }
    }
}

void swire_uart_read_bytes(SwireUart* self, uint8_t* buffer, size_t buffer_size) {
    // UART  |         0         |         1         |         2         |         3         |         4         |
    //        0 x x x x x x x x 1 0 x x x x x x x x 1 0 x x x x x x x x 1 0 x x x x x x x x 1 0 x x x x x x x x 1
    //          0 1 2 3 4 5 6 7     0 1 2 3 4 5 6 7     0 1 2 3 4 5 6 7     0 1 2 3 4 5 6 7     0 1 2 3 4 5 6 7
    // SWIRE |    T    |    7    |    6    |    5    |    4    |    3    |    2    |    1    |    0    |    E    |
    //    TX  0 0 1 1 1 1 1 1 1 1
    //    RX            0 s s s 1 0 s s s 1 0 s s s 1 0 s s s 1 0 s s s 1 0 s s s 1 0 s s s 1 0 s s s 1 0 1 1 1 1
    //
    //            0           1
    //       |_|^^^^^^^| |_______|^|

    uint8_t* tx_buffer = self->tx_buffer;
    uint8_t* rx_buffer = self->rx_buffer;
    uint32_t delay_us = self->read_delay_per_byte_us;
    FuriHalSerialHandle* handle = self->serial_handle;
    furi_hal_serial_tx_wait_complete(handle);
    tx_buffer[0] = 0xff;
    furi_hal_serial_async_rx_start(handle, swire_uart_echo_on_irq_cb, self, true);

    for(size_t i = 0; i < buffer_size; i++) {
        furi_delay_us(delay_us);
        self->rx_event = 0;
        self->rx_buffer_idx = 0;
        furi_hal_serial_tx(handle, tx_buffer, 1);
        while(self->rx_buffer_idx < 5 && (self->rx_event & ~FuriHalSerialRxEventData) == 0)
            ;
        uint32_t acc = 0;
        acc |= ((~rx_buffer[0] >> 1) & 0x01) << 7;
        acc |= ((~rx_buffer[0] >> 6) & 0x01) << 6;
        acc |= ((~rx_buffer[1] >> 1) & 0x01) << 5;
        acc |= ((~rx_buffer[1] >> 6) & 0x01) << 4;
        acc |= ((~rx_buffer[2] >> 1) & 0x01) << 3;
        acc |= ((~rx_buffer[2] >> 6) & 0x01) << 2;
        acc |= ((~rx_buffer[3] >> 1) & 0x01) << 1;
        acc |= ((~rx_buffer[3] >> 6) & 0x01) << 0;
        buffer[i] = acc;
    }

    furi_hal_serial_async_rx_stop(handle);
}

SWIRE_INLINE static void transaction_start(SwireUart* self) {
    swire_uart_write_9bit(self, 0x15a);
}
SWIRE_INLINE static void transaction_addr(SwireUart* self, uint32_t addr) {
    swire_uart_write_9bit(self, (addr >> 16) & 0xff);
    swire_uart_write_9bit(self, (addr >> 8) & 0xff);
    swire_uart_write_9bit(self, (addr >> 0) & 0xff);
}
SWIRE_INLINE static void transaction_end(SwireUart* self) {
    swire_uart_write_9bit(self, 0x1ff);
}

void swire_uart_write(
    SwireUart* self,
    uint32_t addr,
    uint8_t slave_id,
    uint8_t* buffer,
    size_t buffer_size) {
    transaction_start(self);
    transaction_addr(self, addr);
    swire_uart_write_9bit(self, slave_id & 0x7f);
    uint8_t* buffer_end = buffer + buffer_size;
    for(uint8_t* p = buffer; p != buffer_end; p++) {
        swire_uart_write_9bit(self, *p);
    }
    transaction_end(self);
}

void swire_uart_write1(SwireUart* self, uint32_t addr, uint8_t slave_id, uint8_t data) {
    uint8_t buffer[1] = {data};
    swire_uart_write(self, addr, slave_id, buffer, 1);
}

void swire_uart_read(
    SwireUart* self,
    uint32_t addr,
    uint8_t slave_id,
    uint8_t* buffer,
    size_t buffer_size) {
    transaction_start(self);
    transaction_addr(self, addr);
    swire_uart_write_9bit(self, (slave_id & 0x7f) | 0x80);
    swire_uart_read_bytes(self, buffer, buffer_size);
    transaction_end(self);
}

int32_t swire_uart_read1(SwireUart* self, uint32_t addr, uint8_t slave_id) {
    uint8_t buffer[1];
    swire_uart_read(self, addr, slave_id, buffer, 1);
    return buffer[0];
}
