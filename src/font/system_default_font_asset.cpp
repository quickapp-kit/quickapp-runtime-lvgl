#include "quickapp/lvgl/font/system_default_font_asset.h"

namespace quickapp::lvgl::font {
namespace {

#include "system_default_font_asset.inc"

constexpr std::string_view kDigest =
    "5d99238d1f9493227eeaf535e5f9d93634bd177c7b032fb171d69e96a9969f71";

}  // namespace

std::span<const std::uint8_t> systemDefaultFontBytes() noexcept {
  return {quickapp_system_default_cjk_font,
          quickapp_system_default_cjk_font_len};
}

std::string_view systemDefaultFontDigest() noexcept { return kDigest; }

}  // namespace quickapp::lvgl::font
