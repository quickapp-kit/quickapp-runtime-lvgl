#pragma once

#include <cstddef>
#include <cstdint>

#include "quickapp/lvgl/foundation/types.h"

namespace quickapp::lvgl::foundation {

class BackendClock {
 public:
  virtual ~BackendClock() = default;
  [[nodiscard]] virtual std::uint64_t nowNs() const noexcept = 0;
  [[nodiscard]] virtual std::uint64_t resolutionNs() const noexcept = 0;
};

enum class WakeResult : std::uint8_t {
  kNotified,
  kDeadline,
  kStopping,
  kUnsupported,
  kFailed,
  kWrongThread,
};

class WakeupPort {
 public:
  virtual ~WakeupPort() = default;
  [[nodiscard]] virtual WakeResult notify() noexcept = 0;
  [[nodiscard]] virtual WakeResult waitUntil(
      OwnerToken caller, std::uint64_t deadline_ns) noexcept = 0;
  virtual void requestStop() noexcept = 0;
  virtual LocalResult close(OwnerToken caller) noexcept = 0;
};

class DisplayBackend {
 public:
  virtual ~DisplayBackend() = default;
  virtual LocalResult open(OwnerToken caller, const DisplayConfig& config,
                           DisplayCapabilities& capabilities) noexcept = 0;
  virtual LocalResult present(OwnerToken caller,
                              const DisplayFrameView& frame) noexcept = 0;
  virtual LocalResult close(OwnerToken caller) noexcept = 0;
  [[nodiscard]] virtual LifecycleState state() const noexcept = 0;
};

class InputBackend {
 public:
  virtual ~InputBackend() = default;
  virtual LocalResult open(OwnerToken caller, const InputConfig& config,
                           InputCapabilities& capabilities) noexcept = 0;
  virtual LocalResult beginStop(OwnerToken caller) noexcept = 0;
  virtual DrainResult drain(OwnerToken caller, RawInputSample* output,
                            std::size_t capacity) noexcept = 0;
  virtual DrainResult discardPending(OwnerToken caller) noexcept = 0;
  virtual LocalResult close(OwnerToken caller) noexcept = 0;
  [[nodiscard]] virtual LifecycleState state() const noexcept = 0;
};

}  // namespace quickapp::lvgl::foundation
