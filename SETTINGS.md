# Quick settings and Wi-Fi setup

Press bottom-center button B, shown with a cog icon. Back, A or B returns to
the dashboard. The C forecast shortcut is inactive in the menu. The top-bar
speaker remains at x174..199, y0..27; the clock retains seconds.

## Saved settings

History (1, 3, 6 or 12 hours), units, forecasts and sound persist across reboot.
Saved choices override config.h defaults. The dashboard C forecast shortcut also saves
its choice; units are changed inside this menu. Invalid saved timeframes fall back to a supported config
default or three hours. Saving failures are reported on serial; session values
remain active.

A timeframe change requests a poll. The worker uses one timeframe for all
requests in that cycle. Longer history fills after a successful fetch. The
288-entry cap supports 12 hours of ordinary five-minute SGVs (144 readings).
Faster/duplicate-heavy data can reach the cap; SGV history limited warns that
older readings may be missing. Treatments retain the existing 200 fetched /
128 retained bounds and limit warnings.

## Wi-Fi

Reconnect saved Wi-Fi retries the saved network, or config.h when none is saved.
It waits for the current fetch to finish. The menu shows the connected SSID or
Disconnected.

To change networks:

1. Choose Wi-Fi setup and wait for any current Nightscout fetch to finish.
2. On your phone, join the Core2-Setup-xxxx hotspot using the random password
   shown on the Core2. Stay connected despite its no-internet warning.
3. Open http://192.168.4.1 if the captive page does not open automatically.
4. Enter the exact 2.4 GHz SSID and password and choose Test and save. Passwords
   may be 8-63 characters; an empty password selects an open network. Enterprise
   Wi-Fi and 64-character raw WPA keys are unsupported.
5. A connection test allows up to 20 seconds. Credentials replace the previous
   saved record only after successful association and a successful save. Failure
   allows retry from the phone. This tests Wi-Fi, not Internet or Nightscout.
6. Success closes the hotspot and resumes Nightscout polling. Reconnect the phone
   to its usual network. Back/button A or B cancels; setup times out after five minutes.
   Cancel/timeout restores the previous credentials. Cancellation can take until
   the current network attempt yields.

Nightscout fetching pauses during hotspot setup. Cached readings keep aging and
enabled alerts continue. Normal polling continues while viewing other settings.
The hotspot is password protected and permits one client. Setup requests are
accepted only through its AP interface; DNS/HTTP servers stop when setup ends.
The form uses local HTTP within the protected Wi-Fi network. Credentials are
not logged or put in URLs. The Nightscout URL/token remain in config.h and are
not exposed by this page.

Wi-Fi credentials are stored as one bounded record in the ns-wifi Preferences
namespace, separately from display settings in ns-display. Ordinary reflashing
preserves them. Erasing all flash clears saved settings; re-upload firmware to
return to config.h defaults. Device flash has no application-level encryption.
