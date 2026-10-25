"""Bounded CLI regression using the actual engine verifier."""
import hashlib
import json
import os
from pathlib import Path
import subprocess
import struct
import tempfile
import unittest


class PackVerifierTests(unittest.TestCase):
    def test_valid_header_passes_but_wider_coverage_and_zoom_fail(self):
        executable = os.environ.get("ORCMAP_PACK_VERIFY")
        if not executable:
            self.skipTest("set ORCMAP_PACK_VERIFY to built verifier")
        root = Path(__file__).parents[1]
        original = json.loads((root / "examples/m5stack-tab5/test-pack/"
                               "springfield-97477.manifest.json").read_text(encoding="utf-8"))
        archive = bytearray((root / "tests/fixtures/tiny-gzip.pmtiles").read_bytes())
        struct.pack_into("<4i", archive, 102, -10000000, -10000000, 10000000, 10000000)
        for west, max_zoom, expected in ((-1, 0, 0), (-2, 0, 1), (-1, 1, 1)):
            with self.subTest(west=west, max_zoom=max_zoom), tempfile.TemporaryDirectory() as folder:
                manifest = dict(original)
                manifest.update(bounds=dict(min_lon=west, min_lat=-1, max_lon=1, max_lat=1),
                                min_zoom=0, max_zoom=max_zoom, size_bytes=len(archive),
                                output_sha256=hashlib.sha256(archive).hexdigest())
                manifest["pack_id"] = (f"{manifest['source_snapshot']}__{manifest['schema_version']}__"
                                       f"{manifest['content_profile']}__{manifest['region_id']}__"
                                       f"b{west * 10000000}_-10000000_10000000_10000000__z0-{max_zoom}")
                directory = Path(folder)
                (directory / "fixture.pmtiles").write_bytes(archive)
                (directory / "fixture.manifest.json").write_text(json.dumps(manifest), encoding="utf-8")
                result = subprocess.run([executable, folder], capture_output=True, text=True)
                self.assertIn("archive:  z0-0", result.stdout)
                self.assertEqual(result.returncode, expected, result.stdout)

    def test_unreadable_archive_header_is_a_failure(self):
        executable = os.environ.get("ORCMAP_PACK_VERIFY")
        if not executable:
            self.skipTest("set ORCMAP_PACK_VERIFY to built verifier")
        root = Path(__file__).parents[1]
        manifest = json.loads((root / "examples/m5stack-tab5/test-pack/"
                               "springfield-97477.manifest.json").read_text(encoding="utf-8"))
        corrupt = bytes(127)
        manifest.update(size_bytes=len(corrupt), output_sha256=hashlib.sha256(corrupt).hexdigest())
        with tempfile.TemporaryDirectory() as folder:
            directory = Path(folder)
            (directory / "bad.pmtiles").write_bytes(corrupt)
            (directory / "bad.manifest.json").write_text(json.dumps(manifest), encoding="utf-8")
            result = subprocess.run([executable, folder], capture_output=True, text=True)
        self.assertIn("header:   not a valid PMTiles container", result.stdout)
        self.assertNotEqual(result.returncode, 0, result.stdout)


if __name__ == "__main__":
    unittest.main()
