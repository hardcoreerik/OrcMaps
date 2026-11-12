#!/usr/bin/env python3
"""Build 50 US state OrcMaps PMTiles packs from one verified local source pack.

This is a PC-side production tool, not a network downloader or device runtime.
Default mode is a read-only plan. Use --execute to build real archives.
"""

from __future__ import annotations

import argparse
import datetime as dt
import json
import os
from pathlib import Path
import re
import subprocess
import sys

import build_regional_pack as regional
import provision_pack as provision

ROOT = Path(__file__).resolve().parents[2]
CATALOG = ROOT / "data" / "catalog" / "us-states.json"
SUFFIXES = (".pmtiles", ".manifest.json", ".sha256")
EXPECTED_STATE_CODES = frozenset(
    "AL AK AZ AR CA CO CT DE FL GA HI ID IL IN IA KS KY LA ME MD MA MI "
    "MN MS MO MT NE NV NH NJ NM NY NC ND OH OK OR PA RI SC SD TN TX "
    "UT VT VA WA WV WI WY".split()
)



def load_catalog(path: Path = CATALOG) -> list[dict]:
    document = json.loads(path.read_text(encoding="utf-8"))
    if document.get("catalog_version") != 1 or document.get("catalog_kind") != "build-targets-not-downloads":
        raise ValueError("unsupported US state build catalog")
    states = document["states"]
    codes = [state["code"] for state in states]
    if len(states) != 50 or set(codes) != EXPECTED_STATE_CODES:
        raise ValueError("state catalog must have 50 distinct states")
    stems = set()
    for state in states:
        if not re.fullmatch(r"[A-Z]{2}", state["code"]) or state["region_id"] != "US-" + state["code"]:
            raise ValueError("invalid state code/region_id")
        if not 0 <= state["min_zoom"] <= state["max_zoom"] <= 31:
            raise ValueError("invalid state zoom range")
        if not state["parts"]:
            raise ValueError("every state needs at least one geographic part")
        for part in state["parts"]:
            stem = part["stem"]
            bbox = part["bbox"]
            if not re.fullmatch(r"us-[a-z0-9-]+", stem) or stem in stems:
                raise ValueError("duplicate or unsafe state pack stem")
            stems.add(stem)
            if (len(bbox) != 4 or not all(isinstance(v, (int, float)) for v in bbox)
                    or not (-180 <= bbox[0] < bbox[2] <= 180)
                    or not (-regional.MERCATOR_MAX_LAT <= bbox[1] < bbox[3] <= regional.MERCATOR_MAX_LAT)):
                raise ValueError("invalid state pack bounds")
    if len(stems) != 51:
        raise ValueError("expected 51 geographic parts for 50 states (Alaska split)")
    return states


def selected_states(states: list[dict], selectors: list[str]) -> list[dict]:
    if not selectors:
        return states
    matched = set()
    for selector in selectors:
        lookup = selector.strip().upper().removeprefix("US-")
        found = [state for state in states
                 if lookup in (state["code"], state["name"].upper())]
        if len(found) != 1 or found[0]["code"] in matched:
            raise ValueError(f"unknown or duplicate --state selector: {selector}")
        matched.add(found[0]["code"])
    return [state for state in states if state["code"] in matched]

def header_zooms(archive: Path) -> tuple[int, int]:
    """Return (min_zoom, max_zoom) from a PMTiles v3 header (bytes 100 and 101)."""
    with archive.open("rb") as stream:
        header = stream.read(127)
    if len(header) < 127 or header[:8] != b"PMTiles":
        raise ValueError(f"not a PMTiles v3 archive: {archive.name}")
    return header[100], header[101]


def source_preflight(archive: Path, manifest: dict, states: list[dict],
                     zoom_override: int | None, verify_hash: bool) -> None:
    if not archive.is_file():
        raise FileNotFoundError(f"source PMTiles archive not found: {archive}")
    if manifest["schema_version"] != "openmaptiles-3.16" or manifest.get("pmtiles_version") != 3:
        raise ValueError("state builds require a supported OpenMapTiles 3.16 PMTiles v3 source")
    if archive.stat().st_size != manifest.get("size_bytes"):
        raise ValueError("source archive size differs from manifest")
    header_min, header_max = header_zooms(archive)
    sb = manifest["bounds"]
    for state in states:
        zmax = state["max_zoom"] if zoom_override is None else zoom_override
        if not 0 <= state["min_zoom"] <= zmax <= 31:
            raise ValueError("invalid zoom override")
        if state["min_zoom"] < manifest["min_zoom"] or zmax > manifest["max_zoom"]:
            raise ValueError(f"{state['code']}: source has insufficient zoom coverage")
        if state["min_zoom"] < header_min or zmax > header_max:
            raise ValueError(f"{state['code']}: source archive header has insufficient zoom coverage")
        for part in state["parts"]:
            w, s, e, n = part["bbox"]
            if (w < sb["min_lon"] or s < sb["min_lat"] or
                    e > sb["max_lon"] or n > sb["max_lat"]):
                raise ValueError(f"{state['code']} {part['stem']}: source lacks full geographic bounds")
    if verify_hash:
        expected = manifest["output_sha256"].lower()
        if not re.fullmatch(r"[0-9a-f]{64}", expected):
            raise ValueError("source manifest has no usable SHA-256")
        if regional.sha256(archive) != expected:
            raise ValueError("source archive SHA-256 differs from its manifest")


