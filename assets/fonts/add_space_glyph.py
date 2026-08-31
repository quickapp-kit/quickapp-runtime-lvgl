#!/usr/bin/env python3
"""给 NotoSansSC-Alpha.ttf 补上 U+0020 空格字形。

hb-subset 生成子集字体时会丢弃无轮廓的空格字形 (U+0020), 导致运行时凡是
含半角空格的文案 ("186 kcal"/"6 杯") 里的空格被 LVGL 渲染成缺字方块。
本脚本用 fontTools 补回一个空轮廓 + 正确 advance width 的空格字形。

用法:
    python3 add_space_glyph.py NotoSansSC-Alpha.ttf
"""
import sys
from fontTools.ttLib import TTFont
from fontTools.ttLib.tables._g_l_y_f import Glyph

SPACE_ADVANCE = 224  # 与源字体 NotoSansSC-Regular 的空格 advance 一致 (unitsPerEm=1000)


def main(path: str) -> int:
    font = TTFont(path)
    cmap = font.getBestCmap()
    if 0x20 in cmap:
        print(f"{path}: U+0020 已存在, 无需修改")
        return 0

    glyph_name = "space"

    # 1) glyf: 空轮廓字形
    glyf = font["glyf"]
    if glyph_name not in glyf.glyphs:
        empty = Glyph()
        empty.numberOfContours = 0
        glyf.glyphs[glyph_name] = empty
        font.getGlyphOrder()  # ensure order table exists
        if glyph_name not in font.getGlyphOrder():
            font.glyphOrder.append(glyph_name)

    # 2) hmtx: advance width + lsb
    font["hmtx"].metrics[glyph_name] = (SPACE_ADVANCE, 0)

    # 3) cmap: 把 U+0020 映射到 space 字形 (所有 subtable)
    for table in font["cmap"].tables:
        table.cmap[0x20] = glyph_name

    # 4) maxp numGlyphs 会由 fontTools 在 compile 时按 glyphOrder 修正
    font.save(path)
    print(f"{path}: 已补入 U+0020 空格字形 (advance={SPACE_ADVANCE})")
    return 0


if __name__ == "__main__":
    if len(sys.argv) != 2:
        print(__doc__)
        sys.exit(2)
    sys.exit(main(sys.argv[1]))
