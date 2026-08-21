#include "quickapp/lvgl/mount/mount_host.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <string_view>
#include <type_traits>

#include <lvgl.h>

#include "quickapp/lvgl/font/system_default_font_asset.h"

namespace quickapp::lvgl::mount {
namespace {

constexpr std::int32_t kDefaultFontSize = 16;

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

MountHostLimits simulatorMountHostLimits() noexcept { return {16, 512, 64, 16}; }

MountHostLimits embeddedMountHostLimits() noexcept { return {4, 64, 16, 4}; }

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
  if (!owner_.valid() || limits_.max_transactions == 0 ||
      limits_.max_transactions > kStorageCapacity ||
      limits_.max_host_objects == 0 || limits_.max_host_objects > objects_.size() ||
      limits_.max_operations == 0 || limits_.max_operations > kMaxMountOperations ||
      limits_.max_font_instances == 0 ||
      limits_.max_font_instances > fonts_.size()) {
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
    if (fonts_[index].live && fonts_[index].size == size) {
      ++fonts_[index].references;
      return index;
    }
  }
  std::size_t free = limits_.max_font_instances;
  for (std::size_t index = 0; index < limits_.max_font_instances; ++index) {
    if (!fonts_[index].live) {
      free = index;
      break;
    }
  }
  if (free == limits_.max_font_instances) return std::nullopt;
  const auto bytes = font::systemDefaultFontBytes();
  lv_font_t* native = lv_tiny_ttf_create_data_ex(
      bytes.data(), bytes.size(), size, LV_FONT_KERNING_NONE, 8);
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
  if (fonts_[index].references != 0) return;
  auto* native = static_cast<lv_font_t*>(fonts_[index].native_font);
  if (native != nullptr) lv_tiny_ttf_destroy(native);
  fonts_[index] = FontSlot{};
}

bool MountHost::preflight(const MountTransaction& transaction,
                          surface::PageRootHandle*) noexcept {
  if (transaction.operation_count == 0 || transaction.source_id.truncated ||
      transaction.operation_count > limits_.max_operations ||
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
            valid = createdBefore(value.node_id, index) &&
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
                     liveObjectCountForSurface(transaction.surface_id)};
  void* root = nullptr;
  if (!resolveRoot(transaction.surface_id, root).ok() ||
      !preflight(transaction, nullptr)) {
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
              succeeded = false;
              return;
            }
            lv_obj_t* parent = static_cast<lv_obj_t*>(root);
            lv_obj_t* object = nullptr;
            if (value.type == HostComponentType::kText) {
              object = lv_label_create(parent);
            } else if (value.type == HostComponentType::kButton) {
              object = lv_button_create(parent);
            } else {
              object = lv_obj_create(parent);
            }
            if (object == nullptr) {
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
            if (value.type == HostComponentType::kText) {
              lv_label_set_text(object, "");
            } else if (value.type == HostComponentType::kButton) {
              slot.private_label = lv_label_create(object);
              if (slot.private_label == nullptr) succeeded = false;
            }
            if (succeeded && value.type != HostComponentType::kView) {
              const auto default_font = acquireFont(kDefaultFontSize);
              succeeded = default_font.has_value();
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
              if (succeeded) lv_label_set_text(target, text->view().data());
            } else if (name == "enabled") {
              const auto* enabled = std::get_if<bool>(&value.value);
              succeeded = enabled != nullptr && slot.type == HostComponentType::kButton;
              if (succeeded) lv_obj_set_state(object, LV_STATE_DISABLED, !*enabled);
            } else if (name == "backgroundColor" || name == "color") {
              const auto* text = std::get_if<BoundedText>(&value.value);
              lv_color_t color{};
              succeeded = text != nullptr && parseHexColor(text->view(), color);
              if (succeeded && name == "backgroundColor") {
                lv_obj_set_style_bg_color(object, color, 0);
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
                           "mount commit failed");
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
  if (object != nullptr && lv_obj_is_valid(object)) lv_obj_delete(object);
  for (std::size_t cursor = 0; cursor < limits_.max_host_objects; ++cursor) {
    if (removed[cursor]) {
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
