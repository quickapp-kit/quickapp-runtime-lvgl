#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>

namespace quickapp::lvgl::font {

inline constexpr std::string_view kSystemDefaultFontToken = "system-default";
inline constexpr std::uint16_t kSystemDefaultFontWeight = 400;
inline constexpr std::int32_t kSystemDefaultFontMinSize = 1;
inline constexpr std::int32_t kSystemDefaultFontMaxSize = 256;

[[nodiscard]] std::span<const std::uint8_t> systemDefaultFontBytes() noexcept;
[[nodiscard]] std::string_view systemDefaultFontDigest() noexcept;

}  // namespace quickapp::lvgl::font
