#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

#include <SDL3/SDL.h>

#include "quickapp/lvgl/foundation/ports.h"

namespace quickapp::lvgl::backends {

class SdlDisplayBackend final : public foundation::DisplayBackend {
 public:
  explicit SdlDisplayBackend(foundation::OwnerToken owner) noexcept;
  ~SdlDisplayBackend() override;

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

  [[nodiscard]] SDL_WindowID windowId() const noexcept;
  [[nodiscard]] std::uint32_t width() const noexcept { return width_; }
  [[nodiscard]] std::uint32_t height() const noexcept { return height_; }
  [[nodiscard]] std::uint64_t presentedFrames() const noexcept {
    return presented_frames_;
  }

 private:
  foundation::OwnerToken owner_;
  SDL_Window* window_{nullptr};
  std::uint32_t width_{0};
  std::uint32_t height_{0};
  std::size_t max_dirty_regions_{0};
  std::uint64_t presented_frames_{0};
  foundation::LifecycleState state_{
      foundation::LifecycleState::kConstructed};
};

class SdlRawInputBackend final : public foundation::InputBackend {
 public:
  static constexpr std::size_t kMaxSamples = 128;

  SdlRawInputBackend(foundation::OwnerToken owner,
                     SdlDisplayBackend& display) noexcept;

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

  [[nodiscard]] std::size_t depth() const noexcept { return size_; }

 private:
  [[nodiscard]] bool pushSample(
      const foundation::RawInputSample& sample) noexcept;
  void collectEvents() noexcept;

  foundation::OwnerToken owner_;
  SdlDisplayBackend* display_;
  std::array<foundation::RawInputSample, kMaxSamples> samples_{};
  std::size_t capacity_{0};
  std::size_t max_samples_per_drain_{0};
  std::size_t head_{0};
  std::size_t size_{0};
  std::uint64_t sequence_{0};
  std::uint64_t overflow_count_{0};
  std::uint64_t coalesced_count_{0};
  bool mouse_down_{false};
  foundation::LifecycleState state_{
      foundation::LifecycleState::kConstructed};
};

}  // namespace quickapp::lvgl::backends
