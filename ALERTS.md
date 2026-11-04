# Glucose and missing-update sounds

The master speaker toggle (dashboard touch icon or settings) persists in the
runtime profile and controls both sound types. Cyan means enabled; red slash means
muted. Muting stops queued/current tones. Re-enabling re-arms the current glucose
episode. Volume defaults to 80/255 and can be changed in the LAN portal.

## Glucose-range chirp

A fresh actual SGV below 70 or above 180 mg/dL defaults to a single non-blocking
1800 Hz / 180 ms chirp. Low/high thresholds can be changed in the portal; they
also control fresh trace/value colors. Defaults come from config.h, initially
70/180. Thresholds always use mg/dL even when the display shows mmol/L.

- Below low: first occurrence, then every fifteen minutes while fresh and low.
- At either boundary or between them: silent and re-arm the next episode.
- Above high: first occurrence only; no repeating high reminder.
- A direct high-to-low or low-to-high transition chirps for the newly entered band.

Only the latest actual glucose sample is used. Missing, stale, invalid readings
and forecasts do not produce range chirps. Fetch failure alone does not invalidate
fresh cached SGV. A stale/reconnect period does not re-arm an announced high;
a due low reminder waits for fresh low data. Changed thresholds or a changed
Nightscout identity re-arm the range episode; switching sites clears old readings.
Timers use monotonic milliseconds and handle wrap. A tone is consumed only after
the speaker accepts it; failures retry at most once per thirty seconds.

## Missing-update sound

Actual SGV or the AAPS calculation timestamp strictly older than 600 seconds
triggers two descending notes: 950 Hz / 160 ms, about 140 ms silence, then
650 Hz / 160 ms. This differs from the single, higher glucose chirp. Each source
sounds once per stale episode, re-arming only when fresh data returns. Both
becoming stale together share one sound. There are no periodic stale reminders.

A source with no data gets a ten-minute startup grace. Received but already-old
data can alert immediately. Fetch errors do not trigger this while timestamps
remain fresh. AAPS uses calculation time, not upload time. This audio threshold
stays at ten minutes even if portal display-freshness settings are changed.

Sounds are non-blocking and serialized. Playback failure retries after thirty
seconds. Stale episodes are acknowledged after the second note is accepted.
The master toggle stops/suppresses them and re-arms notifications on enabling.
Cached values and alerts continue aging/running while recovery Wi-Fi setup pauses
polling; normal LAN portal viewing does not pause polls.

Episode/timer state is RAM-only and resets on reboot. Saved sound, volume, units,
forecasts and other preferences live in ns-runtime; old sound preferences migrate.
Fixture firmware disables the speaker and does not change saved preferences.
These sounds supplement the display and do not replace CGM/AAPS alerts.
See [VERIFICATION.md](VERIFICATION.md) for testing and hardware limitations.
