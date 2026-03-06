from contextlib import closing
import datetime
import os
import time
import serial
import serial.tools.list_ports
from subprocess import check_call as run
import argparse
from contextlib import ExitStack
from dataclasses import dataclass
import threading
import queue
import concurrent.futures
from collections import deque
import sys

SW_MAX_IMAGE_LEN = 512 * 1024

TLSR_DUMP_CHUNK_SIZE = 4
TLSR_FLASH_CHUNK_SIZE = 32
TLSR_FLASH_PAGE_SIZE = 256

TLSR_FLASH_SECTOR_SIZE = 4096
TLSR_FLASH_CMD_INITIATE_READ = 0x00
TLSR_FLASH_CMD_WRITE = 0x02
TLSR_FLASH_CMD_READ = 0x03
TLSR_FLASH_CMD_WRITE_DISABLE = 0x04
TLSR_FLASH_CMD_READ_STATUS = 0x05
TLSR_FLASH_CMD_WRITE_ENABLE = 0x06
TLSR_FLASH_CMD_ERASE_SECTOR = 0x20  # 4kB
TLSR_FLASH_CMD_ERASE_CHIP = 0x60
TLSR_FLASH_CMD_GET_JEDEC_ID = 0x9F
TLSR_FLASH_CMD_POWER_DOWN = 0xB9
TLSR_FLASH_CMD_ERASE_BLOCK = 0xD8  # 64kB

TLSR_FLASH_FLD_MASTER_SPI_RD = 0x08  # read
TLSR_FLASH_FLD_MASTER_SPI_SDO = 0x02  # auto
TLSR_FLASH_FLD_MASTER_AUTO_READ = (
    TLSR_FLASH_FLD_MASTER_SPI_RD | TLSR_FLASH_FLD_MASTER_SPI_SDO
)

TLSR_FLASH_FLD_MASTER_SPI_BUSY = 0x10
TLSR_FLASH_FLD_SLAVE_SPI_BUSY = 0x40


REG_SPI_DATA = 0x000C
REG_SPI_CTRL = 0x000D


def main(args=None):
    if args is None:
        args = build_argparse().parse_args()

    if args.redeploy_fap:
        redeploy_fap(args)
    if args.dump:
        dump(args)
    if args.erase_sector is not None:
        erase(args, args.erase_sector, cmd=TLSR_FLASH_CMD_ERASE_SECTOR)
    if args.erase_block is not None:
        erase(args, args.erase_block, cmd=TLSR_FLASH_CMD_ERASE_BLOCK)
    if args.flash is not None:
        flash(args)


def build_argparse():
    parser = argparse.ArgumentParser()
    parser.add_argument(
        "--redeploy-fap", action=argparse.BooleanOptionalAction, default=False
    )
    parser.add_argument("--dump", action="store_true", default=False)
    parser.add_argument("--short", action=argparse.BooleanOptionalAction, default=True)
    parser.add_argument("--blink", action=argparse.BooleanOptionalAction, default=True)
    parser.add_argument("--out", type=str, default=None)
    parser.add_argument("--flash", type=str, default=None)
    parser.add_argument("--erase-sector", type=int_literal, default=None)
    parser.add_argument("--erase-block", type=int_literal, default=None)
    parser.add_argument("--length", type=int_literal, default=0)
    parser.add_argument("--chunksize", type=int_literal, default=256)
    parser.add_argument("--chunkcount", type=int_literal, default=1)
    parser.add_argument("--addr", type=int_literal, default=0)
    parser.add_argument("--bitrate", type=int_literal, default=150000)
    parser.add_argument("--baud", type=int_literal, default=115200)
    parser.add_argument("--reset-duration", type=int_literal, default=500)
    parser.add_argument("--debug", action="store_true", default=False)
    return parser


