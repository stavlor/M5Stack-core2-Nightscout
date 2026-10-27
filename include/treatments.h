#pragma once
#include "model.h"
namespace model {
constexpr size_t MAX_TREATMENTS=128;
struct Treatment {
  int64_t t=0, timeMs=0;
  uint64_t pumpKey=0;
  float carbs=0, insulin=0;
  bool smb=false;
  char id[64]={};
};
struct Treatments {
  Treatment events[MAX_TREATMENTS];
  size_t count=0;
  bool limited=false;
};
inline void treatmentFilter(DynamicJsonDocument &doc) {
  JsonObject f=doc.to<JsonArray>().createNestedObject();
  for (const char *k : {"_id","date","mills","created_at","timestamp","carbs","insulin","isSMB","type","isValid","isDeleted","eventType","pumpId","pumpType","pumpSerial"}) f[k]=true;
}
inline int64_t treatmentMilliseconds(JsonVariantConst v,int64_t seconds) {
  if (v.is<int64_t>() || v.is<double>()) {
    double n=v.as<double>();
    return int64_t((n>1e11?n:n*1000)+0.5);
  }
  int fraction=0,digits=0;
  const char *iso=v.is<const char*>()?v.as<const char*>():"";
  if (strlen(iso)>20 && iso[19]=='.') {
    const char *p=iso+20;
    while (digits<3 && isdigit(*p)) {fraction=fraction*10+(*p++-'0');++digits;}
  }
  while (digits++<3) fraction*=10;
  return seconds*1000+fraction;
}
inline uint64_t treatmentPumpKey(JsonObjectConst o) {
  const char *type=o["pumpType"] | "", *serial=o["pumpSerial"] | "";
  char id[32]={};
  if (o["pumpId"].is<const char*>()) {
    const char *v=o["pumpId"].as<const char*>();
    if (strlen(v)>=sizeof(id)) return 0;
    strlcpy(id,v,sizeof(id));
  } else if (!o["pumpId"].is<bool>() && o["pumpId"].is<int64_t>())
    snprintf(id,sizeof(id),"%lld",(long long)o["pumpId"].as<int64_t>());
  if (!*type || !*serial || !*id) return 0;
  uint64_t key=14695981039346656037ULL;
  for (const char *field : {type,serial,static_cast<const char*>(id)}) {
    while (*field) {key=(key^uint8_t(*field++))*1099511628211ULL;}
    key=key*1099511628211ULL;
  }
  return key?key:1;
}
inline bool treatments(JsonVariantConst root,int64_t start,int64_t now,Treatments &out) {
  if (!root.is<JsonArrayConst>()) return false;
  Treatments next;
  next.limited=root.as<JsonArrayConst>().size()>=200;
  for (JsonObjectConst o:root.as<JsonArrayConst>()) {
    if ((o["isValid"].is<bool>() && !o["isValid"].as<bool>()) ||
        (o["isDeleted"].is<bool>() && o["isDeleted"].as<bool>())) continue;
    Treatment e;
    JsonVariantConst eventTime=o["date"];
    e.t=timestamp(eventTime);
    if (!e.t) {eventTime=o["mills"];e.t=timestamp(eventTime);}
    if (!e.t) {eventTime=o["timestamp"];e.t=timestamp(eventTime);}
    if (!e.t) {eventTime=o["created_at"];e.t=timestamp(eventTime);}
    // Exclude scheduled future carb entries until they actually occur.
    if (!validTime(e.t,now) || e.t<start || e.t>now) continue;
    e.timeMs=treatmentMilliseconds(eventTime,e.t);
    float n;
    if (number(o["carbs"],n) && n>0 && n<=1000) e.carbs=n;
    const char *event=o["eventType"] | "";
    bool extended=!strcmp(event,"Combo Bolus") || !strcmp(event,"Extended Bolus");
    if (!extended && number(o["insulin"],n) && n>0 && n<=100) e.insulin=n;
    e.smb=e.insulin>0 && ((o["isSMB"].is<bool>() && o["isSMB"].as<bool>()) ||
      !strcmp(o["type"] | "","SMB"));
    if (!e.carbs && !e.insulin) continue;
    if (e.smb) e.pumpKey=treatmentPumpKey(o);
    const char *id=o["_id"] | "";
    if (strlen(id)<sizeof(e.id)) strlcpy(e.id,id,sizeof(e.id));
    bool duplicate=false;
    for (size_t i=0;i<next.count;++i) {
      const auto &old=next.events[i];
      bool sameId=*e.id && !strcmp(e.id,old.id);
      bool samePump=e.smb && old.smb && e.pumpKey && e.pumpKey==old.pumpKey;
      // Strict fallback only: no time buckets, pixel grouping or nearby-dose merging.
      bool repeatedSmb=e.smb && old.smb && !e.pumpKey && !old.pumpKey &&
        e.timeMs==old.timeMs && e.insulin==old.insulin && e.carbs==old.carbs;
      if (sameId || samePump || repeatedSmb) {duplicate=true;break;}
    }
    if (duplicate) continue;
    if (next.count<MAX_TREATMENTS) next.events[next.count++]=e;
    else {
      next.limited=true;
      size_t oldest=0;
      for (size_t i=1;i<next.count;++i) if (next.events[i].t<next.events[oldest].t) oldest=i;
      if (e.t>next.events[oldest].t) next.events[oldest]=e;
    }
  }
  std::sort(next.events,next.events+next.count,[](const Treatment&a,const Treatment&b){return a.t<b.t;});
  out=next;return true;
}
} // namespace model
