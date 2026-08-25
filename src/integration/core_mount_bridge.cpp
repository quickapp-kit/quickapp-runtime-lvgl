#include "quickapp/lvgl/integration/core_mount_bridge.h"

#include <cmath>
#include <limits>
#include <string_view>
#include <type_traits>
#include <utility>

namespace quickapp::lvgl::integration {
namespace {

using core::RuntimeError;
using core::RuntimeErrorCode;
using core::render::MountOperation;

RuntimeError error(RuntimeErrorCode code, std::string_view message,
                   bool retryable = false) noexcept {
  return RuntimeError::simple(code, message, retryable);
}

core::RuntimeResult<mount::BoundedText> text(std::string_view value) noexcept {
  mount::BoundedText result = mount::BoundedText::from(value);
  if (result.truncated) {
    return core::RuntimeResult<mount::BoundedText>::failure(error(
        RuntimeErrorCode::kAbiInvalidArgument, "Core mount text exceeds LVGL bound"));
  }
  return core::RuntimeResult<mount::BoundedText>::success(result);
}

core::RuntimeResult<mount::HostProperty> property(
    std::string_view name, const std::variant<bool, double, std::string>& value) noexcept {
  if (const auto* boolean = std::get_if<bool>(&value)) {
    return core::RuntimeResult<mount::HostProperty>::success(*boolean);
  }
  if (const auto* string = std::get_if<std::string>(&value)) {
    auto converted = text(*string);
    if (!converted) {
      return core::RuntimeResult<mount::HostProperty>::failure(converted.error());
    }
    return core::RuntimeResult<mount::HostProperty>::success(
        std::move(converted).value());
  }
  const auto* number = std::get_if<double>(&value);
  if (number == nullptr ||
      (name != "borderRadius" && name != "fontSize" && name != "min" &&
       name != "max" && name != "step" && name != "value" &&
       name != "selected") ||
      !std::isfinite(*number) ||
      *number < 0 || *number > static_cast<double>(std::numeric_limits<std::int32_t>::max()) ||
      std::floor(*number) != *number ||
      (name == "fontSize" &&
       (*number < 1 || *number > 256))) {
    return core::RuntimeResult<mount::HostProperty>::failure(error(
        RuntimeErrorCode::kHostFeatureUnsupported,
        "Core mount property is outside the LVGL host contract"));
  }
  if (name == "min" || name == "max" || name == "step" ||
      name == "value" || name == "selected") {
    return core::RuntimeResult<mount::HostProperty>::success(*number);
  }
  return core::RuntimeResult<mount::HostProperty>::success(
      static_cast<std::int32_t>(*number));
}

}  // namespace

CoreMountBridge::CoreMountBridge(
    foundation::OwnerToken owner,
    core::CoreIngressPort<core::render::MountTransactionResult>& results,
    core::ObservationEmitter* observation,
    bool auto_present_full_mount) noexcept
    : owner_(owner), results_(results), observation_(observation),
      auto_present_full_mount_(auto_present_full_mount) {
  if (!owner_.valid()) accepting_.store(false, std::memory_order_release);
}

CoreMountBridge::~CoreMountBridge() noexcept {
  // Core owns the bridge through MountPort. Its close() must precede teardown.
  // Do not drain or execute work from this destructor.
  assert(closed_.load(std::memory_order_acquire) && pendingCount() == 0 &&
         "CoreMountBridge requires explicit close and result drain");
}

void CoreMountBridge::bind(
    mount::MountHost& mounts, surface::SurfaceHostAdapter& surfaces,
    core::CoreIngressPort<surface::SurfaceResult>* passthrough) noexcept {
  mounts_ = &mounts;
  surfaces_ = &surfaces;
  passthrough_ = passthrough;
}

std::optional<std::size_t> CoreMountBridge::find(
    const core::SurfaceId& surface_id,
    const core::MountAttemptId& mount_attempt_id,
    const core::render::RenderSourceId& source_id) const noexcept {
  for (std::size_t index = 0; index < pending_.size(); ++index) {
    const Pending& value = pending_[index];
    if (value.occupied && value.surface_id == surface_id &&
        value.mount_attempt_id == mount_attempt_id && value.source_id == source_id) {
      return index;
    }
  }
  return std::nullopt;
}

std::optional<std::size_t> CoreMountBridge::free() const noexcept {
  for (std::size_t index = 0; index < pending_.size(); ++index) {
    if (!pending_[index].occupied) return index;
  }
  return std::nullopt;
}

core::RuntimeResult<mount::MountTransaction> CoreMountBridge::convert(
    const core::render::MountTransaction& transaction) const noexcept {
  if (transaction.operations.empty() ||
      transaction.operations.size() > mount::kMaxMountOperations) {
    return core::RuntimeResult<mount::MountTransaction>::failure(error(
        RuntimeErrorCode::kQueueOverflow, "Core Mount transaction exceeds LVGL bound"));
  }
  auto source = text(core::render::render_source_wire(transaction.source_id));
  if (!source) {
    return core::RuntimeResult<mount::MountTransaction>::failure(source.error());
  }
  if (source.value().size == 0) {
    return core::RuntimeResult<mount::MountTransaction>::failure(error(
        RuntimeErrorCode::kAbiInvalidArgument, "Core mount source id is empty"));
  }
  try {
    mount::MountTransaction result(
        transaction.surface_id, transaction.revision, transaction.mount_attempt_id,
        std::move(source).value(),
        transaction.mode == core::render::MountMode::kFull
            ? mount::MountMode::kFull
            : mount::MountMode::kIncremental);
    for (const MountOperation& operation : transaction.operations) {
      if (const auto* create = std::get_if<core::render::CreateHost>(&operation)) {
        result.operations[result.operation_count++] =
            mount::CreateHost{create->node_id, create->type};
      } else if (const auto* prop = std::get_if<core::render::SetHostProp>(&operation)) {
        auto name = text(prop->name);
        if (!name) {
          return core::RuntimeResult<mount::MountTransaction>::failure(name.error());
        }
        auto value = property(prop->name, prop->value);
        if (!value) {
          return core::RuntimeResult<mount::MountTransaction>::failure(value.error());
        }
        result.operations[result.operation_count++] = mount::SetHostProp{
            prop->node_id, std::move(name).value(), std::move(value).value()};
      } else if (const auto* layout = std::get_if<core::render::SetHostLayout>(&operation)) {
        const auto& rect = layout->rect;
        if (rect.x < std::numeric_limits<std::int32_t>::min() ||
            rect.x > std::numeric_limits<std::int32_t>::max() ||
            rect.y < std::numeric_limits<std::int32_t>::min() ||
            rect.y > std::numeric_limits<std::int32_t>::max() ||
            rect.width < 0 || rect.width > std::numeric_limits<std::int32_t>::max() ||
            rect.height < 0 || rect.height > std::numeric_limits<std::int32_t>::max()) {
          return core::RuntimeResult<mount::MountTransaction>::failure(error(
              RuntimeErrorCode::kAbiInvalidArgument, "Core layout is outside LVGL bounds"));
        }
        result.operations[result.operation_count++] = mount::SetHostLayout{
            layout->node_id,
            {static_cast<std::int32_t>(rect.x), static_cast<std::int32_t>(rect.y),
             static_cast<std::int32_t>(rect.width), static_cast<std::int32_t>(rect.height)}};
      } else if (const auto* insert = std::get_if<core::render::InsertHostChild>(&operation)) {
        result.operations[result.operation_count++] = mount::InsertHostChild{
            insert->node_id, insert->parent_node_id, insert->index};
      } else if (const auto* move = std::get_if<core::render::MoveHost>(&operation)) {
        result.operations[result.operation_count++] = mount::MoveHost{
            move->node_id, move->new_parent_node_id, move->index};
      } else if (const auto* remove = std::get_if<core::render::RemoveHost>(&operation)) {
        result.operations[result.operation_count++] = mount::RemoveHost{remove->node_id};
      }
    }
    return core::RuntimeResult<mount::MountTransaction>::success(std::move(result));
  } catch (...) {
    return core::RuntimeResult<mount::MountTransaction>::failure(
        error(RuntimeErrorCode::kOutOfMemory, "Core Mount transaction adaptation failed"));
  }
}

core::EnqueueResult CoreMountBridge::post(
    core::render::MountTransaction&& transaction) noexcept {
  if (!accepting_.load(std::memory_order_acquire) || closed() || mounts_ == nullptr ||
      surfaces_ == nullptr) {
    return core::EnqueueResult::failure(
        error(RuntimeErrorCode::kPlatformRejected, "Core Mount bridge is not accepting"));
  }
  auto converted = convert(transaction);
  if (!converted) return core::EnqueueResult::failure(converted.error());
  foundation::TryCriticalSectionGuard guard(admission_);
  if (!guard.acquired()) {
    return core::EnqueueResult::failure(
        error(RuntimeErrorCode::kPlatformRejected, "Core Mount bridge is busy", true));
  }
  const auto slot = free();
  if (!slot.has_value()) {
    return core::EnqueueResult::failure(
        error(RuntimeErrorCode::kQueueOverflow, "Core Mount bridge capacity is full"));
  }
  Pending& pending = pending_[*slot];
  pending.occupied = true;
  pending.surface_id = transaction.surface_id;
  pending.source_id = transaction.source_id;
  pending.mount_attempt_id = transaction.mount_attempt_id;
  pending.revision = transaction.revision;
  pending.operation_count = transaction.operations.size();
  pending.full_mount = transaction.mode == core::render::MountMode::kFull;
  pending.phase = Phase::kMountPending;
  pending.result_error.reset();
  pending.result_mounted = false;
  auto posted = mounts_->post(std::move(converted).value());
  if (!posted) {
    clear(*slot);
    return posted;
  }
  return core::EnqueueResult::success(core::Accepted{});
}

void CoreMountBridge::complete(mount::MountResult result) noexcept {
  std::optional<std::size_t> slot;
  for (std::size_t index = 0; index < pending_.size(); ++index) {
    if (pending_[index].occupied &&
        pending_[index].surface_id == result.surface_id &&
        pending_[index].mount_attempt_id == result.mount_attempt_id &&
        core::render::render_source_wire(pending_[index].source_id) ==
            result.source_id.view()) {
      slot = index;
      break;
    }
  }
  if (!slot.has_value()) return;
  Pending& pending = pending_[*slot];
  if (result.status == mount::MountResultStatus::kFailed) {
    pending.result_mounted = false;
    pending.result_error = result.error.value_or(
        error(RuntimeErrorCode::kPlatformRejected, "LVGL Mount failed"));
    pending.phase = Phase::kCoreResultPending;
    (void)tryCoreResult(*slot);
    return;
  }
  if (!pending.full_mount) {
    pending.result_mounted = true;
    pending.result_error.reset();
    pending.phase = Phase::kCoreResultPending;
    (void)tryCoreResult(*slot);
    return;
  }
  if (surfaces_->markFullMountCommitted(owner_, result.surface_id).error !=
      foundation::LocalError::kNone) {
    pending.result_mounted = false;
    pending.result_error = error(RuntimeErrorCode::kSurfacePresentationFailed,
                                 "S03 rejected mounted root");
    pending.phase = Phase::kCoreResultPending;
    (void)tryCoreResult(*slot);
    return;
  }
  if (!auto_present_full_mount_) {
    pending.result_mounted = true;
    pending.result_error.reset();
    pending.phase = Phase::kCoreResultPending;
    (void)tryCoreResult(*slot);
    return;
  }
  pending.phase = Phase::kPresentPending;
  (void)tryPresent(*slot);
}

core::EnqueueResult CoreMountBridge::tryPresent(std::size_t index) noexcept {
  Pending& pending = pending_[index];
  if (!pending.occupied || pending.phase != Phase::kPresentPending || surfaces_ == nullptr) {
    return core::EnqueueResult::failure(
        error(RuntimeErrorCode::kAbiInvalidArgument, "Present is not pending"));
  }
  const auto* request_id = std::get_if<core::RequestId>(&pending.source_id);
  if (request_id == nullptr) {
    return core::EnqueueResult::failure(
        error(RuntimeErrorCode::kAbiInvalidArgument,
              "incremental Mount cannot request Surface Present"));
  }
  auto posted = surfaces_->post(surface::PresentRootSurfaceHost{
      *request_id, pending.surface_id});
  if (posted) {
    pending.phase = Phase::kPresentPosted;
    emitPresent(core::MarkerName::kPlatformPresentRequested, pending);
    return posted;
  }
  if (!posted.error().retryable && posted.error().code != RuntimeErrorCode::kQueueOverflow) {
    pending.result_mounted = false;
    pending.result_error = posted.error();
    pending.phase = Phase::kCoreResultPending;
    (void)tryCoreResult(index);
  }
  return posted;
}

core::EnqueueResult CoreMountBridge::tryCoreResult(std::size_t index) noexcept {
  Pending& pending = pending_[index];
  if (!pending.occupied || pending.phase != Phase::kCoreResultPending) {
    return core::EnqueueResult::failure(
        error(RuntimeErrorCode::kAbiInvalidArgument, "Core result is not pending"));
  }
  core::render::MountTransactionResult result{
      pending.surface_id, pending.revision, pending.mount_attempt_id,
      pending.source_id, pending.result_mounted, pending.result_error};
  auto posted = results_.post(std::move(result));
  if (posted) clear(index);
  return posted;
}

core::EnqueueResult CoreMountBridge::acceptSurfaceResult(
    surface::SurfaceResult&& result) noexcept {
  const auto present = std::get_if<surface::PresentRootSurfaceHostResult>(&result);
  if (present == nullptr) {
    return passthrough_ == nullptr
               ? core::EnqueueResult::success(core::Accepted{})
               : passthrough_->post(std::move(result));
  }
  std::optional<std::size_t> slot;
  for (std::size_t index = 0; index < pending_.size(); ++index) {
    if (pending_[index].occupied && pending_[index].phase == Phase::kPresentPosted &&
        pending_[index].surface_id == present->surface_id &&
        std::holds_alternative<core::RequestId>(pending_[index].source_id) &&
        std::get<core::RequestId>(pending_[index].source_id) ==
            present->request_id) {
      slot = index;
      break;
    }
  }
  if (!slot.has_value()) {
    return passthrough_ == nullptr
               ? core::EnqueueResult::success(core::Accepted{})
               : passthrough_->post(std::move(result));
  }
  Pending& pending = pending_[*slot];
  if (present->status == surface::SurfaceResultStatus::kPresented) {
    emitPresent(core::MarkerName::kPlatformPresentCompleted, pending);
    pending.result_mounted = true;
    pending.result_error.reset();
  } else {
    const RuntimeError failure = present->error.value_or(
        error(RuntimeErrorCode::kSurfacePresentationFailed, "S03 Present failed"));
    emitPresent(core::MarkerName::kPlatformPresentFailed, pending, failure.code);
    pending.result_mounted = false;
    pending.result_error = failure;
  }
  pending.phase = Phase::kCoreResultPending;
  (void)tryCoreResult(*slot);
  return core::EnqueueResult::success(core::Accepted{});
}

foundation::LocalResult CoreMountBridge::service(foundation::OwnerToken caller,
                                                 std::size_t budget) noexcept {
  if (caller != owner_ || !caller.valid())
    return foundation::LocalResult::failure(foundation::LocalError::kWrongThread);
  if (closed()) return foundation::LocalResult::failure(foundation::LocalError::kInvalidState);
  std::size_t attempted = 0;
  for (std::size_t index = 0; index < pending_.size() && attempted < budget; ++index) {
    if (!pending_[index].occupied) continue;
    if (pending_[index].phase == Phase::kPresentPending) {
      (void)tryPresent(index);
      ++attempted;
    } else if (pending_[index].phase == Phase::kCoreResultPending) {
      (void)tryCoreResult(index);
      ++attempted;
    }
  }
  return foundation::LocalResult::success();
}

void CoreMountBridge::close() noexcept {
  accepting_.store(false, std::memory_order_release);
  if (mounts_ != nullptr) mounts_->close();
  closed_.store(true, std::memory_order_release);
}

std::size_t CoreMountBridge::pendingCount() const noexcept {
  std::size_t count = 0;
  for (const Pending& pending : pending_) count += pending.occupied ? 1U : 0U;
  return count;
}

void CoreMountBridge::emitPresent(core::MarkerName marker, const Pending& pending,
                                  std::optional<core::RuntimeErrorCode> error_code) noexcept {
  if (observation_ == nullptr) return;
  auto surface_id = core::SurfaceIdView::parse(pending.surface_id.wire());
  auto source_wire = core::render::render_source_wire(pending.source_id);
  auto request_id = core::RequestIdView::parse(source_wire);
  auto attempt_id = core::MountAttemptIdView::parse(pending.mount_attempt_id.wire());
  if (!surface_id || !attempt_id) return;
  core::TraceFields fields;
  fields.surface_id = std::move(surface_id).value();
  if (request_id) {
    fields.request_id = std::move(request_id).value();
  } else {
    auto transaction_id = core::TransactionIdView::parse(source_wire);
    if (!transaction_id) return;
    fields.transaction_id = std::move(transaction_id).value();
  }
  fields.mount_attempt_id = std::move(attempt_id).value();
  auto count = core::WireUInt::from(pending.operation_count);
  if (count) fields.operation_count = std::move(count).value();
  fields.error_code = error_code;
  observation_->emit(marker, fields);
}

void CoreMountBridge::clear(std::size_t index) noexcept {
  if (index < pending_.size()) pending_[index] = Pending{};
}

}  // namespace quickapp::lvgl::integration
