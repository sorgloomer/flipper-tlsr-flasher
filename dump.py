from contextlib import closing
import datetime
import os
import time
import serial
import serial.tools.list_ports
from subprocess import check_call as run
import argparse


SW_MAX_IMAGE_LEN = 512 * 1024
SW_CHUNK_SIZE = 256
TLSR_FLASH_CMD_WRITE = 0x02
TLSR_FLASH_CMD_READ = 0x03
TLSR_FLASH_CMD_WRITE_ENABLE = 0x06


def main(args=None):
    if args is None:
        args = build_argparse().parse_args()

    if args.fap:
        redeploy_fap(args)
    if args.dump:
        dump(args)
    if args.flash:
        flash(args)


def build_argparse():
    parser = argparse.ArgumentParser()
    parser.add_argument("--fap", action=argparse.BooleanOptionalAction, default=False)
    parser.add_argument("--dump", action=argparse.BooleanOptionalAction, default=False)
    parser.add_argument("--short", action=argparse.BooleanOptionalAction, default=True)
    parser.add_argument("--flash", type=str, default=None)
    parser.add_argument("--length", type=int, default=0)
    parser.add_argument("--addr", type=int, default=0)
    parser.add_argument("--bitrate", type=int, default=75000)
    parser.add_argument("--baud", type=int, default=115200)
    return parser


def flash(args):
    dump_length = args.length
    if not dump_length:
        dump_length = SW_MAX_IMAGE_LEN
    with closing(Swire(args)) as swire:
        swire.check_programmer()

        swire.write_raw_cmd(f"swire_init 3 {args.bitrate}\n")
        swire.write_raw_cmd(f"reset 70 1000\n")
        if swire.readline() != "ok":
            raise Exception()

        swire.cmd_cpu_stop()
        swire.cmd_write(0x00B2, b"\x7f")  # b0-b4 SWIRE
        do_sanity_check(swire)
        swire.cmd_mspi_set_cs_enable(True)

        swire.cmd_mspi_send_data(TLSR_FLASH_CMD_WRITE_ENABLE)
        swire.cmd_mspi_init_write(args.addr)
        swire.cmd_set_fifo_mode(True)

        meter = BandwidthCounter()

        dump_length = min(dump_length, os.path.getsize(args.flash))
        with open(args.flash, "rb") as f:
            addr = 0
            while addr < dump_length:
                chunksize = min(SW_CHUNK_SIZE, dump_length - addr)
                buf = f.read(chunksize)
                print(f"Writing {len(buf):x}/{chunksize:x}/{dump_length:x} to flash")
                swire.cmd_mspi_send_data(buf)
                addr += chunksize
                meter.add_batch(len(buf))

        swire.cmd_set_fifo_mode(False)
        swire.cmd_mspi_set_cs_enable(False)


def dump(args):
    dump_length = args.length
    if not dump_length:
        dump_length = SW_MAX_IMAGE_LEN
    with closing(Swire(args)) as swire:
        swire.check_programmer()

        swire.write_raw_cmd(f"swire_init 3 {args.bitrate}\n")
        swire.write_raw_cmd(f"reset 70 1000\n")
        if swire.readline() != "ok":
            raise Exception()

        swire.cmd_cpu_stop()
        swire.cmd_write(0x00B2, b"\x7f")  # b0-b4 SWIRE
        do_sanity_check(swire)

        swire.cmd_mspi_set_cs_enable(True)

        swire.cmd_mspi_init_read(args.addr)

        # MSPI Data, 00 to drive MSPI Clock to initiate first read
        # MSPI Control, 0a to auto read mode
        # 0a = FLD_MASTER_SPI_RD | FLD_MASTER_SPI_SDO
        swire.cmd_write(0x000C, bytes.fromhex("000a"))  # MSPI read addr[1]

        swire.cmd_set_fifo_mode(True)

        ts = datetime.datetime.now().strftime("%Y%m%d-%H%M%S")
        meter = BandwidthCounter()
        os.makedirs(".dumps", exist_ok=True)
        with open(f".dumps/dump-{ts}.bin", "wb") as f:
            for addr in range(0, dump_length, SW_CHUNK_SIZE):
                buf = swire.cmd_read(0x000C, SW_CHUNK_SIZE)
                if swire.debug:
                    for chunk in chunks(buf, 32):
                        print(
                            f"DUMP {addr:06x}: {chunk[0:16].hex()}  {chunk[16:32].hex()}"
                        )
                        addr += 32
                if args.short and all(x == 0xFF for x in buf):
                    break
                f.write(buf)
                meter.add_batch(len(buf))

        swire.cmd_set_fifo_mode(False)
        swire.cmd_mspi_set_cs_enable(False)


