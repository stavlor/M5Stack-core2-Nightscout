Latest display update: see DISPLAY.md. Its firmware build/device checks are pending.

Latest revision: see OVERLAYS.md. New overlay tests/build remain pending; the build and test results below describe earlier revisions.

# Verification record — 2026-10-27

## Build and library compatibility

The production `core2` environment compiled and linked successfully with:

| Component | Version / selection |
| --- | --- |
| PlatformIO Core | 6.2.0 |
| Espressif32 PlatformIO platform | 6.12.0 |
| Board | m5stack-core2, ESP32, 16 MB flash, PSRAM enabled |
| Arduino ESP32 framework | 2.0.17; package 3.20017.241212+sha.dcc1105b |
| Xtensa compiler | GCC 8.4.0, esp-2021r2-patch5 |
| M5Unified | 0.2.28 |
| M5GFX | 0.2.35 |
| ArduinoJson | 6.21.5 |
| HTTPS client | Framework HTTPClient + WiFiClientSecure |

Both final PlatformIO environments passed compilation and linking:

| Environment | Flash image | Static RAM reported by linker | Result |
| --- | ---: | ---: | --- |
| core2 | 1,157,437 bytes | 74,252 bytes | SUCCESS |
| parser-tests | 543,453 bytes | 49,044 bytes | SUCCESS |

Static RAM figures do not include task stacks, TLS, response bodies or JSON
allocations created at runtime. PSRAM is enabled; physical endurance/memory
testing remains an acceptance check. No credentialed binary is distributed.

The official Core2 v1.3 page recommends this board and platform version with
M5Unified. This hardware revision uses AXP192, a 320×240 touch LCD, 16 MB flash,
PSRAM and a CH9102 USB serial controller. Its BMI270 IMU is not used; IMU, mic
and speaker initialization are disabled. Power and touch/display initialization
are delegated to M5Unified rather than hard-coded PMIC register writes.

The build resolves `M5.config()`, M5Unified display/touch buttons,
`WiFiClientSecure::setCACert`, `setHandshakeTimeout`, HTTPClient timeouts,
redirect control, chunk-decoding `writeToStream`, ArduinoJson filtering and
the embedded certificate symbol. Runtime hardware operation has not been tested.

On this Windows workstation the first build exceeded practical compiler command
length with the toolchain under the long generated chat folder. A shorter
toolchain directory fixed it. This did not require changing project code or
dependency versions. If you encounter compiler `CreateProcess` errors, use a
shorter PlatformIO core/packages location and ensure the compiler's bin folder
is on PATH. Normal PlatformIO IDE defaults generally provide both.

## Executed parser regression tests

Production `model.h` and the actual pinned ArduinoJson library were compiled
as C++11 using a desktop GCC 5.1.0 host shim and executed. **13 passed, 0 failed**:

1. SGV numeric validation, sorting, deduplication, invalid/future entry rejection.
2. ISO8601 fractions, timezone offset conversion and invalid calendar dates.
3. The exact stale-age boundary.
4. Array IOB handling, fallback priority and valid zero IOB/COB.
5. Zero temporary basal, temporary-rate selection and fixed expiry.
6. Enactment receipt and timestamp selection.
7. Exact uploader isolation.
8. New upload cannot revive old IOB/basal measurements.
9. Valid empty responses clear corresponding data.
10. Wrong JSON root rejected.
11. Malformed JSON rejected by ArduinoJson.
12. Production glucose filter preserves needed fields and excludes noise.
13. Production AAPS filter preserves object IOB, COB and base basal while
    excluding prediction arrays.

The host shim only provides `strlcpy` and serial output; it does not emulate
Wi-Fi, TLS, display or ESP32 memory conditions. The `parser-tests` PlatformIO
environment compiles the fixtures for execution on an actual Core2.

## API compatibility checked against source

Nightscout API v1 defines SGV entries and devicestatus array resources.
Its authorization resolver accepts subject access tokens in `api-secret`,
independently of administrator API_SECRET authentication. Only GET requests
are implemented. See the exact source links in README.

AAPS source revision `aa16db64196e570b15bf41e85422fc159c058308` was inspected.
`DeviceStatusExtension.kt` serializes `created_at`, `device`, `openaps.iob`,
`openaps.suggested`, `openaps.enacted` and pump data. `PumpStatusProviderImpl.kt`
serializes `pump.status.timestamp`, `status`, `extended.BaseBasalRate`,
`TempBasalAbsoluteRate` and `TempBasalRemaining`. The parser supports both
historical array IOB and current object IOB. No closed-loop mode is inferred
from algorithm output; reported pump/mode text is shown separately.

