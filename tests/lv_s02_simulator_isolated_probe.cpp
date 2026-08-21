#include "quickapp/js/engine/quickjs_engine_provider.h"
#include "quickapp/lvgl/backends/libuv_loop_backend.h"
#include "quickapp/lvgl/backends/sdl_backends.h"

#include <cstddef>

int main() {
  quickapp::js::QuickJsEngineProvider provider;
  if (provider.describe().moduleId != "engine.quickjs") {
    return 1;
  }

  constexpr quickapp::lvgl::foundation::OwnerToken owner{1};
  quickapp::lvgl::backends::LibuvLoopBackend loop;
  quickapp::lvgl::backends::SdlDisplayBackend display(owner);
  quickapp::lvgl::backends::SdlRawInputBackend input(owner, display);
  (void)input;
  if (!loop.initialize(owner).ok()) {
    return 2;
  }
  for (std::size_t attempt = 0; attempt < 8; ++attempt) {
    const auto result = loop.close(owner);
    if (result.ok()) {
      return 0;
    }
  }
  return 3;
}
