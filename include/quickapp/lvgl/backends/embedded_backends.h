#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>

#include "quickapp/lvgl/runtime/loop_backend.h"

namespace quickapp::lvgl::backends {

struct BuiltinLoopCallbacks final {
  void* context{nullptr};
  std::uint64_t (*now_ns)(void*) noexcept{nullptr};
  std::uint64_t (*resolution_ns)(void*) noexcept{nullptr};
  foundation::WakeResult (*notify)(void*) noexcept{nullptr};
  foundation::WakeResult (*wait_until)(void*, std::uint64_t) noexcept{nullptr};
  std::size_t (*service)(void*, std::size_t) noexcept{nullptr};
  foundation::LocalResult (*close)(void*) noexcept{nullptr};
};

class BuiltinLoopBackend final : public runtime::OwnerLoopBackend {
 public:
  explicit BuiltinLoopBackend(BuiltinLoopCallbacks callbacks) noexcept;
  ~BuiltinLoopBackend() override;

  foundation::LocalResult initialize(
      foundation::OwnerToken owner) noexcept override;
  foundation::LocalResult serviceOneTurn(
      foundation::OwnerToken caller,
      std::size_t max_callbacks) noexcept override;
  [[nodiscard]] std::uint64_t nowNs() const noexcept override;
  [[nodiscard]] std::uint64_t resolutionNs() const noexcept override;
  [[nodiscard]] foundation::WakeResult notify() noexcept override;
  [[nodiscard]] foundation::WakeResult waitUntil(
      foundation::OwnerToken caller,
      std::uint64_t deadline_ns) noexcept override;
  void requestStop() noexcept override;
  foundation::LocalResult close(
      foundation::OwnerToken caller) noexcept override;

  [[nodiscard]] bool closed() const noexcept { return closed_; }
  [[nodiscard]] std::size_t servicedCallbacks() const noexcept {
    return serviced_callbacks_;
  }

 private:
  BuiltinLoopCallbacks callbacks_;
  foundation::OwnerToken owner_{};
  std::atomic<bool> stopping_{false};
  bool initialized_{false};
  bool closed_{false};
  std::size_t serviced_callbacks_{0};
};

struct DeviceDisplayCallbacks final {
  void* context{nullptr};
  foundation::LocalResult (*open)(
      void*, const foundation::DisplayConfig&,
      foundation::DisplayCapabilities&) noexcept{nullptr};
  foundation::LocalResult (*present)(
      void*, const foundation::DisplayFrameView&) noexcept{nullptr};
  foundation::LocalResult (*close)(void*) noexcept{nullptr};
};

class DeviceCallbackDisplayBackend final : public foundation::DisplayBackend {
 public:
  DeviceCallbackDisplayBackend(foundation::OwnerToken owner,
                               DeviceDisplayCallbacks callbacks) noexcept;

  foundation::LocalResult open(
      foundation::OwnerToken caller,
      const foundation::DisplayConfig& config,
      foundation::DisplayCapabilities& capabilities) noexcept override;
  foundation::LocalResult present(
      foundation::OwnerToken caller,
      const foundation::DisplayFrameView& frame) noexcept override;
  foundation::LocalResult close(
      foundation::OwnerToken caller) noexcept override;
  [[nodiscard]] foundation::LifecycleState state() const noexcept override;

 private:
  foundation::OwnerToken owner_;
  DeviceDisplayCallbacks callbacks_;
  foundation::LifecycleState state_{
      foundation::LifecycleState::kConstructed};
};

struct DeviceInputCallbacks final {
  void* context{nullptr};
  foundation::LocalResult (*open)(
      void*, const foundation::InputConfig&,
      foundation::InputCapabilities&) noexcept{nullptr};
  foundation::LocalResult (*begin_stop)(void*) noexcept{nullptr};
  foundation::DrainResult (*drain)(
      void*, foundation::RawInputSample*, std::size_t) noexcept{nullptr};
  foundation::DrainResult (*discard_pending)(void*) noexcept{nullptr};
  foundation::LocalResult (*close)(void*) noexcept{nullptr};
};

class DeviceCallbackInputBackend final : public foundation::InputBackend {
 public:
  DeviceCallbackInputBackend(foundation::OwnerToken owner,
                             DeviceInputCallbacks callbacks) noexcept;

  foundation::LocalResult open(
      foundation::OwnerToken caller,
      const foundation::InputConfig& config,
      foundation::InputCapabilities& capabilities) noexcept override;
  foundation::LocalResult beginStop(
      foundation::OwnerToken caller) noexcept override;
  foundation::DrainResult drain(
      foundation::OwnerToken caller, foundation::RawInputSample* output,
      std::size_t capacity) noexcept override;
  foundation::DrainResult discardPending(
      foundation::OwnerToken caller) noexcept override;
  foundation::LocalResult close(
      foundation::OwnerToken caller) noexcept override;
  [[nodiscard]] foundation::LifecycleState state() const noexcept override;

 private:
  foundation::OwnerToken owner_;
  DeviceInputCallbacks callbacks_;
  foundation::LifecycleState state_{
      foundation::LifecycleState::kConstructed};
};

}  // namespace quickapp::lvgl::backends
