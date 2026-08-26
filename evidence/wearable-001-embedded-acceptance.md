# Wearable-001 LVGL Embedded Platform Acceptance

日期：2026-08-26

## 输入

- RPK：`/Users/qy/code/my-github/quickapp-kit-ai/quickapp-examples/showcases/wearable-001/dist/wearable-001.rpk`
- SHA-256：`ba62accc70a63ba73b3154db85bf2e613eea3153b936d49e988cfaef97fdba38`
- 大小：`37243` bytes
- 视口合同：`220x220` logical px
- 资源：`task-main.png` 1720 bytes，`task-review.png` 1996 bytes

## 已验证事实

真实 RPK 自动入口：

```text
cd /Users/qy/code/my-github/quickapp-kit-ai/quickapp-examples
SDL_VIDEODRIVER=dummy ./build-m1-s2/quickapp_case001_lvgl \
  --rpk showcases/wearable-001/dist/wearable-001.rpk
```

结果：退出码 `0`，`rpk.loader=true`，`rpk.lvgl_mount=true`，`resources_released=true`。

- RPK 打开成功，两个本地 Image 资源均加载成功。
- Home Page IR 的根 View 为 `220x220`，padding 为 `10`，符合小屏安全区设计。
- Home 包含 Text、Button、Image、Scroll、List；列表 block 为 keyed `for`。
- Home 包含 if block；初始 Mount 记录 `25` 个 Runtime/Platform nodes、`6` 个 handlers。
- Initial RenderTransaction/Mount 完成：`revision=0`、`mounted=1`、`surface=srf:1`。
- teardown 前：`surfaces=1 nodes=25 handlers=6 mount_objects=25 roots=1`。
- teardown 后：`surfaces=0 nodes=0 handlers=0 mount_objects=0 roots=0`。
- JS 资源清理后：`handlers=0 module_entries=0 page_leases=0 active_loads=0 module_bytes=0 page_vms=0 vm_surfaces=0 abi_entries=0 abi_correlations=0 abi_consumers=0 abi_surfaces=0 abi_callbacks=0 page_entries=0 page_factories=0 queue_depth=0`。

## 交互验收状态

当前自动入口对最终 Showcase 使用 `mount_only` 模式，完成真实 RPK Loader、初始 Mount 和 teardown；它没有执行点击、路由 push/back 或重复进入详情。因此以下项目不能标记为真实交互通过：

- SDL 点击进入详情：未验证；
- Detail 返回 Home：未验证；
- 重复进入 Detail：未验证；
- Timer 驱动的持续 SDL 交互：未验证；
- 状态更新后的可见 if/keyed for 变化：已从真实 Page IR 和初始 Mount 证明结构存在，但未完成点击后视觉验证。

## Simulator

尝试命令：

```text
cd /Users/qy/code/my-github/quickapp-kit-ai/quickapp-examples
./build-m1-s2/quickapp_lvgl_simulator \
  --rpk showcases/wearable-001/dist/wearable-001.rpk --zoom 1.0
```

结果：`case001_lvgl_error=SDL display creation failed`。当前执行环境没有可用的真实 SDL GUI；使用 `SDL_VIDEODRIVER=dummy` 时不能产生窗口，因此没有合法 Simulator 截图，也没有伪造截图路径。

## 构建

```text
cd /Users/qy/code/my-github/quickapp-kit-ai/quickapp-runtime-lvgl
cmake --build build -j 4
```

结果：通过。未修改 Core、JS、Toolkit、公共 Contract、Examples Composition Root 或其他平台；本轮只新增本平台 evidence。

## 资源失败清理

本 RPK 的两个本地资源加载成功；初始 Mount 和 teardown 资源均归零。资源加载失败的专用负例由既有 LVGL Mount 回归覆盖，本轮未修改失败语义，也未引入大图、网络依赖或桌面专属逻辑。

状态：`PARTIAL_ACCEPTANCE_GUI_INTERACTION_PENDING`。
