#!/usr/bin/env python3
"""Build an offline-signed map catalog; never upload data or discover keys."""
from __future__ import annotations

import argparse
import base64
import datetime as dt
import hashlib
import json
import re
from pathlib import Path
import subprocess
import tempfile
from urllib.parse import urlsplit

MAX_CATALOG = 128 * 1024
MAX_MANIFEST = 64 * 1024
MAX_REVISION = 9007199254740991  # Exact through cJSON's double representation.


def decode(text: str) -> dict:
    def unique(pairs):
        value = {}
        for key, item in pairs:
            if key in value:
                raise ValueError(f"duplicate JSON key: {key}")
            value[key] = item
        return value
    return json.loads(text, object_pairs_hook=unique,
                      parse_constant=lambda value: (_ for _ in ()).throw(ValueError(value)))


def encode(value: dict) -> bytes:
    nodes = 0
    def bounded(item, depth):
        nonlocal nodes
        nodes += 1
        if nodes > 4096:
            raise ValueError("catalog exceeds 4096 JSON nodes")
        if isinstance(item, str) and "\0" in item:
            raise ValueError("catalog strings must not contain NUL")
        if isinstance(item, (dict, list)):
            if depth >= 16:
                raise ValueError("catalog nesting exceeds 16 containers")
            if isinstance(item, dict):
                if len(item) > 128 or any(not isinstance(k, str) or "\0" in k for k in item):
                    raise ValueError("invalid or oversized object keys")
                values = item.values()
            else:
                values = item
            for child in values:
                bounded(child, depth + 1)
    bounded(value, 0)
    data = (json.dumps(value, sort_keys=True, separators=(",", ":"),
                       ensure_ascii=False, allow_nan=False) + "\n").encode("utf-8")
    if len(data) > MAX_CATALOG:
        raise ValueError("catalog exceeds 128 KiB")
    return data


def read_json(path: Path, limit: int) -> dict:
    with path.open("rb") as stream:
        data = stream.read(limit + 1)
    if len(data) > limit:
        raise ValueError(f"JSON exceeds size limit: {path}")
    value = decode(data.decode("utf-8"))
    if not isinstance(value, dict):
        raise ValueError("expected JSON object")
    return value


def timestamp(value: str) -> dt.datetime:
    if not isinstance(value, str):
        raise ValueError("timestamp must be UTC text")
    try:
        result = dt.datetime.strptime(value, "%Y-%m-%dT%H:%M:%SZ")
    except ValueError as error:
        raise ValueError("timestamp must be YYYY-MM-DDTHH:MM:SSZ") from error
    if result.year < 1970 or result.strftime("%Y-%m-%dT%H:%M:%SZ") != value:
        raise ValueError("timestamp must use canonical UTC format")
    return result


def immutable_url(value: str, digest: str) -> None:
    if (not isinstance(value, str) or len(value) > 512 or
            any(ord(c) <= 32 or ord(c) >= 127 for c in value)):
        raise ValueError("invalid download URL")
    parsed = urlsplit(value)
    parts = parsed.path.split("/")
    if (parsed.scheme != "https" or not parsed.hostname or
            parsed.username is not None or parsed.password is not None or
            parsed.query or parsed.fragment or "\\" in value or
            any(part in (".", "..") for part in parts) or digest not in parts or
            not parsed.path.endswith(".pmtiles") or parsed.port not in (None, 443)):
        raise ValueError("require HTTPS archive URL with SHA-256 path segment and no credentials/query")
    if (len(parsed.hostname) > 253 or
            any(not re.fullmatch(r"[A-Za-z0-9](?:[A-Za-z0-9-]{0,61}[A-Za-z0-9])?", label)
                for label in parsed.hostname.split(".")) or
            not re.fullmatch(r"/[A-Za-z0-9._~/-]*", parsed.path)):
        raise ValueError("v1 requires a DNS host and plain ASCII archive path")


