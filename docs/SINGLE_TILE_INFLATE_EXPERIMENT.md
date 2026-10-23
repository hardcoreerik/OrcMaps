# Optional single-tile inflate experiment — October 23, 2026

Prepared against b59d478 on codex/map-quality-delivery. No version/tag, engine
publication, OrcSDR dependency change or device flash is included.

## Purpose and contract

COM17's routine-pan baseline has a 4.55-second p95 detail-frame time against a
2-second target. Six repeated source tiles are decoded for every frame. The
streaming parser needs two passes because features can precede their tables;
the existing low-memory path inflates the gzip payload on each pass.

MvtStreamOptions::max_buffered_tile_bytes optionally enables one temporary
inflated tile, with a hard256KiB ceiling. Default0 leaves existing callers on
the original streaming path. Scratch reuses the buffer across tiles; decoded
features never accumulate. Gzip ISIZE larger than the configured/hard ceiling
keeps streaming. Allocation failure frees the optional buffer and falls back.
No-exception builds ignore the option, preserving their streaming memory model.
Invalid gzip/CRC/short reads fail rather than falling back to a weaker parser.

The parser's per-feature, layer-filter and sink-cancellation behavior stays
shared. Existing scratch trimming still covers all exits, including exceptions.
The optional buffer remains retained with its scratch, so the consumer must
include up to262144extra bytes plus vector metadata in reclaim accounting.
No pack format, file layout, projection, profile or rendering change is made.

## Focused evidence

Run from the engine worktree:

```powershell
wsl -d Ubuntu -- bash .local/global-assessment/run_compression_only_20261023.sh
```

This runs the compression test group, not the whole OrcMaps suite. Existing
feature equivalence checks now compare default streaming, bounded buffered and
undersized-budget fallback against full decode/translation: geometry, IDs,
extents, layers and properties match. Buffered corrupt-CRC, short-read and
refusing-sink checks pass. The local runner also overrides allocation only to
force the optional256KiB reserve to throw; default streaming still succeeds
with the same feature count. The runner source/script and logs are retained.

Logs beside invocation: maps-single-inflate-compression-20261023.log (initial),
maps-single-inflate-allocation-final-20261023.log (current focused+allocation).
Both exit0; the latter reports COMPRESSION_ONLY_AND_ALLOC_FAILURE failures=0.

P4 portable-core compilation with exceptions passes. The no-exceptions P4 syntax
check also passes. maps-single-inflate-p4-final-20261023.log records224bytes of
static stack for StreamMvtTile on this compiler; it is not whole call-chain or
runtime high-water proof. The inflate helper also uses its existing chunk/header
stack buffers, so native task-stack acceptance remains required.

## Open gates

Focused ASan/UBSan with allocation injection passes (maps-single-inflate-asan-20261023.log).
Five real-archive tiles (world z0 and four regional z8 tiles) also pass complete
feature/property comparisons and 842x330 pixel equality against full decode/
translation, across default, buffered and undersized-budget paths. Exact command:
`wsl -d Ubuntu -- bash .local/global-assessment/run_compression_global_20261023.sh`;
log maps-single-inflate-global-20261023.log, exit0, views=5/failures=0.
The sanitizer run preceded only this additional test-fixture coverage; core source
was unchanged. These are host equivalence results, not hardware timings.

Broader engine regressions,
consumer PSRAM/reclaim accounting and COM17 p50/p95/max/cancellation/radio checks
remain distinct. No hardware speed improvement or2-second acceptance is claimed.
Source publication to a fetchable development revision needs explicit approval;
only then update OrcSDR's pinned component and generated lock. Never edit managed
component sources or vendor an engine copy into OrcSDR.
