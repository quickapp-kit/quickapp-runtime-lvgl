# LVGL B4 Platform Feature Provider Handoff

Date: 2026-08-25

## Conclusion

The LVGL platform Provider now implements the B4 local feature boundary for
prompt, deterministic fetch, and private in-memory file access. Results use the
existing typed Core Feature Contract and return `completed`, `failed`,
`unsupported`, or `cancelled` without adding a bridge or platform-specific
public ABI.

The Provider is independently verified. The real `platform-001.rpk` cannot
currently complete through the existing Examples Composition Root because that
external entry has not registered `system.file`; no Examples file was modified.

## Provider Semantics

### Prompt

- `alert` and `confirm` create an LVGL label under the supplied display root.
- `confirm` returns `completed` with `confirmed=true`.
- Empty text returns `failed/INVALID_ARGUMENT`.
- Deterministic fixture inputs `__failed__`, `__unsupported__`, and
  `__cancelled__` exercise the typed failure states without external UI or
  host dependencies.
- Toast resources are replaced per Surface and deleted on teardown.

### Fetch

- The deterministic success URL is `https://example.test/data` and returns
  `completed`, HTTP `200`, JSON body `{"ok":true}`, and `responseIsJson=true`.
- Other URLs are `unsupported` unless configured as a deterministic failure.
- Configured failures return `failed` with the configured typed error.
- Only GET is supported; other methods return `failed/FETCH_METHOD`.
- Cancellation uses the existing Core `Provider::cancel` path and returns
  `cancelled` for a marked pending request. No network socket or background
  thread is created.

### File

- File data is stored in an in-memory map keyed by `SurfaceId` and path.
- Only `private/` paths are accepted.
- Traversal (`..`), backslash, duplicate slash, empty paths, and outside-scope
  paths return `failed/FILE_PATH`.
- Read, write, exists, and delete operate on the private in-memory Provider.
- Missing reads return `failed/FILE_NOT_FOUND`; writes over 64 KiB return
  `failed/LIMIT_EXCEEDED`.
- The Provider never opens the host filesystem.

## Artifact

- RPK: `/Users/qy/code/my-github/quickapp-kit-ai/quickapp-examples/showcases/platform-001/dist/platform-001.rpk`
- SHA-256: `79ace8e7a28eeef67c31ae3cb519af7c7e3a85c8556c8ecb4811456f3a49035d`
- Size: `17171` bytes
- Runtime format: `quickapp-kit-rpk-v1`
- Entry route: `/pages/Home`
- Declared capabilities: `system.router`, `system.prompt`, `system.fetch`,
  `system.file`
- Routes: `/pages/Home` only

## Verification

### LVGL Provider tests

Passed:

```text
cmake --build /Users/qy/code/my-github/quickapp-kit-ai/quickapp-runtime-lvgl/build \
  --target lv_b4_feature_provider_tests -j 4
SDL_VIDEODRIVER=dummy \
  /Users/qy/code/my-github/quickapp-kit-ai/quickapp-runtime-lvgl/build/lv_b4_feature_provider_tests
```

The test covers:

- Prompt completed, failed, unsupported, and cancelled results.
- Deterministic fetch completed, failed, unsupported, and cancelled results.
- Private file write/read/exists/delete.
- Path traversal rejection and missing file failure.
- Surface teardown clearing prompt, pending fetch, and private file resources.
- Registry close returning typed failure rather than invoking the Provider.

Also passed:

```text
ctest --test-dir /Users/qy/code/my-github/quickapp-kit-ai/quickapp-runtime-lvgl/build \
  --output-on-failure -R 'lv_b4_feature_provider_tests|lv_s04_mount_contract_tests'
```

Result: `2/2 tests passed`.

The platform Provider target also builds:

```text
cmake --build /Users/qy/code/my-github/quickapp-kit-ai/quickapp-runtime-lvgl/build \
  --target quickapp_lvgl_feature_provider lv_b4_feature_provider_tests -j 4
```

### Real RPK attempt

Using the existing LVGL Simulator binary:

```text
cd /Users/qy/code/my-github/quickapp-kit-ai/quickapp-examples
SDL_VIDEODRIVER=dummy ./build-m1-s2/quickapp_lvgl_simulator \
  --rpk showcases/platform-001/dist/platform-001.rpk
```

Observed result:

```text
case001_lvgl_error=RPK open failed: Runtime capability unavailable: system.file
```

This occurs at the external Composition Root capability admission boundary;
the RPK declares `system.file`, while the existing entry registers only the
current prompt/device/page providers. The platform Provider itself is not
reached by this binary.

Rebuilding the external Simulator is also currently blocked by unrelated
Examples code using positional `core::feature::Request` initialization after
the Core Request fields were extended. This task does not modify that directory.

## Scope Limits

- No Core, JS, Toolkit, public Contract, or Examples Composition Root file was
  modified.
- No host filesystem, network, second registry, second route, or platform
  business state was introduced.
- `platform-001.rpk` has only `/pages/Home`; page push/back and repeated entry
  are not applicable to this artifact.

Status: `BLOCKED_EXTERNAL_INTEGRATION`

Next action outside this platform task: register the existing LVGL Provider for
`system.fetch` and `system.file` in the Composition Root and update that entry's
typed Request construction, then rerun the unchanged RPK.
