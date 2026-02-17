from contextlib import closing
import datetime
import os
import queue
import threading
import time
import serial
import serial.tools.list_ports
from subprocess import check_call as run


def main():
    with closing(Swire()) as swire:
        swire.serial.write(b"ga7g4drb info\n")
        line = swire.readline()
        if line == "flitswire info response start":
            line = swire.readlines_until("end")
            swire.serial.write(b"ga7g4drb close\n")
            swire.readlines_until("ga7g4drb closing")

    run(["ufbt", "launch"])

    time.sleep(3)
    with closing(Swire()) as swire:
        swire.serial.write(b"ga7g4drb info\n")

        line = swire.readline()
        if (line + " ").startswith("swire_demo welcome "):
            line = swire.readline()
        if line != "flitswire info response start":
            print("[E] swire emulator not running")
            return
        swire.readlines_until("end")

        swire.serial.write(b"swire_init 3 75000\n")
        swire.serial.write(b"reset 70 1000\n")
        if swire.readline() != "ok":
            raise Exception()

        swire.write(0x0602, b"\x05")  # CPU Stop
        swire.write(0x00B2, b"\x7f")  # b0-b4 SWIRE
        swire.ping_sync()
        sanity_check = swire.read(0x00B2, 1)  # b0-b4 SWIRE
        print(f"sanity_check: {sanity_check.hex()}")
        if sanity_check != b"\x7f":
            print(f"[E] sanity_check failed {sanity_check.hex()}")
            return

        # MSPI = Memory SPI
        # CS = Chip Select

        # MSPI Control, CS bit active low
        swire.write(0x000D, b"\x00")  # MSPI Data, 03 = read

        # addr = Address

        swire.write(0x000C, b"\x03")  # MSPI Data, 03 = read
        swire.write(0x000C, b"\x00")  # MSPI read addr[2]
        swire.write(0x000C, b"\x00")  # MSPI read addr[1]
        swire.write(0x000C, b"\x00")  # MSPI read addr[0]

        # MSPI Data, 00 to drive MSPI Clock to initiate first read
        # MSPI Control, 0a to auto read mode
        swire.write(0x000C, bytes.fromhex("000a"))  # MSPI read addr[1]

        swire.write(
            0x00B3, b"\x80"
        )  # swire mode, fifo, repeated reads from same address

        ts = datetime.datetime.now().strftime("%Y%m%d-%H%M%S")
        os.makedirs(".dumps", exist_ok=True)
        with open(f".dumps/dump-{ts}.bin", "wb") as f:
            SW_MAX_IMAGE_LEN = 512 * 1024
            for addr in range(0, SW_MAX_IMAGE_LEN, 256):
                buf = swire.read(0x000C, 256)
                if swire.debug:
                    for chunk in chunks(buf, 32):
                        print(
                            f"DUMP {addr:06x}: {chunk[0:16].hex()}  {chunk[16:32].hex()}"
                        )
                        addr += 32
                if all(x == 0xFF for x in buf):
                    break
                f.write(buf)

        swire.write(0x00B3, b"\x00")  # swire mode reset to default
        swire.write(0x000D, b"\x01")  # MSPI Control disable CS


def chunks(lst, n):
    """Yield successive n-sized chunks from lst."""
    for i in range(0, len(lst), n):
        yield lst[i : i + n]


class Swire:
    def __init__(self, serial=None, timeout=None):
        if serial is None:
            serial = fl_open_serial()
        if timeout is None:
            timeout = 5
        self.serial = serial
        self.timeout = timeout
        self.serial.timeout = timeout
        self.log_queue = queue.Queue()
        self.data_queue = queue.Queue()
        self.running = True
        self.reader_thread = threading.Thread(target=self._read_worker)

        self.reader_thread.start()
        self.debug = False

    def write(self, addr, buf, slave_id=None):
        if slave_id is None:
            slave_id = 0
        self.serial.write(
            b"".join(
                [
                    f"trs {addr:x} 0 {slave_id:x}\n".encode("utf-8"),
                    f"bw {len(buf):x}\n".encode("utf-8"),
                    buf,
                    b"tre\n",
                ]
            )
        )

    def readline(self, timeout=None):
        if timeout is None:
            timeout = self.timeout
        return self.log_queue.get(timeout=timeout)

    def close(self):
        self.running = False
        self.serial.close()
        self.reader_thread.join()

    def is_open_redundant(self):
        if not self.serial.is_open:
            print(f"[d] not not self.serial.is_open")
            return False
        if not self.running:
            print(f"[d] not self.running")
            return False
        return True

    def ping_sync(self):
        self.serial.write(b"ping\n")
        self.readlines_until("pong")

    def read(self, addr, readlen, slave_id=None, timeout=None):
        if slave_id is None:
            slave_id = 0
        if timeout is None:
            timeout = self.timeout
        self.serial.write(
            b"".join(
                [
                    f"trs {addr:x} 1 {slave_id:x}\n".encode("utf-8"),
                    f"br {readlen:x}\n".encode("utf-8"),
                    b"tre\n",
                ]
            )
        )
        result = self.data_queue.get(timeout=timeout)
        if len(result) != readlen:
            raise Exception()
        return result

    def readlines_until(self, marker):
        while self.is_open_redundant():
            line = self.readline()
            if line == marker:
                return

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


def fl_open_serial():
    print(f"I Listing COM ports")
    ports = serial.tools.list_ports.comports()
    flipper_port = None
    for port, desc, hwid in sorted(ports):
        print(f"  - {port}: {desc} [{hwid}]")
        if "FLIP_" in hwid:
            flipper_port = port

    print(f"I opening {flipper_port}")
    flipper = serial.Serial(flipper_port, baudrate=115200, timeout=1)
    return flipper


if __name__ == "__main__":
    main()
