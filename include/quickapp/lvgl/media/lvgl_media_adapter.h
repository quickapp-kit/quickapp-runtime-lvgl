#pragma once

#include <cstddef>

#include "quickapp/core/package/video_contract.h"

namespace quickapp::lvgl::media {

// LVGL does not provide a decoder or playback backend. Keep the platform
// boundary explicit instead of allocating a partial player object.
class LvglMediaAdapter final {
 public:
  [[nodiscard]] core::package::VideoControlResult control(
      const core::package::VideoControlRequest& request) const noexcept;

  void teardown(const core::SurfaceId&) noexcept {}
  void clear() noexcept {}

  [[nodiscard]] std::size_t liveResourceCount() const noexcept { return 0; }
};

}  // namespace quickapp::lvgl::media
