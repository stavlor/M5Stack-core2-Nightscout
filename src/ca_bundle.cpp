// Project trust-store adapter for the pinned Arduino ESP32 bundle interface.
// Root trust is exact subject + SPKI, including cross-signed presentations.
#include "project_bundle.h"
#include "ca_bundle_info.h"
#include "tls_anchor_policy.h"
static_assert(tlsanchors::UNTRUSTED==MBEDTLS_X509_BADCERT_NOT_TRUSTED,"Anchor flag compatibility");
static_assert(tlsanchors::WEAK_MD==MBEDTLS_X509_BADCERT_BAD_MD,"Anchor flag compatibility");
#include <mbedtls/md.h>
#include <mbedtls/pk.h>
#include <string.h>
#include <stddef.h>
#include <Arduino.h>
#include <esp_heap_caps.h>
#include <mbedtls/platform.h>
#include <mbedtls/bignum.h>
static_assert(MBEDTLS_ERR_MPI_ALLOC_FAILED==-16,"TLS allocation diagnostic compatibility");
extern const uint8_t bundledStart[] asm("_binary_certs_x509_crt_bundle_bin_start");
extern const uint8_t bundledEnd[] asm("_binary_certs_x509_crt_bundle_bin_end");
namespace {
const uint8_t *entries[CA_BUNDLE_ROOT_COUNT];
bool ready=false;
uint32_t lastFlags=0;
mbedtls_x509_crt dummy;
bool dummyInitialized=false;
void *tlsCalloc(size_t count,size_t size) {
  if(size && count>SIZE_MAX/size)return nullptr;
  // The pinned SDK otherwise restricts every TLS allocation to internal RAM.
  // Prefer PSRAM for both record buffers and crypto temporaries; heap_caps_free
  // accepts either region, including allocations made before hook installation.
  void *memory=heap_caps_calloc(count,size,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);
  return memory?memory:heap_caps_calloc(count,size,MALLOC_CAP_INTERNAL|MALLOC_CAP_8BIT);
}
size_t length16(const uint8_t *value) {return size_t(value[0])*256+value[1];}
int compareName(const uint8_t *name,size_t size,const uint8_t *entry) {
  size_t other=length16(entry),common=size<other?size:other;
  int compared=memcmp(name,entry+4,common);
  return compared?compared:size<other?-1:size>other?1:0;
}
const uint8_t *findName(const mbedtls_x509_buf &name) {
  int low=0,high=int(CA_BUNDLE_ROOT_COUNT)-1;
  while(low<=high) {
    int middle=low+(high-low)/2;int compared=compareName(name.p,name.len,entries[middle]);
    if(!compared)return entries[middle];
    if(compared<0)high=middle-1;else low=middle+1;
  }
  return nullptr;
}
const uint8_t *publicKey(const uint8_t *entry) {return entry+4+length16(entry);}
size_t keyLength(const uint8_t *entry) {return length16(entry+2);}
bool trustedIdentity(mbedtls_x509_crt *certificate) {
  const uint8_t *entry=findName(certificate->subject_raw);
  if(!entry)return false;
  unsigned char encoded[2048];
  int size=mbedtls_pk_write_pubkey_der(&certificate->pk,encoded,sizeof(encoded));
  return size>0 && tlsanchors::sameKey(encoded+sizeof(encoded)-size,size_t(size),publicKey(entry),keyLength(entry));
}
int signedBy(mbedtls_x509_crt *certificate,const uint8_t *entry) {
  mbedtls_pk_context key;mbedtls_pk_init(&key);
  int result=mbedtls_pk_parse_public_key(&key,publicKey(entry),keyLength(entry));
  if(result==0 && !mbedtls_pk_can_do(&key,certificate->sig_pk)) result=MBEDTLS_ERR_PK_TYPE_MISMATCH;
  if(result==0) {
    const mbedtls_md_info_t *digest=mbedtls_md_info_from_type(certificate->sig_md);
    unsigned char hash[MBEDTLS_MD_MAX_SIZE];
    result=digest?mbedtls_md(digest,certificate->tbs.p,certificate->tbs.len,hash):MBEDTLS_ERR_MD_FEATURE_UNAVAILABLE;
    if(result==0)
      result=mbedtls_pk_verify_ext(certificate->sig_pk,certificate->sig_opts,&key,certificate->sig_md,
        hash,mbedtls_md_get_size(digest),certificate->sig.p,certificate->sig.len);
  }
  mbedtls_pk_free(&key);return result;
}
void fingerprint(const mbedtls_x509_buf &value,char output[65]) {
  unsigned char digest[32];
  const auto *sha=mbedtls_md_info_from_type(MBEDTLS_MD_SHA256);
  if(!sha || mbedtls_md(sha,value.p,value.len,digest)!=0) {strcpy(output,"unavailable");return;}
  const char hex[]="0123456789abcdef";
  for(unsigned i=0;i<32;++i) {output[2*i]=hex[digest[i]>>4];output[2*i+1]=hex[digest[i]&15];}
  output[64]=0;
}
int verify(void*,mbedtls_x509_crt *certificate,int depth,uint32_t *flags) {
  if(!*flags)return 0;
  const uint32_t anchorFlags=MBEDTLS_X509_BADCERT_NOT_TRUSTED|MBEDTLS_X509_BADCERT_BAD_MD;
  // Date, name, usage, algorithm and revocation errors are never waived.
  if((*flags&~anchorFlags) || !(*flags&MBEDTLS_X509_BADCERT_NOT_TRUSTED)) {lastFlags|=*flags;return 0;}
  tlsanchors::Action action=tlsanchors::action(*flags,ready && trustedIdentity(certificate));
  if(action==tlsanchors::Action::AcceptAnchor) {
    // An explicitly trusted root key does not need its external cross-signature.
    *flags&=~anchorFlags;return 0;
  }
  // Weak hashes are ignored only for an exact trusted anchor above, not for
  // an arbitrary intermediate merely claiming a trusted issuer.
  const uint8_t *issuer=ready?findName(certificate->issuer_raw):nullptr;
  int signature=1; // Not attempted: no eligible trusted issuer.
  if(action==tlsanchors::Action::CheckIssuer && issuer) {
    signature=signedBy(certificate,issuer);
    if(signature==0) {*flags=0;return 0;}
  }
  // Fingerprints identify public certificates without logging hostnames or data.
  char certHash[65],issuerHash[65];
  fingerprint(certificate->raw,certHash);fingerprint(certificate->issuer_raw,issuerHash);
  Serial.printf("TLS anchor failure: depth=%d flags=0x%08lX ready=%u subject=%u issuer=%u signature=%d cert-sha256=%s issuer-dn-sha256=%s\n",
    depth,(unsigned long)*flags,unsigned(ready),unsigned(ready && findName(certificate->subject_raw)!=nullptr),
    unsigned(issuer!=nullptr),signature,certHash,issuerHash);
  lastFlags|=*flags;return MBEDTLS_ERR_X509_FATAL_ERROR;
}
}
extern "C" void arduino_esp_crt_bundle_set(const uint8_t *bundle) {
  lastFlags=0;
  if(bundle!=bundledStart) {ready=false;return;}
  if(ready)return;
  const size_t total=size_t(bundledEnd-bundledStart);
  if(total<2 || length16(bundle)!=CA_BUNDLE_ROOT_COUNT)return;
  size_t position=2;
  for(unsigned i=0;i<CA_BUNDLE_ROOT_COUNT;++i) {
    if(position+4>total)return;
    size_t name=length16(bundle+position),key=length16(bundle+position+2);
    if(!name || !key || key>2048 || name+key>total-position-4)return;
    entries[i]=bundle+position;position+=4+name+key;
    if(i && compareName(entries[i]+4,length16(entries[i]),entries[i-1])<=0)return;
  }
  ready=position==total;
}
extern "C" esp_err_t arduino_esp_crt_bundle_attach(void *configuration) {
  if(!ready || !configuration)return ESP_ERR_INVALID_STATE;
  if(!dummyInitialized) {mbedtls_x509_crt_init(&dummy);dummyInitialized=true;}
  auto *config=static_cast<mbedtls_ssl_config*>(configuration);
  mbedtls_ssl_conf_authmode(config,MBEDTLS_SSL_VERIFY_REQUIRED);
  mbedtls_ssl_conf_ca_chain(config,&dummy,nullptr);
  mbedtls_ssl_conf_verify(config,verify,nullptr);
  return ESP_OK;
}
extern "C" void arduino_esp_crt_bundle_detach(mbedtls_ssl_config *config) {
  ready=false;lastFlags=0;
  if(config) {mbedtls_ssl_conf_verify(config,nullptr,nullptr);mbedtls_ssl_conf_ca_chain(config,nullptr,nullptr);mbedtls_ssl_conf_authmode(config,MBEDTLS_SSL_VERIFY_REQUIRED);}
}
extern "C" uint32_t project_bundle_last_verify_flags() {return lastFlags;}
extern "C" bool project_tls_memory_init() {
  return mbedtls_platform_set_calloc_free(tlsCalloc,heap_caps_free)==0;
}
