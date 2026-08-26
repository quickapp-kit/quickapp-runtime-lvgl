#include "quickapp/lvgl/list/virtual_list.h"

#include <algorithm>
#include <climits>
#include <limits>
#include <set>
#include <utility>

namespace quickapp::lvgl::list {
namespace {

core::RuntimeError listError(core::RuntimeErrorCode code,
                             std::string_view message) noexcept {
  return core::RuntimeError::simple(code, message);
}

}  // namespace

core::RuntimeResult<core::Accepted> VirtualListWindow::configure(
    std::size_t logical_count, std::int32_t row_height,
    std::int32_t viewport_height, std::vector<std::string> keys) noexcept {
  if (max_physical_slots_ == 0 || max_physical_slots_ > 4096 ||
      row_height <= 0 || viewport_height <= 0 || logical_count == 0 ||
      keys.size() != logical_count ||
      logical_count > static_cast<std::size_t>(INT32_MAX / row_height)) {
    return core::RuntimeResult<core::Accepted>::failure(listError(
        core::RuntimeErrorCode::kAbiInvalidArgument,
        "VirtualList configuration is outside bounded contract"));
  }
  std::set<std::string> unique_keys;
  for (const auto& key : keys) {
    if (!unique_keys.insert(key).second) {
      return core::RuntimeResult<core::Accepted>::failure(listError(
          core::RuntimeErrorCode::kAbiInvalidArgument,
          "VirtualList keys must be unique"));
    }
  }
  try {
    logical_count_ = logical_count;
    row_height_ = row_height;
    viewport_height_ = viewport_height;
    keys_ = std::move(keys);
    window_ = VirtualListWindowSnapshot{};
    const auto initial = rebuild(0);
    if (!initial) {
      reset();
      return core::RuntimeResult<core::Accepted>::failure(initial.error());
    }
    return initial
               ? core::RuntimeResult<core::Accepted>::success(core::Accepted{})
               : core::RuntimeResult<core::Accepted>::failure(initial.error());
  } catch (...) {
    reset();
    return core::RuntimeResult<core::Accepted>::failure(listError(
        core::RuntimeErrorCode::kOutOfMemory,
        "VirtualList configuration allocation failed"));
  }
}

core::RuntimeResult<VirtualListWindowSnapshot> VirtualListWindow::scrollTo(
    std::int32_t offset) noexcept {
  if (logical_count_ == 0 || row_height_ <= 0 || viewport_height_ <= 0) {
    return core::RuntimeResult<VirtualListWindowSnapshot>::failure(listError(
        core::RuntimeErrorCode::kLifecycleBusy, "VirtualList is not configured"));
  }
  const auto result = rebuild(offset);
  if (!result) return result;
  return core::RuntimeResult<VirtualListWindowSnapshot>::success(window_);
}

std::optional<VirtualListItem> VirtualListWindow::hitTest(
    std::int32_t y) const noexcept {
  if (y < 0) return std::nullopt;
  const auto absolute = window_.scroll_offset + y;
  if (absolute < 0 || row_height_ <= 0) return std::nullopt;
  const auto index = static_cast<std::size_t>(absolute / row_height_);
  if (index < window_.first_index || index >= window_.last_index_exclusive) {
    return std::nullopt;
  }
  for (const auto& item : window_.items) {
    if (item.logical_index == index) return item;
  }
  return std::nullopt;
}

void VirtualListWindow::reset() noexcept {
  logical_count_ = 0;
  row_height_ = 0;
  viewport_height_ = 0;
  keys_.clear();
  window_ = VirtualListWindowSnapshot{};
}

core::RuntimeResult<VirtualListWindowSnapshot> VirtualListWindow::rebuild(
    std::int32_t offset) noexcept {
  const auto max_offset = std::max(0, contentHeight() - viewport_height_);
  const auto clamped = std::clamp(offset, 0, max_offset);
  const auto first = static_cast<std::size_t>(clamped / row_height_);
  const auto visible = static_cast<std::size_t>(
      (viewport_height_ + row_height_ - 1) / row_height_);
  const auto last = std::min(logical_count_, first + visible + 1);
  if (last - first > max_physical_slots_) {
    return core::RuntimeResult<VirtualListWindowSnapshot>::failure(listError(
        core::RuntimeErrorCode::kOutOfMemory,
        "VirtualList physical slot budget is too small"));
  }
  try {
    std::vector<VirtualListItem> next;
    next.reserve(last - first);
    std::set<std::size_t> used_slots;
    for (std::size_t index = first; index < last; ++index) {
      std::optional<std::size_t> slot;
      for (const auto& old : window_.items) {
        if (old.key == keys_[index] && !used_slots.contains(old.physical_slot)) {
          slot = old.physical_slot;
          break;
        }
      }
      if (!slot.has_value()) {
        for (std::size_t candidate = 0; candidate < max_physical_slots_;
             ++candidate) {
          if (!used_slots.contains(candidate)) {
            slot = candidate;
            break;
          }
        }
      }
      if (!slot.has_value()) {
        return core::RuntimeResult<VirtualListWindowSnapshot>::failure(listError(
            core::RuntimeErrorCode::kOutOfMemory,
            "VirtualList physical slot allocation failed"));
      }
      used_slots.insert(*slot);
      next.push_back(VirtualListItem{index, keys_[index], *slot,
                                     static_cast<std::int32_t>(index * row_height_)});
    }
    window_ = VirtualListWindowSnapshot{first, last, clamped, std::move(next)};
    return core::RuntimeResult<VirtualListWindowSnapshot>::success(window_);
  } catch (...) {
    return core::RuntimeResult<VirtualListWindowSnapshot>::failure(listError(
        core::RuntimeErrorCode::kOutOfMemory,
        "VirtualList window allocation failed"));
  }
}

core::RuntimeResult<core::Accepted> VirtualListHost::configure(
    lv_obj_t* viewport, std::int32_t row_height,
    std::int32_t viewport_height, std::vector<std::string> keys,
    BindCallback bind, void* context) noexcept {
  if (viewport == nullptr || bind == nullptr) {
    return core::RuntimeResult<core::Accepted>::failure(listError(
        core::RuntimeErrorCode::kAbiInvalidArgument,
        "VirtualList host requires a viewport and bind callback"));
  }
  reset();
  viewport_ = viewport;
  row_height_ = row_height;
  bind_ = bind;
  context_ = context;
  auto configured = window_.configure(keys.size(), row_height, viewport_height,
                                      std::move(keys));
  if (!configured) {
    reset();
    return configured;
  }
  auto bound = bindWindow(window_.window());
  if (!bound) reset();
  return bound;
}

core::RuntimeResult<VirtualListWindowSnapshot> VirtualListHost::scrollTo(
    std::int32_t offset) noexcept {
  auto window = window_.scrollTo(offset);
  if (!window) return window;
  auto bound = bindWindow(window.value());
  if (!bound) {
    return core::RuntimeResult<VirtualListWindowSnapshot>::failure(bound.error());
  }
  return window;
}

std::optional<VirtualListItem> VirtualListHost::hitTest(
    std::int32_t y) const noexcept {
  return window_.hitTest(y);
}

lv_obj_t* VirtualListHost::nativeObject(
    std::size_t physical_slot) const noexcept {
  return physical_slot < physical_objects_.size()
             ? physical_objects_[physical_slot]
             : nullptr;
}

void VirtualListHost::reset() noexcept {
  for (auto* object : physical_objects_) {
    if (object != nullptr && lv_obj_is_valid(object)) lv_obj_delete(object);
  }
  physical_objects_.clear();
  viewport_ = nullptr;
  row_height_ = 0;
  bind_ = nullptr;
  context_ = nullptr;
  window_.reset();
}

core::RuntimeResult<core::Accepted> VirtualListHost::bindWindow(
    const VirtualListWindowSnapshot& window) noexcept {
  if (viewport_ == nullptr || bind_ == nullptr) {
    return core::RuntimeResult<core::Accepted>::failure(listError(
        core::RuntimeErrorCode::kLifecycleBusy,
        "VirtualList host is not configured"));
  }
  try {
    std::size_t required = 0;
    for (const auto& item : window.items) {
      required = std::max(required, item.physical_slot + 1);
    }
    while (physical_objects_.size() < required) {
      auto* object = lv_obj_create(viewport_);
      if (object == nullptr) {
        return core::RuntimeResult<core::Accepted>::failure(listError(
            core::RuntimeErrorCode::kOutOfMemory,
            "VirtualList physical object allocation failed"));
      }
      physical_objects_.push_back(object);
    }
    std::vector<bool> active(physical_objects_.size(), false);
    for (const auto& item : window.items) {
      auto* object = physical_objects_[item.physical_slot];
      if (object == nullptr || !lv_obj_is_valid(object) ||
          !bind_(context_, object, item.logical_index, item.key)) {
        return core::RuntimeResult<core::Accepted>::failure(listError(
            core::RuntimeErrorCode::kPlatformRejected,
            "VirtualList item bind failed"));
      }
      lv_obj_set_pos(object, 0, item.y - window.scroll_offset);
      lv_obj_set_width(object, lv_obj_get_width(viewport_));
      lv_obj_set_height(object, row_height_);
      active[item.physical_slot] = true;
    }
    for (std::size_t index = 0; index < physical_objects_.size(); ++index) {
      lv_obj_set_hidden(physical_objects_[index], !active[index]);
    }
    return core::RuntimeResult<core::Accepted>::success(core::Accepted{});
  } catch (...) {
    return core::RuntimeResult<core::Accepted>::failure(listError(
        core::RuntimeErrorCode::kOutOfMemory,
        "VirtualList item binding allocation failed"));
  }
}

}  // namespace quickapp::lvgl::list
