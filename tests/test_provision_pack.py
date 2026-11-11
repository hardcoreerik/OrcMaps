"""Tests for the pin-to-pack provisioner used by the OrcSDR setup wizard."""

import argparse
import hashlib
import importlib.util
import json
from pathlib import Path
import tempfile
import unittest
from unittest import mock


SCRIPT = Path(__file__).parents[1] / "tools" / "pack-builder" / "provision_pack.py"
SPEC = importlib.util.spec_from_file_location("provision_pack", SCRIPT)
PROVISION = importlib.util.module_from_spec(SPEC)
assert SPEC.loader
SPEC.loader.exec_module(PROVISION)


OREGON_BOUNDS = {"min_lon": -126.3879, "min_lat": 41.9634,
                 "max_lon": -116.4545, "max_lat": 46.30349}


def oregon_manifest() -> dict:
    return {
        "bounds": dict(OREGON_BOUNDS),
        "schema_version": "openmaptiles-3.16",
        "source_snapshot": "geofabrik-oregon-2026-09-14",
        "content_profile": "standard",
        "min_zoom": 1,
        "max_zoom": 13,
        "pack_class": "open",
        "output_sha256": "27ce8cbde3d821a015cda5b3792e9f70911b1befc9f07592f2122a342b6cef3c",
        "required_attribution": ["© OpenMapTiles", "© OpenStreetMap contributors"],
        "attribution_links": ["https://openmaptiles.org/",
                              "https://www.openstreetmap.org/copyright"],
        "sources": [
            {"provenance_id": "openmaptiles", "acquired": "2026-09-14",
             "source_version": "openmaptiles schema via Planetiler"},
            {"provenance_id": "openstreetmap", "acquired": "2026-09-14",
             "source_version": "geofabrik-oregon-latest-2026-09-14"},
        ],
    }


class RadiusToBboxTests(unittest.TestCase):
    def test_box_is_centered_and_sized_from_the_radius(self):
        west, south, east, north = PROVISION.radius_to_bbox(44.0, -123.0, 50.0)
        self.assertAlmostEqual((north + south) / 2, 44.0, places=9)
        self.assertAlmostEqual((east + west) / 2, -123.0, places=9)
        # 50 km of latitude is a fixed 0.449 degrees either side.
        self.assertAlmostEqual(north - 44.0, 50.0 / PROVISION.KM_PER_DEG_LAT, places=9)
        # Longitude must be stretched by 1/cos(latitude), never equal to latitude.
        self.assertGreater(east - (-123.0), north - 44.0)

    def test_longitude_span_grows_with_latitude(self):
        _, _, equator_east, _ = PROVISION.radius_to_bbox(0.0, 0.0, 100.0)
        _, _, arctic_east, _ = PROVISION.radius_to_bbox(70.0, 0.0, 100.0)
        self.assertGreater(arctic_east, equator_east)

    def test_polar_radius_is_clamped_to_the_world(self):
        west, south, east, north = PROVISION.radius_to_bbox(84.0, 10.0, 4000.0)
        self.assertEqual((west, east), (-180.0, 180.0))
        self.assertLessEqual(north, PROVISION.builder.MERCATOR_MAX_LAT)
        self.assertGreaterEqual(south, -PROVISION.builder.MERCATOR_MAX_LAT)

    def test_latitude_is_clamped_to_the_mercator_limit(self):
        _, _, _, north = PROVISION.radius_to_bbox(85.0, 0.0, 200.0)
        self.assertLessEqual(north, PROVISION.builder.MERCATOR_MAX_LAT)

    def test_rejects_nonsense_input(self):
        for bad in ((44.0, -123.0, 0.0), (44.0, -123.0, -5.0),
                    (91.0, 0.0, 10.0), (0.0, 181.0, 10.0),
                    (float("nan"), 0.0, 10.0)):
            with self.assertRaises(ValueError):
                PROVISION.radius_to_bbox(*bad)


class SourceCoverageTests(unittest.TestCase):
    def test_pin_outside_the_source_is_rejected(self):
        self.assertTrue(PROVISION.pin_within(44.05, -123.09, OREGON_BOUNDS))
        self.assertFalse(PROVISION.pin_within(40.0, -123.09, OREGON_BOUNDS))
        self.assertFalse(PROVISION.pin_within(44.05, -100.0, OREGON_BOUNDS))

    def test_edge_pin_is_clamped_rather_than_overclaiming(self):
        # A pin near Oregon's western edge wants ocean the source lacks.
        requested = PROVISION.radius_to_bbox(44.0, -126.3, 100.0)
        clamped = PROVISION.clamp_to_source(requested, OREGON_BOUNDS)
        self.assertLess(requested[0], OREGON_BOUNDS["min_lon"])
        self.assertEqual(clamped[0], OREGON_BOUNDS["min_lon"])
        self.assertGreaterEqual(clamped[1], OREGON_BOUNDS["min_lat"])
        self.assertLessEqual(clamped[2], OREGON_BOUNDS["max_lon"])

    def test_non_overlapping_area_is_an_error(self):
        with self.assertRaises(ValueError):
            PROVISION.clamp_to_source((-100.0, 20.0, -99.0, 21.0), OREGON_BOUNDS)

    def test_zoom_range_is_narrowed_to_the_source(self):
        manifest = oregon_manifest()
        options = argparse.Namespace(min_zoom=0, max_zoom=16)
        self.assertEqual(PROVISION.zoom_range(manifest, options), (1, 13))

    def test_zoom_range_with_no_overlap_is_an_error(self):
        manifest = oregon_manifest()
        with self.assertRaises(ValueError):
            PROVISION.zoom_range(manifest, argparse.Namespace(min_zoom=14, max_zoom=16))


