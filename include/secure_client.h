#pragma once
#include <WiFiClientSecure.h>
#include "tls_diagnostics.h"
#include "project_bundle.h"
static_assert(tlsdiagnostics::UNTRUSTED==MBEDTLS_X509_BADCERT_NOT_TRUSTED,"TLS flag compatibility");
static_assert(tlsdiagnostics::HOSTNAME==MBEDTLS_X509_BADCERT_CN_MISMATCH,"TLS flag compatibility");
static_assert(tlsdiagnostics::FUTURE==MBEDTLS_X509_BADCERT_FUTURE,"TLS flag compatibility");
class SecureClient : public WiFiClientSecure {
  uint32_t verifyFlags=0;
public:
  explicit SecureClient(const uint8_t *bundle) {
    setCACertBundle(bundle);setHandshakeTimeout(12);
  }
  uint32_t verificationFlags() const {return verifyFlags;}
  void stop() override {
    // Base connect() calls virtual stop() before returning a handshake error.
    // Capture flags here, before the SDK frees/zeros the SSL context.
    if(sslclient && _lastError<0) {
      uint32_t flags=mbedtls_ssl_get_verify_result(&sslclient->ssl_ctx);
      if(flags && flags!=0xffffffffu)verifyFlags=flags;
      verifyFlags|=project_bundle_last_verify_flags();
    }
    WiFiClientSecure::stop();
  }
};
