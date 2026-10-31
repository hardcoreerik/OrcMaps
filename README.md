# OrcMaps

An offline-first map engine for embedded devices. ESP32-class hardware
(ESP32-S3, ESP32-P4) is the first target; the core is deliberately platform-
portable so other microcontroller-class devices can adopt it later.

OrcMaps renders useful, readable maps entirely from local storage (SD card).
After firmware and map packs are provisioned, boot, pack selection,
validation, pan, zoom, rendering, projection, attribution, and overlays must
work with every network physically unavailable. OrcMaps core has no online
tile fallback, account, activation, telemetry, or update-check requirement.
It owns base geography
(roads, water, boundaries, place labels) and a small set of generic drawing
primitives (markers, polylines, polygons). It does **not** know what an
aircraft, a LoRa node, or an RF site is — applications translate their own
domain data into OrcMaps overlays.

[`hardcoreerik/OrcSDR`](https://github.com/hardcoreerik/OrcSDR) is the first
consumer, replacing its prototype `ORCMAP1` implementation. OrcMaps is built
so unrelated ESP32 projects can use it too.

## Status

Early development. The map-pack container format is decided (PMTiles v3 —
see [`docs/FORMAT_DECISION.md`](docs/FORMAT_DECISION.md)). Implemented and
host-tested: PMTiles archive reader, Web Mercator tile math, MVT container
decode (schema-agnostic), OrcMaps Feature / Geometry model, MVT→Feature
translation, and the style system. An ESP-IDF compile/link smoke test
exists at [`examples/generic-esp32`](examples/generic-esp32) and does
**not** require M5GFX. A host framebuffer render proof exists (no M5GFX).
M5GFX `DisplayTarget` is an optional exported adapter
(`#include "orcmap/m5gfx/display_target.hpp"`). Viewport enumerates
visible tiles (X wrap, no overzoom).
Portable camera controls now support center, integer zoom, geographic pan,
fit/visible bounds, coordinate projection, and anchor-preserving zoom; device
input bindings are not implemented yet.
The portable pack model, manifest validation, deterministic identity,
multi-pack catalog, and one-best-local-pack resolver are host-tested. The
Springfield golden pack now includes its manifest and SHA-256 sidecars.
Runtime JSON discovery and on-device file hashing remain to be implemented.
A hardware-verified static Springfield render exists on M5Stack Tab5
([`examples/m5stack-tab5`](examples/m5stack-tab5); evidence
[`docs/evidence/TAB5_SPRINGFIELD_HARDWARE.md`](docs/evidence/TAB5_SPRINGFIELD_HARDWARE.md)).
A second Springfield hardware demo for the LilyGO T-Display-S3 Touch +
SD Shield is physically verified under ESP-IDF 5.5.4
([`examples/lilygo-tdisplay-s3`](examples/lilygo-tdisplay-s3); evidence
[`docs/evidence/LILYGO_TDISPLAY_S3_SPRINGFIELD_HARDWARE.md`](docs/evidence/LILYGO_TDISPLAY_S3_SPRINGFIELD_HARDWARE.md)).
That demo writes a new numbered timing report to the SD card after every
successful render, avoiding a USB serial dependency. Its first confirmed
numbered-report run rendered the Springfield frame in **1,037 ms**.
Not production-ready: hardware demos remain static and labels are absent.
FeatureKind mapping is
experimental. See [`STATUS.md`](STATUS.md).

## Design principles

1. **Fully offline after provisioning.** The network may deliver packs, but is never part of runtime map operation. Missing detail uses another eligible local pack or reports unavailable; it is never fetched online.
2. **One basemap, many applications.** Base geography is shared; every consumer supplies its own overlays.
3. **Bounded memory.** Map/pack size must never determine RAM usage — everything streams.
4. **Portable core.** No application state (aircraft, nodes, RF data) inside the engine.
5. **Graphics-framework-independent core.** M5GFX is the first *reference integration* because OrcSDR/Tab5 is the first real consumer — it is not the renderer architecture.
6. **Map data is not firmware.** Packs install/update independently of the engine and the consuming app.
7. **User data is not map-pack data.** Markers/waypoints survive pack replacement, are never uploaded during pack discovery.
8. **Safe updates.** A corrupt or partial download never replaces a working pack.
9. **Provenance matters.** Every pack carries source, license, attribution, and hash metadata — enforced by a machine-readable registry (`data/sources/`) and CI, not just documented. See [`docs/DATA_AND_LICENSING.md`](docs/DATA_AND_LICENSING.md).
10. **No unauthorized tile scraping.** Packs are built from legitimate extract data (e.g. OSM `.pbf` extracts), never bulk-scraped raster tile servers.

## Repository layout

```
include/orcmap/        Public headers — the portable API
src/core/              Geo math, Viewport, pack validation/catalog/resolution
src/storage/           Reserved; ByteSource interface lives in include/orcmap/
src/tiles/             PMTiles reader, MVT decoder, bounded gzip decompress, MVT→Feature translation
src/render/            Style system + FeatureTile renderer (host proof)
src/overlays/          Planned marker/polyline/polygon overlay primitives
src/cache/             Planned bounded LRU tile caches
adapters/host/         stdio ByteSource for host tests/tools
adapters/m5gfx/include/orcmap/m5gfx/  Optional DisplayTarget headers (consumer supplies M5GFX)
adapters/esp_idf/      ESP-IDF FILE* ByteSource (app mounts SD)
tools/pack-builder/    Springfield host pack script (Planetiler)
tools/pack-inspect/    Host inspect + real-geography preview
tools/pack-verify/     Planned host-side pack verifier
examples/m5stack-tab5/ Tab5 + SD hardware demo (M5Unified)
examples/lilygo-tdisplay-s3/T-Display-S3 Touch + SD Shield demo (M5GFX)
examples/generic-esp32/ESP-IDF compile smoke test (no graphics framework)
examples/m5gfx/        ESP-IDF + M5GFX DisplayTarget compile proof
examples/host-render/  Host 1280x720 PPM preview (synthetic tiles)
tests/host/            Host-buildable unit tests (no hardware required)
tests/consumer/        External-consumer build gate (public headers only)
data/sources/          Data provenance registry
docs/                  Architecture, format decision, porting, licensing docs
```

## License

AGPL-3.0-only, with commercial licensing available by agreement — the same
model as [`hardcoreerik/esp-rtl-sdr`](https://github.com/hardcoreerik/esp-rtl-sdr).
See [`LICENSE`](LICENSE) and [`LICENSING.md`](LICENSING.md).

Map **data** distributed alongside this engine (e.g. prebuilt map packs) is
subject to its own source license (typically OpenStreetMap's ODbL) —
engine licensing and data licensing are independent. See
[`docs/DATA_AND_LICENSING.md`](docs/DATA_AND_LICENSING.md).

## Contributing

See [`CONTRIBUTING.md`](CONTRIBUTING.md), including how licensing and data
provenance apply to contributions.

## Support development

If this project is useful to you, consider supporting its development. Across ORC projects, support helps fund development time, hardware testing, receiver compatibility, and decoder work.

[GitHub Sponsors](https://github.com/sponsors/hardcoreerik) · [Ko-fi](https://ko-fi.com/hardcoreerik) · [PayPal](https://paypal.me/hardcoreerik)
