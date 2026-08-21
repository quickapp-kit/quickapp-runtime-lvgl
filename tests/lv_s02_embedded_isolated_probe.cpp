#include "quickapp/js/engine/quickjs_engine_provider.h"
#include "quickapp/lvgl/backends/embedded_backends.h"

#include <cstdint>

namespace {

std::uint64_t now(void*) noexcept { return 1; }
std::uint64_t resolution(void*) noexcept { return 1; }

}  // namespace

int main() {
  quickapp::js::QuickJsEngineProvider provider;
  const auto descriptor = provider.describe();
  if (descriptor.moduleId != "engine.quickjs") {
    return 1;
  }

  quickapp::lvgl::backends::BuiltinLoopCallbacks callbacks{};
  callbacks.now_ns = &now;
  callbacks.resolution_ns = &resolution;
  quickapp::lvgl::backends::BuiltinLoopBackend loop(callbacks);
  constexpr quickapp::lvgl::foundation::OwnerToken owner{1};
  if (!loop.initialize(owner).ok() || !loop.close(owner).ok()) {
    return 2;
  }
  return 0;
}
