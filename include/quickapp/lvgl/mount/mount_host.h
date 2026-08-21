#pragma once

#include <array>
#include <cassert>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <optional>

#include "quickapp/core/foundation/port.h"
#include "quickapp/lvgl/foundation/owner_task_queue.h"
#include "quickapp/lvgl/foundation/try_critical_section.h"
#include "quickapp/lvgl/mount/mount_types.h"
#include "quickapp/lvgl/surface/page_root_backend.h"
#include "quickapp/lvgl/surface/surface_host.h"

namespace quickapp::lvgl::mount {

struct MountHostLimits final {
  std::size_t max_transactions{0};
  std::size_t max_host_objects{0};
  std::size_t max_operations{0};
  std::size_t max_font_instances{0};
};

[[nodiscard]] MountHostLimits simulatorMountHostLimits() noexcept;
[[nodiscard]] MountHostLimits embeddedMountHostLimits() noexcept;

class PageRootNativeLookup {
 public:
  virtual ~PageRootNativeLookup() = default;
  [[nodiscard]] virtual void* nativeObject(
      surface::PageRootHandle handle) noexcept = 0;
};

class MountHost final : public core::PlatformMountPort<MountTransaction> {
 public:
  static constexpr std::size_t kStorageCapacity = 16;

  MountHost(foundation::OwnerTaskQueue& owner_tasks,
            foundation::OwnerToken owner, surface::SurfaceHostAdapter& surfaces,
            PageRootNativeLookup& roots, MountResultSink& results,
            MountHostLimits limits) noexcept;
  ~MountHost() noexcept override {
    assert(closed_.load(std::memory_order_acquire) &&
           liveObjectCount() == 0 && liveFontCount() == 0 &&
           pendingCount() == 0 &&
           "MountHost requires explicit owner close");
  }

  [[nodiscard]] core::EnqueueResult post(MountTransaction&& transaction) noexcept override;
  void close() noexcept override;

  [[nodiscard]] foundation::LocalResult service(
      foundation::OwnerToken caller, std::size_t budget = kStorageCapacity) noexcept;
  [[nodiscard]] foundation::LocalResult finishClose(
      foundation::OwnerToken caller) noexcept;
  [[nodiscard]] foundation::LocalResult releaseSurface(
      foundation::OwnerToken caller,
      const core::SurfaceId& surface_id) noexcept;
  [[nodiscard]] std::size_t liveObjectCount() const noexcept;
  [[nodiscard]] std::size_t liveFontCount() const noexcept;
  [[nodiscard]] std::size_t pendingCount() const noexcept;
  [[nodiscard]] std::optional<core::NodeId> nodeAt(
      const core::SurfaceId& surface_id, std::int32_t x,
      std::int32_t y) const noexcept;
  using ClickCallback = void (*)(void*, const core::SurfaceId&,
                                 const core::NodeId&, std::uint64_t) noexcept;
  [[nodiscard]] bool installClickHandler(const core::SurfaceId& surface_id,
                                          const core::NodeId& node_id,
                                          ClickCallback callback,
                                          void* context) noexcept;
  [[nodiscard]] std::size_t objectLimit() const noexcept {
    return limits_.max_host_objects;
  }

 public:
  struct ClickBinding final {
    bool live{false};
    core::SurfaceId surface_id = core::SurfaceId::parse("srf:invalid").value();
    core::NodeId node_id = core::NodeId::parse("node:invalid").value();
    ClickCallback callback{nullptr};
    void* context{nullptr};
  };

 private:
  struct HostSlot final {
    bool live{false};
    bool attached{false};
    std::optional<core::SurfaceId> surface_id;
    std::optional<core::NodeId> node_id;
    core::package::HostComponentType type{core::package::HostComponentType::kView};
    void* native_object{nullptr};
    void* private_label{nullptr};
    std::optional<std::size_t> font_slot;
  };

  struct FontSlot final {
    bool live{false};
    std::int32_t size{0};
    std::size_t references{0};
    void* native_font{nullptr};
  };

  struct TransactionSlot final {
    bool occupied{false};
    std::optional<MountTransaction> transaction;
    std::optional<MountResult> result;
  };
  [[nodiscard]] bool isOwner(foundation::OwnerToken caller) const noexcept;
  [[nodiscard]] std::optional<std::size_t> findSlot(
      const core::SurfaceId& surface_id,
      const core::NodeId& node_id) const noexcept;
  [[nodiscard]] std::optional<std::size_t> freeSlot() const noexcept;
  [[nodiscard]] std::size_t liveObjectCountForSurface(
      const core::SurfaceId& surface_id) const noexcept;
  [[nodiscard]] bool preflight(const MountTransaction& transaction,
                               surface::PageRootHandle* root) noexcept;
  [[nodiscard]] MountResult execute(const MountTransaction& transaction) noexcept;
  [[nodiscard]] foundation::LocalResult resolveRoot(
      const core::SurfaceId& surface_id, void*& root) noexcept;
  [[nodiscard]] std::optional<std::size_t> acquireFont(
      std::int32_t size) noexcept;
  void releaseFont(std::size_t index) noexcept;
  void executeSlot(std::size_t index) noexcept;
  void clearSlot(std::size_t index) noexcept;
  void destroySlot(std::size_t index) noexcept;
  void destroySurfaceObjects(const core::SurfaceId& surface_id) noexcept;
  void destroyAllObjects() noexcept;

  foundation::OwnerTaskQueue& owner_tasks_;
  foundation::OwnerToken owner_{};
  surface::SurfaceHostAdapter& surfaces_;
  PageRootNativeLookup& roots_;
  MountResultSink& results_;
  MountHostLimits limits_{};
  foundation::AtomicTryCriticalSection admission_{};
  std::array<TransactionSlot, kStorageCapacity> transactions_{};
  std::array<HostSlot, kStorageCapacity * 32> objects_{};
  std::array<FontSlot, kStorageCapacity> fonts_{};
  std::array<ClickBinding, kStorageCapacity * 32> click_bindings_{};
  std::atomic<bool> accepting_{true};
  std::atomic<bool> closed_{false};
};

}  // namespace quickapp::lvgl::mount
