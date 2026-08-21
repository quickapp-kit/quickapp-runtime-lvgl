# LV-S02 Verification Evidence

## Conclusion

LV-S02 implementation evidence passes. The Runtime Host and both backend sets
obey the frozen S02 boundaries, all accepted work converges to zero live
resources, and the embedded-only build has no SDL or libuv dependency.

This is isolated S02 evidence. It is not a final V1 product Manifest, product
link map, Surface result, or Case 001 runtime claim.

## Environment

| Item | Value |
|---|---|
| Date | 2026-08-17 |
| Compiler | AppleClang 21.0.0.21000101 |
| Generator | Ninja |
| C++ | C++20 |
| QuickJS | vendored sibling provider |
| libuv | 1.52.1 |
| SDL | SDL3 3.4.14 |
| SDL test driver | `dummy` |

## Build Matrix

| Configuration | Result |
|---|---|
| Debug | 6/6 CTest PASS |
| Release | 6/6 CTest PASS |
| ASan/UBSan | 6/6 CTest PASS |
| TSan | 6/6 CTest PASS |
| Release embedded-only | 3/3 CTest PASS |

Each full matrix run includes LV-S01 regression, Foundation boundary scan,
embedded isolated probe, LV-S02 contract suite, simulator isolated probe and
LV-S02 boundary/trimming scan.

All five configurations were reconfigured and rebuilt with `--clean-first`
after the source manifest was first generated.

## Evidence Binding

`evidence/source-manifest.sha256` binds the exact LV-S02 production sources,
Foundation/fakes consumed by S02, tests, probes, CMake files, README and evidence
inputs used by this report. The manifest intentionally does not hash itself.

Validation result:

```text
shasum: all 39 entries OK
```

Evidence labels used below:

- **Runtime**: an executable assertion ran in CTest.
- **Scan**: the hash-bound source or linked artifact was checked mechanically.
- **Combined**: both forms are required for the stated result.

## Acceptance Mapping

### Composition And Startup

| Case | Evidence | Result |
|---|---|---|
| A01 | **Combined**: `testCompositionAndRealEngine`; `lv_s02_simulator_isolated_probe`; boundary/link scan | Simulator inventory selects one QuickJS, JS Framework, SDL/libuv/File set; validation remains isolated, not a product Manifest. |
| A02 | **Combined**: `testCompositionAndRealEngine`; `lv_s02_embedded_isolated_probe`; embedded-only link scan | Embedded inventory selects one QuickJS, JS Framework, builtin/device/Memory set with no SDL/libuv link. |
| A03 | **Scan**: `RuntimeHost::describeComposition()` returns the injected `const CompositionValidation&`; source manifest binds this implementation | Repeated describe returns the same immutable object and performs no runtime inventory scan. |
| A04 | **Runtime**: `runHostCycle` checks the launch artifact and package size observed by Fake Core; `testCompositionAndRealEngine` freezes the Build Profile separately | Launch data reaches Core without selecting or mutating the Build Profile. |
| A05 | **Runtime**: `runHostCycle(... present_root=true)` | Host remains `starting` until owner pump consumes `presented`, then reports one success and enters `running`. |
| A06 | **Runtime**: `runHostCycle(... present_root=false)` | Failed root never publishes `running`; JS, package, task, input and backend resources are released. |
| A07 | **Runtime**: `testCompositionAndRealEngine`; real `QuickJsEngineProvider` in both isolated probes | Descriptor must match the single Engine module; one context is created and deterministically destroyed. |
| A08 | **Combined**: `testTraceAdapterDoesNotAffectRuntime`; Host cycles use `NoopTraceSink`; composition checks `platform.lvgl.trace` | Adapter accept/drop and Noop paths do not change Host results; Collector/storage remain absent. |

### PackageSource

| Case | Evidence | Result |
|---|---|---|
| P01 | **Runtime**: `testLibuvFileIdentityShortReadAndClose` + `pumpFile` | Positional libuv read returns exact immutable bytes through `PackageCompletionPort` once. |
| P02 | **Runtime**: same test renames the opened file, creates a replacement path, then truncates the original inode | Read remains bound to the opened handle; post-truncate short read becomes an error. |
| P03 | **Runtime**: `testMemoryPackageSourceAndBackpressure` | Memory range read returns the expected copy without a file backend. |
| P04 | **Runtime**: same test, `(offset=size,length=0)` | Exactly one successful empty completion is delivered. |
| P05 | **Runtime**: file close returns `busy` while a read is accepted; Memory source rejects and completes a post-close read with error | Accepted operation completes once; close converges; new admission is closed. |
| P06 | **Combined**: `ReadCapture` owns moved `ImmutableBytes`; `ImmutableBytes` and both source implementations are source-manifest bound; ASan/UBSan passes | Result bytes have ownership independent of Source storage and survive Source close until capture release. |

