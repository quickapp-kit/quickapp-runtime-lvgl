# QuickApp Runtime LVGL

[QuickApp Kit](https://github.com/quickapp-kit) 的 LVGL 平台适配层。面向嵌入式/穿戴设备，使用 [LVGL](https://lvgl.io/) 作为渲染后端。

## 包含内容

该层位于平台无关的 Core 和硬件（或 SDL 模拟器）之间：

- **Foundation** — 宿主线程任务队列、单调时钟、唤醒端口、后端生命周期
- **Runtime Host** — 组合根、生命周期控制、包源、Trace 适配
- **Surface Host** — LVGL 显示 Surface 管理、页面根后端
- **Mount Host** — LVGL 对象挂载、组件创建、Core Render 桥接
- **Font Measure** — 字体度量与文本测量
- **Backends** — SDL3 + libuv（模拟器）/ 回调式（嵌入式）

Foundation 层无动态分配（启动后）、无 RTTI、无异常。

## 环境要求

- C++20 编译器
- CMake 3.24+
- 兄弟仓库：`quickapp-runtime-core`、`quickapp-runtime-js`
- 模拟器：SDL3、libuv（通过 pkg-config）
- 嵌入式：仅需 C++ 工具链

## 构建

```bash
# 默认（模拟器 + 全部模块）
cmake -S . -B build -G Ninja
cmake --build build
ctest --test-dir build --output-on-failure

# 仅嵌入式（不依赖 SDL/libuv）
cmake -S . -B build-embedded -G Ninja -DQUICKAPP_LVGL_BUILD_SIMULATOR=OFF
cmake --build build-embedded
ctest --test-dir build-embedded --output-on-failure
```

### Sanitizer 构建

```bash
cmake -S . -B build-asan -G Ninja -DQUICKAPP_LVGL_ENABLE_ASAN=ON
cmake --build build-asan -j
ctest --test-dir build-asan --output-on-failure

cmake -S . -B build-tsan -G Ninja -DQUICKAPP_LVGL_ENABLE_TSAN=ON
cmake --build build-tsan -j
ctest --test-dir build-tsan --output-on-failure
```

## 目录结构

```
├── include/quickapp/lvgl/    # 公共头文件
├── src/
│   ├── backends/             # SDL3/libuv 模拟器 + 嵌入式后端
│   ├── surface/              # LVGL Surface Host
│   ├── mount/                # LVGL Mount Host
│   ├── measure/              # 字体测量
│   ├── event/                # 输入适配器
│   └── integration/          # Core Mount 桥接
├── fakes/                    # 测试替身
├── tests/                    # 契约测试 & 探针
├── cmake/                    # 边界检查脚本
└── evidence/                 # 验证文档
```

## 相关仓库

- [quickapp-runtime-core](https://github.com/quickapp-kit/quickapp-runtime-core) — C++ 运行时内核
- [quickapp-runtime-js](https://github.com/quickapp-kit/quickapp-runtime-js) — JS 引擎层
- [quickapp-runtime-android](https://github.com/quickapp-kit/quickapp-runtime-android) — Android 适配层

## 许可证

[MIT](LICENSE)
