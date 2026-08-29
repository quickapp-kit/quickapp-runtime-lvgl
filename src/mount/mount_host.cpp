#include "quickapp/lvgl/mount/mount_host.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <string_view>
#include <type_traits>
#include <new>

#include <lvgl.h>

#include "quickapp/lvgl/font/system_default_font_asset.h"

// LVGL links its bundled LodePNG decoder, but its public header only exposes
// decoder registration. Keep this private declaration local to the image host.
extern "C" unsigned lodepng_decode32(unsigned char** out, unsigned* width,
                                      unsigned* height, const unsigned char* in,
                                      std::size_t insize);

namespace quickapp::lvgl::mount {
namespace {

constexpr std::int32_t kDefaultFontSize = 16;
constexpr std::size_t kTinyTtfCacheGlyphCount = 2;

using core::RuntimeError;
using core::RuntimeErrorCode;
using core::package::HostComponentType;

RuntimeError error(RuntimeErrorCode code, const char* message,
                   bool retryable = false) noexcept {
  return RuntimeError::simple(code, message, retryable);
}

struct RootLookupContext final {
  PageRootNativeLookup* lookup{nullptr};
  void* root{nullptr};
};

void lookupRoot(void* context, surface::PageRootHandle handle) noexcept {
  auto* state = static_cast<RootLookupContext*>(context);
  state->root = state->lookup->nativeObject(handle);
}

void resetViewChrome(lv_obj_t* object) noexcept {
  if (object == nullptr) return;
  lv_obj_set_style_bg_opa(object, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(object, 0, 0);
  lv_obj_set_style_outline_width(object, 0, 0);
  lv_obj_set_style_shadow_width(object, 0, 0);
  lv_obj_set_style_radius(object, 0, 0);
  lv_obj_set_style_pad_all(object, 0, 0);
  lv_obj_set_scrollbar_mode(object, LV_SCROLLBAR_MODE_OFF);
}

bool sameNode(const std::optional<core::NodeId>& left,
              const core::NodeId& right) noexcept {
  return left.has_value() && *left == right;
}

void onClick(lv_event_t* event) noexcept {
  if (event == nullptr || lv_event_get_code(event) != LV_EVENT_CLICKED) return;
  auto* binding = static_cast<MountHost::ClickBinding*>(lv_event_get_user_data(event));
  if (binding != nullptr && binding->live && binding->callback != nullptr) {
    binding->callback(binding->context, binding->surface_id, binding->node_id,
                      static_cast<std::uint64_t>(lv_tick_get()) * 1000000ULL);
  }
}

void onInput(lv_event_t* event) noexcept {
  if (event == nullptr) return;
  auto* binding = static_cast<MountHost::InputBinding*>(lv_event_get_user_data(event));
  if (binding == nullptr || !binding->live || binding->callback == nullptr) return;
  const auto code = lv_event_get_code(event);
  auto type = core::package::EventType::kInput;
  if (code == LV_EVENT_FOCUSED) type = core::package::EventType::kFocus;
  else if (code != LV_EVENT_VALUE_CHANGED) return;
  auto* target = static_cast<lv_obj_t*>(lv_event_get_target(event));
  const char* value = target == nullptr ? "" : lv_textarea_get_text(target);
  const auto timestamp = static_cast<std::uint64_t>(lv_tick_get()) * 1000000ULL;
  binding->callback(binding->context, binding->surface_id, binding->node_id,
                    type, value == nullptr ? "" : value, timestamp);
  if (type == core::package::EventType::kInput) {
    binding->callback(binding->context, binding->surface_id, binding->node_id,
                      core::package::EventType::kChange,
                      value == nullptr ? "" : value, timestamp);
  }
}

void onSwitch(lv_event_t* event) noexcept {
  if (event == nullptr || lv_event_get_code(event) != LV_EVENT_VALUE_CHANGED) {
    return;
  }
  auto* binding =
      static_cast<MountHost::SwitchBinding*>(lv_event_get_user_data(event));
  if (binding == nullptr || !binding->live || binding->callback == nullptr) {
    return;
  }
  auto* target = static_cast<lv_obj_t*>(lv_event_get_target(event));
  if (target == nullptr) return;
  binding->callback(binding->context, binding->surface_id, binding->node_id,
                    lv_obj_has_state(target, LV_STATE_CHECKED),
                    static_cast<std::uint64_t>(lv_tick_get()) * 1000000ULL);
}

void onSlider(lv_event_t* event) noexcept {
  if (event == nullptr || lv_event_get_code(event) != LV_EVENT_VALUE_CHANGED) {
    return;
  }
  auto* binding =
      static_cast<MountHost::SliderBinding*>(lv_event_get_user_data(event));
  if (binding == nullptr || !binding->live || binding->callback == nullptr) {
    return;
  }
  auto* target = static_cast<lv_obj_t*>(lv_event_get_target(event));
  if (target == nullptr || binding->scale <= 0) return;
  const double native = static_cast<double>(lv_slider_get_value(target));
  const double raw = native / binding->scale;
  const double value = std::clamp(
      binding->minimum +
          std::round((raw - binding->minimum) / binding->step) * binding->step,
      binding->minimum, binding->maximum);
  binding->callback(binding->context, binding->surface_id, binding->node_id,
                    value, lv_event_get_indev(event) != nullptr,
                    static_cast<std::uint64_t>(lv_tick_get()) * 1000000ULL);
}

void onPicker(lv_event_t* event) noexcept {
  if (event == nullptr) return;
  const auto code = lv_event_get_code(event);
  if (code != LV_EVENT_VALUE_CHANGED && code != LV_EVENT_CANCEL) return;
  auto* binding =
      static_cast<MountHost::PickerBinding*>(lv_event_get_user_data(event));
  if (binding == nullptr || !binding->live || binding->callback == nullptr) {
    return;
  }
  auto* target = static_cast<lv_obj_t*>(lv_event_get_target(event));
  if (target == nullptr) return;
  std::array<char, kMaxPropertyText> value{};
  lv_dropdown_get_selected_str(target, value.data(), value.size());
  const auto selected = static_cast<std::int32_t>(lv_dropdown_get_selected(target));
  const auto timestamp = static_cast<std::uint64_t>(lv_tick_get()) * 1000000ULL;
  if (code == LV_EVENT_CANCEL) {
    binding->callback(binding->context, binding->surface_id, binding->node_id,
                      MountHost::PickerEvent::kCancel, selected, value.data(),
                      timestamp);
    return;
  }
  binding->callback(binding->context, binding->surface_id, binding->node_id,
                    MountHost::PickerEvent::kChange, selected, value.data(),
                    timestamp);
  binding->callback(binding->context, binding->surface_id, binding->node_id,
                    MountHost::PickerEvent::kConfirm, selected, value.data(),
                    timestamp);
}

void onTabs(lv_event_t* event) noexcept {
  if (event == nullptr || lv_event_get_code(event) != LV_EVENT_VALUE_CHANGED) {
    return;
  }
  auto* binding =
      static_cast<MountHost::TabsBinding*>(lv_event_get_user_data(event));
  if (binding == nullptr || !binding->live || binding->callback == nullptr) {
    return;
  }
  auto* target = static_cast<lv_obj_t*>(lv_event_get_current_target(event));
  if (target == nullptr) return;
  const auto index = static_cast<std::int32_t>(lv_tabview_get_tab_active(target));
  auto* button = lv_tabview_get_tab_button(target, index);
  auto* label = button == nullptr
                    ? nullptr
                    : lv_obj_get_child_by_type(button, 0, &lv_label_class);
  const char* value = label == nullptr ? "" : lv_label_get_text(label);
  binding->callback(binding->context, binding->surface_id, binding->node_id,
                    index, value == nullptr ? "" : value,
                    static_cast<std::uint64_t>(lv_tick_get()) * 1000000ULL);
}

void onScroll(lv_event_t* event) noexcept {
  if (event == nullptr) return;
  const auto code = lv_event_get_code(event);
  core::package::EventType type{};
  if (code == LV_EVENT_SCROLL) {
    type = core::package::EventType::kScroll;
  } else if (code == LV_EVENT_SCROLL_END) {
    type = core::package::EventType::kScrollEnd;
  } else {
    return;
  }
  auto* binding =
      static_cast<MountHost::ScrollBinding*>(lv_event_get_user_data(event));
  if (binding == nullptr || !binding->live || binding->callback == nullptr) return;
  auto* target = static_cast<lv_obj_t*>(lv_event_get_target(event));
  if (target == nullptr) return;
  const auto offset = lv_obj_get_scroll_y(target);
  const auto content = lv_obj_get_height(target) + lv_obj_get_scroll_bottom(target);
  const auto viewport = lv_obj_get_height(target);
  binding->callback(binding->context, binding->surface_id, binding->node_id,
                    type, offset, content, viewport,
                    static_cast<std::uint64_t>(lv_tick_get()) * 1000000ULL);
  if (code == LV_EVENT_SCROLL && offset <= 0) {
    binding->callback(binding->context, binding->surface_id, binding->node_id,
                      core::package::EventType::kScrollTop, offset, content,
                      viewport, static_cast<std::uint64_t>(lv_tick_get()) * 1000000ULL);
  }
  if (code == LV_EVENT_SCROLL && lv_obj_get_scroll_bottom(target) <= 0) {
    binding->callback(binding->context, binding->surface_id, binding->node_id,
                      core::package::EventType::kScrollBottom, offset, content,
                      viewport, static_cast<std::uint64_t>(lv_tick_get()) * 1000000ULL);
  }
}

void convertRgbaToLvglBgra(std::vector<std::uint8_t>& pixels) noexcept {
  for (std::size_t index = 0; index + 3 < pixels.size(); index += 4) {
    std::swap(pixels[index], pixels[index + 2]);
  }
}

std::vector<std::uint8_t> resizeBgraNearest(
    const std::vector<std::uint8_t>& source, unsigned source_width,
    unsigned source_height, unsigned target_width,
    unsigned target_height) {
  std::vector<std::uint8_t> result(
      static_cast<std::size_t>(target_width) * target_height * 4U);
  for (unsigned y = 0; y < target_height; ++y) {
    const unsigned source_y =
        std::min(source_height - 1U,
                 static_cast<unsigned>((static_cast<std::uint64_t>(y) *
                                        source_height) /
                                       target_height));
    for (unsigned x = 0; x < target_width; ++x) {
      const unsigned source_x =
          std::min(source_width - 1U,
                   static_cast<unsigned>((static_cast<std::uint64_t>(x) *
                                          source_width) /
                                         target_width));
      const auto source_offset =
          (static_cast<std::size_t>(source_y) * source_width + source_x) * 4U;
      const auto target_offset =
          (static_cast<std::size_t>(y) * target_width + x) * 4U;
      std::copy_n(source.data() + source_offset, 4U,
                  result.data() + target_offset);
    }
  }
  return result;
}

std::string_view propertyName(const BoundedText& property) noexcept {
  return property.view();
}

bool parseHexColor(std::string_view value, lv_color_t& color) noexcept {
  if (value.size() != 7 && value.size() != 9) return false;
  if (value[0] != '#') return false;
  std::uint32_t result = 0;
  for (std::size_t index = 1; index < value.size(); ++index) {
    const char c = value[index];
    std::uint32_t nibble = 0;
    if (c >= '0' && c <= '9') nibble = static_cast<std::uint32_t>(c - '0');
    else if (c >= 'a' && c <= 'f') nibble = static_cast<std::uint32_t>(c - 'a' + 10);
    else if (c >= 'A' && c <= 'F') nibble = static_cast<std::uint32_t>(c - 'A' + 10);
    else return false;
    result = (result << 4U) | nibble;
  }
  color = lv_color_hex(result & 0xFFFFFFU);
  return true;
}

}  // namespace

bool MountHost::updateImageDescriptor(HostSlot& slot, void* native_object,
                                      unsigned width, unsigned height) noexcept {
  auto* object = static_cast<lv_obj_t*>(native_object);
  if (slot.image_descriptor != nullptr) {
    delete static_cast<lv_image_dsc_t*>(slot.image_descriptor);
    slot.image_descriptor = nullptr;
  }
  if (width == 0 || height == 0 || slot.image_pixels.empty()) return false;
  auto* descriptor = new (std::nothrow) lv_image_dsc_t{};
  if (descriptor == nullptr) return false;
  descriptor->header.magic = LV_IMAGE_HEADER_MAGIC;
  descriptor->header.cf = LV_COLOR_FORMAT_ARGB8888;
  descriptor->header.w = width;
  descriptor->header.h = height;
  descriptor->header.stride = width * 4U;
  descriptor->data_size = static_cast<std::uint32_t>(slot.image_pixels.size());
  descriptor->data = slot.image_pixels.data();
  slot.image_descriptor = descriptor;
  lv_image_set_src(object, descriptor);
  lv_image_set_inner_align(object, LV_IMAGE_ALIGN_CENTER);
  return true;
}

bool MountHost::resizeImageForLayout(HostSlot& slot, void* native_object) noexcept {
  auto* object = static_cast<lv_obj_t*>(native_object);
  if (object == nullptr || slot.image_source_width == 0 ||
      slot.image_source_height == 0 || slot.image_source_pixels.empty()) {
    return true;
  }
  const auto target_width = static_cast<unsigned>(lv_obj_get_width(object));
  const auto target_height = static_cast<unsigned>(lv_obj_get_height(object));
  if (target_width == 0 || target_height == 0) return true;

  const auto width_scale = static_cast<double>(target_width) /
                           slot.image_source_width;
  const auto height_scale = static_cast<double>(target_height) /
                            slot.image_source_height;
  const auto scale = std::min(1.0, std::min(width_scale, height_scale));
  const auto width = std::max(
      1U, static_cast<unsigned>(slot.image_source_width * scale));
  const auto height = std::max(
      1U, static_cast<unsigned>(slot.image_source_height * scale));
  try {
    slot.image_pixels =
        (width == slot.image_source_width && height == slot.image_source_height)
            ? slot.image_source_pixels
            : resizeBgraNearest(slot.image_source_pixels,
                                slot.image_source_width,
                                slot.image_source_height, width, height);
  } catch (...) {
    return false;
  }
  return updateImageDescriptor(slot, object, width, height);
}

MountHostLimits simulatorMountHostLimits() noexcept {
  return {16, 512, 256, 16, 512 * 1024, 16};
}

MountHostLimits embeddedMountHostLimits() noexcept {
  return {4, 64, 16, 4, 512 * 1024, 4};
}

MountHost::MountHost(foundation::OwnerTaskQueue& owner_tasks,
                     foundation::OwnerToken owner,
                     surface::SurfaceHostAdapter& surfaces,
                     PageRootNativeLookup& roots, MountResultSink& results,
                     MountHostLimits limits) noexcept
    : owner_tasks_(owner_tasks),
      owner_(owner),
      surfaces_(surfaces),
      roots_(roots),
      results_(results),
      limits_(limits) {
  if (limits_.max_batch_queue_depth == 0) {
    limits_.max_batch_queue_depth = limits_.max_transactions;
  }
  if (!owner_.valid() || limits_.max_transactions == 0 ||
      limits_.max_transactions > kStorageCapacity ||
      limits_.max_host_objects == 0 || limits_.max_host_objects > objects_.size() ||
      limits_.max_operations == 0 || limits_.max_operations > kMaxMountOperations ||
      limits_.max_font_instances == 0 ||
      limits_.max_font_instances > fonts_.size() || limits_.max_batch_bytes == 0 ||
      limits_.max_batch_queue_depth > limits_.max_transactions) {
    accepting_.store(false, std::memory_order_release);
  }
}

bool MountHost::isOwner(foundation::OwnerToken caller) const noexcept {
  return caller.valid() && caller == owner_;
}

std::optional<std::size_t> MountHost::findSlot(
    const core::SurfaceId& surface_id,
    const core::NodeId& node_id) const noexcept {
  for (std::size_t index = 0; index < limits_.max_host_objects; ++index) {
    if (objects_[index].live && objects_[index].surface_id.has_value() &&
        *objects_[index].surface_id == surface_id &&
        sameNode(objects_[index].node_id, node_id)) {
      return index;
    }
  }
  return std::nullopt;
}

std::optional<std::size_t> MountHost::freeSlot() const noexcept {
  for (std::size_t index = 0; index < limits_.max_host_objects; ++index) {
    if (!objects_[index].live) return index;
  }
  return std::nullopt;
}

std::size_t MountHost::liveObjectCount() const noexcept {
  std::size_t count = 0;
  for (std::size_t index = 0; index < limits_.max_host_objects; ++index) {
    if (objects_[index].live) ++count;
  }
  return count;
}

std::size_t MountHost::liveFontCount() const noexcept {
  std::size_t count = 0;
  for (std::size_t index = 0; index < limits_.max_font_instances; ++index) {
    if (fonts_[index].live) ++count;
  }
  return count;
}

std::size_t MountHost::liveObjectCountForSurface(
    const core::SurfaceId& surface_id) const noexcept {
  std::size_t count = 0;
  for (std::size_t index = 0; index < limits_.max_host_objects; ++index) {
    if (objects_[index].live && objects_[index].surface_id.has_value() &&
        *objects_[index].surface_id == surface_id) {
      ++count;
      if (objects_[index].private_label != nullptr) ++count;
    }
  }
  return count;
}

std::size_t MountHost::pendingCount() const noexcept {
  std::size_t count = 0;
  for (std::size_t index = 0; index < limits_.max_transactions; ++index) {
    if (transactions_[index].occupied) ++count;
  }
  return count;
}

std::optional<core::NodeId> MountHost::nodeAt(
    const core::SurfaceId& surface_id, std::int32_t x,
    std::int32_t y) const noexcept {
  std::optional<core::NodeId> result;
  std::int32_t best_area = std::numeric_limits<std::int32_t>::max();
  for (std::size_t index = 0; index < limits_.max_host_objects; ++index) {
    const auto& slot = objects_[index];
    if (!slot.live || !slot.node_id || !slot.surface_id ||
        *slot.surface_id != surface_id || slot.native_object == nullptr) {
      continue;
    }
    lv_area_t area{};
    lv_obj_get_coords(static_cast<lv_obj_t*>(slot.native_object), &area);
    if (x < area.x1 || x > area.x2 || y < area.y1 || y > area.y2) continue;
    const std::int64_t width = static_cast<std::int64_t>(area.x2) - area.x1 + 1;
    const std::int64_t height = static_cast<std::int64_t>(area.y2) - area.y1 + 1;
    const auto candidate_area = width * height;
    if (candidate_area < best_area) {
      best_area = candidate_area > std::numeric_limits<std::int32_t>::max()
                      ? std::numeric_limits<std::int32_t>::max()
                      : static_cast<std::int32_t>(candidate_area);
      result = slot.node_id;
    }
  }
  return result;
}

void* MountHost::nativeObject(const core::SurfaceId& surface_id,
                              const core::NodeId& node_id) const noexcept {
  const auto slot = findSlot(surface_id, node_id);
  return slot.has_value() ? objects_[*slot].native_object : nullptr;
}

std::vector<MountHost::ImageSnapshot> MountHost::imageSnapshots(
    const core::SurfaceId& surface_id) const {
  std::vector<ImageSnapshot> result;
  for (const auto& slot : objects_) {
    if (!slot.live || !slot.surface_id || !slot.node_id ||
        *slot.surface_id != surface_id || slot.type != HostComponentType::kImage) {
      continue;
    }
    result.push_back(ImageSnapshot{*slot.node_id, slot.native_object,
                                   slot.image_descriptor != nullptr,
                                   slot.image_pixels.size()});
  }
  return result;
}

bool MountHost::installClickHandler(const core::SurfaceId& surface_id,
                                    const core::NodeId& node_id,
                                    ClickCallback callback,
                                    void* context) noexcept {
  if (callback == nullptr) return false;
  const auto slot = findSlot(surface_id, node_id);
  if (!slot) return false;
  for (auto& binding : click_bindings_) {
    if (binding.live && binding.surface_id == surface_id &&
        binding.node_id == node_id) {
      binding.callback = callback;
      binding.context = context;
      return true;
    }
  }
  for (auto& binding : click_bindings_) {
    if (!binding.live) {
      binding.live = true;
      binding.surface_id = surface_id;
      binding.node_id = node_id;
      binding.callback = callback;
      binding.context = context;
      lv_obj_add_event_cb(static_cast<lv_obj_t*>(objects_[*slot].native_object),
                          onClick, LV_EVENT_CLICKED, &binding);
      return true;
    }
  }
  return false;
}

bool MountHost::installInputHandler(const core::SurfaceId& surface_id,
                                    const core::NodeId& node_id,
                                    InputCallback callback,
                                    void* context) noexcept {
  if (callback == nullptr) return false;
  const auto slot = findSlot(surface_id, node_id);
  if (!slot || objects_[*slot].type != HostComponentType::kInput) return false;
  for (auto& binding : input_bindings_) {
    if (binding.live && binding.surface_id == surface_id && binding.node_id == node_id) {
      binding.callback = callback;
      binding.context = context;
      return true;
    }
  }
  for (auto& binding : input_bindings_) {
    if (!binding.live) {
      binding.live = true;
      binding.surface_id = surface_id;
      binding.node_id = node_id;
      binding.callback = callback;
      binding.context = context;
      auto* object = static_cast<lv_obj_t*>(objects_[*slot].native_object);
      lv_obj_add_event_cb(object, onInput, LV_EVENT_FOCUSED, &binding);
      lv_obj_add_event_cb(object, onInput, LV_EVENT_VALUE_CHANGED, &binding);
      return true;
    }
  }
  return false;
}

bool MountHost::installSwitchHandler(const core::SurfaceId& surface_id,
                                     const core::NodeId& node_id,
                                     SwitchCallback callback,
                                     void* context) noexcept {
  if (callback == nullptr) return false;
  const auto slot = findSlot(surface_id, node_id);
  if (!slot || objects_[*slot].type != HostComponentType::kSwitch) return false;
  for (auto& binding : switch_bindings_) {
    if (binding.live && binding.surface_id == surface_id &&
        binding.node_id == node_id) {
      binding.callback = callback;
      binding.context = context;
      return true;
    }
  }
  for (auto& binding : switch_bindings_) {
    if (!binding.live) {
      binding.live = true;
      binding.surface_id = surface_id;
      binding.node_id = node_id;
      binding.callback = callback;
      binding.context = context;
      lv_obj_add_event_cb(static_cast<lv_obj_t*>(objects_[*slot].native_object),
                          onSwitch, LV_EVENT_VALUE_CHANGED, &binding);
      return true;
    }
  }
  return false;
}

bool MountHost::installSliderHandler(const core::SurfaceId& surface_id,
                                     const core::NodeId& node_id,
                                     SliderCallback callback,
                                     void* context) noexcept {
  if (callback == nullptr) return false;
  const auto slot = findSlot(surface_id, node_id);
  if (!slot || objects_[*slot].type != HostComponentType::kSlider) return false;
  for (auto& binding : slider_bindings_) {
    if (binding.live && binding.surface_id == surface_id &&
        binding.node_id == node_id) {
      binding.callback = callback;
      binding.context = context;
      binding.minimum = objects_[*slot].slider_minimum;
      binding.maximum = objects_[*slot].slider_maximum;
      binding.step = objects_[*slot].slider_step;
      binding.scale = objects_[*slot].slider_scale;
      return true;
    }
  }
  for (auto& binding : slider_bindings_) {
    if (!binding.live) {
      binding.live = true;
      binding.surface_id = surface_id;
      binding.node_id = node_id;
      binding.callback = callback;
      binding.context = context;
      binding.minimum = objects_[*slot].slider_minimum;
      binding.maximum = objects_[*slot].slider_maximum;
      binding.step = objects_[*slot].slider_step;
      binding.scale = objects_[*slot].slider_scale;
      lv_obj_add_event_cb(static_cast<lv_obj_t*>(objects_[*slot].native_object),
                          onSlider, LV_EVENT_VALUE_CHANGED, &binding);
      return true;
    }
  }
  return false;
}

bool MountHost::installPickerHandler(const core::SurfaceId& surface_id,
                                     const core::NodeId& node_id,
                                     PickerCallback callback,
                                     void* context) noexcept {
  if (callback == nullptr) return false;
  const auto slot = findSlot(surface_id, node_id);
  if (!slot || objects_[*slot].type != HostComponentType::kPicker) return false;
  for (auto& binding : picker_bindings_) {
    if (binding.live && binding.surface_id == surface_id &&
        binding.node_id == node_id) {
      binding.callback = callback;
      binding.context = context;
      return true;
    }
  }
  for (auto& binding : picker_bindings_) {
    if (!binding.live) {
      binding.live = true;
      binding.surface_id = surface_id;
      binding.node_id = node_id;
      binding.callback = callback;
      binding.context = context;
      lv_obj_add_event_cb(static_cast<lv_obj_t*>(objects_[*slot].native_object),
                          onPicker, LV_EVENT_VALUE_CHANGED, &binding);
      lv_obj_add_event_cb(static_cast<lv_obj_t*>(objects_[*slot].native_object),
                          onPicker, LV_EVENT_CANCEL, &binding);
      return true;
    }
  }
  return false;
}

bool MountHost::confirmPicker(const core::SurfaceId& surface_id,
                              const core::NodeId& node_id) noexcept {
  const auto slot = findSlot(surface_id, node_id);
  if (!slot || objects_[*slot].type != HostComponentType::kPicker) return false;
  lv_obj_send_event(static_cast<lv_obj_t*>(objects_[*slot].native_object),
                    LV_EVENT_VALUE_CHANGED, nullptr);
  return true;
}

bool MountHost::cancelPicker(const core::SurfaceId& surface_id,
                             const core::NodeId& node_id) noexcept {
  const auto slot = findSlot(surface_id, node_id);
  if (!slot || objects_[*slot].type != HostComponentType::kPicker) return false;
  lv_obj_send_event(static_cast<lv_obj_t*>(objects_[*slot].native_object),
                    LV_EVENT_CANCEL, nullptr);
  return true;
}

bool MountHost::installTabsHandler(const core::SurfaceId& surface_id,
                                   const core::NodeId& node_id,
                                   TabsCallback callback,
                                   void* context) noexcept {
  if (callback == nullptr) return false;
  const auto slot = findSlot(surface_id, node_id);
  if (!slot || objects_[*slot].type != HostComponentType::kTabs) return false;
  for (auto& binding : tabs_bindings_) {
    if (binding.live && binding.surface_id == surface_id &&
        binding.node_id == node_id) {
      binding.callback = callback;
      binding.context = context;
      return true;
    }
  }
  for (auto& binding : tabs_bindings_) {
    if (!binding.live) {
      binding.live = true;
      binding.surface_id = surface_id;
      binding.node_id = node_id;
      binding.callback = callback;
      binding.context = context;
      lv_obj_add_event_cb(static_cast<lv_obj_t*>(objects_[*slot].native_object),
                          onTabs, LV_EVENT_VALUE_CHANGED, &binding);
      return true;
    }
  }
  return false;
}

bool MountHost::installScrollHandler(const core::SurfaceId& surface_id,
                                     const core::NodeId& node_id,
                                     ScrollCallback callback,
                                     void* context) noexcept {
  if (callback == nullptr) return false;
  const auto slot = findSlot(surface_id, node_id);
  if (!slot || (objects_[*slot].type != HostComponentType::kScroll &&
                objects_[*slot].type != HostComponentType::kList)) {
    return false;
  }
  for (auto& binding : scroll_bindings_) {
    if (binding.live && binding.surface_id == surface_id &&
        binding.node_id == node_id) {
      binding.callback = callback;
      binding.context = context;
      return true;
    }
  }
  for (auto& binding : scroll_bindings_) {
    if (!binding.live) {
      binding.live = true;
      binding.surface_id = surface_id;
      binding.node_id = node_id;
      binding.callback = callback;
      binding.context = context;
      auto* object = static_cast<lv_obj_t*>(objects_[*slot].native_object);
      lv_obj_add_event_cb(object, onScroll, LV_EVENT_SCROLL, &binding);
      lv_obj_add_event_cb(object, onScroll, LV_EVENT_SCROLL_END, &binding);
      return true;
    }
  }
  return false;
}

void MountHost::setResource(
    std::string path,
    std::shared_ptr<const std::vector<std::uint8_t>> bytes) noexcept {
  if (path.empty() || bytes == nullptr) return;
  resources_[std::move(path)] = std::move(bytes);
}

foundation::LocalResult MountHost::resolveRoot(const core::SurfaceId& surface_id,
                                               void*& root) noexcept {
  RootLookupContext context{&roots_, nullptr};
  const auto result = surfaces_.withPageRootForMount(
      owner_, surface_id, &context, &lookupRoot);
  root = context.root;
  if (!result.ok() || root == nullptr) {
    return foundation::LocalResult::failure(
        result.ok() ? foundation::LocalError::kBackendFailed : result.error);
  }
  return foundation::LocalResult::success();
}

std::optional<std::size_t> MountHost::acquireFont(
    std::int32_t size) noexcept {
  if (size < font::kSystemDefaultFontMinSize ||
      size > font::kSystemDefaultFontMaxSize) {
    return std::nullopt;
  }
  for (std::size_t index = 0; index < limits_.max_font_instances; ++index) {
    if (fonts_[index].native_font != nullptr && fonts_[index].size == size) {
      fonts_[index].live = true;
      ++fonts_[index].references;
      return index;
    }
  }
  std::size_t free = limits_.max_font_instances;
  for (std::size_t index = 0; index < limits_.max_font_instances; ++index) {
    if (fonts_[index].native_font == nullptr) {
      free = index;
      break;
    }
  }
  if (free == limits_.max_font_instances) return std::nullopt;
  const auto bytes = font::systemDefaultFontBytes();
  lv_font_t* native = lv_tiny_ttf_create_data_ex(
      bytes.data(), bytes.size(), size, LV_FONT_KERNING_NONE,
      kTinyTtfCacheGlyphCount);
  if (native == nullptr) return std::nullopt;
  fonts_[free] = FontSlot{true, size, 1, native};
  return free;
}

void MountHost::releaseFont(std::size_t index) noexcept {
  if (index >= limits_.max_font_instances || !fonts_[index].live ||
      fonts_[index].references == 0) {
    return;
  }
  --fonts_[index].references;
  if (fonts_[index].references == 0) fonts_[index].live = false;
}

void MountHost::destroyFonts() noexcept {
  for (std::size_t index = 0; index < limits_.max_font_instances; ++index) {
    if (fonts_[index].native_font != nullptr) {
      lv_tiny_ttf_destroy(static_cast<lv_font_t*>(fonts_[index].native_font));
    }
    fonts_[index] = FontSlot{};
  }
}

bool MountHost::applySliderConfiguration(HostSlot& slot,
                                         void* native_object) noexcept {
  if (native_object == nullptr || slot.slider_maximum <= slot.slider_minimum ||
      slot.slider_step <= 0 || !std::isfinite(slot.slider_minimum) ||
      !std::isfinite(slot.slider_maximum) || !std::isfinite(slot.slider_step)) {
    return false;
  }
  const auto native_min = static_cast<std::int64_t>(std::llround(
      slot.slider_minimum * slot.slider_scale));
  const auto native_max = static_cast<std::int64_t>(std::llround(
      slot.slider_maximum * slot.slider_scale));
  if (native_min < std::numeric_limits<std::int32_t>::min() ||
      native_max > std::numeric_limits<std::int32_t>::max() ||
      native_min >= native_max) {
    return false;
  }
  auto* object = static_cast<lv_obj_t*>(native_object);
  lv_slider_set_range(object, static_cast<std::int32_t>(native_min),
                      static_cast<std::int32_t>(native_max));
  const double current = static_cast<double>(lv_slider_get_value(object)) /
                         slot.slider_scale;
  const double quantized = std::clamp(
      slot.slider_minimum +
          std::round((current - slot.slider_minimum) / slot.slider_step) *
              slot.slider_step,
      slot.slider_minimum, slot.slider_maximum);
  const auto native_value = static_cast<std::int64_t>(std::llround(
      quantized * slot.slider_scale));
  if (native_value < native_min || native_value > native_max) return false;
  lv_slider_set_value(object, static_cast<std::int32_t>(native_value),
                      LV_ANIM_OFF);
  return true;
}

bool MountHost::applyPickerSelection(HostSlot& slot,
                                     void* native_object) noexcept {
  if (native_object == nullptr || slot.picker_selected < 0) return false;
  auto* object = static_cast<lv_obj_t*>(native_object);
  const auto count = lv_dropdown_get_option_count(object);
  if (count == 0 || static_cast<std::uint32_t>(slot.picker_selected) >= count) {
    return false;
  }
  lv_dropdown_set_selected(object,
                           static_cast<std::uint32_t>(slot.picker_selected));
  return true;
}

bool MountHost::applyTabsSelection(HostSlot& slot,
                                   void* native_object) noexcept {
  if (native_object == nullptr || slot.type != HostComponentType::kTabs ||
      slot.tabs_selected < 0 ||
      static_cast<std::size_t>(slot.tabs_selected) >= slot.tabs_items.size()) {
    return false;
  }
  lv_tabview_set_active(static_cast<lv_obj_t*>(native_object),
                        static_cast<std::uint32_t>(slot.tabs_selected),
                        LV_ANIM_OFF);
  return true;
}

bool MountHost::preflight(const MountTransaction& transaction,
                          surface::PageRootHandle*) noexcept {
  if (transaction.operation_count == 0 || transaction.source_id.truncated ||
      transaction.operation_count > limits_.max_operations ||
      transaction.batch_count == 0 ||
      transaction.batch_index >= transaction.batch_count ||
      transaction.is_final !=
          (transaction.batch_index + 1 == transaction.batch_count) ||
      (transaction.batch_count > 1 && transaction.mode == MountMode::kFull &&
       transaction.batch_index != 0) ||
      transaction.source_id.size == 0 || transaction.source_id.size > kMaxPropertyText) {
    return false;
  }
  if (transaction.mode == MountMode::kFull) {
    bool first_create = false;
    for (std::size_t index = 0; index < transaction.operation_count; ++index) {
      const auto& operation = transaction.operations[index];
      if (std::holds_alternative<CreateHost>(operation)) {
        if (!first_create) first_create = true;
      } else if (std::holds_alternative<MoveHost>(operation) ||
                 std::holds_alternative<RemoveHost>(operation)) {
        return false;
      }
    }
    if (!first_create) return false;
  }

  const auto existsOrCreated = [&](const core::NodeId& node_id,
                                   std::size_t before) noexcept {
    if (transaction.mode == MountMode::kIncremental &&
        findSlot(transaction.surface_id, node_id).has_value()) {
      return true;
    }
    for (std::size_t prior = 0; prior < before; ++prior) {
      const auto* created = std::get_if<CreateHost>(&transaction.operations[prior]);
      if (created != nullptr && created->node_id == node_id) return true;
    }
    return false;
  };
  const auto createdBefore = [&](const core::NodeId& node_id,
                                 std::size_t before) noexcept {
    for (std::size_t prior = 0; prior < before; ++prior) {
      const auto* created = std::get_if<CreateHost>(&transaction.operations[prior]);
      if (created != nullptr && created->node_id == node_id) return true;
    }
    return false;
  };

  for (std::size_t index = 0; index < transaction.operation_count; ++index) {
    const auto& operation = transaction.operations[index];
    bool valid = true;
    std::visit(
        [&](const auto& value) noexcept {
          using Value = std::decay_t<decltype(value)>;
          if constexpr (std::is_same_v<Value, CreateHost>) {
            valid = transaction.mode == MountMode::kFull ||
                    !findSlot(transaction.surface_id, value.node_id).has_value();
            for (std::size_t prior = 0; valid && prior < index; ++prior) {
              if (const auto* prior_create =
                      std::get_if<CreateHost>(&transaction.operations[prior]);
                  prior_create != nullptr && prior_create->node_id == value.node_id) {
                valid = false;
              }
            }
          } else if constexpr (std::is_same_v<Value, SetHostProp>) {
            valid = existsOrCreated(value.node_id, index);
            const auto name = propertyName(value.property);
            valid = valid && !value.property.truncated &&
                    (name == "text" || name == "enabled" ||
                              name == "value" || name == "checked" ||
                              name == "min" || name == "max" || name == "step" ||
                              name == "mode" || name == "range" || name == "selected" ||
                              name == "items" ||
                              name == "src" ||
                              name == "backgroundColor" || name == "color" ||
                              name == "borderRadius" || name == "textAlign" ||
                              name == "fontSize");
            if (const auto* text = std::get_if<BoundedText>(&value.value);
                text != nullptr && text->truncated) {
              valid = false;
            }
            if (name == "fontSize") {
              const auto* size = std::get_if<std::int32_t>(&value.value);
              valid = valid && size != nullptr &&
                      *size >= font::kSystemDefaultFontMinSize &&
                      *size <= font::kSystemDefaultFontMaxSize;
            }
          } else if constexpr (std::is_same_v<Value, SetHostLayout>) {
            valid = existsOrCreated(value.node_id, index) && value.rect.width >= 0 &&
                    value.rect.height >= 0;
          } else if constexpr (std::is_same_v<Value, InsertHostChild>) {
            valid = (createdBefore(value.node_id, index) ||
                     (transaction.batch_index > 0 &&
                      findSlot(transaction.surface_id, value.node_id).has_value())) &&
                    existsOrCreated(value.parent_node_id, index);
          } else if constexpr (std::is_same_v<Value, MoveHost>) {
            const auto child_slot =
                findSlot(transaction.surface_id, value.node_id);
            const auto parent_slot =
                findSlot(transaction.surface_id, value.new_parent_node_id);
            valid = child_slot.has_value() && parent_slot.has_value();
            if (valid) {
              auto* child = static_cast<lv_obj_t*>(
                  objects_[*child_slot].native_object);
              auto* parent = static_cast<lv_obj_t*>(
                  objects_[*parent_slot].native_object);
              for (auto* current = parent; current != nullptr && lv_obj_is_valid(current);
                   current = lv_obj_get_parent(current)) {
                if (current == child) {
                  valid = false;
                  break;
                }
              }
            }
          } else if constexpr (std::is_same_v<Value, RemoveHost>) {
            valid =
                findSlot(transaction.surface_id, value.node_id).has_value();
          }
        },
        operation);
    if (!valid) return false;
  }
  return true;
}

MountResult MountHost::execute(const MountTransaction& transaction) noexcept {
  MountResult result{transaction.surface_id, transaction.revision,
                     transaction.mount_attempt_id, transaction.source_id,
                     MountResultStatus::kFailed, std::nullopt,
                     liveObjectCountForSurface(transaction.surface_id),
                     transaction.batch_index, transaction.batch_count,
                     transaction.is_final};
  void* root = nullptr;
  const char* failure_reason = "mount commit failed";
  if (!resolveRoot(transaction.surface_id, root).ok() ||
      !preflight(transaction, nullptr)) {
    if (transaction.batch_count > 1) {
      // A failed batch belongs to one logical hidden mount. Do not leave the
      // objects created by earlier batches attached to the staging root.
      destroySurfaceObjects(transaction.surface_id);
      result.live_objects = 0;
    }
    result.error = error(RuntimeErrorCode::kPlatformRejected,
                         "mount preflight rejected");
    return result;
  }

  if (transaction.mode == MountMode::kFull) {
    destroySurfaceObjects(transaction.surface_id);
  }

  for (std::size_t index = 0; index < transaction.operation_count; ++index) {
    bool succeeded = true;
    std::visit(
        [&](const auto& value) noexcept {
          using Value = std::decay_t<decltype(value)>;
          if constexpr (std::is_same_v<Value, CreateHost>) {
            const auto free = freeSlot();
            if (!free.has_value()) {
              failure_reason = "host object capacity exhausted";
              succeeded = false;
              return;
            }
            lv_obj_t* parent = static_cast<lv_obj_t*>(root);
            lv_obj_t* object = nullptr;
            if (value.type == HostComponentType::kText) {
              object = lv_label_create(parent);
            } else if (value.type == HostComponentType::kButton) {
              object = lv_button_create(parent);
            } else if (value.type == HostComponentType::kInput) {
              object = lv_textarea_create(parent);
              if (object != nullptr) lv_textarea_set_one_line(object, true);
            } else if (value.type == HostComponentType::kSwitch) {
              object = lv_switch_create(parent);
            } else if (value.type == HostComponentType::kSlider) {
              object = lv_slider_create(parent);
            } else if (value.type == HostComponentType::kPicker) {
              object = lv_dropdown_create(parent);
            } else if (value.type == HostComponentType::kTabs) {
              object = lv_tabview_create(parent);
            } else if (value.type == HostComponentType::kScroll) {
              object = lv_obj_create(parent);
              if (object != nullptr) {
                lv_obj_set_scrollable(object, true);
                lv_obj_set_scroll_dir(object, LV_DIR_VER);
                lv_obj_set_scrollbar_mode(object, LV_SCROLLBAR_MODE_AUTO);
              }
            } else if (value.type == HostComponentType::kList) {
              // list is a semantic alias for scroll — same vertical scrolling.
              object = lv_obj_create(parent);
              if (object != nullptr) {
                lv_obj_set_scrollable(object, true);
                lv_obj_set_scroll_dir(object, LV_DIR_VER);
                lv_obj_set_scrollbar_mode(object, LV_SCROLLBAR_MODE_AUTO);
              }
            } else if (value.type == HostComponentType::kImage) {
              object = lv_image_create(parent);
            } else {
              object = lv_obj_create(parent);
            }
            if (object == nullptr) {
              failure_reason = "host object creation failed";
              succeeded = false;
              return;
            }
            auto& slot = objects_[*free];
            slot.live = true;
            slot.attached = index == 0 && transaction.mode == MountMode::kFull;
            slot.surface_id = transaction.surface_id;
            slot.node_id = value.node_id;
            slot.type = value.type;
            slot.native_object = object;
            if (value.type == HostComponentType::kView ||
                value.type == HostComponentType::kList ||
                value.type == HostComponentType::kScroll) {
              resetViewChrome(object);
              if (value.type == HostComponentType::kScroll ||
                  value.type == HostComponentType::kList) {
                lv_obj_set_scrollable(object, true);
                lv_obj_set_scroll_dir(object, LV_DIR_VER);
                lv_obj_set_scrollbar_mode(object, LV_SCROLLBAR_MODE_AUTO);
              }
            }
            if (value.type == HostComponentType::kText) {
              lv_label_set_text(object, "");
            } else if (value.type == HostComponentType::kButton) {
              slot.private_label = lv_label_create(object);
              if (slot.private_label == nullptr) {
                failure_reason = "button label creation failed";
                succeeded = false;
              } else {
                lv_obj_align(static_cast<lv_obj_t*>(slot.private_label), LV_ALIGN_CENTER, 0, 0);
              }
            } else if (value.type == HostComponentType::kInput) {
              lv_textarea_set_text(object, "");
            } else if (value.type == HostComponentType::kTabs) {
              resetViewChrome(object);
            }
            if (succeeded && value.type != HostComponentType::kView) {
              const auto default_font = acquireFont(kDefaultFontSize);
              succeeded = default_font.has_value();
              if (!succeeded) failure_reason = "default font allocation failed";
              if (succeeded) {
                auto* target = static_cast<lv_obj_t*>(
                    slot.private_label != nullptr ? slot.private_label : object);
                lv_obj_set_style_text_font(
                    target,
                    static_cast<lv_font_t*>(fonts_[*default_font].native_font), 0);
                slot.font_slot = default_font;
              }
            }
          } else if constexpr (std::is_same_v<Value, SetHostProp>) {
            const auto slot_index =
                findSlot(transaction.surface_id, value.node_id);
            if (!slot_index.has_value()) {
              failure_reason = "host property target not found";
              succeeded = false;
              return;
            }
            auto& slot = objects_[*slot_index];
            auto* object = static_cast<lv_obj_t*>(slot.native_object);
            const auto name = propertyName(value.property);
            if (name == "text") {
              const auto* text = std::get_if<BoundedText>(&value.value);
              auto* target = static_cast<lv_obj_t*>(slot.private_label != nullptr
                                                         ? slot.private_label
                                                         : object);
              succeeded = text != nullptr;
              if (succeeded) {
                std::string value(text->view());
                lv_label_set_text(target, value.c_str());
              }
            } else if (name == "enabled") {
              const auto* enabled = std::get_if<bool>(&value.value);
              succeeded = enabled != nullptr &&
                          (slot.type == HostComponentType::kButton ||
                           slot.type == HostComponentType::kInput ||
                           slot.type == HostComponentType::kSwitch);
              if (succeeded) lv_obj_set_state(object, LV_STATE_DISABLED, !*enabled);
            } else if (name == "checked") {
              const auto* checked = std::get_if<bool>(&value.value);
              succeeded = checked != nullptr && slot.type == HostComponentType::kSwitch;
              if (succeeded) {
                lv_obj_set_state(object, LV_STATE_CHECKED, *checked);
              }
            } else if (name == "min" || name == "max" || name == "step") {
              const auto* number = std::get_if<double>(&value.value);
              succeeded = number != nullptr && slot.type == HostComponentType::kSlider &&
                          std::isfinite(*number);
              if (succeeded) {
                if (name == "min") slot.slider_minimum = *number;
                else if (name == "max") slot.slider_maximum = *number;
                else slot.slider_step = *number;
                succeeded = applySliderConfiguration(slot, object);
                if (!succeeded) failure_reason = "slider configuration invalid";
              }
            } else if (name == "value") {
              if (slot.type == HostComponentType::kInput) {
                const auto* text = std::get_if<BoundedText>(&value.value);
                succeeded = text != nullptr;
                if (succeeded) {
                  std::string value(text->view());
                  lv_textarea_set_text(object, value.c_str());
                }
              } else if (slot.type == HostComponentType::kSlider) {
                const auto* number = std::get_if<double>(&value.value);
                succeeded = number != nullptr && std::isfinite(*number);
                if (succeeded) {
                  const double clamped = std::clamp(*number, slot.slider_minimum,
                                                    slot.slider_maximum);
                  const double steps = std::round(
                      (clamped - slot.slider_minimum) / slot.slider_step);
                  const double quantized = slot.slider_minimum +
                                            steps * slot.slider_step;
                  const auto native = static_cast<std::int64_t>(std::llround(
                      quantized * slot.slider_scale));
                  succeeded = native >= std::numeric_limits<std::int32_t>::min() &&
                              native <= std::numeric_limits<std::int32_t>::max();
                  if (succeeded) {
                    lv_slider_set_value(object, static_cast<std::int32_t>(native),
                                        LV_ANIM_OFF);
                  }
                }
              } else {
                succeeded = false;
              }
            } else if (name == "mode") {
              const auto* mode = std::get_if<BoundedText>(&value.value);
              succeeded = mode != nullptr && slot.type == HostComponentType::kPicker &&
                          mode->view() == "text";
            } else if (name == "range") {
              const auto* range = std::get_if<BoundedText>(&value.value);
              succeeded = range != nullptr && slot.type == HostComponentType::kPicker;
              if (succeeded) {
                std::string options(range->view());
                std::replace(options.begin(), options.end(), '|', '\n');
                lv_dropdown_set_options(object, options.c_str());
              }
            } else if (name == "selected") {
              const auto* selected = std::get_if<double>(&value.value);
              succeeded = selected != nullptr &&
                          (slot.type == HostComponentType::kPicker ||
                           slot.type == HostComponentType::kTabs) &&
                          std::isfinite(*selected) && *selected >= 0 &&
                          std::floor(*selected) == *selected &&
                          *selected <= static_cast<double>(std::numeric_limits<std::uint32_t>::max());
              if (succeeded) {
                if (slot.type == HostComponentType::kPicker) {
                  slot.picker_selected = static_cast<std::int32_t>(*selected);
                  succeeded = applyPickerSelection(slot, object);
                } else {
                  slot.tabs_selected = static_cast<std::int32_t>(*selected);
                  // Properties are decoded from an object map. Keep the
                  // controlled value if items has not been applied yet; the
                  // items operation applies it once native tabs exist.
                  succeeded = slot.tabs_items.empty() ||
                              applyTabsSelection(slot, object);
                }
              }
            } else if (name == "items") {
              const auto* items = std::get_if<BoundedText>(&value.value);
              succeeded = items != nullptr && slot.type == HostComponentType::kTabs &&
                          !items->truncated && !items->view().empty();
              if (succeeded) {
                slot.tabs_items.clear();
                std::string item_text(items->view());
                std::size_t start = 0;
                while (start <= item_text.size()) {
                  const auto end = item_text.find('|', start);
                  const auto token = item_text.substr(
                      start, end == std::string::npos ? std::string::npos : end - start);
                  if (token.empty()) {
                    succeeded = false;
                    break;
                  }
                  slot.tabs_items.push_back(token);
                  if (end == std::string::npos) break;
                  start = end + 1;
                }
                succeeded = succeeded && !slot.tabs_items.empty() &&
                            slot.tabs_items.size() <= 32;
                if (succeeded) {
                  for (const auto& item : slot.tabs_items) {
                    if (lv_tabview_add_tab(object, item.c_str()) == nullptr) {
                      succeeded = false;
                      break;
                    }
                  }
                  if (succeeded) succeeded = applyTabsSelection(slot, object);
                }
              }
            } else if (name == "src") {
              const auto* text = std::get_if<BoundedText>(&value.value);
              const auto resource = text == nullptr
                                        ? resources_.end()
                                        : resources_.find(std::string(text->view()));
              succeeded = text != nullptr && slot.type == HostComponentType::kImage &&
                          resource != resources_.end();
              if (!succeeded) failure_reason = "image resource is unavailable";
              if (succeeded) {
                if (slot.image_descriptor != nullptr) {
                  delete static_cast<lv_image_dsc_t*>(slot.image_descriptor);
                  slot.image_descriptor = nullptr;
                }
                unsigned char* decoded = nullptr;
                unsigned width = 0;
                unsigned height = 0;
                const auto& bytes = *resource->second;
                auto decoded_resource = decoded_resources_.find(std::string(text->view()));
                if (decoded_resource == decoded_resources_.end()) {
                  const unsigned code = lodepng_decode32(
                      &decoded, &width, &height, bytes.data(), bytes.size());
                  auto* decoded_buffer =
                      reinterpret_cast<lv_draw_buf_t*>(decoded);
                  if (code != 0 || decoded_buffer == nullptr || width == 0 ||
                      height == 0 || decoded_buffer->data == nullptr ||
                      decoded_buffer->data_size <
                          static_cast<std::size_t>(width) * height * 4U) {
                    if (decoded_buffer != nullptr) {
                      lv_draw_buf_destroy(decoded_buffer);
                    }
                    failure_reason = "image resource decode failed";
                    succeeded = false;
                  } else {
                    auto pixels = std::make_shared<std::vector<std::uint8_t>>(
                        decoded_buffer->data,
                        decoded_buffer->data +
                            static_cast<std::size_t>(width) * height * 4U);
                    lv_draw_buf_destroy(decoded_buffer);
                    // lodepng_decode32 exposes RGBA bytes through the draw
                    // buffer; LVGL ARGB8888 uses BGRA memory order here.
                    convertRgbaToLvglBgra(*pixels);
                    decoded_resource = decoded_resources_
                        .emplace(std::string(text->view()),
                                 DecodedImageResource{width, height, std::move(pixels)})
                        .first;
                  }
                }
                if (succeeded) {
                  const auto& cached = decoded_resource->second;
                  width = cached.width;
                  height = cached.height;
                  slot.image_source_width = width;
                  slot.image_source_height = height;
                  slot.image_source_pixels = *cached.pixels;
                  slot.image_pixels = slot.image_source_pixels;
                  auto* descriptor = new (std::nothrow) lv_image_dsc_t{};
                  if (descriptor == nullptr) {
                    failure_reason = "image descriptor allocation failed";
                    succeeded = false;
                  } else {
                    descriptor->header.magic = LV_IMAGE_HEADER_MAGIC;
                    descriptor->header.cf = LV_COLOR_FORMAT_ARGB8888;
                    descriptor->header.w = width;
                    descriptor->header.h = height;
                    descriptor->header.stride = width * 4U;
                    descriptor->data_size = static_cast<std::uint32_t>(slot.image_pixels.size());
                    descriptor->data = slot.image_pixels.data();
                    slot.image_descriptor = descriptor;
                    lv_image_set_src(object, descriptor);
                    lv_image_set_inner_align(object, LV_IMAGE_ALIGN_CENTER);
                    if (lv_obj_get_width(object) > 0 &&
                        lv_obj_get_height(object) > 0) {
                      succeeded = resizeImageForLayout(slot, object);
                      if (!succeeded) failure_reason = "image resize failed";
                    }
                  }
                }
              }
            } else if (name == "backgroundColor" || name == "color") {
              const auto* text = std::get_if<BoundedText>(&value.value);
              lv_color_t color{};
              succeeded = text != nullptr && parseHexColor(text->view(), color);
              if (succeeded && name == "backgroundColor") {
                lv_obj_set_style_bg_color(object, color, 0);
                // View objects start transparent so their default chrome does
                // not cover the parent. A declared backgroundColor is an
                // explicit opaque surface style.
                lv_obj_set_style_bg_opa(object, LV_OPA_COVER, 0);
              } else if (succeeded) {
                lv_obj_set_style_text_color(object, color, 0);
              }
            } else if (name == "borderRadius") {
              const auto* radius = std::get_if<std::int32_t>(&value.value);
              succeeded = radius != nullptr && *radius >= 0;
              if (succeeded) lv_obj_set_style_radius(object, *radius, 0);
            } else if (name == "fontSize") {
              const auto* size = std::get_if<std::int32_t>(&value.value);
              succeeded = size != nullptr &&
                          slot.type != HostComponentType::kView;
              if (!succeeded) return;
              if (slot.font_slot.has_value() &&
                  fonts_[*slot.font_slot].live &&
                  fonts_[*slot.font_slot].size == *size) {
                return;
              }
              const auto acquired = acquireFont(*size);
              succeeded = acquired.has_value();
              if (!succeeded) return;
              auto* target = static_cast<lv_obj_t*>(
                  slot.private_label != nullptr ? slot.private_label : object);
              lv_obj_set_style_text_font(
                  target,
                  static_cast<lv_font_t*>(fonts_[*acquired].native_font), 0);
              if (slot.font_slot.has_value()) releaseFont(*slot.font_slot);
              slot.font_slot = acquired;
            } else if (name == "textAlign") {
              const auto* text = std::get_if<BoundedText>(&value.value);
              succeeded = text != nullptr;
              if (succeeded) {
                const auto align = text->view() == "center"
                                       ? LV_TEXT_ALIGN_CENTER
                                       : (text->view() == "right" ? LV_TEXT_ALIGN_RIGHT
                                                                    : LV_TEXT_ALIGN_LEFT);
                lv_obj_set_style_text_align(object, align, 0);
              }
            } else {
              failure_reason = "unsupported host property";
              succeeded = false;
            }
          } else if constexpr (std::is_same_v<Value, SetHostLayout>) {
            const auto slot_index =
                findSlot(transaction.surface_id, value.node_id);
            succeeded = slot_index.has_value();
            if (succeeded) {
              auto* object = static_cast<lv_obj_t*>(objects_[*slot_index].native_object);
              lv_obj_set_pos(object, value.rect.x, value.rect.y);
              lv_obj_set_size(object, value.rect.width, value.rect.height);
              if (objects_[*slot_index].type == HostComponentType::kImage) {
                // Resize the decoded pixels to the final host rectangle so
                // small images do not enter LVGL's transform path.
                auto& image_slot = objects_[*slot_index];
                if (image_slot.image_source_width != 0 &&
                    !resizeImageForLayout(image_slot, object)) {
                  succeeded = false;
                  failure_reason = "image resize failed";
                }
              }
            }
          } else if constexpr (std::is_same_v<Value, InsertHostChild>) {
            const auto child =
                findSlot(transaction.surface_id, value.node_id);
            const auto parent =
                findSlot(transaction.surface_id, value.parent_node_id);
            succeeded = child.has_value() && parent.has_value() &&
                        !objects_[*child].attached;
            if (succeeded) {
              auto* child_object = static_cast<lv_obj_t*>(objects_[*child].native_object);
              auto* parent_object = static_cast<lv_obj_t*>(objects_[*parent].native_object);
              lv_obj_set_parent(child_object, parent_object);
              lv_obj_move_to_index(child_object, static_cast<std::int32_t>(value.index));
              objects_[*child].attached = true;
            }
          } else if constexpr (std::is_same_v<Value, MoveHost>) {
            const auto child =
                findSlot(transaction.surface_id, value.node_id);
            const auto parent =
                findSlot(transaction.surface_id, value.new_parent_node_id);
            succeeded = child.has_value() && parent.has_value() &&
                        child.value() != parent.value();
            if (succeeded) {
              auto* child_object = static_cast<lv_obj_t*>(objects_[*child].native_object);
              auto* parent_object = static_cast<lv_obj_t*>(objects_[*parent].native_object);
              lv_obj_set_parent(child_object, parent_object);
              lv_obj_move_to_index(child_object, static_cast<std::int32_t>(value.index));
            }
          } else if constexpr (std::is_same_v<Value, RemoveHost>) {
            const auto slot_index =
                findSlot(transaction.surface_id, value.node_id);
            succeeded = slot_index.has_value();
            if (succeeded) {
              destroySlot(*slot_index);
            }
          }
        },
        transaction.operations[index]);
    if (!succeeded) {
      destroySurfaceObjects(transaction.surface_id);
      result.error = error(RuntimeErrorCode::kPlatformRejected,
                           failure_reason);
      result.live_objects = 0;
      return result;
    }
  }
  result.status = MountResultStatus::kMounted;
  result.error.reset();
  result.live_objects = liveObjectCountForSurface(transaction.surface_id);
  return result;
}

core::EnqueueResult MountHost::post(MountTransaction&& transaction) noexcept {
  if (!accepting_.load(std::memory_order_acquire) ||
      closed_.load(std::memory_order_acquire)) {
    return core::EnqueueResult::failure(
        error(RuntimeErrorCode::kPlatformRejected, "mount host is closed"));
  }
  foundation::TryCriticalSectionGuard guard(admission_);
  if (!guard.acquired()) {
    return core::EnqueueResult::failure(error(
        RuntimeErrorCode::kPlatformRejected, "mount admission is busy", true));
  }
  std::size_t index = limits_.max_transactions;
  for (std::size_t cursor = 0; cursor < limits_.max_transactions; ++cursor) {
    if (!transactions_[cursor].occupied) {
      index = cursor;
      break;
    }
  }
  if (index == limits_.max_transactions) {
    return core::EnqueueResult::failure(
        error(RuntimeErrorCode::kQueueOverflow, "mount transaction capacity is full"));
  }
  const std::size_t estimated_bytes =
      sizeof(MountTransaction) +
      transaction.operations.capacity() * sizeof(MountOperation) +
      transaction.source_id.size;
  if (estimated_bytes > limits_.max_batch_bytes) {
    return core::EnqueueResult::failure(error(
        RuntimeErrorCode::kOutOfMemory, "mount batch memory budget exceeded"));
  }
  if (pendingCount() >= limits_.max_batch_queue_depth) {
    return core::EnqueueResult::failure(error(
        RuntimeErrorCode::kQueueOverflow, "mount batch queue depth is full", true));
  }
  transactions_[index].occupied = true;
  transactions_[index].transaction.emplace(std::move(transaction));
  const auto posted = owner_tasks_.post(foundation::OwnerTask::make(
      [this, index]() noexcept { executeSlot(index); }));
  if (posted.status != foundation::PostStatus::kAccepted) {
    transactions_[index].transaction.reset();
    transactions_[index].occupied = false;
    return core::EnqueueResult::failure(
        error(RuntimeErrorCode::kQueueOverflow, "owner task queue rejected mount"));
  }
  return core::EnqueueResult::success(core::Accepted{});
}

void MountHost::executeSlot(std::size_t index) noexcept {
  if (index >= limits_.max_transactions || !transactions_[index].occupied ||
      !transactions_[index].transaction.has_value()) {
    return;
  }
  MountResult result = execute(*transactions_[index].transaction);
  results_.complete(std::move(result));
  clearSlot(index);
}

foundation::LocalResult MountHost::service(foundation::OwnerToken caller,
                                           std::size_t budget) noexcept {
  if (!isOwner(caller)) return foundation::LocalResult::failure(foundation::LocalError::kWrongThread);
  const auto pumped = owner_tasks_.pump(caller, budget);
  return pumped.ok() ? foundation::LocalResult::success()
                     : foundation::LocalResult::failure(pumped.error);
}

void MountHost::close() noexcept { accepting_.store(false, std::memory_order_release); }

foundation::LocalResult MountHost::finishClose(
    foundation::OwnerToken caller) noexcept {
  if (!isOwner(caller)) return foundation::LocalResult::failure(foundation::LocalError::kWrongThread);
  if (accepting_.load(std::memory_order_acquire) || pendingCount() != 0) {
    return foundation::LocalResult::failure(foundation::LocalError::kBusy);
  }
  destroyAllObjects();
  destroyFonts();
  decoded_resources_.clear();
  resources_.clear();
  closed_.store(true, std::memory_order_release);
  return foundation::LocalResult::success();
}

foundation::LocalResult MountHost::releaseSurface(
    foundation::OwnerToken caller,
    const core::SurfaceId& surface_id) noexcept {
  if (!isOwner(caller))
    return foundation::LocalResult::failure(foundation::LocalError::kWrongThread);
  for (const auto& slot : transactions_) {
    if (slot.occupied && slot.transaction.has_value() &&
        slot.transaction->surface_id == surface_id) {
      return foundation::LocalResult::failure(foundation::LocalError::kBusy);
    }
  }
  destroySurfaceObjects(surface_id);
  return foundation::LocalResult::success();
}

void MountHost::clearSlot(std::size_t index) noexcept {
  if (index >= limits_.max_transactions) return;
  transactions_[index].result.reset();
  transactions_[index].transaction.reset();
  transactions_[index].occupied = false;
}

void MountHost::destroySlot(std::size_t index) noexcept {
  if (index >= limits_.max_host_objects || !objects_[index].live) return;
  auto* object = static_cast<lv_obj_t*>(objects_[index].native_object);
  std::array<bool, kStorageCapacity * 32> removed{};
  for (std::size_t cursor = 0; cursor < limits_.max_host_objects; ++cursor) {
    if (!objects_[cursor].live) continue;
    auto* candidate = static_cast<lv_obj_t*>(objects_[cursor].native_object);
    for (auto* current = candidate; current != nullptr && lv_obj_is_valid(current);
         current = lv_obj_get_parent(current)) {
      if (current == object) {
        removed[cursor] = true;
        break;
      }
    }
  }
  for (std::size_t cursor = 0; cursor < limits_.max_host_objects; ++cursor) {
    if (removed[cursor]) {
      // Invalidate callbacks before LVGL begins deleting the object tree. LVGL
      // may synchronously dispatch lifecycle callbacks during deletion; those
      // callbacks must observe an inert binding rather than a retired node.
      if (objects_[cursor].surface_id && objects_[cursor].node_id) {
        for (auto& binding : click_bindings_) {
          if (binding.live && binding.surface_id == *objects_[cursor].surface_id &&
              binding.node_id == *objects_[cursor].node_id) {
            binding.live = false;
            binding.callback = nullptr;
            binding.context = nullptr;
          }
        }
        for (auto& binding : input_bindings_) {
          if (binding.live && binding.surface_id == *objects_[cursor].surface_id &&
              binding.node_id == *objects_[cursor].node_id) {
            binding.live = false;
            binding.callback = nullptr;
            binding.context = nullptr;
          }
        }
        for (auto& binding : switch_bindings_) {
          if (binding.live && binding.surface_id == *objects_[cursor].surface_id &&
              binding.node_id == *objects_[cursor].node_id) {
            binding.live = false;
            binding.callback = nullptr;
            binding.context = nullptr;
          }
        }
        for (auto& binding : slider_bindings_) {
          if (binding.live && binding.surface_id == *objects_[cursor].surface_id &&
              binding.node_id == *objects_[cursor].node_id) {
            binding.live = false;
            binding.callback = nullptr;
            binding.context = nullptr;
          }
        }
        for (auto& binding : picker_bindings_) {
          if (binding.live && binding.surface_id == *objects_[cursor].surface_id &&
              binding.node_id == *objects_[cursor].node_id) {
            binding.live = false;
            binding.callback = nullptr;
            binding.context = nullptr;
          }
        }
        for (auto& binding : tabs_bindings_) {
          if (binding.live && binding.surface_id == *objects_[cursor].surface_id &&
              binding.node_id == *objects_[cursor].node_id) {
            binding.live = false;
            binding.callback = nullptr;
            binding.context = nullptr;
          }
        }
        for (auto& binding : scroll_bindings_) {
          if (binding.live && binding.surface_id == *objects_[cursor].surface_id &&
              binding.node_id == *objects_[cursor].node_id) {
            binding.live = false;
            binding.callback = nullptr;
            binding.context = nullptr;
          }
        }
      }
    }
  }
  if (object != nullptr && lv_obj_is_valid(object)) lv_obj_delete(object);
  for (std::size_t cursor = 0; cursor < limits_.max_host_objects; ++cursor) {
    if (removed[cursor]) {
      if (objects_[cursor].image_descriptor != nullptr) {
        delete static_cast<lv_image_dsc_t*>(objects_[cursor].image_descriptor);
      }
      if (objects_[cursor].font_slot.has_value()) {
        releaseFont(*objects_[cursor].font_slot);
      }
      objects_[cursor] = HostSlot{};
    }
  }
}

void MountHost::destroyAllObjects() noexcept {
  for (std::size_t index = limits_.max_host_objects; index != 0; --index) {
    const std::size_t slot = index - 1;
    if (objects_[slot].live) destroySlot(slot);
  }
}

void MountHost::destroySurfaceObjects(
    const core::SurfaceId& surface_id) noexcept {
  for (std::size_t index = limits_.max_host_objects; index != 0; --index) {
    const std::size_t slot = index - 1;
    if (objects_[slot].live && objects_[slot].surface_id.has_value() &&
        *objects_[slot].surface_id == surface_id) {
      destroySlot(slot);
    }
  }
}

}  // namespace quickapp::lvgl::mount
