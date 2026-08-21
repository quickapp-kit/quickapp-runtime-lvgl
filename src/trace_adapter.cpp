#include "quickapp/lvgl/runtime/trace_adapter.h"

namespace quickapp::lvgl::runtime {

void LvglTraceSinkAdapter::emit(const core::TraceEventView& event) noexcept {
  const TraceDispatchStatus status = endpoint_->tryEmit(event);
  if (status == TraceDispatchStatus::kAccepted) {
    accepted_.fetch_add(1, std::memory_order_relaxed);
  } else {
    dropped_.fetch_add(1, std::memory_order_relaxed);
  }
}

std::uint64_t LvglTraceSinkAdapter::acceptedCount() const noexcept {
  return accepted_.load(std::memory_order_relaxed);
}

std::uint64_t LvglTraceSinkAdapter::droppedCount() const noexcept {
  return dropped_.load(std::memory_order_relaxed);
}

}  // namespace quickapp::lvgl::runtime
