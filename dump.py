from contextlib import closing
import serial
import serial.tools.list_ports


def main():

    with closing(fl_open()) as flipper:

        flipper.write(b"ga7g4drb info")
        line = flipper.readline()
        print(f" < {line}")
        if flipper.readline() != "flitswire info response start":
            print("[E] swire emulator not running")
            return
        fl_read_until_line(flipper, "end")

        flipper.write(b"swire_init 3 75000\n")
        if fl_readline(flipper) != "ok":
            return

        fl_write(flipper, 0x0602, 0, b"\x05")  # CPU Stop
        fl_write(flipper, 0x00B2, 0, b"\x7f")  # b0-b4 SWIRE

        sanity_check = fl_read(flipper, 0x00B2, 0, 1)  # b0-b4 SWIRE
        if sanity_check != b"\x7f":
            print(f"[E] sanity_check failed {sanity_check.decode('hex')}")
            return

        # MSPI = Memory SPI
        # CS = Chip Select

        # MSPI Control, CS bit active low
        fl_write(flipper, 0x000D, 0, b"\x03")  # MSPI Data, 03 = read

        # addr = Address

        fl_write(flipper, 0x000C, 0, b"\x03")  # MSPI Data, 03 = read
        fl_write(flipper, 0x000C, 0, b"\x00")  # MSPI read addr[2]
        fl_write(flipper, 0x000C, 0, b"\x00")  # MSPI read addr[1]
        fl_write(flipper, 0x000C, 0, b"\x00")  # MSPI read addr[0]

        # MSPI Data, 00 to drive MSPI Clock to initiate first read
        # MSPI Control, 0a to auto read mode
        fl_write(flipper, 0x000C, 0, bytes.fromhex("000a"))  # MSPI read addr[1]

        fl_write(
            flipper, 0x00B3, 0, b"\x80"
        )  # swire mode, fifo, repeated reads from same address

        SW_IMAGE_LEN = 128
        for addr in range(0, SW_IMAGE_LEN, 16):
            buf = fl_read(flipper, 0x000C, 0, 256)
            print(f"DUMP {addr:06x}: {buf.encode('hex')}")


def fl_write(fl, addr, slaveid, buf):
    fl.write(f"trs {addr:x} 0 {slaveid:x}\n".encode("utf-8"))
    if fl_readline(fl) != "ok":
        raise Exception()
    fl.write(f"bw {len(buf):x}\n".encode("utf-8"))
    fl.write(buf)
    if fl_readline(fl) != "ok":
        raise Exception()
    fl.write(b"tre\n")
    if fl_readline(fl) != "ok":
        raise Exception()


def fl_read(fl, addr, slaveid, readlen):
    fl.write(f"trs {addr:x} 1 {slaveid:x}\n".encode("utf-8"))
    if fl_readline(fl) != "ok":
        raise Exception()
    fl.write(f"br {readlen:x}\n".encode("utf-8"))
    if fl_readline(fl) != "ok":
        raise Exception()
    result = fl.read(readlen)
    if len(result) != readlen:
        raise Exception()
    fl.write(b"tre\n")
    if fl_readline(fl) != "ok":
        raise Exception()


def fl_read_until_line(fl, search):
    while True:
        line = fl_readline(fl)
        if line is None:
            print(f" <* closed")
            return
        if line == search:
            return


def fl_open():
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


def read_everything(flipper):
    while True:
        line = fl_readline(flipper)
        if line is None:
            break


def fl_readline(flipper):
    line = flipper.readline()
    if not line:
        return None
    line = line.decode("utf-8").rstrip("\r\n")
    print(" < " + line)
    return line


if __name__ == "__main__":
    main()
