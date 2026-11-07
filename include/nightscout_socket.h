#pragma once
#include <WebSocketsClient.h>
#include "socketio_protocol.h"
#ifndef NS_ENABLE_SOCKETIO
#define NS_ENABLE_SOCKETIO 1
#endif
// Owned and serviced solely by the network worker under networkGate. The same
// CA bundle adapter and PSRAM allocator protect both WSS and HTTPS connections.
class NightscoutSocket {
  WebSocketsClient ws;
  String origin,token;
  bool enabled=false,subscribed=false,dirty=false,handshaking=false;
  uint32_t received=0,started=0,dirtyAt=0,timeout=45000,lastFetch=0;
  void event(WStype_t type,uint8_t *payload,size_t length) {
    if(type==WStype_DISCONNECTED) {subscribed=false;handshaking=false;return;}
    if(type==WStype_CONNECTED) {received=started=millis();handshaking=true;return;}
    if(type!=WStype_TEXT)return;
    auto packet=nssocket::parse(reinterpret_cast<const char*>(payload),length,timeout);
    if(packet==nssocket::Packet::Ping) {
      // Engine.IO heartbeat (distinct from WebSocket-level ping/pong).
      payload[0]='3';ws.sendTXT(payload,length);received=millis();return;
    }
    if(packet==nssocket::Packet::Open) {received=millis();ws.sendTXT("40/storage,");}
    else if(packet==nssocket::Packet::Connected && handshaking) {
      StaticJsonDocument<1024> doc;doc[0]="subscribe";
      doc[1]["accessToken"]=token;
      auto cols=doc[1].createNestedArray("collections");
      cols.add("entries");cols.add("devicestatus");cols.add("treatments");
      String message="42/storage,1";serializeJson(doc,message);ws.sendTXT(message);
    } else if(packet==nssocket::Packet::Subscribed && handshaking) {
      subscribed=true;handshaking=false;
      Serial.println("Nightscout Socket.IO: storage subscribed");
      markChanged(); // Reconcile updates missed while disconnected.
    } else if(packet==nssocket::Packet::Changed && subscribed)markChanged();
    else if(packet==nssocket::Packet::Close)ws.disconnect();
  }
  void markChanged() {if(!dirty){dirty=true;dirtyAt=millis();}}
public:
  NightscoutSocket() {ws.onEvent([this](WStype_t t,uint8_t *p,size_t n){event(t,p,n);});ws.setReconnectInterval(30000);}
  void stop() {ws.disconnect();enabled=subscribed=dirty=handshaking=false;origin="";token="";}
  void configure(const char *url,const char *subject,bool v3,const uint8_t *bundle) {
    bool wanted=NS_ENABLE_SOCKETIO && v3 && subject[0] && strncmp(subject,"YOUR_",5) && !strncmp(url,"https://",8);
    if(enabled && wanted && origin==url && token==subject)return;
    stop();if(!wanted)return;
    String address=url+8;int slash=address.indexOf('/');
    String authority=slash<0?address:address.substring(0,slash);
    String path=slash<0?"":address.substring(slash);
    String host=authority;uint16_t port=443;
    int colon=authority.lastIndexOf(':');
    if(colon>=0) {
      // IPv6 bracket literals are left to HTTPS polling for this first version.
      if(authority.indexOf('[')>=0)return;
      long value=authority.substring(colon+1).toInt();if(value<1 || value>65535)return;
      port=uint16_t(value);host=authority.substring(0,colon);
    }
    if(!host.length())return;
    origin=url;token=subject;enabled=true;
    path+="/socket.io/?EIO=4&transport=websocket";
    ws.beginSslWithBundle(host.c_str(),port,path.c_str(),bundle,"");
    ws.setReconnectInterval(30000);
  }
  void fetched() {lastFetch=millis();dirty=false;}
  bool tick() {
    if(!enabled)return false;
    ws.loop();uint32_t now=millis();
    if(ws.isConnected() && (nssocket::due(now,received,timeout) || (handshaking && nssocket::due(now,started,15000))))ws.disconnect();
    return subscribed && dirty && nssocket::due(now,dirtyAt,2000) && nssocket::due(now,lastFetch,10000);
  }
};
