#include <M5Unified.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include "secure_client.h"
#include "ca_bundle_info.h"
#include <HTTPClient.h>
#include <esp_timer.h>
#include <esp_heap_caps.h>
#include <Preferences.h>
#include <WebServer.h>
#include <DNSServer.h>
#include <esp_system.h>
#include "quick_settings.h"
#include "runtime_settings.h"
#include <freertos/semphr.h>
#include <atomic>
#include <new>
#include "body_buffer.h"
#include "model.h"
#include "treatments.h"
#include "display_settings.h"
#include "alerts.h"
#include "power_state.h"
#include "ns_settings.h"
#include "ns_api.h"
#include "smb_labels.h"
#include "graph_scale.h"
#if __has_include("config.h")
#include "config.h"
#else
#include "config.example.h"
#endif
#ifdef RUN_PARSER_TESTS
#include "parser_tests.h"
#endif

extern const uint8_t caBundleStart[] asm("_binary_certs_x509_crt_bundle_bin_start");
static_assert(cfg::GRAPH_HOURS>=1 && cfg::GRAPH_HOURS<=12,"Graph hours must be 1..12");
static_assert(cfg::POLL_SECONDS>=30,"Poll no faster than 30 seconds");
struct Snapshot {
  model::History history;
  model::Status status;
  model::Treatments treatments;
  char glucoseError[48]="Starting", statusError[48]="Starting", treatmentError[48]="Starting";
  int64_t clockAt=0, monoAt=0;
  uint32_t connectionEpoch=0;
};
void resetSnapshot(Snapshot &value) {value.~Snapshot();::new(static_cast<void*>(&value)) Snapshot();}
Snapshot ui;
QueueHandle_t updates;
TaskHandle_t networkTask;
bool mmol=cfg::MMOL;
bool showForecast=true;
std::atomic<unsigned> graphHours{quicksettings::restoreHours(cfg::GRAPH_HOURS,3)};
unsigned pollHours=3; // Network task takes one timeframe snapshot per polling cycle.
bool settingsOpen=false;
std::atomic<bool> portalRequested{false},portalCancel{false},reconnectRequested{false};
std::atomic<int> portalState{0}; // idle, waiting, ready, testing, saved, failed, timed out, canceled
char portalName[32]={},portalPassword[17]={};

alerts::State glucoseAlerts;
alerts::StaleState staleAlerts;
bool staleSoundActive=false;
runtimecfg::Settings liveSettings,uiSettings,netSettings;
SemaphoreHandle_t settingsMutex=nullptr,networkGate=nullptr;
std::atomic<uint32_t> settingsRevision{1},connectionEpoch{1};
uint32_t uiRevision=0,uiEpoch=1;
std::atomic<bool> lanRequested{false},apSetupActive{false};
struct LanStatus {int state=0;char address[48]={},code[9]={};};
QueueHandle_t lanUpdates=nullptr;
LanStatus lanStatus;
bool wifiMenuOpen=false;
char settingsMessage[64]={};
std::atomic<bool> networkBusy{false};
uint32_t lastAppliedAt=0;
bool haveApplied=false;
enum class Resource {Glucose,Status,Treatments};
namespace layout {
constexpr int PLOT_TOP=62;
constexpr int LEGEND_Y=52, TIME_Y=184;
constexpr int IOB_Y=194, BASAL_Y=216, FOOTER_Y=231;
}

