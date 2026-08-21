#pragma once

#include <atomic>
#include <cstdint>

#include "quickapp/core/foundation/observation.h"

namespace quickapp::lvgl::runtime {

enum class TraceDispatchStatus : std::uint8_t {
  kAccepted,
  kDropped,
  kClosed,
};

class TraceEndpoint {
 public:
  virtual ~TraceEndpoint() = default;
  [[nodiscard]] virtual TraceDispatchStatus tryEmit(
      const core::TraceEventView& event) noexcept = 0;
};

class LvglTraceSinkAdapter final : public core::TraceSink {
 public:
  explicit LvglTraceSinkAdapter(TraceEndpoint& endpoint) noexcept
      : endpoint_(&endpoint) {}

  void emit(const core::TraceEventView& event) noexcept override;

  [[nodiscard]] std::uint64_t acceptedCount() const noexcept;
  [[nodiscard]] std::uint64_t droppedCount() const noexcept;

 private:
  TraceEndpoint* endpoint_;
  std::atomic<std::uint64_t> accepted_{0};
  std::atomic<std::uint64_t> dropped_{0};
};

}  // namespace quickapp::lvgl::runtime
