# Nightscout Socket.IO live notifications

## Behavior

Enabled automatically in v3 mode. The dashboard first fetches data by HTTPS,
then connects to the same origin over WSS at /socket.io/?EIO=4&transport=websocket
(with the configured URL path prefix preserved). It joins the /storage namespace
and sends subscribe with the saved read-only subject access token and collections
entries, devicestatus and treatments. No additional credential is required.

Create, update and delete notifications schedule a full HTTPS refresh through the
existing v3 parsers. Notifications are coalesced for two seconds, with at least
ten seconds between completed fetches and another socket-triggered fetch. This
updates the existing graph, AAPS status and treatment overlays; it does not add
new display elements or change alert thresholds.

Normal configured polling continues as a safety net. Each subscription/reconnection
also schedules a reconciliation fetch. Manual A refresh still works. If sockets
are blocked, unsupported, denied or disconnected, HTTPS polling remains active.
An early fetch restarts the normal polling interval.

## Compatibility and security

- Requires Nightscout's v3 storage Socket.IO namespace and Engine.IO 4 support.
  Older servers without these continue using HTTPS polling; there is no EIO3
  protocol downgrade. Legacy API mode does not open a socket.
- Uses links2004/WebSockets 2.7.1, pinned alongside Arduino ESP32 2.0.17. Its
  beginSslWithBundle overload uses this project's indexed root-key trust adapter.
- WSS uses the same Mozilla CA store, hostname/time validation and PSRAM-preferred
  TLS allocator as HTTPS. No insecure connection is selected by the integration.
- The token goes in the encrypted subscription message, never the URL or logs.
  The socket sends only namespace/subscription and heartbeat messages; it never
  emits storage writes, remote commands or alarm acknowledgements.
- The WebSocket library handles masking, upgrade validation and WebSocket control
  frames; the project handles Engine.IO ping/pong, namespace connection and the
  subscription acknowledgement. Handshake and heartbeat timeouts disconnect;
  the library retries no more frequently than every 30 seconds.
- Networking stays in the existing worker under networkGate. Settings changes,
  Wi-Fi reconfiguration and recovery hotspot setup close the old socket. Saved
  URL/token changes are applied before the next connection.
- Complete text frames are capped by the library at 15 KiB. Larger/fragmented
  notification payloads are not consumed as data; polling reconciles state.
  IPv6 bracket-literal URLs use polling in this initial implementation.
- To disable at build time, add -DNS_ENABLE_SOCKETIO=0 to platformio.ini build_flags
  and rebuild. There is currently no separate on-device socket toggle.

## Setup and diagnostics

Rebuild and upload normally with Flash-Core2.cmd. PlatformIO downloads the new
pinned WebSockets dependency on the first build. Existing saved settings remain.
The same read permissions used by HTTPS must include the three collections.
The reverse proxy must allow WebSocket upgrade on the Socket.IO path.

Successful authentication prints:

    Nightscout Socket.IO: storage subscribed

A reconnect prints that line again. Absence of the line means only the live
subscription is unavailable; regular HTTPS refreshes continue. Do not interpret
socket connectivity as evidence that glucose/loop data is fresh: existing data
age checks and stale alerts still govern that.

## Verification — 2026-11-06

163 host parser/protocol checks pass, including ten new tests for heartbeat
negotiation, overflow rejection, namespace/ack handling, mutation notifications,
denied subscriptions, unrelated events and timer wrap. The Core2 production
firmware compiles with the pinned dependencies. A read-only desktop WSS probe to
the configured server verified TLS, HTTP upgrade, subscription permissions for
all three collections and Engine.IO ping/pong. It printed no credentials or
clinical values. On-device socket soak testing is still required.

Upstream protocol implementation:
https://github.com/nightscout/cgm-remote-monitor/blob/master/lib/api3/storageSocket.js
WebSocket library:
https://github.com/Links2004/arduinoWebSockets/tree/2.7.1
