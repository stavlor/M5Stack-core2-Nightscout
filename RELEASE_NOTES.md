# Nightscout Core2 source release — 2026-11-03

Firmware source for M5Stack Core2 v1.3 with validated HTTPS Nightscout v3 access,
AAPS data, glucose history/forecasts, treatment markers, audible alerts, automatic
graph scaling and color bands, and saved quick settings with phone-based Wi-Fi
setup.

Included in the ZIP:

- Complete PlatformIO project with pinned dependencies and MIT license.
- Portable Flash-Core2.cmd and cross-platform Python flash.py tool.
- include/config.example.h, TLS root bundle, setup and feature documentation.
- 104 firmware fixture checks and 14 portable flashing-tool tests.
- Per-file SHA-256 manifest.

Extract, open RELEASE.md, create/edit your private config.h, then run the helper
to build and upload. Python and PlatformIO are required; first build downloads
the toolchain/dependencies. No private credentials or precompiled firmware are
included. The artifact is suitable for attaching to a GitHub release; it has
not been published automatically.

Validation: 104 firmware checks passed in the latest observed workstation log;
14 isolated Python release-tool tests passed during packaging. ZIP integrity,
file hashes and credential exclusions were checked. Firmware compilation and
physical setup-flow testing remain separate requirements; see VERIFICATION.md.