class ProvenanceInheritanceTests(unittest.TestCase):
    def test_primary_source_skips_the_schema_obligation_entry(self):
        # The openmaptiles entry describes the SCHEMA, not where the data came
        # from; inheriting it as primary would misreport the data's origin.
        origin = PROVISION.primary_source(oregon_manifest())
        self.assertEqual(origin["provenance_id"], "openstreetmap")

    def test_primary_source_of_a_native_schema_pack_is_its_only_source(self):
        manifest = {"schema_version": "orcmaps-overview-1",
                    "sources": [{"provenance_id": "naturalearth",
                                 "acquired": "2026-09-14",
                                 "source_version": "ne-10m"}]}
        self.assertEqual(PROVISION.primary_source(manifest)["provenance_id"],
                         "naturalearth")

    def test_derived_args_carry_the_source_identity_not_operator_input(self):
        manifest = oregon_manifest()
        options = argparse.Namespace(
            min_zoom=1, max_zoom=13, priority=20, pack_version="2026.09.15.1",
            pmtiles_cli="pmtiles", pmtiles_version="go-pmtiles 1.28.2",
            builder_commit="a" * 40, build_date="2026-09-15", force=False)
        args = PROVISION.derive_args(
            Path("oregon.pmtiles"), manifest, Path("pin.pmtiles"),
            (-123.4, 43.8, -122.7, 44.3), options, "pin", "Pin", "Pin region")
        self.assertEqual(args.source_snapshot, "geofabrik-oregon-2026-09-14")
        self.assertEqual(args.schema_version, "openmaptiles-3.16")
        self.assertEqual(args.content_profile, "standard")
        self.assertEqual(args.provenance_id, "openstreetmap")
        self.assertEqual(args.source_sha256, manifest["output_sha256"])
        self.assertEqual(args.source_label, "oregon.pmtiles")

    def test_openmaptiles_credit_survives_inheritance(self):
        # Credits are inherited AND re-applied by the builder; the result must
        # contain each required credit exactly once.
        manifest = oregon_manifest()
        options = argparse.Namespace(
            min_zoom=1, max_zoom=13, priority=20, pack_version="1",
            pmtiles_cli="pmtiles", pmtiles_version="v", builder_commit="a" * 40,
            build_date="2026-09-15", force=False)
        args = PROVISION.derive_args(
            Path("oregon.pmtiles"), manifest, Path("pin.pmtiles"),
            (-123.4, 43.8, -122.7, 44.3), options, "pin", "Pin", "Pin region")
        PROVISION.builder.apply_schema_attribution(args)
        self.assertEqual(sum("OpenMapTiles" in c for c in args.attribution), 1)
        self.assertEqual(sum("OpenStreetMap" in c for c in args.attribution), 1)


