#pragma once
// Host-only shim: model.h uses no hardware or Arduino String API.
#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <stdarg.h>
inline size_t strlcpy(char *dst,const char *src,size_t n) {
  size_t len=strlen(src);
  if (n) {size_t copy=len<n-1?len:n-1; memcpy(dst,src,copy);dst[copy]=0;}
  return len;
}
struct SerialShim {
  void printf(const char *fmt,...) {va_list args;va_start(args,fmt);vprintf(fmt,args);va_end(args);}
};
static SerialShim Serial;
