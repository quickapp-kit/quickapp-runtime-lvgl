# LVGL Font Profiles

字体选择属于 LVGL 平台构建配置，Core 不感知字体档位。默认配置是
`QUICKAPP_LVGL_FONT_PROFILE=common-cjk`，使用受控的
`NotoSansSC-Common.ttf`。它覆盖仓库当前案例的可见文本和常用基础字符，
不是完整 CJK 字体。

可用档位：

| Profile | 资产 | 用途 |
| --- | --- | --- |
| `minimal` | `NotoSansSC-Alpha.ttf` | 兼容旧 Alpha/P0 的最小内置集 |
| `common-cjk` | `NotoSansSC-Common.ttf` | 默认，覆盖当前基础产品和 Showcase |
| `full-cjk` | LVGL vendored `NotoSansSC-Regular.ttf` | 设备资源预算允许时的完整字体 |
| `custom` | `QUICKAPP_LVGL_FONT_ASSET` | 用户提供的预先子集化 TTF |

Profile 只影响 LVGL 字体资产的构建和内置，不改变 RPK、Core、JS、布局、
事件或公共 ABI。发布到不同设备时可通过 CMake cache 选择档位：

```sh
cmake -S quickapp-runtime-lvgl -B build \
  -DQUICKAPP_LVGL_FONT_PROFILE=common-cjk
cmake --build build
```

嵌入式设备按产品字符集生成自定义字体：

```sh
hb-subset --no-hinting --unicodes=U+0020-007E \
  --text-file=my-device-glyphs.txt NotoSansSC-Regular.ttf \
  --output-file=my-device-font.ttf
cmake -S quickapp-runtime-lvgl -B build-custom \
  -DQUICKAPP_LVGL_FONT_PROFILE=custom \
  -DQUICKAPP_LVGL_FONT_ASSET=/absolute/path/my-device-font.ttf
cmake --build build-custom
```

字体字节数组由构建目录中的 `gen_font_inc.py` 自动生成，并携带实际资产
的 SHA-256 和 profile；源码目录不再需要被构建过程改写。

## Asset Generation

`NotoSansSC-Common.ttf` 基于 LVGL vendored
`NotoSansSC-Regular.ttf`，使用 `system-default-common-cjk-glyphs.txt` 生成。
`hb-subset` 会移除无轮廓的空格字形，因此生成后需要补回 U+0020：

Generation command:

```sh
hb-subset --no-hinting --unicodes=U+0020-007E \
  --text-file=system-default-common-cjk-glyphs.txt \
  NotoSansSC-Regular.ttf \
  --output-file=NotoSansSC-Common.ttf
# hb-subset drops the outline-less space glyph (U+0020); add it back so that
# text containing half-width spaces ("186 kcal", "6 杯") renders correctly
# instead of showing a missing-glyph box:
python3 add_space_glyph.py NotoSansSC-Common.ttf
```

The font remains licensed under the SIL Open Font License 1.1. The source
license is vendored with LVGL at
`tests/src/test_files/fonts/noto/OFL.txt`.
