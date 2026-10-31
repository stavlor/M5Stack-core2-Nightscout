# Nightscout / AAPS dashboard for M5Stack Core2 v1.3

Current documentation: 2026-10-31.

A read-only HTTPS dashboard using **Nightscout API v3 by default**. It shows large
glucose values and graphical trend arrows, history, carb/SMB/bolus markers,
OpenAPS forecasts, IOB/COB, reported basal and mode, local time, power, Wi-Fi,
sync activity, and optional glucose and missing-update sounds. It works on USB or the Core2 battery;
the LCD stays on. It does not send dosing commands or write Nightscout data.
This is a secondary display; use the CGM/AAPS for treatment decisions and primary alerts.

## Documentation

| File | Contents |
| --- | --- |
| [V3.md](V3.md) | Authentication, request fields, response handling, legacy fallback |
| [DISPLAY.md](DISPLAY.md) | Layout, buttons, touch toggle, clock and indicators |
| [OVERLAYS.md](OVERLAYS.md) | Treatment markers, deduplication, predictions and limits |
| [ALERTS.md](ALERTS.md) | Thresholds, repeats, mute persistence and sound settings |
| [VERIFICATION.md](VERIFICATION.md) | Actual test/build evidence and remaining checks |

## Existing configured installation

Keep your existing `include/config.h`; do not replace it with the example.
Rebuild after a firmware/settings change. On the original Windows workstation,
run `Flash-Core2.cmd` from the `outputs` folder beside this project. It runs the
current **88 host checks**, builds `core2`, and uploads to **COM3**. Close M5Burner
and serial monitors first. The helper uses workstation-specific paths and cached
tools; it is outside the portable source ZIP. Its test log is written to
`work/last-parser-tests.log` in the surrounding workspace.

## Fresh setup from the ZIP

1. Install VS Code with PlatformIO IDE, or PlatformIO Core.
2. Extract the ZIP and open the folder containing `platformio.ini`.
3. Copy `include/config.example.h` to `include/config.h` only for a new installation.
4. Enter 2.4 GHz Wi-Fi credentials, the canonical HTTPS URL and a dedicated
   read-only **subject access token**. Use the token as issued, not API_SECRET
   or a SHA1 hash. The URL may have a path prefix but no trailing slash, query,
   embedded credentials or `?token=` parameter.
5. Set `AAPS_DEVICE` to the exact uploaded `device` string when several uploaders
   share the site. Without it, v3 searches for OpenAPS data among only the two
   newest returned device-status records.
6. Check the included CA roots against your site's chain. NTP must work before HTTPS.
7. Connect a USB **data** cable and build/upload **core2**. Install M5Stack's
   CH9102 driver if Windows does not show a serial port.

From the project folder:

```text
pio run -e core2
pio run -e core2 -t upload
pio device monitor -b 115200
```

Append `--upload-port COM3` to upload when needed, using your actual port. Reduce
`upload_speed` to 115200 if flashing is unreliable. `parser-tests` is fixture
firmware, not the dashboard; reflash `core2` afterward. Portable PlatformIO builds
do not automatically run the desktop host suite.

The ZIP contains source/example settings, not a configured binary. `config.h`
is excluded from the archive and Git ignore rules protect it from routine
commits. A configured binary contains credentials in flash; do not publish it.

## Configuration files

| File | Settings |
| --- | --- |
| `include/config.h` | Wi-Fi, URL/token, AAPS device, units, history hours, polling, stale limits, display thresholds, brightness and NTP |
| `include/ns_settings.h` | `USE_V3=true`; false selects explicit v1 fallback |
| `include/display_settings.h` | POSIX timezone; Eastern with automatic US DST |
| `include/alerts.h` | Sound thresholds, low repeat interval, volume, duration and frequency |

Polling defaults to 60 seconds; failures back off from 30 to 240 seconds. A
requests a refresh. History defaults to three hours, configurable from 1-6.
SGV/AAPS values become stale after ten minutes by default. Display and sound
threshold settings are separate; both default to 70/180 mg/dL.

