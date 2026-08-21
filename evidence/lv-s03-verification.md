# LV-S03 Verification Evidence

## Conclusion

LV-S03 implementation evidence passes. The Surface gateway is bounded, all
Surface table and page-root mutations run on the owner thread, accepted
commands produce at most one terminal result, and explicit close converges all
roots, operations, results, and local mappings to zero.

This evidence covers only the LV-S03 Surface Host boundary. It does not claim
that Mount, Event, Measure, Core Navigation, or a final V1 product host exists.

## Source Summary

| Area | Files | Responsibility |
|---|---|---|
| Typed Surface boundary | include/quickapp/lvgl/surface/surface_types.h | Fixed command/result union and local visibility states |
| Page-root port | include/quickapp/lvgl/surface/page_root_backend.h | Owner-only root and content lifecycle ports |
| Surface gateway | include/quickapp/lvgl/surface/surface_host.h, src/surface/surface_host.cpp | Fixed operation slots, replay guard, owner state machine, bounded result retry and close |
| LVGL adapter | include/quickapp/lvgl/surface/lvgl_page_root_backend.h, src/surface/lvgl_page_root_backend.cpp | The only LVGL object boundary; hidden root create/visibility/delete |
| Contract tests | tests/lv_s03_contract_tests.cpp | State, atomicity, replay, backpressure, multi-producer and 10,000-cycle pressure |
| Boundary scan | cmake/check_lv_s03_boundaries.cmake | Shared-layer leakage and future-scope scan |

The shared Surface gateway has no LVGL, SDL or libuv type. The concrete LVGL
type appears only in lvgl_page_root_backend.cpp.

## Matrix

| Configuration | CTest | Result |
|---|---:|---|
| Debug | 10/10 | PASS |
| Release | 10/10 | PASS |
| ASan/UBSan | 10/10 | PASS |
| TSan | 10/10 | PASS |
| Release embedded-only (QUICKAPP_LVGL_BUILD_SIMULATOR=OFF) | 7/7 | PASS |

## Acceptance Mapping

| Case | Evidence | Result |
|---|---|---|
| S03-A01 create | testSurfaceStateMachineAndAtomicCommands | Root is created hidden and mapped only after successful allocation |
| S03-A02 duplicate/replay | completed RequestId replay assertion | Replay is rejected before a second root mutation |
| S03-A03 mount readiness | markFullMountCommitted contract | Present before readiness fails; readiness is owner-only |
| S03-A04 root present | state-machine test | Hidden-mounted root becomes visible in one owner task |
| S03-A05 visibility/no-op | state-machine test | Visibility transition succeeds; repeated hidden has no backend mutation |
| S03-A06 invalid transition | state-machine test | Missing or unmounted state returns typed failure without mutation |
| S03-P01 push | state-machine test | Target becomes visible and explicit source becomes hidden atomically |
| S03-P02 push preflight | state-machine test and scope scan | Invalid source/target/root leaves both local states unchanged |
| S03-P03 explicit source | typed source_surface_id path | No platform-selected stack predecessor exists |
| S03-P04 close/reveal | close path | Content release, root destruction and explicit reveal complete in one owner task |
| S03-P05 close failure | injected FakeContent::canRelease failure | No release or reveal occurs on failed preflight |
| S03-P06 explicit reveal | typed reveal_surface_id path | Only the requested hidden root is revealed |
| S03-N01 capacity | fixed 16/4 limits in target and tests | No dynamic root growth |
| S03-N02 queue/backpressure | testSurfaceBackpressureConflictAndReset | Full/closed admission is typed and has no side effect |
| S03-N03/N04 RequestId | replay and pending conflict assertions | Same RequestId cannot create a second operation |
| S03-N05 Surface conflict | same test | A second command touching an accepted Surface is rejected |
| S03-N06 result backpressure | ResultPort busy path | Result remains in its original slot and is retried by service |
| S03-N07/N08 destroy/reset | injected content failure | Destroy failure resets the local container and makes the ID unaddressable |
| S03-N09 shutdown | explicit close plus finishClose | Accepted work drains by owner turns; remaining roots reset deterministically |
| S03-V06 pressure | 10,000 cycles plus four producers | No accepted operation loss or resource growth |
| S03-V07 boundary | lv_s03_boundary_scan | No route/navigation, Mount, Event/Input, Measure, SDL/libuv leakage |
| S03-V08 sanitizer/resource | all matrix rows | Sanitizers pass; roots/operations/results are zero after close |

## Resource And Thread Evidence

- Profile storage is fixed at 16 live roots and 16 operation slots for
  simulator, 4/4 for embedded.
- SurfaceHostAdapter::post only admits a command; it never calls LVGL.
- SurfaceHostTable, local phases, root handles and content hooks are touched
  by the owner pump.
- Result retry reuses the original operation slot and does not allocate a
  second result queue.
- SurfaceHostAdapter destruction only asserts explicit close invariants.
- The multi-producer test exercises four concurrent producers; failed bounded
  admission does not transfer ownership or execute a partial command.
- The pressure loop executes 50,000 accepted owner commands over 10,000
  create/present/hide/show/destroy cycles and checks zero pending operations
  after every cycle.

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
      find include src fakes tests cmake -type f
      printf '%s\n' evidence/lv-s01-verification.md \
        evidence/lv-s02-verification.md evidence/lv-s03-verification.md \
        evidence/lv-s06-verification.md
    } | LC_ALL=C sort -u | xargs shasum -a 256 \
      > evidence/source-manifest.sha256
    shasum -a 256 -c evidence/source-manifest.sha256
