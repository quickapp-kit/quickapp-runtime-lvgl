#pragma once

#include <cstddef>
#include <cstdint>
#include <climits>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include <lvgl.h>

#include "quickapp/core/foundation/error.h"

namespace quickapp::lvgl::list {

struct VirtualListItem final {
  std::size_t logical_index{0};
  std::string key;
  std::size_t physical_slot{0};
  std::int32_t y{0};
};

struct VirtualListWindowSnapshot final {
  std::size_t first_index{0};
  std::size_t last_index_exclusive{0};
  std::int32_t scroll_offset{0};
  std::vector<VirtualListItem> items;
};

// Platform-only window allocator. Core retains the complete logical list;
// this class owns only bounded physical slots and stable visible-key mapping.
class VirtualListWindow final {
 public:
  explicit VirtualListWindow(std::size_t max_physical_slots) noexcept
      : max_physical_slots_(max_physical_slots) {}

  [[nodiscard]] core::RuntimeResult<core::Accepted> configure(
      std::size_t logical_count, std::int32_t row_height,
      std::int32_t viewport_height, std::vector<std::string> keys) noexcept;
  [[nodiscard]] core::RuntimeResult<VirtualListWindowSnapshot> scrollTo(
      std::int32_t offset) noexcept;
  [[nodiscard]] std::optional<VirtualListItem> hitTest(
      std::int32_t y) const noexcept;
  void reset() noexcept;

  [[nodiscard]] std::size_t logicalCount() const noexcept {
    return logical_count_;
  }
  [[nodiscard]] std::size_t physicalSlotCount() const noexcept {
    return max_physical_slots_;
  }
  [[nodiscard]] std::size_t livePhysicalCount() const noexcept {
    return window_.items.size();
  }
  [[nodiscard]] std::int32_t contentHeight() const noexcept {
    return row_height_ > 0 && logical_count_ <=
                                static_cast<std::size_t>(INT32_MAX / row_height_)
                                ? static_cast<std::int32_t>(logical_count_ * row_height_)
                                : INT32_MAX;
  }
  [[nodiscard]] const VirtualListWindowSnapshot& window() const noexcept {
    return window_;
  }

 private:
  [[nodiscard]] core::RuntimeResult<VirtualListWindowSnapshot> rebuild(
      std::int32_t offset) noexcept;

  std::size_t max_physical_slots_{0};
  std::size_t logical_count_{0};
  std::int32_t row_height_{0};
  std::int32_t viewport_height_{0};
  std::vector<std::string> keys_;
  VirtualListWindowSnapshot window_;
};

class VirtualListHost final {
 public:
  using BindCallback = bool (*)(void*, lv_obj_t*, std::size_t,
                                std::string_view) noexcept;

  explicit VirtualListHost(std::size_t max_physical_slots) noexcept
      : window_(max_physical_slots) {}
  ~VirtualListHost() noexcept { reset(); }

  VirtualListHost(const VirtualListHost&) = delete;
  VirtualListHost& operator=(const VirtualListHost&) = delete;

  [[nodiscard]] core::RuntimeResult<core::Accepted> configure(
      lv_obj_t* viewport, std::int32_t row_height,
      std::int32_t viewport_height, std::vector<std::string> keys,
      BindCallback bind, void* context) noexcept;
  [[nodiscard]] core::RuntimeResult<VirtualListWindowSnapshot> scrollTo(
      std::int32_t offset) noexcept;
  [[nodiscard]] std::optional<VirtualListItem> hitTest(
      std::int32_t y) const noexcept;
  [[nodiscard]] lv_obj_t* nativeObject(std::size_t physical_slot) const noexcept;
  [[nodiscard]] std::size_t livePhysicalCount() const noexcept {
    return physical_objects_.size();
  }
  void reset() noexcept;

 private:
  [[nodiscard]] core::RuntimeResult<core::Accepted> bindWindow(
      const VirtualListWindowSnapshot& window) noexcept;

  lv_obj_t* viewport_{nullptr};
  std::int32_t row_height_{0};
  BindCallback bind_{nullptr};
  void* context_{nullptr};
  VirtualListWindow window_;
  std::vector<lv_obj_t*> physical_objects_;
};

}  // namespace quickapp::lvgl::list
