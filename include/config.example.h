#pragma once
namespace cfg {
constexpr char WIFI_SSID[] = "YOUR_2_4_GHZ_WIFI";
constexpr char WIFI_PASSWORD[] = "YOUR_WIFI_PASSWORD";
// HTTPS origin and optional path prefix; no trailing slash, query or credentials.
constexpr char NS_URL[] = "https://your-site.example.com";
// Nightscout subject ACCESS TOKEN with only readable permissions, not API_SECRET.
constexpr char NS_TOKEN[] = "YOUR_READ_ONLY_ACCESS_TOKEN";
// Exact devicestatus.device string, e.g. openaps://your-phone. Empty accepts any
// openaps uploader; set this if more than one uploader uses your Nightscout.
constexpr char AAPS_DEVICE[] = "";
constexpr bool MMOL = false;
constexpr unsigned POLL_SECONDS = 60;
constexpr unsigned SGV_STALE_SECONDS = 600;
constexpr unsigned AAPS_STALE_SECONDS = 600;
constexpr unsigned GRAPH_HOURS = 3; // 1..6; requests up to 288 points
constexpr int LOW_MGDL = 70;
constexpr int HIGH_MGDL = 180;
constexpr unsigned BRIGHTNESS = 100; // 0..255, always on; USB power
constexpr char NTP1[] = "pool.ntp.org";
constexpr char NTP2[] = "time.cloudflare.com";
}
