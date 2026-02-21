from contextlib import closing
import datetime
import os
import time
import serial
import serial.tools.list_ports
from subprocess import check_call as run
import argparse
from contextlib import ExitStack

SW_MAX_IMAGE_LEN = 512 * 1024
SW_CHUNK_SIZE = 256
TLSR_FLASH_SECTOR_SIZE = 4096
TLSR_FLASH_CMD_INITIATE_READ = 0x00
TLSR_FLASH_CMD_WRITE = 0x02
TLSR_FLASH_CMD_READ = 0x03
TLSR_FLASH_CMD_WRITE_DISABLE = 0x04
TLSR_FLASH_CMD_READ_STATUS = 0x05
TLSR_FLASH_CMD_WRITE_ENABLE = 0x06
TLSR_FLASH_CMD_ERASE_SECTOR = 0x20
TLSR_FLASH_CMD_ERASE_CHIP = 0x60
TLSR_FLASH_CMD_GET_JEDEC_ID = 0x9F
TLSR_FLASH_CMD_POWER_DOWN = 0xB9
TLSR_FLASH_CMD_ERASE_BLOCK = 0xD8


REG_SPI_DATA = 0x000C
REG_SPI_CTRL = 0x000D


def main(args=None):
    if args is None:
        args = build_argparse().parse_args()

    if args.fap:
        redeploy_fap(args)
    if args.dump:
        dump(args)
    if args.flash is not None:
        flash(args)
    if args.erase is not None:
        erase(args)


def build_argparse():
    parser = argparse.ArgumentParser()
    parser.add_argument("--fap", action=argparse.BooleanOptionalAction, default=False)
    parser.add_argument("--dump", action=argparse.BooleanOptionalAction, default=False)
    parser.add_argument("--short", action=argparse.BooleanOptionalAction, default=True)
    parser.add_argument("--out", type=str, default=None)
    parser.add_argument("--flash", type=str, default=None)
    parser.add_argument("--erase", type=int, default=None)
    parser.add_argument("--length", type=int, default=0)
    parser.add_argument("--addr", type=int, default=0)
    parser.add_argument("--bitrate", type=int, default=75000)
    parser.add_argument("--baud", type=int, default=115200)
    parser.add_argument("--reset-duration", type=int, default=500)
    return parser


def erase(args):
    flash_length = args.length
    if not flash_length:
        flash_length = SW_MAX_IMAGE_LEN
    meter = BandwidthCounter()
    with ExitStack() as stack:
        device = stack.enter_context(TlsrDevice(args))

        device.with_common_setup(stack)

        device.flash.mspi_send_data(
            [
                TLSR_FLASH_CMD_WRITE_ENABLE,
                TLSR_FLASH_CMD_ERASE_SECTOR,
                *device.flash.blk_addr(args.erase & (TLSR_FLASH_SECTOR_SIZE - 1)),
            ]
        )
        device.flash.wait_ready()


def flash(args):
    flash_length = args.length
    if not flash_length:
        flash_length = SW_MAX_IMAGE_LEN
    meter = BandwidthCounter()
    with ExitStack() as stack:
        device = stack.enter_context(TlsrDevice(args))

        device.with_common_setup(stack)

        device.flash.mspi_send_data(TLSR_FLASH_CMD_WRITE_ENABLE)
        device.flash.mspi_init_write(args.addr)
        # device.flash.mspi_start_auto_read()  # TODO: maybe not?
        # device.flash.mspi_send_data(TLSR_FLASH_CMD_INITIATE_READ)

        flash_length = min(flash_length, os.path.getsize(args.flash))
        f = open(args.flash, "rb")
        addr = 0
        meter.restart()
        while addr < flash_length:
            chunksize = min(SW_CHUNK_SIZE, flash_length - addr)
            buf = f.read(chunksize)
            print(f"Writing {len(buf):x}/{chunksize:x}/{flash_length:x} to flash")
            device.flash.mspi_send_data(buf)
            device.flash.wait_ready()
            addr += chunksize
            meter.add_batch(len(buf))
    print(f"[i] flashed {meter.humantotal()}")


def dump(args):
    dump_length = args.length
    if not dump_length:
        dump_length = SW_MAX_IMAGE_LEN
    meter = BandwidthCounter()
    with ExitStack() as stack:
        device = stack.enter_context(TlsrDevice(args))

        device.with_common_setup(stack)

        device.flash.mspi_init_read(args.addr)

        device.flash.mspi_start_auto_read()

        ts = datetime.datetime.now().strftime("%Y%m%d-%H%M%S")
        if args.out:
            outfilepath = args.out
        else:
            os.makedirs(".dumps", exist_ok=True)
            outfilepath = f".dumps/dump-{ts}.bin"
        f = stack.enter_context(open(outfilepath, "wb"))
        meter.restart()
        for addr in range(0, dump_length, SW_CHUNK_SIZE):
            buf = device.flash.read(SW_CHUNK_SIZE)
            if device.swire.debug:
                for chunk in chunks(buf, 32):
                    print(f"DUMP {addr:06x}: {chunk[0:16].hex()}  {chunk[16:32].hex()}")
                    addr += 32
            if args.short and all(x == 0xFF for x in buf):
                break
            f.write(buf)
            meter.add_batch(len(buf))

    print(f"[i] dumped {meter.humantotal()}")


