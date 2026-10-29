#include <M5Unified.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <esp_timer.h>
#include <esp_heap_caps.h>
#include <Preferences.h>
#include <atomic>
#include "body_buffer.h"
#include "model.h"
#include "treatments.h"
#include "display_settings.h"
#include "alerts.h"
#include "power_state.h"
#include "ns_settings.h"
#include "ns_api.h"
#if __has_include("config.h")
#include "config.h"
#else
#include "config.example.h"
#endif
#ifdef RUN_PARSER_TESTS
#include "parser_tests.h"
#endif

extern const uint8_t rootStart[] asm("_binary_certs_root_ca_pem_start");
static_assert(cfg::GRAPH_HOURS>=1 && cfg::GRAPH_HOURS<=6,"Graph hours must be 1..6");
static_assert(cfg::POLL_SECONDS>=30,"Poll no faster than 30 seconds");
struct Snapshot {
  model::History history;
  model::Status status;
  model::Treatments treatments;
  char glucoseError[48]="Starting", statusError[48]="Starting", treatmentError[48]="Starting";
  int64_t clockAt=0, monoAt=0;
};
Snapshot ui;
QueueHandle_t updates;
TaskHandle_t networkTask;
bool mmol=cfg::MMOL;
bool showForecast=true;
alerts::State glucoseAlerts;
Preferences alertPreferences;
bool alertPreferencesReady=false;
std::atomic<bool> networkBusy{false};
uint32_t lastAppliedAt=0;
bool haveApplied=false;
enum class Resource {Glucose,Status,Treatments};
namespace layout {
constexpr int PLOT_TOP=72, PLOT_BOTTOM=160;
constexpr int LEGEND_Y=52, FORECAST_LEGEND_Y=62, TIME_Y=184;
constexpr int IOB_Y=194, BASAL_Y=216, FOOTER_Y=231;
}

