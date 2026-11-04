#pragma once
#include <ArduinoJson.h>
#include <stdint.h>
#include <string.h>
#include <stdio.h>
#include "quick_settings.h"
namespace runtimecfg {
constexpr uint32_t MAGIC=0x4e533203;
constexpr unsigned MAX_WIFI=5;
struct Settings {
  uint32_t magic=MAGIC;
  char url[256]={},token[513]={},device[97]={};
  char timezone[97]={},ntp1[65]={},ntp2[65]={};
  quicksettings::Credentials wifi[MAX_WIFI];
  unsigned wifiCount=0,hours=3,poll=60,sgvStale=600,apsStale=600;
  unsigned low=70,high=180,brightness=100,volume=80;
  bool v3=true,mmol=false,forecast=true,sound=true;
};
template<size_t N> inline bool terminated(const char (&s)[N]) {return memchr(s,0,N)!=nullptr;}
inline bool plain(const char *s) {
  for(;*s;++s) if(static_cast<unsigned char>(*s)<32 || *s==127) return false;
  return true;
}
inline bool https(const char *s) {
  if(strncmp(s,"https://",8) || !s[8] || s[8]=='/' || s[strlen(s)-1]=='/') return false;
  for(const char *p=s;*p;++p) if(static_cast<unsigned char>(*p)<=32 || strchr("@?#\\",*p)) return false;
  return true;
}
inline bool valid(const Settings &s) {
  if(s.magic!=MAGIC || !terminated(s.url) || !terminated(s.token) || !terminated(s.device) ||
     !terminated(s.timezone) || !terminated(s.ntp1) || !terminated(s.ntp2)) return false;
  if((s.url[0]?!https(s.url):bool(s.token[0])) || !plain(s.token) || !plain(s.device) || !plain(s.timezone) || !plain(s.ntp1) || !plain(s.ntp2)) return false;
  if(!s.timezone[0] || !s.ntp1[0] || s.wifiCount>MAX_WIFI || !quicksettings::validHours(s.hours)) return false;
  if(s.poll<30 || s.poll>900 || s.sgvStale<60 || s.sgvStale>3600 || s.apsStale<60 || s.apsStale>3600 ||
     s.low<20 || s.low>179 || s.high<=s.low || s.high>600 || s.brightness>255 || s.volume>255) return false;
  for(unsigned i=0;i<s.wifiCount;++i) {
    if(!quicksettings::validCredentials(s.wifi[i])) return false;
    for(unsigned j=0;j<i;++j) if(!strcmp(s.wifi[i].ssid,s.wifi[j].ssid)) return false;
  }
  return true;
}
inline bool connectionChanged(const Settings &a,const Settings &b) {
  return strcmp(a.url,b.url) || strcmp(a.token,b.token) || strcmp(a.device,b.device) || a.v3!=b.v3;
}
inline bool promoteWifi(Settings &s,const quicksettings::Credentials &credentials) {
  if(!quicksettings::validCredentials(credentials)) return false;
  unsigned existing=s.wifiCount;
  for(unsigned i=0;i<s.wifiCount;++i) if(!strcmp(s.wifi[i].ssid,credentials.ssid)) {existing=i;break;}
  if(existing==s.wifiCount && s.wifiCount<MAX_WIFI) ++s.wifiCount;
  unsigned end=existing<s.wifiCount?existing:s.wifiCount-1;
  for(unsigned i=end;i>0;--i) s.wifi[i]=s.wifi[i-1];
  s.wifi[0]=credentials;return true;
}
template<size_t N> inline bool stringField(JsonVariantConst value,char (&out)[N],bool keepBlank=false) {
  if(!value.is<const char*>()) return false;
  const char *text=value.as<const char*>();size_t length=strlen(text);
  if(length>=N || value.as<JsonString>().size()!=length || !plain(text)) return false;
  if(!keepBlank || length) {memcpy(out,text,length+1);}
  return true;
}
inline bool unsignedField(JsonVariantConst value,unsigned &out) {
  if(value.is<bool>() || !value.is<unsigned>()) return false;
  out=value.as<unsigned>();return true;
}
// Merge into a copy. Malformed input cannot mutate the active settings.
inline bool patch(JsonVariantConst input,const Settings &current,Settings &out) {
  if(!input.is<JsonObjectConst>()) return false;
  out=current;JsonObjectConst root=input.as<JsonObjectConst>();
  if(root.containsKey("url") && !stringField(root["url"],out.url)) return false;
  if(root.containsKey("token") && !stringField(root["token"],out.token,true)) return false;
  // Never silently carry an existing credential to a different server/path.
  if(strcmp(out.url,current.url) && current.token[0] &&
     (!root["token"].is<const char*>() || !root["token"].as<const char*>()[0])) return false;
  if(root.containsKey("device") && !stringField(root["device"],out.device)) return false;
  if(root.containsKey("timezone") && !stringField(root["timezone"],out.timezone)) return false;
  if(root.containsKey("ntp1") && !stringField(root["ntp1"],out.ntp1)) return false;
  if(root.containsKey("ntp2") && !stringField(root["ntp2"],out.ntp2)) return false;
  struct Number {const char *key;unsigned *field;};
  Number numbers[]={{"hours",&out.hours},{"poll",&out.poll},{"sgvStale",&out.sgvStale},{"apsStale",&out.apsStale},
    {"low",&out.low},{"high",&out.high},{"brightness",&out.brightness},{"volume",&out.volume}};
  for(auto &number:numbers) if(root.containsKey(number.key) && !unsignedField(root[number.key],*number.field)) return false;
  struct Boolean {const char *key;bool *field;};
  Boolean booleans[]={{"v3",&out.v3},{"mmol",&out.mmol},{"forecast",&out.forecast},{"sound",&out.sound}};
  for(auto &boolean:booleans) if(root.containsKey(boolean.key)) {
    if(!root[boolean.key].is<bool>()) return false;
    *boolean.field=root[boolean.key].as<bool>();
  }
  if(root.containsKey("wifi")) {
    if(!root["wifi"].is<JsonArrayConst>()) return false;
    JsonArrayConst list=root["wifi"].as<JsonArrayConst>();
    if(!list.size() || list.size()>MAX_WIFI) return false;
    out.wifiCount=0;
    for(JsonVariantConst item:list) {
      if(!item.is<JsonObjectConst>()) return false;
      quicksettings::Credentials credentials;
      if(!stringField(item["ssid"],credentials.ssid)) return false;
      bool open=false;
      if(item.as<JsonObjectConst>().containsKey("open")) {
        if(!item["open"].is<bool>()) return false;
        open=item["open"].as<bool>();
      }
      const char *password="";
      if(item.as<JsonObjectConst>().containsKey("password")) {
        if(!item["password"].is<const char*>()) return false;
        password=item["password"].as<const char*>();
        if(item["password"].as<JsonString>().size()!=strlen(password)) return false;
      }
      if(open && *password) return false;
      if(!open && !*password) {
        bool found=false;
        for(unsigned i=0;i<current.wifiCount;++i) if(!strcmp(current.wifi[i].ssid,credentials.ssid)) {
          credentials=current.wifi[i];found=true;break;
        }
        if(!found) return false; // A new open network must be explicit.
      } else if(!open) {
        if(strlen(password)>=sizeof(credentials.password) || !plain(password)) return false;
        strcpy(credentials.password,password);
      }
      if(!quicksettings::validCredentials(credentials)) return false;
      out.wifi[out.wifiCount++]=credentials;
    }
    for(unsigned i=out.wifiCount;i<MAX_WIFI;++i)out.wifi[i]=quicksettings::Credentials{};
  }
  return valid(out);
}
inline void publicJson(JsonObject root,const Settings &s,uint32_t revision) {
  root["revision"]=revision;root["url"]=s.url;root["tokenSet"]=bool(s.token[0]);root["device"]=s.device;
  root["v3"]=s.v3;root["hours"]=s.hours;root["poll"]=s.poll;root["sgvStale"]=s.sgvStale;root["apsStale"]=s.apsStale;
  root["mmol"]=s.mmol;root["forecast"]=s.forecast;root["sound"]=s.sound;root["low"]=s.low;root["high"]=s.high;
  root["brightness"]=s.brightness;root["volume"]=s.volume;root["timezone"]=s.timezone;root["ntp1"]=s.ntp1;root["ntp2"]=s.ntp2;
  JsonArray list=root.createNestedArray("wifi");
  for(unsigned i=0;i<s.wifiCount;++i) {JsonObject network=list.createNestedObject();network["ssid"]=s.wifi[i].ssid;network["passwordSet"]=bool(s.wifi[i].password[0]);}
}
// Failed PIN attempts are bounded; session lifetime is inactivity-based.
class Access {
  unsigned failures=0;uint32_t blockedAt=0,lastUse=0;bool blocked=false,session=false;
public:
  bool limited(uint32_t tick) {if(blocked && uint32_t(tick-blockedAt)>=60000) {blocked=false;failures=0;}return blocked;}
  void failed(uint32_t tick) {if(++failures>=5) {blocked=true;blockedAt=tick;}}
  void login(uint32_t tick) {session=true;lastUse=tick;failures=0;blocked=false;}
  bool authorized(bool cookieMatches,uint32_t tick) {
    if(!session || !cookieMatches) return false;
    if(uint32_t(tick-lastUse)>=15u*60u*1000u) {session=false;return false;}
    lastUse=tick;return true;
  }
  void clear() {session=false;failures=0;blocked=false;}
};
}
