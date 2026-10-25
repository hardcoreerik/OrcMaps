#!/usr/bin/env python3
"""Provision an OrcMaps pack for a dropped pin, ready to copy to a device SD card.

This is the PC half of the OrcSDR setup wizard. The wizard gives a latitude,
a longitude and a radius; this produces `<name>.pmtiles` plus its sidecars in
a staging directory laid out exactly as the device expects.

It deliberately does NOT take the ~25 provenance flags that
build_regional_pack.py requires. Every one of them is derived from the SOURCE
pack's own manifest, because a pack cut from another pack inherits that pack's
provenance -- asking an operator to retype it is how a wrong source snapshot
or a missing credit gets recorded. The cut is performed by importing
build_regional_pack, not by re-implementing it, so the schema-attribution and
pack-identity rules cannot drift between the two tools.
"""

from __future__ import annotations

import argparse
import datetime as dt
import importlib.util
import json
import math
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile

BUILDER_PATH = Path(__file__).with_name("build_regional_pack.py")
_SPEC = importlib.util.spec_from_file_location("orcmaps_regional_pack_builder",
                                               BUILDER_PATH)
builder = importlib.util.module_from_spec(_SPEC)
assert _SPEC.loader is not None
_SPEC.loader.exec_module(builder)

# The device contract: one flat directory, archive paired to manifest by
# filename stem. Mirrors kManifestSuffix/kArchiveSuffix and
# ArchivePathForManifest in src/core/pack_discovery.cpp -- if those change,
# this must change with them.
DEVICE_PACK_DIR = "orcmaps"
MANIFEST_SUFFIX = ".manifest.json"
ARCHIVE_SUFFIX = ".pmtiles"

# Mean-Earth degree lengths. A pack boundary is a coarse budgeting decision,
# not a survey: the error from ignoring the ellipsoid is well under a tile at
# the zooms packs are cut for, and erring large is harmless here because the
# bbox is clamped to the source's own coverage anyway.
KM_PER_DEG_LAT = 111.32


def radius_to_bbox(lat_deg: float, lon_deg: float,
                   radius_km: float) -> tuple[float, float, float, float]:
    """Return west,south,east,north covering `radius_km` around the pin.

    The longitude span grows as 1/cos(latitude), so a fixed radius near the
    poles can exceed the whole globe. That is clamped to a full world span
    rather than allowed to produce nonsense bounds.
    """
    if not all(math.isfinite(v) for v in (lat_deg, lon_deg, radius_km)):
        raise ValueError("pin and radius must be finite")
    if radius_km <= 0:
        raise ValueError("radius must be positive")
    if not -90 <= lat_deg <= 90:
        raise ValueError("latitude must be within +/-90 degrees")
    if not -180 <= lon_deg <= 180:
        raise ValueError("longitude must be within +/-180 degrees")

    d_lat = radius_km / KM_PER_DEG_LAT
    # Use the widest latitude the box will reach, so the corners are covered
    # rather than just the pin's own parallel.
    widest = min(abs(lat_deg) + d_lat, 89.9)
    cos_widest = math.cos(math.radians(widest))
    d_lon = 180.0 if cos_widest <= 1e-9 else radius_km / (KM_PER_DEG_LAT * cos_widest)

    max_lat = builder.MERCATOR_MAX_LAT
    south = max(lat_deg - d_lat, -max_lat)
    north = min(lat_deg + d_lat, max_lat)
    # Once the span reaches the whole globe, say so. Clamping each edge
    # independently instead would shift the box toward the pin -- a pin at
    # lon 10 would yield -170..180, a 350-degree box missing a wedge behind
    # the antimeridian rather than the full world it asked for.
    if d_lon >= 180.0:
        return (-180.0, south, 180.0, north)
    return (max(lon_deg - d_lon, -180.0), south,
            min(lon_deg + d_lon, 180.0), north)


def clamp_to_source(bbox: tuple[float, float, float, float],
                    source_bounds: dict) -> tuple[float, float, float, float]:
    """Intersect the requested box with the source pack's coverage.

    Without this a pin near the edge of a regional source yields a pack whose
    manifest claims coverage the archive does not hold, and the device's
    manifest-vs-archive bounds check rejects it.
    """
    west, south, east, north = bbox
    clamped = (max(west, float(source_bounds["min_lon"])),
               max(south, float(source_bounds["min_lat"])),
               min(east, float(source_bounds["max_lon"])),
               min(north, float(source_bounds["max_lat"])))
    if clamped[0] >= clamped[2] or clamped[1] >= clamped[3]:
        raise ValueError("requested area does not overlap the source pack")
    return clamped


