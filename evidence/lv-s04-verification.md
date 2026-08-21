# LV-S04 Verification Evidence

## 目录

- [1. 结论](#1-结论)
- [2. 实现摘要](#2-实现摘要)
- [3. 验收映射](#3-验收映射)
- [4. 线程与资源](#4-线程与资源)
- [5. 配置矩阵](#5-配置矩阵)
- [6. 范围边界](#6-范围边界)
- [7. 可复现命令](#7-可复现命令)

## 1. 结论

LV-S04 的 M1-Alpha Mount Adapter 已完成：真实 LVGL/SDL window、S03 page root 和 View/Text/Button object 路径通过；full Mount、incremental Move/Remove、属性/布局、Present 后 visible、非法事务、背压、owner-thread 和显式 close 均有运行时证据。

新增联调证明 Core `MountCoordinator` 产生的 typed `MountTransaction` 可经 `CoreMountBridge` 到达 owner-thread S04 Mount；Mount 成功后只由 S03 执行 Present，最后才以 typed Result 回送 Core。Trace 包含 Core 的 Mount/Render 事实和 Platform 的 `platform.present.requested/completed`；close 后 Host object、Surface root、pending transaction 和 owner task 均归零。

本轮已完成 `fontSize` 与 LV-S06 `system-default/400` 合同的定向接线：40px 和 30px 均由仓库内编译期内置的 CJK 字体资产创建 LVGL 字体实例；不支持的属性仍显式失败。

本证据仍不宣称完整 Case 001 S1 通过：联调 Page IR 是最小 Core 测试数据，不是 Toolkit 产出的真实 Runtime RPK；真实 RPK 解包、JS 执行和 Case 001 完整样式仍在跨项目组装中。完整真实 RPK 链路边界保持不变。

## 2. 实现摘要

| 区域 | 文件 | 责任 |
|---|---|---|
| Typed Mount data | `include/quickapp/lvgl/mount/mount_types.h`, `src/mount/mount_types.cpp` | bounded transaction、View/Text/Button operation、typed result/sourceId |
| Mount gateway | `include/quickapp/lvgl/mount/mount_host.h`, `src/mount/mount_host.cpp` | fixed slot、owner-scoped `(SurfaceId, NodeId)` mapping、preflight、owner task、commit、failure cleanup、close |
| LVGL adapter | `include/quickapp/lvgl/mount/lvgl_mount_backend.h`, `src/mount/lvgl_mount_backend.cpp` | 只把 S03 opaque page root 交给 Mount Host；公共头不泄漏 LVGL |
| S03 integration | `include/quickapp/lvgl/surface/lvgl_page_root_backend.h`, `src/surface/lvgl_page_root_backend.cpp` | 提供 owner-only page-root native lookup |
| Real Host test | `tests/lv_s04_mount_contract_tests.cpp` | SDL window、S03 root、View/Text/Button、negative/backpressure/close |
| Core mount bridge | `include/quickapp/lvgl/integration/core_mount_bridge.h`, `src/integration/core_mount_bridge.cpp` | Core typed transaction 有界转换、Mount -> S03 Present -> typed Core Result、platform present Trace |
| Core integration test | `tests/lv_s04_core_mount_integration_tests.cpp` | Core `MountCoordinator` 真实生成 transaction 后的 LVGL/SDL Mount/Present/visible/close |
| Font asset | `assets/fonts/NotoSansSC-Alpha.ttf`, `assets/fonts/system-default-cjk-glyphs.txt`, `src/font/system_default_font_asset.cpp` | 声明、编译期内置并由 Simulator/Embedded 共用的 CJK `system-default/400` 字节 |
| Font/Measure probe | `tests/lv_s04_font_profile_probe.cpp` | 双 Profile 使用同一资产、实际 CJK glyph、fontSize=40 度量和 LVGL 内存归零 |
| Boundary scan | `cmake/check_lv_s04_boundaries.cmake` | public mount header leakage scan |

本地上限：`lvgl-simulator-dev = 16 transactions / 512 Host objects / 64 operations`；`lvgl-embedded-min = 4 / 64 / 16`。Button private label 计入 LVGL object 资源事实，不进入 NodeId 映射。

## 3. 验收映射

### 3.1 功能

| Case | 证据 | 结果 |
|---|---|---|
| A01 real root + components | `testCase001VisibleAndResources` + real `lv_sdl_window_create` | 真实 LVGL 创建 View/Text/Button；Mount Result=`mounted`；3 个 Runtime Node、4 个 LVGL object（含 Button label）。 |
| A02 full sequence | 同一测试的 Create/Set/Insert 序列；`preflight` | full 只接受 Create/Set/Insert，子树最终挂接到 root。 |
| A03 props/layout/fontSize | 同一测试的 text/enabled/backgroundColor/color/borderRadius/textAlign/layout/fontSize | View/Text/Button 的既有属性和 logical-px layout 通过；Text=40px、Button=30px 映射到同一 `system-default/400` 内置字体。 |
| A04 Move | 同一测试的 `MoveHost(button, root, 0)` | 事务成功；对象计数和本地映射数量保持不变。 |
| A05 Remove | 同一测试的 `RemoveHost(title)` | title 被删除；递归清理路径保持 Node/object 计数一致。 |
| A06 Present/visible | S03 Present 后 `lv_obj_is_hidden(root)==false` | Mount 不自动 Present；S03 Present 成功后 root visible。 |
| A07 invalid transaction | `testMountRejectionBackpressureAndClose` | full Move、未知 property 返回 failed，未产生残留 object。 |
| A08 close | 同一测试 close/finishClose | close 拒绝新 post；pending drain 后 live/pending 为零，析构前显式关闭成立。 |
| A09 Core typed integration | `lv_s04_core_mount_integration_tests` | Core `MountCoordinator` 生成 transaction；S04 owner-thread Mount 后 S03 Present；typed Result 回到 Core，root visible，资源归零。 |
| A10 CJK visible/measure | `lv_s04_mount_contract_tests`, `lv_s04_core_mount_integration_tests`, `lv_s04_font_profile_probe` | `欢迎体验快应用开发`、`跳转到详情页` 实际挂载；`中` glyph descriptor 存在；40px 实际 glyph advance 与 S06 Q26.6 width 一致，height 仅有小于 1px 的 LVGL 整数像素量化差。 |

### 3.2 队列、线程和失败

| Case | 证据 | 结果 |
|---|---|---|
| N01 owner thread | 非 owner `service` 与 `finishClose` 断言 | 返回 `kWrongThread`，不执行 LVGL。 |
| N02 bounded admission | 16 transaction slots + 第 17 次 post | 第 17 次立即失败，未动态扩容或阻塞。 |
| N03 close while pending | close 后 finishClose，再 owner pump | pending 时返回 `kBusy`；完成 owner work 后才 close。 |
| N04 post after close | close 后 `post` 断言 | 明确拒绝，无新 task 或 Result。 |
| N05 failed cleanup | 非法 full/property 事务 | failed Result 后 live object 为零。 |
| N06 source correlation | Mount Result `source_id` 断言 | `surfaceId/revision/mountAttemptId/sourceId` 事实随 Result 保留。 |
| N07 public boundary | `lv_s04_boundary_scan` | public mount headers 不含 LVGL、SDL、libuv、RuntimeTreeStore、Revision。 |
| N08 sanitizer/thread | ASan/UBSan、TSan S04 tests | fixture 与 Core typed integration 均未发现地址/未定义行为/数据竞争。 |
| N09 resource convergence | S04 close assertions + S03 Surface close + task depth | object、mapping、transaction slot、task depth 归零。 |
| N10 profile boundary | CMake `embedded-only` + isolated probe | 最小组合不链接 SDL/libuv backend；MountHost 仍可编译。 |
| N11 no hidden authority | source scan + code inspection | 没有 Runtime Tree、Revision、route stack 或 Layout owner。 |
| N12 no later-stage scope | source scan + CMake target graph | 未新增 Input/Event、Measure、Capability、Collector、RPK Loader 或 S08-S10 产品代码。 |

### 3.3 Font contract and asset evidence

| 项目 | 固定事实 | 证据 |
|---|---|---|
| 字体身份 | token=`system-default`、weight=400、digest=`5d99238d1f9493227eeaf535e5f9d93634bd177c7b032fb171d69e96a9969f71` | `include/quickapp/lvgl/font/system_default_font_asset.h`、`src/font/system_default_font_asset.cpp`、`assets/fonts/README.md` |
| 字体资产 | `assets/fonts/NotoSansSC-Alpha.ttf`，约 47 KiB，来自仓库内 LVGL Noto Sans SC TrueType 原始资产；只包含声明的 ASCII 与 Case 001 CJK 字符 | `assets/fonts/system-default-cjk-glyphs.txt`、CMake `quickapp_lvgl_font_asset` |
| fontSize 映射 | 1..256 的整数字号；当前 Case 001 使用 40px/30px；固定容量字体表按字号复用 | `MountHost::acquireFont`、S04 mount contract test |
| 线程 | 字体创建、LVGL style 赋值和释放全部在 Mount owner thread | `MountHost::service`、owner-thread assertions |
| Measure 一致性 | 同一 digest；40px 的 `中` 实际 advance 与 S06 `FontMeasureAdapter` width 相等；height 差值小于 1px | mount contract test、font profile probe |
| 资源 | 删除 title、full rebuild、Surface close、Host close 后 `liveFontCount` 和 LVGL allocator free bytes 回到基线 | S04 contract test、Simulator/Embedded profile probe |

当前资产是 V1 Alpha 的声明子集，不宣称覆盖任意 Unicode CJK。超出资产声明范围的字符覆盖扩展属于后续资产版本，不使用系统字体兜底。

### 3.4 Multi-Surface isolation

`testCase001VisibleAndResources` 创建两个独立 Surface：第二页挂载后，对第一页执行 full rebuild；断言第二页 object 仍存在、全局 object 数量只减少第一页旧对象、Result 的 `live_objects` 只统计目标 Surface。这冻结了 Platform 本地映射的身份域为 `(SurfaceId, NodeId)`，避免一个页面的 full/recovery 清理其他页面。

### 3.5 不属于 S04 的输入/Backend 项

以下项目由既有或后续分 Spec 拥有，本轮不重复实现：

| 项目 | Owner | S04 结论 |
|---|---|---|
| P01-P06 Package/RPK source and bytes | LV-S02/Core/Loader | 不适用；S04 只接受 typed MountTransaction。 |
| B01-B06 SDL/libuv/embedded loop composition | LV-S01/LV-S02 | 不适用；S04 只使用 S02 已装配的真实 LVGL/SDL Host。 |

## 4. 线程与资源

1. Producer 只能把 immutable-by-convention typed transaction 放入 bounded slot；Producer 不读取 LVGL 指针。
2. `service(owner,budget)` 才能执行 `lv_obj_*`、HostSlot 映射、ResultSink 和 close 资源操作。
3. HostSlot 的身份是 `(SurfaceId, NodeId)`；full 先清空目标 Surface 的 Host object/mapping，再按有序操作创建；任何提交失败都返回 failed，不跳过坏操作。
4. incremental Remove 通过 LVGL parent chain 收集并清理后代；Move 保留 native object 和后代映射。
5. `close()` 只关闭 admission；`finishClose(owner)` 在 pending 为零后执行确定销毁；析构断言 closed/live/pending 全部满足。
6. 公共 Mount header 不含平台对象；LVGL 类型只在 implementation/test 与已存在的 concrete page-root backend 中出现。

## 5. 配置矩阵

| 配置 | 执行内容 | 结果 |
|---|---|---|
| Debug | 全量 S01/S02/S03/S04/S06 CTest，含 font profile probe | 14/14 PASS |
| Release | 全量 S01/S02/S03/S04/S06 CTest，含 font profile probe | 14/14 PASS |
| ASan/UBSan | 全量 S01/S02/S03/S04/S06 CTest，含 font profile probe | 14/14 PASS |
| TSan | 全量 S01/S02/S03/S04/S06 CTest，含 font profile probe | 14/14 PASS |
| embedded-only | S01/S02 embedded/S03/S04 boundary/font profile/S06，SDL/libuv 关闭 | 9/9 PASS；同一字体资产和度量通过 |

embedded-only 额外扫描：`nm -u lv_s02_embedded_isolated_probe`、`nm -gU` 和 `otool -L` 未发现 SDL/libuv 符号或动态依赖；`build-s04-embedded` 未生成 simulator backend target。

## 6. 范围边界

- 不复制 Core Runtime Tree、Revision、Route/Navigation 或 Layout authority。
- 不实现 LV-S05 Input/Event、LV-S06 Measure、LV-S07 Capability、LV-S08 Full Runtime、LV-S09 Collector、LV-S10 Case Integration。
- 不新增公共合同，不创建 Alpha 专用 Runtime。
- Mount Result 的成功只表示 Host object 已落地；Core 必须继续通过 S03 Present 后才能提交 visible。
- `CoreMountBridge` 只转换已有 Core typed transaction、协调 Present 和回传结果；不读取 RPK、不执行 JS、不保存 Runtime Tree。
- `CoreMountBridge` 当前的 Core 联调输入是 typed Page IR fixture，不冒充 Toolkit 产出的完整真实 RPK；本轮新增的是字体映射和 CJK Host/Measure 证据。
- 当前 S1 证明真实 LVGL/SDL mount/present/visible 与 Core typed transaction 组合；完整真实 RPK -> JS -> Core -> Mount 仍需跨项目证据，不在此处冒充完成。

## 7. 可复现命令

在 `/Users/qy/code/my-github/quickapp-kit-ai/quickapp-runtime-lvgl` 执行：

```sh
cmake -S . -B build-s04-debug \
  -DQUICKAPP_LVGL_BUILD_TESTS=ON \
  -DQUICKAPP_LVGL_BUILD_S04=ON \
  -DQUICKAPP_LVGL_BUILD_SIMULATOR=ON
cmake --build build-s04-debug -j2
ctest --test-dir build-s04-debug --output-on-failure

cmake -S . -B build-s04-release \
  -DQUICKAPP_LVGL_BUILD_TESTS=ON \
  -DQUICKAPP_LVGL_BUILD_S04=ON \
  -DQUICKAPP_LVGL_BUILD_SIMULATOR=ON \
  -DCMAKE_BUILD_TYPE=Release
cmake --build build-s04-release --target lv_s04_mount_contract_tests \
  lv_s04_core_mount_integration_tests -j2
ctest --test-dir build-s04-release -R 'lv_s04_' --output-on-failure

cmake -S . -B build-s04-asan-sim \
  -DQUICKAPP_LVGL_BUILD_TESTS=ON \
  -DQUICKAPP_LVGL_BUILD_S04=ON \
  -DQUICKAPP_LVGL_BUILD_SIMULATOR=ON \
  -DQUICKAPP_LVGL_ENABLE_ASAN=ON
cmake --build build-s04-asan-sim --target lv_s04_mount_contract_tests \
  lv_s04_core_mount_integration_tests -j2
ASAN_OPTIONS=abort_on_error=1 SDL_VIDEODRIVER=dummy \
  ./build-s04-asan-sim/lv_s04_mount_contract_tests
ctest --test-dir build-s04-asan-sim -R 'lv_s04_' --output-on-failure

cmake -S . -B build-s04-tsan \
  -DQUICKAPP_LVGL_BUILD_TESTS=ON \
  -DQUICKAPP_LVGL_BUILD_S04=ON \
  -DQUICKAPP_LVGL_BUILD_SIMULATOR=ON \
  -DQUICKAPP_LVGL_ENABLE_TSAN=ON
cmake --build build-s04-tsan --target lv_s04_mount_contract_tests \
  lv_s04_core_mount_integration_tests -j2
TSAN_OPTIONS=halt_on_error=1 SDL_VIDEODRIVER=dummy \
  ./build-s04-tsan/lv_s04_mount_contract_tests
ctest --test-dir build-s04-tsan -R 'lv_s04_' --output-on-failure

cmake -S . -B build-s04-embedded \
  -DQUICKAPP_LVGL_BUILD_TESTS=ON \
  -DQUICKAPP_LVGL_BUILD_S04=ON \
  -DQUICKAPP_LVGL_BUILD_SIMULATOR=OFF \
  -DCMAKE_BUILD_TYPE=MinSizeRel
cmake --build build-s04-embedded -j2
ctest --test-dir build-s04-embedded --output-on-failure
! nm -u build-s04-embedded/lv_s02_embedded_isolated_probe | rg -i 'SDL|uv_'
! otool -L build-s04-embedded/lv_s02_embedded_isolated_probe | rg -i 'SDL|uv'

{
  printf '%s\n' CMakeLists.txt README.md lv_conf.h
  find include src fakes tests cmake assets -type f
  printf '%s\n' evidence/lv-s01-verification.md \
    evidence/lv-s02-verification.md evidence/lv-s03-verification.md \
    evidence/lv-s04-verification.md evidence/lv-s06-verification.md
} | LC_ALL=C sort -u | xargs shasum -a 256 \
  > evidence/source-manifest.sha256
shasum -a 256 -c evidence/source-manifest.sha256
```