### Backend And Lifecycle

| Case | Evidence | Result |
|---|---|---|
| B01 | **Runtime**: `testSimulatorAndEmbeddedBackends` | libuv wake/turn, SDL test frame and SDL mouse down/up raw samples pass through LV-S01 ports. |
| B02 | **Runtime**: same test plus embedded-only probe | Caller-owned builtin turn works; missing notify/wait callbacks return `unsupported`; no hidden thread or simulator dependency. |
| B03 | **Combined**: `runHostCycle` exercises foreground raw signal, background typed control and destroy; source-bound `handleRawSignal` switch covers resume/suspend/shutdown | Every accepted action allocates and matches one RequestId/action result. |
| B04 | **Runtime**: repeated resume in `runHostCycle` | Duplicate raw signal is filtered before a second Core control allocation. |
| B05 | **Runtime**: Fake Core completes background with `LIFECYCLE_BUSY` | Host forwards the same retryable typed result without changing lifecycle truth. |
| B06 | **Runtime**: successful and 10,000 repeated Host cycles | Destroy result is consumed on owner pump and all local resources reach zero. |

### Negative And Fault Cases

| Case | Evidence | Result |
|---|---|---|
| N01 | **Scan**: hash-bound `RuntimeHost::validateLaunchProfile` rejects unknown fields, wrong target, empty artifact, invalid params/viewport/route before loop/package creation | Invalid launch has no fallback or partial Session publication. |
| N02 | **Combined**: duplicate Engine inventory fails in `testCompositionAndRealEngine`; hash-bound `categoryCount == 1` check covers zero/two Engine categories | Composition fails instead of selecting a fallback Engine. |
| N03 | **Combined**: real QuickJS positive contract plus hash-bound descriptor equality checks for id/ABI/module/version | Any Provider mismatch returns `kEngineDescriptorMismatch` before Runtime execution. |
| N04 | **Scan**: hash-bound required component/capability loops and `product_manifest=false` result | Missing View/Text/Button or router/prompt/device fails isolated validation and cannot become final V1 evidence. |
| N05 | **Combined**: crossed embedded inventory negative in `testCompositionAndRealEngine`; boundary/link scan | Required set and opposite-profile modules are both rejected; embedded binary has no simulator dependency. |
| N06 | **Runtime**: Memory invalid range/backpressure/post-close and File short-read/capacity paths | Errors expose no partial bytes and completions are not duplicated. |
| N07 | **Runtime**: LV-S01 `testLifecycleCancelAndOpenRollback` | Failure while opening a backend closes already-opened resources and leaves lifecycle/task/input/display/wakeup closed. |
| N08 | **Runtime**: LV-S01 `testBoundedContentionAndOwnerDestruction` and `testLifecycleContentionRetryConverges`; static no-spin scan | Busy returns immediately without ownership transfer; a later bounded attempt converges. |
| N09 | **Runtime**: `runHostCycle` fills all 64 Host task slots; the 65th post is `full`; four pumps execute exactly 16 each | Queue overflow is typed/observable and accepted FIFO work remains bounded. |
| N10 | **Runtime**: alternating Trace endpoint accepts then drops; Host behavior also passes with Noop sink | Sink loss changes counters only, not Runtime state or result. |
| N11 | **Combined**: hash-bound `processCoreCompletions/progressTeardown` preserve destroy failure while continuing local teardown; LV-S01 close-failure test proves remaining resources still close | Failed destroy result is retained and does not block local resource convergence. |
| N12 | **Combined**: worker-thread Core callbacks under TSan; hash-bound pending callback/active-slot teardown barriers | Host cannot reach destroyed while an accepted callback context is active; callback state is consumed only by owner pump. |

### Resource And Trimming Items

| Item | Evidence | Result |
|---:|---|---|
| 1 | `check_lv_s02_boundaries.cmake` source scan | Shared Host/embedded source contains no SDL/libuv/LVGL type leakage. |
| 2 | embedded-only configure/build, `build.ninja` scan and `otool -L` | Embedded target has no SDL/libuv/File simulator target or dynamic dependency. |
| 3 | simulator isolated probe `otool -L` plus target graph scan | Simulator links SDL3/libuv and does not link embedded backend archive. |
| 4 | composition contract and both isolated probes | Exactly one `runtime.js-framework` and one QuickJS identity are accepted; result remains non-product evidence. |
| 5 | simulator/embedded backend tests and worker callback TSan run | Backend calls are owner-bound; package completion crosses only the completion port. |
| 6 | profile-limit assertions, Host queue-full test, Package backpressure test and no-spin scan | Task/input/read/retry/Trace limits are fixed; no unbounded retry queue or spin exists. |
| 7 | successful/root-failed Host assertions and 10,000 cycles | task/input/read/session/backend/engine live state is zero after stop. |
| 8 | Debug, Release, ASan/UBSan and TSan matrix | 10,000 lifecycle cycles pass in all four full configurations. |
| 9 | forbidden semantic source scan | No Surface, Mount, standard Event/Input, Measure, Capability Provider, Collector or LVGL object implementation exists. |
| 10 | `profileDefinition`, Host queue-full/pump-budget assertions and lifecycle drain tests | Both profile capacities, one retry per source per turn and `drain` stop policy remain frozen. |

