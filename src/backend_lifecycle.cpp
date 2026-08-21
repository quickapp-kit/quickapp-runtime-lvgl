#include "quickapp/lvgl/foundation/backend_lifecycle.h"

namespace quickapp::lvgl::foundation {

BackendLifecycleCoordinator::BackendLifecycleCoordinator(
    OwnerTaskQueue& tasks, WakeupPort& wakeup, DisplayBackend& display,
    InputBackend& input) noexcept
    : tasks_(tasks), wakeup_(wakeup), display_(display), input_(input) {}

LocalResult BackendLifecycleCoordinator::open(
    OwnerToken owner, const DisplayConfig& display_config,
    const InputConfig& input_config) noexcept {
  if (state_ != LifecycleState::kConstructed || !owner.valid()) {
    return LocalResult::failure(
        owner.valid() ? LocalError::kInvalidState
                      : LocalError::kInvalidArgument);
  }

  owner_ = owner;
  LocalResult result = tasks_.bindOwner(owner);
  if (!result.ok()) {
    state_ = LifecycleState::kClosed;
    return result;
  }

  result = display_.open(owner, display_config, display_capabilities_);
  if (!result.ok()) {
    closeAfterOpenFailure(owner);
    return result;
  }
  display_open_ = true;

  result = input_.open(owner, input_config, input_capabilities_);
  if (!result.ok()) {
    closeAfterOpenFailure(owner);
    return result;
  }
  input_open_ = true;
  state_ = LifecycleState::kRunning;
  return LocalResult::success();
}

LocalResult BackendLifecycleCoordinator::beginStop(
    OwnerToken caller, StopPolicy policy) noexcept {
  if (!isOwner(caller)) {
    return LocalResult::failure(LocalError::kWrongThread);
  }
  if (state_ == LifecycleState::kClosed) {
    return LocalResult::success();
  }
  if (state_ != LifecycleState::kRunning &&
      state_ != LifecycleState::kStopping) {
    return LocalResult::failure(LocalError::kInvalidState);
  }
  if (state_ == LifecycleState::kStopping && stop_policy_ != policy) {
    return LocalResult::failure(LocalError::kInvalidState);
  }

  const LocalResult queue_result = tasks_.beginStop(caller, policy);
  if (!queue_result.ok()) {
    return queue_result;
  }
  state_ = LifecycleState::kStopping;
  stop_policy_ = policy;

  if (input_open_) {
    const LocalResult input_result = input_.beginStop(caller);
    if (input_result.error == LocalError::kBusy) {
      return input_result;
    }
    if (!input_result.ok()) {
      rememberError(input_result.error);
    }
  }

  wakeup_.requestStop();

  if (policy == StopPolicy::kDrain) {
    for (std::size_t pass = 0;
         pass < tasks_.capacity() && tasks_.depth() != 0; ++pass) {
      const PumpResult pump_result = tasks_.pump(caller);
      if (pump_result.error == LocalError::kBusy) {
        return LocalResult::failure(LocalError::kBusy);
      }
      if (!pump_result.ok() || pump_result.executed == 0) {
        rememberError(pump_result.ok() ? LocalError::kBusy
                                       : pump_result.error);
        return LocalResult::failure(stop_error_);
      }
    }
    if (tasks_.depth() != 0) {
      return LocalResult::failure(LocalError::kBusy);
    }
  }

  if (input_open_ && !input_discarded_) {
    const DrainResult discard_result = input_.discardPending(caller);
    if (discard_result.error == LocalError::kBusy) {
      return LocalResult::failure(LocalError::kBusy);
    }
    if (discard_result.ok()) {
      discarded_input_samples_ += discard_result.sample_count;
      input_discarded_ = true;
    } else {
      rememberError(discard_result.error);
    }
  }
  stop_prepared_ = true;
  return stop_error_ == LocalError::kNone
             ? LocalResult::success()
             : LocalResult::failure(stop_error_);
}

LocalResult BackendLifecycleCoordinator::finishStop(
    OwnerToken caller) noexcept {
  if (!isOwner(caller)) {
    return LocalResult::failure(LocalError::kWrongThread);
  }
  if (state_ == LifecycleState::kClosed) {
    return LocalResult::success();
  }
  if (state_ != LifecycleState::kStopping) {
    return LocalResult::failure(LocalError::kInvalidState);
  }
  if (!stop_prepared_) {
    return LocalResult::failure(LocalError::kInvalidState);
  }

  if (input_open_) {
    const LocalResult result = input_.close(caller);
    if (result.error == LocalError::kBusy) {
      return result;
    }
    if (!result.ok()) {
      rememberError(result.error);
    }
    input_open_ = false;
  }
  if (display_open_) {
    const LocalResult result = display_.close(caller);
    if (result.error == LocalError::kBusy) {
      return result;
    }
    if (!result.ok()) {
      rememberError(result.error);
    }
    display_open_ = false;
  }
  const LocalResult wake_result = wakeup_.close(caller);
  if (wake_result.error == LocalError::kBusy) {
    return wake_result;
  }
  if (!wake_result.ok()) {
    rememberError(wake_result.error);
  }
  const LocalResult queue_result = tasks_.finishStop(caller);
  if (queue_result.error == LocalError::kBusy) {
    return queue_result;
  }
  if (!queue_result.ok()) {
    rememberError(queue_result.error);
    return queue_result;
  }
  state_ = LifecycleState::kClosed;
  return stop_error_ == LocalError::kNone
             ? LocalResult::success()
             : LocalResult::failure(stop_error_);
}

bool BackendLifecycleCoordinator::isOwner(OwnerToken caller) const noexcept {
  return owner_.valid() && caller == owner_;
}

void BackendLifecycleCoordinator::rememberError(LocalError error) noexcept {
  if (stop_error_ == LocalError::kNone && error != LocalError::kNone) {
    stop_error_ = error;
  }
}

void BackendLifecycleCoordinator::closeAfterOpenFailure(
    OwnerToken caller) noexcept {
  if (input_open_) {
    (void)input_.beginStop(caller);
    (void)input_.discardPending(caller);
    (void)input_.close(caller);
    input_open_ = false;
  }
  if (display_open_) {
    (void)display_.close(caller);
    display_open_ = false;
  }
  (void)tasks_.beginStop(caller, StopPolicy::kCancel);
  (void)tasks_.finishStop(caller);
  (void)wakeup_.close(caller);
  state_ = LifecycleState::kClosed;
}

}  // namespace quickapp::lvgl::foundation
