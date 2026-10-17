# Streaming cleanup under allocation failure

The oversized-feature cleanup copied an oversized byte vector while unwinding,
which required another allocation and retained the oversized byte count anyway.
Swap with an empty vector instead. Layer table cleanup likewise uses an empty
swap, with no non-binding shrink request. Geometry cleanup already used this form.

The new host regression covers a failed gzip read and an injected bad_alloc read,
with oversized retained feature bytes and geometry. With exceptions enabled in the
MSVC test build (`/EHsc /fsanitize=address`), both capacity assertions fail on
70fea367's original cleanup and pass with this fix. The before/after logs are saved
beside this note. Exception-enabled host tests pass 1/1 target (all host cases).
Early local attempts omitted /EHsc when overriding CMAKE_CXX_FLAGS; those were
corrected before recording the final regression. No ESP32 hardware was used.

Reproduce from a Visual Studio x64 developer shell:
```
cmake -S tests/host -B .local/rings-host-msvc -G "Visual Studio 18 2026" -A x64 -DCMAKE_CXX_FLAGS="/fsanitize=address /EHsc" -DCMAKE_C_FLAGS=/fsanitize=address
cmake --build .local/rings-host-msvc --config RelWithDebInfo --parallel 4
ctest --test-dir .local/rings-host-msvc -C RelWithDebInfo --output-on-failure
```

This branch includes the separately reviewed polygon-ring PR4 parent; no merge
into OrcMaps main is claimed or authorized. OrcSDR still needs exception-runtime
support and catches at its adapter boundary before low-memory device testing.
