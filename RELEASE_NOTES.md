# Core2 Nightscout dashboard — 2026-11-04

Graph-focused display, LAN configuration portal and TLS reliability update.

## Display and controls

- Larger graph area, compact forecast legend, dynamic glucose scale and shaded
  target/high/low ranges. Stronger IOB/COB values and explicit stale-loop status.
- Treatment markers retain their timestamps; crowded SMB amounts use additional
  label space or combined totals rather than overwriting one another.
- Green refresh icon on A, settings on B and forecast visibility on C.
- Clock, power, Wi-Fi and touch-toggle speaker status. Audible glucose and
  missing-update alerts respect the saved sound setting.

## Configuration

- Optional LAN configuration portal on port 3000, off after every restart.
  Enable it in Settings and use the displayed address and device access code.
- Configure Nightscout URL/read-only token, API mode, uploader, polling,
  display/alerts, time zone/NTP and up to five prioritized Wi-Fi networks.
- Expiring sessions, rate-limited login and same-origin/CSRF checks. Stored
  passwords/tokens are not returned to the browser; blank secret fields retain
  existing values. Existing device settings migrate and survive normal uploads.
- Nightscout v3 remains the primary data API, with the documented legacy option.

## TLS reliability

- Replaces the limited CA list with 118 Mozilla/certifi 2026.10.23 public roots
  in a compact indexed store. Source, license, generator and checks are included.
- Recognizes cross-signed trust anchors by exact trusted subject/public-key
  identity. Date, hostname and usage checks remain enforced.
- Device diagnostics identified an intermittent big-number allocation failure
  during certificate signature verification. TLS now prefers the Core2's PSRAM,
  with internal-memory fallback, instead of the SDK's internal-only policy.
- Startup reports TLS memory mode. Failure diagnostics include verification
  flags, public certificate fingerprints and heap figures, without credentials
  or clinical values. No insecure TLS fallback is enabled.

## Installation

Download and extract the source ZIP. Follow RELEASE.md: install Python 3.9+ and
PlatformIO, then run Flash-Core2.cmd on Windows or python3 flash.py elsewhere.
The first run creates include/config.h from the example without overwriting an
existing configuration. Configure locally and run again to build and upload.
The first build requires internet access to download pinned dependencies.

Includes full PlatformIO source, portable flash helpers, example configuration,
documentation, tests, CA data and SHA-256 manifests. Private configuration,
configured firmware binaries, build caches and logs are excluded. This is a
source release, not an M5Burner image or standalone offline flashing package.

## Validation and remaining limits

- The preceding trust-adapter build passed 153 C++ fixtures on the user's PC.
- The user successfully built/flashed the PSRAM allocator update and reported a
  substantially faster successful initial fetch. Extended stability testing is
  still in progress; intermittent TLS failures are not yet proven eliminated.
- Offline CA tests, independent Node/OpenSSL root-key checks, portable flash-tool
  tests and embedded portal-controller tests pass. They do not emulate hardware.
- The optional LAN portal is intended for the same LAN; do not expose port 3000
  through an internet-facing router rule.
