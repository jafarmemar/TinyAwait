# Contributing to TinyAwait

TinyAwait is intentionally small. Contributions are welcome when they preserve that property.

Before proposing a feature, ask whether it belongs in a tiny non-blocking delay primitive or in a higher-level async framework.

## Expectations

- Keep the core header readable in one sitting.
- Avoid heap allocation, exceptions, RTTI, threads, locks, and dynamic containers.
- Preserve the fixed-memory single-threaded model unless there is strong measured evidence for a change.
- Add tests for lifecycle, wraparound, capacity, allocator state, and memory behavior affected by the change.
- Run GCC and Clang host tests.
- Run ASan/UBSan where supported.
- Measure CPU, Flash/code-size, and RAM impact for changes that add runtime machinery.
- Compare performance changes with identical compiler settings and multiple repetitions; do not turn noisy nanosecond timings into CI gates.
- Do not label an embedded target "verified" without actually building/testing it.

## Local verification

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel
ctest --test-dir build --output-on-failure
```

The capacity-scaling benchmark can also be compiled directly with different `TINYAWAIT_MAX_TASKS` values; see `BENCHMARKS.md`.

Keep public APIs and examples in English and keep benchmark claims reproducible.
