#pragma once
#include <stddef.h>
#include <string.h>
namespace quicksettings {
inline bool validHours(unsigned value) {return value==1 || value==3 || value==6 || value==12;}
inline unsigned restoreHours(unsigned value,unsigned fallback) {
  return validHours(value)?value:validHours(fallback)?fallback:3;
}
struct Credentials {char ssid[33]={};char password[65]={};};
inline bool validCredentials(const Credentials &c) {
  if(!memchr(c.ssid,0,sizeof(c.ssid)) || !memchr(c.password,0,sizeof(c.password))) return false;
  size_t ssid=strlen(c.ssid),password=strlen(c.password);
  if(!ssid || ssid>32 || (password && (password<8 || password>63))) return false;
  return !strchr(c.ssid,'\r') && !strchr(c.ssid,'\n') && !strchr(c.password,'\r') && !strchr(c.password,'\n');
}
}
