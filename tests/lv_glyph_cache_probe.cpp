// Regression guard for the sport-watch "tofu" (缺字方块) bug.
//
// Root cause (confirmed by this probe): the TinyTTF per-font glyph cache is an
// LRU count cache. During a page render LVGL concurrently holds (acquires) the
// bitmaps of all glyphs currently on screen. If the number of distinct glyphs
// held at once exceeds the cache capacity, the LRU cannot evict any in-use
// entry, lv_cache_acquire_or_create returns NULL, and the glyph is silently
// drawn as a missing-glyph box.
//
// This probe creates several font sizes (Detail page uses 18/28/13/12px), then
// rasterizes a full CJK glyph set at each size WHILE HOLDING every reference —
// exactly the render-time footprint. With a too-small cache it prints TOFU
// lines and exits 1; with an adequate cache it exits 0.

#include <cstdint>
#include <cstdio>
#include <string_view>
#include <vector>

#include <lvgl.h>

#include "quickapp/lvgl/font/system_default_font_asset.h"

namespace qfont = quickapp::lvgl::font;

extern "C" unsigned lodepng_decode32(unsigned char** out, unsigned* width,
                                     unsigned* height, const unsigned char* in,
                                     std::size_t insize);

namespace {

// Decode UTF-8 CJK string into code points.
std::vector<std::uint32_t> codePoints(std::string_view text) {
  std::vector<std::uint32_t> out;
  std::size_t i = 0;
  while (i < text.size()) {
    const auto b0 = static_cast<std::uint8_t>(text[i]);
    if ((b0 & 0xF0U) == 0xE0U && i + 2 < text.size()) {
      const auto b1 = static_cast<std::uint8_t>(text[i + 1]);
      const auto b2 = static_cast<std::uint8_t>(text[i + 2]);
      out.push_back(((b0 & 0x0FU) << 12) | ((b1 & 0x3FU) << 6) | (b2 & 0x3FU));
      i += 3;
    } else {
      out.push_back(b0);
      i += 1;
    }
  }
  return out;
}

void report(const char* tag) {
  lv_mem_monitor_t m{};
  lv_mem_monitor(&m);
  std::fprintf(stderr,
      "MEM[%s] used=%u free=%u frag=%u%% max_used=%u total=%u\n", tag,
      static_cast<unsigned>(m.total_size - m.free_size),
      static_cast<unsigned>(m.free_size), static_cast<unsigned>(m.frag_pct),
      static_cast<unsigned>(m.max_used), static_cast<unsigned>(m.total_size));
}

int run() {
  lv_init();
  report("init");

  const auto bytes = qfont::systemDefaultFontBytes();
  if (bytes.empty()) { std::fprintf(stderr, "PROBE font bytes empty\n"); return 2; }

  // Exact Detail-page binding values. Keep spaces and digits here: U+0020
  // was the original missing glyph and must remain covered by this probe.
  constexpr std::string_view kDetail =
      "距目标还差 3158 步距目标还差 114 kcal距目标还差 3 小时"
      "距目标还差 2 杯返回✓";

  // The runtime keeps multiple font instances alive (one per size). Detail
  // page alone uses 18/28/13/12; add 16 as default. All persist while pages
  // stack — reproduce by keeping them all alive simultaneously.
  const std::int32_t sizes[] = {12, 13, 16, 18, 28};
  // Must match kTinyTtfCacheGlyphCount in mount_host.cpp. With cache_size=2
  // this probe reproduces the tofu bug (exit 1); with the fixed value it must
  // rasterize every glyph while holding all references (exit 0).
  constexpr std::size_t kCacheGlyphCount = 256;
  std::vector<lv_font_t*> fonts;
  for (auto size : sizes) {
    lv_font_t* f = lv_tiny_ttf_create_data_ex(
        bytes.data(), bytes.size(), size, LV_FONT_KERNING_NONE, kCacheGlyphCount);
    if (f == nullptr) {
      std::fprintf(stderr, "PROBE.font_create_fail size=%d\n", size);
      return 3;
    }
    fonts.push_back(f);
  }
  report("fonts_created");

  const auto glyphs = codePoints(kDetail);

  // Rasterize every glyph at every size while HOLDING the draw data (do not
  // release), mimicking a full page render that concurrently needs all glyphs.
  std::vector<lv_font_glyph_dsc_t> held;
  std::size_t tofu = 0;
  std::size_t total = 0;
  for (auto* f : fonts) {
    for (auto cp : glyphs) {
      lv_font_glyph_dsc_t g{};
      if (!lv_font_get_glyph_dsc(f, &g, cp, 0)) {
        std::fprintf(stderr, "PROBE.dsc_miss cp=U+%04X\n", cp);
        ++tofu;
        continue;
      }
      ++total;
      if (cp == 0x20U) {
        if (g.adv_w == 0) {
          ++tofu;
          std::fprintf(stderr,
                       "PROBE.TOFU size=%d cp=U+0020 (zero advance)\n",
                       static_cast<int>(f->line_height));
        }
        continue;
      }
      const void* bmp = lv_font_get_glyph_bitmap(&g, nullptr);
      if (bmp == nullptr) {
        ++tofu;
        std::fprintf(stderr, "PROBE.TOFU size=%d cp=U+%04X (rasterize failed)\n",
                     static_cast<int>(f->line_height), cp);
      } else {
        held.push_back(g);  // hold reference, do not release
      }
    }
    report("after_size");
  }

  std::fprintf(stderr, "PROBE.glyph_summary total=%zu tofu=%zu held=%zu\n",
               total, tofu, held.size());

  for (auto& g : held) lv_font_glyph_release_draw_data(&g);
  for (auto* f : fonts) lv_tiny_ttf_destroy(f);
  report("teardown");

  lv_deinit();
  // Non-zero exit if any tofu happened, so CI can gate on it.
  return tofu == 0 ? 0 : 1;
}

}  // namespace

int main() { return run(); }
