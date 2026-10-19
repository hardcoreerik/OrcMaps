# pack-inspect (host-only)

Inspect a PMTiles archive through the real OrcMaps pipeline, or render a
1280×720 preview. Visible tiles come from `orcmap::EnumerateVisibleTiles`. Pack misses are
skipped (`GetTile` false), not enumeration failures.

```
cmake -S tools/pack-inspect -B build-pack-inspect -G "Visual Studio 18 2026"
cmake --build build-pack-inspect --config Release
```

```
orcmap_pack_inspect header ARCHIVE
orcmap_pack_inspect tile ARCHIVE Z X Y
orcmap_pack_inspect sample ARCHIVE --lat LAT --lon LON --zoom Z [--radius N]
orcmap_pack_inspect preview ARCHIVE --lat LAT --lon LON --zoom Z
    [--out FILE --style ID --schema ID --verbose]
```

Style ids: `orcsdr-dark`, `standard-light`, `high-contrast-field`,
`night-red-safe`.

Classification defaults to `openmaptiles-3.16`. Pass
`--schema orcmaps-overview-1` for Natural Earth overview packs.
Pass `--schema protomaps-v4` for Protomaps v4 (kind/kind_detail); it is a
distinct input profile, not an OpenMapTiles archive. Profile support in this
host tool does not establish OrcSDR device compatibility.

Optional real-font place-label previews require FreeType development files:

```
cmake -S tools/pack-inspect -B build-pack-inspect-labels -DORCMAP_INSPECT_LABELS=ON
cmake --build build-pack-inspect-labels
build-pack-inspect-labels/orcmap_pack_inspect preview ARCHIVE --lat 44.05 --lon -123.02 --zoom 8 --schema protomaps-v4 --labels-font /path/to/font.ttf --out labels.ppm
```

Labels use a 128-candidate pool, at most 64 non-overlapping placements and
24-pixel text. `HOST_LABELS` reports rejected names, unsupported glyphs and
capacity limits. English names are preferred, followed by local names.
The initial adapter rejects scripts requiring shaping; a font's existence
does not establish full Unicode support. These host previews do not prove
Tab5 font coverage, memory use or performance. FreeType is optional and is
not added to firmware dependencies. To run the focused real-font test,
configure `-DORCMAP_TEST_FONT=/path/to/DejaVuSans.ttf` and run
`ctest --test-dir build-pack-inspect-labels -R '^orcmap_font_tests$' --output-on-failure`.

Springfield / 97477 host preview (pack from `tools/pack-builder`):

```
build-pack-inspect/Release/orcmap_pack_inspect.exe preview data/local/springfield-97477.pmtiles --lat 44.0500 --lon -123.0220 --zoom 14 --style orcsdr-dark --out data/local/springfield-97477-orcsdr-dark.ppm
```

Generated images are gitignored. Do not treat a committed PPM as project
truth.
