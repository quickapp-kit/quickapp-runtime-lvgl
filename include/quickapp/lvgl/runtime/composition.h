#pragma once

#include "quickapp/js/engine/js_engine_port.h"
#include "quickapp/lvgl/runtime/runtime_types.h"

namespace quickapp::lvgl::runtime {

class CompositionRoot final {
 public:
  [[nodiscard]] static CompositionValidation validateIsolated(
      ProfileId profile_id, const BuildInventoryView& inventory,
      const js::JsEngineProvider& engine_provider) noexcept;
};

}  // namespace quickapp::lvgl::runtime
