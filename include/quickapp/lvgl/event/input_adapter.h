#pragma once

#include <cstddef>

#include "quickapp/core/event/event_router.h"
#include "quickapp/core/foundation/id.h"
#include "quickapp/lvgl/backends/sdl_backends.h"
#include "quickapp/lvgl/mount/mount_host.h"

namespace quickapp::lvgl::event {

class PlatformInputSink {
 public:
  virtual ~PlatformInputSink() = default;
  [[nodiscard]] virtual core::EnqueueResult post(
      core::event::PlatformInputMessage message) noexcept = 0;
};

class SdlInputAdapter final {
 public:
  SdlInputAdapter(backends::SdlRawInputBackend& raw, mount::MountHost& mounts,
                  PlatformInputSink& sink, core::SurfaceId surface) noexcept
      : raw_(raw), mounts_(mounts), sink_(sink),
        surface_(std::move(surface)) {}

  [[nodiscard]] foundation::LocalResult pump(
      foundation::OwnerToken owner, std::size_t budget = 32) noexcept;

 private:
  backends::SdlRawInputBackend& raw_;
  mount::MountHost& mounts_;
  PlatformInputSink& sink_;
  core::SurfaceId surface_;
};

}  // namespace quickapp::lvgl::event
