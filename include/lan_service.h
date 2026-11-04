#pragma once
#include "portal_page.h"
inline void randomHex(char (&out)[33]) {
  snprintf(out,sizeof(out),"%08X%08X%08X%08X",unsigned(esp_random()),unsigned(esp_random()),unsigned(esp_random()),unsigned(esp_random()));
}
inline bool sameSecret(const String &value,const char *expected) {
  size_t size=strlen(expected);if(value.length()!=size) return false;
  unsigned difference=0;for(size_t i=0;i<size;++i) difference|=uint8_t(value[i])^uint8_t(expected[i]);return difference==0;
}
// Dedicated task: serves the LAN UI even during normal HTTPS polling.
void lanWorker(void*) {
  WebServer server(3000);runtimecfg::Access access;
  bool running=false;char code[9]={},session[33]={},csrf[33]={};String boundAddress;
  auto publish=[&](int state) {
    LanStatus status;status.state=state;
    if(state==1) {snprintf(status.address,sizeof(status.address),"http://%s:3000",boundAddress.c_str());strlcpy(status.code,code,sizeof(status.code));}
    xQueueOverwrite(lanUpdates,&status);
  };
  auto headers=[&]() {
    server.sendHeader("Cache-Control","no-store");server.sendHeader("X-Content-Type-Options","nosniff");
    server.sendHeader("X-Frame-Options","DENY");server.sendHeader("Referrer-Policy","no-referrer");
    server.sendHeader("Content-Security-Policy","default-src 'none'; script-src 'unsafe-inline'; style-src 'unsafe-inline'; connect-src 'self'; form-action 'self'; base-uri 'none'; frame-ancestors 'none'");
  };
  auto reply=[&](int status,const char *message,bool success=false) {
    PortalDocument doc(512);doc[success?"message":"error"]=message;
    if(doc.overflowed()) {headers();server.send(503,"application/json","{\"error\":\"Memory unavailable\"}");return;}
    String json;serializeJson(doc,json);headers();server.send(status,"application/json",json);
  };
  auto allowed=[&](bool write) {
    if(!lanRequested.load() || apSetupActive.load() || WiFi.status()!=WL_CONNECTED ||
       server.client().localIP()!=WiFi.localIP() || server.hostHeader()!=WiFi.localIP().toString()+":3000") {
      reply(403,"Use the current LAN address shown on the Core2");return false;
    }
    if(write && server.header("Origin")!=String("http://")+server.hostHeader()) {reply(403,"Origin rejected");return false;}
    return true;
  };
  auto authorized=[&](bool write) {
    if(!allowed(write)) return false;
    String cookies=server.header("Cookie"),value;
    for(int begin=0;begin<int(cookies.length());) {
      int end=cookies.indexOf(';',begin);if(end<0)end=cookies.length();
      String cookie=cookies.substring(begin,end);cookie.trim();
      if(cookie.startsWith("core2_session=")) value=cookie.substring(14);
      begin=end+1;
    }
    if(!access.authorized(session[0] && sameSecret(value,session),millis())) {reply(401,"Authentication required");return false;}
    if(write && !sameSecret(server.header("X-CSRF-Token"),csrf)) {reply(403,"Session check failed; reload this page");return false;}
    return true;
  };
  auto readJson=[&](PortalDocument &doc) {
    String body=server.arg("plain");
    if(body.length()>8192) {reply(413,"Settings request too large");return false;}
    if(deserializeJson(doc,body,DeserializationOption::NestingLimit(8)) || !doc.is<JsonObject>()) {reply(400,"Invalid JSON request");return false;}
    return true;
  };
  auto candidate=[&](runtimecfg::Settings &out,uint32_t &revision) {
    PortalDocument doc(12288);if(!readJson(doc)) return false;
    runtimecfg::Settings current;currentSettings(current,revision);
    if(doc["revision"].is<bool>() || !doc["revision"].is<uint32_t>()) {reply(400,"A settings revision is required");return false;}
    if(doc["revision"].as<uint32_t>()!=revision) {reply(409,"Settings changed; reload before saving");return false;}
    if(!runtimecfg::patch(doc.as<JsonVariantConst>(),current,out)) {reply(400,"Check settings, Wi-Fi names/passwords; a changed URL needs its token");return false;}
    return true;
  };
  const char *headerNames[]={"Cookie","Origin","X-CSRF-Token"};
  server.collectHeaders(headerNames,3);
  server.on("/",HTTP_GET,[&]() {if(!allowed(false))return;headers();server.send_P(200,"text/html; charset=utf-8",LAN_PORTAL_PAGE);});
  server.on("/login",HTTP_POST,[&]() {
    if(!allowed(true))return;
    if(access.limited(millis())) {server.sendHeader("Retry-After","60");reply(429,"Too many attempts; wait one minute");return;}
    PortalDocument doc(512);if(!readJson(doc))return;
    if(!doc["code"].is<const char*>() || doc["code"].as<JsonString>().size()!=8 || !sameSecret(String(doc["code"].as<const char*>()),code)) {access.failed(millis());reply(401,"Incorrect access code");return;}
    randomHex(session);randomHex(csrf);access.login(millis());
    server.sendHeader("Set-Cookie",String("core2_session=")+session+"; HttpOnly; SameSite=Strict; Path=/");reply(200,"Unlocked",true);
  });
  server.on("/api/settings",HTTP_GET,[&]() {
    if(!authorized(false))return;
    runtimecfg::Settings settings;uint32_t revision;currentSettings(settings,revision);
    PortalDocument doc(8192);runtimecfg::publicJson(doc.to<JsonObject>(),settings,revision);doc["csrf"]=csrf;doc["connectedSsid"]=WiFi.SSID();
    if(doc.overflowed()) {reply(500,"Settings serialization failed");return;}
    String json;serializeJson(doc,json);headers();server.send(200,"application/json",json);
  });
  server.on("/api/settings",HTTP_POST,[&]() {
    if(!authorized(true))return;
    runtimecfg::Settings settings;uint32_t revision;if(!candidate(settings,revision))return;
    if(!lanRequested.load()) {reply(403,"Portal has been disabled");return;}
    char error[96]={};
    if(!storeSettings(settings,revision,error,sizeof(error))) {reply(409,error);return;}
    reply(200,"Settings saved. New Wi-Fi credentials are used on reconnect.",true);
  });
  server.on("/api/test",HTTP_POST,[&]() {
    if(!authorized(true))return;
    runtimecfg::Settings settings;uint32_t revision;if(!candidate(settings,revision))return;
    if(xSemaphoreTake(networkGate,pdMS_TO_TICKS(100))!=pdTRUE) {reply(503,"A poll or Wi-Fi operation is running; try again shortly");return;}
    char error[96]={};bool ok=testNightscoutConnection(settings,error,sizeof(error));xSemaphoreGive(networkGate);
    reply(ok?200:400,ok?"HTTPS/auth OK; entries readable. Dashboard polling verifies AAPS and treatments.":error,ok);
  });
  server.on("/api/reconnect",HTTP_POST,[&]() {
    if(!authorized(true))return;
    reply(200,"Reconnect requested. Check the device for its address and new access code.",true);
    reconnectRequested.store(true);xTaskNotifyGive(networkTask);
  });
  server.on("/logout",HTTP_POST,[&]() {
    if(!authorized(true))return;
    access.clear();session[0]=csrf[0]=0;
    server.sendHeader("Set-Cookie","core2_session=; Max-Age=0; HttpOnly; SameSite=Strict; Path=/");reply(200,"Locked",true);
  });
  server.onNotFound([&]() {reply(404,"Not found");});
  int lastState=-1;
  for(;;) {
    bool ready=lanRequested.load() && !apSetupActive.load() && WiFi.status()==WL_CONNECTED && WiFi.localIP()!=IPAddress(0,0,0,0);
    String address=ready?WiFi.localIP().toString():String();
    if(running && (!ready || address!=boundAddress)) {server.stop();running=false;access.clear();session[0]=csrf[0]=code[0]=0;}
    if(ready && !running) {
      boundAddress=address;snprintf(code,sizeof(code),"%08u",unsigned(10000000+esp_random()%90000000));
      server.begin();running=true;lastState=-1;
    }
    int state=running?1:lanRequested.load()?2:0;
    if(state!=lastState) {publish(state);lastState=state;}
    if(running)server.handleClient();
    vTaskDelay(pdMS_TO_TICKS(15));
  }
}
