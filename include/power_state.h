#pragma once
#include <stdint.h>
#include <cmath>
namespace powerstate {
enum class Source {Battery=0,USB=1,External=2};
inline bool supplying(float volts,float milliAmps) {
  return std::isfinite(volts) && std::isfinite(milliAmps) && volts>=4.0f && milliAmps>1.0f;
}
inline Source classify(uint8_t status,float usbVolts,float usbMilliAmps,float extVolts,float extMilliAmps) {
  // AXP192 REG00: bits 5/4 = VBUS present/usable, bits 7/6 = ACIN present/usable.
  // Require measured incoming current too; voltage alone can reflect a backfed rail.
  if ((status&0x30)==0x30 && supplying(usbVolts,usbMilliAmps)) return Source::USB;
  if ((status&0xc0)==0xc0 && supplying(extVolts,extMilliAmps)) return Source::External;
  return Source::Battery;
}
}
