# Graph focus and LAN configuration — 2026-11-04

Source candidate for M5Stack Core2 v1.3. This does not publish or replace the
existing GitHub release automatically.

- Graph focus: 101-pixel graph normally, 91 with an extra insulin label row;
  compact available-curve legend, threshold labels, stronger IOB/COB values,
  explicit stale-loop text and green refresh icon.
- Default-off LAN portal on port 3000, enabled from B > Settings. Device access
  code, rate-limited login, expiring cookie session and same-origin/CSRF checks.
- Runtime Nightscout URL/token/uploader/API, polling/freshness, display/alerts,
  timezone/NTP and five prioritized saved Wi-Fi networks. Secrets are never
  returned to the page; blank secret fields preserve saved values.
- Complete profile storage with existing settings migration, revision conflicts,
  old-site cache/JWT invalidation, ordered network fallback and retained recovery
  hotspot provisioning.

Bundle: complete PlatformIO source, example config, CA roots, documentation,
portable Windows/Python flash tools, fixtures and SHA-256 manifests. Private
config, caches, logs and configured firmware binaries are excluded.

TLS follow-up: the three-root change did not eliminate device verification
failures. This candidate uses all 118 public roots from Mozilla/certifi 2026.10.23
through the ESP32 indexed-bundle API, and records exact verification flags plus
UTC/internal heap on failure. No insecure fallback is enabled. The source PEM,
indexed CA data, metadata, generator and upstream license are included.

Validation: 153 C++ fixtures and the preceding adapter build passed on the user's
PC. Device diagnostics now show signature=-16 (MPI allocation failure), despite
finding the trusted issuer. This source candidate installs PSRAM-preferred TLS
allocation before worker startup, with internal fallback. Build/device validation
of that memory change is pending. Certificate checks remain required.
