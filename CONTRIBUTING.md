# Contributing to TinyAwait

TinyAwait is intentionally small. Changes are welcome when they keep the library predictable, easy to audit, and practical on embedded targets.

Before adding a feature, first ask whether it belongs in a small non-blocking delay primitive or in a higher-level async framework.

## Code style

- Use C++20 and keep the public API simple.
- Use 4 spaces for indentation. Do not use tabs.
- Put opening braces on the same line as the declaration or statement.
- Prefer descriptive names over short abbreviations outside tight local loops.
- Keep implementation details inside `tinyawait::detail`.
- Use configuration macros only for compile-time library settings or platform integration.
- Avoid heap allocation, exceptions, RTTI, threads, locks, and dynamic containers in the TinyAwait execution path.
- Keep comments short and explain why a non-obvious choice exists rather than restating the code.
- Do not add abstraction layers unless they clearly improve correctness, portability, memory use, or measured performance.

## Correctness

Changes that touch scheduling, coroutine lifetime, or frame storage should include focused regression tests. Important areas include:

- coroutine creation, suspension, completion, and destruction;
- parent/child and detached task lifetimes;
- clock wraparound and full-range delays;
- task-count and memory exhaustion;
- frame-arena reuse, alignment, splitting, and coalescing;
- unusual release order and fragmentation;
- no-heap behavior;
- shared state across multiple translation units.

Run the full host suite before proposing a change:

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel
ctest --test-dir build --output-on-failure
```

For allocator or coroutine-frame changes, also check GCC and Clang at `-O0`, `-O2`, and `-Os`, and run ASan/UBSan where available.

## Performance and memory

TinyAwait targets small embedded workloads, so a faster result that consumes substantially more RAM or Flash is not automatically better.

When a change adds runtime machinery:

- compare against the current implementation with identical compiler flags;
- run several repetitions and report a representative value rather than the best run;
- measure CPU, static RAM, and linked code size;
- separate scheduler cost from allocator cost when possible;
- keep host measurements clearly labeled as host measurements;
- do not turn noisy nanosecond benchmarks into CI pass/fail thresholds.

If a simpler candidate is slightly slower but removes a correctness or portability risk, document the trade-off instead of hiding it.

## Documentation

Keep public documentation direct and practical. Explain configuration, limits, and failure behavior in plain language. Avoid marketing-style claims and do not call a target verified unless it was actually built or tested with that target's toolchain or hardware.

Examples and public API documentation should remain in English.