def int_literal(s):
    if s.startswith("0x") or s.startswith("0X"):
        return int(s, 16)
    if s.startswith("0b") or s.startswith("0B"):
        return int(s, 2)
    if s.startswith("0o") or s.startswith("0O"):
        return int(s, 8)
    return int(s, 10)


def erase(args, addr, cmd):
    with ExitStack() as stack:
        device = stack.enter_context(TlsrDevice(args))
        device.apply_common_setup(stack)
        device.flash.erase(addr=addr, cmd=cmd).result()


def flash(args):
    flash_file_size = args.length
    if not flash_file_size:
        flash_file_size = SW_MAX_IMAGE_LEN
    meter = BandwidthCounter()
    addr = args.addr
    erased_end = 0
    if (addr % TLSR_FLASH_SECTOR_SIZE) != 0:
        raise Exception(f"Start addr must be multiple of 0x{TLSR_FLASH_SECTOR_SIZE:x}")
    flash_path = args.flash
    flash_start = addr
    flash_file_size = min(flash_file_size, os.path.getsize(flash_path))
    flash_end = addr + flash_file_size
    with ExitStack() as stack:
        device = stack.enter_context(TlsrDevice(args))

        device.apply_common_setup(stack)

        flash_file = stack.enter_context(open(flash_path, "rb"))

        meter.restart()

        def flasher_generator():
            nonlocal device, addr, erased_end, meter, flash_start, flash_end, flash_file

            while addr < flash_end:
                sector_addr, sector_end = get_enclosing_block(
                    addr, TLSR_FLASH_SECTOR_SIZE
                )
                _, page_end = get_enclosing_block(addr, TLSR_FLASH_PAGE_SIZE)
                if addr >= erased_end:
                    if addr != sector_addr:
                        print(
                            f"[W] address misaligned from sector for erase {addr} != {sector_addr}"
                        )
                    print(f"[i] erasing sector 0x{sector_addr:06x}")
                    device.flash.erase_sector(sector_addr)
                    erased_end = sector_end

                chunk_size = min(page_end, flash_end) - addr
                chunk_buf = flash_file.read(chunk_size)
                assert len(chunk_buf) == chunk_size
                device.flash.write(addr, chunk_buf)
                yield device.swire.consume_and_checkpoint()
                addr += chunk_size
                meter.add_batch(len(chunk_buf), "flash speed")

        with closing(flasher_generator()) as _iter1:
            with closing(run_multiplexed(_iter1)) as _iter2:
                for _ in _iter2:
                    pass

    print(f"[i] flashed {meter.humantotal_bytes()}")


def do_blinking(args):
    with ExitStack() as stack:
        device = stack.enter_context(TlsrDevice(args))
        device.apply_common_setup(stack)
        device.maybe_blinking_led1()


