#include "quickapp/lvgl/mount/lvgl_mount_backend.h"

namespace quickapp::lvgl::mount {

void* LvglMountBackend::nativeObject(surface::PageRootHandle handle) noexcept {
  return roots_ == nullptr ? nullptr : roots_->nativeObject(handle);
}

}  // namespace quickapp::lvgl::mount
