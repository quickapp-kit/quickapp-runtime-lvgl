#pragma once

#include <cstddef>

#include "quickapp/lvgl/mount/mount_host.h"
#include "quickapp/lvgl/surface/lvgl_page_root_backend.h"

namespace quickapp::lvgl::mount {

class LvglMountBackend final : public PageRootNativeLookup {
 public:
  explicit LvglMountBackend(surface::LvglPageRootBackend& roots) noexcept
      : roots_(&roots) {}
  [[nodiscard]] void* nativeObject(
      surface::PageRootHandle handle) noexcept override;

 private:
  surface::LvglPageRootBackend* roots_;
};

// The backend is kept as a narrow translation unit boundary. MountHost owns
// mapping and transaction policy; this class only exposes opaque LVGL roots.

}  // namespace quickapp::lvgl::mount
