#pragma once

#include "src/swire/swire_uart.h"
#include "src/buildconf.h"

void do_by_uart_dumploop(SwireUart* swire) {
    FuriString* string = furi_string_alloc();
    uint8_t* buffer = malloc(256);
    furi_check(buffer);

    for(uint32_t addr = 0; addr < SW_IMAGE_LEN; addr += 16) {
        uint32_t baddr = addr & 0xff;
        if(baddr == 0) {
            int32_t rest_len = SW_IMAGE_LEN - addr;
            if(rest_len > 0x100) {
                rest_len = 0x100;
            }
            swire_uart_read(swire, 0x000c, 0, buffer, rest_len);
        }
        furi_string_printf(
            string,
            "DUMP %06lx:   %02lx %02lx %02lx %02lx",
            addr,
            (uint32_t)buffer[baddr + 0],
            (uint32_t)buffer[baddr + 1],
            (uint32_t)buffer[baddr + 2],
            (uint32_t)buffer[baddr + 3]);
        furi_string_cat_printf(
            string,
            " %02lx %02lx %02lx %02lx",
            (uint32_t)buffer[baddr + 4],
            (uint32_t)buffer[baddr + 5],
            (uint32_t)buffer[baddr + 6],
            (uint32_t)buffer[baddr + 7]);
        furi_string_cat_printf(
            string,
            "  %02lx %02lx %02lx %02lx",
            (uint32_t)buffer[baddr + 8],
            (uint32_t)buffer[baddr + 9],
            (uint32_t)buffer[baddr + 10],
            (uint32_t)buffer[baddr + 11]);
        furi_string_cat_printf(
            string,
            " %02lx %02lx %02lx %02lx\r\n",
            (uint32_t)buffer[baddr + 12],
            (uint32_t)buffer[baddr + 13],
            (uint32_t)buffer[baddr + 14],
            (uint32_t)buffer[baddr + 15]);
        furi_log_puts(furi_string_get_cstr(string));
    }
    furi_log_puts("DUMP FINISHED\r\n");
    free(buffer);
    furi_string_free(string);
}

void cmd_do_by_uart() {
    // SwireUart* swire = swire_uart_alloc(921600);
    SwireUart* swire = swire_uart_alloc(377804);
    swire->read_delay_per_byte_us = 35;
    uint8_t cmd[2];

    swire_uart_write1(swire, 0x0602, 0, 0x05); // CPU Stop
    swire_uart_write1(swire, 0x00b2, 0, 0x7f); // b0-b4 SWIRE
    int32_t sanity_check = swire_uart_read1(swire, 0x00b2, 0); // b0-b4 SWIRE
    FURI_LOG_W("swire", "Sanity test...");
    if(sanity_check != 0x7f) {
        FURI_LOG_W("swire", "Sanity test failed %02ld", sanity_check);
    }
    // MSPI = Memory SPI
    // CS = Chip Select
    swire_uart_write1(swire, 0x000d, 0, 0x00); // MSPI Control, CS bit active low

    // addr = Address
    swire_uart_write1(swire, 0x000c, 0, 0x03); // MSPI Data, 03 = read
    swire_uart_write1(swire, 0x000c, 0, 0x00); // MSPI read addr[2]
    swire_uart_write1(swire, 0x000c, 0, 0x00); // MSPI read addr[1]
    swire_uart_write1(swire, 0x000c, 0, 0x00); // MSPI read addr[0]

    cmd[0] = 0x00; // MSPI Data, 00 to drive MSPI Clock to initiate first read
    cmd[1] = 0x0a; // MSPI Control, auto read mode
    swire_uart_write(swire, 0x000c, 0, cmd, 2);

    swire_uart_write1(
        swire, 0x00b3, 0, 0x80); // swire mode, fifo, repeated reads from same address

    do_by_uart_dumploop(swire);

    swire_uart_write1(swire, 0x00b3, 0, 0x00); // swire mode reset to default
    swire_uart_write1(swire, 0x000d, 0, 0x01); // MSPI Control disable CS

    furi_delay_ms(500);
    SWIRE_UART_FREE(swire);
}
