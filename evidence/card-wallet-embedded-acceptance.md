# Card Wallet Embedded Acceptance

## Conclusion

The regenerated `card-wallet.rpk` passes the LVGL `220x220` small-screen runtime path. The real package loads, all three image resources mount, the Home page becomes visible, the Detail route can be entered and closed repeatedly, and final teardown returns Runtime, JS, Surface, Handler, Mount, Font, and Feature resources to zero.

The observed white-screen failure was in the Simulator Composition Root, not Core, Router, Runtime Tree, Toolkit, resource registration, or LVGL Mount. The entry omitted the packaged Framework module and assumed fixed Handler IDs that do not match Card Wallet.

## Package

- Path: `/Users/qy/code/my-github/quickapp-kit-ai/quickapp-examples/showcases/card-wallet/dist/card-wallet.rpk`
- SHA-256: `285df62d8a0c0cea70f929feb40defec612c316177ed1a2ebfa975f5add89ded`
- Runtime metadata: `META-INF/runtime.json`
- Pages: `pages/Home/*`, `pages/Detail/*`
- Shared module: `shared/_quickapp-kit_framework-v1.js`
- Assets: `card-door.png`, `card-transit.png`, `card-work.png`

## Commands

```text
cd /Users/qy/code/my-github/quickapp-kit-ai/quickapp-examples

cmake --build build-m1-s2 -j 4 \
  --target quickapp_case001_lvgl quickapp_lvgl_simulator

SDL_VIDEODRIVER=dummy ./build-m1-s2/quickapp_case001_lvgl \
  --rpk showcases/card-wallet/dist/card-wallet.rpk \
  --viewport 220x220 \
  --zoom 1.0

./build-m1-s2/quickapp_lvgl_simulator \
  --rpk showcases/card-wallet/dist/card-wallet.rpk \
  --viewport 220x220 \
  --zoom 2.0
```

## Interaction Results

- Home became visible on `srf:1` with 20 mounted Platform objects.
- Three packaged PNG resources loaded successfully.
- The real page-level Detail Handler `hdl:2` entered `/pages/Detail` through Core Router.
- Detail mounted successfully on fresh Surfaces `srf:2`, `srf:3`, and `srf:4`.
- The real Detail back Handler `hdl:2` closed each Detail Surface and revealed Home.
- Three repeated Detail cycles completed with three valid Image mounts.
- Invalid and late Handler input remained rejected.
- The interactive SDL window remained open until an explicit close request.

## Teardown

```text
resources.before_cleanup surfaces=0 nodes=0 handlers=0 live_surface=0 mount_objects=0 roots=0
resources.feature_after_cleanup providers=0
resources.js_after_cleanup handlers=0 module_entries=0 page_leases=0 active_loads=0 module_bytes=0 module_pending=0 app_vms=0 page_vms=0 vm_surfaces=0 abi_entries=0 abi_correlations=0 abi_consumers=0 abi_surfaces=0 abi_callbacks=0 page_entries=0 page_factories=0 queue_depth=0
simulator.closed=true
resources_released=true
```

## Root Cause

1. The old RPK used `quickapp-kit/runtime.json`; the regenerated package correctly uses `META-INF/runtime.json`.
2. The Simulator did not preload the packaged `@quickapp-kit/framework-v1`, so Page initialization could not complete.
3. The Simulator bound `hdl:1` and block handlers only. Card Wallet uses page Handler `hdl:2` for Home Detail and Detail Back.
4. Resource registration and LVGL Mount were healthy once the entry wiring was corrected.

## Remaining Issues

- This RPK uses a fixed `220x220` root View and its content fits the viewport. It does not create an overflowing Scroll container, so meaningful vertical scrolling is not covered by this package.
- No screenshot was retained; structured runtime interaction logs are the acceptance evidence.
- One immediate close directly after startup produced incomplete cleanup output. Repeating the close after the runtime reached steady interactive state completed with exit code `0` and all resources at zero.
