#pragma once
#include "model.h"
#include "body_buffer.h"
#include "treatments.h"
// Runs on the same ESP32 framework and ArduinoJson version as production.
inline int runParserTests() {
  unsigned passed=0,failed=0;
  auto check=[&](bool b,const char *name){Serial.printf("%s %s\n",b?"PASS":"FAIL",name); b?++passed:++failed;};
  DynamicJsonDocument doc(16384);
  const int64_t now=1793102400; // 2026-10-27T12:00:00Z
  static model::History h;
  model::Status s;
  deserializeJson(doc,R"([{"sgv":110,"date":1793102400000,"direction":"Flat"},{"sgv":105,"date":1793102100000},{"sgv":9,"date":1793102399000},{"sgv":"120","date":1793102399000},{"sgv":120,"date":1793103000000},{"sgv":110,"date":1793102400000}])");
  check(model::entries(doc.as<JsonVariantConst>(),now,h) && h.count==2 && h.p[0].sgv==105,"SGV validation, ordering, dedup, future rejection");
  deserializeJson(doc,R"({"date":"2026-10-27T14:00:00.123+02:00","bad":"2026-02-30T12:00:00Z"})");
  check(model::timestamp(doc["date"])==now && !model::timestamp(doc["bad"]),"ISO offset/fraction and calendar validation");
  check(model::fresh(now-600,now,600) && !model::fresh(now-601,now,600),"Stale boundary");
  deserializeJson(doc,R"([{"created_at":"2026-10-27T12:00:00Z","device":"openaps://test","openaps":{"suggested":{"timestamp":"2026-10-27T11:59:00Z","IOB":0,"COB":0},"iob":[{"time":"2026-10-27T11:58:00Z","iob":1.2},{"time":"2026-10-27T12:05:00Z","iob":9}],"enacted":{"timestamp":"2026-10-27T11:59:30Z","received":true}},"pump":{"status":{"timestamp":"2026-10-27T11:58:00Z","status":"normal"},"extended":{"BaseBasalRate":0.8,"TempBasalAbsoluteRate":0,"TempBasalRemaining":10}}}])");
  check(model::statuses(doc.as<JsonVariantConst>(),"openaps://test",now,s) && s.iob.value==0 && s.cob.value==0,"Array IOB fallback and valid zeros");
  check(s.basal.value==0 && s.tempUntil==now+600 && !strcmp(s.basalKind,"Temp"),"Zero temp basal and expiry");
  check(!strcmp(s.activity,"Enacted") && s.loopTime==now-30,"Enact timestamp");
  check(model::statuses(doc.as<JsonVariantConst>(),"other",now,s) && s.uploaded==0,"Uploader isolation");
  deserializeJson(doc,R"([{"created_at":"2026-10-27T12:00:00Z","openaps":{"iob":{"time":"2026-10-27T11:00:00Z","iob":1}},"pump":{"status":{"timestamp":"2026-10-27T11:00:00Z"},"extended":{"BaseBasalRate":1}}}])");
  model::statuses(doc.as<JsonVariantConst>(),"",now,s);
  check(!model::fresh(s.iob.t,now,600) && !model::fresh(s.basal.t,now,600),"Fresh upload cannot revive old metrics");
  deserializeJson(doc,"[]"); model::entries(doc.as<JsonVariantConst>(),now,h); model::statuses(doc.as<JsonVariantConst>(),"",now,s);
  check(!h.count && !s.uploaded,"Empty responses clear cache");
  deserializeJson(doc,"{\"error\":\"unauthorized\"}");
  check(!model::entries(doc.as<JsonVariantConst>(),now,h) && !model::statuses(doc.as<JsonVariantConst>(),"",now,s),"Wrong root rejected");
  check(bool(deserializeJson(doc,"[{broken")),"Malformed JSON rejected");
  DynamicJsonDocument filter(2048);
  model::makeFilter(filter,true);
  auto e=deserializeJson(doc,R"([{"sgv":100,"date":1793102400000,"direction":"Flat","noise":{"anything":[1,2,3]}}])",DeserializationOption::Filter(filter));
  check(!e && model::entries(doc.as<JsonVariantConst>(),now,h) && h.count==1 && doc[0]["noise"].isNull(),"Production glucose JSON filter");
  model::makeFilter(filter,false);
  e=deserializeJson(doc,R"([{"created_at":"2026-10-27T12:00:00Z","openaps":{"suggested":{"timestamp":"2026-10-27T12:00:00Z","COB":0,"predBGs":{"IOB":[1,2,3]}},"iob":{"time":"2026-10-27T12:00:00Z","iob":1.5}},"pump":{"status":{"timestamp":"2026-10-27T12:00:00Z","status":"normal"},"extended":{"BaseBasalRate":0.9}}}])",DeserializationOption::Filter(filter));
  check(!e && model::statuses(doc.as<JsonVariantConst>(),"",now,s) && s.iob.value==1.5f && s.cob.value==0 && s.basal.value==0.9f && doc[0]["openaps"]["suggested"]["predBGs"]["IOB"].size()==3,"Production AAPS filter preserves object IOB and predictions");
  {
    BodyBuffer b(8);
    check(b.append(reinterpret_cast<const uint8_t*>("abc"),3)==3 &&
      b.append(reinterpret_cast<const uint8_t*>("defgh"),5)==5 &&
      b.length==8 && !strcmp(b.data,"abcdefgh"),"Decoded body supports multiple chunks and exact bound");
    check(b.append(reinterpret_cast<const uint8_t*>("x"),1)==0 && b.overflow &&
      b.length==8 && b.append(reinterpret_cast<const uint8_t*>("x"),1)==0,
      "Oversized body is rejected and cannot resume after a retry");
  }
  {
    BodyBuffer b(8,[](size_t)->void*{return nullptr;});
    check(!b.data && b.append(reinterpret_cast<const uint8_t*>("x"),1)==0,
      "Allocation failure does not masquerade as oversized response");
  }
  static model::Treatments treatments;
  model::Forecast predictions;
  deserializeJson(doc,R"([{"_id":"smb","date":1793102340000,"insulin":0.12,"isSMB":true},{"_id":"meal","created_at":"2026-10-27T11:58:00Z","carbs":12.5,"insulin":1.25,"type":"NORMAL"},{"_id":"other","date":1793102300000,"insulin":0.1,"eventType":"Correction Bolus"}])");
  model::treatments(doc.as<JsonVariantConst>(),now-10800,now,treatments);
  check(treatments.count==3 && treatments.events[0].carbs==12.5f && treatments.events[2].smb && !treatments.events[1].smb,"SMB flag, carbs/bolus coexistence and no correction-bolus guessing");
  deserializeJson(doc,R"([{"date":1793102340000,"insulin":0.2,"type":"SMB"},{"date":1793102340000,"carbs":10,"isValid":false},{"date":1793102340000,"carbs":10,"isDeleted":true},{"date":1793102340000,"carbs":"10"},{"date":1793102340000,"carbs":-1},{"date":1793102500000,"carbs":20},{"date":1793002340000,"carbs":20},{"date":1793102340000,"insulin":2,"eventType":"Combo Bolus"}])");
  model::treatments(doc.as<JsonVariantConst>(),now-10800,now,treatments);
  check(treatments.count==1 && treatments.events[0].smb,"SMB type; deleted, invalid, future, out-of-window and extended treatments excluded");
  deserializeJson(doc,R"([{"_id":"same","date":1793102340000,"carbs":10},{"_id":"same","date":1793102340000,"carbs":10},{"_id":"distinct","date":1793102340000,"carbs":10}])");
  model::treatments(doc.as<JsonVariantConst>(),now-10800,now,treatments);
  check(treatments.count==2,"Deduplicate treatment IDs without merging distinct events at the same time");
  deserializeJson(doc,"[]");model::treatments(doc.as<JsonVariantConst>(),now-10800,now,treatments);
  check(!treatments.count,"Empty treatment response clears markers");
  deserializeJson(doc,"{}");
  check(!model::treatments(doc.as<JsonVariantConst>(),now-10800,now,treatments),"Non-array treatment response rejected");
  model::treatmentFilter(filter);
  e=deserializeJson(doc,R"([{"date":1793102340000,"insulin":0.2,"isSMB":true,"notes":"not needed","carbs":5}])",DeserializationOption::Filter(filter));
  model::treatments(doc.as<JsonVariantConst>(),now-10800,now,treatments);
  check(!e && treatments.count==1 && treatments.events[0].smb && treatments.events[0].carbs==5 && doc[0]["notes"].isNull(),"Production treatment filter preserves marker fields and removes notes");
  deserializeJson(doc,R"({"timestamp":"2026-10-27T11:59:00Z","predBGs":{"IOB":[100,"bad",120],"COB":[100,110,130],"UAM":[null,true,-5,601],"ZT":[100,90]}})");
  model::forecast(doc.as<JsonVariantConst>(),now,predictions);
  check(predictions.t==now-60 && predictions.count[0]==3 && std::isnan(predictions.bg[0][1]) && predictions.bg[0][2]==120 && predictions.count[2]==0,"Prediction validation keeps five-minute indices and rejects malformed series");
  deserializeJson(doc,R"({"timestamp":"2026-10-27T12:05:00Z","predBGs":{"IOB":[200,210]}})");
  model::forecast(doc.as<JsonVariantConst>(),now,predictions);
  check(predictions.t==now-60,"Future forecast source cannot replace current predictions");
  check(!model::fresh(predictions.t,now+601,600),"Forecast staleness uses calculation time");
  deserializeJson(doc,R"([{"created_at":"2026-10-27T12:00:00Z","openaps":{"suggested":{"timestamp":"2026-10-27T11:58:00Z","predBGs":{"IOB":[100,110]}},"enacted":{"timestamp":"2026-10-27T11:59:00Z","predBGs":{"COB":[100,120]}}}}])");
  model::statuses(doc.as<JsonVariantConst>(),"",now,s);
  check(s.forecast.t==now-60 && s.forecast.count[1]==2 && s.forecast.count[0]==0,"Choose newest prediction run without mixing calculation sources");
  deserializeJson(doc,R"([{"created_at":"2026-10-27T12:00:00Z","openaps":{}}])");
  model::statuses(doc.as<JsonVariantConst>(),"",now,s);
  check(!s.forecast.t,"Missing predictions clear a previously populated forecast");
  doc.clear();
  JsonObject run=doc.to<JsonObject>();run["timestamp"]=now;
  JsonArray predicted=run["predBGs"]["IOB"].to<JsonArray>();
  for (int i=0;i<70;++i) predicted.add(100+i);
  model::forecast(doc.as<JsonVariantConst>(),now,predictions);
  check(predictions.count[0]==49 && predictions.bg[0][48]==148,"Forecast length bounded to four hours at five-minute steps");
  doc.clear();JsonArray events=doc.to<JsonArray>();
  for (int i=149;i>=0;--i) {
    JsonObject row=events.createNestedObject();row["date"]=now-i*30;row["carbs"]=1;
  }
  model::treatments(doc.as<JsonVariantConst>(),now-10800,now,treatments);
  check(treatments.count==128 && treatments.limited && treatments.events[0].t==now-127*30 && treatments.events[127].t==now,"Bounded markers keep newest events even for unordered input");
  model::treatmentFilter(filter);
  e=deserializeJson(doc,R"([{"_id":"a","date":1793102340000,"insulin":0.5,"isSMB":true,"pumpId":42,"pumpType":"Pump","pumpSerial":"A"},{"_id":"b","date":1793102341000,"insulin":0.5,"isSMB":true,"pumpId":"42","pumpType":"Pump","pumpSerial":"A"},{"_id":"c","date":1793102342000,"insulin":0.5,"isSMB":true,"pumpId":42,"pumpType":"Pump","pumpSerial":"A"}])",DeserializationOption::Filter(filter));
  model::treatments(doc.as<JsonVariantConst>(),now-10800,now,treatments);
  check(!e && treatments.count==1,"Repeated SMB uploads with one pump identity produce one marker");
  deserializeJson(doc,R"([{"_id":"a","date":1793102340123,"insulin":0.5,"isSMB":true},{"_id":"b","created_at":"2026-10-27T11:59:00.123Z","insulin":0.5,"type":"SMB"},{"_id":"c","date":1793102340123,"insulin":0.5,"isSMB":true}])");
  model::treatments(doc.as<JsonVariantConst>(),now-10800,now,treatments);
  check(treatments.count==1,"SMB duplicate fallback matches exact milliseconds and dose across timestamp formats");
  deserializeJson(doc,R"([{"date":1793102340123,"insulin":0.5,"isSMB":true},{"date":1793102340124,"insulin":0.5,"isSMB":true},{"date":1793102340123,"insulin":0.6,"isSMB":true}])");
  model::treatments(doc.as<JsonVariantConst>(),now-10800,now,treatments);
  check(treatments.count==3,"Distinct SMB milliseconds or amounts are not collapsed");
  deserializeJson(doc,R"([{"date":1793102340123,"insulin":0.5,"isSMB":true,"pumpId":42,"pumpType":"Pump","pumpSerial":"A"},{"date":1793102340123,"insulin":0.5,"isSMB":true,"pumpId":42,"pumpType":"Pump","pumpSerial":"B"}])");
  model::treatments(doc.as<JsonVariantConst>(),now-10800,now,treatments);
  check(treatments.count==2,"SMB pump identifiers are scoped to pump type and serial");
  Serial.printf("RESULT: %u passed, %u failed\n",passed,failed);
  return failed?1:0;
}
