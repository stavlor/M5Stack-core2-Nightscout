#pragma once
#include <stdint.h>
namespace tlsdiagnostics {
constexpr uint32_t EXPIRED=0x1,REVOKED=0x2,HOSTNAME=0x4,UNTRUSTED=0x8,MISSING=0x40,FUTURE=0x200;
constexpr uint32_t ALGORITHM=0x4000|0x8000|0x10000;
inline const char *message(uint32_t flags) {
  if(!flags || flags==0xffffffffu)return "TLS verify: flags unavailable";
  if(flags&(EXPIRED|FUTURE))return "TLS cert date/clock invalid";
  if(flags&HOSTNAME)return "TLS hostname mismatch";
  if(flags&REVOKED)return "TLS certificate revoked";
  if(flags&MISSING)return "TLS peer certificate missing";
  if(flags&ALGORITHM)return "TLS certificate algorithm/key";
  if(flags&UNTRUSTED)return "TLS CA/chain untrusted";
  return "TLS verify: see serial flags";
}
}
