# LV-S01 Verification Evidence

## Conclusion

The LV-S01 directed correction passes Debug, Release, ASan/UBSan, and TSan. Queue destruction performs no cleanup or waiting; critical-section contention performs one attempt and returns `busy`. Fixed storage, FIFO, pump budget, owner-only execution/destruction, and drain/cancel shutdown remain intact.

## Environment

- Date: 2026-08-16
- Compiler: Apple Clang 21.0.0
- CMake: 4.4.2
- Generator: Ninja 1.13.2
- Language mode: C++17, exceptions and RTTI disabled for project targets

## Commands

```sh
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug
cmake --build build
ctest --test-dir build --output-on-failure

cmake -S . -B build-asan -G Ninja -DCMAKE_BUILD_TYPE=Debug \
  -DCMAKE_CXX_FLAGS="-fsanitize=address,undefined -fno-omit-frame-pointer" \
  -DCMAKE_EXE_LINKER_FLAGS="-fsanitize=address,undefined"
cmake --build build-asan
ctest --test-dir build-asan --output-on-failure

cmake -S . -B build-tsan -G Ninja -DCMAKE_BUILD_TYPE=Debug \
  -DCMAKE_CXX_FLAGS="-fsanitize=thread -fno-omit-frame-pointer" \
  -DCMAKE_EXE_LINKER_FLAGS="-fsanitize=thread"
cmake --build build-tsan
ctest --test-dir build-tsan --output-on-failure
```

## Results

| Evidence | Result |
|---|---|
| Release build | PASS; 2/2 CTest |
| Owner binding and wrong-owner rejection | PASS |
| Fixed-capacity FIFO and full rejection | PASS; accepted tasks preserved |
| Pump budget and producer concurrency | PASS; caller performs bounded `busy` retry; TSan clean |
| Deterministic queue/input contention | PASS; immediate `busy` with ownership preserved |
| Partial stop contention and retry | PASS; `running -> stopping -> closed` converges |
| Accepted task capture destructor owner | PASS for execute and cancel |
| Queue destructor contract | PASS; Debug invariant visible, Release performs no hidden cleanup |
| Monotonic clock and unsupported wait fallback | PASS |
| Display validation, synchronous frame borrow, failure injection | PASS |
| Raw input bound, move coalescing, edge overflow, bounded drain | PASS |
| Drain and cancel shutdown | PASS |
| Open rollback and close-failure cleanup | PASS |
| Caller-owned single-thread/static-storage configuration | PASS |
| 10,000 open/pump/stop/close cycles | PASS; final task/input depth zero |
| ASan/UBSan | PASS; no memory or undefined-behavior finding |
| TSan | PASS; no data-race finding |
| Foundation boundary scan | PASS |

Release artifact sizes for this verification environment:

| Artifact | Bytes |
|---|---:|
| `libquickapp_lvgl_foundation.a` | 11,600 |
| `libquickapp_lvgl_foundation_fakes.a` | 16,160 |
| `lv_s01_contract_tests` | 99,208 |

## Resource Boundary

- `OwnerTask` uses 64-byte inline callable storage; it performs no heap allocation.
- Task and raw-input queues receive fixed storage and capacity at construction.
- Runtime operations never grow those queues.
- Critical sections use one `test_and_set` attempt. Failure returns `busy` without moving task/sample ownership.
- Stop rejects new work, drains or destroys every accepted task exactly once on the owner, discards pending raw samples explicitly, then closes Input, Display, Wakeup, and the task queue.
- `OwnerTaskQueue` destruction only asserts `closed && depth == 0`; it does not acquire the critical section, stop, execute, destroy, or wait.
- The constrained test uses one task slot, one input slot, no blocking wait, no hidden thread, and no file I/O.
- V1 does not claim ISR safety or lock freedom.

## Dependency Boundary

The Foundation target links no Core, SDL, libuv, LVGL, filesystem, logging, or window library. The automated source scan rejects concrete platform includes/symbols, upper-layer terms, `while(true)`, and looped `test_and_set`.
