# Wearable Viewport & Shape Verification

Date: 2026-08-27
Build: quickapp-examples/build-m1-s2 (Debug, -Wall -Wextra -Wpedantic -Werror)

## Changes

### Task 1: Simulator viewport parameterization + round clipping

File: `quickapp-examples/composition/case001_lvgl.cpp`

- `--viewport <W>x<H>`: override logical viewport (default 720x1280 for interactive, 320x240 for headless)
- `--shape round|rect`: when `round`, apply `lv_obj_set_style_radius(screen, LV_RADIUS_CIRCLE, 0)` + `clip_corner=true` + black background
- `--zoom` continues to apply on SDL window size (SDL window = viewport * zoom)

### Task 2: scroll/list container LVGL mount

File: `quickapp-runtime-lvgl/src/mount/mount_host.cpp`

- `list` now treated as semantic alias of `scroll`: creates `lv_obj` with `scrollable=true`, `LV_DIR_VER`, `LV_SCROLLBAR_MODE_AUTO`
- Post-creation `resetViewChrome` block applies scroll settings to both `kScroll` and `kList`
- `installScrollHandler` already accepts both types (no change needed)

## Verification Evidence

### 1. Gallery Showcase Regression (default viewport)

```
SDL_VIDEODRIVER=dummy ./quickapp_case001_lvgl \
  --rpk showcases/gallery-001/dist/gallery-001.rpk
```

```
simulator.ready display=320x240 shape=rect
rpk.opened=true
event.real_lvgl_button=true
event.sdl_pointer_attached=true
event.dispatch.once=true
navigation.core_stack=true
resources_released=true
exit=0
```

### 2. Wearable Round Watch (--viewport 240x240 --shape round)

```
SDL_VIDEODRIVER=dummy ./quickapp_case001_lvgl \
  --rpk showcases/wearable-001/dist/wearable-001.rpk \
  --viewport 240x240 --shape round
```

```
simulator.shape=round viewport=240x240
simulator.ready display=240x240 shape=round
rpk.opened=true
event.real_lvgl_button=true
event.sdl_pointer_attached=true
event.dispatch.once=true
navigation.core_stack=true
resources_released=true
exit=0
```

### 3. Band Viewport (--viewport 194x368, rect)

```
SDL_VIDEODRIVER=dummy ./quickapp_case001_lvgl \
  --rpk showcases/gallery-001/dist/gallery-001.rpk \
  --viewport 194x368
```

```
simulator.ready display=194x368 shape=rect
rpk.opened=true
resources_released=true
exit=0
```

### 4. lv_s04 CTest Regression

```
cd quickapp-runtime-lvgl/build-s04-debug && ctest -R "lv_s04"
```

```
1/4 Test #14: lv_s04_font_profile_probe .............   Passed
2/4 Test #15: lv_s04_mount_contract_tests ...........   Passed
3/4 Test #16: lv_s04_core_mount_integration_tests ...   Passed
4/4 Test #18: lv_s04_boundary_scan ..................   Passed
100% tests passed out of 4
```

### 5. Consumer Showcase Regression

```
SDL_VIDEODRIVER=dummy ./quickapp_case001_lvgl \
  --rpk showcases/consumer-001/dist/consumer-001.rpk
```

```
resources_released=true
exit=0
```

## Completion Criteria

| Criterion | Status |
|-----------|--------|
| `--viewport` and `--shape` arguments functional | PASS |
| Round clipping applied in SDL window | PASS |
| Gallery showcase regression exit 0 | PASS |
| Consumer showcase regression exit 0 | PASS |
| Wearable showcase regression exit 0 | PASS |
| lv_s04 CTest suite passes | PASS |
| list treated as scroll semantic alias | PASS |
