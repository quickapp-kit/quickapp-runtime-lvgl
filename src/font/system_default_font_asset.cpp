#include "quickapp/lvgl/font/system_default_font_asset.h"

namespace quickapp::lvgl::font {
namespace {

#include "system_default_font_asset.inc"

constexpr std::string_view kDigest =
    "7950a42a77d58c3f3afa47a2a0c4b1ab5aca3507aae1eb8015ce55eb4daed507";

}  // namespace

std::span<const std::uint8_t> systemDefaultFontBytes() noexcept {
  return {quickapp_system_default_cjk_font,
          quickapp_system_default_cjk_font_len};
}

std::string_view systemDefaultFontDigest() noexcept { return kDigest; }

}  // namespace quickapp::lvgl::font
