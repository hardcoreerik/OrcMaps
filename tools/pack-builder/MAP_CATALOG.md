# Owned map catalog v1

`build_map_catalog.py` prepares signed metadata for already-built map packs.
It does not upload data, make a release, copy archives, locate private keys,
or enable device downloads. It is separate from OrcSDR's historical data
catalog, which contains ORCMAP1 and other radio databases.

Input example (replace the hash, paths and URL with verified release values):

```json
{
  "schema": "orcmaps-catalog-input-v1",
  "revision": 1,
  "generated_at": "2026-10-19T00:00:00Z",
  "expires_at": "2026-11-19T00:00:00Z",
  "packs": [{
    "manifest": "world.manifest.json",
    "urls": ["https://maps.example.test/FULL_LOWERCASE_SHA256/world.pmtiles"]
  }]
}
```

Relative manifest paths resolve beside the input file. Each archive sits
beside its manifest under the matching stem. Each mirror must use HTTPS,
include the archive's actual SHA-256 as a complete path segment, and omit
credentials, query strings and fragments. Four mirrors and 32 packs are the
limits. URL naming alone cannot guarantee that a server never changes data;
the download's expected hash remains mandatory.
Version 1 uses DNS hostnames (or IPv4 text), port 443, and plain ASCII paths
containing letters, digits, `/`, `-`, `.`, `_`, `~`. Percent-encoded paths and
IPv6 literals are outside this initial contract. UTC dates start at 1970.

Build the current `tools/pack-verify` executable first. The builder requires
its `header-coverage-v1` preflight contract, refusing older warning-only
verifiers. Each source directory must contain only compatible packs; all its
manifests are checked. Archive sizes and full SHA-256 values are independently
verified before signing. Pack manifest validation, schema/profile support and
header/root-directory parsing remain implemented by the actual engine tool.
This does not render or exhaustively decode every tile.

```powershell
python tools/pack-builder/build_map_catalog.py catalog-input.json `
  --pack-verifier build-pack-verify/Debug/orcmap_pack_verify.exe `
  --private-key X:/offline/map-signing.pem `
  --verify-public-key map-signing-public.pem `
  --out new-catalog-directory
```

`--openssl` may name an explicit OpenSSL executable. Signing requires an
operator-supplied P-256 private key and matching public key; neither is
discovered automatically or embedded in output. The output directory must
not already exist. Keep private material offline and outside the repository.

The signed UTF-8 payload is compact JSON, sorted keys, no nonfinite numbers,
and one trailing newline. The detached `.sig` is base64 DER ECDSA/SHA-256 plus
a newline, matching the existing project's signature approach. The signature
is checked with the supplied public key before output is written. ECDSA
signatures need not be identical across repeated signing runs; payloads are
deterministic for the same input/manifests.

Output schema `orcmaps-catalog-v1` carries revision, UTC generation/expiry and
entries containing the complete pack manifest plus an `archive` object with
`urls`, `bytes` and `sha256`. No remote entry chooses an SD destination path.
The manifest retains bounds, detail/zoom, source/build identity and credits.
Payloads are bounded to 128 KiB; revision is a positive integer no greater
than 2^53−1, allowing exact interpretation by the consumer's JSON library.
The shared device limits are 4096 JSON value nodes, at most 128 members per
object, and 16 nested containers. NUL characters are refused in strings/keys.
The builder enforces these before signing so it cannot emit a payload the
bounded consumer would reject merely for structure.

## Consumer gates still required

OrcSDR must authenticate the exact received bytes with its approved public
key **before** interpreting entries. It must reject the legacy/unknown schema,
duplicate keys, unsupported profiles and unsafe URLs; compare revision against
persisted accepted state and check expiry with a trustworthy clock. Merely
having those fields in this builder does not implement rollback/freshness
protection. A clock-unavailable policy still needs an explicit implementation.

The integration branch now has a worker-only signed consumer with host tests:
`ui/map_catalog.cpp` authenticates before parsing and checks these bounds,
schema, manifests, URL/identity agreement, expiry and supplied revision/hash.
It refuses downloads when the caller cannot provide trusted UTC, and rejects
reuse of one revision with different payload bytes. It is not yet wired to
network acquisition or persisted accepted-state handling; no production key
or endpoint is configured by that source change.

The device must then enforce the expected archive size/hash, TLS, resume object
identity, bounded storage, cancellation, staging and activation/recovery. A
signed manifest cannot substitute for hashing downloaded bytes. Mirrors must
deliver the same signed artifact. Manual compatible-pack import must remain
available without depending on this catalog or hosting service.

No production trust key, catalog endpoint, hosting provider or publication is
chosen by this tool. Those require separate approval and device verification.
