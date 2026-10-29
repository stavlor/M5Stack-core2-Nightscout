#pragma once
#include <ArduinoJson.h>
#include <stdint.h>
#include <string.h>
namespace nsapi {
inline JsonVariantConst collection(JsonVariantConst root,bool v3) {
  if (!v3) return root.is<JsonArrayConst>()?root:JsonVariantConst();
  if (!root.is<JsonObjectConst>() || root["status"].is<bool>() || !root["status"].is<int>() ||
      root["status"].as<int>()!=200 || !root["result"].is<JsonArrayConst>()) return JsonVariantConst();
  return root["result"];
}
inline void responseFilter(DynamicJsonDocument &out,const DynamicJsonDocument &items,bool v3) {
  out.clear();
  if (v3) {out["status"]=true;out["result"].set(items.as<JsonVariantConst>());}
  else out.set(items.as<JsonVariantConst>());
}
inline bool tokenResponse(JsonVariantConst root,int64_t now,const char *&token,int64_t &expiry) {
  token=nullptr;expiry=0;
  if (!root.is<JsonObjectConst>() || !root["token"].is<const char*>() ||
      root["exp"].is<bool>() || !root["exp"].is<int64_t>()) return false;
  const char *v=root["token"].as<const char*>();
  size_t n=strlen(v);int dots=0;
  if (!n || n>4096) return false;
  for (size_t i=0;i<n;++i) {
    char c=v[i];
    if (c=='.') {++dots;continue;}
    if (!((c>='a' && c<='z') || (c>='A' && c<='Z') || (c>='0' && c<='9') || c=='-' || c=='_')) return false;
  }
  if (dots!=2 || *v=='.' || v[n-1]=='.' || strstr(v,"..")) return false;
  int64_t exp=root["exp"].as<int64_t>();
  if (exp<=now+30 || exp>=4102444800LL) return false;
  token=v;expiry=exp;return true;
}
inline bool renewNeeded(bool haveToken,int64_t expiry,int64_t now) {
  return !haveToken || expiry<=now+60;
}
}
