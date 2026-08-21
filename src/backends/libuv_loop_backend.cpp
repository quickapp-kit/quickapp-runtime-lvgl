#include "quickapp/lvgl/backends/libuv_loop_backend.h"

#include <cassert>
#include <limits>

namespace quickapp::lvgl::backends {

LibuvLoopBackend::~LibuvLoopBackend() {
  assert((!initialized_ || closed_) &&
         "LibuvLoopBackend requires explicit close");
}

foundation::LocalResult LibuvLoopBackend::initialize(
    foundation::OwnerToken owner) noexcept {
  if (initialized_ || !owner.valid()) {
    return foundation::LocalResult::failure(
        foundation::LocalError::kInvalidArgument);
  }
  if (uv_loop_init(&loop_) != 0) {
    return foundation::LocalResult::failure(
        foundation::LocalError::kBackendFailed);
  }
  async_.data = this;
  if (uv_async_init(&loop_, &async_, &LibuvLoopBackend::onAsync) != 0) {
    (void)uv_loop_close(&loop_);
    return foundation::LocalResult::failure(
        foundation::LocalError::kBackendFailed);
  }
  timer_.data = this;
  if (uv_timer_init(&loop_, &timer_) != 0) {
    uv_close(reinterpret_cast<uv_handle_t*>(&async_), nullptr);
    (void)uv_run(&loop_, UV_RUN_NOWAIT);
    (void)uv_loop_close(&loop_);
    return foundation::LocalResult::failure(
        foundation::LocalError::kBackendFailed);
  }
  owner_ = owner;
  initialized_ = true;
  return foundation::LocalResult::success();
}

foundation::LocalResult LibuvLoopBackend::serviceOneTurn(
    foundation::OwnerToken caller,
    std::size_t max_callbacks) noexcept {
  if (caller != owner_) {
    return foundation::LocalResult::failure(
        foundation::LocalError::kWrongThread);
  }
  if (!initialized_ || closed_ || max_callbacks == 0) {
    return foundation::LocalResult::failure(
        foundation::LocalError::kInvalidState);
  }
  (void)uv_run(&loop_, UV_RUN_NOWAIT);
  return foundation::LocalResult::success();
}

std::uint64_t LibuvLoopBackend::nowNs() const noexcept {
  return uv_hrtime();
}

std::uint64_t LibuvLoopBackend::resolutionNs() const noexcept {
  return 1;
}

foundation::WakeResult LibuvLoopBackend::notify() noexcept {
  if (!initialized_ || closed_ || stopping_.load(std::memory_order_relaxed)) {
    return foundation::WakeResult::kStopping;
  }
  return uv_async_send(&async_) == 0 ? foundation::WakeResult::kNotified
                                    : foundation::WakeResult::kFailed;
}

foundation::WakeResult LibuvLoopBackend::waitUntil(
    foundation::OwnerToken caller,
    std::uint64_t deadline_ns) noexcept {
  if (caller != owner_) {
    return foundation::WakeResult::kWrongThread;
  }
  if (!initialized_ || closed_ || stopping_.load(std::memory_order_relaxed)) {
    return foundation::WakeResult::kStopping;
  }
  const std::uint64_t now = nowNs();
  if (deadline_ns <= now) {
    return foundation::WakeResult::kDeadline;
  }
  const std::uint64_t delta_ns = deadline_ns - now;
  std::uint64_t delay_ms = delta_ns / 1000000ULL;
  if (delta_ns % 1000000ULL != 0) {
    ++delay_ms;
  }
  if (delay_ms > std::numeric_limits<std::uint64_t>::max() - 1) {
    return foundation::WakeResult::kFailed;
  }
  deadline_fired_ = false;
  notified_.store(false, std::memory_order_relaxed);
  if (uv_timer_start(&timer_, &LibuvLoopBackend::onDeadline, delay_ms, 0) !=
      0) {
    return foundation::WakeResult::kFailed;
  }
  (void)uv_run(&loop_, UV_RUN_ONCE);
  (void)uv_timer_stop(&timer_);
  if (stopping_.load(std::memory_order_relaxed)) {
    return foundation::WakeResult::kStopping;
  }
  if (deadline_fired_) {
    return foundation::WakeResult::kDeadline;
  }
  return foundation::WakeResult::kNotified;
}

void LibuvLoopBackend::requestStop() noexcept {
  stopping_.store(true, std::memory_order_relaxed);
  if (initialized_ && !closed_) {
    (void)uv_async_send(&async_);
  }
}

foundation::LocalResult LibuvLoopBackend::close(
    foundation::OwnerToken caller) noexcept {
  if (caller != owner_) {
    return foundation::LocalResult::failure(
        foundation::LocalError::kWrongThread);
  }
  if (closed_) {
    return foundation::LocalResult::success();
  }
  if (!initialized_) {
    closed_ = true;
    return foundation::LocalResult::success();
  }
  if (!closing_) {
    closing_ = true;
    stopping_.store(true, std::memory_order_relaxed);
    (void)uv_timer_stop(&timer_);
    uv_close(reinterpret_cast<uv_handle_t*>(&timer_), nullptr);
    uv_close(reinterpret_cast<uv_handle_t*>(&async_), nullptr);
  }
  (void)uv_run(&loop_, UV_RUN_NOWAIT);
  const int result = uv_loop_close(&loop_);
  if (result == UV_EBUSY) {
    return foundation::LocalResult::failure(foundation::LocalError::kBusy);
  }
  if (result != 0) {
    return foundation::LocalResult::failure(
        foundation::LocalError::kBackendFailed);
  }
  closed_ = true;
  return foundation::LocalResult::success();
}

void LibuvLoopBackend::onAsync(uv_async_t* handle) noexcept {
  auto* backend = static_cast<LibuvLoopBackend*>(handle->data);
  backend->notified_.store(true, std::memory_order_relaxed);
}

void LibuvLoopBackend::onDeadline(uv_timer_t* handle) noexcept {
  auto* backend = static_cast<LibuvLoopBackend*>(handle->data);
  backend->deadline_fired_ = true;
}

}  // namespace quickapp::lvgl::backends
