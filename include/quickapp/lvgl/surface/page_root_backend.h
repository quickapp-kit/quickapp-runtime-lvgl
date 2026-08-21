#pragma once

#include <cstdint>

#include "quickapp/core/foundation/id.h"
#include "quickapp/lvgl/foundation/types.h"
#include "quickapp/lvgl/surface/surface_types.h"

namespace quickapp::lvgl::surface {

struct PageRootHandle final {
  std::uint32_t value{0};

  [[nodiscard]] bool valid() const noexcept { return value != 0; }
  friend bool operator==(PageRootHandle, PageRootHandle) = default;
};

struct PageRootCreateResult final {
  foundation::LocalError error{foundation::LocalError::kNone};
  PageRootHandle handle{};

  [[nodiscard]] bool ok() const noexcept {
    return error == foundation::LocalError::kNone && handle.valid();
  }
};

class PageRootBackend {
 public:
  virtual ~PageRootBackend() = default;
  [[nodiscard]] virtual PageRootCreateResult createHidden(
      SurfaceViewport viewport) noexcept = 0;
  [[nodiscard]] virtual bool valid(PageRootHandle handle) const noexcept = 0;
  virtual void setHiddenNoFail(PageRootHandle handle, bool hidden) noexcept = 0;
  virtual void destroyNoFail(PageRootHandle handle) noexcept = 0;
  virtual void resetNoFail(PageRootHandle handle) noexcept = 0;
};

class SurfaceContentLifecyclePort {
 public:
  virtual ~SurfaceContentLifecyclePort() = default;
  [[nodiscard]] virtual foundation::LocalResult canRelease(
      const core::SurfaceId& surface_id) noexcept = 0;
  virtual void releaseNoFail(const core::SurfaceId& surface_id) noexcept = 0;
  virtual void resetNoFail(const core::SurfaceId& surface_id) noexcept = 0;
};

class EmptySurfaceContentLifecycle final : public SurfaceContentLifecyclePort {
 public:
  [[nodiscard]] foundation::LocalResult canRelease(
      const core::SurfaceId&) noexcept override {
    return foundation::LocalResult::success();
  }
  void releaseNoFail(const core::SurfaceId&) noexcept override {}
  void resetNoFail(const core::SurfaceId&) noexcept override {}
};

}  // namespace quickapp::lvgl::surface
