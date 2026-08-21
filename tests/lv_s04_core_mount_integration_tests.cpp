#include <array>
#include <cstdio>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include <lvgl.h>
#include <lvgl/drivers/sdl/lv_sdl_window.h>

#include "quickapp/core/foundation/app_runtime_factory.h"
#include "quickapp/core/foundation/counters.h"
#include "quickapp/core/foundation/observation_fakes.h"
#include "quickapp/core/package/page_ir.h"
#include "quickapp/core/render/fakes.h"
#include "quickapp/core/render/initial_render_pipeline.h"
#include "quickapp/lvgl/foundation/fakes.h"
#include "quickapp/lvgl/foundation/owner_task_queue.h"
#include "quickapp/lvgl/integration/core_mount_bridge.h"
#include "quickapp/lvgl/mount/lvgl_mount_backend.h"
#include "quickapp/lvgl/mount/mount_host.h"
#include "quickapp/lvgl/surface/lvgl_page_root_backend.h"
#include "quickapp/lvgl/surface/surface_host.h"

namespace qcore = quickapp::core;
namespace qlf = quickapp::lvgl::foundation;
namespace qlm = quickapp::lvgl::mount;
namespace qli = quickapp::lvgl::integration;
namespace qls = quickapp::lvgl::surface;

namespace {

#define CHECK(expression)                                                     \
  do {                                                                        \
    if (!(expression)) {                                                      \
      std::fprintf(stderr, "CHECK failed at %s:%d: %s\n", __FILE__, __LINE__, \
                   #expression);                                             \
      return false;                                                           \
    }                                                                         \
  } while (false)

constexpr qlf::OwnerToken kOwner{1};

qcore::RequestId request(std::string value) {
  return qcore::RequestId::parse(std::move(value)).value();
}

qcore::SurfaceId surface(std::string value) {
  return qcore::SurfaceId::parse(std::move(value)).value();
}

qcore::ComponentInstanceId component(std::string value) {
  return qcore::ComponentInstanceId::parse(std::move(value)).value();
}

class SurfaceResultRouter final
    : public qcore::CoreIngressPort<qls::SurfaceResult> {
 public:
  explicit SurfaceResultRouter(qli::CoreMountBridge& bridge) noexcept
      : bridge_(bridge) {}

  qcore::EnqueueResult post(qls::SurfaceResult&& result) noexcept override {
    return bridge_.acceptSurfaceResult(std::move(result));
  }

  void close() noexcept override { closed_ = true; }
  [[nodiscard]] bool closed() const noexcept { return closed_; }

 private:
  qli::CoreMountBridge& bridge_;
  bool closed_{false};
};

class CoreMountResults final
    : public qcore::CoreIngressPort<qcore::render::MountTransactionResult> {
 public:
  qcore::EnqueueResult post(
      qcore::render::MountTransactionResult&& result) noexcept override {
    if (coordinator_ == nullptr) {
      return qcore::EnqueueResult::failure(qcore::RuntimeError::simple(
          qcore::RuntimeErrorCode::kPlatformRejected,
          "Core coordinator is not bound"));
    }
    return coordinator_->accept(std::move(result));
  }

  void close() noexcept override { closed_ = true; }
  void bind(qcore::render::MountCoordinator& coordinator) noexcept {
    coordinator_ = &coordinator;
  }

 private:
  qcore::render::MountCoordinator* coordinator_{nullptr};
  bool closed_{false};
};

class PageFixture final {
 public:
  PageFixture() {
    auto created = factory.create();
    if (!created) return;
    identity_.emplace(std::move(created).value());
    const std::string json = R"PAGE({
      "schemaVersion":1,
      "templateId":"core-bridge-case-001",
      "rootTemplateNodeId":1,
      "nodes":[
        {"templateNodeId":1,"host":{"type":"View","props":{},"style":{
          "width":{"value":100,"unit":"percent"},
          "height":{"value":100,"unit":"percent"}}},
          "children":[{"kind":"node","templateNodeId":2},{"kind":"node","templateNodeId":3}]},
        {"templateNodeId":2,"host":{"type":"Text","props":{"text":"欢迎体验快应用开发"},"style":{
          "fontSize":40}},"children":[]},
        {"templateNodeId":3,"host":{"type":"Button","props":{"text":"跳转到详情页","enabled":true},"style":{
          "borderRadius":8,"fontSize":30}},"children":[]}
      ],"bindings":[],"blocks":[],"handlers":[]
    })PAGE";
    std::vector<std::uint8_t> bytes(json.begin(), json.end());
    auto parsed = qcore::package::parse_page_ir("/pages/Demo", bytes);
    if (!parsed) return;
    qcore::package::PageIrCache cache(1U << 20U);
    if (!cache.put(std::move(parsed).value())) return;
    auto pinned = cache.pin("/pages/Demo");
    if (pinned) page = std::move(pinned).value();
  }