class StagingTests(unittest.TestCase):
    def test_staging_keeps_every_file_on_the_same_stem(self):
        # Discovery pairs an archive to its manifest by filename stem, so a
        # rename that touches one file and not the others breaks the pack.
        with tempfile.TemporaryDirectory() as directory:
            work = Path(directory) / "work"
            work.mkdir()
            output = work / "pin-eugene.pmtiles"
            output.write_bytes(b"archive")
            (work / "pin-eugene.manifest.json").write_text(json.dumps({
                "size_bytes": 7, "output_sha256": hashlib.sha256(b"archive").hexdigest()
            }), encoding="utf-8")
            (work / "pin-eugene.sha256").write_text("hash\n", encoding="ascii")

            card = Path(directory) / "card" / PROVISION.DEVICE_PACK_DIR
            copied = PROVISION.stage(output, card)

            names = sorted(p.name for p in copied)
            self.assertEqual(names, ["pin-eugene.manifest.json",
                                     "pin-eugene.pmtiles", "pin-eugene.sha256"])
            stems = {n.split(".")[0] for n in names}
            self.assertEqual(stems, {"pin-eugene"})
            self.assertTrue((card / "pin-eugene.pmtiles").is_file())
            self.assertEqual((card / "pin-eugene.sha256").read_text(encoding="ascii"),
                             hashlib.sha256(b"archive").hexdigest() + "  pin-eugene.pmtiles\n")

    def fixture(self, directory):
        work = Path(directory) / "work"
        work.mkdir()
        output = work / "map.pmtiles"
        output.write_bytes(b"new archive")
        output.with_suffix(".manifest.json").write_text(json.dumps({
            "size_bytes": 11,
            "output_sha256": hashlib.sha256(b"new archive").hexdigest()
        }), encoding="utf-8")
        return output, Path(directory) / "card"

    def test_existing_archive_or_manifest_is_never_overwritten(self):
        for suffix in (".pmtiles", ".manifest.json", ".sha256"):
            with self.subTest(suffix=suffix), tempfile.TemporaryDirectory() as directory:
                output, card = self.fixture(directory)
                card.mkdir()
                existing = card / ("map" + suffix)
                existing.write_bytes(b"valuable existing data")
                with self.assertRaises(FileExistsError):
                    PROVISION.stage(output, card)
                self.assertEqual(existing.read_bytes(), b"valuable existing data")
                self.assertEqual(list(card.iterdir()), [existing])

    def test_hash_mismatch_never_publishes_manifest(self):
        with tempfile.TemporaryDirectory() as directory:
            output, card = self.fixture(directory)
            output.write_bytes(b"bad archive")  # same length, wrong bytes
            with self.assertRaises(ValueError):
                PROVISION.stage(output, card)
            self.assertFalse(list(card.glob("*.manifest.json")))

    def test_copy_failure_preserves_other_packs_and_publishes_nothing(self):
        with tempfile.TemporaryDirectory() as directory:
            output, card = self.fixture(directory)
            card.mkdir()
            other = card / "old.manifest.json"
            other.write_bytes(b"existing")
            with mock.patch.object(PROVISION.shutil, "copy2", side_effect=OSError("SD removed")):
                with self.assertRaises(OSError):
                    PROVISION.stage(output, card)
            self.assertEqual(list(card.iterdir()), [other])

    def test_manifest_is_last_published_file(self):
        with tempfile.TemporaryDirectory() as directory:
            output, card = self.fixture(directory)
            published = []
            rename = Path.rename
            def record(path, target):
                published.append(Path(target).name)
                return rename(path, target)
            with mock.patch.object(Path, "rename", record):
                PROVISION.stage(output, card)
            self.assertEqual(published[-1], "map.manifest.json")

    def test_missing_manifest_is_rejected(self):
        with tempfile.TemporaryDirectory() as directory:
            output, card = self.fixture(directory)
            output.with_suffix(".manifest.json").unlink()
            with self.assertRaises(FileNotFoundError):
                PROVISION.stage(output, card)

    def test_activation_failure_cleans_only_this_attempt(self):
        with tempfile.TemporaryDirectory() as directory:
            output, card = self.fixture(directory)
            rename = Path.rename
            def fail_manifest(path, target):
                if Path(target).name.endswith(".manifest.json"):
                    raise OSError("manifest rename failed")
                return rename(path, target)
            with mock.patch.object(Path, "rename", fail_manifest):
                with self.assertRaises(OSError):
                    PROVISION.stage(output, card)
            self.assertEqual(list(card.iterdir()), [])

    def test_copy_corruption_is_detected_before_publication(self):
        with tempfile.TemporaryDirectory() as directory:
            output, card = self.fixture(directory)
            def corrupt(source, target):
                Path(target).write_bytes(b"bad archive")
            with mock.patch.object(PROVISION.shutil, "copy2", corrupt):
                with self.assertRaises(ValueError):
                    PROVISION.stage(output, card)
            self.assertEqual(list(card.iterdir()), [])

    def test_provisioner_cannot_build_into_the_live_pack_directory(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            source = root / "source.manifest.json"
            source.write_text(json.dumps(oregon_manifest()), encoding="utf-8")
            source.with_name("source.pmtiles").write_bytes(b"archive")
            with self.assertRaisesRegex(ValueError, "build outside"):
                PROVISION.main([
                    "--source-manifest", str(source), "--lat", "44", "--lon", "-123",
                    "--radius-km", "50", "--name", "home", "--sd-root", str(root),
                    "--work-dir", str(root / PROVISION.DEVICE_PACK_DIR),
                    "--builder-commit", "test",
                ])
            self.assertFalse((root / PROVISION.DEVICE_PACK_DIR).exists())

    def test_manifest_loader_rejects_an_incomplete_manifest(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "broken.manifest.json"
            path.write_text(json.dumps({"bounds": OREGON_BOUNDS}), encoding="utf-8")
            with self.assertRaises(ValueError):
                PROVISION.load_source_manifest(path)


if __name__ == "__main__":
    unittest.main()