def get_enclosing_block(x, block_size):
    start = (x // block_size) * block_size
    return start, start + block_size


def dump(args):
    dump_length = args.length
    if not dump_length:
        dump_length = SW_MAX_IMAGE_LEN
    meter = BandwidthCounter()
    with ExitStack() as stack:
        device = stack.enter_context(TlsrDevice(args))

        device.apply_common_setup(stack)

        ts = datetime.datetime.now().strftime("%Y%m%d-%H%M%S")
        if args.out:
            outfilepath = args.out
        else:
            os.makedirs(".dumps", exist_ok=True)
            outfilepath = f".dumps/dump-{ts}.bin"
        f = stack.enter_context(open(outfilepath, "wb"))
        fmeta = stack.enter_context(open(outfilepath + ".meta", "w"))
        fmeta.write(f"dump timestamp: {ts}\n")
        fmeta.write(f"args: {sys.argv}\n")
        stack.enter_context(ConfigTimeoutContext(device.swire, 5))
        meter.restart()

        def generate_futures():
            for addr in range(0, dump_length, args.chunksize * args.chunkcount):
                with closing(
                    device.flash.read_chunks(addr, args.chunksize, args.chunkcount)
                ) as l_iter:
                    for data_future in l_iter:

                        def _defer(addr, data_future, checkpoint_future):
                            def tail():
                                checkpoint_future.result()
                                return addr, data_future.result()

                            return LazyValue(tail)

                        checkpoint_future = device.swire.consume_and_checkpoint()
                        yield _defer(addr, data_future, checkpoint_future)

        only_ff_from_addr = 0
        for addr, data in run_multiplexed(generate_futures()):
            if not (args.short and all(b == 0xFF for b in data)):
                only_ff_from_addr = addr + len(data)
            if only_ff_from_addr + 256 <= addr:
                print(f"short end reached at {addr}")
                break
            f.write(data)
            meter.add_batch(len(data), "dump speed")

        avgspeed = meter.humanavg_bits()
        fmeta.write(f"dumped: {meter.humantotal_bytes()}\n")
        fmeta.write(f"average speed: {avgspeed}\n")
        print(f"[i] dumped: {meter.humantotal_bytes()}")
        print(f"[i] average speed: {avgspeed}")


def run_multiplexed(gen_futures, parallelism=2):
    l_futures = deque()
    for f in gen_futures:
        l_futures.append(f)
        while len(l_futures) >= parallelism:
            yield l_futures.popleft().result()
    while len(l_futures) > 0:
        yield l_futures.popleft().result()


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
        self.cpu = TlsrCpu825x(self.swire)
        self.flash = TlsrFlash(self)

    def close(self):
        if self.swire_owned:
            self.swire.close()

    def reset(self, reset_delay_ms=70):
        self.swire.write_raw_cmd(f"reset {reset_delay_ms} {self.args.reset_duration}\n")
        return self.swire._defer_ok("device.reset")

    def sleep_us(self, us):
        self.swire.write_raw_cmd(f"slus {us}\n")
        return self.swire._defer_ok("device.sleep_us")

    def do_sanity_check(self):
        self.swire.ping_sync("sanity_check")
        swire_clk_div = self.cpu.register(TlsrCpu825x.Regs.SWIRE_CLK_DIV)

        swire_clk_div.write(0x7F)

        sanity_check = swire_clk_div.read().result()
        print(f"[i] sanity_check 0x7f: {sanity_check:02x}")
        if sanity_check != 0x7F:
            raise Exception(f"[E] sanity_check failed {sanity_check:02x}")

    def do_set_swire_clock(self):
        swire_clk_div = self.cpu.register(TlsrCpu825x.Regs.SWIRE_CLK_DIV)
        swire_clk_div_value = int(round(9500000 / self.args.bitrate))
        print(f"[i] set swire clock divider to: {swire_clk_div_value:02x}")
        swire_clk_div.write(swire_clk_div_value)
        sanity_check = swire_clk_div.read().result()
        if sanity_check != swire_clk_div_value:
            raise Exception(
                f"[E] failed to set slave swire clock divider {sanity_check:02x}"
            )

    def apply_common_setup(self, stack):
        self.swire.check_programmer()
        self.swire.peek_wait()
        self.swire.init(self.args.bitrate)
        self.reset()
        self.cpu.stop()
        self.swire.peek_wait()
        if self.args.blink:
            self.maybe_blinking_led1().result()
        self.do_sanity_check()
        self.do_set_swire_clock()
        stack.enter_context(self.cpu.with_transaction_mode(DontCare))
        stack.enter_context(self.flash.with_cs(DontCare))
        cpu_metadata = self.cpu.read_cpu_metadata().result()
        print(f"[i] {cpu_metadata}")

    def maybe_blinking_led1(self, blink_count=None, period=None):
        if blink_count is None:
            blink_count = 1
        if period is None:
            period = 1
        # Configure C3 GPIO pin for LED1
        self.swire.transaction_write(
            0xC0, b"\x00"
        )  # IEN Input Enable, active high, disable
        self.swire.transaction_write(0x0596, b"\x08")  # GPIO Enable, active high
        self.swire.transaction_write(
            0xC2, b"\xff"
        )  # DS Drive Strength, active high, strong
        self.swire.transaction_write(
            0x0592, b"\xf7"
        )  # OEN Output Enable, active low, enable
        for i in range(blink_count):
            time.sleep(period / 2)
            self.swire.transaction_write(0x0593, b"\x08")  # Output Led Off
            time.sleep(period / 2)
            self.swire.transaction_write(0x0593, b"\x00")  # Output Led On
            if self.args.debug:
                print(f"[d] Try blinking led {i}")
        return self.swire.consume_and_checkpoint()


@dataclass(frozen=True)
class RegisterMetadata:
    bytecount: int
    address: int


class TlsrCpu825x:
    class Regs:
        VER_ID = RegisterMetadata(bytecount=1, address=0x7D)
        PROG_ID = RegisterMetadata(bytecount=2, address=0x7E)
        SWIRE_CLK_DIV = RegisterMetadata(bytecount=1, address=0xB2)  # b0-b4 SWIRE

    def __init__(self, swire):
        self.swire = swire
        self.transaction_mode = MaybeDontCareState("mem")

    def register(self, regmeta):
        return BoundRegister(self.swire, regmeta)

    def read_cpu_metadata(self):
        ver_id = self.register(TlsrCpu825x.Regs.VER_ID).read()
        prog_id = self.register(TlsrCpu825x.Regs.PROG_ID).read()
        swire_clk_div = self.register(TlsrCpu825x.Regs.SWIRE_CLK_DIV).read()

        def tail():
            return CpuMetadata(
                ver_id=ver_id.result(),
                prog_id=prog_id.result(),
                swire_clk_div=swire_clk_div.result(),
            )

        return LazyValue(tail)

    def stop(self):
        return self.swire.transaction_write(0x0602, b"\x05")  # CPU Stop

    def set_fifo_mode(self, value):
        return self.set_transaction_mode("fifo" if value else "mem")

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
    cpu: TlsrCpu825x
    device: TlsrDevice
    swire: Swire

    def __init__(self, device):
        self.device = device
        self.swire = device.swire
        self.cpu = device.cpu
        self.cs_enabled = MaybeDontCareState(False)
        self.flash_ready_timeout = 0.5

    def read(self, addr, bytecount):
        with closing(self.read_chunks(addr, bytecount)) as l_iter:
            return next(l_iter)

    def read_chunks(self, addr, chunksize, chunkcount=1):
        with self.cpu.with_fifo():
            self.mspi_set_cs(False)
            self.device.sleep_us(1)
            self.mspi_set_cs(True)
            self.mspi_send_data(
                [
                    TLSR_FLASH_CMD_READ,
                    *self.blk_addr(addr),
                    0x00,  # dummy byte to initiate read clock
                ]
            )
            self.mspi_send_control(TLSR_FLASH_FLD_MASTER_AUTO_READ)
            self.swire.transaction_read_start(REG_SPI_DATA)
            try:
                for _ in range(chunkcount):
                    yield self.swire.transaction_read_block(chunksize)
            finally:
                self.swire.transaction_end()

                # Transaction END signal initiates one more byte of read from the
                # MSPI Data register, so it is not safe to start a new transaction
                # without repositioning the flash cursor
                self.mspi_set_cs(False)

    def _flash_send_cmd(self, cmd, gap_us=None):
        self.mspi_set_cs(False)
        self.device.sleep_us(1)
        with self.cpu.with_fifo():
            self.mspi_set_cs(True)
            self.mspi_send_data(cmd, gap_us=gap_us)

    def write_by_byte(self, addr, data):
        with self.cpu.with_fifo():
            self._flash_send_cmd([TLSR_FLASH_CMD_WRITE_ENABLE])
            self._flash_send_cmd(
                [
                    TLSR_FLASH_CMD_WRITE,
                    *self.blk_addr(addr),
                ]
            )
            for i in range(len(data)):
                self.mspi_send_data([data[i]])
                self.swire.write_raw_cmd("wmspi\n")
            self._wait_flash_ready_and_disable_cs()
            return self.swire.consume_and_checkpoint()

    def _wait_flash_ready_and_disable_cs(self):
        self.swire.write_raw_cmd("wfr\n")

        def tail():
            line = self.swire.readline()
            if self.swire.debug:
                print(f"[d] _wait_flash_ready_and_disable_cs result: {line}")
            if not (line + " ").startswith("ok "):
                print(f"[e] error _wait_flash_ready_and_disable_cs: {line}")
                raise Exception(f"error _wait_flash_ready_and_disable_cs: {line}")

        return self.swire.defer(tail)

    def write(self, addr, data):
        return self._flash_mspi_write(cmd=TLSR_FLASH_CMD_WRITE, addr=addr, data=data)

    def erase_sector(self, addr):
        return self._flash_mspi_write(
            cmd=TLSR_FLASH_CMD_ERASE_SECTOR, addr=addr, data=None
        )

    def erase_block(self, addr):
        return self._flash_mspi_write(
            cmd=TLSR_FLASH_CMD_ERASE_BLOCK, addr=addr, data=None
        )

    def _flash_mspi_write(self, cmd, addr, data):
        with self.cpu.with_fifo():
            self._flash_send_cmd([TLSR_FLASH_CMD_WRITE_ENABLE])
            self._flash_send_cmd(
                [
                    cmd,
                    *([] if addr is None else self.blk_addr(addr)),
                    *([] if data is None else data),
                ],
                gap_us=10,
            )
            return self._wait_flash_ready_and_disable_cs()

    def mspi_set_cs(self, value):
        if self.cs_enabled.set(value):
            self._mspi_set_cs_force(value)

    def _mspi_set_cs_force(self, value):
        # Chip Select
        if self.swire.debug:
            print(f"[d] set mspi chip select {value}")
        self.mspi_send_control([b"\x01", b"\x00"][value])  # MSPI Control disable CS
        self.cs_enabled.actual = value

    def with_cs(self, cs=True):
        return TlsrFlashCsContext(self, cs)

    def mspi_send_control(self, data):
        if self.swire.debug:
            print(f"[d] flash.mspi_send_control({data!r})")
        self.swire.transaction_write(REG_SPI_CTRL, data)

    def mspi_send_data(self, data, gap_us=None):
        with ExitStack() as stack:
            stack.enter_context(self.cpu.with_fifo())
            if self.swire.debug:
                print(f"[d] flash.mspi_send_data({data!r})")
            self.swire.transaction_write(REG_SPI_DATA, data, gap_us=gap_us)

    def mspi_init_read(self, addr):
        self.mspi_send_fcmd_addr(TLSR_FLASH_CMD_READ, addr)

    def mspi_init_write(self, addr):
        self.mspi_send_fcmd_addr(TLSR_FLASH_CMD_WRITE, addr)

    def mspi_send_fcmd_addr(self, cmd, addr):
        with self.cpu.with_fifo():
            if self.swire.debug:
                print(f"[d] flash.mspi_init_cmd_addr({cmd!r}, 0x{addr:x})")
            self.mspi_send_data([cmd, *self.blk_addr(addr)])
        # TODO: why not fifo send it in one transaction? seems to be working
        # self.mspi_send_data(cmd)
        # self.mspi_send_data((addr >> 16) & 0xFF)
        # self.mspi_send_data((addr >> 8) & 0xFF)
        # self.mspi_send_data((addr >> 0) & 0xFF)

    def blk_addr(self, addr):
        return bigendian_encode(addr, 3)

    def mspi_start_auto_read(self):
        # MSPI Data, 00 to drive MSPI Clock to initiate first read
        # MSPI Control, 0a to auto read mode
        # 0a = FLD_MASTER_SPI_RD | FLD_MASTER_SPI_SDO
        with ExitStack() as stack:
            stack.enter_context(self.cpu.with_transaction_mode("mem"))
            stack.enter_context(self.with_cs())
            self.swire.transaction_write(
                REG_SPI_DATA,
                [
                    TLSR_FLASH_CMD_INITIATE_READ,
                    TLSR_FLASH_FLD_MASTER_AUTO_READ,
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
    def __init__(self, args, serial=None, timeout=None, slave_id=None):
        if serial is None:
            serial = fl_open_serial(args)
        if timeout is None:
            timeout = 5
        if slave_id is None:
            slave_id = 0
        self.serial = serial
        self.timeout = timeout
        self.serial.timeout = timeout
        self.serial.write_timeout = timeout
        self.debug = args.debug
        self.running = True
        self.slave_id = slave_id
        self.response_executor = BlockingThreadPoolExecutor(max_workers=1, queue_size=4)
        self.last_future = None
        self._defer_depth = 0

    def peek_wait(self, keep=0):
        # todo: deprecated
        f = self.last_future
        while f is not None and keep > 0:
            f = f.history
            keep -= 1
        result = None
        if f is not None:
            result = f.result()
        return result

    def consume_and_checkpoint(self):
        # todo: deprecated
        return self.last_future

    def defer(self, fn):
        future = self.response_executor.submit(fn)
        self.last_future = ChainedFuture(future, self.last_future)
        return self.last_future

    def _defer_ok(self, error_prefix, expected_result=None):
        if expected_result is None:
            expected_result = "ok"

        def tail():
            resp = self.readline()
            if resp != expected_result:
                raise Exception(f"{error_prefix}: {resp}")

        return self.defer(tail)

    def consume_until_timeout(self):
        leftovers = 0
        with ConfigTimeoutContext(self.serial, 0.25):
            while True:
                data = self.serial.read(1024)
                if not data:
                    break
                leftovers += len(data)
        if self.debug:
            print(f"[d] Leftover bytes in input usb buffer: {leftovers}")

    def check_programmer(self):
        self.consume_until_timeout()
        self.write_raw_cmd(b"ga7g4drb info\n")

        def tail():
            line = self.readline()
            if (line + " ").startswith("swire_demo welcome "):
                line = self.readline()
            if line != "flitswire info response start":
                raise Exception(f"swire emulator not running, got {line}")
            self.readlines_until("end")

        return self.defer(tail)

    def write_raw_cmd(self, data, comment=None):
        if self.debug:
            if comment is None:
                print(f" >  {data!r}")
            else:
                print(f" >  {data!r} comment={comment!r}")
        self._write_raw(data)

    def write_raw_data(self, data):
        if self.debug:
            print(f" >  ::bytes len(data)={len(data)}")
        self._write_raw(data)

    def _write_raw(self, data):
        if isinstance(data, str):
            data = data.encode("utf-8")
        written = self.serial.write(data)
        # print(f"[d] Writtten {written}/{len(data)}")
        if len(data) != written:
            raise Exception(f"Write error {written}/{len(data)}")

    def transaction_write(self, addr, data, slave_id=None, gap_us=None):
        if slave_id is None:
            slave_id = self.slave_id
        if gap_us is None:
            gap_us = 0
        if isinstance(data, int):
            data = [data]
        if isinstance(data, list):
            data = bytes(data)

        self.write_raw_cmd(f"trw {addr:x} {slave_id:x} {len(data):x} {gap_us}\n")
        self.write_raw_data(data)

        return self._defer_ok("error in transaction_write")

    def transaction_read(self, addr, readlen, slave_id=None, timeout=None):
        if slave_id is None:
            slave_id = self.slave_id
        if timeout is None:
            timeout = self.timeout
        self.write_raw_cmd(f"trr {addr:x} {slave_id:x} {readlen:x}\n")

        def tail():
            result = self._readdatamsg()
            if result is None:
                raise Exception("connection closed while reading")
            if len(result) != readlen:
                raise Exception("could not read enough bytes")
            status = self.readline()
            if status != "ok":
                raise Exception(f"error result from transaction_read: {status}")
            return result

        return self.defer(tail)

    def transaction_read_start(self, addr, slave_id=None):
        return self._transaction_start(
            addr=addr,
            wr=1,
            slave_id=slave_id,
            tag="transaction_read_start",
        )

    def transaction_read_block(self, block_size):
        self.write_raw_cmd(f"br {block_size:x}\n")

        def tail():
            resp = self.readline()
            if resp != "ok":
                raise Exception(f"transaction_read_block: error {resp}")
            data = self._readdatamsg()
            if len(data) != block_size:
                raise Exception(
                    f"transaction_read_block: length error, len(data) {len(data)} != block_size {block_size}"
                )
            return data

        return self.defer(tail)

    def transaction_write_start(self, addr, slave_id=None):
        return self._transaction_start(
            addr=addr,
            wr=0,
            slave_id=slave_id,
            tag="transaction_write_start",
        )

    def transaction_write_block(self, block):
        self.write_raw_cmd(f"bw {len(block):x}\n")
        self.write_raw_data(block)
        return self._defer_ok("transaction_write_block")

    def transaction_end(self):
        self.write_raw_cmd(f"tre\n")
        return self._defer_ok("transaction_end")

    def _transaction_start(self, addr, wr, tag, slave_id=None):
        if slave_id is None:
            slave_id = self.slave_id
        self.write_raw_cmd(f"trs {addr:x} {wr} {slave_id:x}\n")
        return self._defer_ok(tag)

    def init(self, bitrate):
        # assert 75000 <= bitrate <= 1000000
        assert 30000 <= bitrate <= 1000000
        self.write_raw_cmd(f"swire_init 3 {bitrate}\n")
        return self._defer_ok("error in swire_init")

    def readline(self, echo=None):
        if echo is None:
            echo = True

        while self.is_open_redundant():
            line = self.serial.readline()
            if line is None:
                raise Exception("serial closed")
            if not line:
                raise Exception("serial timeout")
            line = line.decode("utf-8").rstrip("\r\n")
            if self.debug and echo:
                print(f"  < {line}")
            if not line.startswith("#"):
                return line

    def close(self):
        self.response_executor.shutdown(wait=True)
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

    def ping_sync(self, comment=None):
        self.write_raw_cmd(b"ping\n", comment=comment)

        def tail():
            resp = self.readline()
            if resp != "pong":
                raise Exception(f"ping expected pong, got: {resp}")

        return self.defer(tail)

    def readlines_until(self, marker, echo=None):
        while self.is_open_redundant():
            line = self.readline(echo=echo)
            if line == marker:
                return

    def _readdatamsg(self):
        line = self.readline()
        if not line.startswith("data "):
            raise Exception(f"Data expected, found: {line}")
        bytecount = int(line[5:], 16)
        result = self.serial.read(bytecount)
        assert len(result) == bytecount
        return result


@dataclass()
class CpuMetadata:
    ver_id: int
    prog_id: int
    swire_clk_div: int


class LazyValue:
    def __init__(self, fn):
        self._fn = fn
        self._state = None
        self._lock = threading.Lock()
        self._value = None

    def _calc(self):
        if self._state is None:
            with self._lock:
                if self._state is None:
                    try:
                        result = self._fn()
                    except Exception as ex:
                        self._value = ex
                        self._state = "error"
                        return
                    self._value = result
                    self._state = "result"

    def result(self):
        self._calc()
        if self._state == "error":
            raise self._value
        if self._state == "result":
            return self._value
        raise Exception("wrong future")


class MapFuture(LazyValue):
    def __init__(self, fn, future):
        def fn2():
            return fn(future.result())

        super().__init__(fn2)


class CheckpointFuture:
    def __init__(self, futures):
        self.futures = futures

    def result(self):
        for f in self.futures:
            f.result()


class ChainedFuture:
    def __init__(self, target, history):
        self.target = target
        self.history = history

    def result(self):
        if self.history is not None:
            self.history.result()  # to throw exceptions
            self.history = None
        return self.target.result()


class BoundRegister:
    def __init__(self, swire, regmeta):
        self.swire = swire
        self.regmeta = regmeta

    def read(self):
        future = self.swire.transaction_read(
            self.regmeta.address, self.regmeta.bytecount
        )
        return MapFuture(bigendian_decode, future)

    def write(self, value: int):
        assert 0 <= value < (1 << (8 * self.regmeta.bytecount))
        return self.swire.transaction_write(
            self.regmeta.address, bigendian_encode(value, self.regmeta.bytecount)
        )


def bigendian_encode(number, bytecount):
    return bytes(
        ((number >> ((bytecount - i - 1) * 8)) & 0xFF) for i in range(bytecount)
    )


def bigendian_decode(buf):
    result = 0
    for byte in buf:
        result = (result << 8) | byte
    return result


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

    def add_batch(self, size, message=None):
        self.count_window += size
        self.count_total += size
        if time.time() > self.pivot:
            self.pivot += self.window
            if self.first_print:
                self.first_print = False
            if message is not None:
                print(
                    f"[i] {message}: {humanbits(8 * self.count_window / self.window)}/s"
                )
            self.count_window = 0

    def humantotal_bytes(self):
        return humanbytes(self.count_total)

    def humanavg_bytes(self):
        return humanbytes(self.count_total / (time.time() - self.start)) + "/s"

    def humanavg_bits(self):
        return humanbits(8 * self.count_total / (time.time() - self.start)) + "/s"

    def print_report(self, msg):
        span = time.time() - self.pivot
        print(f"[i] {msg} {self.humantotal_bytes()} in {span:03f} s")


class ConfigTimeoutContext(ClosingMixin):
    def __init__(self, target, timeout):
        self.target = target
        self.new_timeout = timeout
        self.old_timeout = target.timeout
        self.target.timeout = self.new_timeout

    def close(self):
        self.target.timeout = self.old_timeout


class Box:
    def __init__(self, value):
        self.value = value


def humanbytes(x_bytes):
    """Return the given bytes as a human friendly KB, MB, GB, or TB string."""
    KB = float(1024)
    MB = float(KB**2)  # 1,048,576
    GB = float(KB**3)  # 1,073,741,824
    TB = float(KB**4)  # 1,099,511,627,776

    if x_bytes < KB:
        return f"{x_bytes} B"
    if x_bytes < MB:
        return f"{x_bytes / KB:.2f} kB"
    if x_bytes < GB:
        return f"{x_bytes / MB:.2f} MB"
    if x_bytes < TB:
        return f"{x_bytes / GB:.2f} GB"
    return f"{x_bytes / TB:.2f} TB"


def humanbits(x_bits):
    """Return the given bytes as a human friendly KB, MB, GB, or TB string."""
    KB = float(1024)
    MB = float(KB**2)  # 1,048,576
    GB = float(KB**3)  # 1,073,741,824
    TB = float(KB**4)  # 1,099,511,627,776

    if x_bits < KB:
        return f"{x_bits} b"
    if x_bits < MB:
        return f"{x_bits / KB:.2f} kb"
    if x_bits < GB:
        return f"{x_bits / MB:.2f} Mb"
    if x_bits < TB:
        return f"{x_bits / GB:.2f} Gb"
    return f"{x_bits / TB:.2f} Tb"


class BlockingThreadPoolExecutor(concurrent.futures.ThreadPoolExecutor):
    def __init__(self, *, queue_size=0, **kwargs):
        super().__init__(**kwargs)
        self._work_queue = queue.Queue(maxsize=queue_size)


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
