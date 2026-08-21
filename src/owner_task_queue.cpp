#include "quickapp/lvgl/foundation/owner_task_queue.h"

#include <algorithm>
#include <utility>

namespace quickapp::lvgl::foundation {

OwnerTaskQueue::OwnerTaskQueue(OwnerTask* storage, std::size_t capacity,
                               std::size_t max_tasks_per_pump,
                               WakeupPort* wakeup,
                               TryCriticalSection* critical_section) noexcept
    : storage_(storage),
      capacity_(capacity),
      max_tasks_per_pump_(max_tasks_per_pump),
      wakeup_(wakeup),
      critical_section_(critical_section == nullptr
                            ? &local_critical_section_
                            : critical_section) {}

LocalResult OwnerTaskQueue::bindOwner(OwnerToken owner) noexcept {
  TryCriticalSectionGuard guard(*critical_section_);
  if (!guard.acquired()) {
    return LocalResult::failure(LocalError::kBusy);
  }
  if (!owner.valid() || storage_ == nullptr || capacity_ == 0 ||
      max_tasks_per_pump_ == 0) {
    return LocalResult::failure(LocalError::kInvalidArgument);
  }
  if (state_.load(std::memory_order_relaxed) !=
      LifecycleState::kConstructed) {
    return LocalResult::failure(LocalError::kInvalidState);
  }
  owner_ = owner;
  state_.store(LifecycleState::kRunning, std::memory_order_release);
  return LocalResult::success();
}

PostOutcome OwnerTaskQueue::post(OwnerTask&& task) noexcept {
  bool should_wake = false;
  {
    TryCriticalSectionGuard guard(*critical_section_);
    if (!guard.acquired()) {
      return {PostStatus::kBusy, WakeResult::kNotified};
    }
    if (!task.valid()) {
      return {PostStatus::kInvalid, WakeResult::kNotified};
    }
    const LifecycleState current_state =
        state_.load(std::memory_order_relaxed);
    if (current_state == LifecycleState::kStopping ||
        current_state == LifecycleState::kClosed) {
      return {PostStatus::kStopping, WakeResult::kStopping};
    }
    if (current_state != LifecycleState::kRunning || storage_ == nullptr) {
      return {PostStatus::kInvalid, WakeResult::kFailed};
    }
    if (size_ == capacity_) {
      return {PostStatus::kFull, WakeResult::kNotified};
    }

    should_wake = size_ == 0;
    const std::size_t tail = (head_ + size_) % capacity_;
    storage_[tail] = std::move(task);
    ++size_;
    peak_depth_ = std::max(peak_depth_, size_);
    depth_snapshot_.store(size_, std::memory_order_release);
    peak_depth_snapshot_.store(peak_depth_, std::memory_order_release);
  }

  WakeResult wake_result = WakeResult::kNotified;
  if (should_wake && wakeup_ != nullptr) {
    wake_result = wakeup_->notify();
  }
  return {PostStatus::kAccepted, wake_result};
}

PumpResult OwnerTaskQueue::pump(OwnerToken caller,
                                std::size_t requested_max) noexcept {
  if (!isOwner(caller)) {
    return {LocalError::kWrongThread, 0, depth()};
  }
  const LifecycleState current_state = state();
  if (current_state != LifecycleState::kRunning &&
      !(current_state == LifecycleState::kStopping &&
        stop_policy_ == StopPolicy::kDrain)) {
    return {LocalError::kInvalidState, 0, depth()};
  }

  const std::size_t budget =
      requested_max == 0
          ? max_tasks_per_pump_
          : std::min(requested_max, max_tasks_per_pump_);
  std::size_t executed = 0;
  OwnerTask task;
  for (; executed < budget; ++executed) {
    const PopResult pop_result = tryPop(task);
    if (pop_result.error != LocalError::kNone) {
      return {pop_result.error, executed, depth()};
    }
    if (!pop_result.has_task) {
      break;
    }
    task.run();
  }
  return {LocalError::kNone, executed, depth()};
}

LocalResult OwnerTaskQueue::beginStop(OwnerToken caller,
                                      StopPolicy policy) noexcept {
  if (!isOwner(caller)) {
    return LocalResult::failure(LocalError::kWrongThread);
  }
  {
    TryCriticalSectionGuard guard(*critical_section_);
    if (!guard.acquired()) {
      return LocalResult::failure(LocalError::kBusy);
    }
    const LifecycleState current_state =
        state_.load(std::memory_order_relaxed);
    if (current_state == LifecycleState::kClosed) {
      return LocalResult::success();
    }
    if (current_state == LifecycleState::kStopping &&
        stop_policy_ != policy) {
      return LocalResult::failure(LocalError::kInvalidState);
    }
    if (current_state != LifecycleState::kRunning &&
        current_state != LifecycleState::kStopping) {
      return LocalResult::failure(LocalError::kInvalidState);
    }
    if (current_state == LifecycleState::kRunning) {
      state_.store(LifecycleState::kStopping, std::memory_order_release);
      stop_policy_ = policy;
    }
  }

  if (wakeup_ != nullptr) {
    wakeup_->requestStop();
  }
  if (policy == StopPolicy::kCancel) {
    return cancelPending(caller);
  }
  return LocalResult::success();
}

LocalResult OwnerTaskQueue::finishStop(OwnerToken caller) noexcept {
  if (!isOwner(caller)) {
    return LocalResult::failure(LocalError::kWrongThread);
  }
  TryCriticalSectionGuard guard(*critical_section_);
  if (!guard.acquired()) {
    return LocalResult::failure(LocalError::kBusy);
  }
  const LifecycleState current_state =
      state_.load(std::memory_order_relaxed);
  if (current_state == LifecycleState::kClosed) {
    return LocalResult::success();
  }
  if (current_state != LifecycleState::kStopping || size_ != 0) {
    return LocalResult::failure(LocalError::kInvalidState);
  }
  state_.store(LifecycleState::kClosed, std::memory_order_release);
  return LocalResult::success();
}

std::size_t OwnerTaskQueue::depth() const noexcept {
  return depth_snapshot_.load(std::memory_order_acquire);
}

std::size_t OwnerTaskQueue::peakDepth() const noexcept {
  return peak_depth_snapshot_.load(std::memory_order_acquire);
}

LifecycleState OwnerTaskQueue::state() const noexcept {
  return state_.load(std::memory_order_acquire);
}

bool OwnerTaskQueue::destructionInvariantHolds() const noexcept {
  return state() == LifecycleState::kClosed && depth() == 0;
}

bool OwnerTaskQueue::isOwner(OwnerToken caller) const noexcept {
  return owner_.valid() && caller == owner_;
}

OwnerTaskQueue::PopResult OwnerTaskQueue::tryPop(
    OwnerTask& output) noexcept {
  TryCriticalSectionGuard guard(*critical_section_);
  if (!guard.acquired()) {
    return {LocalError::kBusy, false};
  }
  if (size_ == 0) {
    return {LocalError::kNone, false};
  }
  output = std::move(storage_[head_]);
  head_ = (head_ + 1) % capacity_;
  --size_;
  depth_snapshot_.store(size_, std::memory_order_release);
  return {LocalError::kNone, true};
}

LocalResult OwnerTaskQueue::cancelPending(OwnerToken caller) noexcept {
  if (!isOwner(caller)) {
    return LocalResult::failure(LocalError::kWrongThread);
  }
  OwnerTask task;
  for (std::size_t attempt = 0; attempt < capacity_; ++attempt) {
    const PopResult pop_result = tryPop(task);
    if (pop_result.error != LocalError::kNone) {
      return LocalResult::failure(pop_result.error);
    }
    if (!pop_result.has_task) {
      return LocalResult::success();
    }
    task.reset();
  }
  return depth() == 0 ? LocalResult::success()
                      : LocalResult::failure(LocalError::kBusy);
}

}  // namespace quickapp::lvgl::foundation