def triplet(archive: Path) -> list[Path]:
    return [archive.with_suffix(suffix) for suffix in SUFFIXES]


def verify_built_pack(archive: Path, state: dict, part: dict,
                      source_hash: str | None, expected_max_zoom: int | None = None) -> dict:
    files = triplet(archive)
    if not all(path.is_file() for path in files):
        raise ValueError(f"incomplete archive/manifest/checksum triplet: {archive.name}")
    with archive.open("rb") as stream:
        if archive.stat().st_size < 127 or stream.read(8) != b"PMTiles\x03":
            raise ValueError(f"not a PMTiles v3 archive: {archive.name}")
    manifest = json.loads(files[1].read_text(encoding="utf-8"))
    header_min, header_max = header_zooms(archive)
    if (header_min != manifest.get("min_zoom")
            or header_max < manifest.get("max_zoom", 32)):
        raise ValueError(f"pack header zoom range does not match its manifest: {archive.name}")
    expected_bounds = dict(zip(("min_lon", "min_lat", "max_lon", "max_lat"), part["bbox"]))
    if (manifest.get("region_id") != state["region_id"]
            or manifest.get("bounds") != expected_bounds
            or manifest.get("schema_version") != "openmaptiles-3.16"
            or manifest.get("min_zoom") != state["min_zoom"]
            or not (state["min_zoom"] <= manifest.get("max_zoom", -1) <= 31)
            or (expected_max_zoom is not None and manifest.get("max_zoom") != expected_max_zoom)
            or manifest.get("size_bytes") != archive.stat().st_size
            or (source_hash is not None and source_hash.lower() not in
                [str(v).lower() for v in manifest.get("input_hashes", {}).values()])):
        raise ValueError(f"pack metadata does not match expected build target: {archive.name}")
    digest = regional.sha256(archive)
    tokens = files[2].read_text(encoding="ascii").split()
    if (manifest.get("output_sha256") != digest or len(tokens) != 2
            or tokens[0].lower() != digest or tokens[1] != archive.name):
        raise ValueError(f"archive or checksum sidecar is corrupt: {archive.name}")
    return {
        "stem": part["stem"],
        "region_id": state["region_id"],
        "display_name": manifest["display_name"],
        "bounds": expected_bounds,
        "min_zoom": manifest["min_zoom"],
        "max_zoom": manifest["max_zoom"],
        "bytes": archive.stat().st_size,
        "sha256": digest,
        "files": [path.name for path in files],
    }


def make_builder_args(source_archive: Path, source: dict, state: dict,
                      part: dict, args: argparse.Namespace, commit: str) -> list[str]:
    origin = provision.primary_source(source)
    archive = args.output_dir / (part["stem"] + ".pmtiles")
    zmax = state["max_zoom"] if args.max_zoom is None else args.max_zoom
    command = [
        "--source", str(source_archive),
        "--bbox=" + ",".join(str(v) for v in part["bbox"]),
        "--min-zoom", str(state["min_zoom"]), "--max-zoom", str(zmax),
        "--content-profile", source["content_profile"], "--output", str(archive),
        "--region-id", state["region_id"],
        "--region-name", state["name"] + ", USA",
        "--display-name", part["display_name"],
        "--source-snapshot", source["source_snapshot"],
        "--source-version", origin["source_version"],
        "--source-label", source_archive.name,
        "--source-sha256", source["output_sha256"],
        "--provenance-id", origin["provenance_id"],
        "--acquired", origin["acquired"],
        "--pack-version", args.pack_version, "--pack-class", source["pack_class"],
        "--priority", str(args.priority),
        "--schema-version", source["schema_version"],
        "--pmtiles-cli", args.pmtiles_cli,
        "--builder", "orcmaps-us-states / go-pmtiles extract",
        "--builder-version", args.pmtiles_version,
        "--builder-commit", commit, "--build-date", args.build_date,
    ]
    for credit in source.get("required_attribution", []):
        command.extend(["--attribution", credit])
    for link in source.get("attribution_links", []):
        command.extend(["--attribution-link", link])
    if args.force:
        command.append("--force")
    if not args.execute:
        command.append("--dry-run")
    return command


def resolve_commit(value: str | None, execute: bool) -> str:
    if value:
        if not re.fullmatch(r"[0-9a-fA-F]{40}", value):
            raise ValueError("--builder-commit must be 40 hexadecimal characters")
        return value.lower()
    probe = subprocess.run(["git", "-C", str(ROOT), "rev-parse", "HEAD"],
                           capture_output=True, text=True, check=False)
    commit = probe.stdout.strip()
    if re.fullmatch(r"[0-9a-fA-F]{40}", commit):
        return commit.lower()
    if execute:
        raise ValueError("cannot determine builder commit; pass --builder-commit")
    return "0" * 40


