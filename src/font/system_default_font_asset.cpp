#include "quickapp/lvgl/font/system_default_font_asset.h"

namespace quickapp::lvgl::font {
namespace {

#include "system_default_font_asset.inc"

constexpr std::string_view kDigest =
    "c44bb9ee7e921021ce95877315e570a39cda184a1ceb23ca6812987ce265d01d";

}  // namespace

std::span<const std::uint8_t> systemDefaultFontBytes() noexcept {
  return {quickapp_system_default_cjk_font,
          quickapp_system_default_cjk_font_len};
}

std::string_view systemDefaultFontDigest() noexcept { return kDigest; }

}  // namespace quickapp::lvgl::font
