#pragma once

#include <array>
#include <atomic>
#include <cassert>
#include <cstddef>
#include <optional>

#include "quickapp/core/foundation/observation.h"
#include "quickapp/core/foundation/port.h"
#include "quickapp/core/render/initial_render_pipeline.h"
#include "quickapp/lvgl/mount/mount_host.h"

namespace quickapp::lvgl::integration {

// Adapts the Core typed MountPort to the bounded LVGL MountHost and coordinates
// the S03 root Present before acknowledging a successful mount to Core.
class CoreMountBridge final : public core::render::MountPort,
                              public mount::MountResultSink {
 public:
  static constexpr std::size_t kCapacity = mount::MountHost::kStorageCapacity;

  CoreMountBridge(
      foundation::OwnerToken owner,
      core::CoreIngressPort<core::render::MountTransactionResult>& results,
      core::ObservationEmitter* observation = nullptr,
      bool auto_present_full_mount = true) noexcept;
  ~CoreMountBridge() noexcept override;

  CoreMountBridge(const CoreMountBridge&) = delete;
  CoreMountBridge& operator=(const CoreMountBridge&) = delete;

  void bind(mount::MountHost& mounts,
            surface::SurfaceHostAdapter& surfaces,
            core::CoreIngressPort<surface::SurfaceResult>* passthrough = nullptr) noexcept;

  [[nodiscard]] core::EnqueueResult post(
      core::render::MountTransaction&& transaction) noexcept override;
  void close() noexcept override;

  void complete(mount::MountResult result) noexcept override;

  // Called by the S03 SurfaceHost result port on the owner thread.
  [[nodiscard]] core::EnqueueResult acceptSurfaceResult(
      surface::SurfaceResult&& result) noexcept;

  [[nodiscard]] foundation::LocalResult service(
      foundation::OwnerToken caller, std::size_t budget = kCapacity) noexcept;
  [[nodiscard]] std::size_t pendingCount() const noexcept;
  [[nodiscard]] bool closed() const noexcept {
    return closed_.load(std::memory_order_acquire);
  }

 private:
  enum class Phase : std::uint8_t {
    kMountPending,
    kPresentPending,
    kPresentPosted,
    kCoreResultPending,
  };

  struct Pending final {
    bool occupied{false};
    core::SurfaceId surface_id = core::SurfaceId::parse("srf:invalid").value();
    core::render::RenderSourceId source_id =
        core::RequestId::parse("req:invalid").value();
    core::MountAttemptId mount_attempt_id =
        core::MountAttemptId::parse("mnt:invalid").value();
    std::uint64_t revision{0};
    std::size_t operation_count{0};
    bool full_mount{false};
    Phase phase{Phase::kMountPending};
    bool result_mounted{false};
    std::optional<core::RuntimeError> result_error;
  };

  [[nodiscard]] std::optional<std::size_t> find(
      const core::SurfaceId& surface_id,
      const core::MountAttemptId& mount_attempt_id,
      const core::render::RenderSourceId& source_id) const noexcept;
  [[nodiscard]] std::optional<std::size_t> free() const noexcept;
  [[nodiscard]] core::RuntimeResult<mount::MountTransaction> convert(
      const core::render::MountTransaction& transaction) const noexcept;
  [[nodiscard]] core::EnqueueResult tryPresent(std::size_t index) noexcept;
  [[nodiscard]] core::EnqueueResult tryCoreResult(std::size_t index) noexcept;
  void emitPresent(core::MarkerName marker, const Pending& pending,
                   std::optional<core::RuntimeErrorCode> error = std::nullopt) noexcept;
  void clear(std::size_t index) noexcept;

  foundation::OwnerToken owner_{};
  core::CoreIngressPort<core::render::MountTransactionResult>& results_;
  core::ObservationEmitter* observation_{nullptr};
  bool auto_present_full_mount_{true};
  mount::MountHost* mounts_{nullptr};
  surface::SurfaceHostAdapter* surfaces_{nullptr};
  core::CoreIngressPort<surface::SurfaceResult>* passthrough_{nullptr};
  foundation::AtomicTryCriticalSection admission_{};
  std::array<Pending, kCapacity> pending_{};
  std::atomic<bool> accepting_{true};
  std::atomic<bool> closed_{false};
};

}  // namespace quickapp::lvgl::integration