## Nightscout and AAPS

Use a dedicated subject with `readable` or equivalent `api:entries:read`,
`api:devicestatus:read` and `api:treatments:read` rights. The firmware only issues
GETs but cannot prove a supplied token lacks write permission; scope it in
Nightscout. V3 exchanges it for a temporary JWT and uses Bearer authorization.
See [V3.md](V3.md) for details and the explicit legacy fallback.

Enable AAPS glucose, treatment and device-status uploads. AAPS uses its own upload
credential; the display uses a separate read-only credential. Glucose-only uploads
cannot provide IOB/COB, pump state or predictions. The display does not download
profiles or calculate insulin, carbohydrate absorption or predictions locally.

## TLS, limits and errors

`certs/root-ca.pem` contains ISRG Root X1 and GTS Root R4. Chain, hostname and
validity checks remain enabled; HTTP and redirects are rejected. The approved
live v3 probe succeeded with this bundle. For another CA, obtain its root from a
trusted official source, verify its fingerprint, update the PEM, rebuild and
flash. The server must send intermediates. Routine leaf renewal under the same
root usually needs no firmware change.

NTP defaults to `pool.ntp.org` and `time.cloudflare.com` (UDP/123). Use reachable
local servers if needed. A clock before 2026-01-01 blocks API access. Ages continue
with a monotonic timer after an initial anchor, including during network outages.
Connect/TLS-handshake/read timeouts are 10/12/20 seconds. HTTP chunks are decoded
before JSON parsing. Bodies are capped at 192 KiB in fixed buffers, preferring
PSRAM; data JSON uses 64 KiB and nesting limit 16. JWT exchange has smaller bounds.

Malformed/oversized responses retain prior data while it ages; valid empty
collections clear corresponding data. Metric strings/nulls/booleans/nonfinite
values are rejected. SGV must be 20-600 mg/dL; records over two minutes in the
future are rejected. Treatment markers exclude any future event. Health data
stays in RAM; only the alert-enabled preference persists.

The footer prioritizes SGV errors, then AAPS, then treatments. Long errors are
ellipsized beside the activity indicator. Serial diagnostics contain codes and
sizes, not credentials or response bodies. Check:

- **Auth / HTTP 401 or 403:** token rights, v3 availability and proxy rules. Both
  auth and data requests explicitly use `ESP32HTTPClient`, matching the successful probe.
- **TLS verify / Auth TLS/network:** final hostname, NTP, CA and intermediate chain.
- **Body/memory error:** note the code/size, shorten history if necessary.
- **Missing AAPS values:** upload fields/timestamps and exact `AAPS_DEVICE` filter.
- **Treatments limited:** bounded history may omit older events; reduce history hours.

Networking runs separately from the UI. A very slow peer can extend a response
by sending bytes within each read timeout, but the UI remains responsive.
Non-ASCII pump mode text may not render correctly. There is no captive portal.

## Tests and references

See [VERIFICATION.md](VERIFICATION.md) for actual evidence and outstanding checks.
Core2 fixture firmware has no Wi-Fi, audible tones or saved-preference writes:

```text
pio run -e parser-tests -t upload
pio device monitor -b 115200
```

Desktop tests after obtaining ArduinoJson through a normal PlatformIO build:

```text
g++ -std=c++11 -Itest/compat -Iinclude -I.pio/libdeps/core2/ArduinoJson/src test/host.cpp -o parser-tests
```

Run the executable (`parser-tests.exe` on Windows). The shim replaces Arduino
string-copy/serial facilities, not the production parsing/state models.

- [Core2 v1.3 hardware](https://docs.m5stack.com/en/core/Core2_v1.3)
- [M5Unified](https://github.com/m5stack/M5Unified) and [M5GFX](https://github.com/m5stack/M5GFX)
- [Nightscout v3 specification](https://github.com/nightscout/cgm-remote-monitor/blob/master/lib/api3/swagger.yaml)
- [ArduinoJson v6](https://arduinojson.org/v6/)

Project source is MIT licensed; dependencies retain their own licenses.
