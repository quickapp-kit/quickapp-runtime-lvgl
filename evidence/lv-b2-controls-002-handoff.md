# LVGL B2 Slider + Picker Handoff

Date: 2026-08-25

## Conclusion

The LVGL platform mapping for `Slider` and text `Picker` is implemented and
passes the platform contract test. The test covers the real LVGL native objects,
range/step/value synchronization, typed Slider `change`, Picker selection,
change/confirm/cancel, and deterministic Surface teardown.

The real `controls-002.rpk` cannot currently reach LVGL Mount through the
existing Examples Composition Root: it exits before mount with
`RPK open failed: Runtime component unavailable`. The Composition Root has not
registered `Slider` and `Picker`; it is outside the allowed platform-only scope
and was not modified.

## Platform Changes

- `HostComponentType::kSlider` maps to `lv_slider_create`.
- Slider `min`, `max`, and `step` are converted to a fixed-scale native range
  (`scale=1000`) and values are quantized to the declared step.
- `value` is applied from Core as a numeric property and the native value is
  converted back to the declared logical range.
- `installSliderHandler` exposes a platform-local typed callback carrying
  `(value, isFromUser)` for `LV_EVENT_VALUE_CHANGED`.
- `HostComponentType::kPicker` maps to `lv_dropdown_create`.
- Picker `mode=text`, pipe-separated `range`, and zero-based `selected` are
  applied through LVGL dropdown APIs.
- Picker `LV_EVENT_VALUE_CHANGED` emits typed `change` followed by `confirm`;
  `LV_EVENT_CANCEL` emits typed `cancel`.
- Slider and Picker bindings are invalidated before native object deletion.
- Surface release removes native objects and bindings; live object and font
  counts return to zero.

No Core, JS, Toolkit, public Contract, or Examples Composition Root file was
modified by this task.

## Artifact

- RPK: `/Users/qy/code/my-github/quickapp-kit-ai/quickapp-examples/showcases/controls-002/dist/controls-002.rpk`
- SHA-256: `b738c890107d54f82ecf2c3f949c5df3688b6760e45d326b08f4c23de53d297a`
- Size: `16427` bytes
- Toolkit runtime metadata: `quickapp-kit-rpk-v1`, `runtimeAbi=quickapp-kit-runtime-v1`
- Routes in artifact: `/pages/Home` only
- Page IR contains one Slider with `min=0`, `max=100`, `step=5`, `value=40`
- Page IR contains one text Picker with `range=安静|标准|性能`, `selected=1`

## Verification

### LVGL platform contract

Passed:

```text
cmake --build /Users/qy/code/my-github/quickapp-kit-ai/quickapp-runtime-lvgl/build \
  --target lv_s04_mount_contract_tests -j 4
SDL_VIDEODRIVER=dummy \
  /Users/qy/code/my-github/quickapp-kit-ai/quickapp-runtime-lvgl/build/lv_s04_mount_contract_tests
```

The B2 test verifies:

- Slider native value `40` is represented by the fixed-scale native value
  `40000`.
- A native update to `45000` produces logical value `45` and
  `isFromUser=false` for a programmatic LVGL event.
- Picker initial selection is index `1` and value `标准`.
- Picker confirm produces `change` then `confirm`.
- Updating selection to index `2` produces value `性能`.
- Picker cancel produces `cancel`.
- After Surface release, Slider/Picker callbacks cannot be invoked and
  `liveObjectCount=0`, `liveFontCount=0`.

### LVGL CTest

Passed:

```text
ctest --test-dir /Users/qy/code/my-github/quickapp-kit-ai/quickapp-runtime-lvgl/build \
  --output-on-failure -R 'lv_s04_font_profile_probe|lv_s04_mount_contract_tests|lv_s04_core_mount_integration_tests'
```

Result: `3/3 tests passed`.

The full LVGL build also passed:

```text
cmake --build /Users/qy/code/my-github/quickapp-kit-ai/quickapp-runtime-lvgl/build -j 4
```

### Real RPK integration attempt

Both existing LVGL entry points were built successfully:

```text
cd /Users/qy/code/my-github/quickapp-kit-ai/quickapp-examples
cmake --build build-m1-s2 -j 4 \
  --target quickapp_case001_lvgl quickapp_lvgl_simulator
```

The real RPK was then loaded through the existing Composition Root:

```text
SDL_VIDEODRIVER=dummy ./build-m1-s2/quickapp_case001_lvgl \
  --rpk showcases/controls-002/dist/controls-002.rpk
SDL_VIDEODRIVER=dummy ./build-m1-s2/quickapp_lvgl_simulator \
  --rpk showcases/controls-002/dist/controls-002.rpk
```

Both exited with:

```text
case001_lvgl_error=RPK open failed: Runtime component unavailable
```

This failure occurs before Core Mount and is caused by the external
Composition Root component registry not containing `Slider` and `Picker`.
The platform implementation therefore has no truthful real-RPK screenshot or
event result to claim in this platform-only task.

## Scope Limits

- `controls-002.rpk` has no Detail route or navigation handler, so push/back and
  repeated page-entry cannot be verified from this artifact.
- Real RPK integration is blocked before LVGL Mount by Examples Composition Root
  registration, which is outside the allowed modification directory.
- No second Runtime Tree, router, bridge, or platform business state was added.

Status: `BLOCKED_EXTERNAL_INTEGRATION`

Next action outside this platform task: register `Slider` and `Picker` in the
existing Composition Root and rerun the same RPK without changing this LVGL
platform contract.
