# Nightscout / AAPS dashboard for M5Stack Core2 v1.3

Current source: 2026-11-04 â€” Graph focus and LAN configuration candidate.

Read-only, validated HTTPS Nightscout v3 dashboard with glucose/trend/history,
OpenAPS forecasts, treatment markers, IOB/COB, basal/pump state, clock, power,
Wi-Fi and optional glucose/missing-update sounds. Graph focus uses a compact
legend and adaptive treatment rows for a 101-pixel graph normally, 91 when crowded.
B opens settings. A has a green refresh icon; C toggles forecasts.

The optional LAN configuration service runs on port 3000, defaults to off on
every restart and is enabled from the device. It configures Nightscout URL/token,
uploader/API, five ordered Wi-Fi networks and display/alert/time settings without
recompiling. Existing saved settings migrate. It never writes Nightscout data.
This is a secondary display; use CGM/AAPS for primary alerts and treatment decisions.

## Start here

1. Extract the release ZIP. Install Python 3.9+ and PlatformIO Core or PlatformIO
   IDE for VS Code. Use a USB data cable and the CH9102 driver if needed.
2. Run Flash-Core2.cmd (Windows) or `python3 flash.py --init-config`. The helper
   creates include/config.h from the example only if missing and never overwrites it.
3. Either edit Wi-Fi/HTTPS URL/read-only subject token in config.h before building,
   or build with the examples and provision from the device afterward.
4. Run Flash-Core2.cmd again, or `python3 flash.py --port /dev/ttyUSB0` using your
   actual port. PlatformIO downloads pinned dependencies/compiler on first build.
5. To provision without compiled credentials: B > Wi-Fi setup > Temporary setup
   hotspot. Join the displayed network and open http://192.168.4.1 to connect Wi-Fi.
   Then B > LAN portal: On, open the displayed LAN address on port 3000 and enter
   the device code. Save Nightscout URL/token there. Turn the portal off when done.

Existing private config.h and saved preferences should be kept during updates.
A saved runtime profile overrides compiled defaults, including Wi-Fi and token.
Ordinary reflashing preserves it. Changing defaults in config.h will not replace
an already saved profile; edit it in the portal or erase flash and re-upload.

See [RELEASE.md](RELEASE.md) for portable flashing options and checksums.
The workstation-specific helper in the surrounding outputs directory remains
available locally: it runs the current 163 C++ host fixtures before building and
uploading to COM3. The portable helper runs normal PlatformIO builds; it does not
automatically run desktop C++ fixtures.

## Documentation

| File | Contents |
| --- | --- |
| [SETTINGS.md](SETTINGS.md) | LAN portal, access code, saved networks, fallback and hotspot recovery |
| [DISPLAY.md](DISPLAY.md) | Graph focus layout, fonts, controls, scaling and status |
| [ALERTS.md](ALERTS.md) | Glucose/stale sounds, threshold settings and mute |
| [OVERLAYS.md](OVERLAYS.md) | Treatment labels/deduplication and forecasts |
| [V3.md](V3.md) | Nightscout requests, JWTs, bounds and explicit legacy mode |
| [VERIFICATION.md](VERIFICATION.md) | Passing checks, compiler limitation and remaining hardware checks |

## Nightscout and HTTPS

Use the canonical HTTPS origin and optional path prefix, without trailing slash,
query, embedded credentials or token parameter. Use a dedicated read-only subject
access token, not API_SECRET or a SHA1 hash. It needs entries, device-status and
treatments read permissions. AAPS uses its own upload credential. Enable glucose,
treatment and device-status uploads there. Set the exact AAPS device filter when
multiple uploaders share a site; only two newest status documents are searched.

V3 is the default. It exchanges the subject token for a RAM-only JWT over HTTPS.
The complete Mozilla/certifi 2026.10.23 snapshot (118 public roots) is embedded
using the ESP32 indexed CA-bundle API. Chain, hostname and time checks remain
active; redirects, plain HTTP Nightscout and insecure fallback are rejected.
See [certs/README.md](certs/README.md) for source, licensing and reproducible
updates. Public leaf renewals normally need no root change. A private/custom CA
requires an explicit root addition and regeneration/rebuild.
NTP must work for TLS; defaults use pool.ntp.org/time.cloudflare.com. No profile
or prediction calculations run locally. Values describe uploaded snapshots.