def redeploy_fap(args):
    with closing(Swire(args)) as swire:
        swire.consume_until_timeout()
        swire.write_raw_cmd("ga7g4drb info\n")
        line = swire.readline()
        if line == "flitswire info response start":
            line = swire.readlines_until("end")
            swire.write_raw_cmd("ga7g4drb close\n")
            swire.readlines_until("ga7g4drb closing")
    run(["ufbt", "launch"])
    time.sleep(1)


def chunks(lst, n):
    """Yield successive n-sized chunks from lst."""
    for i in range(0, len(lst), n):
        yield lst[i : i + n]


class ClosingMixin:
    def __enter__(self):
        return self

    def __exit__(self, exc_type, exc, tb):
        self.close()
        if exc_type is not None or exc is not None:
            raise exc


class TlsrDevice(ClosingMixin):
    def __init__(self, args, swire=None):
        self.swire_owned = False
        if swire is None:
            swire = Swire(args)
            self.swire_owned = True
        self.args = args
        self.swire = swire
        self.cpu = TlsrCpu(self.swire)
        self.flash = TlsrFlash(self.swire, self.cpu)

    def close(self):
        if self.swire_owned:
            self.swire.close()

    def reset(self):
        self.swire.write_raw_cmd(f"reset 70 {self.args.reset_duration}\n")
        if self.swire.readline() != "ok":
            raise Exception()

    def do_sanity_check(self):
        self.swire.ping_sync()
        self.swire.transaction_write(0x00B2, b"\x7f")  # b0-b4 SWIRE
        sanity_check = self.swire.transaction_read(0x00B2, 1)  # b0-b4 SWIRE
        print(f"[i] sanity_check: {sanity_check.hex()}")
        if sanity_check != b"\x7f":
            raise Exception(f"[E] sanity_check failed {sanity_check.hex()}")

    def with_common_setup(self, stack):
        self.swire.check_programmer()
        self.swire.init(self.args.bitrate)
        self.reset()
        self.cpu.stop()
        self.do_sanity_check()
        stack.enter_context(self.cpu.with_transaction_mode(DontCare))
        stack.enter_context(self.flash.with_cs(DontCare))


class TlsrCpu:
    def __init__(self, swire):
        self.swire = swire
        self.transaction_mode = MaybeDontCareState("mem")

    def stop(self):
        self.swire.transaction_write(0x0602, b"\x05")  # CPU Stop

    def set_fifo_mode(self, value):
        self.set_transaction_mode("fifo" if value else "mem")

    def with_transaction_mode(self, mode):
        return TlsrCpuTransactionModeContext(self, mode)

    def with_fifo(self):
        return TlsrCpuTransactionModeContext(self, "fifo")

    def set_transaction_mode(self, mode):
        mode_code = None
        if mode == "fifo":
            mode_code = 0x80
        if mode == "mem":
            mode_code = 0x00
        if not self.transaction_mode.set(mode):
            return
        if mode_code is None:
            raise Exception(f"Unhandled mode setting {mode!r}")
        # swire mode, fifo, repeated reads from same address
        self.swire.transaction_write(0x00B3, mode_code)


class TlsrCpuTransactionModeContext(ClosingMixin):
    def __init__(self, cpu, mode):
        self.cpu = cpu
        self.old_mode = cpu.transaction_mode.desired
        self.new_mode = mode
        self.cpu.set_transaction_mode(self.new_mode)

    def close(self):
        self.cpu.set_transaction_mode(self.old_mode)


