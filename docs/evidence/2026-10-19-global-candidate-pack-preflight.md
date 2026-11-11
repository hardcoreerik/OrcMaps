# Global regional candidate: pack preflight

The existing approved capped extract was finalized without rewriting its
archive or downloading more map data. Local files are under
`.local/global-assessment/`: `global-z8-candidate.pmtiles`,
`global-z8-candidate.manifest.json` and `global-z8-candidate.sha256`.

- Bytes: **543,380,195**.
- SHA-256: `16adde0b4f67734535db6871b89949e68c7e1dabf1c34d2bd7bd23842a9e5991`.
- PMTiles v3, zoom 0–8, Protomaps-v4 profile/schema.
- Bounds: longitude -180 to 180, latitude -85.0511287 to 85.0511287.
- Pack ID: `20260331t0400z-v4-9e09dd25__protomaps-v4__protomaps-v4__world__b-1800000000_-850511287_1800000000_850511287__z0-8`.

The existing-output pack builder calculated the full local archive hash and
matched the earlier download record. Provenance records the source URL/ETag,
OSM replication 118749 at 2026-03-31T04:00:00Z, Protomaps v4.14.3 and Natural
Earth with its exact revision explicitly unspecified. The upstream 134 GB
archive's full hash was **not** verified; the input hash identifies our local
capped extract. This is candidate metadata, not approval to publish data.

The actual engine pack verifier configured and built successfully, then ran:

```powershell
.local/global-pack-verify-20261019/Debug/orcmap_pack_verify.exe .local/global-assessment
```

Result: **one manifest accepted, zero rejected; archive/header bounds and
zoom agree**. Logs in this worktree root:
`global-candidate-sidecars-20261019.log`,
`global-pack-verify-configure-20261019.log`,
`global-pack-verify-build-20261019.log`, `global-pack-verify-20261019.log`.

The OrcSDR worktree separately built the verifier from its exact managed
component pin `722d0d382f99b2887694af333f85b1ee048f4208` and accepted the same
candidate, one accepted and zero rejected. Its root logs are
`global-pack-consumer-configure-20261019.log`,
`global-pack-consumer-build-20261019.log`,
`global-pack-consumer-verify-20261019.log`.

The verifier's word "installed" means host discovery accepted the local pair;
**nothing was installed on COM17 or COM39**. Discovery checks manifest
compatibility and archive size; header reporting is an additional preflight.
It does not hash every archive byte or render all tiles. Full hashing above
was a distinct builder operation. Worldwide quality, hardware SD transfer,
on-device rendering, hosted catalog and resumable delivery remain open.
