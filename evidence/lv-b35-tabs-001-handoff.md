# LVGL B3.5 Tabs Handoff

## 结论

LVGL 已完成 Tabs 的平台 Host 映射：真实的 `lv_tabview` 承载 Tabs，Core 仍拥有唯一 Runtime Tree 和 `selected` 状态；平台只负责原生对象、用户输入和 `change(index,value)` 回调。

## 真实 RPK

- 路径：`/Users/qy/code/my-github/quickapp-kit-ai/quickapp-examples/showcases/tabs-001/dist/tabs-001.rpk`
- 大小：`14878` bytes
- SHA-256：`9a53e285d8d4cf13080b782f64762b6ab44596ad3c3ab68ace08a19340108792`
- 真实 IR：`Tabs.items="首页|任务|我的"`、`Tabs.selected=0`
- 真实 Handler：`change`；页面 JS 将 `event.index` 写回 `selected`

## 平台实现

- `MountHost` 的 `HostComponentType::kTabs` 创建 `lv_tabview`。
- `items` 按 `|` 分隔创建原生 tab button，最多 32 项；空项和空列表拒绝挂载。
- `selected` 使用 `lv_tabview_set_active(..., LV_ANIM_OFF)`，支持受控增量更新；items 尚未到达时先保存 selected，避免属性顺序影响挂载。
- `installTabsHandler` 监听 `LV_EVENT_VALUE_CHANGED`，回调零基 `index` 和对应 tab 文本 `value`。
- 节点销毁、Surface release 和 Mount close 会使 Tabs callback binding 失效，并释放 LVGL 对象和字体资源。
- 未创建第二棵树、第二套路由或平台私有业务状态。

## 平台测试

```text
cmake --build /Users/qy/code/my-github/quickapp-kit-ai/quickapp-runtime-lvgl/build -j 4 --target lv_b35_tabs_mount_tests
SDL_VIDEODRIVER=dummy /Users/qy/code/my-github/quickapp-kit-ai/quickapp-runtime-lvgl/build/lv_b35_tabs_mount_tests
ctest --test-dir /Users/qy/code/my-github/quickapp-kit-ai/quickapp-runtime-lvgl/build --output-on-failure -R 'lv_b35_tabs_mount_tests|lv_s04_mount_contract_tests'
```

结果：`2/2 passed`。

测试证据：

- 首次 Mount 成功，原生 tab 数量为 `3`，初始 selected 为 `0`。
- 用户切换到 `1` 后回调 `change(index=1,value=任务)`。
- Core/Render 侧受控更新 selected 到 `2` 后，仍使用同一个 Tabs NativeHandle，active index 为 `2`。
- Surface release 后 `liveObjectCount=0`、`liveFontCount=0`，旧 handler 不可重新安装到已释放节点。

## 真实 Simulator 限制

已尝试：

```text
SDL_VIDEODRIVER=dummy /Users/qy/code/my-github/quickapp-kit-ai/quickapp-examples/build-m1-s2/quickapp_lvgl_simulator --rpk showcases/tabs-001/dist/tabs-001.rpk
```

结果：`RPK open failed: unsupported host component`。这是现有 Examples Composition Root 的组件注册/适配入口尚未接入 Tabs，不是 LVGL `MountHost` 的平台实现失败。按本任务边界未修改 `quickapp-examples`，因此保留该入口阻塞；真实 RPK 已完成产物和 IR provenance 验证，平台 Host 生命周期由专用 Mount 测试覆盖。

状态：`READY_FOR_PLATFORM_REVIEW`。
