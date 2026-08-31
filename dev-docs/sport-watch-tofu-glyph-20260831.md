# sport-watch Detail 页中文渲染成缺字方块 (tofu) — 根因与修复

日期: 2026-08-31
组件: quickapp-runtime-lvgl (LVGL 平台侧字体/字形光栅化)
影响: sport-watch 等含半角空格文案的 Showcase, Detail 页出现缺字方块

## 现象

- Detail 页文案如 `6842 步` / `距目标还差 3158 步` / `186 kcal` 中,
  空格位置渲染成方块 (▢), 相邻中文字看起来"消失"。
- 高频中文字 (距/目/标/还/差) 正常; 方块稳定出现在固定位置。
- 与字号无关、与是否有数字无关。

## 排查过程 (含走过的弯路, 供后人避坑)

1. **误判一: 字符集缺失(整字)**。用 fontTools 验证 cmap, 杯/时/小/距/目/标
   等中文字**全部在**字体里, 排除"中文字缺失"。
2. **误判二: TinyTTF glyph cache 容量不足**。`kTinyTtfCacheGlyphCount = 2`
   确实过小 —— 写独立探针 (tests/lv_glyph_cache_probe.cpp) 复现: 渲染时
   LVGL 会并发持有(acquire)多个字形 bitmap, 一屏不同字形数超过缓存容量时,
   LRU 无可淘汰条目, `lv_cache_acquire_or_create` 返回 NULL → 静默 tofu。
   这是**真实隐患**, 但把容量提到 256 后探针 tofu=0, 现象**仍在** → 说明
   它不是本 case 的根因。
3. **误判三: LVGL 内存池 (LV_MEM_SIZE) 太小/碎片**。探针实测: 失败发生时
   池仍剩 55KB、碎片 0% → 排除。
4. **真因: 字体缺半角空格字形 U+0020**。

## 根因

运行时字体 `assets/fonts/NotoSansSC-Alpha.ttf` 由 `hb-subset` 子集化生成。
**hb-subset 会丢弃无轮廓的空格字形 (U+0020)** —— 即便命令行 `--unicodes`
显式包含 U+0020。fontTools 实测该子集字体 cmap 中 `U+0020 MISSING`。

因此凡是文案含半角空格 (`6842 步`、`186 kcal`), 该空格字符找不到字形,
被 LVGL 渲染为缺字方块。这解释了所有现象:
- 方块只在空格位置 (与字号/内容无关)。
- 中文字本身都在, 所以其它字正常。

## 修复

纯 LVGL 平台侧改动, **不涉及 Core / Contract / Schema / DSL / RPK**:

1. `assets/fonts/add_space_glyph.py`: 用 fontTools 给子集字体补回 U+0020
   空格字形 (空轮廓 + advance=224, 与源字体 NotoSansSC-Regular 一致)。
2. `assets/fonts/gen_font_inc.py`: 重新生成 `src/font/system_default_font_asset.inc`
   (C 字节数组), 并刷新 SHA-256。
3. `src/font/system_default_font_asset.cpp`: 更新 `kDigest` 常量。
4. `assets/fonts/README.md`: 记录补空格 + 重生成 .inc 的步骤。

字形数 326 → 327 (仅新增空格), digest 由 `62bc8f7d...` 变为 `567d4818...`。

## 顺带的健壮性改进 (非本 case 根因, 但确为隐患)

- `src/mount/mount_host.cpp`: `kTinyTtfCacheGlyphCount` 2 → 256, 避免多字形
  一屏并发渲染时因 LRU 缓存过小而 tofu。
- `lv_conf.h`: `LV_MEM_SIZE` 64KB → 4MB, 桌面模拟器与 8MB PSRAM 的
  ESP32-S3 均可容纳, 给字形/图片解码留足空间。
- `tests/lv_glyph_cache_probe.cpp`: 新增回归探针, 满屏多字号 CJK 光栅化,
  glyph cache 过小则 exit 1。

## 仍存在的潜在隐患 (记录备查, 本次未触发)

`BoundedText::from` (src/mount/mount_types.cpp) 对文本按**字节**截断到
`kMaxPropertyText = 512`, 未对齐 UTF-8 字符边界。若某文本恰好在 512 字节处
落在一个多字节中文字中间, 会产生非法 UTF-8 → 潜在乱码/方块。sport-watch
文案很短, 未触发。如未来出现长文本 tofu, 优先排查此处。
