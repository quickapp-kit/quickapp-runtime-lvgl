# System Default CJK Font

`NotoSansSC-Alpha.ttf` is the V1 Alpha `system-default/400` font asset.
It is a deterministic subset of LVGL's vendored
`NotoSansSC-Regular.ttf`, limited to ASCII and the code points listed in
`system-default-cjk-glyphs.txt`. The list includes the full text corpus used
by the Case 001 Demo and DemoDetail pages.

Generation command:

```sh
hb-subset --no-hinting --unicodes=U+0020-007E \
  --text-file=system-default-cjk-glyphs.txt \
  NotoSansSC-Regular.ttf \
  --output-file=NotoSansSC-Alpha.ttf
```

The font remains licensed under the SIL Open Font License 1.1. The source
license is vendored with LVGL at
`tests/src/test_files/fonts/noto/OFL.txt`.
