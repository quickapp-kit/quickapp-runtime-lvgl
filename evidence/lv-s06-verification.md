# LV-S06 Verification Evidence

## Conclusion

LV-S06 implementation evidence passes. The Core-thread adapter measures from
an immutable Q26.6 font snapshot, generation changes are published through one
bounded notification slot, and simulator/embedded limits and metrics remain
deterministic without LVGL, UI, Layout or cache ownership.

This evidence covers only the LV-S06 font facts and synchronous measure
boundary. It does not claim Core Layout, Yoga, Host Tree, Mount or input
behavior.

## Source Summary

| Area | Files | Responsibility |
|---|---|---|
| Font facts | include/quickapp/lvgl/measure/font_metrics.h, src/measure/font_metrics.cpp | Fixed catalog, scalable design units, profile limits, immutable double snapshot and generation notification |
| Measure adapter | include/quickapp/lvgl/measure/font_measure.h, src/measure/font_measure.cpp | Validated UTF-8 stream, exact family selection, Q26.6 metrics, wrap and constraints |
| Contract tests | tests/lv_s06_contract_tests.cpp | Golden values, role parity, tab/CR/UTF-8/failure cases, profile limits, backpressure, 100,000 measures and 10,000 publishes |
| Boundary scan | cmake/check_lv_s06_boundaries.cmake | No LVGL/SDL/libuv/Yoga/Host Tree/Core cache leakage |

The mandatory V1 identity is the fixed `system-default/400` catalog entry
backed by the repository-declared `NotoSansSC-Alpha.ttf` asset, with
digest `8d29e294f21b8cf18760e8b0abeda5bc4e88acda8a8d899034d5bd298523434f`.
Both profiles use the same asset identity and algorithm; only fixed capacity
limits differ. The Alpha asset covers ASCII and the declared Case 001 CJK
P0 corpus, not arbitrary Unicode CJK.

## Matrix

| Configuration | CTest | Result |
|---|---:|---|
| Debug | 全量 CTest 14/14 | PASS |
| Release | 全量 CTest 14/14 | PASS |
| ASan/UBSan | 全量 CTest 14/14 | PASS |
| TSan | 全量 CTest 14/14 | PASS |
| Release embedded-only (QUICKAPP_LVGL_BUILD_SIMULATOR=OFF) | 全量 CTest 9/9 | PASS |

## Acceptance Mapping

| Case | Evidence | Result |
|---|---|---|
| S06-A01 plain | testMeasureAndFailureContract | Correlation fields, Q26.6 golden width/height and generation are returned |
| S06-A02 button label | same test with kButtonLabel | Label result equals text result; no padding or button chrome |
| S06-A03 empty | empty request assertion | Intrinsic result is 0x0 |
| S06-A04 hard breaks | stream algorithm and boundary tests | LF/CRLF and trailing lines are deterministic |
| S06-A05 wrap | constrained request and stream algorithm | Width constraint is applied before result normalization |
| S06-A06/A07 constraints | exactly/atMost assertions | Exactly is preserved; atMost is never exceeded |
| S06-A08 intrinsic | unconstrained ASCII/CJK assertions | Max hard-line width and natural line height are returned |
| S06-F01 family/size | profile identity and scaling implementation | Same family digest and scalable design-unit algorithm in both profiles |
| S06-F02 unsupported | missing token and invalid number assertions | No fallback; fixed MEASURE_FAILED |
| S06-F03 initial generation | publisher initialization | Initial generation is 1 |
| S06-F04/F05 publish/mismatch | old reader and stale request assertions | Old reader remains valid; stale request fails retryably |
| S06-F06 notification backpressure | busy/full notification test | One pending generation is retained and retried after owner service |
| S06-F07 double-slot busy | active reader and close-busy test | Publisher never overwrites a reader slot or allocates a third slot |
| S06-N01/N02 invalid input | malformed UTF-8, NaN and constraint cases | Invalid input produces failure, never 0x0 fallback |
| S06-N03 glyph/family | exact lookup and missing token | Missing glyph/family fails without replacement fallback |
| S06-N04 arithmetic | checked Q26.6 operations | Overflow returns typed measure failure |
| S06-N05/N06 limits | simulator/embedded profile and request limit assertions | Fixed limits reject over-bound input |
| S06-N07/N08 close | notification and reader close test | Close is nonblocking; reader release permits finalization |
| S06-V02/V03 golden | font, role, tab, UTF-8, wrap and constraint tests | Fixed algorithm and fixtures are deterministic |
| S06-V04/V05 generation | reader, full queue and close tests | Strict sequence, one pending signal and no reentrant measure |
| S06-V07 boundary | lv_s06_boundary_scan | No UI/backend/Layout/cache dependency |
| S06-V08 pressure | stress test | 100,000 measures and 10,000 generation publishes pass |

