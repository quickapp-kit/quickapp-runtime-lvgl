#include <array>
#include <cstdio>
#include <string>
#include <vector>

#include <lvgl.h>
#include <lvgl/drivers/sdl/lv_sdl_window.h>

#include "quickapp/core/foundation/port.h"
#include "quickapp/lvgl/foundation/fakes.h"
#include "quickapp/lvgl/foundation/owner_task_queue.h"
#include "quickapp/lvgl/mount/lvgl_mount_backend.h"
#include "quickapp/lvgl/mount/mount_host.h"
#include "quickapp/lvgl/surface/lvgl_page_root_backend.h"
#include "quickapp/lvgl/surface/surface_host.h"

namespace qcore = quickapp::core;
namespace qlf = quickapp::lvgl::foundation;
namespace qfake = quickapp::lvgl::foundation::fakes;
namespace qlm = quickapp::lvgl::mount;
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
qcore::NodeId node(std::string value) {
  return qcore::NodeId::parse(std::move(value)).value();
}
qcore::MountAttemptId attempt(std::string value) {
  return qcore::MountAttemptId::parse(std::move(value)).value();
}

class SurfaceResults final : public qcore::CoreIngressPort<qls::SurfaceResult> {
 public:
  qcore::EnqueueResult post(qls::SurfaceResult&& result) noexcept override {
    results.push_back(std::move(result));
    return qcore::EnqueueResult::success(qcore::Accepted{});
  }
  void close() noexcept override {}
  std::vector<qls::SurfaceResult> results;
};

class MountResults final : public qlm::MountResultSink {
 public:
  void complete(qlm::MountResult result) noexcept override {
    results.push_back(std::move(result));
  }
  [[nodiscard]] std::size_t size() const noexcept { return results.size(); }
  [[nodiscard]] const qlm::MountResult& operator[](std::size_t index) const noexcept {
    return results[index];
  }
  std::vector<qlm::MountResult> results;
};

struct TabsEvents final {
  std::vector<std::int32_t> indexes;
  std::vector<std::string> values;
  static void callback(void* context, const qcore::SurfaceId&,
                       const qcore::NodeId&, std::int32_t index,
                       const char* value, std::uint64_t) noexcept {
    auto* self = static_cast<TabsEvents*>(context);
    if (self == nullptr) return;
    self->indexes.push_back(index);
    self->values.emplace_back(value == nullptr ? "" : value);
  }
};

bool testTabsLifecycle() {
  lv_init();
  auto* display = lv_sdl_window_create(320, 240);
  CHECK(display != nullptr);
  lv_display_set_default(display);

  qfake::FakeWakeup wakeup(kOwner);
  std::array<qlf::OwnerTask, 64> task_storage{};
  qlf::OwnerTaskQueue tasks(task_storage.data(), task_storage.size(), 64,
                            &wakeup);
  CHECK(tasks.bindOwner(kOwner).ok());
  qls::LvglPageRootBackend roots(lv_screen_active());
  qls::EmptySurfaceContentLifecycle content;
  SurfaceResults surface_results;
  qls::SurfaceHostAdapter surfaces(
      tasks, kOwner, roots, content, surface_results,
      qls::simulatorSurfaceHostLimits());
  const auto surface_id = surface("srf:tabs-001");
  CHECK(surfaces.post(qls::CreateSurfaceHost{
      request("req:tabs-create"), surface_id, {320, 240}}));
  CHECK(tasks.pump(kOwner, 16).ok());
  CHECK(surfaces.service(kOwner, 16).error == qlf::LocalError::kNone);

  qlm::LvglMountBackend backend(roots);
  MountResults mount_results;
  qlm::MountHost mount(tasks, kOwner, surfaces, backend, mount_results,
                       qlm::simulatorMountHostLimits());
  const auto root = node("node:tabs-root");
  const auto tabs = node("node:tabs-control");
  qlm::MountTransaction transaction(
      surface_id, 0, attempt("mnt:tabs-001"), qlm::BoundedText::from("tabs-001"),
      qlm::MountMode::kFull);
  transaction.operations[0] =
      qlm::CreateHost{root, qcore::package::HostComponentType::kView};
  transaction.operations[1] =
      qlm::CreateHost{tabs, qcore::package::HostComponentType::kTabs};
  transaction.operations[2] =
      qlm::SetHostProp{tabs, qlm::BoundedText::from("items"),
                       qlm::BoundedText::from("首页|任务|我的")};
  transaction.operations[3] =
      qlm::SetHostProp{tabs, qlm::BoundedText::from("selected"), 0.0};
  transaction.operations[4] = qlm::SetHostLayout{root, {0, 0, 320, 240}};
  transaction.operations[5] = qlm::SetHostLayout{tabs, {12, 20, 296, 56}};
  transaction.operations[6] = qlm::InsertHostChild{tabs, root, 0};
  transaction.operation_count = 7;
  CHECK(mount.post(std::move(transaction)));
  CHECK(mount.service(kOwner, 32).ok());
  CHECK(mount_results.size() == 1);
  CHECK(mount_results[0].status == qlm::MountResultStatus::kMounted);
  CHECK(mount.liveObjectCount() == 2);

  auto* native = static_cast<lv_obj_t*>(mount.nativeObject(surface_id, tabs));
  CHECK(native != nullptr);
  CHECK(lv_tabview_get_tab_count(native) == 3);
  CHECK(lv_tabview_get_tab_active(native) == 0);
  const auto native_identity = native;

  TabsEvents events;
  CHECK(mount.installTabsHandler(surface_id, tabs, &TabsEvents::callback,
                                 &events));
  lv_tabview_set_active(native, 1, LV_ANIM_OFF);
  lv_obj_send_event(native, LV_EVENT_VALUE_CHANGED, nullptr);
  CHECK(lv_tabview_get_tab_active(native) == 1);
  CHECK(events.indexes.size() == 1);
  CHECK(events.indexes.front() == 1);
  CHECK(events.values.front() == "任务");

  qlm::MountTransaction controlled(
      surface_id, 1, attempt("mnt:tabs-001-update"),
      qlm::BoundedText::from("tabs-001-update"), qlm::MountMode::kIncremental);
  controlled.operations[0] =
      qlm::SetHostProp{tabs, qlm::BoundedText::from("selected"), 2.0};
  controlled.operation_count = 1;
  CHECK(mount.post(std::move(controlled)));
  CHECK(mount.service(kOwner, 16).ok());
  CHECK(mount_results.size() == 2);
  CHECK(mount_results[1].status == qlm::MountResultStatus::kMounted);
  CHECK(lv_tabview_get_tab_active(native) == 2);
  CHECK(mount.nativeObject(surface_id, tabs) == native_identity);

  CHECK(mount.releaseSurface(kOwner, surface_id).ok());
  CHECK(mount.liveObjectCount() == 0);
  CHECK(mount.liveFontCount() == 0);
  CHECK(!mount.installTabsHandler(surface_id, tabs, &TabsEvents::callback,
                                  &events));
  mount.close();
  CHECK(mount.finishClose(kOwner).ok());
  surfaces.close();
  CHECK(surfaces.finishClose(kOwner).ok());
  CHECK(tasks.beginStop(kOwner, qlf::StopPolicy::kCancel).ok());
  CHECK(tasks.finishStop(kOwner).ok());
  lv_deinit();
  return true;
}

}  // namespace

int main() {
  if (!testTabsLifecycle()) return 1;
  std::puts("LVGL B3.5 Tabs mount tests passed");
  return 0;
}
