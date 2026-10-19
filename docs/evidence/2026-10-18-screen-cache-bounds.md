# Screen-cache painted bounds — October 18, 2026

Base: `86e27a0`, branch `codex/map-quality-delivery`. Main unchanged. Not yet integrated into OrcSDR or tested on hardware.

During the OrcSDR map-quality integration review, the shared screen cache was found to report centerline bounds instead of the painted bounds promised by its header. The CYD band renderer culls features using these fields, so a thick line can disappear from a neighboring band. This also makes the new cache unsafe to reuse with equivalent culling in Tab5.

The focused regression draws horizontal/vertical strokes at widths 1, 2, 3, 4 and 7.5 using the actual host raster target, then checks that every painted pixel is inside the reported bounds. Before the fix, all eight thick-line cases fail. After including the shared raster stroke's centered footprint, all ten cases pass. Even-width top/left bias follows the existing stroke contract. Drawn geometry and public signatures are unchanged.

Files: `src/render/renderer.cpp`, `tests/host/test_screen_bounds.cpp`, `tests/host/CMakeLists.txt`, this evidence note.

From this worktree root, Ubuntu WSL:

```sh
cmake -S tests/host -B .local/screen-bounds -DCMAKE_BUILD_TYPE=Debug
cmake --build .local/screen-bounds --target orcmap_screen_bounds_tests -j4
ctest --test-dir .local/screen-bounds -R '^orcmap_screen_bounds_tests$' --output-on-failure
```

Red: exit 1, 0/1 tests pass; failures identify painted pixels outside bounds for both orientations at every thick width. Green: exit 0, 1/1 pass. `git diff --check`: exit 0.

Logs alongside invocation: `screen-bounds-config-20261018.log`, `screen-bounds-build-red-20261018.log`, `screen-bounds-red-20261018.log`, `screen-bounds-build-green-20261018.log`, `screen-bounds-green-20261018.log`.

Limits: focused raster/bounds regression only; no full engine suite, native build, CYD or Tab5 flash, or physical band-seam acceptance. Global coverage, labels and Tab5 fluid interaction remain separate integration gates.
