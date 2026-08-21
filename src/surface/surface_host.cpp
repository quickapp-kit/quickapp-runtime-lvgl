#include "quickapp/lvgl/surface/surface_host.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <type_traits>
#include <utility>

namespace quickapp::lvgl::surface {
namespace {

using core::RuntimeError;
using core::RuntimeErrorCode;

RuntimeError error(RuntimeErrorCode code, const char* message,
                   bool retryable = false) noexcept {
  return RuntimeError::simple(code, message, retryable);
}

const core::RequestId& commandRequestId(const SurfaceCommand& command) noexcept {
  return std::visit(
      [](const auto& value) -> const core::RequestId& {
        return value.request_id;
      },
      command);
}

template <typename Result>
Result failure(const core::RequestId& request_id,
               const core::SurfaceId& surface_id, RuntimeError runtime_error) {
  return Result{request_id, surface_id, SurfaceResultStatus::kFailed,
                std::move(runtime_error)};
}

bool commandTouches(const SurfaceCommand& command,
                    const core::SurfaceId& surface_id) noexcept {
  return std::visit(
      [&surface_id](const auto& value) noexcept {
        if (value.surface_id == surface_id) {
          return true;
        }
        using Value = std::decay_t<decltype(value)>;
        if constexpr (std::is_same_v<Value, PresentPushSurfaceHost>) {
          return value.source_surface_id == surface_id;
        } else if constexpr (std::is_same_v<Value, CloseSurfaceHost>) {
          return value.reveal_surface_id == surface_id;
        }
        return false;
      },
      command);
}

}  // namespace

SurfaceHostLimits simulatorSurfaceHostLimits() noexcept { return {16, 16}; }

SurfaceHostLimits embeddedSurfaceHostLimits() noexcept { return {4, 4}; }

SurfaceHostAdapter::SurfaceHostAdapter(
    foundation::OwnerTaskQueue& owner_tasks, foundation::OwnerToken owner,
    PageRootBackend& roots, SurfaceContentLifecyclePort& content,
    core::CoreIngressPort<SurfaceResult>& results,
    SurfaceHostLimits limits) noexcept
    : owner_tasks_(owner_tasks),
      owner_(owner),
      roots_(roots),
      content_(content),
      results_(results),
      limits_(limits) {
  if (!owner_.valid() || limits_.max_live_roots > kStorageCapacity ||
      limits_.max_operations > kStorageCapacity ||
      limits_.max_live_roots == 0 || limits_.max_operations == 0) {
    accepting_.store(false, std::memory_order_release);
  }
}

bool SurfaceHostAdapter::isOwner(foundation::OwnerToken caller) const noexcept {
  return caller == owner_ && caller.valid();
}

std::optional<std::size_t> SurfaceHostAdapter::findRecord(
    const core::SurfaceId& surface_id) const noexcept {
  for (std::size_t index = 0; index < limits_.max_live_roots; ++index) {
    if (records_[index].has_value() &&
        records_[index]->surface_id == surface_id) {
      return index;
    }
  }
  return std::nullopt;
}

std::optional<std::size_t> SurfaceHostAdapter::findFreeRecord() const noexcept {
  for (std::size_t index = 0; index < limits_.max_live_roots; ++index) {
    if (!records_[index].has_value()) {
      return index;
    }
  }
  return std::nullopt;
}

bool SurfaceHostAdapter::operationConflicts(const SurfaceCommand& left,
                                            const SurfaceCommand& right) const
    noexcept {
  return std::visit(
      [&right](const auto& value) noexcept {
        if (commandTouches(right, value.surface_id)) {
          return true;
        }
        using Value = std::decay_t<decltype(value)>;
        if constexpr (std::is_same_v<Value, PresentPushSurfaceHost>) {
          return commandTouches(right, value.source_surface_id);
        } else if constexpr (std::is_same_v<Value, CloseSurfaceHost>) {
          return commandTouches(right, value.reveal_surface_id);
        }
        return false;
      },
      left);
}

core::EnqueueResult SurfaceHostAdapter::post(SurfaceCommand&& command) noexcept {
  if (!accepting_.load(std::memory_order_acquire) ||
      closed_.load(std::memory_order_acquire)) {
    return core::EnqueueResult::failure(
        error(RuntimeErrorCode::kPlatformRejected, "surface host is closed"));
  }

  foundation::TryCriticalSectionGuard guard(admission_);
  if (!guard.acquired()) {
    return core::EnqueueResult::failure(error(
        RuntimeErrorCode::kPlatformRejected, "surface admission is busy", true));
  }
  if (!accepting_.load(std::memory_order_relaxed) ||
      closed_.load(std::memory_order_relaxed)) {
    return core::EnqueueResult::failure(
        error(RuntimeErrorCode::kPlatformRejected, "surface host is closed"));
  }

  std::size_t operation_index = limits_.max_operations;
  for (std::size_t index = 0; index < limits_.max_operations; ++index) {
    const auto state = operations_[index].state.load(std::memory_order_acquire);
    if (state == OperationState::kFree &&
        operation_index == limits_.max_operations) {
      operation_index = index;
    }
    if (operations_[index].command.has_value() &&
        operationConflicts(command, *operations_[index].command)) {
      return core::EnqueueResult::failure(error(
          RuntimeErrorCode::kPlatformRejected,
          "surface already has an accepted control command", true));
    }
  }
  const auto& request_id = commandRequestId(command);
  for (const auto& operation : operations_) {
    if (operation.command.has_value() &&
        commandRequestId(*operation.command) == request_id) {
      return core::EnqueueResult::failure(error(
          RuntimeErrorCode::kPlatformRejected,
          "surface request id is already accepted", true));
    }
  }
  for (const auto& completed : completed_requests_) {
    if (completed.has_value() && *completed == request_id) {
      return core::EnqueueResult::failure(error(
          RuntimeErrorCode::kPlatformRejected,
          "surface request id was already completed", false));
    }
  }
  if (operation_index == limits_.max_operations) {
    overflow_count_.fetch_add(1, std::memory_order_relaxed);
    return core::EnqueueResult::failure(
        error(RuntimeErrorCode::kQueueOverflow, "surface operation capacity is full"));
  }

  auto& operation = operations_[operation_index];
  operation.command.emplace(std::move(command));
  operation.result.reset();
  operation.state.store(OperationState::kQueued, std::memory_order_release);

  const foundation::PostOutcome posted = owner_tasks_.post(
      foundation::OwnerTask::make([this, operation_index]() noexcept {
        executeOperation(operation_index);
      }));
  if (posted.status != foundation::PostStatus::kAccepted) {
    operation.command.reset();
    operation.state.store(OperationState::kFree, std::memory_order_release);
    if (posted.status == foundation::PostStatus::kFull) {
      overflow_count_.fetch_add(1, std::memory_order_relaxed);
      return core::EnqueueResult::failure(
          error(RuntimeErrorCode::kQueueOverflow, "owner task queue is full"));
    }
    return core::EnqueueResult::failure(error(
        RuntimeErrorCode::kPlatformRejected, "owner task admission failed", true));
  }
  return core::EnqueueResult::success(core::Accepted{});
}

void SurfaceHostAdapter::close() noexcept {
  accepting_.store(false, std::memory_order_release);
}

SurfaceServiceResult SurfaceHostAdapter::service(foundation::OwnerToken caller,
                                                 std::size_t budget) noexcept {
  if (!isOwner(caller)) {
    return {foundation::LocalError::kWrongThread, 0, 0};
  }
  if (closed_.load(std::memory_order_acquire)) {
    return {foundation::LocalError::kInvalidState, 0, 0};
  }
  std::size_t delivered = 0;
  std::size_t released = 0;
  for (std::size_t index = 0;
       index < limits_.max_operations && delivered + released < budget;
       ++index) {
    const auto state = operations_[index].state.load(std::memory_order_acquire);
    if (state == OperationState::kResultPending) {
      const std::size_t before = pendingResultCount();
      deliverResult(index);
      if (pendingResultCount() < before) {
        ++delivered;
      }
    }
    if (operations_[index].state.load(std::memory_order_acquire) ==
        OperationState::kDelivered) {
      foundation::TryCriticalSectionGuard guard(admission_);
      if (guard.acquired()) {
        clearOperation(index);
        ++released;
      }
    }
  }
  return {foundation::LocalError::kNone, delivered, released};
}

foundation::LocalResult SurfaceHostAdapter::markFullMountCommitted(
    foundation::OwnerToken caller, const core::SurfaceId& surface_id) noexcept {
  if (!isOwner(caller)) {
    return foundation::LocalResult::failure(foundation::LocalError::kWrongThread);
  }
  const auto index = findRecord(surface_id);
  if (!index.has_value() || records_[*index]->phase != LocalSurfacePhase::kHiddenEmpty) {
    return foundation::LocalResult::failure(foundation::LocalError::kInvalidState);
  }
  records_[*index]->phase = LocalSurfacePhase::kHiddenMounted;
  return foundation::LocalResult::success();
}

foundation::LocalResult SurfaceHostAdapter::withPageRootForMount(
    foundation::OwnerToken caller, const core::SurfaceId& surface_id,
    void* context, RootCallback callback) noexcept {
  if (!isOwner(caller) || callback == nullptr) {
    return foundation::LocalResult::failure(foundation::LocalError::kWrongThread);
  }
  const auto index = findRecord(surface_id);
  if (!index.has_value() ||
      records_[*index]->phase == LocalSurfacePhase::kDestroying) {
    return foundation::LocalResult::failure(foundation::LocalError::kInvalidState);
  }
  callback(context, records_[*index]->root);
  return foundation::LocalResult::success();
}

foundation::LocalResult SurfaceHostAdapter::finishClose(
    foundation::OwnerToken caller) noexcept {
  if (!isOwner(caller)) {
    return foundation::LocalResult::failure(foundation::LocalError::kWrongThread);
  }
  if (accepting_.load(std::memory_order_acquire)) {
    return foundation::LocalResult::failure(foundation::LocalError::kInvalidState);
  }
  (void)service(caller, kStorageCapacity);
  if (pendingOperationCount() != 0) {
    return foundation::LocalResult::failure(foundation::LocalError::kBusy);
  }
  for (std::size_t index = 0; index < limits_.max_live_roots; ++index) {
    if (!records_[index].has_value()) {
      continue;
    }
    const auto release = content_.canRelease(records_[index]->surface_id);
    if (release.ok()) {
      content_.releaseNoFail(records_[index]->surface_id);
    } else {
      content_.resetNoFail(records_[index]->surface_id);
    }
    roots_.resetNoFail(records_[index]->root);
    records_[index].reset();
    reset_count_.fetch_add(1, std::memory_order_relaxed);
  }
  closed_.store(true, std::memory_order_release);
  return foundation::LocalResult::success();
}

std::size_t SurfaceHostAdapter::liveRootCount() const noexcept {
  std::size_t count = 0;
  for (std::size_t index = 0; index < limits_.max_live_roots; ++index) {
    count += records_[index].has_value() ? 1U : 0U;
  }
  return count;
}

std::size_t SurfaceHostAdapter::pendingOperationCount() const noexcept {
  std::size_t count = 0;
  for (std::size_t index = 0; index < limits_.max_operations; ++index) {
    count += operations_[index].state.load(std::memory_order_acquire) ==
                     OperationState::kFree
                 ? 0U
                 : 1U;
  }
  return count;
}

std::size_t SurfaceHostAdapter::pendingResultCount() const noexcept {
  std::size_t count = 0;
  for (std::size_t index = 0; index < limits_.max_operations; ++index) {
    count += operations_[index].state.load(std::memory_order_acquire) ==
                     OperationState::kResultPending
                 ? 1U
                 : 0U;
  }
  return count;
}

void SurfaceHostAdapter::executeOperation(std::size_t index) noexcept {
  if (index >= limits_.max_operations ||
      operations_[index].state.load(std::memory_order_acquire) !=
          OperationState::kQueued) {
    return;
  }
  operations_[index].state.store(OperationState::kExecuting,
                                 std::memory_order_release);
  operations_[index].result.emplace(execute(*operations_[index].command));
  operations_[index].state.store(OperationState::kResultPending,
                                 std::memory_order_release);
  deliverResult(index);
}

void SurfaceHostAdapter::deliverResult(std::size_t index) noexcept {
  if (index >= limits_.max_operations ||
      operations_[index].state.load(std::memory_order_acquire) !=
          OperationState::kResultPending ||
      !operations_[index].result.has_value()) {
    return;
  }
  const core::EnqueueResult posted = results_.post(std::move(*operations_[index].result));
  if (posted) {
    operations_[index].result.reset();
    operations_[index].state.store(OperationState::kDelivered,
                                   std::memory_order_release);
    return;
  }
  if (posted.error().code != RuntimeErrorCode::kQueueOverflow) {
    operations_[index].result.reset();
    operations_[index].state.store(OperationState::kDelivered,
                                   std::memory_order_release);
  }
}

SurfaceResult SurfaceHostAdapter::execute(const SurfaceCommand& command) noexcept {
  return std::visit(
      [this](const auto& value) -> SurfaceResult {
        using Value = std::decay_t<decltype(value)>;
        if constexpr (std::is_same_v<Value, CreateSurfaceHost>) {
          return executeCreate(value);
        } else if constexpr (std::is_same_v<Value, PresentRootSurfaceHost>) {
          return executePresentRoot(value);
        } else if constexpr (std::is_same_v<Value, PresentPushSurfaceHost>) {
          return executePresentPush(value);
        } else if constexpr (std::is_same_v<Value, SetSurfaceVisibility>) {
          return executeVisibility(value);
        } else if constexpr (std::is_same_v<Value, CloseSurfaceHost>) {
          return executeClose(value);
        } else {
          return executeDestroy(value);
        }
      },
      command);
}

SurfaceResult SurfaceHostAdapter::executeCreate(
    const CreateSurfaceHost& command) noexcept {
  if (!std::isfinite(command.viewport.width) ||
      !std::isfinite(command.viewport.height) || command.viewport.width <= 0 ||
      command.viewport.height <= 0) {
    return failure<CreateSurfaceHostResult>(
        command.request_id, command.surface_id,
        error(RuntimeErrorCode::kAbiInvalidArgument, "invalid surface viewport"));
  }
  if (findRecord(command.surface_id).has_value()) {
    return failure<CreateSurfaceHostResult>(
        command.request_id, command.surface_id,
        error(RuntimeErrorCode::kSurfaceHostAlreadyExists, "surface host exists"));
  }
  const auto record_index = findFreeRecord();
  if (!record_index.has_value()) {
    return failure<CreateSurfaceHostResult>(
        command.request_id, command.surface_id,
        error(RuntimeErrorCode::kOutOfMemory, "surface host capacity is full"));
  }
  const PageRootCreateResult created = roots_.createHidden(command.viewport);
  if (!created.ok()) {
    return failure<CreateSurfaceHostResult>(
        command.request_id, command.surface_id,
        error(created.error == foundation::LocalError::kCapacityExhausted
                  ? RuntimeErrorCode::kOutOfMemory
                  : RuntimeErrorCode::kPlatformRejected,
              "page root creation failed"));
  }
  records_[*record_index].emplace(SurfaceRecord{
      command.surface_id, command.viewport, created.handle,
      LocalSurfacePhase::kHiddenEmpty});
  return CreateSurfaceHostResult{command.request_id, command.surface_id,
                                SurfaceResultStatus::kCreated, std::nullopt};
}

SurfaceResult SurfaceHostAdapter::executePresentRoot(
    const PresentRootSurfaceHost& command) noexcept {
  const auto index = findRecord(command.surface_id);
  if (!index.has_value() ||
      records_[*index]->phase != LocalSurfacePhase::kHiddenMounted ||
      !roots_.valid(records_[*index]->root)) {
    return failure<PresentRootSurfaceHostResult>(
        command.request_id, command.surface_id,
        error(RuntimeErrorCode::kSurfacePresentationFailed,
              "root is not ready for presentation"));
  }
  roots_.setHiddenNoFail(records_[*index]->root, false);
  records_[*index]->phase = LocalSurfacePhase::kVisible;
  return PresentRootSurfaceHostResult{command.request_id, command.surface_id,
                                     SurfaceResultStatus::kPresented,
                                     std::nullopt};
}

SurfaceResult SurfaceHostAdapter::executePresentPush(
    const PresentPushSurfaceHost& command) noexcept {
  const auto target = findRecord(command.surface_id);
  const auto source = findRecord(command.source_surface_id);
  if (!target.has_value() || !source.has_value() || *target == *source ||
      records_[*target]->phase != LocalSurfacePhase::kHiddenMounted ||
      records_[*source]->phase != LocalSurfacePhase::kVisible ||
      !roots_.valid(records_[*target]->root) ||
      !roots_.valid(records_[*source]->root)) {
    return PresentPushSurfaceHostResult{
        command.request_id, command.surface_id, command.source_surface_id,
        SurfaceResultStatus::kFailed,
        error(RuntimeErrorCode::kSurfacePresentationFailed,
              "push surfaces are not ready")};
  }
  roots_.setHiddenNoFail(records_[*target]->root, false);
  roots_.setHiddenNoFail(records_[*source]->root, true);
  records_[*target]->phase = LocalSurfacePhase::kVisible;
  records_[*source]->phase = LocalSurfacePhase::kHidden;
  return PresentPushSurfaceHostResult{
      command.request_id, command.surface_id, command.source_surface_id,
      SurfaceResultStatus::kPresented, std::nullopt};
}

SurfaceResult SurfaceHostAdapter::executeVisibility(
    const SetSurfaceVisibility& command) noexcept {
  const auto index = findRecord(command.surface_id);
  if (!index.has_value()) {
    return SetSurfaceVisibilityResult{
        command.request_id, command.surface_id, command.visibility,
        SurfaceResultStatus::kFailed,
        error(RuntimeErrorCode::kSurfaceHostNotFound, "surface host not found")};
  }
  auto& record = *records_[*index];
  const bool currently_visible = record.phase == LocalSurfacePhase::kVisible;
  const bool requested_visible = command.visibility == SurfaceVisibility::kVisible;
  if (record.phase != LocalSurfacePhase::kVisible &&
      record.phase != LocalSurfacePhase::kHidden) {
    return SetSurfaceVisibilityResult{
        command.request_id, command.surface_id, command.visibility,
        SurfaceResultStatus::kFailed,
        error(RuntimeErrorCode::kPlatformRejected,
              "surface is not visibility-controllable")};
  }
  if (currently_visible != requested_visible) {
    roots_.setHiddenNoFail(record.root, requested_visible == false);
    record.phase = requested_visible ? LocalSurfacePhase::kVisible
                                     : LocalSurfacePhase::kHidden;
  }
  return SetSurfaceVisibilityResult{command.request_id, command.surface_id,
                                    command.visibility,
                                    SurfaceResultStatus::kCompleted,
                                    std::nullopt};
}

SurfaceResult SurfaceHostAdapter::executeClose(
    const CloseSurfaceHost& command) noexcept {
  const auto closing = findRecord(command.surface_id);
  const auto reveal = findRecord(command.reveal_surface_id);
  if (!closing.has_value() || !reveal.has_value() || *closing == *reveal ||
      records_[*closing]->phase != LocalSurfacePhase::kVisible ||
      records_[*reveal]->phase != LocalSurfacePhase::kHidden ||
      !roots_.valid(records_[*closing]->root) ||
      !roots_.valid(records_[*reveal]->root)) {
    return CloseSurfaceHostResult{
        command.request_id, command.surface_id, command.reveal_surface_id,
        SurfaceResultStatus::kFailed,
        error(RuntimeErrorCode::kPlatformRejected, "close surfaces are not ready")};
  }
  if (!content_.canRelease(records_[*closing]->surface_id).ok()) {
    return CloseSurfaceHostResult{
        command.request_id, command.surface_id, command.reveal_surface_id,
        SurfaceResultStatus::kFailed,
        error(RuntimeErrorCode::kPlatformRejected, "surface content is busy")};
  }
  records_[*closing]->phase = LocalSurfacePhase::kDestroying;
  content_.releaseNoFail(records_[*closing]->surface_id);
  roots_.destroyNoFail(records_[*closing]->root);
  records_[*closing].reset();
  roots_.setHiddenNoFail(records_[*reveal]->root, false);
  records_[*reveal]->phase = LocalSurfacePhase::kVisible;
  return CloseSurfaceHostResult{command.request_id, command.surface_id,
                                command.reveal_surface_id,
                                SurfaceResultStatus::kCompleted, std::nullopt};
}

SurfaceResult SurfaceHostAdapter::executeDestroy(
    const DestroySurfaceHost& command) noexcept {
  const auto index = findRecord(command.surface_id);
  if (!index.has_value()) {
    return DestroySurfaceHostResult{
        command.request_id, command.surface_id, SurfaceResultStatus::kFailed,
        error(RuntimeErrorCode::kSurfaceHostNotFound, "surface host not found")};
  }
  auto& record = *records_[*index];
  record.phase = LocalSurfacePhase::kDestroying;
  if (!content_.canRelease(record.surface_id).ok()) {
    content_.resetNoFail(record.surface_id);
    roots_.resetNoFail(record.root);
    records_[*index].reset();
    reset_count_.fetch_add(1, std::memory_order_relaxed);
    return DestroySurfaceHostResult{
        command.request_id, command.surface_id, SurfaceResultStatus::kFailed,
        error(RuntimeErrorCode::kPlatformRejected,
              "destroy required a container reset")};
  }
  content_.releaseNoFail(record.surface_id);
  roots_.destroyNoFail(record.root);
  records_[*index].reset();
  return DestroySurfaceHostResult{command.request_id, command.surface_id,
                                  SurfaceResultStatus::kDestroyed, std::nullopt};
}

void SurfaceHostAdapter::clearOperation(std::size_t index) noexcept {
  if (operations_[index].command.has_value()) {
    completed_requests_[completed_request_cursor_] =
        commandRequestId(*operations_[index].command);
    completed_request_cursor_ =
        (completed_request_cursor_ + 1) % completed_requests_.size();
  }
  operations_[index].command.reset();
  operations_[index].result.reset();
  operations_[index].state.store(OperationState::kFree,
                                 std::memory_order_release);
}

void SurfaceHostAdapter::resetRecord(std::size_t index) noexcept {
  if (!records_[index].has_value()) {
    return;
  }
  content_.resetNoFail(records_[index]->surface_id);
  roots_.resetNoFail(records_[index]->root);
  records_[index].reset();
  reset_count_.fetch_add(1, std::memory_order_relaxed);
}

}  // namespace quickapp::lvgl::surface