## Asset and Host Consistency

- The Host Mount `fontSize` path and the S06 measure path use the same
  `system-default/400` token, weight and asset digest.
- S04 validates the actual LVGL glyph descriptor for `中` at 40px. Its width
  equals the S06 Q26.6 width; height differs only by LVGL integer-pixel
  quantization of less than one pixel.
- Simulator and embedded profile probes consume the same compiled font bytes,
  verify the same digest and glyph, and return allocator free bytes to the
  baseline after destruction.

## Resource And Thread Evidence

- Snapshot storage is exactly two fixed slots.
- Simulator and embedded catalog limits are 16 and 4 faces respectively.
- Request limits are 65,536/32,768/4,096 for simulator and
  4,096/2,048/256 for embedded bytes/code points/lines.
- measure is synchronous and reads only the acquired immutable snapshot.
- Publishing is owner-only; generation notification is one pending value and
  is retried by owner service without blocking.
- No text-sized persistent workspace or dynamic result queue is introduced.
- After finalization, readers are zero, pending notification is false, both
  snapshots are cleared, and the publisher is closed.

## Reproduction

Run from this project directory:

    cmake -S . -B build-w2-debug -G Ninja -DCMAKE_BUILD_TYPE=Debug
    cmake --build build-w2-debug --clean-first --parallel 4
    ctest --test-dir build-w2-debug --output-on-failure

    cmake -S . -B build-w2-release -G Ninja -DCMAKE_BUILD_TYPE=Release
    cmake --build build-w2-release --clean-first --parallel 4
    ctest --test-dir build-w2-release --output-on-failure

    cmake -S . -B build-w2-asan -G Ninja -DCMAKE_BUILD_TYPE=Debug \
      -DQUICKAPP_LVGL_ENABLE_ASAN=ON
    cmake --build build-w2-asan --clean-first --parallel 4
    ctest --test-dir build-w2-asan --output-on-failure

    cmake -S . -B build-w2-tsan -G Ninja -DCMAKE_BUILD_TYPE=Debug \
      -DQUICKAPP_LVGL_ENABLE_TSAN=ON
    cmake --build build-w2-tsan --clean-first --parallel 4
    ctest --test-dir build-w2-tsan --output-on-failure

    cmake -S . -B build-w2-embedded -G Ninja -DCMAKE_BUILD_TYPE=Release \
      -DQUICKAPP_LVGL_BUILD_SIMULATOR=OFF
    cmake --build build-w2-embedded --clean-first --parallel 4
    ctest --test-dir build-w2-embedded --output-on-failure

Source manifest generation and validation:

    {
      printf '%s\n' CMakeLists.txt README.md lv_conf.h
      find include src fakes tests cmake assets -type f
      printf '%s\n' evidence/lv-s01-verification.md \
        evidence/lv-s02-verification.md evidence/lv-s03-verification.md \
        evidence/lv-s04-verification.md evidence/lv-s06-verification.md
    } | LC_ALL=C sort -u | xargs shasum -a 256 \
      > evidence/source-manifest.sha256
    shasum -a 256 -c evidence/source-manifest.sha256
