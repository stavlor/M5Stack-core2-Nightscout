#pragma once
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

// Fixed-capacity decoded HTTP body. No reallocations while TLS is active.
class BodyBuffer {
public:
  char *data;
  const size_t capacity;
  size_t length=0;
  bool overflow=false;
  explicit BodyBuffer(size_t cap, void *(*allocate)(size_t)=malloc)
    : data(static_cast<char*>(allocate(cap+1))), capacity(cap) {
    if (data) data[0]=0;
  }
  ~BodyBuffer() { free(data); }
  BodyBuffer(const BodyBuffer&)=delete;
  BodyBuffer& operator=(const BodyBuffer&)=delete;
  size_t append(const uint8_t *p,size_t n) {
    if (!data || overflow) return 0;
    if (n>capacity-length) {overflow=true;return 0;}
    memcpy(data+length,p,n);length+=n;data[length]=0;return n;
  }
};