def write_local_index(out: Path, states: list[dict], source_hash: str | None = None) -> dict:
    """Only include fully verified states; never claim a remote URL exists."""
    entries = []
    incomplete = []
    for state in states:
        parts = []
        for part in state["parts"]:
            archive = out / (part["stem"] + ".pmtiles")
            present = [path.exists() for path in triplet(archive)]
            if not all(present):
                if any(present):
                    incomplete.append(part["stem"])
                parts = []
                break
            parts.append(verify_built_pack(archive, state, part, source_hash))
        if len(parts) == len(state["parts"]):
            entries.append({"code": state["code"], "name": state["name"],
                            "region_id": state["region_id"], "parts": parts})
    index = {
        "index_version": 1,
        "kind": "verified-local-pack-artifacts",
        "downloadable": False,
        "notes": "Local artifact inventory only. Upload/verify archives before publishing URLs or advertising downloads.",
        "ready_states": len(entries),
        "incomplete_parts": incomplete,
        "complete_50_states": len(entries) == 50,
        "states": entries,
    }
    destination = out / "us-states-built.json"
    destination.parent.mkdir(parents=True, exist_ok=True)
    partial = destination.with_suffix(".partial.json")
    partial.write_text(json.dumps(index, indent=2, ensure_ascii=False) + "\n",
                       encoding="utf-8")
    os.replace(partial, destination)
    return index


def parser() -> argparse.ArgumentParser:
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("--source-manifest", type=Path, required=True,
                   help="Existing verified source pack .manifest.json")
    p.add_argument("--source-archive", type=Path,
                   help="PMTiles source; inferred from manifest stem when omitted")
    p.add_argument("--output-dir", type=Path, default=ROOT / "data/local/us-states")
    p.add_argument("--state", action="append", default=[], help="State name, code, or US-code; repeat as needed")
    p.add_argument("--max-zoom", type=int, help="Optional max zoom override (may increase storage)")
    p.add_argument("--pack-version", default=dt.date.today().strftime("%Y.%m.%d.1"))
    p.add_argument("--pmtiles-cli", default="pmtiles")
    p.add_argument("--pmtiles-version", default="go-pmtiles 1.28.2")
    p.add_argument("--builder-commit", help="40-character OrcMaps commit; auto-detected in a git clone")
    p.add_argument("--build-date", default=dt.date.today().isoformat())
    p.add_argument("--priority", type=int, default=10,
                   help="Regional priority (keep below local packs)")
    p.add_argument("--execute", action="store_true", help="Build real archives; default only plans")
    p.add_argument("--resume", action="store_true", help="Skip existing fully verified triplets")
    p.add_argument("--force", action="store_true", help="Explicitly overwrite existing artifacts")
    return p


def main(argv: list[str] | None = None) -> int:
    args = parser().parse_args(argv)
    if args.force and args.resume:
        raise ValueError("--force and --resume cannot be combined")
    states = load_catalog()
    wanted = selected_states(states, args.state)
    source_path = args.source_manifest
    source = provision.load_source_manifest(source_path)
    archive = args.source_archive or source_path.with_name(
        source_path.name.removesuffix(".manifest.json") + ".pmtiles")
    source_preflight(archive, source, wanted, args.max_zoom, args.execute)
    commit = resolve_commit(args.builder_commit, args.execute)

    planned = [(state, part) for state in wanted for part in state["parts"]]
    if args.execute and not args.force:
        for state, part in planned:
            output = args.output_dir / (part["stem"] + ".pmtiles")
            if any(path.exists() for path in triplet(output)):
                if args.resume:
                    verify_built_pack(output, state, part, source["output_sha256"],
                                      state["max_zoom"] if args.max_zoom is None else args.max_zoom)
                else:
                    raise FileExistsError(f"existing pack: {output}; use --resume or --force")

    for state, part in planned:
        output = args.output_dir / (part["stem"] + ".pmtiles")
        if args.execute and args.resume and all(path.exists() for path in triplet(output)):
            print(f"verified and skipped: {output}")
            continue
        command = make_builder_args(archive, source, state, part, args, commit)
        if regional.main(command) != 0:
            raise ValueError(f"pack extraction failed: {part['stem']}")
        if args.execute:
            verify_built_pack(output, state, part, source["output_sha256"])

    if args.execute:
        index = write_local_index(args.output_dir, states)
        print(json.dumps({"built_targets": len(planned),
                          "verified_states_in_output": index["ready_states"],
                          "complete_50_states": index["complete_50_states"],
                          "local_index": str(args.output_dir / "us-states-built.json")},
                         indent=2))
    else:
        print(f"Read-only plan: {len(wanted)} states, {len(planned)} target archives; "
              "nothing downloaded or generated. Add --execute to build.")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (OSError, ValueError, KeyError, TypeError, json.JSONDecodeError,
            subprocess.CalledProcessError) as exc:
        print(f"error: {exc}", file=sys.stderr)
        raise SystemExit(2)
