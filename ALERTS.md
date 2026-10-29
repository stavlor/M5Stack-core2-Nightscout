# Glucose chirps

The Core2's built-in speaker now plays one short 180 ms, 1800 Hz chirp at volume
80/255 when a fresh glucose reading is below 70 or above 180 mg/dL. Thresholds
are evaluated in mg/dL internally, independently of the displayed glucose unit.
Exactly 70 and 180 are in range.

Low: chirp at first occurrence, then every 15 minutes while fresh readings remain
low. High: chirp at first occurrence with no repeat while the high continues.
Returning to 70-180 re-arms the next out-of-range episode. A direct transition
from high to low or low to high chirps for the newly entered band.

Only actual current SGV is used. Forecasts, missing values, invalid sensor values
and stale data never trigger a chirp. A Wi-Fi outage or stale period does not
re-arm a high episode. If a low reminder becomes due while data is stale, it
waits for a fresh low reading. The timer uses a monotonic counter, independent
of timezone, NTP adjustments and counter wrap. Episodes reset after a reboot;
a fresh out-of-range reading after boot therefore chirps once.

The tone is non-blocking. A failed tone request does not consume the alert;
failed requests retry at most every 30 seconds while the triggering data remains
fresh. Speaker initialization remains disabled in the parser-test firmware.

Constants for volume, tone length/frequency, thresholds and low repeat interval
are in include/alerts.h. The screen behavior and A/B/C buttons are unchanged.

Verification: Speaker.tone and setVolume signatures and the internal speaker
configuration were checked against the pinned M5Unified source. Added state
fixtures for first highs/lows, no high repeats, range re-arming, the exact
15-minute boundary, reminder reset, fresh/stale behavior, direct band transitions,
invalid data, unacknowledged tones and millis wrap. These run automatically in
Flash-Core2.cmd before firmware build/upload. Their new execution and physical
tone/volume verification remain pending on the user's PC; this session cannot
launch the compiler. No audible hardware test was performed here.

After flashing, check audibility at the chosen volume and verify episode/repeat
behavior against actual timestamped readings. This supplements the existing
secondary display; it does not replace the CGM/AAPS alert system.
