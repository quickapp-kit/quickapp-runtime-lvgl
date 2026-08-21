#pragma once

#include "quickapp/lvgl/surface/page_root_backend.h"

namespace quickapp::lvgl::surface {

class LvglPageRootBackend final : public PageRootBackend {
 public:
  explicit LvglPageRootBackend(void* parent_object) noexcept;

  [[nodiscard]] PageRootCreateResult createHidden(
      SurfaceViewport viewport) noexcept override;
  [[nodiscard]] bool valid(PageRootHandle handle) const noexcept override;
  void setHiddenNoFail(PageRootHandle handle, bool hidden) noexcept override;
  void destroyNoFail(PageRootHandle handle) noexcept override;
  void resetNoFail(PageRootHandle handle) noexcept override;
  [[nodiscard]] void* nativeObject(PageRootHandle handle) noexcept;

 private:
  static constexpr std::size_t kMaxRoots = 16;
  void* parent_object_{nullptr};
  void* roots_[kMaxRoots]{};
};

}  // namespace quickapp::lvgl::surface
