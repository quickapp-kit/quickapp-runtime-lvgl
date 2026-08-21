# QuickApp Runtime LVGL

LVGL platform adapter for [QuickApp Kit](https://github.com/quickapp-kit). Targets embedded/wearable devices using [LVGL](https://lvgl.io/) as the rendering backend.

## What's here

This layer sits between the platform-independent Core and actual hardware (or an SDL simulator):

- **Foundation** — owner-thread task queue, monotonic clock, wakeup ports, backend lifecycle
- **Runtime Host** — composition root, lifecycle control, package source, trace adapter
- **Surface Host** — LVGL display surface management, page root backend
- **Mount Host** — LVGL object mount, component creation, Core render bridge
- **Font Measure** — font metrics and text measurement for layout
- **Backends** — SDL3 + libuv (simulator) / callback-based (embedded)

No dynamic allocation beyond startup. No RTTI, no exceptions in foundation code.

## Requirements

- C++20 compiler
- CMake 3.24+
- Sibling repos: `quickapp-runtime-core`, `quickapp-runtime-js`
- Simulator: SDL3, libuv (via pkg-config)
- Embedded: none beyond the C++ toolchain

## Build

```bash
# Default (simulator + all modules)
cmake -S . -B build -G Ninja
cmake --build build
ctest --test-dir build --output-on-failure

# Embedded-only (no SDL/libuv dependency)
cmake -S . -B build-embedded -G Ninja -DQUICKAPP_LVGL_BUILD_SIMULATOR=OFF
cmake --build build-embedded
ctest --test-dir build-embedded --output-on-failure
```

### Sanitizer builds

```bash
cmake -S . -B build-asan -G Ninja -DQUICKAPP_LVGL_ENABLE_ASAN=ON
cmake --build build-asan -j
ctest --test-dir build-asan --output-on-failure

cmake -S . -B build-tsan -G Ninja -DQUICKAPP_LVGL_ENABLE_TSAN=ON
cmake --build build-tsan -j
ctest --test-dir build-tsan --output-on-failure
```

## Project Structure

```
├── include/quickapp/lvgl/    # Public headers
├── src/
│   ├── backends/             # SDL3/libuv simulator + embedded backends
│   ├── surface/              # LVGL surface host
│   ├── mount/                # LVGL mount host
│   ├── measure/              # Font measure
│   ├── event/                # Input adapter
│   └── integration/          # Core mount bridge
├── fakes/                    # Test doubles
├── tests/                    # Contract tests & probes
├── cmake/                    # Boundary check scripts
└── evidence/                 # Verification docs
```

## Related

- [quickapp-runtime-core](https://github.com/quickapp-kit/quickapp-runtime-core) — C++ runtime kernel
- [quickapp-runtime-js](https://github.com/quickapp-kit/quickapp-runtime-js) — JS engine layer
- [quickapp-runtime-android](https://github.com/quickapp-kit/quickapp-runtime-android) — Android adapter

## License

[MIT](LICENSE)
