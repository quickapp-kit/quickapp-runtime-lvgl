#pragma once

#include <array>
#include <cassert>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <memory>
#include <map>
#include <string>
#include <vector>

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
  [[nodiscard]] void* nativeObject(const core::SurfaceId& surface_id,
                                   const core::NodeId& node_id) const noexcept;
  struct ImageSnapshot final {
    core::NodeId node_id;
    void* native_object{nullptr};
    bool has_descriptor{false};
    std::size_t pixel_bytes{0};
  };
  [[nodiscard]] std::vector<ImageSnapshot> imageSnapshots(
      const core::SurfaceId& surface_id) const;
  using ClickCallback = void (*)(void*, const core::SurfaceId&,
                                 const core::NodeId&, std::uint64_t) noexcept;
  [[nodiscard]] bool installClickHandler(const core::SurfaceId& surface_id,
                                          const core::NodeId& node_id,
                                          ClickCallback callback,
                                          void* context) noexcept;
  using InputCallback = void (*)(void*, const core::SurfaceId&,
                                 const core::NodeId&, core::package::EventType,
                                 const char*, std::uint64_t) noexcept;
  [[nodiscard]] bool installInputHandler(const core::SurfaceId& surface_id,
                                          const core::NodeId& node_id,
                                          InputCallback callback,
                                          void* context) noexcept;
  using SwitchCallback = void (*)(void*, const core::SurfaceId&,
                                  const core::NodeId&, bool, std::uint64_t) noexcept;
  [[nodiscard]] bool installSwitchHandler(const core::SurfaceId& surface_id,
                                           const core::NodeId& node_id,
                                           SwitchCallback callback,
                                           void* context) noexcept;
  using SliderCallback = void (*)(void*, const core::SurfaceId&,
                                  const core::NodeId&, double, bool,
                                  std::uint64_t) noexcept;
  [[nodiscard]] bool installSliderHandler(const core::SurfaceId& surface_id,
                                           const core::NodeId& node_id,
                                           SliderCallback callback,
                                           void* context) noexcept;
  enum class PickerEvent : std::uint8_t { kChange, kConfirm, kCancel };
  using PickerCallback = void (*)(void*, const core::SurfaceId&,
                                  const core::NodeId&, PickerEvent,
                                  std::int32_t, const char*,
                                  std::uint64_t) noexcept;
  [[nodiscard]] bool installPickerHandler(const core::SurfaceId& surface_id,
                                           const core::NodeId& node_id,
                                           PickerCallback callback,
                                           void* context) noexcept;
  [[nodiscard]] bool confirmPicker(const core::SurfaceId& surface_id,
                                   const core::NodeId& node_id) noexcept;
  [[nodiscard]] bool cancelPicker(const core::SurfaceId& surface_id,
                                  const core::NodeId& node_id) noexcept;
  using TabsCallback = void (*)(void*, const core::SurfaceId&,
                                const core::NodeId&, std::int32_t,
                                const char*, std::uint64_t) noexcept;
  [[nodiscard]] bool installTabsHandler(const core::SurfaceId& surface_id,
                                         const core::NodeId& node_id,
                                         TabsCallback callback,
                                         void* context) noexcept;
  using ScrollCallback = void (*)(void*, const core::SurfaceId&,
                                  const core::NodeId&, core::package::EventType,
                                  std::int32_t, std::int32_t, std::int32_t,
                                  std::uint64_t) noexcept;
  [[nodiscard]] bool installScrollHandler(const core::SurfaceId& surface_id,
                                          const core::NodeId& node_id,
                                          ScrollCallback callback,
                                          void* context) noexcept;
  void setResource(std::string path,
                   std::shared_ptr<const std::vector<std::uint8_t>> bytes) noexcept;
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

  struct InputBinding final {
    bool live{false};
    core::SurfaceId surface_id = core::SurfaceId::parse("srf:invalid").value();
    core::NodeId node_id = core::NodeId::parse("node:invalid").value();
    InputCallback callback{nullptr};
    void* context{nullptr};
  };

  struct SwitchBinding final {
    bool live{false};
    core::SurfaceId surface_id = core::SurfaceId::parse("srf:invalid").value();
    core::NodeId node_id = core::NodeId::parse("node:invalid").value();
    SwitchCallback callback{nullptr};
    void* context{nullptr};
  };

  struct SliderBinding final {
    bool live{false};
    core::SurfaceId surface_id = core::SurfaceId::parse("srf:invalid").value();
    core::NodeId node_id = core::NodeId::parse("node:invalid").value();
    SliderCallback callback{nullptr};
    void* context{nullptr};
    double minimum{0};
    double maximum{100};
    double step{1};
    double scale{1000};
  };

  struct PickerBinding final {
    bool live{false};
    core::SurfaceId surface_id = core::SurfaceId::parse("srf:invalid").value();
    core::NodeId node_id = core::NodeId::parse("node:invalid").value();
    PickerCallback callback{nullptr};
    void* context{nullptr};
  };

  struct TabsBinding final {
    bool live{false};
    core::SurfaceId surface_id = core::SurfaceId::parse("srf:invalid").value();
    core::NodeId node_id = core::NodeId::parse("node:invalid").value();
    TabsCallback callback{nullptr};
    void* context{nullptr};
  };

  struct ScrollBinding final {
    bool live{false};
    core::SurfaceId surface_id = core::SurfaceId::parse("srf:invalid").value();
    core::NodeId node_id = core::NodeId::parse("node:invalid").value();
    ScrollCallback callback{nullptr};
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
    void* image_descriptor{nullptr};
    unsigned image_source_width{0};
    unsigned image_source_height{0};
    std::vector<std::uint8_t> image_source_pixels;
    std::vector<std::uint8_t> image_pixels;
    std::optional<std::size_t> font_slot;
    double slider_minimum{0};
    double slider_maximum{100};
    double slider_step{1};
    double slider_scale{1000};
    std::int32_t picker_selected{0};
    std::int32_t tabs_selected{0};
    std::vector<std::string> tabs_items;
  };

  struct DecodedImageResource final {
    unsigned width{0};
    unsigned height{0};
    std::shared_ptr<const std::vector<std::uint8_t>> pixels;
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
  [[nodiscard]] bool updateImageDescriptor(HostSlot& slot, void* object,
                                           unsigned width, unsigned height) noexcept;
  [[nodiscard]] bool resizeImageForLayout(HostSlot& slot, void* object) noexcept;
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
  void destroyFonts() noexcept;
  [[nodiscard]] bool applySliderConfiguration(HostSlot& slot,
                                               void* object) noexcept;
  [[nodiscard]] bool applyPickerSelection(HostSlot& slot,
                                          void* object) noexcept;
  [[nodiscard]] bool applyTabsSelection(HostSlot& slot,
                                        void* object) noexcept;

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
  std::array<InputBinding, kStorageCapacity * 32> input_bindings_{};
  std::array<SwitchBinding, kStorageCapacity * 32> switch_bindings_{};
  std::array<SliderBinding, kStorageCapacity * 32> slider_bindings_{};
  std::array<PickerBinding, kStorageCapacity * 32> picker_bindings_{};
  std::array<TabsBinding, kStorageCapacity * 32> tabs_bindings_{};
  std::array<ScrollBinding, kStorageCapacity * 32> scroll_bindings_{};
  std::map<std::string, std::shared_ptr<const std::vector<std::uint8_t>>, std::less<>> resources_;
  std::map<std::string, DecodedImageResource, std::less<>> decoded_resources_;
  std::atomic<bool> accepting_{true};
  std::atomic<bool> closed_{false};
};

}  // namespace quickapp::lvgl::mount
