#pragma once
#include <stdint.h>
#include <esp_err.h>
#include <mbedtls/ssl.h>
extern "C" {
// Interface used by Arduino ESP32's WiFiClientSecure archive. The project
// supplies these four functions so the old archive verifier is not linked.
esp_err_t arduino_esp_crt_bundle_attach(void *configuration);
void arduino_esp_crt_bundle_detach(mbedtls_ssl_config *configuration);
void arduino_esp_crt_bundle_set(const uint8_t *bundle);
uint32_t project_bundle_last_verify_flags();
// Call once after PSRAM initialization and before any network tasks start.
bool project_tls_memory_init();
}
