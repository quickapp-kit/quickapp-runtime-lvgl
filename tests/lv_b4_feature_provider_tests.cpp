#include <cassert>
#include <cstdio>
#include <string>

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

Request request(ModuleId module, Method method, const RequestId& request_id,
                const SurfaceId& surface_id) {
  return {request_id, surface_id, module, method, "", std::nullopt, 0,
          std::nullopt, "", {}, std::nullopt, 0, "", std::nullopt,
          std::nullopt, std::nullopt};
}

void testProvider() {
  lv_init();
  lv_display_t* display = lv_sdl_window_create(320, 240);
  assert(display != nullptr);
  lv_display_set_default(display);

  auto request_id = RequestId::parse("req:b4-provider");
  auto cancel_id = RequestId::parse("req:b4-cancel");
  auto surface_id = SurfaceId::parse("srf:b4-provider");
  assert(request_id && cancel_id && surface_id);

  quickapp::lvgl::feature::LvglFeatureProvider provider(lv_screen_active());
  quickapp::core::feature::ModuleRegistry registry;
  assert(registry.register_provider(ModuleId::kSystemPrompt, provider));
  assert(registry.register_provider(ModuleId::kSystemFetch, provider));
  assert(registry.register_provider(ModuleId::kSystemFile, provider));

  auto alert = request(ModuleId::kSystemPrompt, Method::kAlert,
                       request_id.value(), surface_id.value());
  alert.text = "hello";
  assert(registry.invoke(alert).status == Status::kSuccess);
  assert(provider.live_resource_count() == 1);

  auto confirm = request(ModuleId::kSystemPrompt, Method::kConfirm,
                         request_id.value(), surface_id.value());
  confirm.text = "continue";
  const auto confirmed = registry.invoke(confirm);
  assert(confirmed.status == Status::kSuccess && confirmed.confirmed == true);

  auto prompt_failed = alert;
  prompt_failed.text = "__failed__";
  assert(registry.invoke(prompt_failed).status == Status::kFailed);
  auto prompt_unsupported = alert;
  prompt_unsupported.text = "__unsupported__";
  assert(registry.invoke(prompt_unsupported).status == Status::kUnsupported);
  auto prompt_cancelled = alert;
  prompt_cancelled.text = "__cancelled__";
  assert(registry.invoke(prompt_cancelled).status == Status::kCancelled);

  auto fetch = request(ModuleId::kSystemFetch, Method::kFetch,
                       request_id.value(), surface_id.value());
  fetch.url = "https://example.test/data";
  fetch.http_method = "GET";
  fetch.response_type = "json";
  const auto fetched = registry.invoke(fetch);
  assert(fetched.status == Status::kSuccess);
  assert(fetched.http_status == 200);
  assert(fetched.response_body == "{\"ok\":true}");
  assert(fetched.response_is_json == true);

  provider.setFetchFailure("https://example.test/fail", "NETWORK_FAILED",
                           "deterministic network failure");
  auto fetch_failed = fetch;
  fetch_failed.url = "https://example.test/fail";
  assert(registry.invoke(fetch_failed).status == Status::kFailed);
  provider.setFetchUnsupported("https://example.test/unsupported");
  auto fetch_unsupported = fetch;
  fetch_unsupported.url = "https://example.test/unsupported";
  assert(registry.invoke(fetch_unsupported).status == Status::kUnsupported);
  auto fetch_method_failed = fetch;
  fetch_method_failed.http_method = "POST";
  assert(registry.invoke(fetch_method_failed).status == Status::kFailed);

  provider.markPendingFetch(cancel_id.value(), surface_id.value());
  auto fetch_cancel = request(ModuleId::kSystemFetch, Method::kFetchCancel,
                              request_id.value(), surface_id.value());
  fetch_cancel.cancel_request_id = cancel_id.value();
  assert(registry.invoke(fetch_cancel).status == Status::kCancelled);
  assert(registry.cancel(cancel_id.value(), surface_id.value()).status ==
         Status::kUnsupported);

  auto write = request(ModuleId::kSystemFile, Method::kFileWrite,
                       request_id.value(), surface_id.value());
  write.path = "private/state.txt";
  write.data = "ready";
  assert(registry.invoke(write).status == Status::kSuccess);
  auto read = request(ModuleId::kSystemFile, Method::kFileRead,
                      request_id.value(), surface_id.value());
  read.path = write.path;
  assert(registry.invoke(read).file_data == "ready");
  auto exists = request(ModuleId::kSystemFile, Method::kFileExists,
                        request_id.value(), surface_id.value());
  exists.path = write.path;
  assert(registry.invoke(exists).file_exists == true);
  auto remove = request(ModuleId::kSystemFile, Method::kFileDelete,
                        request_id.value(), surface_id.value());
  remove.path = write.path;
  assert(registry.invoke(remove).status == Status::kSuccess);
  assert(registry.invoke(exists).file_exists == false);

  auto traversal = write;
  traversal.path = "private/../outside.txt";
  assert(registry.invoke(traversal).status == Status::kFailed);
  auto missing = read;
  missing.path = "private/missing.txt";
  assert(registry.invoke(missing).status == Status::kFailed);
  auto file_unsupported = write;
  file_unsupported.method = Method::kShowToast;
  assert(registry.invoke(file_unsupported).status == Status::kUnsupported);

  provider.putPrivateFile(surface_id.value(), "private/teardown.txt", "x");
  provider.markPendingFetch(cancel_id.value(), surface_id.value());
  registry.teardown(surface_id.value());
  assert(provider.live_resource_count() == 0);
  assert(registry.cancel(cancel_id.value(), surface_id.value()).status ==
         Status::kUnsupported);
  registry.close();
  assert(registry.invoke(alert).status == Status::kFailed);

  lv_deinit();
}

}  // namespace

int main() {
  testProvider();
  std::puts("LVGL B4 feature provider tests passed");
  return 0;
}
