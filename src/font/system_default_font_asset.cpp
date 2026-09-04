#include "quickapp/lvgl/font/system_default_font_asset.h"

namespace quickapp::lvgl::font {
namespace {

#include <system_default_font_asset.inc>

}  // namespace

std::span<const std::uint8_t> systemDefaultFontBytes() noexcept {
  return {quickapp_system_default_cjk_font,
          quickapp_system_default_cjk_font_len};
}

std::string_view systemDefaultFontDigest() noexcept {
  return quickapp_system_default_font_digest;
}

std::string_view systemDefaultFontProfile() noexcept {
  return quickapp_system_default_font_profile;
}

}  // namespace quickapp::lvgl::font
