# LVGL B1 Input + Switch Handoff

Date: 2026-08-25

## Conclusion

The LVGL Host implementation for `Input` and `Switch` is complete at the platform Mount boundary. The local LVGL contract test passes for creation, initial value/checked state, focus/input/change delivery, typed Switch `change(bool)`, Surface release, and deterministic teardown.

The external Composition Root registration has since been completed. The real `controls-001.rpk` now loads through Core and reaches LVGL Mount. This handoff records only the LVGL implementation and verification; the Composition Root change is outside this platform directory.

## Platform Changes

- `MountHost::CreateHost` maps `HostComponentType::kSwitch` to `lv_switch_create`.
- `checked` maps to `LV_STATE_CHECKED` and `enabled` maps to `LV_STATE_DISABLED` for Switch.
- `installSwitchHandler` exposes a platform-local typed callback carrying `bool checked` for LVGL `LV_EVENT_VALUE_CHANGED`.
- Existing Input mapping remains `lv_textarea` with `value`, `input`, `change`, and `focus` callbacks.
- Input and Switch bindings are invalidated before LVGL object deletion.
- Surface release removes native objects, event bindings, and font references; full close remains deterministic.

No Core, JS, Toolkit, public Contract, or Examples Composition Root file was modified by this task.

## Artifact

- RPK: `/Users/qy/code/my-github/quickapp-kit-ai/quickapp-examples/showcases/controls-001/dist/controls-001.rpk`
- SHA-256: `1e25a27daf59e5ae2f6b0bd046a7e9c4fe2876cfcd4dcebea2e26a7ed7f829`
- Size: `16773` bytes
- Source IR contains one route, `/pages/Home`, with real `Input`, `Switch`, and Button handlers.

## Verification

### LVGL platform contract

Passed:

```text
cmake --build /Users/qy/code/my-github/quickapp-kit-ai/quickapp-runtime-lvgl/build \
  --target lv_s04_mount_contract_tests -j 4
SDL_VIDEODRIVER=dummy \
  /Users/qy/code/my-github/quickapp-kit-ai/quickapp-runtime-lvgl/build/lv_s04_mount_contract_tests
```

The test verifies:

- Initial Input value is `QuickApp`.
- Input focus, input, and change callbacks are delivered; change value is `Changed`.
- Switch starts checked and emits one typed `change(false)` callback after unchecking.
- Surface release leaves `liveObjectCount=0` and `liveFontCount=0`.
- Explicit close succeeds after release.

### Existing RPK regression

Passed with exit code `0`:

```text
SDL_VIDEODRIVER=dummy ./build-m1-s2/quickapp_case001_lvgl --case-002
SDL_VIDEODRIVER=dummy ./build-m1-s2/quickapp_case001_lvgl --binding-001
```

Both report `resources_released=true`.

### LVGL CTest

The B1-relevant suite passed `10/10`:

```text
ctest --test-dir /Users/qy/code/my-github/quickapp-kit-ai/quickapp-runtime-lvgl/build \
  --output-on-failure -R 'lv_s01|lv_s03|lv_s04|lv_s06'
```

This includes `lv_s04_mount_contract_tests`, `lv_s04_core_mount_integration_tests`, font profile, boundary scans, and S01/S03/S06 regressions. The unrelated full-suite `lv_s02_contract_tests` libuv/display initialization failure remains outside B1.

### Real controls RPK launch

Attempted:

```text
cd /Users/qy/code/my-github/quickapp-kit-ai/quickapp-examples
SDL_VIDEODRIVER=dummy ./build-m1-s2/quickapp_lvgl_simulator \
  --rpk showcases/controls-001/dist/controls-001.rpk
```

Result after the Composition Root registration fix:

```text
phase=rpk_opened
platform.mount.complete ... mounted=1 ... revision=0
b1.controls.handlers input=node:4 switch=node:7 button=node:9
lvgl.input.dispatch type=focus ... accepted=1
lvgl.input.dispatch type=input ... accepted=1
lvgl.input.dispatch type=change ... accepted=1
lvgl.switch.dispatch event=change checked=0 ... accepted=1
b1.controls input_events=3 switch_event=change payload.checked=0 js_handler=onSwitch state_written=1 resources=stable
rpk.controls001=true
event.input=true
event.switch_change=true
event.switch_payload_checked=false
event.js_handler_onSwitch=true
state.checked_written=true
resources_released=true
```

The RPK was built by Toolkit and contains one real `Input`, one real `Switch`, and five handlers. The platform event path is `LVGL -> platform callback -> Core EventRouter -> JS handler`.

Interactive Simulator run also passed startup and teardown:

```text
SDL_VIDEODRIVER=dummy ./build-m1-s2/quickapp_lvgl_simulator \
  --rpk showcases/controls-001/dist/controls-001.rpk
```

It reported `simulator.ready`, `b1.controls.handlers`, then on close `simulator.closed=true` and `resources_released=true`.

## Scope Limits

- `controls-001.rpk` has no Detail route or router handler, so push/back and repeated page-entry cannot be truthfully verified with this artifact.
- No push/back result is claimed because this exact RPK has only `/pages/Home` and no Detail or router handler.
- No second tree, router, or bridge was added.

Status: `READY_FOR_ARCH_REVIEW`
