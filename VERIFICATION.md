# Verification record

Reviewed 2026-11-04 for the Graph focus / LAN portal source candidate.

## Current evidence

| Area | Result | Limit |
| --- | --- | --- |
| Previous C++ host suite | Latest saved workstation log: 104 passed, 0 failed | Predates this candidate |
| Current C++ suite | 140 checks prepared; 36 new runtime/access cases | Cannot execute compiler here; new cases unrun |
| ESP32 production build attempt | Pinned platform/libraries resolved; compiler and object converter could not execute | No new firmware binary or compiler diagnostics for this source |
| Portable flash helper | 14 isolated Python tests passed | Does not invoke a real device/compiler in these tests |
| Embedded portal controller | Node checks passed against the actual embedded script | DOM/HTTP mocks; does not test the ESP32 HTTP server/radio |
| Source audit | Delimiter balance, portal IDs/references, pinned header APIs checked | Not a substitute for C++ compilation |

The host compiler launch fails with Windows Illegal System DLL Relocation in this
session. PlatformIO reaches compilation but Windows cannot execute the compiler
or PEM object converter. Run the local Flash-Core2.cmd to compile/run all 140 host
fixtures before building/uploading. The portable release helper runs a normal
PlatformIO production build; desktop fixture checks are separate.

Older .bin/.elf files and older successful build figures are historical. They
do not validate this candidate and are not bundled. No live Nightscout request
was made as part of this update. Earlier user-approved v3 checks established
point-in-time compatibility: auth succeeded, entries/device-status/treatments
returned HTTP 200, and validated TLS succeeded with the included CA bundle.

## New regression cases

Runtime profile version, terminated strings, HTTPS URL restrictions, numeric and
boolean types, history/poll/freshness/brightness limits, ordered thresholds,
blank-token preservation, changed-URL credential isolation, blank-password preservation, explicit open networks,
duplicate/empty Wi-Fi lists, ordered secret association, bounded hotspot network
promotion, public JSON secret exclusion, embedded NUL rejection and nonmutating
patch parsing. Access cases cover initial lock, five-attempt cooldown, cookie
matching, inactivity renewal/expiry, counter wrap and revocation. A custom
threshold case checks production glucose-alert inputs.

Portal browser-controller checks exercise initial/incorrect/correct login,
clearing entered codes, blank secret payloads, network reordering/new network
entry, CSRF header forwarding, stale-save errors, lock cleanup and same-origin /
no-store requests. No real password/token or private URL appears in test fixtures.

Existing parser tests cover SGV/status/treatment/forecast validation, bounded
HTTP buffers, timestamp handling, duplicate/tombstone rules, stale state, SMB
label grouping, graph scale/target intersection, glucose/stale episodes, power
classification, v3 envelopes and JWT checks. Host shims replace Arduino serial
and string-copy facilities, not the parsing/state models. Fixture firmware now
uses a dedicated 49 KiB task to avoid placing parser history temporaries on the
small Arduino loop stack. It starts no Wi-Fi, tones or saved-preference writes.

## Compatibility and storage review

Pinned: espressif32 6.12.0, m5stack-core2, Arduino ESP32 2.0.17, M5Unified 0.2.28,
M5GFX 0.2.35, ArduinoJson 6.21.5. WebServer/DNSServer/Preferences/Wi-Fi/TLS and
FreeRTOS come from that framework. PSRAM remains enabled. No new external
library is needed. Core2 v1.3 uses AXP192, CH9102 USB serial and 320×240 LCD.
Installed headers confirm baseline_right, FreeSansBold12pt7b, collected-header
array API, AP lifecycle and client local-IP methods used by the implementation.

Main UI, network worker and portal task use separate settings copies. A mutex
protects complete versioned NVS profile commits with revision checks; a network
gate serializes Wi-Fi changes and HTTPS connection tests against polling. Old
Nightscout-identity snapshots are discarded, and JWT/range episodes reset on
identity changes. Large snapshot resets construct in place rather than on the
small UI stack. Portal JSON documents prefer PSRAM to reduce competition with TLS internal
heap allocations. These paths require runtime/heap validation on the device.

The LAN service starts disabled after boot and runs on port 3000 only when
requested with a usable station connection. AP recovery pauses it. The current
IP, same-origin writes, session cookie and CSRF token gate configuration access.
Public profile serialization returns no stored token/password values. JSON
request parsing checks an 8 KiB body and nesting limit eight; the framework's
HTTP parser receives the request before that application check. This is a small
trusted-LAN service, not an Internet-facing appliance. Traffic is local HTTP;
NVS and configured firmware have no application-level credential encryption.

## Display geometry review

Graph y62..162 normally, y62..152 when a second insulin row is placed. The compact
legend fits y52..59 between y49/60 dividers. Carb/insulin labels end by y183;
time labels y184..191, divider y192, metric values baseline y211 and basal y216.
Metric labels are at y201. Refresh/settings/forecast hints fit y229..239 outside
the x278 activity rectangle. Threshold labels are suppressed near scale bounds.
The graph retains per-value trace colors, visible range bands and clipped dashed
forecasts; CLIP/asterisk warns without occupying the normal shortcut footer.
Actual font metrics, label collisions and treatment density need screen review.

## TLS roots

| Root | DER SHA256 |
| --- | --- |
| ISRG Root X1 | 96bcec06264976f37460779acf28c5a7cfe8a3c0aae11a8ffcee05c0bddf08c6 |
| GTS Root R4 | 349dfa4058c5e263123b398ae795573c4e1313c83fe68f93556cd5e8031b3c7d |

The root bundle is unchanged. HTTPS chain/hostname/time validation and redirect
rejection remain enabled. JWTs remain in RAM; the saved read-only subject token
is in the private runtime profile. No response bodies or secrets are logged.

## Remaining device acceptance

Compile and run all 140 C++ fixtures, then build production. Confirm migration
from existing settings, normal/reboot preference persistence and portal OFF after
restart. Check the address/code display, login limits, session expiry, secret
preservation, API rejection of stale revisions/incorrect origin/CSRF and closure
when disabled. Exercise URL/token/API changes without old data reappearing.

Test primary unavailable => backup connection, priority changes, all networks
unavailable => recovery hotspot, failed/canceled hotspot preserving the list,
successful promotion, reconnect/IP changes and portal restart. Test busy HTTPS
validation and sustained concurrent portal/polling memory use. Verify Graph
focus at light/dense treatment loads, both units, threshold labels, forecasts,
stale warning, metric fonts and green refresh icon. Check actual audio/mute and
low reminder timing, stale data/recovery, USB/battery/Wi-Fi and partial redraws.
