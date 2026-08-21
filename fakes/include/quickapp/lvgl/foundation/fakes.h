#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <limits>

#include "quickapp/lvgl/foundation/owner_task.h"
#include "quickapp/lvgl/foundation/ports.h"
#include "quickapp/lvgl/foundation/try_critical_section.h"
#include "quickapp/lvgl/foundation/types.h"

namespace quickapp::lvgl::foundation::fakes {

class FakeClock final : public BackendClock {
 public:
  explicit FakeClock(std::uint64_t resolution_ns = 1) noexcept;

  [[nodiscard]] std::uint64_t nowNs() const noexcept override;
  [[nodiscard]] std::uint64_t resolutionNs() const noexcept override;
  LocalResult setNowNs(std::uint64_t value) noexcept;
  LocalResult advanceNs(std::uint64_t delta) noexcept;

 private:
  std::atomic<std::uint64_t> now_ns_{0};
  std::uint64_t resolution_ns_{1};
};

class FakeWakeup final : public WakeupPort {
 public:
  FakeWakeup(OwnerToken owner, bool wait_supported = true) noexcept;

  [[nodiscard]] WakeResult notify() noexcept override;
  [[nodiscard]] WakeResult waitUntil(
      OwnerToken caller, std::uint64_t deadline_ns) noexcept override;
  void requestStop() noexcept override;
  LocalResult close(OwnerToken caller) noexcept override;

  void setNotifyResult(WakeResult result) noexcept;
  void setNextWaitResult(WakeResult result) noexcept;
  [[nodiscard]] std::uint64_t notifyCount() const noexcept;
  [[nodiscard]] std::uint64_t waitCount() const noexcept;
  [[nodiscard]] std::uint64_t lastDeadlineNs() const noexcept;
  [[nodiscard]] bool closed() const noexcept;

 private:
  OwnerToken owner_{};
  bool wait_supported_{true};
  std::atomic<bool> stopping_{false};
  std::atomic<bool> closed_{false};
  std::atomic<std::uint64_t> notify_count_{0};
  std::atomic<std::uint64_t> wait_count_{0};
  std::atomic<std::uint64_t> last_deadline_ns_{0};
  std::atomic<WakeResult> notify_result_{WakeResult::kNotified};
  std::atomic<WakeResult> next_wait_result_{WakeResult::kDeadline};
};

struct FakeTaskState {
  std::size_t executions{0};
  std::size_t destructions{0};
  OwnerToken destruction_owner{};
  OwnerToken (*current_owner)(void*) noexcept{nullptr};
  void* current_owner_context{nullptr};
};

[[nodiscard]] OwnerTask makeFakeTask(FakeTaskState& state) noexcept;

struct FakeDisplayFaults {
  bool fail_open{false};
  std::size_t fail_present_call{0};
  bool fail_close{false};
};

class FakeDisplay final : public DisplayBackend {
 public:
  FakeDisplay(OwnerToken owner, DisplayCapabilities capabilities) noexcept;

  LocalResult open(OwnerToken caller, const DisplayConfig& config,
                   DisplayCapabilities& capabilities) noexcept override;
  LocalResult present(OwnerToken caller,
                      const DisplayFrameView& frame) noexcept override;
  LocalResult close(OwnerToken caller) noexcept override;
  [[nodiscard]] LifecycleState state() const noexcept override;

  void setFaults(FakeDisplayFaults faults) noexcept;
  [[nodiscard]] std::size_t presentCount() const noexcept;
  [[nodiscard]] std::uint64_t lastFrameChecksum() const noexcept;
  [[nodiscard]] std::uint64_t lastFrameSequence() const noexcept;
  [[nodiscard]] std::size_t closeCount() const noexcept;

 private:
  [[nodiscard]] LocalResult validateFrame(
      const DisplayFrameView& frame) const noexcept;

  OwnerToken owner_{};
  DisplayCapabilities capabilities_{};
  FakeDisplayFaults faults_{};
  LifecycleState state_{LifecycleState::kConstructed};
  std::size_t present_count_{0};
  std::size_t close_count_{0};
  std::uint64_t last_frame_checksum_{0};
  std::uint64_t last_frame_sequence_{0};
};

class FakeInput final : public InputBackend {
 public:
  FakeInput(OwnerToken owner, RawInputSample* storage,
            std::size_t capacity,
            TryCriticalSection* critical_section = nullptr) noexcept;
  ~FakeInput() override = default;

  LocalResult open(OwnerToken caller, const InputConfig& config,
                   InputCapabilities& capabilities) noexcept override;
  LocalResult beginStop(OwnerToken caller) noexcept override;
  DrainResult drain(OwnerToken caller, RawInputSample* output,
                    std::size_t capacity) noexcept override;
  DrainResult discardPending(OwnerToken caller) noexcept override;
  LocalResult close(OwnerToken caller) noexcept override;
  [[nodiscard]] LifecycleState state() const noexcept override;

  LocalResult pushRaw(const RawInputSample& sample) noexcept;
  void setFailOpen(bool fail) noexcept;
  void setFailClose(bool fail) noexcept;
  [[nodiscard]] std::size_t depth() const noexcept;
  [[nodiscard]] std::size_t peakDepth() const noexcept;
  [[nodiscard]] std::uint64_t overflowCount() const noexcept;
  [[nodiscard]] std::uint64_t coalescedCount() const noexcept;
  [[nodiscard]] std::size_t closeCount() const noexcept;

 private:
  [[nodiscard]] bool isOwner(OwnerToken caller) const noexcept;

  OwnerToken owner_{};
  RawInputSample* storage_{nullptr};
  std::size_t capacity_{0};
  std::size_t max_samples_per_drain_{0};
  AtomicTryCriticalSection local_critical_section_{};
  TryCriticalSection* critical_section_{nullptr};
  std::size_t head_{0};
  std::size_t size_{0};
  std::size_t peak_depth_{0};
  std::atomic<std::size_t> depth_snapshot_{0};
  std::atomic<std::size_t> peak_depth_snapshot_{0};
  std::atomic<std::uint64_t> overflow_count_{0};
  std::atomic<std::uint64_t> coalesced_count_{0};
  std::atomic<std::size_t> close_count_{0};
  std::atomic<LifecycleState> state_{LifecycleState::kConstructed};
  std::atomic<bool> fail_open_{false};
  std::atomic<bool> fail_close_{false};
};

}  // namespace quickapp::lvgl::foundation::fakes