int64_t monotonicSeconds() { return esp_timer_get_time()/1000000; }
int64_t displayNow() {
  return ui.clockAt ? ui.clockAt+monotonicSeconds()-ui.monoAt : 0;
}
bool configured() {
  String u=cfg::NS_URL;
  return strcmp(cfg::WIFI_SSID,"YOUR_2_4_GHZ_WIFI") &&
    strcmp(cfg::NS_TOKEN,"YOUR_READ_ONLY_ACCESS_TOKEN") && strlen(cfg::NS_TOKEN)>0 &&
    u.startsWith("https://") && u.length()>8 && !u.endsWith("/") &&
    u.indexOf('@')<0 && u.indexOf('?')<0 && u.indexOf('#')<0 &&
    !strchr(cfg::NS_TOKEN,'\r') && !strchr(cfg::NS_TOKEN,'\n');
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
bool ensureJwt(char *error,size_t cap) {
  int64_t now=time(nullptr);
  if (!nsapi::renewNeeded(jwt.length()>0,jwtExpiry,now)) return true;
  if (authCooling && uint32_t(millis()-authFailedAt)<30000) {
    strlcpy(error,lastAuthError,cap);return false;
  }
  auto fail=[&](const char *message){authFailure(message);strlcpy(error,message,cap);return false;};
  if (now<1767225600) return fail("V3 auth needs valid clock");
  WiFiClientSecure tls;tls.setCACert(reinterpret_cast<const char*>(rootStart));tls.setHandshakeTimeout(12);
  HTTPClient http;http.setUserAgent("ESP32HTTPClient");http.setConnectTimeout(10000);http.setTimeout(20000);http.setReuse(false);
  http.setFollowRedirects(HTTPC_DISABLE_FOLLOW_REDIRECTS);
  // This standard NS endpoint necessarily carries the subject token in its
  // TLS-protected path. Never print the path, credentials or response body.
  if (!http.begin(tls,String(cfg::NS_URL)+"/api/v2/authorization/request/"+urlEncode(cfg::NS_TOKEN)))
    return fail("V3 auth setup failed");
  http.addHeader("Accept","application/json");http.addHeader("Accept-Encoding","identity");
  int code=http.GET();
  if (code!=200) {
    http.end();char message[48];
    if (code<0) snprintf(message,sizeof(message),"Auth TLS/network %d",code);
    else snprintf(message,sizeof(message),"Auth HTTP %d",code);
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
  if (nscfg::USE_V3 && !ensureJwt(error,cap)) return false;
  const bool glucose=resource==Resource::Glucose;
  const char *label=glucose?"SGV":resource==Resource::Status?"AAPS":"Treatments";
  WiFiClientSecure tls;
  tls.setCACert(reinterpret_cast<const char*>(rootStart));
  tls.setHandshakeTimeout(12);
  HTTPClient http;
  http.setUserAgent("ESP32HTTPClient");
  http.setConnectTimeout(10000);
  http.setTimeout(20000);
  http.setReuse(false);
  http.setFollowRedirects(HTTPC_DISABLE_FOLLOW_REDIRECTS);
  if (!http.begin(tls,String(cfg::NS_URL)+path)) {strlcpy(error,"HTTPS setup failed",cap); return false;}
  http.addHeader("Accept","application/json");
  http.addHeader("Accept-Encoding","identity");
  if (nscfg::USE_V3) http.addHeader("Authorization","Bearer "+jwt);
  else http.addHeader("api-secret",cfg::NS_TOKEN);
  int code=http.GET();
  if (nscfg::USE_V3 && code==401 && allowRenew) {
    http.end();jwt="";jwtExpiry=0;
    if (!ensureJwt(error,cap)) return false;
    return fetch(path,resource,s,time(nullptr),error,cap,false);
  }
  if (nscfg::USE_V3 && code==401) authFailure("V3 access denied after renewal");
  bool ok=false;
  if (code!=200) {
    if (code<0) {
      char detail[160]={};
      int tlsCode=tls.lastError(detail,sizeof(detail));
      if (tlsCode==-0x2700) strlcpy(error,"TLS verify: CA/time/hostname",cap);
      else if (tlsCode<0) snprintf(error,cap,"TLS error -0x%04X",unsigned(-tlsCode));
      else snprintf(error,cap,"Network error %d",code);
      // No URL, headers, token or response body in diagnostic output.
      Serial.printf("%s HTTP-client=%d TLS=%d %s; UTC=%lld\n",
        label,code,tlsCode,detail,(long long)time(nullptr));
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
      nsapi::responseFilter(filter,items,nscfg::USE_V3);
      DynamicJsonDocument doc(65536);
      auto err=deserializeJson(doc,body.buffer.data,body.buffer.length,DeserializationOption::Filter(filter),DeserializationOption::NestingLimit(16));
      if (err) strlcpy(error,"Invalid/oversized JSON",cap);
      else {
        JsonVariantConst collection=nsapi::collection(doc.as<JsonVariantConst>(),nscfg::USE_V3);
        if (resource==Resource::Glucose) ok=model::entries(collection,now,s.history);
        else if (resource==Resource::Status) ok=model::statuses(collection,cfg::AAPS_DEVICE,now,s.status);
        else ok=model::treatments(collection,now-int64_t(cfg::GRAPH_HOURS)*3600,now,s.treatments);
        strlcpy(error,ok?"":nscfg::USE_V3?"Invalid V3 result/status":"Expected JSON array",cap);
      }
    }
  }
  http.end(); return ok;
}
void worker(void*) {
  static Snapshot s;
  unsigned failures=0;
  WiFi.mode(WIFI_STA); WiFi.setAutoReconnect(true); WiFi.persistent(false);
  configTzTime(displaycfg::TIMEZONE,cfg::NTP1,cfg::NTP2);
  for (;;) {
    networkBusy.store(true);
    bool good=false;
    if (!configured()) {
      strlcpy(s.glucoseError,"Edit include/config.h",sizeof(s.glucoseError));
      strlcpy(s.statusError,"Configuration required",sizeof(s.statusError));
    } else {
      if (WiFi.status()!=WL_CONNECTED) {
        WiFi.disconnect(); WiFi.begin(cfg::WIFI_SSID,cfg::WIFI_PASSWORD);
        for (int i=0;i<80 && WiFi.status()!=WL_CONNECTED;++i) vTaskDelay(pdMS_TO_TICKS(250));
      }
      if (WiFi.status()!=WL_CONNECTED) {
        strlcpy(s.glucoseError,"WiFi offline",sizeof(s.glucoseError));
        strlcpy(s.statusError,"WiFi offline",sizeof(s.statusError));
      } else {
        int64_t now=time(nullptr);
        if (now<1767225600) {
          strlcpy(s.glucoseError,"Waiting for NTP clock",sizeof(s.glucoseError));
          strlcpy(s.statusError,"TLS needs valid time",sizeof(s.statusError));
        } else {
          // Keep a monotonic anchor so an NTP outage never freezes data age.
          s.clockAt=now; s.monoAt=monotonicSeconds();
          const long long sinceMs=(now-int64_t(cfg::GRAPH_HOURS)*3600)*1000;
          char since[32],until[32];snprintf(since,sizeof(since),"%lld",sinceMs);
          snprintf(until,sizeof(until),"%lld",(long long)(now+120)*1000);
          String entryPath=nscfg::USE_V3?
            String("/api/v3/entries?type$eq=sgv&date$gte=")+since+"&date$lte="+until+
              "&sort$desc=date&limit=288&fields=date,sgv,direction,isValid,isDeleted":
            String("/api/v1/entries/sgv.json?count=288&find[date][$gte]=")+since;
          bool a=fetch(entryPath,Resource::Glucose,s,now,s.glucoseError,sizeof(s.glucoseError));
          String path=nscfg::USE_V3?
            "/api/v3/devicestatus?sort$desc=date&limit=2&fields=date,created_at,device,openaps,pump,isValid,isDeleted":
            "/api/v1/devicestatus.json?count=2&find[openaps][$exists]=true";
          if (*cfg::AAPS_DEVICE) path+=String(nscfg::USE_V3?"&device$eq=":"&find[device]=")+urlEncode(cfg::AAPS_DEVICE);
          bool b=fetch(path,Resource::Status,s,time(nullptr),s.statusError,sizeof(s.statusError));
          String treatmentPath;
          if (nscfg::USE_V3) {
            char current[32];snprintf(current,sizeof(current),"%lld",(long long)time(nullptr)*1000);
            treatmentPath=String("/api/v3/treatments?date$gte=")+since+"&date$lte="+current+
              "&sort$desc=date&limit=200&fields=identifier,date,created_at,mills,timestamp,carbs,insulin,isSMB,type,isValid,isDeleted,eventType,pumpId,pumpType,pumpSerial";
          } else {
            time_t windowStart=time(nullptr)-int64_t(cfg::GRAPH_HOURS)*3600;
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
    }
    networkBusy.store(false);
    xQueueOverwrite(updates,&s);
    failures=good?0:std::min(failures+1,4u);
    unsigned wait=good?cfg::POLL_SECONDS:std::min(15u*(1u<<failures),300u);
    ulTaskNotifyTake(pdTRUE,pdMS_TO_TICKS(wait*1000));
  }
}
String amount(float n, unsigned digits=2) {return std::isfinite(n)?String(n,digits):String("--");}
String compactAmount(float value) {
  String text=amount(value,2);
  while (text.indexOf('.')>=0 && text.endsWith("0")) text.remove(text.length()-1);
  if (text.endsWith(".")) text.remove(text.length()-1);
  return text;
}
String age(int64_t t,int64_t now) {
  if (!model::validTime(t,now)) return "unknown";
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
  h.integer(model::fresh(glucoseTime,now,cfg::SGV_STALE_SECONDS));
  for (int64_t t : {s.iob.t,s.cob.t,s.basal.t,s.loopTime,s.pumpTime,s.forecast.t})
    h.integer(model::fresh(t,now,cfg::AAPS_STALE_SECONDS));
  h.integer(!s.tempUntil || now<s.tempUntil);
  int64_t start=now-int64_t(cfg::GRAPH_HOURS)*3600;
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
  String ageText="Age "+age(glucoseTime,now);
  String loopText="Age "+age(ui.status.loopTime,now);
  if (force || ageText!=previousAge || loopText!=previousLoop) {
    M5.Display.fillRect(174,32,146,12,TFT_BLACK);
    M5.Display.setTextSize(1);M5.Display.setTextColor(TFT_LIGHTGREY,TFT_BLACK);
    M5.Display.setCursor(178,35);M5.Display.print(ageText);
    // Bright blue distinguishes AAPS age from the glucose age beside it.
    M5.Display.setTextColor(0x551F,TFT_BLACK);
    M5.Display.setCursor(315-M5.Display.textWidth(loopText),35);M5.Display.print(loopText);
    previousAge=ageText;previousLoop=loopText;
  }
}
uint16_t glucoseColor(float value,bool fresh) {
  if (!fresh) return TFT_LIGHTGREY;
  if (value<cfg::LOW_MGDL) return TFT_RED;
  if (value>cfg::HIGH_MGDL) return TFT_ORANGE;
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
void draw() {
  M5.Display.fillScreen(TFT_BLACK);
  int64_t now=displayNow();
  bool have=ui.history.count>0;
  model::Point last=have?ui.history.p[ui.history.count-1]:model::Point{};
  bool fresh=have && model::fresh(last.t,now,cfg::SGV_STALE_SECONDS);
  uint16_t col=glucoseColor(last.sgv,fresh);
  line(2,"NIGHTSCOUT   "+String(mmol?"mmol/L":"mg/dL")+"   "+(fresh?"":"STALE"),fresh?TFT_CYAN:TFT_ORANGE);
  String glucoseText=have?amount(mmol?last.sgv/18.0182f:last.sgv,mmol?1:0):"--";
  line(18,glucoseText,col,4);
  int arrowCenter=5+M5.Display.textWidth(glucoseText)+26;
  drawTrendArrow(have?last.direction:"NONE",arrowCenter,33,col);
  // History, treatment lanes and a separately marked future interval.
  const auto &pred=ui.status.forecast;
  bool predicted=showForecast && fresh && model::fresh(pred.t,now,cfg::AAPS_STALE_SECONDS);
  int64_t start=now-int64_t(cfg::GRAPH_HOURS)*3600;
  int64_t end=now+(predicted?3600:0);
  auto xx=[&](int64_t t){return 30+int((t-start)*284/(end-start));};
  float low=40,high=250;
  for (size_t i=0;i<ui.history.count;++i) if (ui.history.p[i].t>=start && ui.history.p[i].t<=now) {
    low=std::min(low,ui.history.p[i].sgv-10);high=std::max(high,ui.history.p[i].sgv+10);
  }
  if (predicted) for (size_t k=0;k<model::PRED_LINES;++k) for (size_t i=0;i<pred.count[k];++i) {
    int64_t t=pred.t+int64_t(i)*300;
    float v=pred.bg[k][i];
    if (t>=now-300 && t<=end+300 && std::isfinite(v)) {low=std::min(low,v-10);high=std::max(high,v+10);}
  }
  auto yy=[&](float v){return layout::PLOT_BOTTOM-int((v-low)/(high-low)*(layout::PLOT_BOTTOM-layout::PLOT_TOP));};
  M5.Display.setTextSize(1);
  auto label=[&](int x,int y,const String &text,uint16_t color) {
    M5.Display.setTextColor(color,TFT_BLACK);M5.Display.setCursor(x,y);M5.Display.print(text);
  };
  label(30,layout::LEGEND_Y,"C carbs(g)",TFT_ORANGE);label(104,layout::LEGEND_Y,"S SMB(U)",TFT_CYAN);label(167,layout::LEGEND_Y,"B bolus(U)",TFT_LIGHTGREY);
  if (predicted) {
    const char *keys[]={"IOB","COB","UAM","ZT"};
    const uint16_t colors[]={TFT_MAGENTA,TFT_ORANGE,TFT_YELLOW,TFT_CYAN};
    for (size_t k=0;k<model::PRED_LINES;++k) label(30+k*45,layout::FORECAST_LEGEND_Y,keys[k],pred.count[k]?colors[k]:TFT_DARKGREY);
    label(225,layout::FORECAST_LEGEND_Y,"FORECAST",TFT_LIGHTGREY);
  } else label(30,layout::FORECAST_LEGEND_Y,showForecast?"Forecast unavailable/stale":"Forecast off (button C)",TFT_DARKGREY);
  label(0,layout::PLOT_TOP,amount(mmol?high/18.0182f:high,0),TFT_LIGHTGREY);
  label(0,layout::PLOT_BOTTOM-8,amount(mmol?low/18.0182f:low,0),TFT_LIGHTGREY);
  for (int v : {cfg::LOW_MGDL,cfg::HIGH_MGDL}) if (v>=low && v<=high)
    M5.Display.drawFastHLine(30,yy(v),284,TFT_DARKGREY);
  if (predicted) M5.Display.drawFastVLine(xx(now),layout::PLOT_TOP,layout::PLOT_BOTTOM-layout::PLOT_TOP+1,TFT_LIGHTGREY);
  int px=-1,py=0;int64_t pt=0;float previousGlucose=NAN;
  for (size_t i=0;i<ui.history.count;++i) {
    const auto &p=ui.history.p[i];if (p.t<start || p.t>now) continue;
    int x=xx(p.t),y=yy(p.sgv);
    if (px>=0 && p.t-pt<=720) glucoseSegment(px,py,previousGlucose,x,y,p.sgv,fresh);
    M5.Display.fillCircle(x,y,1,glucoseColor(p.sgv,fresh));
    px=x;py=y;pt=p.t;previousGlucose=p.sgv;
  }
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
        if (x>=xx(now) && x<=314 && y>=layout::PLOT_TOP && y<=layout::PLOT_BOTTOM) M5.Display.drawPixel(x,y,colors[k]);
      }
    }
  }
  // Markers sit inside the plot above its lower threshold line. Compact
  // amount labels sit beneath that line, in separate carb/insulin rows.
  const int treatmentLine=(cfg::LOW_MGDL>=low && cfg::LOW_MGDL<=high)?yy(cfg::LOW_MGDL):layout::PLOT_BOTTOM;
  const int carbMarkerY=std::max(layout::PLOT_TOP+3,treatmentLine-5);
  const int insulinMarkerY=std::max(layout::PLOT_TOP+3,treatmentLine-12);
  int lastCarbLabel=0,lastInsulinLabel=0;
  for (size_t i=0;i<ui.treatments.count;++i) {
    const auto &e=ui.treatments.events[i];if (e.t<start || e.t>now) continue;
    int x=xx(e.t);
    if (e.carbs>0) {
      M5.Display.fillTriangle(x,carbMarkerY-2,x-2,carbMarkerY+2,x+2,carbMarkerY+2,TFT_ORANGE);
      String text="C"+compactAmount(e.carbs);
      int width=text.length()*6,tx=std::min(x+4,314-width);
      if (tx>=lastCarbLabel) {label(tx,treatmentLine+3,text,TFT_ORANGE);lastCarbLabel=tx+width+3;}
    }
    if (e.insulin>0) {
      uint16_t color=e.smb?TFT_CYAN:TFT_LIGHTGREY;
      M5.Display.fillCircle(x,insulinMarkerY,e.smb?2:1,color);
      String text=String(e.smb?"S":"B")+compactAmount(e.insulin);
      int width=text.length()*6,tx=std::min(x+4,314-width);
      if (tx>=lastInsulinLabel) {label(tx,treatmentLine+12,text,color);lastInsulinLabel=tx+width+3;}
    }
  }
  label(30,layout::TIME_Y,"-"+String(cfg::GRAPH_HOURS)+"h",TFT_LIGHTGREY);
  label(std::max(62,xx(now)-12),layout::TIME_Y,"now",TFT_LIGHTGREY);
  if (predicted) label(278,layout::TIME_Y,"+1h",TFT_LIGHTGREY);
  auto &s=ui.status;
  auto value=[&](const model::Metric &m,unsigned d){ return model::fresh(m.t,now,cfg::AAPS_STALE_SECONDS)?amount(m.value,d):String("--");};
  line(layout::IOB_Y,"IOB "+value(s.iob,2)+" U     COB "+value(s.cob,0)+" g",TFT_WHITE,2);
  bool basalFresh=model::fresh(s.basal.t,now,cfg::AAPS_STALE_SECONDS) && (!s.tempUntil || now<s.tempUntil);
  String basalText="Basal "+(basalFresh?amount(s.basal.value):String("--"))+" U/h ("+s.basalKind+")";
  line(layout::BASAL_Y,basalText,TFT_WHITE);
  String modeValue=model::fresh(s.pumpTime,now,cfg::AAPS_STALE_SECONDS)?s.pump:"Unknown/stale";
  String modeText="Mode "+modeValue;
  int available=315-(5+M5.Display.textWidth(basalText)+12);
  if (M5.Display.textWidth(modeText)>available) {
    while (modeValue.length() && M5.Display.textWidth("Mode "+modeValue+"...")>available)
      modeValue.remove(modeValue.length()-1);
    modeText="Mode "+modeValue+"...";
  }
  M5.Display.setTextColor(TFT_LIGHTGREY,TFT_BLACK);
  M5.Display.setCursor(315-M5.Display.textWidth(modeText),layout::BASAL_Y);M5.Display.print(modeText);
  String err=*ui.glucoseError?String("SGV: ")+ui.glucoseError:*ui.statusError?String("AAPS: ")+ui.statusError:*ui.treatmentError?String("Treat: ")+ui.treatmentError:ui.treatments.limited?"Treatments limited; reduce graph hours":"A refresh  B units  C forecasts";
  M5.Display.setTextSize(1);
  if (M5.Display.textWidth(err)>267) {
    while (err.length() && M5.Display.textWidth(err+"...")>267) err.remove(err.length()-1);
    err+="...";
  }
  line(layout::FOOTER_Y,err,TFT_ORANGE);
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
    have && model::fresh(latest.t,now,cfg::SGV_STALE_SECONDS),tick);
  if (event==alerts::Event::None) {retrying=false;return;}
  if (retrying && retryEvent==event && uint32_t(tick-lastAttempt)<30000) return;
  lastAttempt=tick;retryEvent=event;
  if (M5.Speaker.tone(alerts::CHIRP_HZ,alerts::CHIRP_MS)) {
    glucoseAlerts.played(event,tick);retrying=false;
  } else {
    retrying=true;Serial.println("Glucose chirp could not be queued; will retry while fresh.");
  }
}
void toggleGlucoseAlerts() {
  bool enabled=!glucoseAlerts.isEnabled();
  glucoseAlerts.setEnabled(enabled);
  if (!enabled) M5.Speaker.stop();
  if (alertPreferencesReady && !alertPreferences.putBool("alerts-on",enabled))
    Serial.println("Could not save alert toggle; current session setting still applies.");
  drawSpeakerIndicator(true);
  // Muting clears retries immediately; enabling evaluates the current fresh SGV.
  updateGlucoseAlerts(displayNow());
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
  M5.Speaker.setVolume(alerts::VOLUME);
#ifdef RUN_PARSER_TESTS
  runParserTests();
  for (;;) delay(1000);
#endif
  alertPreferencesReady=alertPreferences.begin("ns-display",false);
  if (alertPreferencesReady) glucoseAlerts.setEnabled(alertPreferences.getBool("alerts-on",true));
  updates=xQueueCreate(1,sizeof(Snapshot));
  if (!updates || xTaskCreatePinnedToCore(worker,"network",49152,nullptr,1,&networkTask,0)!=pdPASS) {
    line(20,"Memory/task allocation failed",TFT_RED); for (;;) delay(1000);
  }
  draw();
}
void loop() {
  M5.update();
  for (size_t i=0;i<M5.Touch.getCount();++i) {
    const auto &touch=M5.Touch.getDetail(i);
    if (touch.wasPressed() && touch.x>=174 && touch.x<200 && touch.y>=0 && touch.y<28) {
      toggleGlucoseAlerts();break;
    }
  }
  if (M5.BtnA.wasPressed()) xTaskNotifyGive(networkTask);
  bool unitsPressed=M5.BtnB.wasPressed(), forecastPressed=M5.BtnC.wasPressed();
  if (unitsPressed) mmol=!mmol;
  if (forecastPressed) showForecast=!showForecast;
  bool received=xQueueReceive(updates,&ui,0)==pdTRUE;
  if (received) {lastAppliedAt=millis();haveApplied=true;}
  bool dataChanged=received && payloadFingerprint()!=renderedPayload;
  static int64_t lastCheckedSecond=-1;
  int64_t now=displayNow();
  bool newSecond=now!=lastCheckedSecond;
  if (received || newSecond) updateGlucoseAlerts(now);
  bool timedChanged=(newSecond || received) && timedFingerprint(now)!=renderedTimed;
  if (!rendered || dataChanged || timedChanged || unitsPressed || forecastPressed) draw();
  else if (newSecond) drawLiveFields(now);
  lastCheckedSecond=now;
  drawPowerIndicator();
  drawWifiIndicator();
  drawActivityIndicator();
  delay(10);
}
