from contextlib import closing
from subprocess import check_call as run
import serial
import serial.tools.list_ports


def wait_for_line(cli, line):
    while cli.is_open:
        data = cli.readline()
        if data is None:
            raise Exception("data is none")
        if not data:
            timeout += 1
            if timeout >= 5:
                raise Exception("timeout")
        else:
            timeout = 0
            if data:
                data = data.decode("utf-8").rstrip("\r\n")
                print(f" < {data}")
                if data == line:
                    break
                if "NOT Initializing USB..." in data:
                    break


def main():
    cli = open_flipper()
    with closing(open_flipper()) as cli:
        cli.write(b"ga7g4drb close\n")
    run("ufbt launch")
    with closing(open_flipper()) as cli:
        wait_for_line("")
        cli.write(b"bbt\n")
        cli.write(b"ga7g4drb close\n")


def open_flipper():
    ports = serial.tools.list_ports.comports()
    flipper_port = None
    for port, desc, hwid in sorted(ports):
        if "FLIP_" in hwid:
            flipper_port = port
    return serial.Serial(flipper_port, baudrate=115200, timeout=1)


if __name__ == "__main__":
    main()
