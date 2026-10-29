See ALERTS.md for enabled first-occurrence glucose chirps and 15-minute low reminders. This supersedes older statements that sound is disabled.

See DISPLAY.md for change-driven redraws and the upper-right local clock.

See OVERLAYS.md for the latest SMB/carb markers, prediction pane, button C controls and verification status. That update supersedes older descriptions below.

# Nightscout / AAPS display for M5Stack Core2 v1.3

A USB-powered, read-only 320×240 dashboard: current glucose and trend, a time-based
3-hour graph, uploaded IOB/COB, reported base or temporary basal, and AAPS activity
and reported pump/mode text. No insulin delivery commands or Nightscout writes.
This is a secondary information display, not a dosing device or alarm system.
Check the CGM/AAPS directly for treatment decisions and alerts.

## Quick setup

1. Install VS Code and the **PlatformIO IDE** extension, or PlatformIO Core.
2. Extract the ZIP, then open the `nightscout-core2` folder containing
   `platformio.ini` in VS Code. Allow PlatformIO to install its dependencies.
3. Copy `include/config.example.h` to `include/config.h`.
4. Enter your 2.4 GHz Wi-Fi SSID/password, HTTPS Nightscout URL and read-only
   **subject access token**. Use the token exactly as issued, not a SHA1 hash,
   API_SECRET, or a browser login URL. Do not put `?token=...` in the URL.
   Use an origin like `https://example.org`, optionally with a path prefix,
   and no trailing slash. Never commit or share `config.h` or a configured binary.
5. Set `AAPS_DEVICE` to the exact `device` field from your AAPS devicestatus if
   you have several uploaders. Empty accepts the newest record containing
   `openaps`; it cannot distinguish AAPS from other OpenAPS-compatible uploaders.
6. Check the HTTPS trust instructions below. The included CA is suitable only
   if your server's chain terminates at ISRG Root X1 or GTS Root R4.
7. Connect the Core2 v1.3 through a USB **data** cable. Use **PlatformIO: Build**,
   then **PlatformIO: Upload** for environment `core2`. Keep USB power connected.
   On Windows, install the CH9102 driver from M5Stack if the serial port is missing.
8. Open the serial monitor at 115200 if needed. Credentials and API bodies are
   never intentionally logged. The first screen asks for configuration if the
   placeholder values have not been replaced. NTP must work before HTTPS starts.

CLI equivalents (from the project folder):

```text
pio run -e core2
pio run -e core2 -t upload
pio device monitor -b 115200
```

If there are several serial devices, append `--upload-port COM5` to the upload
command, replacing COM5 with the actual Core2 port. Close the serial monitor
before uploading. If flashing is unreliable, reduce `upload_speed` to 115200.

The archive contains source, not a preconfigured flash image: the firmware must
be built with your own URL, Wi-Fi details and token. Secrets are stored in flash
in the compiled firmware; someone with physical access may extract them.

## Nightscout and AAPS configuration

In Nightscout's **Admin Tools → Subjects**, create a dedicated subject with only
the `readable` role and use its access token. An equivalent custom role needs
`api:entries:read` and `api:devicestatus:read`. Do not assign admin, careportal,
or upload permissions. This firmware cannot verify that a supplied token lacks
write rights; enforce that in Nightscout. The raw subject token is sent over
validated TLS in the `api-secret` header, which Nightscout's authorization code
accepts for subject tokens. It is not the administrator API_SECRET.

In AAPS, enable Nightscout synchronization and device-status uploads as well as
glucose uploads. Ensure its own upload credential has the permissions AAPS needs;
the Core2 read-only token is separate. Check that your Nightscout actually has
recent `openaps` and `pump` data. A glucose-only uploader cannot supply these.
Nightscout's `DEVICESTATUS_ADVANCED=true` is useful for its own OpenAPS display;
the Core2 reads the REST documents directly. No profile or treatments download
is required, and the display does not independently calculate IOB or COB.

Only these two API v1 GET resources are used:

```text
/api/v1/entries/sgv.json?count=288&find[date][$gte]=WINDOW_START_UNIX_MS
/api/v1/devicestatus.json?count=2&find[openaps][$exists]=true
```

