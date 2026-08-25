# LVGL B6 URL Provider Handoff

## 结论

LVGL 不打开外部网页，也不实现 WebView Host Component。当前公共 typed Feature ABI 没有 `openUrl`、`webview` 或等价的网页导航请求；LVGL 对未声明的网页能力返回 `Status::kUnsupported`，不创建 LVGL 对象，不改变 Core Navigation。

## 验证范围

- 平台：`quickapp-runtime-lvgl`
- Provider：`src/feature/lvgl_feature_provider.cpp`
- 回归测试：`tests/lv_b6_url_provider_tests.cpp`
- 测试使用现有 `ModuleId::kPageHost` / `Method::kShowToast` typed 请求，并附带外部 URL 字段，验证平台不会把 URL 字段旁路解释成网页打开。
- 返回：`status=kUnsupported`，`error.code=CAPABILITY_UNSUPPORTED`。
- `live_resource_count`：调用前后均为 `0`；teardown 后仍为 `0`。

## RPK 状态

当前工作区没有以下输入，不能伪造真实 RPK 验收：

`/Users/qy/code/my-github/quickapp-kit-ai/quickapp-examples/showcases/url-001/dist/url-001.rpk`

也没有 `quickapp-examples/showcases/url-001` 目录。因此本轮没有 RPK 路径或 SHA-256，也没有启动 Simulator。该缺口属于测试输入/Toolkit 侧，不通过新增平台能力绕过。

## 构建与测试

```text
cmake --build /Users/qy/code/my-github/quickapp-kit-ai/quickapp-runtime-lvgl/build -j 4 --target lv_b6_url_provider_tests
SDL_VIDEODRIVER=dummy /Users/qy/code/my-github/quickapp-kit-ai/quickapp-runtime-lvgl/build/lv_b6_url_provider_tests
ctest --test-dir /Users/qy/code/my-github/quickapp-kit-ai/quickapp-runtime-lvgl/build --output-on-failure -R 'lv_b6_url_provider_tests|lv_b4_feature_provider_tests|lv_s04_mount_contract_tests'
```

结果：`lv_b6_url_provider_tests`、`lv_b4_feature_provider_tests`、`lv_s04_mount_contract_tests` 均通过。

## 架构边界

- 未修改 Core、JS、Toolkit、公共 Contract 或 Examples Composition Root。
- 未新增 ModuleId、Method、Bridge、Runtime Tree、Navigation 或 WebView。
- `system.fetch` 仍是独立的网络请求语义，不被当作网页打开语义。
- 状态：`BLOCKED_MISSING_URL_RPK`；LVGL 的拒绝行为已验证，待真实 `url-001.rpk` 提供后才能做 RPK 加载验收。