## Contract Summary

- 7 LV-S02 contract groups pass.
- Composition positive/negative cases use the real QuickJS descriptor.
- `CompositionValidation` remains `isolated_evidence=true` and
  `product_manifest=false`.
- Memory and file reads cover zero range, invalid range, completion backpressure,
  fixed file identity after rename/replace, short read and close race.
- SDL displays a test frame and converts SDL mouse events only to raw samples.
- libuv provides owner-loop wakeup and file I/O only.
- Embedded uses caller-owned builtin loop and device callback ports.
- Core callbacks are delivered from a worker thread; Host state changes only in
  owner `pumpOnce`.
- Host queue capacity 64 and pump budget 16 are enforced; the 65th post returns
  `full` and increments the overflow counter.
- 10,000 isolated compose/start/fake-root/destroy cycles pass in every full
  configuration.
- Every cycle ends with task/input/read/session/engine depth or live count zero.
- Root presentation failure triggers Host fallback JS stop and full local rollback.

## Dependency And Scope Evidence

The Release embedded isolated probe links only:

```text
/usr/lib/libSystem.B.dylib
/usr/lib/libc++.1.dylib
```

The Release simulator isolated probe links libuv 1 and SDL3 in addition to the
system libraries. Configuring with `QUICKAPP_LVGL_BUILD_SIMULATOR=OFF` creates no
SDL/libuv backend target or package lookup.

Source scans pass for backend leakage, unbounded spin and forbidden later-stage
semantics. No Surface, Mount, standard Event/Input, Measure, Capability Provider,
Collector, or LVGL object code is present.

## Isolated Artifact Sizes

These values describe S02 static archives/probes only and are not product size
claims:

| Artifact | Release bytes |
|---|---:|
| Runtime Host archive | 42,744 |
| Embedded backend archive | 15,552 |
| libuv backend archive | 24,568 |
| SDL backend archive | 12,880 |
| Embedded isolated probe | 1,145,296 |
| Simulator isolated probe | 1,146,784 |

Final V1 Manifest, actual product link map, binary size and cross-platform Case
evidence remain owned by later integration work.

## Reproduction

Run from `quickapp-runtime-lvgl`.

Source manifest generation and validation:

```sh
{
  printf '%s\n' CMakeLists.txt README.md
  find include src fakes tests cmake -type f
  printf '%s\n' evidence/lv-s01-verification.md \
    evidence/lv-s02-verification.md
} | LC_ALL=C sort -u | xargs shasum -a 256 \
  > evidence/source-manifest.sha256
shasum -a 256 -c evidence/source-manifest.sha256
```

Build and test matrix:

```sh
cmake -S . -B build-s02-debug -G Ninja -DCMAKE_BUILD_TYPE=Debug
cmake --build build-s02-debug --clean-first -j 8
ctest --test-dir build-s02-debug --output-on-failure

cmake -S . -B build-s02-release -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build-s02-release --clean-first -j 8
ctest --test-dir build-s02-release --output-on-failure

cmake -S . -B build-s02-asan -G Ninja -DCMAKE_BUILD_TYPE=Debug \
  -DQUICKAPP_LVGL_ENABLE_ASAN=ON
cmake --build build-s02-asan --clean-first -j 8
ctest --test-dir build-s02-asan --output-on-failure

cmake -S . -B build-s02-tsan -G Ninja -DCMAKE_BUILD_TYPE=Debug \
  -DQUICKAPP_LVGL_ENABLE_TSAN=ON
cmake --build build-s02-tsan --clean-first -j 8
ctest --test-dir build-s02-tsan --output-on-failure

cmake -S . -B build-s02-embedded -G Ninja -DCMAKE_BUILD_TYPE=Release \
  -DQUICKAPP_LVGL_BUILD_SIMULATOR=OFF
cmake --build build-s02-embedded --clean-first -j 8
ctest --test-dir build-s02-embedded --output-on-failure
! rg -n 'quickapp_lvgl_(sdl|libuv)_backends|PkgConfig::(SDL3|LIBUV)' \
  build-s02-embedded/build.ninja
/usr/bin/otool -L build-s02-embedded/lv_s02_embedded_isolated_probe
```
