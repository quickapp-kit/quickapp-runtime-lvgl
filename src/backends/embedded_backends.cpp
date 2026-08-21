#include "quickapp/lvgl/backends/embedded_backends.h"

#include <cassert>

namespace quickapp::lvgl::backends {

BuiltinLoopBackend::BuiltinLoopBackend(
    BuiltinLoopCallbacks callbacks) noexcept
    : callbacks_(callbacks) {}

BuiltinLoopBackend::~BuiltinLoopBackend() {
  assert((!initialized_ || closed_) &&
         "BuiltinLoopBackend requires explicit close");
}

foundation::LocalResult BuiltinLoopBackend::initialize(
    foundation::OwnerToken owner) noexcept {
  if (initialized_ || !owner.valid() || callbacks_.now_ns == nullptr ||
      callbacks_.resolution_ns == nullptr ||
      callbacks_.resolution_ns(callbacks_.context) == 0) {
    return foundation::LocalResult::failure(
        foundation::LocalError::kInvalidArgument);
  }
  owner_ = owner;
  initialized_ = true;
  closed_ = false;
  return foundation::LocalResult::success();
}

foundation::LocalResult BuiltinLoopBackend::serviceOneTurn(
    foundation::OwnerToken caller,
    std::size_t max_callbacks) noexcept {
  if (caller != owner_) {
    return foundation::LocalResult::failure(
        foundation::LocalError::kWrongThread);
  }
  if (!initialized_ || closed_ || max_callbacks == 0) {
    return foundation::LocalResult::failure(
        foundation::LocalError::kInvalidState);
  }
  if (callbacks_.service != nullptr) {
    serviced_callbacks_ +=
        callbacks_.service(callbacks_.context, max_callbacks);
  }
  return foundation::LocalResult::success();
}

std::uint64_t BuiltinLoopBackend::nowNs() const noexcept {
  return callbacks_.now_ns == nullptr ? 0
                                      : callbacks_.now_ns(callbacks_.context);
}

std::uint64_t BuiltinLoopBackend::resolutionNs() const noexcept {
  return callbacks_.resolution_ns == nullptr
             ? 0
             : callbacks_.resolution_ns(callbacks_.context);
}

foundation::WakeResult BuiltinLoopBackend::notify() noexcept {
  if (closed_ || stopping_.load(std::memory_order_relaxed)) {
    return foundation::WakeResult::kStopping;
  }
  return callbacks_.notify == nullptr
             ? foundation::WakeResult::kUnsupported
             : callbacks_.notify(callbacks_.context);
}

foundation::WakeResult BuiltinLoopBackend::waitUntil(
    foundation::OwnerToken caller,
    std::uint64_t deadline_ns) noexcept {
  if (caller != owner_) {
    return foundation::WakeResult::kWrongThread;
  }
  if (closed_ || stopping_.load(std::memory_order_relaxed)) {
    return foundation::WakeResult::kStopping;
  }
  return callbacks_.wait_until == nullptr
             ? foundation::WakeResult::kUnsupported
             : callbacks_.wait_until(callbacks_.context, deadline_ns);
}

void BuiltinLoopBackend::requestStop() noexcept {
  stopping_.store(true, std::memory_order_relaxed);
}

foundation::LocalResult BuiltinLoopBackend::close(
    foundation::OwnerToken caller) noexcept {
  if (caller != owner_) {
    return foundation::LocalResult::failure(
        foundation::LocalError::kWrongThread);
  }
  if (closed_) {
    return foundation::LocalResult::success();
  }
  foundation::LocalResult result = foundation::LocalResult::success();
  if (callbacks_.close != nullptr) {
    result = callbacks_.close(callbacks_.context);
  }
  if (result.error == foundation::LocalError::kBusy) {
    return result;
  }
  closed_ = true;
  stopping_.store(true, std::memory_order_relaxed);
  return result;
}

DeviceCallbackDisplayBackend::DeviceCallbackDisplayBackend(
    foundation::OwnerToken owner,
    DeviceDisplayCallbacks callbacks) noexcept
    : owner_(owner), callbacks_(callbacks) {}

foundation::LocalResult DeviceCallbackDisplayBackend::open(
    foundation::OwnerToken caller,
    const foundation::DisplayConfig& config,
    foundation::DisplayCapabilities& capabilities) noexcept {
  if (caller != owner_) {
    return foundation::LocalResult::failure(
        foundation::LocalError::kWrongThread);
  }
  if (state_ != foundation::LifecycleState::kConstructed ||
      callbacks_.open == nullptr) {
    return foundation::LocalResult::failure(
        foundation::LocalError::kInvalidState);
  }
  const foundation::LocalResult result =
      callbacks_.open(callbacks_.context, config, capabilities);
  state_ = result.ok() ? foundation::LifecycleState::kRunning
                       : foundation::LifecycleState::kClosed;
  return result;
}

foundation::LocalResult DeviceCallbackDisplayBackend::present(
    foundation::OwnerToken caller,
    const foundation::DisplayFrameView& frame) noexcept {
  if (caller != owner_) {
    return foundation::LocalResult::failure(
        foundation::LocalError::kWrongThread);
  }
  if (state_ != foundation::LifecycleState::kRunning ||
      callbacks_.present == nullptr) {
    return foundation::LocalResult::failure(
        foundation::LocalError::kInvalidState);
  }
  return callbacks_.present(callbacks_.context, frame);
}

foundation::LocalResult DeviceCallbackDisplayBackend::close(
    foundation::OwnerToken caller) noexcept {
  if (caller != owner_) {
    return foundation::LocalResult::failure(
        foundation::LocalError::kWrongThread);
  }
  if (state_ == foundation::LifecycleState::kClosed) {
    return foundation::LocalResult::success();
  }
  foundation::LocalResult result = foundation::LocalResult::success();
  if (callbacks_.close != nullptr) {
    result = callbacks_.close(callbacks_.context);
  }
  if (result.error == foundation::LocalError::kBusy) {
    return result;
  }
  state_ = foundation::LifecycleState::kClosed;
  return result;
}

foundation::LifecycleState DeviceCallbackDisplayBackend::state()
    const noexcept {
  return state_;
}

DeviceCallbackInputBackend::DeviceCallbackInputBackend(
    foundation::OwnerToken owner,
    DeviceInputCallbacks callbacks) noexcept
    : owner_(owner), callbacks_(callbacks) {}

foundation::LocalResult DeviceCallbackInputBackend::open(
    foundation::OwnerToken caller,
    const foundation::InputConfig& config,
    foundation::InputCapabilities& capabilities) noexcept {
  if (caller != owner_) {
    return foundation::LocalResult::failure(
        foundation::LocalError::kWrongThread);
  }
  if (state_ != foundation::LifecycleState::kConstructed ||
      callbacks_.open == nullptr) {
    return foundation::LocalResult::failure(
        foundation::LocalError::kInvalidState);
  }
  const foundation::LocalResult result =
      callbacks_.open(callbacks_.context, config, capabilities);
  state_ = result.ok() ? foundation::LifecycleState::kRunning
                       : foundation::LifecycleState::kClosed;
  return result;
}

foundation::LocalResult DeviceCallbackInputBackend::beginStop(
    foundation::OwnerToken caller) noexcept {
  if (caller != owner_) {
    return foundation::LocalResult::failure(
        foundation::LocalError::kWrongThread);
  }
  if (state_ == foundation::LifecycleState::kStopping ||
      state_ == foundation::LifecycleState::kClosed) {
    return foundation::LocalResult::success();
  }
  if (state_ != foundation::LifecycleState::kRunning) {
    return foundation::LocalResult::failure(
        foundation::LocalError::kInvalidState);
  }
  foundation::LocalResult result = foundation::LocalResult::success();
  if (callbacks_.begin_stop != nullptr) {
    result = callbacks_.begin_stop(callbacks_.context);
  }
  if (result.ok()) {
    state_ = foundation::LifecycleState::kStopping;
  }
  return result;
}

foundation::DrainResult DeviceCallbackInputBackend::drain(
    foundation::OwnerToken caller, foundation::RawInputSample* output,
    std::size_t capacity) noexcept {
  if (caller != owner_) {
    return {foundation::LocalError::kWrongThread, 0, 0, 0};
  }
  if (state_ != foundation::LifecycleState::kRunning ||
      callbacks_.drain == nullptr || output == nullptr || capacity == 0) {
    return {foundation::LocalError::kInvalidState, 0, 0, 0};
  }
  return callbacks_.drain(callbacks_.context, output, capacity);
}

foundation::DrainResult DeviceCallbackInputBackend::discardPending(
    foundation::OwnerToken caller) noexcept {
  if (caller != owner_) {
    return {foundation::LocalError::kWrongThread, 0, 0, 0};
  }
  if (callbacks_.discard_pending == nullptr) {
    return {foundation::LocalError::kNone, 0, 0, 0};
  }
  return callbacks_.discard_pending(callbacks_.context);
}

foundation::LocalResult DeviceCallbackInputBackend::close(
    foundation::OwnerToken caller) noexcept {
  if (caller != owner_) {
    return foundation::LocalResult::failure(
        foundation::LocalError::kWrongThread);
  }
  if (state_ == foundation::LifecycleState::kClosed) {
    return foundation::LocalResult::success();
  }
  foundation::LocalResult result = foundation::LocalResult::success();
  if (callbacks_.close != nullptr) {
    result = callbacks_.close(callbacks_.context);
  }
  if (result.error == foundation::LocalError::kBusy) {
    return result;
  }
  state_ = foundation::LifecycleState::kClosed;
  return result;
}

foundation::LifecycleState DeviceCallbackInputBackend::state()
    const noexcept {
  return state_;
}

}  // namespace quickapp::lvgl::backends
