Latest data activity indicator: the lower right, on the A/B/C footer baseline, now shows a cyan GET spinner while a network polling cycle is running. After the UI receives the completed snapshot, it briefly shows green UPD with a checkmark, or orange ERR when an endpoint reported an error. A dim dot denotes idle between polls. The spinner redraws only its small 42x11 rectangle every 150 ms; the graph stays unchanged. Footer messages are shortened with an ellipsis when needed to keep the indicator clear. Cross-task busy state uses a C++ atomic; all display writes remain on the UI task. The existing pre-flash checks remain unchanged; updated build and physical icon/timing verification are pending through Flash-Core2.cmd.

Power disconnect correction: source detection now requires AXP192 input-present AND usable status, at least 4.0 V, and measured input current above 1 mA. This replaces the original presence-only check, preventing a lingering/backfed voltage from being reported as USB supply. Fully charged operation does not require the charging flag. Power is now sampled once per second. Failed status-register reads show an unknown source rather than a retained USB label. Only PMIC status/ADC registers are read; power-routing and charging settings are not changed.

Register definitions were checked in the manufacturer datasheet hosted by M5Stack: https://m5stack.oss-cn-shenzhen.aliyuncs.com/resource/docs/datasheet/core/AXP192_datasheet_cn.pdf (REG00/REG01 and VBUS/ACIN ADC registers). ADC initialization was checked in pinned M5Unified (all ADC channels enabled). Added seven classification fixtures to the pre-flash suite. Those new tests, firmware build and physical unplug/reconnect verification remain pending on the user PC. After reflash, unplug USB, wait about one second and confirm the battery percentage appears; reconnect and confirm USB/USB+.

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
