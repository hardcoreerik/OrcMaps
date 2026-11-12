"""Host-only regression tests for 50-state OrcMaps pack production."""
from __future__ import annotations

import contextlib
import hashlib
import io
import json
from pathlib import Path
import sys
import tempfile
import unittest
from unittest import mock

BUILDER_DIR = Path(__file__).parents[1] / "tools" / "pack-builder"
sys.path.insert(0, str(BUILDER_DIR))
import build_us_state_packs as states


class UsStatePackTests(unittest.TestCase):
    def setUp(self):
        self.catalog = states.load_catalog()
        self.by_code = {s["code"]: s for s in self.catalog}

    def test_all_fifty_states_and_unique_artifacts(self):
        self.assertEqual(50, len(self.catalog))
        self.assertEqual(50, len(self.by_code))
        self.assertEqual(51, sum(len(s["parts"]) for s in self.catalog))
        self.assertEqual(2, len(self.by_code["AK"]["parts"]))
        self.assertIn("HI", self.by_code)
        self.assertEqual("US-OR", self.by_code["OR"]["region_id"])
        self.assertTrue(all(not p["stem"].endswith(".pmtiles")
                            for s in self.catalog for p in s["parts"]))

    def test_alaska_dateline_handling(self):
        west, east = self.by_code["AK"]["parts"]
        self.assertEqual(-180, west["bbox"][0])
        self.assertEqual(180, east["bbox"][2])
        self.assertLess(west["bbox"][2], 0)
        self.assertGreater(east["bbox"][0], 0)
        self.assertLess(west["bbox"][2] - west["bbox"][0], 100)
        self.assertLess(east["bbox"][2] - east["bbox"][0], 15)

    def test_state_selection_is_strict(self):
        self.assertEqual(["OR", "WA"], [s["code"] for s in states.selected_states(
            self.catalog, ["Oregon", "US-WA"])])
        with self.assertRaises(ValueError):
            states.selected_states(self.catalog, ["OR", "US-OR"])
        with self.assertRaises(ValueError):
            states.selected_states(self.catalog, ["GU"])

    def fixture(self, directory: Path):
        archive = directory / "united-states.pmtiles"
        archive.write_bytes(b"PMTiles\x03" + bytes(120))
        sha = hashlib.sha256(archive.read_bytes()).hexdigest()
        manifest = {
            "bounds": {"min_lon": -180, "min_lat": 15, "max_lon": 180, "max_lat": 75},
            "schema_version": "openmaptiles-3.16",
            "pmtiles_version": 3,
            "source_snapshot": "test",
            "sources": [{"provenance_id": "openstreetmap",
                         "acquired": "2026-11-12", "source_version": "test"},
                        {"provenance_id": "openmaptiles",
                         "acquired": "2026-11-12", "source_version": "test"}],
            "min_zoom": 0, "max_zoom": 13, "content_profile": "standard",
            "size_bytes": archive.stat().st_size, "output_sha256": sha,
            "pack_class": "open",
            "required_attribution": ["© OpenMapTiles", "© OpenStreetMap contributors"],
            "attribution_links": ["https://openmaptiles.org/",
                                  "https://www.openstreetmap.org/copyright"],
        }
        source_manifest = directory / "united-states.manifest.json"
        source_manifest.write_text(json.dumps(manifest), encoding="utf-8")
        return archive, source_manifest, manifest

    def test_preflight_refuses_incomplete_coverage(self):
        with tempfile.TemporaryDirectory() as temp:
            archive, _, source = self.fixture(Path(temp))
            source["bounds"]["min_lon"] = -120
            with self.assertRaisesRegex(ValueError, "geographic bounds"):
                states.source_preflight(archive, source, [self.by_code["OR"]], None, False)
            source["bounds"]["min_lon"] = -180
            source["max_zoom"] = 8
            with self.assertRaisesRegex(ValueError, "zoom coverage"):
                states.source_preflight(archive, source, [self.by_code["OR"]], None, False)

    def test_default_is_read_only_and_one_state_builds_one_target(self):
        with tempfile.TemporaryDirectory() as temp:
            archive, source_manifest, _ = self.fixture(Path(temp))
            dest = Path(temp) / "out"
            buffer = io.StringIO()
            with mock.patch.object(states.regional, "main", return_value=0) as invoke, \
                    contextlib.redirect_stdout(buffer):
                outcome = states.main([
                    "--source-manifest", str(source_manifest),
                    "--source-archive", str(archive),
                    "--state", "OR", "--output-dir", str(dest),
                    "--builder-commit", "a" * 40,
                ])
            self.assertEqual(0, outcome)
            self.assertEqual(1, invoke.call_count)
            self.assertIn("--dry-run", invoke.call_args.args[0])
            self.assertIn("Read-only plan: 1 states, 1 target archives",
                          buffer.getvalue())
            self.assertFalse(dest.exists())

    def test_verified_local_index_does_not_claim_downloads(self):
        with tempfile.TemporaryDirectory() as temp:
            dest = Path(temp)
            state = self.by_code["OR"]
            part = state["parts"][0]
            archive = dest / (part["stem"] + ".pmtiles")
            archive.write_bytes(b"PMTiles\x03" + bytes(120))
            sha = hashlib.sha256(archive.read_bytes()).hexdigest()
            source_sha = "b" * 64
            meta = {
                "region_id": state["region_id"],
                "display_name": part["display_name"],
                "bounds": dict(zip(("min_lon", "min_lat", "max_lon", "max_lat"),
                                   part["bbox"])),
                "schema_version": "openmaptiles-3.16",
                "min_zoom": state["min_zoom"],
                "max_zoom": state["max_zoom"],
                "size_bytes": archive.stat().st_size,
                "input_hashes": {"united-states.pmtiles": source_sha},
                "output_sha256": sha,
            }
            archive.with_suffix(".manifest.json").write_text(json.dumps(meta),
                                                             encoding="utf-8")
            archive.with_suffix(".sha256").write_text(
                f"{sha}  {archive.name}\n", encoding="ascii")
            index = states.write_local_index(dest, self.catalog, source_sha)
            self.assertEqual(1, index["ready_states"])
            self.assertFalse(index["downloadable"])
            self.assertFalse(index["complete_50_states"])
            self.assertEqual("OR", index["states"][0]["code"])
            self.assertEqual("verified-local-pack-artifacts", index["kind"])
            self.assertTrue((dest / "us-states-built.json").is_file())

            archive.write_bytes(archive.read_bytes() + b"broken")
            with self.assertRaisesRegex(ValueError, "metadata|corrupt"):
                states.write_local_index(dest, self.catalog, source_sha)

    def test_resume_rejects_wrong_zoom(self):
        with tempfile.TemporaryDirectory() as temp:
            dest = Path(temp)
            state = self.by_code["OR"]
            part = state["parts"][0]
            archive = dest / (part["stem"] + ".pmtiles")
            archive.write_bytes(b"PMTiles\x03" + bytes(120))
            sha = hashlib.sha256(archive.read_bytes()).hexdigest()
            source_sha = "a" * 64
            meta = {
                "region_id": state["region_id"],
                "display_name": part["display_name"],
                "bounds": dict(zip(("min_lon", "min_lat", "max_lon", "max_lat"), part["bbox"])),
                "schema_version": "openmaptiles-3.16",
                "min_zoom": state["min_zoom"],
                "max_zoom": state["max_zoom"],
                "size_bytes": archive.stat().st_size,
                "input_hashes": {"source.pmtiles": source_sha},
                "output_sha256": sha,
            }
            archive.with_suffix(".manifest.json").write_text(json.dumps(meta))
            archive.with_suffix(".sha256").write_text(f"{sha}  {archive.name}\n")
            with self.assertRaisesRegex(ValueError, "metadata"):
                states.verify_built_pack(archive, state, part, source_sha, 13)


if __name__ == "__main__":
    unittest.main()