class TlsrFlash:
    def __init__(self, swire, cpu):
        self.swire = swire
        self.cpu = cpu
        self.cs_enabled = MaybeDontCareState(False)
        self.flash_ready_timeout = 0.5

    def mspi_set_cs(self, value):
        if self.cs_enabled.set(value):
            self.mspi_set_cs_force(value)

    def mspi_set_cs_force(self, value):
        # Chip Select
        print(f"[d] set mspi chip select {value}")
        self.mspi_send_control([b"\x01", b"\x00"][value])  # MSPI Control disable CS
        self.cs_enabled.actual = value

    def with_cs(self, cs=True):
        return TlsrFlashCsContext(self, cs)

    def mspi_send_control(self, data):
        print(f"[d] flash.mspi_send_control({data!r})")
        self.swire.transaction_write(REG_SPI_CTRL, data)

    def mspi_send_data(self, data):
        with ExitStack() as stack:
            stack.enter_context(self.with_cs())
            stack.enter_context(self.cpu.with_fifo())
            print(f"[d] mspi_send_data {data!r}")
            self.swire.transaction_write(REG_SPI_DATA, data)

    def read(self, length):
        with ExitStack() as stack:
            stack.enter_context(self.with_cs())
            stack.enter_context(self.cpu.with_fifo())
            print(f"[d] flash.read({length!r})")
            return self.swire.transaction_read(REG_SPI_DATA, length)

    def mspi_init_read(self, addr):
        self.mspi_send_fcmd_addr(TLSR_FLASH_CMD_READ, addr)

    def mspi_init_write(self, addr):
        self.mspi_send_fcmd_addr(TLSR_FLASH_CMD_WRITE, addr)

    def mspi_send_fcmd_addr(self, cmd, addr):
        with self.cpu.with_fifo():
            print(f"[d] flash.mspi_init_cmd_addr({cmd!r}, 0x{addr:x})")
            self.mspi_send_data([cmd, *self.blk_addr(addr)])
        # TODO: why not fifo send it in one transaction? seems to be working
        # self.mspi_send_data(cmd)
        # self.mspi_send_data((addr >> 16) & 0xFF)
        # self.mspi_send_data((addr >> 8) & 0xFF)
        # self.mspi_send_data((addr >> 0) & 0xFF)

    def blk_addr(self, addr):
        return bigendian(addr, 3)

    def mspi_start_auto_read(self):
        # MSPI Data, 00 to drive MSPI Clock to initiate first read
        # MSPI Control, 0a to auto read mode
        # 0a = FLD_MASTER_SPI_RD | FLD_MASTER_SPI_SDO
        FLD_MASTER_SPI_RD = 0x08  # read
        FLD_MASTER_SPI_SDO = 0x02  # auto
        FLD_MASTER_SPI_BUSY = (0x10,)
        FLD_SLAVE_SPI_BUSY = (0x40,)
        with ExitStack() as stack:
            stack.enter_context(self.cpu.with_transaction_mode("mem"))
            stack.enter_context(self.with_cs())
            self.swire.transaction_write(
                REG_SPI_DATA,
                [
                    TLSR_FLASH_CMD_INITIATE_READ,
                    FLD_MASTER_SPI_RD | FLD_MASTER_SPI_SDO,
                ],
            )  # MSPI read addr[1]

    def read_status(self):
        with self.with_cs(True):
            self.mspi_send_data(TLSR_FLASH_CMD_READ_STATUS)
            self.mspi_send_data(TLSR_FLASH_CMD_INITIATE_READ)
            return self.read(1)[0]

    def wait_ready(self, timeout=None):
        if timeout is None:
            timeout = self.flash_ready_timeout
        timeout_end = time.time() + timeout
        status = None
        quit_next = False
        while not quit_next:
            if time.time() > timeout_end:
                quit_next = True
            status = self.read_status()
            if status == 0x00:
                return
        status_str = f"{status:02x}" if isinstance(status, int) else repr(status)
        raise Exception(f"wait_flash_ready timeout, status: {status_str}")

    def _assert_cs(self):
        if not self.cs_enabled:
            raise Exception("cs needs to be enabled")


class TlsrFlashCsContext(ClosingMixin):
    def __init__(self, flash, cs):
        self.flash = flash
        self.old_cs = self.flash.cs_enabled.desired
        self.new_cs = cs
        self.flash.mspi_set_cs(self.new_cs)

    def close(self):
        self.flash.mspi_set_cs(self.old_cs)