This establishes source/schema compatibility, not a successful request to your
private server. Older AAPS versions and individual pumps can omit fields; those
values display as unknown instead of being calculated or guessed.

## Included TLS trust anchor

ISRG Root X1, fetched from Let's Encrypt's official HTTPS certificate repository.
DER SHA256 fingerprint:

```text
96:BC:EC:06:26:49:76:F3:74:60:77:9A:CF:28:C5:A7:
CF:E8:A3:C0:AA:E1:1A:8F:FC:EE:05:C0:BD:DF:08:C6
```

Validity: 2015-06-04 11:04:38 UTC to 2035-06-04 11:04:38 UTC.
Only servers chaining to an included root are trusted. See README for
replacing/adding roots. No certificate validation bypass is provided.

## Checks still requiring your Core2 and Nightscout

- Configure and flash, verify Wi-Fi/NTP, then compare SGV, age, IOB, COB and
  labeled basal with the raw Nightscout reports and AAPS.
- Confirm the selected uploader is your AAPS instance and inspect the exact
  reported mode text. `normal` does not establish closed-loop mode.
- Run the on-device parser-test variant and check its serial summary, then
  reflash production firmware.
- Verify touch A refresh and B units, LCD orientation and legibility.
- Disconnect Wi-Fi beyond ten minutes: old SGV must show STALE and age;
  IOB/COB/basal/mode must become unknown. Reconnect and verify recovery.
- Confirm wrong-token 401/403, unknown CA, invalid hostname, missing NTP and
  empty datasets fail visibly. Confirm the read token has no write permissions
  in Nightscout's subject administration, without attempting medical-data writes.
- Leave USB powered for a sustained run to assess reconnection, heap behavior,
  power stability and display performance. No physical endurance test was run here.

## TLS repair after first physical-device flash

The user's browser certificate screenshot identifies Google Trust Services WE1
as the server certificate issuer. The initial firmware bundle only trusted
ISRG Root X1. Added GTS Root R4 while retaining ISRG Root X1. The added root was
extracted from installed certifi and checked against Google's published DER
SHA256 fingerprint:
`349dfa4058c5e263123b398ae795573c4e1313c83fe68f93556cd5e8031b3c7d`.
Reference: <https://pki.goog/repo/cps/5.14/GTS-CPS.html>.
Python/OpenSSL successfully parsed the resulting bundle and counted two CA
certificates. TLS validation and hostname checks remain enabled.

Also added precise TLS/HTTP-client error codes to the screen and serial output,
without logging credentials, URLs, headers or response bodies. The lastError
method signature was checked against the installed ESP32 2.0.17 library source.
The build results above predate this repair. This session cannot execute the
compiler under its current restrictions, so rebuilding the repaired firmware
and verifying HTTPS on the device remain pending. Run the local Flash-Core2.cmd
again to build and upload. No configured credentials were changed.

## Response-body repair after the second device flash

The device advanced past TLS to an HTTP 200 response but showed the old combined
Incomplete/large response error. Its cause could not be distinguished from that
message alone. Replaced the growing Arduino String with a bounded, fixed
192 KiB maximum body buffer allocated in PSRAM when available, with explicit
allocation, size, short-read and transport errors. Known Content-Length sizes
allocate only that many bytes. Unknown/chunked sizes use the maximum. Parsing
uses the decoded mutable buffer with ArduinoJson's zero-copy mode, avoiding
additional string copies. HTTPClient still decodes transfer chunks; certificate
and hostname validation remain enabled.

Glucose requests now filter by the configured graph window rather than always
downloading up to a full day of readings. Status requests use count=2 and
find[openaps][$exists]=true, plus the existing optional exact device filter,
rather than fetching 24 large status snapshots. Read timeout is 20 seconds.

Added three host/on-device response-buffer fixtures: multiple chunk writes to
the exact bound, sticky overflow rejection including HTTPClient's retry, and
allocation failure. Flash-Core2.cmd now compiles and runs all 16 fixtures before
building/uploading the firmware and saves their result in work/last-parser-tests.log.
The prior 13-test result and firmware builds above are historical. The updated
16-test executable, firmware rebuild and device response are pending execution
on the user's PC; this restricted session cannot run the compiler. The buffer
is independent of Arduino for host regression testing. HTTPClient and PSRAM API
signatures were inspected in the installed framework source.
