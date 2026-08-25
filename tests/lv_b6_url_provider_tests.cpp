#include <cassert>
#include <cstdio>

#include <lvgl.h>
#include <lvgl/drivers/sdl/lv_sdl_window.h>

#include "quickapp/core/feature/module_registry.h"
#include "quickapp/lvgl/feature/lvgl_feature_provider.h"

namespace {

using quickapp::core::RequestId;
using quickapp::core::SurfaceId;
using quickapp::core::feature::Method;
using quickapp::core::feature::ModuleId;
using quickapp::core::feature::Request;
using quickapp::core::feature::Status;

Request url_shaped_request(const RequestId& request_id,
                           const SurfaceId& surface_id) {
  Request request{request_id,
                  surface_id,
                  ModuleId::kPageHost,
                  Method::kShowToast,
                  "",
                  std::nullopt,
                  0,
                  std::nullopt,
                  "",
                  {},
                  std::nullopt,
                  0,
                  "",
                  std::nullopt,
                  std::nullopt,
                  std::nullopt};
  request.url = "https://example.invalid/external-page";
  return request;
}

void test_lvgl_rejects_external_url_without_web_host() {
  lv_init();
  auto* display = lv_sdl_window_create(320, 240);
  assert(display != nullptr);
  lv_display_set_default(display);

  const auto request_id = RequestId::parse("req:b6-url");
  const auto surface_id = SurfaceId::parse("srf:b6-url");
  assert(request_id.has_value() && surface_id.has_value());

  quickapp::lvgl::feature::LvglFeatureProvider provider(lv_screen_active());
  quickapp::core::feature::ModuleRegistry registry;
  assert(registry.register_provider(ModuleId::kPageHost, provider));

  const auto result = registry.invoke(
      url_shaped_request(request_id.value(), surface_id.value()));
  assert(result.status == Status::kUnsupported);
  assert(result.error.has_value());
  assert(result.error->code == "CAPABILITY_UNSUPPORTED");
  assert(provider.live_resource_count() == 0);

  registry.teardown(surface_id.value());
  assert(provider.live_resource_count() == 0);
  lv_deinit();
}

}  // namespace

int main() {
  test_lvgl_rejects_external_url_without_web_host();
  std::puts("LVGL B6 URL provider tests passed");
  return 0;
}
