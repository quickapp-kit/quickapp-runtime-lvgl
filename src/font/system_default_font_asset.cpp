#include "quickapp/lvgl/font/system_default_font_asset.h"

namespace quickapp::lvgl::font {
namespace {

#include "system_default_font_asset.inc"

constexpr std::string_view kDigest =
    "62bc8f7df98318f1881f0655f07945fb27ac3a3db0dac7aa934532b6c73da563";

}  // namespace

std::span<const std::uint8_t> systemDefaultFontBytes() noexcept {
  return {quickapp_system_default_cjk_font,
          quickapp_system_default_cjk_font_len};
}

std::string_view systemDefaultFontDigest() noexcept { return kDigest; }

}  // namespace quickapp::lvgl::font
