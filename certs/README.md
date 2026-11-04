# Public CA trust store

Firmware now uses the complete Mozilla root snapshot distributed by certifi
2026.10.23: 118 roots. It no longer uses the earlier three-root PEM list.

- mozilla-ca.pem: unmodified certificate data (LF-normalized) from that installed
  certifi release. Mozilla source: https://github.com/certifi/python-certifi
- x509_crt_bundle.bin: indexed subject/public-key data consumed by the pinned
  Arduino ESP32 2.0.17 setCACertBundle API. This is CA data, not firmware.
- bundle-info.json: source version, format, root count and SHA-256 hashes.
- CERTIFI-LICENSE.txt: upstream license text for the redistributed CA bundle.

The generated binary is 54,752 bytes. It contains sorted DER subject names and
SubjectPublicKeyInfo keys; the SDK verifies signatures using the matching key
without loading all PEM certificates into the smaller internal RAM. The package
includes the binary so users do not need extra build dependencies.

## Reproduce / update

From the project directory, using Python 3.9+:

```text
python tools/build_ca_bundle.py --source-version 2026.10.23
python -m unittest discover -s test -p test_ca_bundle.py -v
node test/test_ca_bundle_node.cjs
```

Generation uses only the Python standard library. The optional Node 18+ check
independently compares every embedded public key with Node/OpenSSL's X509 parser.
The format and byte order match the Arduino framework's esp_crt_bundle.c reader.
Source subjects must be unique or have identical keys; ambiguous keys are rejected.

For maintenance, obtain a reviewed newer certifi/Mozilla CA snapshot, replace
mozilla-ca.pem, update the source version, regenerate and run checks before
rebuilding firmware. The generator also updates bundle-info.json and the compiled
build-identification header. Private CAs require an explicitly reviewed root
addition and regeneration; self-signed servers are not trusted automatically.

## Scope

This provides broad public-issuer coverage, not complete browser-engine parity.
The pinned ESP32 TLS stack still governs supported protocols/algorithms, chain
construction, name and time checks. Browser-specific CT/revocation policies are
not added by this root store. Existing valid leaf renewals normally need no update;
root-store changes still require rebuilding/flashing. No online CA download or
insecure acceptance fallback runs on the device.

At boot, serial output identifies the source version, root count and bundle hash.
The project adapter in src/ca_bundle.cpp recognizes cross-signed roots only when
their exact subject and public key match a bundled anchor. It preserves date,
hostname and usage failures. Intermittent device trust failures remain under
investigation; successful requests alone do not establish that they are resolved.
Unresolved anchor checks now log certificate and issuer-name SHA-256 fingerprints,
chain depth, root lookup results and the signature verification return code.
These diagnostics contain no certificate names, tokens or clinical values.
Failures report verification flags, UTC and internal heap figures without the URL,
subject token, response body or clinical values. The old generic -0x2700 message
alone could not distinguish hostname/date/trust/algorithm failures.

## TLS memory follow-up

Device signature=-16 identifies MBEDTLS_ERR_MPI_ALLOC_FAILED, rather than a
missing issuer. The pinned SDK confines TLS to internal RAM. The project now
installs PSRAM-preferred calloc/free hooks once after hardware initialization
and before networking starts, with internal RAM fallback. Both allocation paths
are zero-filled and use heap_caps_free; overflow is rejected. This is a memory
policy change, not a certificate-verification exception. Device validation of
this change is pending. Startup logs identify the allocator and PSRAM free size.
