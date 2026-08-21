#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>

#include <uv.h>

#include "quickapp/lvgl/runtime/loop_backend.h"

namespace quickapp::lvgl::backends {

class LibuvLoopBackend final : public runtime::OwnerLoopBackend {
 public:
  LibuvLoopBackend() noexcept = default;
  ~LibuvLoopBackend() override;

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

  [[nodiscard]] uv_loop_t* nativeLoop() noexcept {
    return initialized_ ? &loop_ : nullptr;
  }
  [[nodiscard]] bool closed() const noexcept { return closed_; }

 private:
  static void onAsync(uv_async_t* handle) noexcept;
  static void onDeadline(uv_timer_t* handle) noexcept;

  uv_loop_t loop_{};
  uv_async_t async_{};
  uv_timer_t timer_{};
  foundation::OwnerToken owner_{};
  std::atomic<bool> notified_{false};
  std::atomic<bool> stopping_{false};
  bool deadline_fired_{false};
  bool initialized_{false};
  bool closing_{false};
  bool closed_{false};
};

}  // namespace quickapp::lvgl::backends