class Swire:
    def __init__(self, args, serial=None, timeout=None):
        if serial is None:
            serial = fl_open_serial(args)
        if timeout is None:
            timeout = 5
        self.serial = serial
        self.timeout = timeout
        self.serial.timeout = timeout
        self.debug = False
        self.running = True

    def consume_until_timeout(self):
        while True:
            data = self.serial.read(1)
            if not data:
                return

    def check_programmer(self):
        self.consume_until_timeout()
        self.serial.write(b"ga7g4drb info\n")
        line = self.readline()
        if (line + " ").startswith("swire_demo welcome "):
            line = self.readline()
        if line != "flitswire info response start":
            print("[E] swire emulator not running")
            return
        self.readlines_until("end")

    def write_raw_cmd(self, data):
        if isinstance(data, str):
            data = data.encode("utf-8")
        self.serial.write(data)

    def transaction_write(self, addr, data, slave_id=None):
        if slave_id is None:
            slave_id = 0
        if isinstance(data, int):
            data = [data]
        if isinstance(data, list):
            data = bytes(data)

        self.write_raw_cmd(f"trw {addr:x} {slave_id:x} {len(data):x}\n")
        self.write_raw_cmd(data)
        resp = self.readline(echo=False)
        if resp != "ok":
            raise Exception(resp)

    def transaction_read(self, addr, readlen, slave_id=None, timeout=None):
        if slave_id is None:
            slave_id = 0
        if timeout is None:
            timeout = self.timeout
        self.write_raw_cmd(f"trr {addr:x} {slave_id:x} {readlen:x}\n")

        result = self._readdatamsg()
        if result is None:
            raise Exception("connection closed while reading")
        if len(result) != readlen:
            raise Exception("could not read enough bytes")
        status = self.readline()
        if status != "ok":
            raise Exception(f"error result from transaction_read: {status}")
        return result

    def init(self, bitrate):
        self.write_raw_cmd(f"swire_init 3 {bitrate}\n")

    def readline(self, timeout=None, echo=None):
        if timeout is None:
            timeout = self.timeout
        if echo is None:
            echo = True

        self.serial.timeout = timeout
        while self.is_open_redundant():
            line = self.serial.readline()
            if line is None:
                raise Exception("serial closed")
            if not line:
                raise Exception("serial timeout")
            line = line.decode("utf-8").rstrip("\r\n")
            if self.debug and echo:
                print(f" < {line}")
            if not line.startswith("#"):
                return line

    def close(self):
        self.running = False
        self.serial.close()

    def is_open_redundant(self):
        if not self.serial.is_open:
            print(f"[d] not not self.serial.is_open")
            return False
        if not self.running:
            print(f"[d] not self.running")
            return False
        return True

    def ping_sync(self):
        self.write_raw_cmd(b"ping\n")
        self.readlines_until("pong")

    def readlines_until(self, marker, echo=None):
        while self.is_open_redundant():
            line = self.readline(echo=echo)
            if line == marker:
                return

    def _readdatamsg(self):
        line = self.readline()
        if not line.startswith("data "):
            raise Exception("Data expected, found: {line}")
        bytecount = int(line[5:], 16)
        return self.serial.read(bytecount)

    def _read_worker(self):
        while self.is_open_redundant():
            line = self.serial.readline()
            if line is None:
                return
            if not line:
                continue
            line = line.decode("utf-8").rstrip("\r\n")
            if self.debug:
                print(f" < {line}")
            if not line.startswith("#"):
                self.log_queue.put(line)
            if line.startswith("data "):
                buffer_size = int(line[5:].strip(), 16)
                buffer = self.serial.read(buffer_size)
                self.data_queue.put(buffer)


def bigendian(number, bytecount):
    return bytes(
        ((number >> ((bytecount - i - 1) * 8)) & 0xFF) for i in range(bytecount)
    )


def fl_open_serial(args):
    print(f"[d] Listing COM ports")
    ports = serial.tools.list_ports.comports()
    flipper_port = None
    for port, desc, hwid in sorted(ports):
        print(f"     - {port}: {desc} [{hwid}]")
        if "FLIP_" in hwid:
            flipper_port = port

    print(f"[d] opening {flipper_port}")
    flipper = serial.Serial(flipper_port, baudrate=args.baud, timeout=1)
    return flipper


class BandwidthCounter:
    def __init__(self):
        self.window = 1
        self.restart()

    def restart(self):
        self.count_window = 0
        self.count_total = 0
        self.start = time.time()
        self.pivot = self.start + self.window
        self.first_print = True

    def add_batch(self, size):
        self.count_window += size
        self.count_total += size
        if time.time() > self.pivot:
            self.pivot += self.window
            if self.first_print:
                self.first_print = False
            print(f"[d] Read speed: {humanbytes(self.count_window / self.window)}/s")
            self.count_window = 0

    def humantotal(self):
        return humanbytes(self.count_total)

    def print_report(self, msg):
        span = time.time() - self.pivot
        print(f"[i] {msg} {self.humantotal()} in {span:03f} s")


def humanbytes(x):
    """Return the given bytes as a human friendly KB, MB, GB, or TB string."""
    KB = float(1024)
    MB = float(KB**2)  # 1,048,576
    GB = float(KB**3)  # 1,073,741,824
    TB = float(KB**4)  # 1,099,511,627,776

    if x < KB:
        return f"{x} B"
    if x < MB:
        return f"{x / KB:.2f} kB"
    if x < GB:
        return f"{x / MB:.2f} MB"
    if x < TB:
        return f"{x / GB:.2f} GB"
    return f"{x / TB:.2f} TB"


class MaybeDontCareState:
    def __init__(self, value):
        self.actual = value
        self.desired = value

    def set(self, value):
        self.desired = value
        if value is DontCare or self.actual == value:
            return False
        self.actual = value
        return True


class DontCareType:
    def __repr__(self):
        return "DontCare"

    def __str__(self):
        return "DontCare"


DontCare = DontCareType()


if __name__ == "__main__":
    main()