## Configuration and limits

Compiled defaults are in include/config.h (copy from config.example.h),
include/ns_settings.h, include/display_settings.h and include/alerts.h. Runtime
profile settings are managed by the portal and device menu; see SETTINGS.md for
limits, migration and storage behavior. Portal enablement itself is not saved.

History supports 1/3/6/12 hours; polls default to 60 seconds and allow 30â€“900.
Freshness defaults to ten minutes. Glucose bodies are bounded at 192 KiB, data
JSON at 64 KiB/nesting 16. Entries retain up to 288 points, treatments 128 from
up to 200 fetched records. At the entry cap an SGV history limited warning may
mean older data is missing. Failed/malformed reads retain cached values while
aging; successful empty collections clear them. Nightscout identity changes
clear cached data so another site's values cannot remain on screen.

Health data stays in RAM; settings and saved credentials live in NVS. Diagnostics
print status/counts/sizes, not credentials or response bodies. The LAN portal
never returns saved token/password values; blank secret fields preserve them for
unchanged destinations. A changed Nightscout URL requires its token explicitly.
The portal uses HTTP on a trusted LAN, not HTTPS. Do not expose it to the Internet.

## Build and tests

From the project folder:

```text
pio run -e core2
pio run -e core2 -t upload --upload-port COM3
pio device monitor -b 115200
```

Use your actual upload port. If flashing is unreliable reduce upload_speed to
115200. Fixture firmware is separate from the dashboard:

```text
pio run -e parser-tests -t upload
pio device monitor -b 115200
```

Fixture firmware runs on its own 49 KiB task, with no Wi-Fi, tones or preference
writes. Reflash core2 afterward. Desktop host fixtures after fetching ArduinoJson:

```text
g++ -std=c++11 -Itest/compat -Iinclude -I.pio/libdeps/core2/ArduinoJson/src test/host.cpp -o parser-tests
```

Run that executable. Portable flashing-tool and embedded portal-controller checks:

```text
python -m unittest discover -s test -p test_flash_tool.py -v
node test/test_portal_frontend.cjs
```

The 153 C++ fixtures and preceding trust-adapter firmware built successfully on
the user's workstation. Device diagnostics then identified signature=-16:
MBEDTLS_ERR_MPI_ALLOC_FAILED during intermediate verification. This candidate
installs a PSRAM-preferred TLS allocator before workers start, with internal
RAM fallback and unchanged certificate validation. The user built/flashed it successfully and reported a faster successful initial
fetch; extended stability testing remains in progress. Python bundle/flash-tool checks and Node portal/key
checks are independent of hardware. See VERIFICATION.md for current evidence.

```text
python -m unittest discover -s test -p test_ca_bundle.py -v
node test/test_ca_bundle_node.cjs
```

## Troubleshooting

- Wi-Fi offline: reconnect saved networks or use the recovery hotspot.
- Auth/401/403: check subject token, endpoint rights, API mode and proxy rules.
- TLS failure: capture the TLS anchor failure line and verify=0x... serial fields. The
  footer now distinguishes date, hostname, trust and algorithm failures.
- Missing AAPS: check uploader filter, fields and calculation timestamps.
- Limited history/body error: reduce history hours and note the serial size/code.
- Portal unavailable: enable it on the device, use its current IP with :3000 and
  the same LAN. Use the device's new code if the service or Wi-Fi restarted.

Project source is MIT licensed; dependencies retain their own licenses.

Node 18+ is optional and is only needed for the portal browser-controller tests.

## Live Socket.IO updates

V3 mode automatically subscribes to Nightscout's secure /storage socket for
entries, AAPS device status and treatments. Notifications trigger an early HTTPS
refresh; configured polling remains the fallback. No new token or settings are
needed. See [SOCKETIO.md](SOCKETIO.md) for compatibility, timing, security,
build-time disable and diagnostics.
