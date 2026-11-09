# Standalone generic overlay demo

This synthetic fixture uses an application-controlled marker and polyline; it
contains no real observations and has no OrcSDR dependency.

```sh
cmake -S examples/generic-overlays -B build-overlay-demo
cmake --build build-overlay-demo
./build-overlay-demo/generic_overlay_demo
```

It counts the rendering primitives sent to a generic `RenderTarget`, then
hit-tests the map center and prints the selected application ID. No map pack
is required; a real application renders its offline basemap first, then its
overlay pass, then attribution/controls.
