#pragma once
#include <stdint.h>
#include <stddef.h>
#include <string.h>
namespace tlsanchors {
constexpr uint32_t UNTRUSTED=0x8,WEAK_MD=0x4000;
enum class Action {PreserveFailure,AcceptAnchor,CheckIssuer};
inline bool sameKey(const unsigned char *actual,size_t actualSize,const unsigned char *expected,size_t expectedSize) {
  return actual && expected && actualSize && actualSize==expectedSize && !memcmp(actual,expected,actualSize);
}
inline Action action(uint32_t flags,bool exactTrustedIdentity) {
  if(!flags || (flags&~(UNTRUSTED|WEAK_MD)) || !(flags&UNTRUSTED))return Action::PreserveFailure;
  if(exactTrustedIdentity)return Action::AcceptAnchor;
  return flags==UNTRUSTED?Action::CheckIssuer:Action::PreserveFailure;
}
}
