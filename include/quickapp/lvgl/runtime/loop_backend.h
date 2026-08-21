#pragma once

#include "quickapp/lvgl/foundation/ports.h"

namespace quickapp::lvgl::runtime {

class OwnerLoopBackend : public foundation::BackendClock,
                         public foundation::WakeupPort {
 public:
  ~OwnerLoopBackend() override = default;
  virtual foundation::LocalResult initialize(
      foundation::OwnerToken owner) noexcept = 0;
  virtual foundation::LocalResult serviceOneTurn(
      foundation::OwnerToken caller,
      std::size_t max_callbacks) noexcept = 0;
};

}  // namespace quickapp::lvgl::runtime
