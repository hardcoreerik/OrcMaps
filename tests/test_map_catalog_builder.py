"""Owned map catalog contract; no hosting or production credentials."""
import copy
import hashlib
import importlib.util
import json
from pathlib import Path
import subprocess
import tempfile
import unittest
from unittest import mock

SCRIPT = Path(__file__).parents[1] / "tools/pack-builder/build_map_catalog.py"
SPEC = importlib.util.spec_from_file_location("map_catalog_builder", SCRIPT)
CATALOG = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(CATALOG)


class CatalogTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.archive = self.root / "world.pmtiles"
        self.archive.write_bytes(b"PMTiles\x03fixture")
        self.digest = hashlib.sha256(self.archive.read_bytes()).hexdigest()
        self.manifest = {
            "pack_id": "world-fixture", "size_bytes": self.archive.stat().st_size,
            "output_sha256": self.digest, "display_name": "World",
            "bounds": {"min_lon": -180, "min_lat": -85, "max_lon": 180, "max_lat": 85},
            "required_attribution": ["Fixture credit"], "content_profile": "protomaps-v4",
        }
        self.path = self.root / "world.manifest.json"
        self.path.write_text(json.dumps(self.manifest), encoding="utf-8")
        self.spec = {
            "schema": "orcmaps-catalog-input-v1", "revision": 1,
            "generated_at": "2026-10-19T00:00:00Z", "expires_at": "2026-11-19T00:00:00Z",
            "packs": [{"product_id": "global-regional", "manifest": "world.manifest.json",
                       "urls": [f"https://maps.example.test/{self.digest}/world.pmtiles"]}],
        }

    def build(self, spec=None):
        checked = []
        result = CATALOG.assemble(spec or self.spec, self.root, checked.append)
        self.assertEqual(checked, [self.root])
        return result

    def test_catalog_binds_manifest_bytes_hash_and_mirrors(self):
        result = self.build()
        self.assertEqual(result["schema"], "orcmaps-catalog-v1")
        self.assertEqual(result["packs"][0]["manifest"], self.manifest)
        self.assertEqual(result["packs"][0]["product_id"], "global-regional")
        self.assertEqual(result["packs"][0]["archive"]["sha256"], self.digest)
        self.assertNotIn("destination", result["packs"][0]["archive"])
        self.assertEqual(CATALOG.encode(result), CATALOG.encode(copy.deepcopy(result)))

    def test_wrong_size_and_same_size_hash_corruption_are_rejected(self):
        for data in (b"short", b"PMTiles\x03corrupt"):
            with self.subTest(data=data):
                self.archive.write_bytes(data)
                with self.assertRaises(ValueError):
                    self.build()

    def test_unsafe_or_mutable_urls_are_rejected(self):
        for url in ("http://maps.test/a", "https://user:secret@maps.test/a",
                    "https://maps.test/latest.pmtiles", "https://maps.test/../" + self.digest,
                    f"https://maps.test/{self.digest}/x?token=secret",
                    f"https://maps.test/{self.digest}/x#fragment",
                    f"https://maps.test/{self.digest}/x%20y.pmtiles",
                    f"https://-bad.test/{self.digest}/x.pmtiles",
                    f"https://maps.test/{self.digest}/x\n"):
            with self.subTest(url=url):
                spec = copy.deepcopy(self.spec)
                spec["packs"][0]["urls"] = [url]
                with self.assertRaises(ValueError):
                    self.build(spec)

    def test_duplicate_pack_identity_is_rejected(self):
        self.spec["packs"] *= 2
        with self.assertRaises(ValueError):
            self.build()

    def test_product_identity_is_explicit_bounded_and_unique(self):
        for product in ("", "../manual", "UPPER", "a"*64, None, 7):
            spec = copy.deepcopy(self.spec)
            spec["packs"][0]["product_id"] = product
            with self.subTest(product=product), self.assertRaises(ValueError):
                self.build(spec)
        spec = copy.deepcopy(self.spec)
        del spec["packs"][0]["product_id"]
        with self.assertRaises(ValueError):
            self.build(spec)
        # Distinct artifacts for the same logical product must not coexist.
        second = dict(self.manifest, pack_id="world-next-version")
        (self.root/"next.manifest.json").write_text(json.dumps(second), encoding="utf-8")
        (self.root/"next.pmtiles").write_bytes(self.archive.read_bytes())
        self.spec["packs"].append(dict(self.spec["packs"][0], manifest="next.manifest.json"))
        with self.assertRaisesRegex(ValueError, "duplicate product"):
            self.build()

    def test_revision_time_and_limits_are_checked(self):
        for key, value in (("revision", True), ("revision", 0), ("revision", 1.5),
                           ("expires_at", "2026-01-01T00:00:00Z"),
                           ("generated_at", "not a timestamp"),
                           ("generated_at", "1900-01-01T00:00:00Z"), ("packs", [])):
            with self.subTest(key=key, value=value):
                spec = copy.deepcopy(self.spec)
                spec[key] = value
                with self.assertRaises(ValueError):
                    self.build(spec)

    def test_engine_rejection_is_not_ignored(self):
        def reject(directory):
            raise ValueError("engine rejected schema")
        with self.assertRaisesRegex(ValueError, "engine rejected"):
            CATALOG.assemble(self.spec, self.root, reject)

    def test_duplicate_json_keys_are_rejected(self):
        with self.assertRaises(ValueError):
            CATALOG.decode('{"revision":1,"revision":2}')

    def test_old_verifier_success_is_not_a_strict_preflight(self):
        result = subprocess.CompletedProcess([], 0, stdout="RESULT: OK\n")
        with mock.patch.object(CATALOG.subprocess, "run", return_value=result):
            with self.assertRaisesRegex(ValueError, "strict header"):
                CATALOG.engine_preflight(Path("verifier"), self.root)

    def test_payload_matches_device_structure_limits(self):
        deep = 0
        for _ in range(17):
            deep = [deep]
        for value in ({"x": "hidden\x00text"}, {"x": [0]*4096},
                      {str(i): i for i in range(129)}, {"x": deep}):
            with self.subTest(value_type=type(value)):
                with self.assertRaises(ValueError):
                    CATALOG.encode(value)

    def test_signature_verifies_and_modified_payload_fails(self):
        openssl = Path("C:/Program Files/Git/usr/bin/openssl.exe")
        if not openssl.exists():
            self.skipTest("explicit OpenSSL fixture unavailable")
        private, public = self.root / "test-private.pem", self.root / "test-public.pem"
        subprocess.run([str(openssl), "genpkey", "-algorithm", "EC", "-pkeyopt",
                        "ec_paramgen_curve:P-256", "-out", str(private)], check=True,
                       capture_output=True)
        subprocess.run([str(openssl), "pkey", "-in", str(private), "-pubout", "-out",
                        str(public)], check=True, capture_output=True)
        payload = CATALOG.encode(self.build())
        signature = CATALOG.sign(payload, private, public, str(openssl))
        self.assertTrue(CATALOG.verify(payload, signature, public, str(openssl)))
        self.assertFalse(CATALOG.verify(payload + b" ", signature, public, str(openssl)))


if __name__ == "__main__":
    unittest.main()
