#pragma once

#include "quickapp/lvgl/foundation/owner_task_queue.h"
#include "quickapp/lvgl/foundation/ports.h"
#include "quickapp/lvgl/foundation/types.h"

namespace quickapp::lvgl::foundation {

class BackendLifecycleCoordinator final {
 public:
  BackendLifecycleCoordinator(OwnerTaskQueue& tasks, WakeupPort& wakeup,
                              DisplayBackend& display,
                              InputBackend& input) noexcept;

  LocalResult open(OwnerToken owner, const DisplayConfig& display_config,
                   const InputConfig& input_config) noexcept;
  LocalResult beginStop(OwnerToken caller, StopPolicy policy) noexcept;
  LocalResult finishStop(OwnerToken caller) noexcept;

  [[nodiscard]] LifecycleState state() const noexcept { return state_; }
  [[nodiscard]] OwnerToken owner() const noexcept { return owner_; }
  [[nodiscard]] const DisplayCapabilities& displayCapabilities() const noexcept {
    return display_capabilities_;
  }
  [[nodiscard]] const InputCapabilities& inputCapabilities() const noexcept {
    return input_capabilities_;
  }
  [[nodiscard]] std::size_t discardedInputSamples() const noexcept {
    return discarded_input_samples_;
  }

 private:
  [[nodiscard]] bool isOwner(OwnerToken caller) const noexcept;
  void closeAfterOpenFailure(OwnerToken caller) noexcept;
  void rememberError(LocalError error) noexcept;

  OwnerTaskQueue& tasks_;
  WakeupPort& wakeup_;
  DisplayBackend& display_;
  InputBackend& input_;
  OwnerToken owner_{};
  LifecycleState state_{LifecycleState::kConstructed};
  StopPolicy stop_policy_{StopPolicy::kCancel};
  DisplayCapabilities display_capabilities_{};
  InputCapabilities input_capabilities_{};
  std::size_t discarded_input_samples_{0};
  bool display_open_{false};
  bool input_open_{false};
  bool input_discarded_{false};
  bool stop_prepared_{false};
  LocalError stop_error_{LocalError::kNone};
};

}  // namespace quickapp::lvgl::foundation
