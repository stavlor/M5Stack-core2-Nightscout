#pragma once
#include "model.h"
#include "body_buffer.h"
#include "treatments.h"
#include "alerts.h"
#include "power_state.h"
#include "ns_api.h"
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
  {
    alerts::State a;
    check(a.poll(200,true,0)==alerts::Event::High,"First fresh high reading requests one chirp");
    a.played(alerts::Event::High,0);
    check(a.poll(250,true,1800000)==alerts::Event::None,"Continuing highs do not repeat even when glucose changes");
    a.poll(180,true,1800100);
    check(a.poll(181,true,1800200)==alerts::Event::High,"Returning to range re-arms the next high episode");
  }
  {
    alerts::State a;
    check(a.poll(69,true,100)==alerts::Event::Low,"First fresh low reading requests one chirp");
    a.played(alerts::Event::Low,100);
    check(a.poll(60,true,100+alerts::LOW_REPEAT_MS-1)==alerts::Event::None &&
      a.poll(65,true,100+alerts::LOW_REPEAT_MS)==alerts::Event::Low,
      "Low reminder fires at 15 minutes, not before");
    a.played(alerts::Event::Low,100+alerts::LOW_REPEAT_MS);
    check(a.poll(60,true,100+alerts::LOW_REPEAT_MS+1)==alerts::Event::None,
      "Successful low reminder restarts its 15-minute interval");
    a.poll(70,true,100+alerts::LOW_REPEAT_MS+2);
    check(a.poll(69,true,100+alerts::LOW_REPEAT_MS+3)==alerts::Event::Low,
      "A new low after an in-range reading chirps immediately");
  }
  {
    alerts::State a;a.poll(200,true,0);a.played(alerts::Event::High,0);
    check(a.poll(100,false,1000)==alerts::Event::None && a.poll(200,true,2000)==alerts::Event::None,
      "Stale data and reconnects do not re-arm an ongoing high");
    check(a.poll(60,true,3000)==alerts::Event::Low,"A direct high-to-low transition chirps for the low");
    a.played(alerts::Event::Low,3000);
    check(a.poll(60,false,3000+alerts::LOW_REPEAT_MS)==alerts::Event::None &&
      a.poll(60,true,3001+alerts::LOW_REPEAT_MS)==alerts::Event::Low,
      "Stale low reminders are silent and resume only with fresh low data");
  }
  {
    alerts::State a;
    check(a.poll(70,true,0)==alerts::Event::None && a.poll(180,true,1)==alerts::Event::None &&
      a.poll(NAN,true,2)==alerts::Event::None && a.poll(9,true,3)==alerts::Event::None,
      "70 and 180 are in range; missing and invalid glucose never chirp");
    check(a.poll(60,true,4)==alerts::Event::Low && a.poll(60,true,5)==alerts::Event::Low,
      "A failed or unacknowledged tone does not consume the first alert");
  }
  {
    alerts::State a;uint32_t start=0xfffffff0u;
    a.poll(60,true,start);a.played(alerts::Event::Low,start);
    check(a.poll(60,true,uint32_t(start+1000u))==alerts::Event::None &&
      a.poll(60,true,uint32_t(start+alerts::LOW_REPEAT_MS))==alerts::Event::Low,
      "Low repeat timing survives the millis counter wrap");
  }
  {
    alerts::State a;a.setEnabled(false);
    check(!a.isEnabled() && a.poll(60,true,0)==alerts::Event::None &&
      a.poll(200,true,1)==alerts::Event::None,"Muted alerts suppress both low and high chirps");
    a.setEnabled(true);bool initialHigh=a.poll(200,true,2)==alerts::Event::High;
    a.played(alerts::Event::High,2);
    check(a.isEnabled() && initialHigh && a.poll(200,true,3)==alerts::Event::None,
      "Re-enabling evaluates current fresh glucose once");
    a.setEnabled(true);
    check(a.poll(200,true,4)==alerts::Event::None,"Setting an already-enabled toggle does not re-arm highs");
    a.setEnabled(false);bool silent=a.poll(60,true,900000)==alerts::Event::None;
    a.setEnabled(true);bool initialLow=a.poll(60,true,900001)==alerts::Event::Low;
    a.played(alerts::Event::Low,900001);
    check(silent && initialLow && a.poll(60,true,900002)==alerts::Event::None,
      "Mute blocks low reminders; re-enable starts a fresh low reminder interval");
  }
  check(powerstate::classify(0x20,5.0f,150,0,0)==powerstate::Source::Battery,
    "VBUS-present alone does not prove incoming USB power");
  check(powerstate::classify(0x30,5.0f,150,0,0)==powerstate::Source::USB,
    "Usable USB supplying current is shown even without a charging flag");
  check(powerstate::classify(0x30,0,0,0,0)==powerstate::Source::Battery,
    "Unplugged USB ADC values override lingering input flags");
  check(powerstate::classify(0x30,5.0f,0,0,0)==powerstate::Source::Battery,
    "Backfed USB voltage with no incoming current is not USB power");
  check(powerstate::classify(0xc0,0,0,5.0f,150)==powerstate::Source::External,
    "Usable separate ACIN input is classified as external power");
  check(powerstate::classify(0x30,5.0f,NAN,0,0)==powerstate::Source::Battery,
    "Invalid current data cannot assert USB power");
  check(powerstate::classify(0xf0,5.0f,0,5.0f,150)==powerstate::Source::External,
    "Input selection follows measured supply instead of unused USB voltage");
  {
    DynamicJsonDocument items(4096),wrapped(6144);
    model::makeFilter(items,true);nsapi::responseFilter(wrapped,items,true);
    e=deserializeJson(doc,R"({"status":200,"result":[{"date":1793102400000,"sgv":123,"direction":"Flat","noise":7}]})",DeserializationOption::Filter(wrapped));
    bool parsed=model::entries(nsapi::collection(doc.as<JsonVariantConst>(),true),now,h);
    check(!e && parsed && h.count==1 && h.p[0].sgv==123 && doc["result"][0]["noise"].isNull(),
      "Production v3 response filter and wrapped glucose result");
    deserializeJson(doc,R"({"status":200,"result":{"sgv":999}})");
    check(!model::entries(nsapi::collection(doc.as<JsonVariantConst>(),true),now,h) && h.count==1,
      "Invalid v3 result cannot overwrite a good glucose snapshot");
    deserializeJson(doc,R"({"status":403,"result":[]})");
    check(nsapi::collection(doc.as<JsonVariantConst>(),true).isNull(),"Non-success v3 status is rejected");
    deserializeJson(doc,R"({"status":"200","result":[]})");
    check(nsapi::collection(doc.as<JsonVariantConst>(),true).isNull(),"V3 status must be numeric");
    deserializeJson(doc,R"({"status":200,"result":[]})");
    check(model::entries(nsapi::collection(doc.as<JsonVariantConst>(),true),now,h) && !h.count,
      "Successful empty v3 collection clears old data");
    deserializeJson(doc,R"([{"date":1793102400000,"sgv":124}])");
    check(model::entries(nsapi::collection(doc.as<JsonVariantConst>(),false),now,h) && h.count==1 &&
      nsapi::collection(doc.as<JsonVariantConst>(),true).isNull(),"Legacy arrays remain explicit v1-only responses");
    model::makeFilter(items,false);nsapi::responseFilter(wrapped,items,true);
    e=deserializeJson(doc,R"({"status":200,"result":[{"date":1793102400000,"device":"aaps","openaps":{"suggested":{"timestamp":"2026-10-27T12:00:00Z","IOB":1,"predBGs":{"IOB":[100,110]}}}}]})",DeserializationOption::Filter(wrapped));
    model::statuses(nsapi::collection(doc.as<JsonVariantConst>(),true),"aaps",now,s);
    check(!e && s.uploaded==now && s.iob.value==1 && s.forecast.count[0]==2,
      "V3 canonical device date and predictions survive envelope filtering");
    model::treatmentFilter(items);nsapi::responseFilter(wrapped,items,true);
    e=deserializeJson(doc,R"({"status":200,"result":[{"identifier":"public-id","date":1793102340000,"carbs":10},{"identifier":"public-id","date":1793102340000,"carbs":10},{"identifier":"deleted","date":1793102340000,"carbs":10,"isValid":false}]})",DeserializationOption::Filter(wrapped));
    model::treatments(nsapi::collection(doc.as<JsonVariantConst>(),true),now-10800,now,treatments);
    check(!e && treatments.count==1 && !strcmp(treatments.events[0].id,"public-id"),
      "V3 public identifiers deduplicate treatments and tombstones are excluded");
    deserializeJson(doc,R"([{"date":1793102400000,"sgv":123,"isValid":false},{"date":1793102400000,"sgv":124,"isDeleted":true}])");
    model::entries(doc.as<JsonVariantConst>(),now,h);
    check(!h.count,"Deleted glucose documents never become current readings");
    deserializeJson(doc,R"([{"date":1793102400000,"isValid":false,"openaps":{"suggested":{"timestamp":"2026-10-27T12:00:00Z","IOB":1}}}])");
    model::statuses(doc.as<JsonVariantConst>(),"",now,s);
    check(!s.uploaded,"Deleted device status cannot revive loop or pump values");
  }
  {
    const char *token=nullptr;int64_t expires=0;
    deserializeJson(doc,R"({"token":"aaa.bbb.ccc","exp":1793106000})");
    check(nsapi::tokenResponse(doc.as<JsonVariantConst>(),now,token,expires) && expires==now+3600,
      "Authorization response yields bounded JWT and expiration");
    check(!nsapi::renewNeeded(true,now+61,now) && nsapi::renewNeeded(true,now+60,now) &&
      nsapi::renewNeeded(false,now+3600,now),"JWT renews before expiry and when missing");
    deserializeJson(doc,R"({"token":"aaa.bbb.ccc","exp":1793102400})");
    check(!nsapi::tokenResponse(doc.as<JsonVariantConst>(),now,token,expires),"Expired JWT is not accepted");
    deserializeJson(doc,R"({"token":"aaa.bbb.ccc"})");
    check(!nsapi::tokenResponse(doc.as<JsonVariantConst>(),now,token,expires),"JWT without expiration fails closed");
    deserializeJson(doc,R"({"token":"aaa.bbb.ccc\r\nInjected","exp":1793106000})");
    check(!nsapi::tokenResponse(doc.as<JsonVariantConst>(),now,token,expires),"JWT header-injection characters are rejected");
    deserializeJson(doc,R"({"token":"aaa..ccc","exp":1793106000})");
    check(!nsapi::tokenResponse(doc.as<JsonVariantConst>(),now,token,expires),"Empty JWT segments are rejected");
  }
  Serial.printf("RESULT: %u passed, %u failed\n",passed,failed);
  return failed?1:0;
}