def do_sanity_check(swire):
    swire.ping_sync()
    sanity_check = swire.cmd_read(0x00B2, 1)  # b0-b4 SWIRE
    print(f"sanity_check: {sanity_check.hex()}")
    if sanity_check != b"\x7f":
        raise Exception(f"[E] sanity_check failed {sanity_check.hex()}")


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

    def cmd_write(self, addr, data, slave_id=None):
        if slave_id is None:
            slave_id = 0
        if isinstance(data, int):
            data = bytes([data])

        self.write_raw_cmd(f"trw {addr:x} {slave_id:x} {len(data):x}\n")
        self.write_raw_cmd(data)
        resp = self.readline(echo=False)
        if resp != "ok":
            raise Exception(resp)

    def cmd_cpu_stop(self):
        self.cmd_write(0x0602, b"\x05")  # CPU Stop

    def cmd_set_fifo_mode(self, value):
        # swire mode, fifo, repeated reads from same address
        self.cmd_write(0x00B3, [b"\x00", b"\x80"][value])

    def cmd_mspi_set_cs_enable(self, value):
        # MSPI = Memory SPI
        # CS = Chip Select
        # MSPI Control, CS bit active low
        self.cmd_mspi_send_control([b"\x01", b"\x00"][value])  # MSPI Control disable CS

    def cmd_mspi_send_control(self, data):
        self.cmd_write(0x000D, data)

    def cmd_mspi_send_data(self, data):
        self.cmd_write(0x000C, data)

    def cmd_mspi_init_read(self, addr):
        self.cmd_mspi_init_cmd_addr(TLSR_FLASH_CMD_READ, addr)

    def cmd_mspi_init_write(self, addr):
        self.cmd_mspi_init_cmd_addr(TLSR_FLASH_CMD_WRITE, addr)

    def cmd_mspi_init_cmd_addr(self, cmd, addr):
        self.cmd_mspi_send_data(cmd)
        self.cmd_mspi_send_data((addr >> 16) & 0xFF)
        self.cmd_mspi_send_data((addr >> 8) & 0xFF)
        self.cmd_mspi_send_data((addr >> 0) & 0xFF)

    def cmd_read(self, addr, readlen, slave_id=None, timeout=None):
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
            raise Exception(status)
        return result

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


def fl_open_serial(args):
    print(f"I Listing COM ports")
    ports = serial.tools.list_ports.comports()
    flipper_port = None
    for port, desc, hwid in sorted(ports):
        print(f"  - {port}: {desc} [{hwid}]")
        if "FLIP_" in hwid:
            flipper_port = port

    print(f"I opening {flipper_port}")
    flipper = serial.Serial(flipper_port, baudrate=args.baud, timeout=1)
    return flipper


class BandwidthCounter:
    def __init__(self):
        self.window = 1
        self.count = 0
        self.pivot = time.time() + self.window

    def start(self):
        self.count = 0
        self.pivot = time.time() + self.window

    def add_batch(self, size):
        self.count += size
        if time.time() > self.pivot:
            self.pivot += self.window
            print(f"Read speed: {humanbytes(self.count / self.window)}/s")
            self.count = 0


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


if __name__ == "__main__":
    main()
