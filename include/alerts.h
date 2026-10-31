#pragma once
#include <stdint.h>
#include <cmath>
namespace alerts {
constexpr float LOW_MGDL=70, HIGH_MGDL=180;
constexpr uint32_t LOW_REPEAT_MS=15u*60u*1000u;
constexpr uint8_t VOLUME=80; // 0..255
constexpr unsigned CHIRP_HZ=1800, CHIRP_MS=180;
constexpr unsigned STALE_SECONDS=600;
// Independent episode bits let either source go stale without hiding the other.
class StaleState {
  uint8_t current=0,announced=0;
public:
  uint8_t poll(bool glucoseStale,bool loopStale,bool enabled) {
    current=(glucoseStale?1:0)|(loopStale?2:0);
    announced &= current;
    if (!enabled) {announced=0;return 0;}
    return current & ~announced;
  }
  void played(uint8_t mask) {announced |= mask & current;}
};
enum class Event {None,Low,High};
class State {
  Event band=Event::None;
  bool announced=false;
  bool enabled=true;
  uint32_t lastLow=0;
public:
  bool isEnabled() const {return enabled;}
  void setEnabled(bool value) {
    if (value==enabled) return;
    enabled=value;
    if (enabled) {band=Event::None;announced=false;}
  }
  Event poll(float mgdl,bool fresh,uint32_t tick) {
    // Missing/stale samples suppress sound without re-arming the episode.
    if (!enabled || !fresh || !std::isfinite(mgdl) || mgdl<20 || mgdl>600) return Event::None;
    Event next=mgdl<LOW_MGDL?Event::Low:mgdl>HIGH_MGDL?Event::High:Event::None;
    if (next!=band) {band=next;announced=false;}
    if (band==Event::None) return Event::None;
    if (!announced) return band;
    if (band==Event::Low && uint32_t(tick-lastLow)>=LOW_REPEAT_MS) return Event::Low;
    return Event::None;
  }
  // Consume an alert only after the speaker accepts the tone.
  void played(Event event,uint32_t tick) {
    if (!enabled || event==Event::None || event!=band) return;
    announced=true;
    if (event==Event::Low) lastLow=tick;
  }
};
} // namespace alerts
