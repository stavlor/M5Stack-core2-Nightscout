# Verification record

Reviewed 2026-10-31 against the current source and local evidence.

## Current evidence

| Area | Evidence | Limit |
| --- | --- | --- |
| Earlier host regression suite | Local `work/last-parser-tests.log`: 73 passed, 0 failed | Does not emulate display, radio, TLS, touch or speaker hardware |
| Earlier ESP32 build output | Prior `firmware.bin` and `firmware.elf`, binary size 1,173,584 bytes, predate the SMB label changes | File evidence does not prove successful upload or complete physical validation |
| Live v3 authentication | Approved probe exchanged saved subject token for a JWT using validated HTTPS | Does not exercise expiry over time on the ESP32 |
| Live v3 data queries | All three collections returned HTTP 200 with valid status/result arrays; AAPS snapshot and SGV date/value fields present | Point-in-time compatibility check |
| Baseline physical operation | User confirmed earlier firmware worked on Core2 and supplied working screen images | Not independent confirmation of every current feature |

The live entries response contained 36 records / 2,431 bytes; device status two
records / 7,142 bytes with a matching OpenAPS snapshot; treatments 28 records /
6,852 bytes. Counts describe one approved check, not a fixed expected result.
No credentials, private URLs or clinical values were displayed or saved by that
probe. The bundled CA and hostname checks succeeded. A generic Python user-agent
first received 403; repeating with ESP32HTTPClient succeeded, and firmware now
explicitly uses that user-agent on auth and data requests.

The observed host log now contains 79 passed, zero failed, covering the SMB
layout additions. The current suite has 88 checks: nine additional checks cover
stale episode acknowledgement, independent sources, recovery, mute and the
strict ten-minute boundary. The readability/stale-sound changes still require
compilation and device validation. The compiler launch was attempted again and
failed with Windows Illegal System DLL Relocation in this session. Use the local
Flash-Core2.cmd to run all 88 checks before building/uploading. Earlier binary
evidence above predates these changes.

## Pinned build components

| Component | Selection |
| --- | --- |
| PlatformIO platform | espressif32 6.12.0 |
| Board/framework | m5stack-core2, Arduino ESP32 2.0.17 |
| Framework package observed | 3.20017.241212+sha.dcc1105b |
| Xtensa compiler | GCC 8.4.0, esp-2021r2-patch5 |
| M5Unified / M5GFX | 0.2.28 / 0.2.35 |
| ArduinoJson | 6.21.5 |
| HTTPS | Framework HTTPClient and WiFiClientSecure |

Core2 v1.3 uses AXP192, a 320x240 LCD and CH9102 USB serial. PSRAM is enabled.
IMU/microphone initialization is disabled; the internal speaker is enabled for
production chirps and disabled in parser-test firmware. Hardware initialization
is handled by M5Unified. APIs used for speaker/touch, power status/ADC reads,
Preferences, timezone and HTTP handling were checked against installed sources.
Earlier baseline production and parser-test environments compiled successfully;
their old image/RAM figures are not presented as measurements of current firmware.

## What the 73 host checks cover

- SGV numeric validation, timestamps, sorting, duplicate samples and stale limits.
- Object/array IOB, zero values, COB, pump/base/temp basal timing and uploader selection.
- Empty/invalid roots and production JSON filters.
- Fixed response-buffer chunk writes, exact bounds, overflow and allocation failure.
- Treatment types/times, invalid/deleted/future/extended records, public IDs,
  SMB pump/exact-time deduplication, filtering and bounded newest-event retention.
- Forecast arrays, invalid-sample gaps, whole-run selection, future/stale/absent
  calculations and sample bounds.
- Alert first occurrences, no high repeats, low timer/re-arm/wrap behavior,
  stale suppression, pending tones and persistent-setting state-model behavior.
- Muted/re-enabled alert behavior and power-input classification.
- V3 envelopes, canonical dates, tombstones, legacy-array separation, JWT response
  validation and renewal boundaries.

Persistence is tested as state behavior on the host, not actual ESP32 NVS writes.
HTTP 401 replay/cooldown and real JWT renewal are reviewed implementation paths;
they were not simulated by a full transport mock. The host shim replaces only
Arduino string-copy and serial interfaces. Geometric bounds were also checked
for the 320x240 layout; actual touch calibration and legibility require hardware.

The local Flash-Core2.cmd compiles/runs this suite before building/uploading. The
portable PlatformIO project also provides `parser-tests` for on-device fixtures;
that environment does not connect to Wi-Fi, play tones or alter mute preferences.

## TLS roots and source checks

| Root | DER SHA256 |
| --- | --- |
| ISRG Root X1 | 96bcec06264976f37460779acf28c5a7cfe8a3c0aae11a8ffcee05c0bddf08c6 |
| GTS Root R4 | 349dfa4058c5e263123b398ae795573c4e1313c83fe68f93556cd5e8031b3c7d |

The first root came from Let's Encrypt's official repository. The Google root
was extracted from installed certifi and checked against Google's publication.
The bundle parsed as two CA certificates. No insecure TLS fallback exists.

- [ISRG Root X1](https://letsencrypt.org/certs/isrgrootx1.pem)
- [Google published root fingerprints](https://pki.goog/repo/cps/5.14/GTS-CPS.html)
- [Core2 v1.3](https://docs.m5stack.com/en/core/Core2_v1.3)
- [AXP192 register definitions](https://m5stack.oss-cn-shenzhen.aliyuncs.com/resource/docs/datasheet/core/AXP192_datasheet_cn.pdf)
- [Nightscout v3 specification](https://github.com/nightscout/cgm-remote-monitor/blob/master/lib/api3/swagger.yaml)
- [Nightscout authorization](https://github.com/nightscout/cgm-remote-monitor/blob/master/lib/authorization/index.js)
- [AAPS pump status serializer at inspected revision](https://github.com/nightscout/AndroidAPS/blob/aa16db64196e570b15bf41e85422fc159c058308/implementation/src/main/kotlin/app/aaps/implementation/pump/PumpStatusProviderImpl.kt)

## Current device checks still needing confirmation

Compare v3 values, timestamps, treatment markers and projections with Nightscout;
check time/units/arrows and dense labels; verify B/C and speaker touch handling;
confirm mute persists; test low/high episode timing and audibility; check USB
unplug/reconnect and Wi-Fi icons; confirm activity and identical-poll redraw behavior.
Verify stale suppression after network loss, recovery, token renewal/revocation,
CA/hostname failures and bounded memory behavior during a sustained powered run.
Do not infer that these passed from a successful host suite or a generated binary.
