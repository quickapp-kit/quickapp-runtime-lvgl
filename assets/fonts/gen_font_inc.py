#!/usr/bin/env python3
"""从 NotoSansSC-Alpha.ttf 生成 system_default_font_asset.inc (C 字节数组)。

保持与原 .inc 完全一致的格式: 头部注释(源名/SHA-256/Size) + 16 字节一行的
hex 数组 + _len。同时打印 SHA-256 供更新 .cpp 里的 digest 常量。

用法:
    python3 gen_font_inc.py NotoSansSC-Alpha.ttf ../../src/font/system_default_font_asset.inc
"""
import hashlib
import sys


def main(ttf_path: str, inc_path: str) -> int:
    with open(ttf_path, "rb") as f:
        data = f.read()

    digest = hashlib.sha256(data).hexdigest()
    size = len(data)
    name = ttf_path.split("/")[-1]

    lines = []
    lines.append(f"// Auto-generated from {name}")
    lines.append(f"// SHA-256: {digest}")
    lines.append(f"// Size: {size} bytes")
    lines.append(
        f"static const unsigned char quickapp_system_default_cjk_font[{size}] = {{")
    for i in range(0, size, 16):
        chunk = data[i:i + 16]
        hexes = ", ".join(f"0x{b:02X}" for b in chunk)
        lines.append(f"  {hexes},")
    lines.append("};")
    lines.append(
        f"static const unsigned int quickapp_system_default_cjk_font_len = {size};")

    with open(inc_path, "w") as f:
        f.write("\n".join(lines) + "\n")

    print(f"生成 {inc_path}")
    print(f"SHA-256: {digest}")
    print(f"Size: {size}")
    return 0


if __name__ == "__main__":
    if len(sys.argv) != 3:
        print(__doc__)
        sys.exit(2)
    sys.exit(main(sys.argv[1], sys.argv[2]))
