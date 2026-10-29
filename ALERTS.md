# Glucose chirps and mute control

The built-in speaker plays one non-blocking 180 ms, 1800 Hz chirp at volume
80/255 for a fresh out-of-range SGV, when alerts are enabled.

| Glucose | Behavior |
| --- | --- |
| Below 70 mg/dL | First-occurrence chirp; repeat every 15 minutes while fresh and low |
| 70 through 180 inclusive | Silent; re-arm the next out-of-range episode |
| Above 180 mg/dL | First-occurrence chirp; no repeat while high continues |

A direct high-to-low or low-to-high transition chirps for the newly entered band.
The sound thresholds are mg/dL regardless of display units. They are defined in
`include/alerts.h`, separately from the display thresholds in config.h. That
header also holds volume, tone length/frequency and low repeat interval.

## Speaker touch toggle

Tap the speaker icon just left of the power indicator. Cyan/blue means enabled;
a red slash means muted. Muting stops an existing tone and blocks further chirps.
The enabled setting is stored in ESP32 Preferences (`ns-display`, `alerts-on`)
and restored after reboot; first use is enabled. If saving fails, serial reports
it and the current session's setting still applies.

Re-enabling re-arms the current episode: a fresh out-of-range reading chirps once
immediately, and low repeats start from that chirp. Episode/timer history stays
in RAM. After reboot with alerts enabled, a fresh out-of-range reading is a first
occurrence and chirps once. Unit and forecast button preferences are not persisted.

## Freshness and timing

Only the latest actual SGV is used. Forecasts, missing/invalid readings and stale
data do not chirp. Default SGV freshness is ten minutes. A failed fetch does not
automatically invalidate a still-fresh cached sample; it keeps aging normally.
A stale period/reconnect does not re-arm an already-announced high. A low reminder
due while stale waits for fresh low data.

Low timers use monotonic milliseconds, independent of NTP/timezone and safe
across counter wrap. An alert is consumed only when the speaker accepts the tone.
Failed requests retry at most every 30 seconds while the triggering reading is
fresh. Parser-test firmware disables the speaker and does not change saved mute.

These chirps supplement the secondary display and do not replace CGM/AAPS alerts.
Current test/build evidence and remaining sound/touch checks are recorded in
[VERIFICATION.md](VERIFICATION.md).
