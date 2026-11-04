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

Validation: previous 104 C++ host checks passed in the saved workstation log;
36 new cases bring the current suite to 140 but remain unrun because the compiler
cannot launch in this session. Portable helper Python tests and embedded portal
controller Node checks pass. No fresh firmware binary is included or claimed.
Run the fixtures/build locally and complete the device checks in VERIFICATION.md
before treating this candidate as validated on hardware.
