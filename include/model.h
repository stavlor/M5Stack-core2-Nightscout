#pragma once
#include <Arduino.h>
#include <ArduinoJson.h>
#include <algorithm>
#include <cmath>
#include <time.h>

namespace model {
constexpr size_t MAX_POINTS = 288;
constexpr int64_t MIN_TIME = 1577836800; // 2020-01-01
inline void makeFilter(DynamicJsonDocument &filter, bool glucose) {
  JsonObject f=filter.to<JsonArray>().createNestedObject();
  if (glucose) {f["sgv"]=true;f["date"]=true;f["dateString"]=true;f["direction"]=true;return;}
  f["created_at"]=true;f["device"]=true;
  for (const char *key : {"suggested","enacted"}) {
    JsonObject a=f["openaps"][key].to<JsonObject>();
    for (const char *k : {"timestamp","IOB","COB","received"}) a[k]=true;
    for (const char *k : {"IOB","COB","UAM","ZT"}) a["predBGs"][k]=true;
  }
  f["openaps"]["iob"]=true;
  f["pump"]["status"]=true;
  JsonObject e=f["pump"]["extended"].to<JsonObject>();
  for (const char *k : {"TempBasalRemaining","TempBasalAbsoluteRate","BaseBasalRate"}) e[k]=true;
}
struct Point { int64_t t = 0; float sgv = NAN; char direction[24] = {}; };
struct History { Point p[MAX_POINTS]; size_t count = 0; };
struct Metric { float value = NAN; int64_t t = 0; };
constexpr size_t PRED_SAMPLES=49;
constexpr size_t PRED_LINES=4;
struct Forecast {
  int64_t t=0;
  float bg[PRED_LINES][PRED_SAMPLES];
  size_t count[PRED_LINES]={};
  Forecast() {for (auto &line:bg) for (float &v:line) v=NAN;}
};
struct Status {
  Forecast forecast;
  int64_t uploaded = 0, loopTime = 0, pumpTime = 0, tempUntil = 0;
  Metric iob, cob, basal;
  char activity[24] = "Unknown", pump[40] = "Unknown", basalKind[12] = "Unknown";
};
inline bool number(JsonVariantConst v, float &n) {
  if (v.is<bool>() || !(v.is<float>() || v.is<long>() || v.is<double>())) return false;
  n = v.as<float>(); return std::isfinite(n);
}
// Strict ISO8601, including fractional seconds and Z or explicit UTC offset.
// Numeric Nightscout timestamps may be seconds or milliseconds.
inline int64_t timestamp(JsonVariantConst v) {
  if (v.is<int64_t>() || v.is<double>()) {
    double n = v.as<double>();
    if (!std::isfinite(n)) return 0;
    if (n > 1e11) n /= 1000;
    return n >= MIN_TIME && n < 4102444800.0 ? int64_t(n) : 0;
  }
  if (!v.is<const char*>()) return 0;
  const char *s = v.as<const char*>();
  if (strlen(s)<20 || s[4]!='-' || s[7]!='-' || s[10]!='T' || s[13]!=':' || s[16]!=':') return 0;
  for (int i : {0,1,2,3,5,6,8,9,11,12,14,15,17,18}) if (!isdigit(s[i])) return 0;
  int y, mo, d, h, mi, sec, used = 0;
  if (sscanf(s, "%4d-%2d-%2dT%2d:%2d:%2d%n", &y,&mo,&d,&h,&mi,&sec,&used) != 6) return 0;
  if (y < 2020 || y > 2099 || mo < 1 || mo > 12 || h < 0 || h > 23 || mi < 0 || mi > 59 || sec < 0 || sec > 59) return 0;
  const int md[] = {31,28,31,30,31,30,31,31,30,31,30,31};
  int days = md[mo-1] + (mo == 2 && y % 4 == 0);
  if (d < 1 || d > days) return 0;
  const char *z = s + used;
  if (*z == '.') { ++z; if (!isdigit(*z)) return 0; while (isdigit(*z)) ++z; }
  int off = 0;
  if (*z == 'Z' && z[1] == 0) {}
  else {
    int oh, om, n = 0;
    if (strlen(z)!=6 || (*z!='+' && *z!='-') || z[3]!=':' || !isdigit(z[1]) || !isdigit(z[2]) || !isdigit(z[4]) || !isdigit(z[5]) || sscanf(z+1,"%2d:%2d%n",&oh,&om,&n)!=2 || n!=5 || oh>23 || om>59) return 0;
    off = (oh*60+om)*60 * (*z == '+' ? 1 : -1);
  }
  // Civil date -> days since epoch; independent of local timezone and timegm.
  y -= mo <= 2;
  int era = y / 400, yo = y - era*400;
  int doy = (153*(mo+(mo > 2 ? -3 : 9))+2)/5+d-1;
  int64_t t = int64_t(era*146097+yo*365+yo/4-yo/100+doy-719468)*86400+h*3600+mi*60+sec-off;
  return t >= MIN_TIME ? t : 0;
}
inline bool validTime(int64_t t, int64_t now) { return t >= MIN_TIME && t <= now+120; }
inline bool fresh(int64_t t, int64_t now, unsigned limit) {
  return validTime(t,now) && now-t <= limit;
}
inline bool metric(JsonVariantConst v, int64_t t, int64_t now, Metric &out, float lo, float hi) {
  float n;
  if (!validTime(t,now) || !number(v,n) || n < lo || n > hi) return false;
  out.value=n; out.t=t; return true;
}
inline bool entries(JsonVariantConst root, int64_t now, History &out) {
  if (!root.is<JsonArrayConst>()) return false;
  History next;
  for (JsonObjectConst o : root.as<JsonArrayConst>()) {
    float n;
    int64_t t = timestamp(o["date"]);
    if (!t) t = timestamp(o["dateString"]);
    if (!validTime(t,now) || now-t > 86400 || !number(o["sgv"],n) || n < 20 || n > 600) continue;
    if (next.count == MAX_POINTS) break;
    Point &p = next.p[next.count++]; p.t=t; p.sgv=n;
    strlcpy(p.direction,o["direction"] | "NONE",sizeof(p.direction));
  }
  std::sort(next.p,next.p+next.count,[](const Point&a,const Point&b){return a.t<b.t;});
  size_t n=0;
  for (size_t i=0;i<next.count;++i) {
    if (n && next.p[n-1].t==next.p[i].t) next.p[n-1]=next.p[i];
    else next.p[n++]=next.p[i];
  }
  next.count=n; out=next; return true; // [] intentionally clears old data
}
inline int64_t fieldTime(JsonVariantConst o, const char *a, const char *b=nullptr) {
  int64_t t=timestamp(o[a]); return t || !b ? t : timestamp(o[b]);
}
inline void forecast(JsonVariantConst run,int64_t now,Forecast &out) {
  int64_t t=timestamp(run["timestamp"]);
  if (!validTime(t,now) || t<=out.t) return;
  Forecast next;
  const char *keys[]={"IOB","COB","UAM","ZT"};
  bool any=false;
  for (size_t k=0;k<PRED_LINES;++k) {
    JsonArrayConst a=run["predBGs"][keys[k]].as<JsonArrayConst>();
    size_t n=std::min(a.size(),PRED_SAMPLES), valid=0;
    for (size_t i=0;i<n;++i) {
      float value;
      if (number(a[i],value) && value>=20 && value<=600) {next.bg[k][i]=value;++valid;}
    }
    if (valid>=2) {next.count[k]=n;any=true;}
  }
  if (any) {next.t=t;out=next;}
}
inline bool statuses(JsonVariantConst root, const char *device, int64_t now, Status &out) {
  if (!root.is<JsonArrayConst>()) return false;
  JsonObjectConst selected;
  int64_t newest=0;
  for (JsonObjectConst o : root.as<JsonArrayConst>()) {
    if (!o["openaps"].is<JsonObjectConst>()) continue;
    const char *name=o["device"] | "";
    if (*device && strcmp(device,name)) continue;
    int64_t t=timestamp(o["created_at"]);
    if (validTime(t,now) && t>newest) { selected=o; newest=t; }
  }
  Status s;
  if (selected.isNull()) { out=s; return true; }
  s.uploaded=newest;
  JsonVariantConst aps=selected["openaps"], sug=aps["suggested"], en=aps["enacted"], io=aps["iob"];
  int64_t st=fieldTime(sug,"timestamp"), et=fieldTime(en,"timestamp");
  forecast(sug,now,s.forecast);
  forecast(en,now,s.forecast);
  if (validTime(st,now)) { s.loopTime=st; strlcpy(s.activity,"Suggested",sizeof(s.activity)); }
  if (validTime(et,now) && et>=s.loopTime) {
    s.loopTime=et;
    strlcpy(s.activity,en["received"].is<bool>() && en["received"].as<bool>() ? "Enacted" : "Enact reported",sizeof(s.activity));
  }
  if (io.is<JsonArrayConst>()) io=io[0]; // never use future prediction elements
  int64_t it=fieldTime(io,"time","timestamp");
  metric(io["iob"],it,now,s.iob,-100,100);
  Metric candidate;
  if (metric(sug["IOB"],st,now,candidate,-100,100) && candidate.t>s.iob.t) s.iob=candidate;
  if (metric(sug["COB"],st,now,candidate,0,1000)) s.cob=candidate;
  if (metric(en["COB"],et,now,candidate,0,1000) && candidate.t>s.cob.t) s.cob=candidate;
  JsonVariantConst pump=selected["pump"], ext=pump["extended"];
  // Pump's own status timestamp, NOT upload time, gates basal freshness.
  s.pumpTime=timestamp(pump["status"]["timestamp"]);
  if (validTime(s.pumpTime,now)) {
    strlcpy(s.pump,pump["status"]["status"] | "Unknown",sizeof(s.pump));
    float rem;
    if (number(ext["TempBasalRemaining"],rem) && rem>0 && rem<=1440 &&
        metric(ext["TempBasalAbsoluteRate"],s.pumpTime,now,s.basal,0,100)) {
      // Remaining minutes apply at upload snapshot; never prolong on each poll.
      s.tempUntil=newest+int64_t(rem*60);
      strlcpy(s.basalKind,"Temp",sizeof(s.basalKind));
    } else if (metric(ext["BaseBasalRate"],s.pumpTime,now,s.basal,0,100)) {
      strlcpy(s.basalKind,"Base",sizeof(s.basalKind));
    }
  }
  out=s; return true;
}
} // namespace model
