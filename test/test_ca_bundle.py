"""Offline checks for the complete Mozilla store and ESP32 indexed bundle."""
import hashlib,importlib.util,json
from pathlib import Path
import ssl,unittest
ROOT=Path(__file__).resolve().parents[1]
spec=importlib.util.spec_from_file_location('ca_builder',ROOT/'tools/build_ca_bundle.py')
builder=importlib.util.module_from_spec(spec);spec.loader.exec_module(builder)
INFO=json.loads((ROOT/'certs/bundle-info.json').read_text())
class CertificateBundleTests(unittest.TestCase):
    def test_source_snapshot_is_pinned(self):
        pem=(ROOT/'certs/mozilla-ca.pem').read_bytes()
        self.assertNotIn(b'\0',pem)
        self.assertEqual(hashlib.sha256(pem).hexdigest(),INFO['pem_sha256'])
        self.assertEqual(ssl.create_default_context(cafile=str(ROOT/'certs/mozilla-ca.pem')).cert_store_stats()['x509_ca'],INFO['roots'])
    def test_firmware_bundle_matches_every_source_root(self):
        records=builder.records_from_pem((ROOT/'certs/mozilla-ca.pem').read_bytes())
        binary=(ROOT/'certs/x509_crt_bundle.bin').read_bytes()
        self.assertEqual(builder.decode(binary),records)
        self.assertEqual(builder.encode(records),binary)
        self.assertEqual(len(records),INFO['roots'])
        self.assertEqual(hashlib.sha256(binary).hexdigest(),INFO['bundle_sha256'])
    def test_malformed_der_and_truncated_bundle_are_rejected(self):
        with self.assertRaises(ValueError):builder.subject_and_key(b'\x30\x80')
        with self.assertRaises(ValueError):builder.decode((ROOT/'certs/x509_crt_bundle.bin').read_bytes()[:-1])
if __name__=='__main__':unittest.main()
