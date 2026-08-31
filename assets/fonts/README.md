# System Default CJK Font

`NotoSansSC-Alpha.ttf` is the V1 Alpha `system-default/400` font asset.
It is a deterministic subset of LVGL's vendored
`NotoSansSC-Regular.ttf`, limited to ASCII and the code points listed in
`system-default-cjk-glyphs.txt`. The list includes the full text corpus used
by the Case 001, LVGL P0 baseline, Gallery-001, Consumer-001, and
Wearable-001 pages.

Showcase additions are kept as a separate auditable line in
`system-default-cjk-glyphs.txt`; this remains a controlled subset rather than
a complete CJK font.

Generation command:

```sh
hb-subset --no-hinting --unicodes=U+0020-007E \
  --text-file=system-default-cjk-glyphs.txt \
  NotoSansSC-Regular.ttf \
  --output-file=NotoSansSC-Alpha.ttf
# hb-subset drops the outline-less space glyph (U+0020); add it back so that
# text containing half-width spaces ("186 kcal", "6 杯") renders correctly
# instead of showing a missing-glyph box:
python3 add_space_glyph.py NotoSansSC-Alpha.ttf
# Regenerate the embedded C byte array and refresh the SHA-256 in
# ../../src/font/system_default_font_asset.cpp:
python3 gen_font_inc.py NotoSansSC-Alpha.ttf \
  ../../src/font/system_default_font_asset.inc
```

The font remains licensed under the SIL Open Font License 1.1. The source
license is vendored with LVGL at
`tests/src/test_files/fonts/noto/OFL.txt`.