int64_t monotonicSeconds() { return esp_timer_get_time()/1000000; }
int64_t displayNow() {
  return ui.clockAt ? ui.clockAt+monotonicSeconds()-ui.monoAt : 0;
}
void currentSettings(runtimecfg::Settings &out,uint32_t &revision,uint32_t *epoch=nullptr) {
  xSemaphoreTake(settingsMutex,portMAX_DELAY);out=liveSettings;revision=settingsRevision.load();if(epoch)*epoch=connectionEpoch.load();xSemaphoreGive(settingsMutex);
}
bool storeSettings(const runtimecfg::Settings &next,uint32_t expected,char *error,size_t cap) {
  if(!runtimecfg::valid(next)) {strlcpy(error,"Invalid settings",cap);return false;}
  xSemaphoreTake(settingsMutex,portMAX_DELAY);
  if(expected!=settingsRevision.load()) {xSemaphoreGive(settingsMutex);strlcpy(error,"Settings changed; reload before saving",cap);return false;}
  Preferences saved;bool ok=false;
  if(saved.begin("ns-runtime",false)) {ok=saved.putBytes("profile",&next,sizeof(next))==sizeof(next);saved.end();}
  if(ok) {
    if(runtimecfg::connectionChanged(liveSettings,next)) ++connectionEpoch;
    liveSettings=next;++settingsRevision;
  }
  xSemaphoreGive(settingsMutex);
  strlcpy(error,ok?"":"Could not save settings",cap);
  if(ok && networkTask) xTaskNotifyGive(networkTask);
  return ok;
}
void loadRuntimeSettings() {
  runtimecfg::Settings defaults;
  strlcpy(defaults.url,cfg::NS_URL,sizeof(defaults.url));strlcpy(defaults.token,cfg::NS_TOKEN,sizeof(defaults.token));
  strlcpy(defaults.device,cfg::AAPS_DEVICE,sizeof(defaults.device));defaults.v3=nscfg::USE_V3;
  strlcpy(defaults.timezone,displaycfg::TIMEZONE,sizeof(defaults.timezone));
  strlcpy(defaults.ntp1,cfg::NTP1,sizeof(defaults.ntp1));strlcpy(defaults.ntp2,cfg::NTP2,sizeof(defaults.ntp2));
  defaults.hours=quicksettings::restoreHours(cfg::GRAPH_HOURS,3);defaults.poll=cfg::POLL_SECONDS;
  defaults.sgvStale=cfg::SGV_STALE_SECONDS;defaults.apsStale=cfg::AAPS_STALE_SECONDS;
  defaults.low=cfg::LOW_MGDL;defaults.high=cfg::HIGH_MGDL;defaults.brightness=cfg::BRIGHTNESS;defaults.mmol=cfg::MMOL;defaults.volume=alerts::VOLUME;
  if(!runtimecfg::https(defaults.url) || strlen(cfg::NS_URL)>=sizeof(defaults.url)) {defaults.url[0]=0;defaults.token[0]=0;}
  if(strlen(cfg::NS_TOKEN)>=sizeof(defaults.token) || !strcmp(defaults.token,"YOUR_READ_ONLY_ACCESS_TOKEN"))defaults.token[0]=0;
  defaults.poll=std::max(30u,std::min(defaults.poll,900u));
  defaults.sgvStale=std::max(60u,std::min(defaults.sgvStale,3600u));defaults.apsStale=std::max(60u,std::min(defaults.apsStale,3600u));
  if(defaults.low<20 || defaults.low>179)defaults.low=70;
  if(defaults.high<=defaults.low || defaults.high>600)defaults.high=180;
  defaults.brightness=std::min(defaults.brightness,255u);defaults.volume=std::min(defaults.volume,255u);
  if(!defaults.timezone[0])strlcpy(defaults.timezone,"UTC0",sizeof(defaults.timezone));
  if(!defaults.ntp1[0])strlcpy(defaults.ntp1,"pool.ntp.org",sizeof(defaults.ntp1));
  quicksettings::Credentials configuredWifi;
  strlcpy(configuredWifi.ssid,cfg::WIFI_SSID,sizeof(configuredWifi.ssid));strlcpy(configuredWifi.password,cfg::WIFI_PASSWORD,sizeof(configuredWifi.password));
  if(strcmp(configuredWifi.ssid,"YOUR_2_4_GHZ_WIFI") && quicksettings::validCredentials(configuredWifi)) runtimecfg::promoteWifi(defaults,configuredWifi);
  Preferences old;
  if(old.begin("ns-display",true)) {
    defaults.hours=quicksettings::restoreHours(old.getUInt("hours",defaults.hours),defaults.hours);
    defaults.mmol=old.getBool("mmol",defaults.mmol);defaults.forecast=old.getBool("forecast",true);defaults.sound=old.getBool("alerts-on",true);old.end();
  }
  if(old.begin("ns-wifi",true)) {
    quicksettings::Credentials previous;
    if(old.getBytesLength("network")==sizeof(previous) && old.getBytes("network",&previous,sizeof(previous))==sizeof(previous)) runtimecfg::promoteWifi(defaults,previous);
    old.end();
  }
  liveSettings=defaults;
  Preferences saved;
  if(saved.begin("ns-runtime",true)) {
    runtimecfg::Settings candidate;
    if(saved.getBytesLength("profile")==sizeof(candidate) && saved.getBytes("profile",&candidate,sizeof(candidate))==sizeof(candidate) && runtimecfg::valid(candidate)) liveSettings=candidate;
    saved.end();
  }
  uiSettings=netSettings=liveSettings;
}
bool configured() {
  return runtimecfg::https(netSettings.url) && netSettings.token[0] && strcmp(netSettings.token,"YOUR_READ_ONLY_ACCESS_TOKEN");
}
// Bounded decoded response sink. HTTPClient::writeToStream handles HTTP chunks;
// don't deserialize http.getStream() directly (chunk framing is not JSON).
void *allocateBody(size_t n) {
  void *p=heap_caps_malloc(n,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);
  return p ? p : malloc(n);
}
class Body : public Stream {
public:
  static constexpr size_t limit=192*1024;
  BodyBuffer buffer;
  explicit Body(size_t capacity) : buffer(capacity,allocateBody) {}
  size_t write(uint8_t c) override {return write(&c,1);}
  size_t write(const uint8_t *p,size_t n) override {return buffer.append(p,n);}
  int available() override {return 0;}
  int read() override {return -1;}
  int peek() override {return -1;}
  void flush() override {}
};
// Portal documents prefer PSRAM so an open settings page does not compete
// with TLS buffers for the smaller internal heap.
struct PortalAllocator {
  void *allocate(size_t size) {return allocateBody(size);}
  void deallocate(void *value) {free(value);}
  void *reallocate(void *value,size_t size) {
    if(!size) {free(value);return nullptr;}
    void *next=heap_caps_realloc(value,size,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);
    return next?next:realloc(value,size);
  }
};
using PortalDocument=BasicJsonDocument<PortalAllocator>;
// Network task only: JWTs are kept in RAM and never written to Preferences.
String jwt;
int64_t jwtExpiry=0;
uint32_t authFailedAt=0;
bool authCooling=false;
char lastAuthError[48]={};
String urlEncode(const char *value) {
  String out;
  while (*value) {
    uint8_t c=uint8_t(*value++);
    if ((c>='a' && c<='z') || (c>='A' && c<='Z') || (c>='0' && c<='9') || c=='-' || c=='_' || c=='.' || c=='~') out+=char(c);
    else {char encoded[4];snprintf(encoded,sizeof(encoded),"%%%02X",c);out+=encoded;}
  }
  return out;
}
void authFailure(const char *message) {
  jwt="";jwtExpiry=0;authCooling=true;authFailedAt=millis();
  strlcpy(lastAuthError,message,sizeof(lastAuthError));
}
void tlsFailure(SecureClient &tls,const char *label,int httpCode,char *error,size_t cap) {
  char detail[160]={};int code=tls.lastError(detail,sizeof(detail));uint32_t flags=tls.verificationFlags();
  if(flags || code==MBEDTLS_ERR_X509_CERT_VERIFY_FAILED)strlcpy(error,tlsdiagnostics::message(flags),cap);
  else if(code<0)snprintf(error,cap,"TLS error -0x%04X",unsigned(-code));
  else snprintf(error,cap,"Network error %d",httpCode);
  Serial.printf("%s HTTP-client=%d TLS=%d verify=0x%08lX %s; UTC=%lld free=%u largest=%u roots=%u verifier=anchors\n",
    label,httpCode,code,(unsigned long)flags,detail,(long long)time(nullptr),
    unsigned(heap_caps_get_free_size(MALLOC_CAP_INTERNAL|MALLOC_CAP_8BIT)),
    unsigned(heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL|MALLOC_CAP_8BIT)),unsigned((caBundleStart[0]<<8)|caBundleStart[1]));
}
bool ensureJwt(char *error,size_t cap) {
  int64_t now=time(nullptr);
  if (!nsapi::renewNeeded(jwt.length()>0,jwtExpiry,now)) return true;
  if (authCooling && uint32_t(millis()-authFailedAt)<30000) {
    strlcpy(error,lastAuthError,cap);return false;
  }
  auto fail=[&](const char *message){authFailure(message);strlcpy(error,message,cap);return false;};
  if (now<1767225600) return fail("V3 auth needs valid clock");
  SecureClient tls(caBundleStart);
  HTTPClient http;http.setUserAgent("ESP32HTTPClient");http.setConnectTimeout(10000);http.setTimeout(20000);http.setReuse(false);
  http.setFollowRedirects(HTTPC_DISABLE_FOLLOW_REDIRECTS);
  // This standard NS endpoint necessarily carries the subject token in its
  // TLS-protected path. Never print the path, credentials or response body.
  if (!http.begin(tls,String(netSettings.url)+"/api/v2/authorization/request/"+urlEncode(netSettings.token)))
    return fail("V3 auth setup failed");
  http.addHeader("Accept","application/json");http.addHeader("Accept-Encoding","identity");
  int code=http.GET();
  if (code!=200) {
    char message[48];
    if(code<0)tlsFailure(tls,"Auth",code,message,sizeof(message));
    else snprintf(message,sizeof(message),"Auth HTTP %d",code);
    http.end();
    return fail(message);
  }
  constexpr size_t maxAuth=8192;
  if (http.getSize()>int(maxAuth)) {http.end();return fail("Auth response too large");}
  Body body(http.getSize()>=0?size_t(http.getSize()):maxAuth);
  if (!body.buffer.data) {http.end();return fail("Auth memory unavailable");}
  int got=http.writeToStream(&body),expected=http.getSize();http.end();
  if (got<0 || body.buffer.overflow || (expected>=0 && got!=expected)) return fail("Auth response incomplete");
  DynamicJsonDocument filter(256);filter["token"]=true;filter["exp"]=true;
  DynamicJsonDocument doc(6144);
  auto err=deserializeJson(doc,body.buffer.data,body.buffer.length,DeserializationOption::Filter(filter),DeserializationOption::NestingLimit(8));
  const char *value=nullptr;int64_t expiry=0;
  if (err || !nsapi::tokenResponse(doc.as<JsonVariantConst>(),time(nullptr),value,expiry))
    return fail("Auth token/expiry invalid");
  jwt="";
  if (!jwt.concat(value)) return fail("Auth token memory unavailable");
  jwtExpiry=expiry;authCooling=false;lastAuthError[0]=0;
  return true;
}
bool fetch(const String &path, Resource resource, Snapshot &s, int64_t now, char *error, size_t cap, bool allowRenew=true) {
  if (netSettings.v3 && !ensureJwt(error,cap)) return false;
  const bool glucose=resource==Resource::Glucose;
  const char *label=glucose?"SGV":resource==Resource::Status?"AAPS":"Treatments";
  SecureClient tls(caBundleStart);
  HTTPClient http;
  http.setUserAgent("ESP32HTTPClient");
  http.setConnectTimeout(10000);
  http.setTimeout(20000);
  http.setReuse(false);
  http.setFollowRedirects(HTTPC_DISABLE_FOLLOW_REDIRECTS);
  if (!http.begin(tls,String(netSettings.url)+path)) {strlcpy(error,"HTTPS setup failed",cap); return false;}
  http.addHeader("Accept","application/json");
  http.addHeader("Accept-Encoding","identity");
  if (netSettings.v3) http.addHeader("Authorization","Bearer "+jwt);
  else http.addHeader("api-secret",netSettings.token);
  int code=http.GET();
  if (netSettings.v3 && code==401 && allowRenew) {
    http.end();jwt="";jwtExpiry=0;
    if (!ensureJwt(error,cap)) return false;
    return fetch(path,resource,s,time(nullptr),error,cap,false);
  }
  if (netSettings.v3 && code==401) authFailure("V3 access denied after renewal");
  bool ok=false;
  if (code!=200) {
    if (code<0) {
      tlsFailure(tls,label,code,error,cap);
    }
    else snprintf(error,cap,"HTTP %d%s",code,(code==401 || code==403)?" check read token":"");
  } else if (http.getSize()>int(Body::limit)) strlcpy(error,"Response too large",cap);
  else {
    const int declared=http.getSize();
    Body body(declared>=0 ? size_t(declared) : Body::limit);
    if (!body.buffer.data) {
      strlcpy(error,"Response memory unavailable",cap);
      http.end(); return false;
    }
    int got=http.writeToStream(&body);
    const int expected=http.getSize(); // chunked decoding may set this now
    Serial.printf("%s body: result=%d bytes=%u expected=%d limit=%u overflow=%d\n",
      label,got,unsigned(body.buffer.length),expected,
      unsigned(body.buffer.capacity),body.buffer.overflow);
    if (body.buffer.overflow) strlcpy(error,"Response exceeds 192 KiB",cap);
    else if (got<0) snprintf(error,cap,"Body read %d (%u bytes)",got,unsigned(body.buffer.length));
    else if (expected>=0 && got!=expected) snprintf(error,cap,"Short body %d/%d",got,expected);
    else {
      // Filter retains supported glucose forecasts and removes unused subtrees.
      DynamicJsonDocument items(4096),filter(6144);
      if (resource==Resource::Treatments) model::treatmentFilter(items);
      else model::makeFilter(items,glucose);
      nsapi::responseFilter(filter,items,netSettings.v3);
      DynamicJsonDocument doc(65536);
      auto err=deserializeJson(doc,body.buffer.data,body.buffer.length,DeserializationOption::Filter(filter),DeserializationOption::NestingLimit(16));
      if (err) strlcpy(error,"Invalid/oversized JSON",cap);
      else {
        JsonVariantConst collection=nsapi::collection(doc.as<JsonVariantConst>(),netSettings.v3);
        if (resource==Resource::Glucose) ok=model::entries(collection,now,s.history);
        else if (resource==Resource::Status) ok=model::statuses(collection,netSettings.device,now,s.status);
        else ok=model::treatments(collection,now-int64_t(pollHours)*3600,now,s.treatments);
        strlcpy(error,ok?"":netSettings.v3?"Invalid V3 result/status":"Expected JSON array",cap);
        if(ok && glucose && collection.size()>=model::MAX_POINTS) strlcpy(error,"SGV history limited",cap);
      }
    }
  }
  http.end(); return ok;
}
bool connectSavedWifi() {
  if(WiFi.status()==WL_CONNECTED) return true;
  for(unsigned i=0;i<netSettings.wifiCount;++i) {
    const auto &network=netSettings.wifi[i];
    WiFi.disconnect(false);WiFi.begin(network.ssid,network.password);
    for(int wait=0;wait<40 && WiFi.status()!=WL_CONNECTED;++wait) {
      if(portalRequested.load()) return false;
      vTaskDelay(pdMS_TO_TICKS(250));
    }
    if(WiFi.status()==WL_CONNECTED && WiFi.SSID()==network.ssid) return true;
  }
  return false;
}
// The network task owns all Wi-Fi mutations. Normal Nightscout fetches pause
// for setup; the UI continues aging cached readings and running enabled alerts.
void runWifiSetup() {
  snprintf(portalName,sizeof(portalName),"Core2-Setup-%04X",unsigned(esp_random()&0xffff));
  snprintf(portalPassword,sizeof(portalPassword),"%08X%08X",unsigned(esp_random()),unsigned(esp_random()));
  WiFi.setAutoReconnect(false);WiFi.mode(WIFI_AP_STA);
  if(!WiFi.softAP(portalName,portalPassword,1,0,1)) {portalState.store(5);WiFi.mode(WIFI_STA);WiFi.setAutoReconnect(false);return;}
  WebServer server(80);DNSServer dns;
  dns.start(53,"*",WiFi.softAPIP());
  bool success=false;
  auto page=[&]() {
    if(server.client().localIP()!=WiFi.softAPIP()) {server.send(403,"text/plain","Join the Core2 setup Wi-Fi first.");return;}
    server.sendHeader("Cache-Control","no-store");
    server.send(200,"text/html",R"HTML(<!doctype html><html><meta name="viewport" content="width=device-width,initial-scale=1"><title>Core2 Wi-Fi</title><body style="font:18px sans-serif;max-width:420px;margin:30px auto;padding:15px"><h2>Core2 Wi-Fi</h2><p>Enter a 2.4 GHz network. Leave the password empty for an open network.</p><form method="post" action="/save"><p><label>Network name<br><input name="ssid" maxlength="32" required></label></p><p><label>Password<br><input name="password" type="password" maxlength="63" autocomplete="new-password"></label></p><button>Test and save</button></form><p>The previous settings are kept if connection fails.</p></body></html>)HTML");
  };
  server.on("/",HTTP_GET,page);server.onNotFound(page);
  server.on("/save",HTTP_POST,[&]() {
    if(server.client().localIP()!=WiFi.softAPIP()) {server.send(403,"text/plain","Join the Core2 setup Wi-Fi first.");return;}
    server.sendHeader("Cache-Control","no-store");
    String ssid=server.arg("ssid"),password=server.arg("password");
    quicksettings::Credentials c;
    if(ssid.length()>32 || password.length()>63) {server.send(400,"text/plain","Network name or password too long.");return;}
    strlcpy(c.ssid,ssid.c_str(),sizeof(c.ssid));strlcpy(c.password,password.c_str(),sizeof(c.password));
    if(strlen(c.ssid)!=ssid.length() || strlen(c.password)!=password.length() || !quicksettings::validCredentials(c)) {server.send(400,"text/plain","Enter a network name and an empty or 8-63 character password.");return;}
    portalState.store(3);WiFi.disconnect(false);
    for(int i=0;i<100 && WiFi.status()==WL_CONNECTED && !portalCancel.load();++i) vTaskDelay(pdMS_TO_TICKS(20));
    WiFi.begin(c.ssid,c.password);
    auto connected=[&](){return WiFi.status()==WL_CONNECTED && WiFi.SSID()==ssid;};
    for(int i=0;i<80 && !connected() && !portalCancel.load();++i) vTaskDelay(pdMS_TO_TICKS(250));
    if(!connected() || portalCancel.load()) {
      portalState.store(5);server.send(400,"text/plain","Connection failed. Previous settings kept. Retry or cancel on the Core2.");return;
    }
    runtimecfg::Settings candidate;uint32_t revision;currentSettings(candidate,revision);
    bool stored=runtimecfg::promoteWifi(candidate,c);
    char saveError[64]={};
    if(stored) stored=storeSettings(candidate,revision,saveError,sizeof(saveError));
    if(!stored) {portalState.store(5);server.send(500,"text/plain","Could not save. Previous settings kept.");return;}
    success=true;
    server.sendHeader("Cache-Control","no-store");server.send(200,"text/plain","Connected and saved. You can reconnect your phone to your usual network.");
    portalState.store(4);
  });
  server.begin();portalState.store(2);uint32_t started=millis();
  while(!success && !portalCancel.load() && uint32_t(millis()-started)<300000) {
    dns.processNextRequest();server.handleClient();vTaskDelay(pdMS_TO_TICKS(10));
  }
  if(success) vTaskDelay(pdMS_TO_TICKS(500));
  else portalState.store(portalCancel.load()?7:6);
  server.stop();dns.stop();WiFi.softAPdisconnect(true);WiFi.mode(WIFI_STA);
  WiFi.setAutoReconnect(false);WiFi.disconnect(false);
}
void worker(void*) {
  static Snapshot s;
  unsigned failures=0;
  WiFi.persistent(false);WiFi.mode(WIFI_STA);WiFi.setAutoReconnect(false);
  String lastTimezone,lastNtp1,lastNtp2;
  for (;;) {
    xSemaphoreTake(networkGate,portMAX_DELAY);
    if(portalRequested.exchange(false)) {
      if(portalCancel.load()) portalState.store(7);
      else {apSetupActive.store(true);runWifiSetup();apSetupActive.store(false);}
    }
    uint32_t revision,epoch;currentSettings(netSettings,revision,&epoch);
    if(s.connectionEpoch!=epoch) {
      resetSnapshot(s);s.connectionEpoch=epoch;jwt="";jwtExpiry=0;authCooling=false;
    }
    if(lastTimezone!=netSettings.timezone || lastNtp1!=netSettings.ntp1 || lastNtp2!=netSettings.ntp2) {
      configTzTime(netSettings.timezone,netSettings.ntp1,netSettings.ntp2);
      lastTimezone=netSettings.timezone;lastNtp1=netSettings.ntp1;lastNtp2=netSettings.ntp2;
    }
    if(reconnectRequested.exchange(false)) {vTaskDelay(pdMS_TO_TICKS(500));WiFi.disconnect(false);}
    pollHours=netSettings.hours;
    networkBusy.store(true);
    bool good=false;
    if(!connectSavedWifi()) {
      strlcpy(s.glucoseError,"WiFi offline",sizeof(s.glucoseError));
      strlcpy(s.statusError,"WiFi offline",sizeof(s.statusError));
    } else if(!configured()) {
      strlcpy(s.glucoseError,"Configure Nightscout",sizeof(s.glucoseError));
      strlcpy(s.statusError,"Use LAN portal or config.h",sizeof(s.statusError));
    } else {
        int64_t now=time(nullptr);
        if (now<1767225600) {
          strlcpy(s.glucoseError,"Waiting for NTP clock",sizeof(s.glucoseError));
          strlcpy(s.statusError,"TLS needs valid time",sizeof(s.statusError));
        } else {
          // Keep a monotonic anchor so an NTP outage never freezes data age.
          s.clockAt=now; s.monoAt=monotonicSeconds();
          const long long sinceMs=(now-int64_t(pollHours)*3600)*1000;
          char since[32],until[32];snprintf(since,sizeof(since),"%lld",sinceMs);
          snprintf(until,sizeof(until),"%lld",(long long)(now+120)*1000);
          String entryPath=netSettings.v3?
            String("/api/v3/entries?type$eq=sgv&date$gte=")+since+"&date$lte="+until+
              "&sort$desc=date&limit=288&fields=date,sgv,direction,isValid,isDeleted":
            String("/api/v1/entries/sgv.json?count=288&find[date][$gte]=")+since;
          bool a=fetch(entryPath,Resource::Glucose,s,now,s.glucoseError,sizeof(s.glucoseError));
          String path=netSettings.v3?
            "/api/v3/devicestatus?sort$desc=date&limit=2&fields=date,created_at,device,openaps,pump,isValid,isDeleted":
            "/api/v1/devicestatus.json?count=2&find[openaps][$exists]=true";
          if (*netSettings.device) path+=String(netSettings.v3?"&device$eq=":"&find[device]=")+urlEncode(netSettings.device);
          bool b=fetch(path,Resource::Status,s,time(nullptr),s.statusError,sizeof(s.statusError));
          String treatmentPath;
          if (netSettings.v3) {
            char current[32];snprintf(current,sizeof(current),"%lld",(long long)time(nullptr)*1000);
            treatmentPath=String("/api/v3/treatments?date$gte=")+since+"&date$lte="+current+
              "&sort$desc=date&limit=200&fields=identifier,date,created_at,mills,timestamp,carbs,insulin,isSMB,type,isValid,isDeleted,eventType,pumpId,pumpType,pumpSerial";
          } else {
            time_t windowStart=time(nullptr)-int64_t(pollHours)*3600;
            struct tm utc;gmtime_r(&windowStart,&utc);
            char iso[32];strftime(iso,sizeof(iso),"%Y-%m-%dT%H:%M:%S.000Z",&utc);
            char legacy[320];snprintf(legacy,sizeof(legacy),
              "/api/v1/treatments.json?count=200&find[$or][0][date][$gte]=%lld&find[$or][1][created_at][$gte]=%s",
              (long long)windowStart*1000,iso);
            treatmentPath=legacy;
          }
          bool c=fetch(treatmentPath,Resource::Treatments,s,time(nullptr),s.treatmentError,sizeof(s.treatmentError));
          good=a&&b&&c;
        }
    }
    networkBusy.store(false);
    xQueueOverwrite(updates,&s);
    xSemaphoreGive(networkGate);
    failures=good?0:std::min(failures+1,4u);
    unsigned wait=good?netSettings.poll:std::min(15u*(1u<<failures),300u);
    ulTaskNotifyTake(pdTRUE,pdMS_TO_TICKS(wait*1000));
  }
}
bool testNightscoutConnection(const runtimecfg::Settings &settings,char *error,size_t cap) {
  if(!settings.token[0] || !strcmp(settings.token,"YOUR_READ_ONLY_ACCESS_TOKEN")) {strlcpy(error,"Set a read-only Nightscout token",cap);return false;}
  if(time(nullptr)<1767225600) {strlcpy(error,"Wait for a valid NTP clock before testing HTTPS",cap);return false;}
  String accessToken;
  auto get=[&](const String &path,Body &body,bool authenticate) {
    SecureClient tls(caBundleStart);
    HTTPClient http;http.setUserAgent("ESP32HTTPClient");http.setConnectTimeout(10000);http.setTimeout(20000);http.setReuse(false);
    http.setFollowRedirects(HTTPC_DISABLE_FOLLOW_REDIRECTS);
    if(!http.begin(tls,String(settings.url)+path)) return -1;
    http.addHeader("Accept","application/json");http.addHeader("Accept-Encoding","identity");
    if(authenticate) {if(settings.v3)http.addHeader("Authorization","Bearer "+accessToken);else http.addHeader("api-secret",settings.token);}
    int code=http.GET();
    if(code<0)tlsFailure(tls,"Portal-test",code,error,cap);
    if(code==200) {int got=http.writeToStream(&body);int expected=http.getSize();if(got<0 || body.buffer.overflow || (expected>=0 && got!=expected))code=-2;}
    http.end();return code;
  };
  if(settings.v3) {
    Body auth(8192);if(!auth.buffer.data) {strlcpy(error,"Test response allocation failed",cap);return false;}
    int code=get(String("/api/v2/authorization/request/")+urlEncode(settings.token),auth,false);
    if(code!=200) {if(code>0)snprintf(error,cap,"Auth HTTP %d",code);else strlcpy(error,"Auth TLS/network or response failed",cap);return false;}
    PortalDocument filter(256),doc(6144);filter["token"]=true;filter["exp"]=true;
    auto jsonError=deserializeJson(doc,auth.buffer.data,auth.buffer.length,DeserializationOption::Filter(filter),DeserializationOption::NestingLimit(8));
    const char *token=nullptr;int64_t expires=0;
    if(jsonError || !nsapi::tokenResponse(doc.as<JsonVariantConst>(),time(nullptr),token,expires)) {strlcpy(error,"Invalid authorization response",cap);return false;}
    accessToken=token;
  }
  Body body(8192);if(!body.buffer.data) {strlcpy(error,"Test response allocation failed",cap);return false;}
  int code=get(settings.v3?"/api/v3/entries?type$eq=sgv&limit=1&fields=date,sgv":"/api/v1/entries/sgv.json?count=1",body,true);
  if(code!=200) {if(code>0)snprintf(error,cap,"Entries HTTP %d",code);else strlcpy(error,"Entries TLS/network or response failed",cap);return false;}
  PortalDocument doc(6144);
  if(deserializeJson(doc,body.buffer.data,body.buffer.length,DeserializationOption::NestingLimit(8)) || nsapi::collection(doc.as<JsonVariantConst>(),settings.v3).isNull()) {
    strlcpy(error,"Invalid entries response",cap);return false;
  }
  return true;
}
#include "lan_service.h"
String amount(float n, unsigned digits=2) {return std::isfinite(n)?String(n,digits):String("--");}
String compactAmount(float value) {
  String text=amount(value,2);
  while (text.indexOf('.')>=0 && text.endsWith("0")) text.remove(text.length()-1);
  if (text.endsWith(".")) text.remove(text.length()-1);
  return text;
}
String age(int64_t t,int64_t now) {
  if (!model::validTime(t,now)) return "--";
  int64_t m=std::max(int64_t(0),now-t)/60;
  return m>999 ? ">999m" : String(long(m))+"m";
}
void line(int y,const String &s,uint16_t color=TFT_WHITE,int size=1) {
  M5.Display.setTextColor(color,TFT_BLACK); M5.Display.setTextSize(size);
  M5.Display.setCursor(5,y); M5.Display.print(s);
}
// Native filled graphic arrows avoid font-dependent diagonal glyphs.
void drawOneTrendArrow(float cx,float cy,float dx,float dy,uint16_t color,float headHalf=6) {
  const float length=11,headLength=8,shaftHalf=1.5f;
  float nx=-dy,ny=dx;
  float tx=cx+dx*length,ty=cy+dy*length;
  float bx=tx-dx*headLength,by=ty-dy*headLength;
  float sx=cx-dx*length,sy=cy-dy*length;
  auto pixel=[](float v){return int(std::lround(v));};
  int ax=pixel(sx+nx*shaftHalf),ay=pixel(sy+ny*shaftHalf);
  int az=pixel(sx-nx*shaftHalf),aw=pixel(sy-ny*shaftHalf);
  int ex=pixel(bx+nx*shaftHalf),ey=pixel(by+ny*shaftHalf);
  int ez=pixel(bx-nx*shaftHalf),ew=pixel(by-ny*shaftHalf);
  M5.Display.fillTriangle(ax,ay,az,aw,ex,ey,color);
  M5.Display.fillTriangle(az,aw,ex,ey,ez,ew,color);
  M5.Display.fillTriangle(pixel(tx),pixel(ty),pixel(bx+nx*headHalf),pixel(by+ny*headHalf),
    pixel(bx-nx*headHalf),pixel(by-ny*headHalf),color);
}
void drawTrendArrow(const char *direction,int cx,int cy,uint16_t color) {
  if (!strcmp(direction,"DoubleUp") || !strcmp(direction,"DoubleDown")) {
    float dy=!strcmp(direction,"DoubleUp")?-1.0f:1.0f;
    drawOneTrendArrow(cx-6,cy,0,dy,color,4.5f);
    drawOneTrendArrow(cx+6,cy,0,dy,color,4.5f);
  } else if (!strcmp(direction,"SingleUp")) drawOneTrendArrow(cx,cy,0,-1,color);
  else if (!strcmp(direction,"FortyFiveUp")) drawOneTrendArrow(cx,cy,0.7071068f,-0.7071068f,color);
  else if (!strcmp(direction,"Flat")) drawOneTrendArrow(cx,cy,1,0,color);
  else if (!strcmp(direction,"FortyFiveDown")) drawOneTrendArrow(cx,cy,0.7071068f,0.7071068f,color);
  else if (!strcmp(direction,"SingleDown")) drawOneTrendArrow(cx,cy,0,1,color);
  else {
    M5.Display.setTextSize(2);M5.Display.setTextColor(color,TFT_BLACK);
    M5.Display.setCursor(cx-6,cy-8);M5.Display.print("?");
  }
}
// Compare rendered content, not struct padding or the poll's clock anchors.
// Normalize missing float values so repeated NaNs do not trigger redraws.
struct ScreenFingerprint {
  uint64_t value=14695981039346656037ULL;
  void byte(uint8_t v) {value=(value^v)*1099511628211ULL;}
  void integer(uint64_t v) {for (unsigned i=0;i<8;++i) {byte(v&255);v>>=8;}}
  void number(float v) {
    uint32_t bits=0;
    if (std::isnan(v)) bits=0x7fc00000;
    else if (v!=0) memcpy(&bits,&v,sizeof(bits));
    integer(bits);
  }
  void text(const char *v) {while (*v) byte(uint8_t(*v++));byte(0);}
};
uint64_t payloadFingerprint() {
  ScreenFingerprint h;
  h.integer(ui.history.count);
  for (size_t i=0;i<ui.history.count;++i) {
    const auto &p=ui.history.p[i];h.integer(p.t);h.number(p.sgv);h.text(p.direction);
  }
  const auto &s=ui.status;
  for (int64_t t : {s.uploaded,s.loopTime,s.pumpTime,s.tempUntil}) h.integer(t);
  for (const auto *m : {&s.iob,&s.cob,&s.basal}) {h.number(m->value);h.integer(m->t);}
  h.text(s.activity);h.text(s.pump);h.text(s.basalKind);h.integer(s.forecast.t);
  for (size_t k=0;k<model::PRED_LINES;++k) {
    h.integer(s.forecast.count[k]);
    for (size_t i=0;i<s.forecast.count[k];++i) h.number(s.forecast.bg[k][i]);
  }
  h.integer(ui.treatments.count);h.integer(ui.treatments.limited);
  for (size_t i=0;i<ui.treatments.count;++i) {
    const auto &e=ui.treatments.events[i];
    h.integer(e.t);h.number(e.carbs);h.number(e.insulin);h.integer(e.smb);h.text(e.id);
  }
  h.text(ui.glucoseError);h.text(ui.statusError);h.text(ui.treatmentError);
  return h.value;
}
uint64_t timedFingerprint(int64_t now) {
  // Staleness/expiry is a visible state change even without a new upload.
  ScreenFingerprint h;
  const auto &s=ui.status;
  int64_t glucoseTime=ui.history.count?ui.history.p[ui.history.count-1].t:0;
  h.integer(model::fresh(glucoseTime,now,uiSettings.sgvStale));
  for (int64_t t : {s.iob.t,s.cob.t,s.basal.t,s.loopTime,s.pumpTime,s.forecast.t})
    h.integer(model::fresh(t,now,uiSettings.apsStale));
  h.integer(!s.tempUntil || now<s.tempUntil);
  int64_t start=now-int64_t(graphHours.load())*3600;
  // Repaint only when a sample enters/leaves the visible history window.
  for (size_t i=0;i<ui.history.count;++i) if (ui.history.p[i].t>=start && ui.history.p[i].t<=now) h.integer(i+1);
  h.integer(0xffff);
  for (size_t i=0;i<ui.treatments.count;++i) if (ui.treatments.events[i].t>=start && ui.treatments.events[i].t<=now) h.integer(i+1);
  return h.value;
}
uint64_t renderedPayload=0,renderedTimed=0;
bool rendered=false;
void drawActivityIndicator(bool force=false) {
  static int previousMode=-1,previousFrame=-1;
  uint32_t tick=millis();
  bool failed=*ui.glucoseError || *ui.statusError || *ui.treatmentError;
  int mode=networkBusy.load()?1:haveApplied && uint32_t(tick-lastAppliedAt)<500?(failed?3:2):0;
  int frame=mode==1?(tick/150)%4:0;
  if (!force && mode==previousMode && frame==previousFrame) return;
  previousMode=mode;previousFrame=frame;
  M5.Display.fillRect(278,229,42,11,TFT_BLACK);
  if (!mode) {M5.Display.fillCircle(311,235,1,TFT_DARKGREY);return;}
  uint16_t color=mode==1?TFT_CYAN:mode==2?TFT_GREEN:TFT_ORANGE;
  M5.Display.setTextSize(1);M5.Display.setTextColor(color,TFT_BLACK);
  M5.Display.setCursor(282,layout::FOOTER_Y);M5.Display.print(mode==1?"GET":mode==2?"UPD":"ERR");
  if (mode==1) {
    const int dx[]={0,3,0,-3},dy[]={-3,0,3,0};
    for (int i=0;i<4;++i) M5.Display.drawLine(311,235,311+dx[i],235+dy[i],i==frame?color:TFT_DARKGREY);
  } else if (mode==2) {
    M5.Display.drawLine(308,235,310,237,color);M5.Display.drawLine(310,237,315,232,color);
  } else {
    M5.Display.drawLine(308,232,315,238,color);M5.Display.drawLine(308,238,315,232,color);
  }
}
void drawSpeakerIndicator(bool force=false) {
  static int previous=-1;
  bool enabled=glucoseAlerts.isEnabled();
  if (!force && previous==int(enabled)) return;
  previous=enabled;
  M5.Display.fillRect(174,0,26,14,TFT_BLACK);
  uint16_t color=enabled?TFT_CYAN:TFT_DARKGREY;
  M5.Display.fillRect(179,4,4,5,color);
  M5.Display.fillTriangle(182,4,188,1,188,11,color);
  if (enabled) {
    M5.Display.drawLine(191,3,193,5,color);
    M5.Display.drawLine(193,5,193,8,color);
    M5.Display.drawLine(193,8,191,10,color);
    M5.Display.drawPixel(190,6,color);
  } else M5.Display.drawLine(178,1,197,12,TFT_RED);
}
void drawPowerIndicator(bool force=false) {
  static bool sampled=false,charging=false;
  static uint32_t sampledAt=0;
  static int level=-1,source=0;
  static int shownLevel=-999,shownSource=-999,shownCharging=-1;
  uint32_t tick=millis();
  if (!sampled || tick-sampledAt>=1000) {
    sampled=true;sampledAt=tick;
    bool battery=true,usb=false,external=false,known=true;
    if (M5.Power.getType()==m5::Power_Class::pmic_axp192) {
      uint8_t status[2]={};
      known=M5.Power.Axp192.readRegister(0x00,status,sizeof(status));
      if (known) {
        auto input=powerstate::classify(status[0],
          M5.Power.Axp192.getVBUSVoltage(),M5.Power.Axp192.getVBUSCurrent(),
          M5.Power.Axp192.getACINVoltage(),M5.Power.Axp192.getACINCurrent());
        usb=input==powerstate::Source::USB;external=input==powerstate::Source::External;
        battery=status[1]&0x20;
        charging=(usb || external) && (status[1]&0x40);
      } else {battery=false;charging=false;}
    } else {
      int mv=M5.Power.getVBUSVoltage();known=mv>=0;usb=mv>=4000;
      charging=usb && M5.Power.isCharging()==m5::Power_Class::is_charging;
    }
    level=battery?M5.Power.getBatteryLevel():-1;
    if (level<0) level=-1;else level=std::min(level,100);
    source=known?(usb?1:external?2:0):-1;
  }
  if (!force && level==shownLevel && source==shownSource && int(charging)==shownCharging) return;
  shownLevel=level;shownSource=source;shownCharging=charging;
  M5.Display.fillRect(200,0,48,12,TFT_BLACK);
  uint16_t color=source>0?TFT_GREEN:source<0?TFT_LIGHTGREY:level<0?TFT_LIGHTGREY:level<=20?TFT_RED:TFT_WHITE;
  M5.Display.drawRect(201,2,14,8,color);
  M5.Display.fillRect(215,4,2,4,color);
  if (level>=0) {
    int fill=level?std::max(1,(level*10+50)/100):0;
    if (fill) M5.Display.fillRect(203,4,fill,4,color);
  } else M5.Display.drawFastHLine(205,6,5,color);
  String text=source<0?"?":source==1?"USB":source==2?"EXT":level>=0?String(level)+"%":"--%";
  if (source>0 && charging) text+="+";
  M5.Display.setTextSize(1);M5.Display.setTextColor(color,TFT_BLACK);
  M5.Display.setCursor(221,2);M5.Display.print(text);
}
void drawWifiIndicator(bool force=false) {
  static bool sampled=false,connected=false;
  static uint32_t sampledAt=0;
  static int bars=0,shownBars=-1,shownConnected=-1;
  uint32_t tick=millis();
  if (!sampled || tick-sampledAt>=1000) {
    sampled=true;sampledAt=tick;
    connected=WiFi.status()==WL_CONNECTED;
    int rssi=connected?WiFi.RSSI():0;
    bars=!connected || rssi>=0?0:rssi>=-55?4:rssi>=-67?3:rssi>=-75?2:1;
  }
  if (!force && bars==shownBars && int(connected)==shownConnected) return;
  shownBars=bars;shownConnected=connected;
  M5.Display.fillRect(248,0,16,12,TFT_BLACK);
  if (!connected) {
    M5.Display.drawLine(250,3,260,10,TFT_RED);
    M5.Display.drawLine(250,10,260,3,TFT_RED);
    return;
  }
  uint16_t color=bars>=3?TFT_GREEN:bars==2?TFT_YELLOW:TFT_ORANGE;
  for (int i=0;i<4;++i) {
    int height=(i+1)*2;
    M5.Display.fillRect(249+i*3,10-height,2,height,i<bars?color:TFT_DARKGREY);
  }
}
void drawLiveFields(int64_t now,bool force=false) {
  static char previousClock[9]={};
  static String previousAge,previousLoop;
  static bool previousBgFresh=false,previousApsFresh=false;
  char clock[9]="--:--:--";
  if (now>=model::MIN_TIME) {
    time_t seconds=time_t(now);struct tm local;
    if (localtime_r(&seconds,&local)) strftime(clock,sizeof(clock),"%H:%M:%S",&local);
  }
  if (force || strcmp(clock,previousClock)) {
    M5.Display.fillRect(264,0,56,12,TFT_BLACK);
    M5.Display.setTextSize(1);M5.Display.setTextColor(TFT_WHITE,TFT_BLACK);
    M5.Display.setCursor(267,2);M5.Display.print(clock);
    strlcpy(previousClock,clock,sizeof(previousClock));
  }
  int64_t glucoseTime=ui.history.count?ui.history.p[ui.history.count-1].t:0;
  String ageText="BG "+age(glucoseTime,now);
  String loopText="APS "+age(ui.status.loopTime,now);
  bool bgFresh=model::fresh(glucoseTime,now,uiSettings.sgvStale);
  bool apsFresh=model::fresh(ui.status.loopTime,now,uiSettings.apsStale);
  if (force || ageText!=previousAge || loopText!=previousLoop || bgFresh!=previousBgFresh || apsFresh!=previousApsFresh) {
    M5.Display.fillRect(174,32,146,12,TFT_BLACK);
    M5.Display.setTextSize(1);M5.Display.setTextColor(bgFresh?TFT_LIGHTGREY:TFT_ORANGE,TFT_BLACK);
    M5.Display.setCursor(176,35);M5.Display.print(ageText);
    // Bright blue distinguishes AAPS age from the glucose age beside it.
    M5.Display.setTextColor(apsFresh?0x551F:TFT_ORANGE,TFT_BLACK);
    M5.Display.setCursor(315-M5.Display.textWidth(loopText),35);M5.Display.print(loopText);
    M5.Display.fillRect(174,16,146,12,TFT_BLACK);
    if(!apsFresh) {M5.Display.setTextColor(TFT_ORANGE,TFT_BLACK);M5.Display.setCursor(176,18);M5.Display.print("No recent loop");}
    previousAge=ageText;previousLoop=loopText;previousBgFresh=bgFresh;previousApsFresh=apsFresh;
  }
}
uint16_t glucoseColor(float value,bool fresh) {
  if (!fresh) return TFT_LIGHTGREY;
  if (value<uiSettings.low) return TFT_RED;
  if (value>uiSettings.high) return TFT_ORANGE;
  return TFT_GREEN;
}
void glucoseSegment(int x1,int y1,float v1,int x2,int y2,float v2,bool fresh) {
  int steps=std::max(abs(x2-x1),abs(y2-y1));
  for (int i=0;i<=steps;++i) {
    float part=float(i)/std::max(1,steps);
    M5.Display.drawPixel(x1+(x2-x1)*i/std::max(1,steps),
      y1+(y2-y1)*i/std::max(1,steps),glucoseColor(v1+(v2-v1)*part,fresh));
  }
}
void drawSettingsCog(int x,int y) {
  const uint16_t color=TFT_LIGHTGREY;
  M5.Display.drawCircle(x,y,3,color);M5.Display.drawCircle(x,y,1,color);
  for(int i=0;i<8;++i) {
    float a=i*0.78539816f;
    M5.Display.drawLine(x+int(std::lround(3*std::cos(a))),y+int(std::lround(3*std::sin(a))),
      x+int(std::lround(5*std::cos(a))),y+int(std::lround(5*std::sin(a))),color);
  }
}
void drawFooterControls() {
  M5.Display.setFont(&fonts::Font0);M5.Display.setTextSize(1);
  M5.Display.setTextColor(TFT_LIGHTGREY,TFT_BLACK);
  M5.Display.setCursor(38,231);M5.Display.print("A");
  M5.Display.setCursor(145,231);M5.Display.print("B");
  M5.Display.setCursor(243,231);M5.Display.print("C");
  // Circular refresh arrow, drawn directly to avoid missing font glyphs.
  const int cx=53,cy=234;
  for(int i=0;i<15;++i) {
    float a=(45+i*18)*0.017453293f,b=(45+(i+1)*18)*0.017453293f;
    M5.Display.drawLine(cx+int(std::lround(4*std::cos(a))),cy+int(std::lround(4*std::sin(a))),
      cx+int(std::lround(4*std::cos(b))),cy+int(std::lround(4*std::sin(b))),TFT_GREEN);
  }
  M5.Display.fillTriangle(cx+3,cy-4,cx+5,cy-1,cx+1,cy-1,TFT_GREEN);
  drawSettingsCog(160,234);
  // Actual line followed by a dashed projection: the forecast shortcut.
  uint16_t forecastColor=showForecast?TFT_CYAN:TFT_DARKGREY;
  M5.Display.drawLine(253,238,256,234,forecastColor);
  M5.Display.drawLine(256,234,259,236,forecastColor);
  M5.Display.drawFastVLine(259,230,9,TFT_DARKGREY);
  M5.Display.drawLine(261,234,262,233,forecastColor);
  M5.Display.drawLine(264,232,265,231,forecastColor);
  M5.Display.fillTriangle(268,229,268,232,265,229,forecastColor);
}
void draw() {
  M5.Display.fillScreen(TFT_BLACK);
  int64_t now=displayNow();
  bool have=ui.history.count>0;
  model::Point last=have?ui.history.p[ui.history.count-1]:model::Point{};
  bool fresh=have && model::fresh(last.t,now,uiSettings.sgvStale);
  uint16_t col=glucoseColor(last.sgv,fresh);
  line(2,"NIGHTSCOUT",TFT_LIGHTGREY);
  M5.Display.setTextColor(TFT_LIGHTGREY,TFT_BLACK);
  M5.Display.setCursor(77,2);M5.Display.print(mmol?"mmol/L":"mg/dL");
  if (!fresh) {M5.Display.setTextColor(TFT_ORANGE,TFT_BLACK);M5.Display.setCursor(125,2);M5.Display.print("STALE");}
  String glucoseText=have?amount(mmol?last.sgv/18.0182f:last.sgv,mmol?1:0):"--";
  M5.Display.setFont(&fonts::FreeSansBold18pt7b);
  M5.Display.setTextSize(1);M5.Display.setTextColor(col,TFT_BLACK);
  M5.Display.setTextDatum(lgfx::textdatum_t::baseline_left);
  M5.Display.drawString(glucoseText,5,43);
  int arrowCenter=5+M5.Display.textWidth(glucoseText)+26;
  M5.Display.setFont(&fonts::Font0);
  M5.Display.setTextDatum(lgfx::textdatum_t::top_left);
  drawTrendArrow(have?last.direction:"NONE",arrowCenter,33,col);
  // History, treatment lanes and a separately marked future interval.
  const auto &pred=ui.status.forecast;
  bool predicted=showForecast && fresh && model::fresh(pred.t,now,uiSettings.apsStale);
  int64_t start=now-int64_t(graphHours.load())*3600;
  int64_t end=now+(predicted?3600:0);
  auto xx=[&](int64_t t){return 30+int((t-start)*284/(end-start));};
  M5.Display.setFont(&fonts::Font0);M5.Display.setTextSize(1);
  const int labelLeft=30,labelRight=std::min(314,xx(now));
  static smblabels::Layout smbLabels;smbLabels.reset();
  for(size_t i=0;i<ui.treatments.count;++i) {
    const auto &event=ui.treatments.events[i];
    if(event.t>=start && event.t<=now && event.smb && event.insulin>0) smbLabels.add(xx(event.t),event.insulin);
  }
  auto smbText=[&](const smblabels::Group &group) {
    String text=compactAmount(group.units);if(group.count>1)text="="+text;return text;
  };
  bool labelsFit=smbLabels.arrange(labelLeft,labelRight,[&](const smblabels::Group &group){return M5.Display.textWidth(smbText(group));});
  bool secondInsulinRow=false;
  if(labelsFit)for(size_t i=0;i<smbLabels.size;++i)secondInsulinRow|=smbLabels.groups[i].lane==1;
  struct BolusLabel {size_t index;int x,lane;};
  static BolusLabel bolusLabels[smblabels::CAPACITY];size_t bolusLabelCount=0;
  int lastBolusEnd[2]={labelLeft,labelLeft};
  for(size_t i=0;i<ui.treatments.count;++i) {
    const auto &event=ui.treatments.events[i];
    if(event.smb || event.insulin<=0 || event.t<start || event.t>now)continue;
    String text="B"+compactAmount(event.insulin);int width=M5.Display.textWidth(text);
    int x=std::max(labelLeft,std::min(xx(event.t)-width/2,labelRight+1-width));
    for(int row=0;row<2;++row)if(x>=lastBolusEnd[row] && (!labelsFit || !smbLabels.overlaps(row,x,width))) {
      if(bolusLabelCount<smblabels::CAPACITY)bolusLabels[bolusLabelCount++]={i,x,row};
      secondInsulinRow|=row==1;lastBolusEnd[row]=x+width+3;break;
    }
  }
  const int plotBottom=secondInsulinRow?152:162;
  graphscale::Range visibleRange;
  for (size_t i=0;i<ui.history.count;++i) {
    const auto &point=ui.history.p[i];
    if (point.t>=start && point.t<=now) visibleRange.add(point.sgv);
  }
  auto scale=visibleRange.bounds();float low=scale.low,high=scale.high;
  bool forecastClipped=false;
  if (predicted) for(size_t k=0;k<model::PRED_LINES;++k) for(size_t i=0;i<pred.count[k];++i) {
    int64_t t=pred.t+int64_t(i)*300;float v=pred.bg[k][i];
    if(t>=now-300 && t<=end+300 && std::isfinite(v) && (v<low || v>high)) forecastClipped=true;
  }
  auto yy=[&](float v){return plotBottom-int((v-low)/(high-low)*(plotBottom-layout::PLOT_TOP));};
  // Shade each plot row by its represented glucose value. This keeps bands
  // clipped to the dynamic scale with no gaps/overlapping boundary pixels.
  for (int y=layout::PLOT_TOP;y<=plotBottom;++y) {
    float value=low+(high-low)*float(plotBottom-y)/(plotBottom-layout::PLOT_TOP);
    uint16_t background=value<70?0x2000:value<=180?0x0120:value<=249?0x20A0:0x2000;
    M5.Display.drawFastHLine(30,y,285,background);
  }
  M5.Display.setTextSize(1);
  auto label=[&](int x,int y,const String &text,uint16_t color) {
    M5.Display.setTextColor(color,TFT_BLACK);M5.Display.setCursor(x,y);M5.Display.print(text);
  };
  label(30,layout::LEGEND_Y,"C g",TFT_ORANGE);label(65,layout::LEGEND_Y,"SMB U",TFT_CYAN);label(110,layout::LEGEND_Y,"Bol U",TFT_LIGHTGREY);
  M5.Display.drawFastVLine(155,51,8,0x4208);
  if(predicted) {
    const char *keys[]={"IOB","COB","UAM","ZT"};const uint16_t colors[]={TFT_MAGENTA,TFT_ORANGE,TFT_YELLOW,TFT_CYAN};
    int legendX=170;
    for(size_t k=0;k<model::PRED_LINES;++k)if(pred.count[k]) {label(legendX,layout::LEGEND_Y,keys[k],colors[k]);legendX+=36;}
    if(forecastClipped) {label(307,layout::LEGEND_Y,"*",TFT_ORANGE);label(128,18,"CLIP",TFT_ORANGE);}
  } else label(170,layout::LEGEND_Y,showForecast?"Forecast stale":"Forecast off",TFT_DARKGREY);
  M5.Display.drawFastHLine(18,49,297,0x4208);M5.Display.drawFastHLine(18,60,297,0x4208);
  auto axisLabel=[&](int y,float mgdl) {
    String text=amount(mmol?mgdl/18.0182f:mgdl,mmol?1:0);
    label(std::max(0,27-M5.Display.textWidth(text)),y,text,TFT_LIGHTGREY);
  };
  axisLabel(layout::PLOT_TOP,high);
  axisLabel(plotBottom-8,low);
  for(int v:{70,180})if(v>=low && v<=high) {
    int y=yy(v);M5.Display.drawFastHLine(30,y,285,0x2104);
    if(y-4>layout::PLOT_TOP+10 && y-4<plotBottom-13)axisLabel(y-4,v);
  }
  if (predicted) M5.Display.drawFastVLine(xx(now),layout::PLOT_TOP,plotBottom-layout::PLOT_TOP+1,TFT_LIGHTGREY);
  int px=-1,py=0;int64_t pt=0;float previousGlucose=NAN;
  for (size_t i=0;i<ui.history.count;++i) {
    const auto &p=ui.history.p[i];if (p.t<start || p.t>now) continue;
    int x=xx(p.t),y=yy(p.sgv);
    if (px>=0 && p.t-pt<=720) glucoseSegment(px,py,previousGlucose,x,y,p.sgv,fresh);
    M5.Display.fillCircle(x,y,1,glucoseColor(p.sgv,fresh));
    px=x;py=y;pt=p.t;previousGlucose=p.sgv;
  }
  // Emphasize the newest actual reading without thickening the entire trace.
  if (px>=0) M5.Display.fillCircle(px,py,2,glucoseColor(previousGlucose,fresh));
  else label(55,layout::PLOT_TOP+20,"No glucose in this window",TFT_LIGHTGREY);
  if (predicted) {
    const uint16_t colors[]={TFT_MAGENTA,TFT_ORANGE,TFT_YELLOW,TFT_CYAN};
    for (size_t k=0;k<model::PRED_LINES;++k) for (size_t i=1;i<pred.count[k];++i) {
      int64_t ta=pred.t+int64_t(i-1)*300,tb=ta+300;
      float va=pred.bg[k][i-1],vb=pred.bg[k][i];
      if (!std::isfinite(va) || !std::isfinite(vb) || tb<=now || ta>=end) continue;
      // Clip crossing segments at now and the forecast pane's right edge.
      int64_t ca=std::max(ta,now),cb=std::min(tb,end);
      float ya=va+(vb-va)*float(ca-ta)/300, yb=va+(vb-va)*float(cb-ta)/300;
      int x1=xx(ca),x2=xx(cb),y1=yy(ya),y2=yy(yb);
      int steps=std::max(abs(x2-x1),abs(y2-y1));
      for (int q=0;q<=steps;++q) if (q%5<3) {
        int x=x1+(x2-x1)*q/std::max(1,steps),y=y1+(y2-y1)*q/std::max(1,steps);
        if (x>=xx(now) && x<=314 && y>=layout::PLOT_TOP && y<=plotBottom) M5.Display.drawPixel(x,y,colors[k]);
      }
    }
  }
  // Keep treatment lanes at the plot bottom as the glucose scale changes.
  // Amount labels use the reserved rows beneath the plot.
  const int treatmentLine=plotBottom;
  const int carbMarkerY=std::max(layout::PLOT_TOP+3,treatmentLine-5);
  const int insulinMarkerY=std::max(layout::PLOT_TOP+3,treatmentLine-12);
  int lastCarbLabel=labelLeft;
  for (size_t i=0;i<ui.treatments.count;++i) {
    const auto &e=ui.treatments.events[i];if (e.t<start || e.t>now) continue;
    int x=xx(e.t);
    if (e.carbs>0) {
      M5.Display.fillTriangle(x,carbMarkerY-2,x-2,carbMarkerY+2,x+2,carbMarkerY+2,TFT_ORANGE);
      String text="C"+compactAmount(e.carbs);
      int width=M5.Display.textWidth(text),tx=std::max(labelLeft,std::min(x-width/2,labelRight+1-width));
      if (tx>=lastCarbLabel) {label(tx,treatmentLine+2,text,TFT_ORANGE);lastCarbLabel=tx+width+3;}
    }
    if (e.insulin>0) {
      M5.Display.fillCircle(x,insulinMarkerY,e.smb?2:1,e.smb?TFT_CYAN:TFT_LIGHTGREY);
    }
  }
  if (labelsFit) for (size_t i=0;i<smbLabels.size;++i) {
    const auto &g=smbLabels.groups[i];int y=treatmentLine+13+g.lane*11;
    if (g.count>1) {
      M5.Display.drawFastHLine(g.first,y-2,g.last-g.first+1,TFT_CYAN);
      M5.Display.drawFastVLine(g.first,y-3,2,TFT_CYAN);
      M5.Display.drawFastVLine(g.last,y-3,2,TFT_CYAN);
    } else M5.Display.drawLine(g.first,y-3,g.x+g.width/2,y-1,TFT_CYAN);
    label(g.x,y,smbText(g),TFT_CYAN);
  }
  // Manual boluses use the positions reserved during the layout pass.
  for(size_t i=0;i<bolusLabelCount;++i) {
    const auto &placement=bolusLabels[i];const auto &event=ui.treatments.events[placement.index];
    String text="B"+compactAmount(event.insulin);int y=treatmentLine+13+placement.lane*11;
    M5.Display.drawLine(xx(event.t),y-3,placement.x+M5.Display.textWidth(text)/2,y-1,TFT_LIGHTGREY);
    label(placement.x,y,text,TFT_LIGHTGREY);
  }
  label(30,layout::TIME_Y,"-"+String(graphHours.load())+"h",TFT_LIGHTGREY);
  label(std::max(62,xx(now)-12),layout::TIME_Y,"now",TFT_LIGHTGREY);
  if (predicted) label(278,layout::TIME_Y,"+1h",TFT_LIGHTGREY);
  // Treatment and time labels occupy y154..191, between these dividers.
  M5.Display.drawFastHLine(30,plotBottom+1,285,0x4208);
  M5.Display.drawFastHLine(30,192,285,0x4208);
  auto &s=ui.status;
  auto value=[&](const model::Metric &m,unsigned d){ return model::fresh(m.t,now,uiSettings.apsStale)?amount(m.value,d):String("--");};
  // Quieter labels with stronger, fixed-position metric values.
  M5.Display.setFont(&fonts::Font0);M5.Display.setTextSize(1);M5.Display.setTextColor(TFT_LIGHTGREY,TFT_BLACK);
  M5.Display.setCursor(5,201);M5.Display.print("IOB");M5.Display.setCursor(132,201);M5.Display.print("U");
  M5.Display.setCursor(165,201);M5.Display.print("COB");M5.Display.setCursor(299,201);M5.Display.print("g");
  M5.Display.setFont(&fonts::FreeSansBold12pt7b);M5.Display.setTextColor(TFT_WHITE,TFT_BLACK);
  M5.Display.setTextDatum(lgfx::textdatum_t::baseline_right);
  M5.Display.drawString(value(s.iob,2),124,211);M5.Display.drawString(value(s.cob,0),291,211);
  M5.Display.setFont(&fonts::Font0);M5.Display.setTextDatum(lgfx::textdatum_t::top_left);
  bool basalFresh=model::fresh(s.basal.t,now,uiSettings.apsStale) && (!s.tempUntil || now<s.tempUntil);
  String basalText="Basal "+(basalFresh?amount(s.basal.value):String("--"))+" U/h ("+s.basalKind+")";
  line(layout::BASAL_Y,basalText,TFT_WHITE);
  String modeValue=model::fresh(s.pumpTime,now,uiSettings.apsStale)?s.pump:"Unknown/stale";
  String modeText="Mode "+modeValue;
  int available=315-(5+M5.Display.textWidth(basalText)+12);
  if (M5.Display.textWidth(modeText)>available) {
    while (modeValue.length() && M5.Display.textWidth("Mode "+modeValue+"...")>available)
      modeValue.remove(modeValue.length()-1);
    modeText="Mode "+modeValue+"...";
  }
  M5.Display.setTextColor(TFT_LIGHTGREY,TFT_BLACK);
  M5.Display.setCursor(315-M5.Display.textWidth(modeText),layout::BASAL_Y);M5.Display.print(modeText);
  String err=*ui.glucoseError?String("SGV: ")+ui.glucoseError:*ui.statusError?String("AAPS: ")+ui.statusError:*ui.treatmentError?String("Treat: ")+ui.treatmentError:ui.treatments.limited?"Treatments limited; reduce graph hours":"";
  M5.Display.setTextSize(1);
  if (M5.Display.textWidth(err)>267) {
    while (err.length() && M5.Display.textWidth(err+"...")>267) err.remove(err.length()-1);
    err+="...";
  }
  bool footerWarning=*ui.glucoseError || *ui.statusError || *ui.treatmentError || ui.treatments.limited;
  if(footerWarning) line(layout::FOOTER_Y,err,TFT_ORANGE);
  else drawFooterControls();
  drawLiveFields(now,true);
  drawSpeakerIndicator(true);
  drawPowerIndicator(true);
  drawWifiIndicator(true);
  drawActivityIndicator(true);
  renderedPayload=payloadFingerprint();renderedTimed=timedFingerprint(now);rendered=true;
}
void updateGlucoseAlerts(int64_t now) {
  static bool retrying=false;
  static alerts::Event retryEvent=alerts::Event::None;
  static uint32_t lastAttempt=0;
  uint32_t tick=millis();
  bool have=ui.history.count>0;
  model::Point latest=have?ui.history.p[ui.history.count-1]:model::Point{};
  alerts::Event event=glucoseAlerts.poll(latest.sgv,
    have && model::fresh(latest.t,now,uiSettings.sgvStale),tick,uiSettings.low,uiSettings.high);
  if (event==alerts::Event::None) {retrying=false;return;}
  if (staleSoundActive || M5.Speaker.isPlaying()) return;
  if (retrying && retryEvent==event && uint32_t(tick-lastAttempt)<30000) return;
  lastAttempt=tick;retryEvent=event;
  if (M5.Speaker.tone(alerts::CHIRP_HZ,alerts::CHIRP_MS)) {
    glucoseAlerts.played(event,tick);retrying=false;
  } else {
    retrying=true;Serial.println("Glucose chirp could not be queued; will retry while fresh.");
  }
}
// Two lower descending notes distinguish missing updates from the SGV chirp.
// Called every loop; tone scheduling never blocks UI or network work.
void updateStaleAlerts(int64_t now) {
  static bool startupGraceDone=false,retrying=false;
  static uint32_t phaseAt=0,lastAttempt=0;
  static uint8_t sounding=0;
  uint32_t tick=millis();
  if (tick>=600000) startupGraceDone=true;
  bool have=ui.history.count>0;
  int64_t bg=have?ui.history.p[ui.history.count-1].t:0;
  uint8_t pending=staleAlerts.poll(
    (have || startupGraceDone) && !model::fresh(bg,now,alerts::STALE_SECONDS),
    (ui.status.loopTime>0 || startupGraceDone) && !model::fresh(ui.status.loopTime,now,alerts::STALE_SECONDS),
    glucoseAlerts.isEnabled());
  if (staleSoundActive) {
    if (!(pending & sounding)) {M5.Speaker.stop();staleSoundActive=false;sounding=0;return;}
    if (uint32_t(tick-phaseAt)>=300 && !M5.Speaker.isPlaying()) {
      if (M5.Speaker.tone(650,160)) {staleAlerts.played(sounding);retrying=false;}
      else {retrying=true;lastAttempt=tick;}
      staleSoundActive=false;sounding=0;
    }
    return;
  }
  if (!pending) {retrying=false;return;}
  if (M5.Speaker.isPlaying() || (retrying && uint32_t(tick-lastAttempt)<30000)) return;
  if (M5.Speaker.tone(950,160)) {sounding=pending;phaseAt=tick;staleSoundActive=true;}
  else {retrying=true;lastAttempt=tick;}
}
bool applyUiSettings(bool force=false) {
  if(!force && uiRevision==settingsRevision.load())return false;
  runtimecfg::Settings next;uint32_t revision,epoch;currentSettings(next,revision,&epoch);
  bool changedThresholds=uiSettings.low!=next.low || uiSettings.high!=next.high;
  uiSettings=next;uiRevision=revision;mmol=next.mmol;showForecast=next.forecast;graphHours.store(next.hours);
  if(changedThresholds && glucoseAlerts.isEnabled())glucoseAlerts.setEnabled(false);
  glucoseAlerts.setEnabled(next.sound);
  M5.Display.setBrightness(settingsOpen?std::max(20u,next.brightness):next.brightness);M5.Speaker.setVolume(next.volume);
  if(!next.sound) {M5.Speaker.stop();staleSoundActive=false;}
  if(uiEpoch!=epoch) {
    glucoseAlerts.setEnabled(false);glucoseAlerts.setEnabled(next.sound);staleAlerts=alerts::StaleState{};
    if(staleSoundActive)M5.Speaker.stop();staleSoundActive=false;
    int64_t clock=ui.clockAt,mono=ui.monoAt;resetSnapshot(ui);ui.clockAt=clock;ui.monoAt=mono;ui.connectionEpoch=epoch;uiEpoch=epoch;
  }
  rendered=false;return true;
}
void saveDisplaySettings() {
  runtimecfg::Settings next;uint32_t revision;currentSettings(next,revision);
  next.hours=graphHours.load();next.mmol=mmol;next.forecast=showForecast;next.sound=glucoseAlerts.isEnabled();
  storeSettings(next,revision,settingsMessage,sizeof(settingsMessage));applyUiSettings(true);
}
void toggleGlucoseAlerts() {
  glucoseAlerts.setEnabled(!glucoseAlerts.isEnabled());saveDisplaySettings();
  if(!settingsOpen)drawSpeakerIndicator(true);
  updateGlucoseAlerts(displayNow());
}
void drawSettings() {
  M5.Display.setBrightness(std::max(20u,uiSettings.brightness));
  M5.Display.fillScreen(TFT_BLACK);M5.Display.setFont(&fonts::Font2);
  M5.Display.setTextSize(1);M5.Display.setTextDatum(lgfx::textdatum_t::top_left);
  M5.Display.setTextColor(TFT_WHITE,TFT_BLACK);M5.Display.drawString("Quick settings",8,3);
  M5.Display.setFont(&fonts::Font0);
  auto text=[](int x,int y,const String &value,uint16_t color=TFT_LIGHTGREY) {
    M5.Display.setTextColor(color,TFT_BLACK);M5.Display.setCursor(x,y);M5.Display.print(value);
  };
  auto button=[&](int x,int y,int w,const String &value,bool selected=false) {
    M5.Display.drawRoundRect(x,y,w,28,4,selected?TFT_CYAN:0x632C);
    int tx=x+(w-M5.Display.textWidth(value))/2;text(tx,y+10,value,selected?TFT_CYAN:TFT_WHITE);
  };
  button(258,2,58,"Back");
  int state=portalState.load();
  if(state==1 || state==2 || state==3 || state==5) {
    text(8,40,"Wi-Fi setup",TFT_CYAN);
    if(state==1) text(8,60,"Waiting for current fetch to finish...");
    else {
      text(8,61,"Join this temporary network on your phone:");
      text(8,80,portalName,TFT_WHITE);
      text(8,100,String("Password: ")+portalPassword,TFT_WHITE);
      text(8,124,"Then open http://192.168.4.1",TFT_CYAN);
      text(8,146,state==3?"Testing connection...":state==5?"Test/save failed. Retry on your phone.":"Enter your 2.4 GHz network and password.");
      text(8,171,"Setup closes after 5 minutes.");
      text(8,187,"Nightscout updates pause during setup.");
      text(8,203,"Cached data ages; enabled alerts keep running.");
    }
    text(8,226,"Back cancels setup.");return;
  }
  if(wifiMenuOpen) {
    text(8,43,"Wi-Fi connections",TFT_CYAN);
    button(8,76,304,"Reconnect saved Wi-Fi");button(8,120,304,"Temporary setup hotspot");
    text(8,166,"Manage saved networks in the LAN portal.");
    text(8,190,"Hotspot setup also works without LAN.");return;
  }
  text(8,35,"Graph history");
  const unsigned hours[]={1,3,6,12};
  for(int i=0;i<4;++i)button(8+i*77,49,71,String(hours[i])+" hours",graphHours.load()==hours[i]);
  button(8,87,146,String("Sound: ")+(glucoseAlerts.isEnabled()?"On":"Off"),glucoseAlerts.isEnabled());
  button(166,87,146,String("Forecast: ")+(showForecast?"On":"Off"),showForecast);
  button(8,125,146,String("Units: ")+(mmol?"mmol/L":"mg/dL"));
  button(166,125,146,"Wi-Fi setup");
  button(8,159,304,String("LAN portal: ")+(lanRequested.load()?"On":"Off"),lanRequested.load());
  if(lanRequested.load()) {
    if(lanStatus.state==1) {text(8,198,lanStatus.address,TFT_CYAN);text(8,212,String("Access code: ")+lanStatus.code,TFT_WHITE);}
    else text(8,198,"Waiting for LAN Wi-Fi...",TFT_ORANGE);
  } else {
    text(8,198,"Off by default; port 3000");
    String network=WiFi.status()==WL_CONNECTED?WiFi.SSID():String("Disconnected");
    while(network.length() && M5.Display.textWidth(network)>290)network.remove(network.length()-1);
    text(8,212,network);
  }
  if(*settingsMessage)text(8,229,String(settingsMessage).substring(0,50),TFT_ORANGE);

}
void closeSettings() {
  if(wifiMenuOpen) {wifiMenuOpen=false;drawSettings();return;}
  int state=portalState.load();
  if(state==1 || state==2 || state==3 || state==5) portalCancel.store(true);
  settingsOpen=false;wifiMenuOpen=false;M5.Display.setBrightness(uiSettings.brightness);rendered=false;
}
void settingsTouch(int x,int y) {
  if(x>=258 && y<32) {closeSettings();return;}
  int state=portalState.load();
  if(state==1 || state==2 || state==3 || state==5) return;
  if(wifiMenuOpen) {
    if(y>=76 && y<104 && x>=8 && x<312) {reconnectRequested.store(true);xTaskNotifyGive(networkTask);}
    if(y>=120 && y<148 && x>=8 && x<312) {
      wifiMenuOpen=false;portalCancel.store(false);portalState.store(1);portalRequested.store(true);xTaskNotifyGive(networkTask);
    }
    drawSettings();return;
  }
  if(y>=49 && y<77 && x>=8 && x<312) {
    const unsigned hours[]={1,3,6,12};int column=(x-8)/77;
    if(column<4 && (x-8)%77<71) {graphHours.store(hours[column]);saveDisplaySettings();xTaskNotifyGive(networkTask);}
  } else if(y>=87 && y<115) {
    if(x>=8 && x<154) toggleGlucoseAlerts();
    else if(x>=166 && x<312) {showForecast=!showForecast;saveDisplaySettings();}
  } else if(y>=125 && y<153) {
    if(x>=8 && x<154) {mmol=!mmol;saveDisplaySettings();}
    else if(x>=166 && x<312) {
      wifiMenuOpen=true;
    }
  } else if(y>=159 && y<187 && x>=8 && x<312) {
    lanRequested.store(!lanRequested.load());lanStatus=LanStatus{};
  }
  drawSettings();
}
void setup() {
  auto c=M5.config(); c.internal_imu=false; c.internal_mic=false;
#ifdef RUN_PARSER_TESTS
  c.internal_spk=false;
#else
  c.internal_spk=true;
#endif
  M5.begin(c); M5.Display.setRotation(1); M5.Display.setBrightness(cfg::BRIGHTNESS);
  Serial.begin(115200);
  // Install once, before either worker can create a TLS context.
  if(!project_tls_memory_init()) {
    Serial.println("TLS memory allocator initialization failed");
    line(20,"TLS memory setup failed",TFT_RED);for(;;)delay(1000);
  }
  Serial.printf("TLS memory: PSRAM preferred, free=%u internal=%u\n",
    unsigned(heap_caps_get_free_size(MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT)),
    unsigned(heap_caps_get_free_size(MALLOC_CAP_INTERNAL|MALLOC_CAP_8BIT)));
  Serial.printf("TLS trust: Mozilla/certifi %s roots=%u verifier=root-key-anchors bundle=%s\n",CA_BUNDLE_SOURCE_VERSION,unsigned((caBundleStart[0]<<8)|caBundleStart[1]),CA_BUNDLE_SHA256);
  M5.Speaker.setVolume(alerts::VOLUME);
#ifdef RUN_PARSER_TESTS
  if(xTaskCreatePinnedToCore([](void*) {runParserTests();vTaskDelete(nullptr);},"parser-tests",49152,nullptr,1,nullptr,1)!=pdPASS)
    Serial.println("Parser test task allocation failed");
  for (;;) delay(1000);
#endif
  settingsMutex=xSemaphoreCreateMutex();networkGate=xSemaphoreCreateMutex();
  updates=xQueueCreate(1,sizeof(Snapshot));lanUpdates=xQueueCreate(1,sizeof(LanStatus));
  if(!settingsMutex || !networkGate || !updates || !lanUpdates) {
    line(20,"Memory allocation failed",TFT_RED);for(;;)delay(1000);
  }
  loadRuntimeSettings();applyUiSettings(true);
  if(xTaskCreatePinnedToCore(worker,"network",49152,nullptr,1,&networkTask,0)!=pdPASS ||
     xTaskCreatePinnedToCore(lanWorker,"lan-portal",16384,nullptr,1,nullptr,0)!=pdPASS) {
    line(20,"Task allocation failed",TFT_RED);for(;;)delay(1000);
  }
  draw();
}
void loop() {
  M5.update();
  bool settingsChanged=applyUiSettings();
  bool lanChanged=xQueueReceive(lanUpdates,&lanStatus,0)==pdTRUE;
  for (size_t i=0;i<M5.Touch.getCount();++i) {
    const auto &touch=M5.Touch.getDetail(i);if(!touch.wasPressed()) continue;
    if(settingsOpen) settingsTouch(touch.x,touch.y);
    else if(touch.y>=0 && touch.y<28) {
      if(touch.x>=174 && touch.x<200) toggleGlucoseAlerts();
    }
    break;
  }
  bool a=M5.BtnA.wasPressed(),b=M5.BtnB.wasPressed(),c=M5.BtnC.wasPressed();
  bool forecastPressed=false;
  if(settingsOpen) {if(a || b) closeSettings();}
  else {
    if(a) xTaskNotifyGive(networkTask);
    if(b) {settingsOpen=true;drawSettings();}
    forecastPressed=c && !b;
    if(forecastPressed) {showForecast=!showForecast;saveDisplaySettings();}
  }
  bool received=xQueueReceive(updates,&ui,0)==pdTRUE;
  if(received && ui.connectionEpoch!=uiEpoch) {
    int64_t clock=ui.clockAt,mono=ui.monoAt;resetSnapshot(ui);ui.clockAt=clock;ui.monoAt=mono;ui.connectionEpoch=uiEpoch;received=false;rendered=false;
  }
  if (received) {lastAppliedAt=millis();haveApplied=true;}
  bool dataChanged=received && payloadFingerprint()!=renderedPayload;
  static int64_t lastCheckedSecond=-1;
  int64_t now=displayNow();bool newSecond=now!=lastCheckedSecond;
  if(received || newSecond) updateGlucoseAlerts(now);
  updateStaleAlerts(now);
  bool timedChanged=(newSecond || received) && timedFingerprint(now)!=renderedTimed;
  if(settingsOpen) {
    static int previousPortal=-1,previousWifi=-1;
    int state=portalState.load(),wifi=int(WiFi.status());
    if(state!=previousPortal || wifi!=previousWifi || settingsChanged || lanChanged) {drawSettings();previousPortal=state;previousWifi=wifi;}
  } else {
    if(!rendered || dataChanged || timedChanged || forecastPressed) draw();
    else if(newSecond) drawLiveFields(now);
    drawPowerIndicator();drawWifiIndicator();drawActivityIndicator();
  }
  lastCheckedSecond=now;delay(10);
}
