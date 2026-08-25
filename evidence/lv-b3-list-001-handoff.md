# LVGL B3 List + Scroll Handoff

Date: 2026-08-25

## Conclusion

The LVGL platform implementation for `Scroll` and `List` is complete at the
Mount boundary. `Scroll` is a real vertically scrollable LVGL host container;
`List` is a non-scrolling LVGL content container whose keyed children remain
ordinary Runtime Host nodes. The platform emits typed scroll lifecycle events
and releases all native objects and bindings with the Surface.

The real `list-001.rpk` is currently blocked before LVGL Mount by the existing
Examples Composition Root because that root has not registered `List` and
`Scroll`. It is also a single-route artifact with `/pages/Home` only, so this
artifact cannot truthfully prove Detail push/back or repeated page entry.

## Platform Changes

- `HostComponentType::kScroll` maps to `lv_obj_create` with vertical scrolling,
  automatic scrollbar policy, and a real viewport.
- `HostComponentType::kList` maps to `lv_obj_create` as a non-scrolling content
  container. Its children remain individually mounted Runtime Nodes.
- `installScrollHandler` exposes a platform-local typed callback carrying:
  `EventType`, `scrollOffset`, `contentSize`, `viewportSize`, and timestamp.
- LVGL `LV_EVENT_SCROLL` maps to `scroll`; `LV_EVENT_SCROLL_END` maps to
  `scrollend`.
- A Scroll event at offset `0` emits `scrolltop`; an event with no remaining
  scroll range emits `scrollbottom`.
- Scroll bindings are invalidated before native object deletion.
- A keyed-style `MoveHost` preserves the existing child Native Object pointer;
  platform code does not create or own a second list state or key map.

No Core, JS, Toolkit, public Contract, or Examples Composition Root file was
modified by this task.

## Artifact

- RPK: `/Users/qy/code/my-github/quickapp-kit-ai/quickapp-examples/showcases/list-001/dist/list-001.rpk`
- SHA-256: `f9087a6e1a9b0cc9c104a57586b6196636b8a2853d386ab68551fa2c0eb640c2`
- Size: `18826` bytes
- Runtime format: `quickapp-kit-rpk-v1`
- Page IR contains `Scroll -> List -> keyed for` and local image resource
- Page IR handlers: `scroll`, `scrollend`, `scrolltop`, `scrollbottom`, and
  keyed item `click`
- Artifact routes: `/pages/Home` only

## Verification

### LVGL platform contract

Passed:

```text
cmake --build /Users/qy/code/my-github/quickapp-kit-ai/quickapp-runtime-lvgl/build \
  --target lv_s04_mount_contract_tests -j 4
SDL_VIDEODRIVER=dummy \
  /Users/qy/code/my-github/quickapp-kit-ai/quickapp-runtime-lvgl/build/lv_s04_mount_contract_tests
```

The B3 test verifies:

- Scroll native object is vertically scrollable.
- Scroll viewport height is `80` and List content height is `240`.
- The real content range is positive (`scrollBottom > 0`).
- Scrolling produces a positive offset and `scroll` callback data with
  `contentSize > viewportSize`.
- `scrollend`, `scrolltop`, and `scrollbottom` are emitted from LVGL events.
- Moving an existing list child preserves its Native Object pointer and live
  object count; no Remove + Instantiate occurs for the move.
- Surface release leaves `liveObjectCount=0` and `liveFontCount=0`.
- Installing a handler after release is rejected.

### LVGL CTest

Passed:

```text
ctest --test-dir /Users/qy/code/my-github/quickapp-kit-ai/quickapp-runtime-lvgl/build \
  --output-on-failure -R 'lv_s04_font_profile_probe|lv_s04_mount_contract_tests|lv_s04_core_mount_integration_tests'
```

Result: `3/3 tests passed`.

The platform target build passed:

```text
cmake --build /Users/qy/code/my-github/quickapp-kit-ai/quickapp-runtime-lvgl/build \
  --target quickapp_lvgl_mount_host lv_s04_mount_contract_tests -j 4
```

### Real RPK integration attempt

The existing LVGL Simulator was run with the real Toolkit artifact:

```text
cd /Users/qy/code/my-github/quickapp-kit-ai/quickapp-examples
SDL_VIDEODRIVER=dummy ./build-m1-s2/quickapp_lvgl_simulator \
  --rpk showcases/list-001/dist/list-001.rpk
```

Observed result:

```text
case001_lvgl_error=RPK open failed: Runtime component unavailable
```

The failure occurs before Core Mount because the existing Composition Root
component list contains neither `List` nor `Scroll`. This is outside the
platform-only modification boundary, so no Examples file was changed and no
real-RPK screenshot or click result is claimed.

## Scope Limits

- The platform does not implement keyed identity, list data, routing, or page
  state. Core remains the sole authority for Runtime Tree, keyed Block identity,
  Event Router, and Navigation.
- `list-001.rpk` has no Detail page or navigation handler. Push/back, repeated
  Detail entry, and route teardown are therefore `NOT APPLICABLE` to this
  artifact, not passed claims.
- Real RPK integration status is `BLOCKED_EXTERNAL_INTEGRATION` until the
  external Composition Root registers `List` and `Scroll`.
- No second Runtime Tree, list state, router, bridge, or platform business state
  was added.

Status: `BLOCKED_EXTERNAL_INTEGRATION`

Next action outside this platform task: register `List` and `Scroll` in the
existing Composition Root and rerun the same RPK without changing this LVGL
platform implementation.
