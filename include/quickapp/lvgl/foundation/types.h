#pragma once

#include <cstddef>
#include <cstdint>

namespace quickapp::lvgl::foundation {

enum class LocalError : std::uint8_t {
  kNone = 0,
  kInvalidArgument,
  kWrongThread,
  kInvalidState,
  kCapacityExhausted,
  kBusy,
  kUnsupported,
  kBackendFailed,
};

struct LocalResult {
  LocalError error{LocalError::kNone};

  [[nodiscard]] constexpr bool ok() const noexcept {
    return error == LocalError::kNone;
  }

  [[nodiscard]] static constexpr LocalResult success() noexcept {
    return {};
  }

  [[nodiscard]] static constexpr LocalResult failure(
      LocalError error_value) noexcept {
    return {error_value};
  }
};

enum class LifecycleState : std::uint8_t {
  kConstructed,
  kRunning,
  kStopping,
  kClosed,
};

enum class StopPolicy : std::uint8_t {
  kDrain,
  kCancel,
};

struct OwnerToken {
  std::uintptr_t value{0};

  [[nodiscard]] constexpr bool valid() const noexcept { return value != 0; }

  friend constexpr bool operator==(OwnerToken lhs, OwnerToken rhs) noexcept {
    return lhs.value == rhs.value;
  }

  friend constexpr bool operator!=(OwnerToken lhs, OwnerToken rhs) noexcept {
    return !(lhs == rhs);
  }
};

enum class PixelFormat : std::uint8_t {
  kRgb565,
  kRgb888,
  kRgba8888,
};

struct Rect {
  std::uint32_t x{0};
  std::uint32_t y{0};
  std::uint32_t width{0};
  std::uint32_t height{0};
};

struct DisplayConfig {
  std::uint32_t physical_width_px{0};
  std::uint32_t physical_height_px{0};
  PixelFormat preferred_pixel_format{PixelFormat::kRgb565};
  std::size_t max_dirty_regions{0};
};

struct DisplayCapabilities {
  std::uint32_t physical_width_px{0};
  std::uint32_t physical_height_px{0};
  PixelFormat pixel_format{PixelFormat::kRgb565};
  std::size_t max_dirty_regions{0};
};

struct DisplayFrameView {
  const std::byte* pixels{nullptr};
  std::size_t byte_length{0};
  std::size_t stride_bytes{0};
  std::uint32_t width_px{0};
  std::uint32_t height_px{0};
  PixelFormat pixel_format{PixelFormat::kRgb565};
  const Rect* dirty_regions{nullptr};
  std::size_t dirty_region_count{0};
  std::uint64_t frame_sequence{0};
};

enum class RawInputAction : std::uint8_t {
  kDown,
  kMove,
  kUp,
  kCancel,
};

struct RawInputSample {
  std::uint32_t device_id{0};
  std::uint32_t contact_id{0};
  RawInputAction action{RawInputAction::kCancel};
  std::int32_t physical_x{0};
  std::int32_t physical_y{0};
  std::uint16_t pressure{0};
  bool has_pressure{false};
  std::uint64_t timestamp_ns{0};
  std::uint64_t sample_sequence{0};
};

struct InputConfig {
  std::size_t max_samples_per_drain{0};
};

struct InputCapabilities {
  std::size_t capacity{0};
  bool supports_pressure{false};
};

struct DrainResult {
  LocalError error{LocalError::kNone};
  std::size_t sample_count{0};
  std::uint64_t overflow_count{0};
  std::uint64_t coalesced_count{0};

  [[nodiscard]] bool ok() const noexcept {
    return error == LocalError::kNone;
  }
};

}  // namespace quickapp::lvgl::foundation
