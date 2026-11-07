#pragma once
#include <ArduinoJson.h>
#include <stdint.h>
#include <string.h>
namespace nssocket {
enum class Packet { Ignore, Open, Ping, Connected, Subscribed, Changed, Close };
inline Packet parse(const char *data,size_t length,uint32_t &timeout) {
  if(!length)return Packet::Ignore;
  if(data[0]=='2')return length<=125?Packet::Ping:Packet::Close;
  if(data[0]=='1')return Packet::Close;
  if(data[0]=='0') {
    StaticJsonDocument<512> doc;
    if(deserializeJson(doc,data+1,length-1))return Packet::Close;
    if(!doc["sid"].is<const char*>() || !doc["pingInterval"].is<unsigned>() || !doc["pingTimeout"].is<unsigned>())return Packet::Close;
    uint64_t total=uint64_t(doc["pingInterval"].as<unsigned>())+doc["pingTimeout"].as<unsigned>();
    if(total<1000 || total>300000)return Packet::Close;
    timeout=uint32_t(total);return Packet::Open;
  }
  if(length>=12 && !memcmp(data,"40/storage,",11))return Packet::Connected;
  if(length>=11 && (!memcmp(data,"41/storage,",11) || !memcmp(data,"44/storage,",11)))return Packet::Close;
  bool ack=length>12 && !memcmp(data,"43/storage,1",12);
  bool event=length>11 && !memcmp(data,"42/storage,",11);
  if(!ack && !event)return Packet::Ignore;
  size_t offset=ack?12:11;
  if(event) {
    // Socket.IO's JSON.stringify encoding is compact. Inspect only its event
    // name; never allocate a document for the potentially large data argument.
    const char *names[]={"[\"create\"","[\"update\"","[\"delete\""};
    for(const char *name:names) {
      size_t n=strlen(name);
      if(length>offset+n && !memcmp(data+offset,name,n) && (data[offset+n]==',' || data[offset+n]==']'))return Packet::Changed;
    }
    return Packet::Ignore;
  }
  // Keep only the event name or subscription result. Storage document payloads
  // are deliberately discarded; REST remains the data source.
  StaticJsonDocument<192> filter;StaticJsonDocument<512> doc;
  filter[0]["success"]=true;
  if(deserializeJson(doc,data+offset,length-offset,DeserializationOption::Filter(filter),DeserializationOption::NestingLimit(24)))return Packet::Close;
  return doc[0]["success"]==true?Packet::Subscribed:Packet::Close;
}
// Unsigned subtraction makes scheduling safe across millis() wrap.
inline bool due(uint32_t now,uint32_t since,uint32_t interval) {return uint32_t(now-since)>=interval;}
}
