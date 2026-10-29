# Display and controls

Current behavior, 2026-10-29.

## Layout

- Large glucose value with filled graphical up/down/flat/diagonal/double arrows;
  unknown or uncomputable trends show `?`. SGV is mg/dL internally; mmol/L uses
  a conversion factor of 18.0182.
- Upper-right HH:MM:SS clock, plus speaker, power and Wi-Fi indicators.
- Gray glucose `Age` and blue AAPS calculation `Age` side by side above forecasts.
  There is no separate Suggested/status-age row.
- History and optional future pane, with treatment markers at the lower threshold.
- IOB/COB, then reported basal and right-aligned `Mode`. Long mode text is shortened.
- Footer A/B/C labels or prioritized error text, with data activity at the right.

Glucose uses 4x text. Plot bounds are y=72..152, time labels 184, IOB/COB 194,
basal/mode 216 and footer 231 on the 320x240 landscape display.

## Controls

| Control | Action |
| --- | --- |
| A, lower left | Request refresh |
| B, center | Toggle mg/dL / mmol/L until reboot |
| C, lower right | Toggle forecasts until reboot; initially enabled |
| Speaker icon left of power | Toggle glucose chirps; saved across reboot |

Speaker cyan/blue means enabled; a red slash means muted. Its touch area is
x174..199, y0..27. A press toggles once; holding does not repeatedly toggle.
Muting stops the current tone. Re-enabling evaluates the current fresh reading.
See [ALERTS.md](ALERTS.md).

## Clock and small indicators

The timezone is in `include/display_settings.h`: default
`EST5EDT,M3.2.0/2,M11.1.0/2` for Eastern time with automatic US DST. Use `UTC0`
for UTC or another POSIX rule as needed. API timestamps remain UTC epoch values.
Clock dashes indicate that the UI has not received a valid time anchor yet.

**Power:** approximate battery percentage when on battery; green USB/EXT when
that external input supplies power; plus sign while charging. Battery at or
below 20% is red without external power. Unknown input shows `?`; unavailable
battery level shows `--%` when on battery. USB wall adapters and computer ports
cannot be distinguished.

Core2 v1.3 uses AXP192 present/usable flags plus at least 4 V and more than 1 mA
incoming current. Charging alone is not used to classify external supply; a full
battery can still show USB. Failed status reads show unknown. The firmware only
reads PMIC status/ADC registers and does not alter routing/charging configuration.
Power is sampled once per second.

**Wi-Fi:** 1-4 bars, green for stronger and yellow/orange for weaker signal.
Disconnected is a red cross; connected with unavailable RSSI shows gray bars.
It is sampled once per second. Wi-Fi association does not prove Nightscout access.

## Activity indicator

| Indicator, lower right | Meaning |
| --- | --- |
| Cyan GET + spinner | Network polling cycle, including connection/fetch work |
| Green UPD + check | Completed successful snapshot applied; brief pulse |
| Orange ERR | Completed cycle reported an endpoint error; brief pulse |
| Dim dot | Idle between polls |

Only its 42x11 rectangle animates every 150 ms while busy. Completion pulses last
500 ms. A successful HTTP cycle does not itself establish freshness; the values'
own ages still apply. Long footer text is ellipsized to avoid overlap.

## Refresh, colors and status

The full dashboard redraws when data/error content changes, a visible freshness
or expiry state changes, a history item enters/leaves the window, or B/C changes
its setting. Identical polls do not repaint the graph. Clock seconds, age text,
power, Wi-Fi, speaker and activity update independent small rectangles. The graph
does not continuously scroll between full redraws.

Glucose points and interpolated segments are green in range, orange above and
red below display thresholds. Stale history is gray. Default freshness is ten
minutes; old SGV shows STALE with advancing age. Missing values show `--` and
unknown ages/mode are explicit. Valid empty results clear data; errors retain it.

IOB/COB and basal use calculation/pump timestamps, not upload freshness. Temp
basal requires a valid absolute rate and positive remaining duration; otherwise
reported base basal is shown. A temp expires at upload time plus reported
remaining minutes and becomes unknown until a new report. Suggested rates and
extended boluses are not added to basal. This is an uploaded snapshot, not proof
of current delivery.

Mode is the pump's uploaded `status.status` text. It is not inferred from an
OpenAPS suggestion; `normal` does not establish closed-loop mode. Stale/missing
pump mode shows Unknown/stale.

The LCD stays on. There is no Wi-Fi alarm or persistent health-data cache.
Treatments/forecasts are described in [OVERLAYS.md](OVERLAYS.md); glucose chirps
in [ALERTS.md](ALERTS.md). Verification status is in [VERIFICATION.md](VERIFICATION.md).
