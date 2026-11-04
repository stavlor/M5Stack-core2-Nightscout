# Quick settings, saved Wi-Fi and LAN configuration

Press bottom-center B (cog) to open settings. Back, A or B closes the menu;
inside the Wi-Fi submenu they return to the main settings page. C is inactive
while settings are open. The top speaker touch toggle remains available on the
dashboard. Display settings save automatically.

## Device menu

- Graph history: 1, 3, 6 or 12 hours; a change requests a new poll.
- Sound: master toggle for glucose-range and missing-update sounds.
- Forecast: enable/disable the future pane.
- Units: mg/dL or mmol/L.
- Wi-Fi setup: reconnect saved networks or start a temporary recovery hotspot.
- LAN portal: start/stop configuration service on TCP port 3000.

## LAN portal: off by default

1. Connect the Core2 to Wi-Fi, then choose B > LAN portal: On.
2. Open the exact http://IP-ADDRESS:3000 address displayed on the Core2 from
   another device on that LAN. The example address in previews is not your IP.
3. Enter the eight-digit access code shown underneath the address.
4. Edit Nightscout, Wi-Fi, or display/alert settings and choose Save settings.
   Saving is explicit; Test connection does not save.
5. Turn LAN portal Off on the device when finished. Closing the settings screen
   leaves an enabled portal running. Every restart turns the portal off again.

The service runs separately from polling. It stops listening when disabled or
Wi-Fi is unavailable, and pauses while the recovery hotspot is active. Wi-Fi/IP
changes or service restarts can produce a new code and require a new login.
A current request may need to finish before its listener stops.

### Available configuration

Nightscout: canonical HTTPS URL, read-only subject access token, uploader filter,
v3/default or explicit v1 mode, polling interval (30–900 seconds), and SGV/AAPS
freshness (1–60 minutes). Blank token input preserves the saved token for the same URL. A changed URL
requires explicitly entering the token for that address; an old credential is
never silently carried to another destination. The actual
token is never returned to the browser. Test connection validates TLS, auth and
entries access; normal dashboard polling verifies the other endpoints. If a poll
is running, testing returns a busy message; try again after the poll finishes.

Wi-Fi: up to five exact SSIDs in priority order, passwords, explicit open-network
selection, reorder and remove. Blank password preserves an existing network's
secret; a new protected network requires an 8–63-character password. New open
networks require the Open network checkbox. SSIDs are limited to 32 bytes.
Enterprise networks and 64-character raw WPA keys are unsupported. Stored
passwords are never sent back. The active connection is marked in the list.

Display/alerts: history, units, forecasts, master sound toggle, low/high alert and
trace-color thresholds, volume, brightness, POSIX timezone and two NTP hosts.
The background bands remain fixed at 70/180/249 mg/dL. Missing-update audio stays
at the fixed ten-minute limit independently of displayed freshness settings.
TLS verification stays enabled; the Mozilla CA snapshot still requires a reviewed source update/rebuild for
new trust anchors or a private CA. See certs/README.md.

Saving a stale browser form is rejected if device settings changed after it was
loaded; Reload saved settings before retrying. Values are validated before one
complete profile replaces the prior profile. A failed save does not activate it.
Changes to Nightscout URL/token/uploader/API invalidate old cached data and JWTs.

### Network fallback

After disconnection, the network task tries saved networks in order, allowing
approximately ten seconds per network. It keeps a working backup connected;
it does not interrupt it to probe the primary on each poll. Reconnect saved Wi-Fi
retries from the top. This can change the IP and interrupt the browser connection.

Adding/reordering Wi-Fi in the LAN portal saves it for the next reconnect. Save
does not test association or immediately disconnect the working network. Keep a
working backup. If all credentials fail, use the physical recovery hotspot.

### Access and storage

The portal uses an ephemeral code, HttpOnly/SameSite session cookie, per-session
request token and same-origin/current-IP checks. Five incorrect code attempts
cause a one-minute cooldown. Sessions expire after fifteen minutes of inactivity;
Lock revokes the current session. Pages and API responses are marked no-store.
Use the HTTP portal on a trusted LAN; its traffic is not HTTPS encrypted. Do not
forward this configuration port to the Internet.

The complete profile is stored in the ns-runtime Preferences namespace. Defaults
come from config.h, ns_settings.h, display_settings.h and alerts.h. Existing
ns-display and single-network ns-wifi settings migrate automatically; the old
saved Wi-Fi becomes primary, and a distinct configured Wi-Fi remains a backup.
After a profile is saved, it takes precedence over compiled defaults. Reflashing
without erasing flash preserves it. Erasing all flash removes saved settings and
requires re-uploading firmware. NVS has no application-level encryption.

## Recovery hotspot

B > Wi-Fi setup > Temporary setup hotspot starts the existing password-protected
Core2-Setup-xxxx network, even when no saved network is reachable. Join it from
a phone using the displayed password, then open http://192.168.4.1. Enter a
2.4 GHz network and choose Test and save. A successful connection and save promotes
that network to primary, retaining earlier networks as backups up to the cap.
Failure keeps the previous list; Cancel/Back or a five-minute timeout closes
setup and retries the saved list. The connection attempt can take up to twenty
seconds. The hotspot form handles Wi-Fi only; configure Nightscout through the
LAN portal after connecting.

Normal Nightscout polling pauses during recovery setup. Cached readings continue
aging and enabled alerts keep running. The LAN configuration service pauses too.
The recovery server accepts requests only through its own AP interface and closes
its DNS/HTTP services when setup ends.

## History limits

The 288-entry cap covers twelve hours of five-minute SGVs (144 readings). Faster
or duplicate-heavy data can hit the cap and show SGV history limited. Treatments
retain 200 fetched / 128 retained bounds with their existing limit warning.
A longer window fills after a successful fetch; sparse history retains gaps.

The settings screen temporarily uses at least brightness 20/255 so B can recover
access even when the saved dashboard brightness is zero. Closing settings restores
the configured brightness.
