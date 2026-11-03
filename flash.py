"""Build/flash the sibling PlatformIO project without machine-specific paths."""
import argparse
import importlib.util
from pathlib import Path
import shutil
import subprocess
import sys

ROOT = Path(__file__).resolve().parent


def platformio_command():
    for name in ("pio", "platformio"):
        executable = shutil.which(name)
        if executable:
            return [executable]
    if importlib.util.find_spec("platformio") is not None:
        return [sys.executable, "-m", "platformio"]
    penv = Path.home() / ".platformio" / "penv"
    for interpreter in (penv / "Scripts" / "python.exe", penv / "bin" / "python"):
        if interpreter.is_file():
            return [str(interpreter), "-m", "platformio"]
    return None


def prepare_config(root):
    destination = root / "include" / "config.h"
    if destination.exists():
        return True
    shutil.copyfile(root / "include" / "config.example.h", destination)
    print("Created include/config.h from the example. Edit Wi-Fi, HTTPS Nightscout")
    print("URL and read-only subject token there, then run this tool again.")
    print("Your configuration stays on your computer; do not publish config.h.")
    return False


def main(argv=None):
    parser = argparse.ArgumentParser(description="Build and flash the Core2 dashboard using PlatformIO.")
    action = parser.add_mutually_exclusive_group()
    action.add_argument("--build-only", action="store_true", help="build without uploading")
    action.add_argument("--list-ports", action="store_true", help="list connected serial ports")
    action.add_argument("--monitor", action="store_true", help="open the 115200-baud serial monitor")
    action.add_argument("--init-config", action="store_true", help="create config.h if missing; never overwrite it")
    parser.add_argument("--port", help="serial port, e.g. COM3 or /dev/ttyUSB0; otherwise autodetect")
    args = parser.parse_args(argv)
    if not (ROOT / "platformio.ini").is_file():
        print("Keep flash.py beside platformio.ini in the extracted project.", file=sys.stderr)
        return 2
    if args.init_config:
        if prepare_config(ROOT):
            print("Existing include/config.h kept.")
        return 0
    if not (args.list_ports or args.monitor) and not prepare_config(ROOT):
        return 2
    command = platformio_command()
    if not command:
        print("PlatformIO was not found. Install PlatformIO IDE for VS Code, or run:", file=sys.stderr)
        print("  python -m pip install platformio", file=sys.stderr)
        return 2
    if args.list_ports:
        command += ["device", "list"]
    elif args.monitor:
        command += ["device", "monitor", "--baud", "115200"]
        if args.port:
            command += ["--port", args.port]
    else:
        command += ["run", "--project-dir", str(ROOT), "--environment", "core2"]
        if not args.build_only:
            command += ["--target", "upload"]
            if args.port:
                command += ["--upload-port", args.port]
        print("Building dashboard" + ("." if args.build_only else " and uploading to the Core2."), flush=True)
    try:
        return subprocess.run(command, cwd=ROOT, check=False).returncode
    except OSError as error:
        print("Could not start PlatformIO: " + str(error), file=sys.stderr)
        return 1
    except KeyboardInterrupt:
        print("Stopped.", file=sys.stderr)
        return 130


if __name__ == "__main__":
    raise SystemExit(main())
