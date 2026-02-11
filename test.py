from subprocess import check_call as run
import serial
import serial.tools.list_ports


def main():
    run("ufbt build")
    run("ufbt launch")
    cli = open_flipper()
    try:
        cli.write(b"log\r\n")
        timeout = 0
        while cli.is_open:
            data = cli.readline()
            if data is None:
                break
            if not data:
                timeout += 1
                if timeout >= 30:
                    break
            else:
                timeout = 0
            if data:
                data = data.decode("utf-8").rstrip("\r\n")
                print(data)
                if "NOT Initializing USB..." in data:
                    break
    finally:
        print(f"[i] Cleanup...")
        if cli.is_open:
            print(f"[i] Closing...")
            cli.close()
        print(f"[i] Closed")


def open_flipper():
    ports = serial.tools.list_ports.comports()
    flipper_port = None
    for port, desc, hwid in sorted(ports):
        if "FLIP_" in hwid:
            flipper_port = port
    return serial.Serial(flipper_port, baudrate=115200, timeout=1)


if __name__ == "__main__":
    main()
