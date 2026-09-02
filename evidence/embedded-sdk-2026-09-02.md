# Embedded SDK Evidence

## 结论

LVGL 平台已提供 `libquickapp_runtime.a`、稳定 C ABI、可选动态库配置和最小 C Host 样例。关闭 Simulator 后可以独立构建，不依赖 SDL 或 libuv。

## 公共边界

- 入口：`include/quickapp/lvgl/sdk/runtime.h`
- 生命周期：`create -> load_rpk -> attach_surface -> dispatch_input/update_lifecycle/pump -> destroy`
- 公共类型只有不透明句柄、固定宽度整数、枚举、长度明确 buffer 和 typed result。
- LVGL、C++、Core、JS、Runtime Tree 和具体 EventLoop 类型不进入 ABI。
- `qak_runtime_config_t.adapter` 是 Platform Composition Root 的 C ABI 注入边界；实际 Host 负责将调用接入既有 JS/Core/LVGL 组合。
- RPK bytes 在 ABI 层复制到 Runtime 自有存储；内存 RPK 校验 ZIP signature 和可选 SHA-256，路径源校验路径安全性并交由 Host Adapter 读取和完成包级校验。

## 产物

嵌入式构建：

```text
cmake -S . -B build-sdk -G Ninja -DQUICKAPP_LVGL_BUILD_SIMULATOR=OFF
cmake --build build-sdk -j 4
```

- `build-sdk/libquickapp_runtime.a`
- `build-sdk/quickapp_lvgl_sdk_example`
- `build-sdk/quickapp_sdk_c_api_tests`
- 静态库 SHA-256：`f4700d4ac15f04dce843f5146d96fabe5894bf6130b8d15b215c3dd5ade9201d`

可选动态库构建：

```text
cmake -S . -B build-sdk-shared -G Ninja -DQUICKAPP_LVGL_BUILD_SIMULATOR=OFF -DQUICKAPP_LVGL_BUILD_TESTS=OFF -DQUICKAPP_LVGL_BUILD_SHARED_SDK=ON
cmake --build build-sdk-shared --target quickapp_runtime_shared -j 4
cmake --install build-sdk-shared --prefix /tmp/quickapp-kit-sdk-check
```

- macOS 产物：`build-sdk-shared/libquickapp_runtime.dylib`
- Linux 等平台由同一配置产出对应 `.so` 命名。
- 动态库 SHA-256：`4d602d58d41edd99ad74fb3e21ffeecca37ff1b4ccb000d5074ef276a883ccf4`

## 验证

```text
ctest --test-dir build-sdk --output-on-failure
```

结果：12/12 通过；包含 C ABI、嵌入式依赖隔离、Core/LVGL 边界、字体和媒体回归。

```text
./build-sdk/quickapp_sdk_c_api_tests
./build-sdk/quickapp_lvgl_sdk_example
```

结果：均通过。C ABI 测试覆盖创建、内存 RPK、Surface、Pointer、Lifecycle、Pump、摘要失败和路径穿越拒绝。

真实 RPK：

```text
SDL_VIDEODRIVER=dummy ./quickapp-examples/build-m1-s2/quickapp_case001_lvgl --rpk quickapp-examples/showcases/wallet-001/dist/wallet-001.rpk
```

- RPK：`quickapp-examples/showcases/wallet-001/dist/wallet-001.rpk`
- SHA-256：`c35a63ada9288655fce18a3aa35b4d105a1c0174457a2c448302692dc3024b98`
- 结果：真实 RPK 打开、资源加载、Home 首屏挂载、LVGL/SDL Simulator 启动和 teardown 通过；资源、Surface、Runtime Node、Handler、LVGL Mount 对象最终归零。

## 已验证事实

- SDK 静态链接不要求 SDL/libuv。
- 公开符号为七个 `qak_runtime_*` C 函数；未暴露 `lv_obj_t` 或 C++ 类型。
- SDK 安装包含静态库和公共头文件；共享库只在显式开启时构建。
- 旧 SDL 测试在嵌入式 profile 下已按配置排除，避免错误引入桌面依赖。

## 待验证项

- 当前 C ABI 已冻结 Host 注入边界，但真实设备固件仍需提供一个具体 Platform Composition Root Adapter；本轮没有把 Examples 组合根迁入 SDK。
- ESP32-S3 的交叉编译、设备 LVGL display/input callback 和真实固件内存预算需要在目标工具链上验证。

## 最大风险

C ABI 已具备，但真实设备 Host Adapter 尚未成为 SDK 内置实现；下一步应以一个设备回调适配器接入真实 JS/Core/LVGL 组合，而不是扩展公共 ABI。
