#pragma once

#include <array>
#include <atomic>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <optional>

#include "quickapp/core/foundation/port.h"
#include "quickapp/lvgl/foundation/owner_task_queue.h"
#include "quickapp/lvgl/foundation/try_critical_section.h"
#include "quickapp/lvgl/surface/page_root_backend.h"
#include "quickapp/lvgl/surface/surface_types.h"

namespace quickapp::lvgl::surface {

enum class LocalSurfacePhase : std::uint8_t {
  kHiddenEmpty,
  kHiddenMounted,
  kVisible,
  kHidden,
  kDestroying,
};

struct SurfaceHostLimits final {
  std::size_t max_live_roots{0};
  std::size_t max_operations{0};
};

[[nodiscard]] SurfaceHostLimits simulatorSurfaceHostLimits() noexcept;
[[nodiscard]] SurfaceHostLimits embeddedSurfaceHostLimits() noexcept;

struct SurfaceServiceResult final {
  foundation::LocalError error{foundation::LocalError::kNone};
  std::size_t delivered{0};
  std::size_t released{0};
};

class SurfaceHostAdapter final
    : public core::PlatformSurfacePort<SurfaceCommand> {
 public:
  static constexpr std::size_t kStorageCapacity = 16;
  using RootCallback = void (*)(void*, PageRootHandle) noexcept;

  SurfaceHostAdapter(foundation::OwnerTaskQueue& owner_tasks,
                     foundation::OwnerToken owner,
                     PageRootBackend& roots,
                     SurfaceContentLifecyclePort& content,
                     core::CoreIngressPort<SurfaceResult>& results,
                     SurfaceHostLimits limits) noexcept;
  ~SurfaceHostAdapter() noexcept override {
    assert(closed_.load(std::memory_order_acquire) && liveRootCount() == 0 &&
           pendingOperationCount() == 0 &&
           "SurfaceHostAdapter requires explicit owner close");
  }

  [[nodiscard]] core::EnqueueResult post(SurfaceCommand&& command) noexcept override;
  void close() noexcept override;

  [[nodiscard]] SurfaceServiceResult service(
      foundation::OwnerToken caller,
      std::size_t budget = kStorageCapacity) noexcept;
  [[nodiscard]] foundation::LocalResult markFullMountCommitted(
      foundation::OwnerToken caller,
      const core::SurfaceId& surface_id) noexcept;
  [[nodiscard]] foundation::LocalResult withPageRootForMount(
      foundation::OwnerToken caller, const core::SurfaceId& surface_id,
      void* context, RootCallback callback) noexcept;
  [[nodiscard]] foundation::LocalResult finishClose(
      foundation::OwnerToken caller) noexcept;

  [[nodiscard]] std::size_t liveRootCount() const noexcept;
  [[nodiscard]] std::size_t pendingOperationCount() const noexcept;
  [[nodiscard]] std::size_t pendingResultCount() const noexcept;
  [[nodiscard]] std::uint64_t overflowCount() const noexcept {
    return overflow_count_.load(std::memory_order_relaxed);
  }
  [[nodiscard]] std::uint64_t resetCount() const noexcept {
    return reset_count_.load(std::memory_order_relaxed);
  }

 private:
  enum class OperationState : std::uint8_t {
    kFree,
    kQueued,
    kExecuting,
    kResultPending,
    kDelivered,
  };

  struct OperationSlot final {
    std::atomic<OperationState> state{OperationState::kFree};
    std::optional<SurfaceCommand> command;
    std::optional<SurfaceResult> result;
  };

  struct SurfaceRecord final {
    core::SurfaceId surface_id;
    SurfaceViewport viewport;
    PageRootHandle root;
    LocalSurfacePhase phase{LocalSurfacePhase::kHiddenEmpty};
  };

  [[nodiscard]] bool isOwner(foundation::OwnerToken caller) const noexcept;
  [[nodiscard]] std::optional<std::size_t> findRecord(
      const core::SurfaceId& surface_id) const noexcept;
  [[nodiscard]] std::optional<std::size_t> findFreeRecord() const noexcept;
  [[nodiscard]] bool operationConflicts(const SurfaceCommand& left,
                                        const SurfaceCommand& right) const noexcept;
  void executeOperation(std::size_t index) noexcept;
  void deliverResult(std::size_t index) noexcept;
  [[nodiscard]] SurfaceResult execute(const SurfaceCommand& command) noexcept;
  [[nodiscard]] SurfaceResult executeCreate(
      const CreateSurfaceHost& command) noexcept;
  [[nodiscard]] SurfaceResult executePresentRoot(
      const PresentRootSurfaceHost& command) noexcept;
  [[nodiscard]] SurfaceResult executePresentPush(
      const PresentPushSurfaceHost& command) noexcept;
  [[nodiscard]] SurfaceResult executeVisibility(
      const SetSurfaceVisibility& command) noexcept;
  [[nodiscard]] SurfaceResult executeClose(
      const CloseSurfaceHost& command) noexcept;
  [[nodiscard]] SurfaceResult executeDestroy(
      const DestroySurfaceHost& command) noexcept;
  void clearOperation(std::size_t index) noexcept;
  void resetRecord(std::size_t index) noexcept;

  foundation::OwnerTaskQueue& owner_tasks_;
  foundation::OwnerToken owner_;
  PageRootBackend& roots_;
  SurfaceContentLifecyclePort& content_;
  core::CoreIngressPort<SurfaceResult>& results_;
  SurfaceHostLimits limits_;
  foundation::AtomicTryCriticalSection admission_;
  std::array<OperationSlot, kStorageCapacity> operations_{};
  std::array<std::optional<SurfaceRecord>, kStorageCapacity> records_{};
  std::array<std::optional<core::RequestId>, kStorageCapacity>
      completed_requests_{};
  std::size_t completed_request_cursor_{0};
  std::atomic<bool> accepting_{true};
  std::atomic<bool> closed_{false};
  std::atomic<std::uint64_t> overflow_count_{0};
  std::atomic<std::uint64_t> reset_count_{0};
};

}  // namespace quickapp::lvgl::surface