def pin_within(lat_deg: float, lon_deg: float, source_bounds: dict) -> bool:
    return (float(source_bounds["min_lat"]) <= lat_deg <= float(source_bounds["max_lat"])
            and float(source_bounds["min_lon"]) <= lon_deg <= float(source_bounds["max_lon"]))


def load_source_manifest(path: Path) -> dict:
    manifest = json.loads(path.read_text(encoding="utf-8"))
    for key in ("bounds", "schema_version", "source_snapshot", "sources",
                "min_zoom", "max_zoom", "content_profile", "output_sha256"):
        if key not in manifest:
            raise ValueError(f"source manifest is missing '{key}': {path}")
    if not manifest["sources"]:
        raise ValueError(f"source manifest records no sources: {path}")
    return manifest


def primary_source(manifest: dict) -> dict:
    """Pick the data source, not a schema-obligation entry.

    A manifest built with the OpenMapTiles schema carries an `openmaptiles`
    entry alongside the real data source. Inheriting that as the primary
    provenance would misreport where the map data came from.
    """
    schema_need = builder.schema_obligation(manifest["schema_version"])
    schema_id = schema_need["provenance_id"] if schema_need else None
    for source in manifest["sources"]:
        if source.get("provenance_id") != schema_id:
            return source
    return manifest["sources"][0]


def derive_args(source_archive: Path, source_manifest: dict, output: Path,
                bbox: tuple[float, float, float, float], options: argparse.Namespace,
                region_id: str, display_name: str,
                region_name: str) -> argparse.Namespace:
    """Build the full build_regional_pack argument set from the source pack."""
    origin = primary_source(source_manifest)
    return argparse.Namespace(
        source=str(source_archive),
        bbox=bbox,
        geojson=None,
        min_zoom=options.min_zoom,
        max_zoom=options.max_zoom,
        content_profile=source_manifest["content_profile"],
        output=output,
        region_id=region_id,
        region_name=region_name,
        display_name=display_name,
        source_snapshot=source_manifest["source_snapshot"],
        source_version=origin["source_version"],
        source_label=source_archive.name,
        source_sha256=source_manifest["output_sha256"],
        provenance_id=origin["provenance_id"],
        acquired=origin["acquired"],
        pack_version=options.pack_version,
        pack_class=source_manifest.get("pack_class", "open"),
        # Credits are inherited verbatim; the schema obligation is then
        # re-applied by the builder, which is idempotent.
        attribution=list(source_manifest.get("required_attribution", [])),
        attribution_link=list(source_manifest.get("attribution_links", [])),
        priority=options.priority,
        schema_version=source_manifest["schema_version"],
        pmtiles_cli=options.pmtiles_cli,
        builder="orcmaps-provision-pack / go-pmtiles extract",
        builder_version=options.pmtiles_version,
        builder_commit=options.builder_commit,
        build_date=options.build_date,
        force=options.force,
        existing_output=False,
        dry_run=False,
    )


def zoom_range(source_manifest: dict, options: argparse.Namespace) -> tuple[int, int]:
    """Clamp the requested zooms to what the source actually holds.

    Asking for z14 from a z1-13 source silently yields nothing at z14 while
    the manifest claims it, so the range is narrowed rather than trusted.
    """
    src_min = int(source_manifest["min_zoom"])
    src_max = int(source_manifest["max_zoom"])
    low = max(options.min_zoom, src_min)
    high = min(options.max_zoom, src_max)
    if low > high:
        raise ValueError(
            f"requested z{options.min_zoom}-{options.max_zoom} does not overlap "
            f"the source's z{src_min}-{src_max}")
    return low, high