  ~PageFixture() {
    identity_.reset();
    factory.stop();
    (void)factory.teardown();
  }

  [[nodiscard]] bool valid() const noexcept {
    return identity_.has_value() && static_cast<bool>(page);
  }

  qcore::AppRuntimeFactory factory;
  std::optional<qcore::AppRuntimeIdentity> identity_;
  qcore::package::PageIrHandle page;
};

class InitialResults final : public qcore::render::InitialContentResultSink {
 public:
  void complete(qcore::surface::InitialContentResult result) noexcept override {
    entries.push_back(std::move(result));
  }
  void close() noexcept override { closed = true; }
  std::vector<qcore::surface::InitialContentResult> entries;
  bool closed{false};
};

bool testCoreTypedMountToPresent() {
  PageFixture fixture;
  CHECK(fixture.valid());
  lv_init();
  lv_display_t* display = lv_sdl_window_create(320, 240);
  CHECK(display != nullptr);
  lv_display_set_default(display);

  qlf::fakes::FakeWakeup wakeup(kOwner);
  std::array<qlf::OwnerTask, 128> task_storage{};
  qlf::OwnerTaskQueue tasks(task_storage.data(), task_storage.size(), 128,
                            &wakeup);
  CHECK(tasks.bindOwner(kOwner).ok());

  qls::LvglPageRootBackend roots(lv_screen_active());
  qls::EmptySurfaceContentLifecycle content;
  qcore::testing::ManualClock clock(1000);
  qcore::testing::RecordingTraceSink traces(64);
  qcore::ObservationEmitter platform_trace(
      "run:lvgl-core-mount", qcore::TraceProducer::kPlatform, "lvgl", clock,
      1000, traces);

  CoreMountResults core_results;
  auto bridge = std::make_unique<qli::CoreMountBridge>(
      kOwner, core_results, &platform_trace);
  qli::CoreMountBridge* bridge_raw = bridge.get();
  SurfaceResultRouter surface_results(*bridge_raw);
  qls::SurfaceHostAdapter surfaces(
      tasks, kOwner, roots, content, surface_results,
      qls::simulatorSurfaceHostLimits());
  const auto surface_id = surface("srf:core-bridge");
  CHECK(surfaces.post(qls::CreateSurfaceHost{request("req:create"), surface_id,
                                             {320, 240}}));
  CHECK(tasks.pump(kOwner, 16).ok());
  CHECK(surfaces.service(kOwner, 16).error == qlf::LocalError::kNone);

  qlm::LvglMountBackend native_roots(roots);
  qlm::MountHost mounts(tasks, kOwner, surfaces, native_roots, *bridge_raw,
                        qlm::simulatorMountHostLimits());
  bridge_raw->bind(mounts, surfaces);

  qcore::RuntimeCounters counters;
  qcore::testing::RecordingTraceSink core_traces(64);
  qcore::ObservationEmitter core_trace(
      "run:core-mount", qcore::TraceProducer::kCore, "core", clock, 1000,
      core_traces);
  auto measure = std::make_unique<qcore::render::testing::FakeMeasurePort>();
  auto initial_results = std::make_unique<InitialResults>();
  InitialResults* initial_results_raw = initial_results.get();
  qcore::render::MountCoordinatorDependencies dependencies{
      &fixture.identity_->request_ids(), &counters, std::move(measure),
      std::move(bridge), std::move(initial_results), &core_trace, nullptr};
  auto coordinator_result = qcore::render::MountCoordinator::create(
      std::move(dependencies));
  CHECK(coordinator_result);
  auto coordinator = std::move(coordinator_result).value();
  core_results.bind(*coordinator);

  CHECK(coordinator->post(qcore::surface::InitialContentCommand{
      request("req:initial"), surface_id, fixture.page}));
  auto owner = component("cmp:core-bridge-page");
  CHECK(coordinator->submit(qcore::render::InitialRenderIntent{
      surface_id, request("req:j-instantiate"), std::move(owner), fixture.page,
      {}, {320, 240}, {}}));
  CHECK(mounts.service(kOwner, 32).ok());
  CHECK(surfaces.service(kOwner, 32).error == qlf::LocalError::kNone);
  CHECK(initial_results_raw->entries.size() == 1);
  CHECK(initial_results_raw->entries[0].prepared);
  CHECK(!lv_obj_is_hidden(static_cast<lv_obj_t*>(
      roots.nativeObject(qls::PageRootHandle{1}))));
  CHECK(mounts.liveObjectCount() == 3);
  CHECK(mounts.liveFontCount() == 2);
  CHECK(bridge_raw->pendingCount() == 0);

  auto* page_root = static_cast<lv_obj_t*>(
      roots.nativeObject(qls::PageRootHandle{1}));
  auto* host_root = lv_obj_get_child(page_root, 0);
  auto* title = lv_obj_get_child(host_root, 0);
  auto* button = lv_obj_get_child(host_root, 1);
  auto* button_label = lv_obj_get_child(button, 0);
  CHECK(host_root != nullptr && title != nullptr && button != nullptr &&
        button_label != nullptr);
  CHECK(!lv_obj_is_hidden(title));
  CHECK(std::string_view(lv_label_get_text(title)) ==
        "欢迎体验快应用开发");
  CHECK(std::string_view(lv_label_get_text(button_label)) == "跳转到详情页");
  const auto* title_font = lv_obj_get_style_text_font(title, LV_PART_MAIN);
  const auto* button_font =
      lv_obj_get_style_text_font(button_label, LV_PART_MAIN);
  CHECK(title_font != nullptr && button_font != nullptr &&
        title_font->line_height > button_font->line_height);
  lv_font_glyph_dsc_t glyph{};
  CHECK(lv_font_get_glyph_dsc(title_font, &glyph, 0x4e2d, 0));

  bool saw_present_requested = false;
  bool saw_present_completed = false;
  for (std::size_t index = 0; index < traces.size(); ++index) {
    auto event = traces.at(index);
    CHECK(event.has_value());
    if (!event) continue;
    saw_present_requested |=
        event->marker_name == qcore::MarkerName::kPlatformPresentRequested;
    saw_present_completed |=
        event->marker_name == qcore::MarkerName::kPlatformPresentCompleted;
  }
  CHECK(saw_present_requested && saw_present_completed);
  bool saw_mount_submitted = false;
  bool saw_mount_completed = false;
  bool saw_render_presented = false;
  for (std::size_t index = 0; index < core_traces.size(); ++index) {
    auto event = core_traces.at(index);
    CHECK(event.has_value());
    if (!event) continue;
    saw_mount_submitted |=
        event->marker_name == qcore::MarkerName::kMountTransactionSubmitted;
    saw_mount_completed |=
        event->marker_name == qcore::MarkerName::kMountTransactionCompleted;
    saw_render_presented |=
        event->marker_name == qcore::MarkerName::kRenderTransactionPresented;
  }
  CHECK(saw_mount_submitted && saw_mount_completed && saw_render_presented);

  coordinator->release_surface(surface_id);
  coordinator->close();
  CHECK(mounts.finishClose(kOwner).ok());
  CHECK(mounts.liveObjectCount() == 0);
  CHECK(mounts.liveFontCount() == 0);
  surfaces.close();
  CHECK(surfaces.finishClose(kOwner).ok());
  CHECK(surface_results.closed() == false);
  CHECK(tasks.beginStop(kOwner, qlf::StopPolicy::kCancel).ok());
  CHECK(tasks.finishStop(kOwner).ok());
  CHECK(tasks.depth() == 0);
  coordinator.reset();
  lv_deinit();
  return true;
}

}  // namespace

int main() { return testCoreTypedMountToPresent() ? 0 : 1; }
