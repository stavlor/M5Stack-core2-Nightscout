# Display and controls

Current behavior, 2026-10-31.

## Layout

- Large glucose value with filled graphical up/down/flat/diagonal/double arrows;
  unknown or uncomputable trends show `?`. SGV is mg/dL internally; mmol/L uses
  a conversion factor of 18.0182.
- Upper-right HH:MM:SS clock, plus speaker, power and Wi-Fi indicators.
- Gray glucose `BG` age and blue AAPS calculation `APS` age side by side above forecasts.
  There is no separate Suggested/status-age row.
- History and optional future pane, with treatment markers near the plot bottom.
- IOB/COB, then reported basal and right-aligned `Mode`. Long mode text is shortened.
- Footer A/B/C labels or prioritized error text, with data activity at the right.

Glucose uses FreeSans Bold 18pt at native size, with 25-pixel digit height
and a baseline at y=43. IOB/COB use native Font2 (16-pixel line height) in
fixed columns: IOB at x5, value right edge x124, U at x132; COB at x165,
value right edge x291, g at x299. Numeric precision remains two decimals
for IOB and whole grams for COB. Other text retains Font0. Plot bounds are y=72..152, time labels 184, IOB/COB 194,
basal/mode 216 and footer 231 on the 320x240 landscape display.

## Controls

| Control | Action |
| --- | --- |
| A, lower left | Request refresh |
| B, center | Toggle mg/dL / mmol/L until reboot |
| C, lower right | Toggle forecasts until reboot; initially enabled |
| Speaker icon left of power | Toggle all audible alerts; saved across reboot |

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

The compact legend shows Carbs g, SMB U and Bolus U. The header is subdued
and gridlines are dim gray so the solid glucose trace and dashed forecasts stand out.

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

## Additional clarity refinements

The header has fixed positions for the title, units and STALE warning, leaving
space for the top-right icons. Missing ages show --. BG and APS ages turn amber
when missing/stale; fresh APS remains blue. Changes at the freshness boundary
refresh their small rectangle even when the minute text has not changed.

The treatment legend includes the matching triangle/dot shapes. Y-axis bounds
are right-aligned and retain one decimal in mmol/L. The latest actual glucose
point is slightly larger than earlier points. Empty history shows an explicit
No glucose in this window message. Normal button hints are gray; footer errors
and treatment-limit warnings remain amber. IOB/COB labels and units are gray,
with values in white. Forecast curves remain dashed and historical glucose solid;
missing-data gaps and per-value glucose coloring are preserved.

## Automatic graph scale and target band

The Y-axis follows actual SGVs within the visible history window (three hours by
default). Padding is the greater of 10 mg/dL or 10% of the observed range, with
a minimum 60 mg/dL span, rounded outward to 10 mg/dL increments and a nonnegative
lower bound. There is no fixed 40/250 baseline. Empty history uses 70..180.
All calculations use mg/dL; axis labels convert to mmol/L when selected.

Subtle background bands follow the visible scale: dark red below 70 mg/dL,
dark green from 70 through 180, dark orange above 180 through 249, and dark red
above 249. Bands are clipped to the visible scale and extend through the forecast
pane. The classification uses mg/dL even when labels display mmol/L. Forecasts do not expand the SGV-derived scale; off-scale curves
are clipped and the legend shows FCAST CLIPPED. Treatment markers and their label
rows stay anchored at the graph bottom as the scale changes. Alert thresholds
and glucose trace colors retain their existing settings.