def stage(output: Path, destination: Path) -> list[Path]:
    """Verify copied bytes, then publish the manifest last, without replacement.

    Requires exclusive host access to the unmounted device card. The manifest
    is discovery's commit marker; an interrupted copy must never expose it.
    """
    if output.suffix != ARCHIVE_SUFFIX:
        raise ValueError("archive must end in .pmtiles")
    manifest_path = output.with_suffix(MANIFEST_SUFFIX)
    manifest = json.loads(manifest_path.read_text(encoding="utf-8"))
    expected_size = manifest.get("size_bytes")
    expected_hash = manifest.get("output_sha256", "")
    if (type(expected_size) is not int or expected_size <= 0 or
            not isinstance(expected_hash, str) or len(expected_hash) != 64 or
            any(c not in "0123456789abcdefABCDEF" for c in expected_hash)):
        raise ValueError("manifest requires archive size and SHA-256")
    if output.stat().st_size != expected_size:
        raise ValueError("archive size differs from manifest")
    destination.mkdir(parents=True, exist_ok=True)
    targets = [destination / output.with_suffix(suffix).name
               for suffix in (ARCHIVE_SUFFIX, ".sha256", MANIFEST_SUFFIX)]
    # A per-name reservation prevents concurrent provisioner invocations.
    lock = destination / (".install-" + output.stem)
    lock.mkdir()
    published: list[Path] = []
    try:
        for target in targets:
            if target.exists() or target.is_symlink():
                raise FileExistsError(f"refusing to replace existing pack file: {target}")
        with tempfile.TemporaryDirectory(prefix=".stage-", dir=destination) as folder:
            staging = Path(folder)
            archive = staging / output.name
            shutil.copy2(output, archive)
            if (archive.stat().st_size != expected_size or
                    builder.sha256(archive) != expected_hash.lower()):
                raise ValueError("copied archive SHA-256 differs from manifest")
            # Bind the checksum to the actual copied filename; an optional
            # source .sha256 is not trusted as a second source of identity.
            (staging / targets[1].name).write_text(
                f"{expected_hash.lower()}  {output.name}\n", encoding="ascii")
            (staging / targets[2].name).write_text(
                json.dumps(manifest, indent=2, ensure_ascii=False) + "\n",
                encoding="utf-8")
            for target in targets:
                pending = staging / target.name
                with pending.open("r+b") as handle:
                    os.fsync(handle.fileno())
                if target.exists() or target.is_symlink():
                    raise FileExistsError(f"pack file appeared during staging: {target}")
                pending.rename(target)
                published.append(target)
        return published
    except BaseException:
        # Only this attempt's new files are removed. Existing packs are never
        # renamed, truncated or used as rollback scratch space.
        for target in reversed(published):
            target.unlink(missing_ok=True)
        raise
    finally:
        lock.rmdir()


