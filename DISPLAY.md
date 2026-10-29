Latest Wi-Fi indicator: four compact signal bars now sit between power status and the clock. Green indicates stronger signal, yellow/orange weaker signal, and a red cross indicates Wi-Fi disconnected. Connected devices with an unavailable RSSI show gray bars. Signal is sampled once per second; only the icon redraws when its connection state or bar count changes. Wi-Fi association does not prove Nightscout/API connectivity; existing endpoint errors still provide that status.

The Core2 v1.3 has a built-in speaker and NS4168 I2S amplifier, documented at https://docs.m5stack.com/en/core/Core2_v1.3 . Speaker initialization remains disabled and no audible alerts were added in this revision; sound can be enabled in a future alert feature. Wi-Fi status/RSSI signatures were checked against the pinned Arduino ESP32 library. Build and physical icon checks remain pending through Flash-Core2.cmd.

Latest power indicator: a small battery icon now appears to the left of the upper-right clock. On battery it shows an approximate percentage, with red below or at 20 percent. External USB power shows green USB (including a USB wall adapter), or EXT for the AXP192 separate external-input flag. A plus sign means actively charging; externally powered but fully charged still shows USB/EXT without the plus. Unknown/absent battery shows an unfilled icon and an unknown percentage when no external power is detected. The hardware cannot distinguish a USB wall adapter from a USB computer port.

Power is sampled every five seconds independently of the clock and Nightscout. Only the indicator rectangle redraws when its displayed state changes. The AXP192 input, battery-presence and charge APIs were checked in the pinned M5Unified source. Rebuild and physical USB/battery checks remain pending through Flash-Core2.cmd.

Latest space allocation: glucose and trend use 4x text instead of 3x. The plot extends from y=72 to y=160 (88-pixel span, formerly 56), using the freed lower status area. Time labels are at y=184, IOB/COB at y=194, basal/right-aligned Mode at y=216, and footer at y=231. Clock and age positions remain at the upper right. Marker/forecast clipping and threshold placement use the enlarged plot bounds. Physical layout verification and the firmware build remain pending through Flash-Core2.cmd.

Latest layout: the upper-right age row now shows glucose Age in gray on the left and AAPS calculation Age in bright blue on the right, above the forecast area. The separate AAPS Suggested/status-age row is removed. Mode is right-aligned on the basal-rate row, with long mode text truncated to prevent overlap. These changes supersede older status-row descriptions below. The updated firmware build and physical layout check remain pending through Flash-Core2.cmd.

# Display refresh and clock

The full dashboard now redraws only when its data/error payload changes, a
visible freshness/expiry state changes, a history item enters/leaves the graph
window, or the user toggles units/forecasts. Identical polls no longer trigger
full repaints. The graph does not scroll/repaint just because the clock ticks.
Polling frequency and stale-data thresholds remain configured as before.

The upper-right clock displays HH:MM:SS. Only its small rectangle updates each
second. Glucose and AAPS age labels update their own rectangles when their text
changes, normally once per minute. Staleness and temp-basal expiry still update
even if no new server data arrives. The clock shows dashes before time sync.

Timezone is configured in include/display_settings.h. Its default is Eastern
America/New_York, using EST5EDT,M3.2.0/2,M11.1.0/2 for automatic US daylight-saving
time. Set TIMEZONE to UTC0 for UTC, or to another POSIX TZ rule if needed. API
and freshness calculations remain based on UTC epoch timestamps.

Changed-payload comparisons exclude poll clock anchors, struct padding and
unused array capacity. NaNs and signed zeros are normalized. The fingerprint is
only a display invalidation optimization, not a security check.

Verification: configTzTime and localtime_r APIs were checked against the installed
ESP32 Arduino framework. The existing 29 pre-flash regression checks remain in
the local flash script. This session cannot run the compiler; the updated firmware
build and physical verification remain pending through Flash-Core2.cmd.

After upload, verify that the graph stays still while seconds advance, identical
polls do not flash the screen, B/C still redraw immediately, and old data becomes
stale on schedule after network loss. Check the displayed local time against a
clock set to Eastern time.
