# Source release: 2026-11-03

This bundle contains the complete PlatformIO project, a portable build/flash
helper, configuration example, TLS roots, tests, documentation and MIT license.
It does not contain configured firmware binaries, private config.h, compiler
caches or serial logs. Build locally with your own read-only credentials.

## Requirements

- M5Stack Core2 v1.3 and a USB data cable.
- Python 3.9 or newer (Python launcher on Windows recommended).
- PlatformIO IDE for VS Code, or PlatformIO Core installed with
  `python -m pip install platformio` (use python3 if appropriate).
- Working Internet for PlatformIO's first build to fetch the pinned platform,
  libraries and compiler. The helper is not an offline standalone flasher.
- CH9102 serial driver if the device does not appear on Windows.

## Windows

1. Extract the entire ZIP; keep flash.py beside platformio.ini.
2. Double-click Flash-Core2.cmd. On first use it creates include/config.h and
   stops so you can edit it. It never overwrites an existing config.h.
3. Edit that file with your 2.4 GHz Wi-Fi, canonical HTTPS Nightscout URL and
   dedicated read-only subject access token. See README.md for URL/token details.
4. Connect the Core2, close other serial applications and run Flash-Core2.cmd
   again. PlatformIO builds locally and autodetects the upload port.

For an explicit port or build-only operation, open a terminal in the project:

```text
Flash-Core2.cmd --list-ports
Flash-Core2.cmd --port COM3
Flash-Core2.cmd --build-only
Flash-Core2.cmd --monitor --port COM3
```

The wrapper pauses when double-clicked so messages remain visible. Passing any
arguments disables that pause for terminal/automation use. It returns the build
or upload exit status. The flasher does not erase all flash, preserving saved
settings and Wi-Fi credentials. See SETTINGS.md to change Wi-Fi afterward.

## macOS / Linux / any Python terminal

```text
python3 flash.py --init-config
```

Edit include/config.h, then:

```text
python3 flash.py --list-ports
python3 flash.py --build-only
python3 flash.py --port /dev/ttyUSB0
python3 flash.py --monitor --port /dev/ttyUSB0
```

Use the port shown on your system (macOS commonly uses /dev/cu.*). Linux may
require serial-device group membership. Omit --port to use PlatformIO detection.
The helper searches PATH, the current Python's PlatformIO module, and the usual
~/.platformio/penv interpreter installed by PlatformIO IDE. It does not install
software automatically. `python flash.py --help` lists all options.

## Verification and limits

The latest observed workstation firmware host log has 104 passed, zero failed.
The portable helper has 14 isolated Python tests; no device is touched by them.
The helper invokes a normal PlatformIO production build, not the desktop C++
host suite. Fixture instructions and detailed evidence are in VERIFICATION.md.
No new clean firmware compile or physical device test was completed as part of
packaging because compiler executables could not launch in the authoring session.
A local build is required before upload; this is a source release, not a verified
precompiled image or an M5Burner package.

Run the release-tool checks from the extracted project:

```text
python -m unittest discover -s test -p test_flash_tool.py -v
```

SHA256SUMS.txt inside the ZIP lists every bundled source/document/tool file.
The separate release SHA256SUMS.txt lists the ZIP and release-notes asset hashes.

Private configuration and configured firmware embed credentials. Keep them off
GitHub. The source license covers this project; PlatformIO dependencies retain
their own licenses and are fetched separately.
