#pragma once

#include <atomic>
#include <cassert>
#include <cstddef>

#include "quickapp/lvgl/foundation/owner_task.h"
#include "quickapp/lvgl/foundation/ports.h"
#include "quickapp/lvgl/foundation/try_critical_section.h"
#include "quickapp/lvgl/foundation/types.h"

namespace quickapp::lvgl::foundation {

enum class PostStatus : std::uint8_t {
  kAccepted,
  kFull,
  kStopping,
  kBusy,
  kInvalid,
};

struct PostOutcome {
  PostStatus status{PostStatus::kInvalid};
  WakeResult wake_result{WakeResult::kNotified};
};

struct PumpResult {
  LocalError error{LocalError::kNone};
  std::size_t executed{0};
  std::size_t remaining{0};

  [[nodiscard]] bool ok() const noexcept {
    return error == LocalError::kNone;
  }
};

class OwnerTaskQueue final {
 public:
  OwnerTaskQueue(OwnerTask* storage, std::size_t capacity,
                 std::size_t max_tasks_per_pump,
                 WakeupPort* wakeup = nullptr,
                 TryCriticalSection* critical_section = nullptr) noexcept;
  ~OwnerTaskQueue() noexcept {
    assert(destructionInvariantHolds() &&
           "OwnerTaskQueue requires explicit owner stop/close");
  }

  OwnerTaskQueue(const OwnerTaskQueue&) = delete;
  OwnerTaskQueue& operator=(const OwnerTaskQueue&) = delete;

  LocalResult bindOwner(OwnerToken owner) noexcept;
  [[nodiscard]] PostOutcome post(OwnerTask&& task) noexcept;
  [[nodiscard]] PumpResult pump(OwnerToken caller,
                                std::size_t requested_max = 0) noexcept;
  LocalResult beginStop(OwnerToken caller, StopPolicy policy) noexcept;
  LocalResult finishStop(OwnerToken caller) noexcept;

  [[nodiscard]] std::size_t depth() const noexcept;
  [[nodiscard]] std::size_t peakDepth() const noexcept;
  [[nodiscard]] std::size_t capacity() const noexcept { return capacity_; }
  [[nodiscard]] LifecycleState state() const noexcept;
  [[nodiscard]] OwnerToken owner() const noexcept { return owner_; }
  [[nodiscard]] bool destructionInvariantHolds() const noexcept;

 private:
  struct PopResult {
    LocalError error{LocalError::kNone};
    bool has_task{false};
  };

  [[nodiscard]] bool isOwner(OwnerToken caller) const noexcept;
  [[nodiscard]] PopResult tryPop(OwnerTask& output) noexcept;
  [[nodiscard]] LocalResult cancelPending(OwnerToken caller) noexcept;

  OwnerTask* storage_{nullptr};
  std::size_t capacity_{0};
  std::size_t max_tasks_per_pump_{0};
  WakeupPort* wakeup_{nullptr};
  OwnerToken owner_{};
  AtomicTryCriticalSection local_critical_section_{};
  TryCriticalSection* critical_section_{nullptr};
  std::size_t head_{0};
  std::size_t size_{0};
  std::size_t peak_depth_{0};
  std::atomic<std::size_t> depth_snapshot_{0};
  std::atomic<std::size_t> peak_depth_snapshot_{0};
  std::atomic<LifecycleState> state_{LifecycleState::kConstructed};
  StopPolicy stop_policy_{StopPolicy::kCancel};
};

}  // namespace quickapp::lvgl::foundation
