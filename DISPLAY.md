# Graph focus display and controls

Current layout: 2026-11-04, 320×240 landscape.

## Layout

The header retains FreeSans Bold 18pt glucose (25-pixel digits, baseline y43),
a filled trend arrow, units, speaker/power/Wi-Fi and HH:MM:SS clock. BG age is
gray when fresh and APS age blue; missing/stale ages are amber. Missing ages show
--. The upper-right header shows No recent loop when AAPS is missing/stale.

One compact legend row at y52 combines C g, SMB U, Bol U and only the available
forecast curves. Dividers frame it at y49/60. Plot starts at y62 and normally ends
at y162: 101 pixels high, about 25% taller than the previous 81-pixel graph.
If measured SMB/manual-bolus labels need a second insulin row, it ends at y152:
91 pixels high. Carb and insulin rows remain separate; individual markers stay.

Treatment amounts sit below the plot: carbs at bottom+2, insulin at bottom+13,
with bottom+24 only when needed. Time labels remain at y184, divider y192.
IOB/COB values use FreeSans Bold 12pt, right-aligned at x124/x291, baseline y211;
small gray labels/units sit at y201. Basal and right-aligned Mode remain at y216.

## Controls

| Control | Action |
| --- | --- |
| A, green circular arrow | Refresh; Back in settings |
| B, cog | Open/close settings; units are configured there |
| C, solid/dashed projection icon | Toggle forecasts; cyan enabled, gray off |
| Speaker, x174..199 y0..27 | Toggle all audible alerts; saved across restart |

Footer icons occupy y229..239, outside the x278..319 activity rectangle. Errors
and treatment-limit warnings temporarily replace hints; physical controls remain
available. C is inactive inside settings. See SETTINGS.md for the LAN portal and
Wi-Fi submenu. The clock keeps seconds; there is no top-bar cog.

## Graph scale and color

The axis follows SGVs inside the selected history window. Padding is at least
10 mg/dL or 10% of the observed range, with a minimum 60 mg/dL span. Bounds round
outward to tens and stay nonnegative. Empty history uses 70..180. There is no
fixed 40/250 baseline. mg/dL bounds show whole numbers; mmol/L shows one decimal.
Labels at 70/180 transitions appear when space permits without crowding bounds.

Background: dark red below 70, green from 70 through 180, orange above 180 through
249 and red above 249. Only visible portions are shaded. Historical glucose is a
solid per-value colored trace; missing-data gaps remain. The latest point is
slightly larger. Fresh trace colors use the configured low/high thresholds;
stale history is gray. Empty history shows No glucose in this window.

Forecasts are dashed and do not expand the SGV-derived scale. Off-scale portions
are clipped, with CLIP in the header and an asterisk beside the forecast legend.
The green refresh shortcut remains visible unless a higher-priority data error
or treatment-limit warning occupies the footer. Forecasts disappear if either
SGV or the calculation is stale.

## Clock, status and redraw

Timezone defaults to Eastern with US DST and can be changed in the portal.
API timestamps remain UTC. Clock dashes indicate no valid UI time anchor yet.
Power shows approximate battery percentage or USB/EXT; a plus indicates charging.
Battery <=20% is red without external supply. AXP192 input flags, >=4 V and
>1 mA incoming current distinguish usable external power, including a full battery.
Failed readings show unknown; a USB wall adapter cannot be distinguished from a PC.
Wi-Fi bars reflect RSSI; a red cross means disconnected. Both sample once a second.

Activity: cyan GET/spinner while fetching, brief green UPD after a successful
snapshot, brief orange ERR after errors, dim dot while idle. It updates its own
42×11 rectangle. Data freshness depends on timestamps, not HTTP success.

The dashboard redraws only for changed content, visible freshness/expiry/window
changes or settings. Clock, ages and small indicators update independently.
The graph does not continuously scroll between full redraws. Runtime profile
changes force a redraw; changed Nightscout identity clears old cached data.

IOB/COB/basal age from their calculation/pump timestamps, not upload time. Temp
basal expires from reported remaining duration. Suggested rates/extended boluses
are not added to basal. Mode is uploaded pump status, not an inference from an
OpenAPS suggestion. Missing/stale values and mode remain explicit.
