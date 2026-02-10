import serial

import serial.tools.list_ports


def main():
    ports = serial.tools.list_ports.comports()

    print(f"I Listing COM ports")
    flipper_port = None
    for port, desc, hwid in sorted(ports):
        print(f"  - {port}: {desc} [{hwid}]")
        if "FLIP_" in hwid:
            flipper_port = port

    print(f"I opening {flipper_port}")
    flipper = serial.Serial(flipper_port, baudrate=115200, timeout=1)
    try:

        # read_everything(flipper)
        # flipper.write(b"loader close\r\n")
        # read_everything(flipper)
        # flipper.write(b'loader open "Telink SWire Demo"\r\n')

        read_everything(flipper)
        flipper.write(b"log\r\n")

        with open("dump.bin", "wb") as f:
            timeout_counter = 0
            while True:
                line = my_readline(flipper)
                if line is None:
                    timeout_counter += 1
                    if timeout_counter < 30:
                        continue
                    break
                timeout_counter = 0
                print(f"  " + line)
                if line == "DUMP FINISHED":
                    break
                if line.startswith("DUMP "):
                    f.write(bytes.fromhex(line[12:]))

    finally:
        flipper.close()


def read_everything(flipper):
    while True:
        line = my_readline(flipper)
        if line is None:
            break
        print(f"  " + line)


def my_readline(flipper):
    line = flipper.readline()
    if not line:
        return None
    return line.decode("utf-8").rstrip("\r\n")


if __name__ == "__main__":
    main()
