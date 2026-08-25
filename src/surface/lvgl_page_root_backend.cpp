#include "quickapp/lvgl/surface/lvgl_page_root_backend.h"

#include <cmath>
#include <cstdint>
#include <limits>

#include <lvgl.h>

namespace quickapp::lvgl::surface {

LvglPageRootBackend::LvglPageRootBackend(void* parent_object) noexcept
    : parent_object_(parent_object) {}

PageRootCreateResult LvglPageRootBackend::createHidden(
    SurfaceViewport viewport) noexcept {
  if (!std::isfinite(viewport.width) || !std::isfinite(viewport.height) ||
      viewport.width <= 0 || viewport.height <= 0 ||
      viewport.width > std::numeric_limits<std::int32_t>::max() ||
      viewport.height > std::numeric_limits<std::int32_t>::max()) {
    return {foundation::LocalError::kInvalidArgument, {}};
  }
  std::size_t slot = kMaxRoots;
  for (std::size_t index = 0; index < kMaxRoots; ++index) {
    if (roots_[index] == nullptr) {
      slot = index;
      break;
    }
  }
  if (slot == kMaxRoots) {
    return {foundation::LocalError::kCapacityExhausted, {}};
  }
  auto* root = lv_obj_create(static_cast<lv_obj_t*>(parent_object_));
  if (root == nullptr) {
    return {foundation::LocalError::kBackendFailed, {}};
  }
  // The page root is a viewport container, not a visible DSL View. Remove
  // LVGL theme chrome so the runtime tree owns the visual result.
  lv_obj_set_style_bg_color(root, lv_color_hex(0xFFFFFF), 0);
  lv_obj_set_style_bg_opa(root, LV_OPA_COVER, 0);
  lv_obj_set_style_border_width(root, 0, 0);
  lv_obj_set_style_outline_width(root, 0, 0);
  lv_obj_set_style_shadow_width(root, 0, 0);
  lv_obj_set_style_radius(root, 0, 0);
  lv_obj_set_style_pad_all(root, 0, 0);
  lv_obj_set_layout(root, LV_LAYOUT_NONE);
  lv_obj_set_size(root, static_cast<std::int32_t>(std::lround(viewport.width)),
                  static_cast<std::int32_t>(std::lround(viewport.height)));
  // The page root is the platform viewport. Keep scrolling in the existing
  // host tree so long content never requires a second runtime container.
  lv_obj_set_scrollable(root, true);
  lv_obj_set_scroll_dir(root, LV_DIR_VER);
  lv_obj_set_scrollbar_mode(root, LV_SCROLLBAR_MODE_AUTO);
  lv_obj_set_hidden(root, true);
  roots_[slot] = root;
  return {foundation::LocalError::kNone,
          PageRootHandle{static_cast<std::uint32_t>(slot + 1)}};
}

bool LvglPageRootBackend::valid(PageRootHandle handle) const noexcept {
  if (!handle.valid() || handle.value > kMaxRoots) {
    return false;
  }
  auto* root = static_cast<lv_obj_t*>(roots_[handle.value - 1]);
  return root != nullptr && lv_obj_is_valid(root);
}

void LvglPageRootBackend::setHiddenNoFail(PageRootHandle handle,
                                          bool hidden) noexcept {
  if (!valid(handle)) {
    return;
  }
  auto* root = static_cast<lv_obj_t*>(roots_[handle.value - 1]);
  lv_obj_set_hidden(root, hidden);
}

void LvglPageRootBackend::destroyNoFail(PageRootHandle handle) noexcept {
  if (!handle.valid() || handle.value > kMaxRoots) {
    return;
  }
  auto*& entry = roots_[handle.value - 1];
  if (entry != nullptr) {
    lv_obj_delete(static_cast<lv_obj_t*>(entry));
    entry = nullptr;
  }
}

void LvglPageRootBackend::resetNoFail(PageRootHandle handle) noexcept {
  destroyNoFail(handle);
}

void* LvglPageRootBackend::nativeObject(PageRootHandle handle) noexcept {
  if (!valid(handle)) {
    return nullptr;
  }
  return roots_[handle.value - 1];
}

}  // namespace quickapp::lvgl::surface
