#pragma once

#include "src/swire/swire_bitbang.hpp"

namespace MspiFlash {

constexpr uint32_t REG_MSPI_DATA = 0x0c;
constexpr uint32_t REG_MSPI_CTRL = 0x0d;
constexpr uint32_t REG_MCU_CTRL = 0xb3;
constexpr uint32_t MCU_CTRL_FIFO = 0x80;
constexpr uint32_t MCU_CTRL_MEM = 0x00;
constexpr uint32_t FLD_MSPI_BUSY = 0x01;
constexpr uint32_t FLD_MSPI_CS = 0x01;

constexpr uint32_t FLASH_READ_STATUS_CMD_LOWBYTE = 0x05;
constexpr uint32_t FLASH_READ_STATUS_CMD_HIGHBYTE = 0x35;

constexpr uint32_t FLASH_STATUS_LOWBYTE_BIT_BUSY = 0x01;

namespace MspiFlashError {
enum enum_t {
    ERROR = 0xffffffff,
    ERROR_TIMEOUT = 0xfffffffe,
    ERROR_MODE = 0xfffffffd,
    ERROR_FLAG = 0x80000000,
};
}

namespace McuMode {
enum enum_t {
    UNKNOWN = 0,
    MEM,
    FIFO,
};
}

class MspiFlash {
    SwireBitbang* swire;
    uint32_t slave_id = 0;
    McuMode::enum_t mcu_mode = McuMode::UNKNOWN;

public:
    MspiFlash(SwireBitbang* swire)
        : swire(swire) {};

    void ensure_fifo_mode() {
        _write_addr_8bit(REG_MCU_CTRL, MCU_CTRL_FIFO);
        this->mcu_mode = McuMode::FIFO;
    }

    void mspi_wait(void) {
        if(this->mcu_mode != McuMode::FIFO) {
            throw MspiFlashError::ERROR_MODE;
        }
        swire_bitbang_transaction_start(this->swire, REG_MSPI_CTRL, SwireBitbangRwRead, slave_id);

        uint32_t timeout_deadline = furi_get_tick() + 100; // it is actually microseconds
        for(;;) {
            auto data = _read_swire_byte();
            if((data & FLD_MSPI_BUSY) == 0) {
                break;
            }
            if(((int32_t)(furi_get_tick() - timeout_deadline)) > 0) {
                swire_bitbang_transaction_end(this->swire);
                throw MspiFlashError::ERROR_TIMEOUT;
            }
        }
        swire_bitbang_transaction_end(this->swire);
    }

    inline void mspi_high() {
        _write_addr_8bit(REG_MSPI_CTRL, FLD_MSPI_CS);
    }

    inline void mspi_low() {
        _write_addr_8bit(REG_MSPI_CTRL, 0);
    }

    inline uint8_t mspi_get() {
        return _read_addr_8bit(REG_MSPI_DATA);
    }

    inline void mspi_write(uint8_t c) {
        _write_addr_8bit(REG_MSPI_DATA, c);
    }

    inline void mspi_ctrl_write(uint8_t c) {
        _write_addr_8bit(REG_MSPI_CTRL, c);
    }

    inline uint8_t mspi_read() {
        mspi_write(0); // dummy, issue clock
        mspi_wait();
        return mspi_get();
    }

    inline uint8_t _read_addr_8bit(uint32_t addr) {
        swire_bitbang_transaction_start(this->swire, addr, SwireBitbangRwRead, slave_id);
        int32_t data = swire_bitbang_byte_read(this->swire);
        swire_bitbang_transaction_end(this->swire);

        if(data < 0) {
            throw data == FuriStatusErrorTimeout ? MspiFlashError::ERROR_TIMEOUT :
                                                   MspiFlashError::ERROR;
        }
        _check_error();
        return static_cast<uint8_t>(data);
    }

    inline uint8_t _read_swire_byte() {
        auto data = swire_bitbang_byte_read(this->swire);
        if(data < 0) {
            throw data == FuriStatusErrorTimeout ? MspiFlashError::ERROR_TIMEOUT :
                                                   MspiFlashError::ERROR;
        }
        _check_error();
        return static_cast<uint8_t>(data);
    }

    void _check_error() {
        if(swire_bitbang_has_error(this->swire)) {
            throw MspiFlashError::ERROR;
        }
    }

    void _write_addr_8bit(uint32_t addr, uint8_t value) {
        swire_bitbang_transaction_start(this->swire, addr, SwireBitbangRwWrite, slave_id);
        swire_bitbang_byte_write(this->swire, value);
        swire_bitbang_transaction_end(this->swire);
        _check_error();
    }
};

}