def assemble(spec: dict, root: Path, verify_directory) -> dict:
    if spec.get("schema") != "orcmaps-catalog-input-v1":
        raise ValueError("expected orcmaps-catalog-input-v1")
    if set(spec) != {"schema", "revision", "generated_at", "expires_at", "packs"}:
        raise ValueError("unknown or missing catalog input field")
    revision = spec["revision"]
    if type(revision) is not int or not 1 <= revision <= MAX_REVISION:
        raise ValueError("revision must be a positive exact integer")
    if timestamp(spec["expires_at"]) <= timestamp(spec["generated_at"]):
        raise ValueError("expiry must follow generation time")
    packs = spec["packs"]
    if not isinstance(packs, list) or not 1 <= len(packs) <= 32:
        raise ValueError("catalog requires 1–32 packs")
    result = {key: spec[key] for key in ("revision", "generated_at", "expires_at")}
    result.update(schema="orcmaps-catalog-v1", packs=[])
    checked, identities = set(), set()
    for entry in packs:
        if not isinstance(entry, dict) or set(entry) != {"manifest", "urls"}:
            raise ValueError("pack requires only manifest path and URLs")
        manifest_path = (root / entry["manifest"]).resolve()
        if not manifest_path.name.endswith(".manifest.json"):
            raise ValueError("manifest filename must end in .manifest.json")
        manifest = read_json(manifest_path, MAX_MANIFEST)
        identity = manifest.get("pack_id")
        if not isinstance(identity, str) or not identity or len(identity.encode("utf-8")) > 255:
            raise ValueError("missing or oversized pack identity")
        if identity in identities:
            raise ValueError("duplicate pack identity")
        identities.add(identity)
        size, digest = manifest.get("size_bytes"), manifest.get("output_sha256")
        if (type(size) is not int or not 1 <= size <= 0xffffffff or
                not isinstance(digest, str) or len(digest) != 64 or
                any(c not in "0123456789abcdef" for c in digest)):
            raise ValueError("manifest requires positive uint32 size and lowercase SHA-256")
        urls = entry["urls"]
        if not isinstance(urls, list) or not 1 <= len(urls) <= 4:
            raise ValueError("require 1–4 archive mirrors")
        for url in urls:
            immutable_url(url, digest)
        if len(set(urls)) != len(urls):
            raise ValueError("duplicate mirror URL")
        archive = manifest_path.with_name(manifest_path.name[:-len(".manifest.json")] + ".pmtiles")
        if archive.stat().st_size != size:
            raise ValueError("archive size differs from manifest")
        actual = hashlib.sha256()
        with archive.open("rb") as stream:
            for chunk in iter(lambda: stream.read(1024 * 1024), b""):
                actual.update(chunk)
        if actual.hexdigest() != digest:
            raise ValueError("archive SHA-256 differs from manifest")
        if manifest_path.parent not in checked:
            verify_directory(manifest_path.parent)
            checked.add(manifest_path.parent)
        result["packs"].append({"manifest": manifest,
                                "archive": {"urls": urls, "bytes": size, "sha256": digest}})
    result["packs"].sort(key=lambda entry: entry["manifest"]["pack_id"])
    encode(result)  # Enforce the device payload limit before producing anything.
    return result


def verify(payload: bytes, signature: bytes, public: Path, openssl: str) -> bool:
    try:
        raw = base64.b64decode(signature.strip(), validate=True)
    except ValueError:
        return False
    if not 8 <= len(raw) <= 80:
        return False
    with tempfile.TemporaryDirectory(prefix="orcmaps-signature-") as folder:
        root = Path(folder)
        (root / "payload").write_bytes(payload)
        (root / "signature").write_bytes(raw)
        result = subprocess.run([openssl, "dgst", "-sha256", "-verify", str(public),
                                 "-signature", str(root / "signature"), str(root / "payload")],
                                capture_output=True)
        return result.returncode == 0


def sign(payload: bytes, private: Path, public: Path, openssl: str) -> bytes:
    description = subprocess.run([openssl, "pkey", "-pubin", "-in", str(public),
                                   "-text", "-noout"], capture_output=True, check=True).stdout
    if b"ASN1 OID: prime256v1" not in description:
        raise ValueError("catalog verification key must be P-256")
    result = subprocess.run([openssl, "dgst", "-sha256", "-sign", str(private)],
                            input=payload, capture_output=True, check=True)
    signature = base64.b64encode(result.stdout) + b"\n"
    if not verify(payload, signature, public, openssl):
        raise ValueError("signature did not verify with supplied public key")
    return signature


def engine_preflight(executable: Path, directory: Path) -> None:
    result = subprocess.run([str(executable), str(directory)], check=True,
                            capture_output=True, text=True)
    if "preflight_contract: header-coverage-v1" not in result.stdout.splitlines():
        raise ValueError("rebuild pack verifier: strict header/coverage contract required")
    print(result.stdout, end="")


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("input", type=Path)
    parser.add_argument("--pack-verifier", type=Path, required=True)
    parser.add_argument("--private-key", type=Path, required=True)
    parser.add_argument("--verify-public-key", type=Path, required=True)
    parser.add_argument("--openssl", default="openssl")
    parser.add_argument("--out", type=Path, required=True, help="new output directory")
    args = parser.parse_args()
    verifier = args.pack_verifier.resolve()
    def preflight(directory):
        engine_preflight(verifier, directory)
    payload = encode(assemble(read_json(args.input, MAX_CATALOG),
                              args.input.resolve().parent, preflight))
    signature = sign(payload, args.private_key, args.verify_public_key, args.openssl)
    args.out.mkdir(parents=True, exist_ok=False)
    (args.out / "maps-catalog-v1.json").write_bytes(payload)
    (args.out / "maps-catalog-v1.sig").write_bytes(signature)
    print(f"Catalog and verified signature written to {args.out}; no uploads performed")


if __name__ == "__main__":
    main()
