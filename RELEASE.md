# Source release: Graph focus / LAN portal / TLS memory, 2026-11-04

This bundle includes full PlatformIO source, portable build/flash helpers,
configuration example, TLS roots, regression fixtures, documentation and MIT
license. No private config.h, configured binary, compiler cache or log is included.

## Requirements

M5Stack Core2 v1.3, USB data cable, Python 3.9+, and PlatformIO IDE for VS Code or
PlatformIO Core (`python -m pip install platformio`). The first build downloads
the pinned platform/libraries/compiler. This is not an offline standalone flasher
or an M5Burner image. Install M5Stack's CH9102 driver if no serial port appears.

## Windows

Extract the entire archive and keep flash.py beside platformio.ini. Double-click
Flash-Core2.cmd. First use creates include/config.h only if missing and stops
for editing; existing private config is preserved. Either fill your Wi-Fi and
HTTPS Nightscout URL/read-only token there, or leave example values and provision
through the device's hotspot followed by its LAN portal (README.md/SETTINGS.md).
Run the helper again to build/upload with automatic port detection.

From a terminal in the extracted project:

```text
Flash-Core2.cmd --list-ports
Flash-Core2.cmd --port COM3
Flash-Core2.cmd --build-only
Flash-Core2.cmd --monitor --port COM3
```

The wrapper pauses when launched without arguments. Arguments disable the pause
for terminal/automation use. It returns the build/upload exit status. Close other
serial applications first. Uploading normally preserves saved NVS preferences.

## Other systems / Python terminal

```text
python3 flash.py --init-config
python3 flash.py --list-ports
python3 flash.py --build-only
python3 flash.py --port /dev/ttyUSB0
python3 flash.py --monitor --port /dev/ttyUSB0
```

Choose your actual port (macOS commonly /dev/cu.*); omit --port for detection.
Linux may require serial-device group membership. The helper finds PlatformIO
on PATH, in the current Python, or the standard ~/.platformio/penv environment
installed by PlatformIO IDE. It does not install software automatically.
`python flash.py --help` lists options.

## Device configuration

B opens settings; A refresh has a green arrow; C toggles forecasts. B > Wi-Fi
setup provides reconnect and recovery hotspot provisioning. LAN portal is off
after each restart. Enable it on the device, open the displayed address on port
3000 from the same LAN and enter the displayed access code. See SETTINGS.md for
five-network fallback, full Nightscout/profile settings and access/storage details.

A saved runtime profile takes precedence over config.h defaults. Existing saved
settings migrate on first use. Reflashing retains them; erasing flash clears them
and requires uploading the firmware again.

## Verification status

The 153-fixture trust-adapter firmware built on the user's workstation, but
intermittent device failures remained. New diagnostics identify signature=-16
(MBEDTLS_ERR_MPI_ALLOC_FAILED). This candidate moves TLS allocations from the
SDK's internal-only policy to PSRAM preference with internal fallback. Validation
stays required. The user built/flashed this allocation change and reported a faster successful
initial fetch; extended stability testing is ongoing;
existing release ZIPs are not automatically updated and contain no configured image.

The portable helper's 14 isolated Python tests and the actual embedded portal
controller's Node checks pass. These do not replace C++ or ESP32 tests. They can
be run locally without connecting a device:

```text
python -m unittest discover -s test -p test_flash_tool.py -v
node test/test_portal_frontend.cjs
node test/test_ca_bundle_node.cjs
python -m unittest discover -s test -p test_ca_bundle.py -v
```

The portable helper invokes a normal PlatformIO production build. Desktop C++
fixtures require a C++ compiler and ArduinoJson headers; on-device fixture firmware
is provided as parser-tests. README.md gives both workflows. The original local
workstation helper in the surrounding outputs directory runs all C++ checks first.

SHA256SUMS.txt inside the ZIP covers every bundled source/tool/document. The
separate release checksum asset covers the ZIP and release-notes asset. Configured
firmware and config.h contain private credentials and must not be published.
Dependencies retain their own licenses and are fetched separately.

Node 18+ is optional and is only needed for the portal browser-controller tests.

Three offline CA checks pin the reviewed source snapshot, reproduce every binary
record and reject malformed/truncated data. The optional Node check independently
verifies all 118 embedded keys against the source certificates. The CA .bin file
is public trust data; it is not a firmware image. See certs/README.md.
