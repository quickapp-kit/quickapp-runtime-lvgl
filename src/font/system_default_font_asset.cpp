#include "quickapp/lvgl/font/system_default_font_asset.h"

namespace quickapp::lvgl::font {
namespace {

#include "system_default_font_asset.inc"

constexpr std::string_view kDigest =
    "567d481837ba4bc18185d0995ca3dce296ea4b491ed1903ef3d4273efa377ec5";

}  // namespace

std::span<const std::uint8_t> systemDefaultFontBytes() noexcept {
  return {quickapp_system_default_cjk_font,
          quickapp_system_default_cjk_font_len};
}

std::string_view systemDefaultFontDigest() noexcept { return kDigest; }

}  // namespace quickapp::lvgl::font