def parser() -> argparse.ArgumentParser:
    p = argparse.ArgumentParser(description=__doc__,
                                formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument("--source-manifest", type=Path, required=True,
                   help="Manifest of the staged pack to cut from")
    p.add_argument("--source-archive", type=Path,
                   help="Archive to cut (default: the manifest's own stem)")
    p.add_argument("--lat", type=float, required=True, help="Pin latitude in degrees")
    p.add_argument("--lon", type=float, required=True, help="Pin longitude in degrees")
    p.add_argument("--radius-km", type=float, required=True,
                   help="Half-width of the coverage box around the pin")
    p.add_argument("--name", required=True,
                   help="Pack name; becomes <name>.pmtiles on the card")
    p.add_argument("--display-name", help="Human label shown by the device")
    p.add_argument("--region-name", help="Descriptive region text for the manifest")
    p.add_argument("--work-dir", type=Path,
                   help="Where the pack is built (default: beside the staging dir)")
    p.add_argument("--sd-root", type=Path,
                   help=f"Card root; the pack is written to <root>/{DEVICE_PACK_DIR}/")
    p.add_argument("--min-zoom", type=int, default=1)
    p.add_argument("--max-zoom", type=int, default=13)
    p.add_argument("--priority", type=int, default=20)
    p.add_argument("--pack-version", default=dt.date.today().strftime("%Y.%m.%d.1"))
    p.add_argument("--pmtiles-cli", default="pmtiles")
    p.add_argument("--pmtiles-version", default="go-pmtiles 1.28.2")
    p.add_argument("--builder-commit", required=True,
                   help="40-hex commit of the OrcMaps tree doing the build")
    p.add_argument("--build-date", default=dt.date.today().isoformat())
    p.add_argument("--force", action="store_true",
                   help="Replace an existing pack of the same name")
    p.add_argument("--dry-run", action="store_true",
                   help="Report the plan without cutting anything")
    return p


def main(argv: list[str] | None = None) -> int:
    options = parser().parse_args(argv)
    if not builder.IDENTITY_PART.fullmatch(options.name):
        raise ValueError("name allows only letters, digits, period, and hyphen")

    manifest_path = options.source_manifest
    source_manifest = load_source_manifest(manifest_path)
    source_archive = options.source_archive
    if source_archive is None:
        stem = manifest_path.name[: -len(MANIFEST_SUFFIX)] \
            if manifest_path.name.endswith(MANIFEST_SUFFIX) else manifest_path.stem
        source_archive = manifest_path.with_name(stem + ARCHIVE_SUFFIX)
    if not source_archive.is_file():
        raise FileNotFoundError(f"source archive not found: {source_archive}")

    bounds = source_manifest["bounds"]
    if not pin_within(options.lat, options.lon, bounds):
        raise ValueError(
            f"pin {options.lat:.5f},{options.lon:.5f} is outside the source pack "
            f"({bounds['min_lat']:.5f}..{bounds['max_lat']:.5f}, "
            f"{bounds['min_lon']:.5f}..{bounds['max_lon']:.5f})")

    requested = radius_to_bbox(options.lat, options.lon, options.radius_km)
    bbox = clamp_to_source(requested, bounds)
    low, high = zoom_range(source_manifest, options)
    options.min_zoom, options.max_zoom = low, high

    sd_dir = (options.sd_root / DEVICE_PACK_DIR) if options.sd_root else None
    work_dir = options.work_dir or Path.cwd()
    if sd_dir is not None and work_dir.resolve() == sd_dir.resolve():
        raise ValueError("build outside the device pack directory, then stage")
    output = work_dir / f"{options.name}{ARCHIVE_SUFFIX}"

    display_name = options.display_name or options.name
    region_name = options.region_name or (
        f"{options.radius_km:g} km around {options.lat:.5f}, {options.lon:.5f}")

    build_args = derive_args(source_archive, source_manifest, output, bbox,
                             options, options.name, display_name, region_name)

    plan = {
        "pin": {"lat": options.lat, "lon": options.lon,
                "radius_km": options.radius_km},
        "requested_bbox": list(requested),
        "bbox": list(bbox),
        "clamped_to_source": list(bbox) != list(requested),
        "zoom": {"min": low, "max": high},
        "pack_id": builder.pack_id(build_args, bbox),
        "output": str(output),
        "staged_to": str(sd_dir) if sd_dir else None,
        "schema_version": source_manifest["schema_version"],
        "attribution": build_args.attribution,
    }
    if options.dry_run:
        print(json.dumps(plan, indent=2, ensure_ascii=False))
        return 0

    work_dir.mkdir(parents=True, exist_ok=True)
    builder.validate_provenance(build_args)
    command = [options.pmtiles_cli, "extract", str(source_archive), str(output),
               f"--minzoom={low}", f"--maxzoom={high}",
               f"--bbox={','.join(format(v, '.15g') for v in bbox)}"]

    manifest_out = output.with_suffix(MANIFEST_SUFFIX)
    if (output.exists() or manifest_out.exists()) and not options.force:
        raise FileExistsError(f"refusing to replace immutable pack: {output}")

    partial = output.with_name(output.stem + ".partial.pmtiles")
    partial.unlink(missing_ok=True)
    command[3] = str(partial)
    try:
        # The CLI prints progress to stdout. This tool's stdout is a JSON
        # result a wizard parses, so the child's chatter is relayed to stderr
        # where it stays visible to an operator without corrupting the result.
        subprocess.run(command, check=True, stdout=sys.stderr)
        partial.replace(output)
        builder.write_sidecars(build_args, bbox)
    finally:
        partial.unlink(missing_ok=True)

    plan["size_bytes"] = output.stat().st_size
    plan["attribution"] = build_args.attribution
    # Always report the device-visible file set, whether the pack was built
    # straight into the card directory or copied there afterwards. A caller
    # should not have to infer which happened to find out what it can install.
    if sd_dir is not None and sd_dir.resolve() != work_dir.resolve():
        staged = stage(output, sd_dir)
    else:
        staged = [p for p in (output, output.with_suffix(MANIFEST_SUFFIX),
                              output.with_suffix(".sha256")) if p.is_file()]
    plan["staged_files"] = [str(p) for p in staged]
    print(json.dumps(plan, indent=2, ensure_ascii=False))
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (OSError, ValueError, subprocess.CalledProcessError) as exc:
        print(f"error: {exc}", file=sys.stderr)
        raise SystemExit(2)