With `AAPS_DEVICE` set, the second request also includes a URL-encoded
`find[device]` equality filter. This avoids another uploader crowding AAPS out
of the bounded two-record response. The glucose request covers only the configured
graph window, using a numeric Unix-millisecond lower bound. API v1 remains the compatibility interface
even if AAPS itself uses NSClientV3. Your installation's `/api-docs/` can confirm
the endpoints. Proxies must allow the `api-secret` request header and deliver
JSON without a browser-only login challenge.

## TLS validation and certificate maintenance

`certs/root-ca.pem` contains **ISRG Root X1** (from
<https://letsencrypt.org/certs/isrgrootx1.pem>) and **GTS Root R4**, for Google
Trust Services chains such as WE1. The Google root was extracted from the
installed certifi CA bundle and its DER SHA256 checked against Google
Trust Services official publication:
<https://pki.goog/repo/cps/5.14/GTS-CPS.html>. Its fingerprint is
`349dfa4058c5e263123b398ae795573c4e1313c83fe68f93556cd5e8031b3c7d`.
The PEM bundle is embedded and passed to
`WiFiClientSecure::setCACert`. The ESP32 validates the server chain, validity
dates and URL hostname. There is no insecure fallback or certificate bypass.
HTTPS redirects are refused, so use the final canonical URL. HTTP is rejected.

Before flashing, inspect your site's certificate chain in a browser or with
`openssl s_client -connect YOUR_HOST:443 -servername YOUR_HOST -showcerts`.
If its trust anchor differs, obtain the appropriate **root CA** from that CA's
official repository (or your private CA administrator), verify its SHA256
fingerprint through a trusted source, and replace `certs/root-ca.pem` with the
PEM root. The same file may contain several PEM roots during a planned CA
transition. Rebuild and flash. Do not trust a root merely because an unverified
server provided it. Servers must send the necessary intermediate certificates.

Do not pin the short-lived leaf certificate. Routine server certificate renewals
under the same trusted root do not require a rebuild; root/chain changes can.
NTP uses `pool.ntp.org` and `time.cloudflare.com` over UDP/123. If blocked, change
these to a reachable local NTP server in config.h. NTP is unauthenticated, as in
the underlying Arduino stack; use a trusted network. A clock before 2026-01-01
blocks API connections. Age continues advancing with an ESP32 monotonic timer
while Wi-Fi or NTP is unavailable after the initial sync.

## Reading the display

- **SGV/trend**: Nightscout `sgv` is always mg/dL. mmol/L is a presentation
  conversion using 18.0182. The ASCII arrows represent DoubleUp `^^`, SingleUp
  `^`, FortyFiveUp `/^`, Flat `->`, FortyFiveDown `v\`, SingleDown `v`, DoubleDown
  `vv`; all other direction values display `?`.
- **Graph**: actual sample timestamps, oldest left, now right. It starts at
  40–250 mg/dL and expands to include visible data. Lines break across gaps
  exceeding 12 minutes. Thresholds are configurable display guides.
- **STALE**: default age limit is 10 minutes. Old glucose remains visibly gray
  with age, and the header says STALE. An empty valid response clears it to `--`.
  A request failure retains the last good data, with an error and advancing age.
- **IOB/COB**: shown only while the underlying metric timestamp is recent.
  Missing, invalid or old values show `--`; a genuine zero displays as zero.
  `openaps.iob` may be an object or an array; only its first element is current.
  Newer suggested IOB is a fallback; COB comes from timestamped suggested or
  enacted data. No future IOB predictions are displayed.
- **Basal**: `pump.extended.TempBasalAbsoluteRate` plus a positive
  `TempBasalRemaining` takes priority and is labeled **Temp**; otherwise
  `BaseBasalRate` is labeled **Base**. Values use the pump status timestamp.
  A temp expires at upload time plus remaining minutes; thereafter it shows
  `--` until a new report. This is an uploaded snapshot, not confirmation of
  current delivery. A base rate is the pump's reported base, not total delivered
  insulin, and extended bolus rates are not added. No suggested rate is shown
  as delivered basal. Older/pump-specific uploads without these fields show `--`.
- **AAPS**: `Suggested`, `Enacted`, or `Enact reported` describes the newest
  timestamped OpenAPS activity. Enacted requires `received: true`; otherwise an
  enacted record is only labeled reported. An enacted record does **not** prove
  closed-loop mode. `Mode/pump` is the pump's uploaded `status.status` text,
  truncated to fit. New AAPS versions may report mode here; older versions may
  only say `normal`. Missing/stale status says `Unknown/stale`. The display does
  not infer closed/open/suspended mode from the existence of a suggestion.

Touch button **A** (lower left) requests a refresh; **B** (center) toggles units
until reboot. C is unused. Default refresh is 60 seconds; failures back off from
30 seconds to at most 240 seconds. The LCD stays on, with no sleep or audible
alarms. Brightness is configurable. No battery modifications are required.

## Robustness and limits

All networking runs on a separate task; display input and age continue during
DNS/TLS/HTTP waits. Requests have connect, handshake and read timeouts. Responses
are capped at 192 KiB in a fixed buffer (PSRAM preferred); decoded JSON uses a bounded 64/48 KiB document and a nesting
limit of 16, with filters excluding unused predictions. HTTP chunked bodies are
decoded by HTTPClient before JSON parsing. Malformed/oversized data and non-array
roots do not replace a good snapshot. A valid empty array clears the corresponding
data. Numeric fields reject nulls, strings, booleans and nonfinite values. SGV
outside 20–600 mg/dL and timestamps over 2 minutes in the future are rejected.
Entries are sorted and deduplicated, with at most 288 points. Up to two matching device
statuses are inspected; the newest matching uploader is used without merging
different uploaders or carrying values forward from older records.

Wi-Fi loss, HTTP 401/403, unexpected HTTP status, failed TLS and bad JSON have
short screen errors. For TLS/network errors, check the canonical hostname,
Wi-Fi, NTP, CA and server intermediate chain. Credentials are intentionally
absent from diagnostics. The screen shows one endpoint error at a time (SGV
first); IOB/COB/basal can remain absent even while glucose is current. Body failures
now report an explicit read error code and byte count, allocation failure,
size limit or declared-length mismatch. Serial diagnostics contain only sizes
and error codes, never tokens or response contents.

The screen is fixed landscape, ASCII text; non-ASCII localized pump status may
not render correctly. Arbitrarily slow peers can keep an individual response
open while delivering bytes within read timeouts, but the display task keeps
running. This project has no persistent health-data cache or captive portal.

## Verification

See `VERIFICATION.md` for the actual build and parser test results, versions,
source references and remaining physical-device acceptance checks.

For the same parser tests on the Core2, build/upload `parser-tests` instead of
`core2`, then open the serial monitor. This test firmware runs fixtures and stops;
reflash `core2` afterward. It does not connect to Wi-Fi or Nightscout.

```text
pio run -e parser-tests -t upload
pio device monitor -b 115200
```

To run parser tests on a desktop with GCC, first run a normal PlatformIO build
to download ArduinoJson, then compile the host shim with C++11 or newer:

```text
g++ -std=c++11 -Itest/compat -Iinclude -I.pio/libdeps/core2/ArduinoJson/src test/host.cpp -o parser-tests
```

Run the resulting executable (Windows: `parser-tests.exe`). The shim substitutes
only string copy and serial printing; the production model/parser is unchanged.

## Compatibility references

- Core2 v1.3 hardware and official PlatformIO environment:
  <https://docs.m5stack.com/en/core/Core2_v1.3>
- M5Unified: <https://github.com/m5stack/M5Unified>
- M5GFX: <https://github.com/m5stack/M5GFX>
- ArduinoJson v6: <https://arduinojson.org/v6/>
- ESP32 platform 6.12.0: <https://github.com/platformio/platform-espressif32/releases/tag/v6.12.0>
- Nightscout API and authorization:
  <https://github.com/nightscout/cgm-remote-monitor/blob/master/lib/server/swagger.yaml>
  and <https://github.com/nightscout/cgm-remote-monitor/blob/master/lib/authorization/index.js>
- AAPS serializers at checked revision aa16db64196e570b15bf41e85422fc159c058308:
  <https://github.com/nightscout/AndroidAPS/blob/aa16db64196e570b15bf41e85422fc159c058308/implementation/src/main/kotlin/app/aaps/implementation/pump/PumpStatusProviderImpl.kt>
  and <https://github.com/nightscout/AndroidAPS/blob/aa16db64196e570b15bf41e85422fc159c058308/plugins/sync/src/main/kotlin/app/aaps/plugins/sync/nsclient/extensions/DeviceStatusExtension.kt>

Project source is MIT licensed; dependencies retain their own licenses.
