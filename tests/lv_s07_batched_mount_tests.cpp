#include <array>
#include <cstdio>
#include <string>
#include <utility>
#include <vector>

#include <lvgl.h>
#include <lvgl/drivers/sdl/lv_sdl_window.h>

#include "quickapp/core/foundation/port.h"
#include "quickapp/core/render/initial_render_pipeline.h"
#include "quickapp/lvgl/foundation/fakes.h"
#include "quickapp/lvgl/foundation/owner_task_queue.h"
#include "quickapp/lvgl/integration/core_mount_bridge.h"
#include "quickapp/lvgl/mount/lvgl_mount_backend.h"
#include "quickapp/lvgl/mount/mount_host.h"
#include "quickapp/lvgl/surface/lvgl_page_root_backend.h"
#include "quickapp/lvgl/surface/surface_host.h"

namespace qc = quickapp::core;
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

qc::RequestId request(std::string value) {
  return qc::RequestId::parse(std::move(value)).value();
}

qc::SurfaceId surface(std::string value) {
  return qc::SurfaceId::parse(std::move(value)).value();
}

qc::NodeId node(std::string value) {
  return qc::NodeId::parse(std::move(value)).value();
}

qc::MountAttemptId attempt(std::string value) {
  return qc::MountAttemptId::parse(std::move(value)).value();
}

enum class FailurePlacement { kNone, kFirst, kMiddle, kFinal };

// Keep the Surface result path typed while routing Present back to the bridge.
class SurfaceRouter final : public qc::CoreIngressPort<qls::SurfaceResult> {
 public:
  explicit SurfaceRouter(qli::CoreMountBridge& bridge) noexcept
      : bridge_(bridge) {}
  qc::EnqueueResult post(qls::SurfaceResult&& result) noexcept override {
    return bridge_.acceptSurfaceResult(std::move(result));
  }
  void close() noexcept override {}

 private:
  qli::CoreMountBridge& bridge_;
};

class CoreResults final
    : public qc::CoreIngressPort<qc::render::MountTransactionResult> {
 public:
  qc::EnqueueResult post(
      qc::render::MountTransactionResult&& result) noexcept override {
    results.push_back(std::move(result));
    return qc::EnqueueResult::success(qc::Accepted{});
  }
  void close() noexcept override {}
  std::vector<qc::render::MountTransactionResult> results;
};

qc::render::MountTransaction makeTransaction(const qc::SurfaceId& surface_id,
                                              FailurePlacement failure) {
  qc::render::MountTransaction transaction{
      surface_id, 0, attempt("mnt:long-list"), request("req:long-list"),
      qc::render::MountMode::kFull, {}};
  auto& ops = transaction.operations;
  const auto root = node("node:long-root");
  ops.emplace_back(qc::render::CreateHost{
      root, qc::package::HostComponentType::kView});
  ops.emplace_back(qc::render::SetHostLayout{root, {0, 0, 320, 640}});
  for (std::size_t index = 0; index < 20; ++index) {
    const auto suffix = std::to_string(index);
    const auto item = node("node:item-" + suffix);
    const auto title = node("node:title-" + suffix);
    const auto status = node("node:status-" + suffix);
    const auto y = static_cast<double>(index * 76);
    ops.emplace_back(qc::render::CreateHost{
        item, qc::package::HostComponentType::kView});
    ops.emplace_back(qc::render::SetHostProp{
        item, "backgroundColor", std::string("#ffffff")});
    ops.emplace_back(qc::render::SetHostProp{
        item, "borderRadius", 6.0});
    ops.emplace_back(qc::render::SetHostLayout{item, {8, y, 304, 70}});
    ops.emplace_back(qc::render::CreateHost{
        title, qc::package::HostComponentType::kText});
    ops.emplace_back(qc::render::SetHostProp{
        title, "text", std::string("任务 ") + suffix});
    ops.emplace_back(qc::render::SetHostProp{
        title, "color", std::string("#111111")});
    ops.emplace_back(qc::render::SetHostProp{title, "fontSize", 14.0});
    ops.emplace_back(qc::render::SetHostLayout{title, {18, y + 12, 130, 24}});
    ops.emplace_back(qc::render::CreateHost{
        status, qc::package::HostComponentType::kText});
    ops.emplace_back(qc::render::SetHostProp{
        status, "text", std::string("待处理")});
    ops.emplace_back(qc::render::SetHostProp{
        status, "color", std::string("#2774c7")});
    ops.emplace_back(qc::render::SetHostProp{status, "fontSize", 12.0});
    ops.emplace_back(qc::render::SetHostLayout{status, {160, y + 12, 110, 24}});
  }
  for (std::size_t index = 0; index < 20; ++index) {
    const auto suffix = std::to_string(index);
    const auto item = node("node:item-" + suffix);
    const auto title = node("node:title-" + suffix);
    const auto status = node("node:status-" + suffix);
    ops.emplace_back(qc::render::InsertHostChild{item, root, index});
    ops.emplace_back(qc::render::InsertHostChild{title, item, 0});
    ops.emplace_back(qc::render::InsertHostChild{status, item, 1});
  }
  const qc::render::InsertHostChild invalid{
      node("node:invalid-child"), node("node:missing-parent"), 0};
  if (failure == FailurePlacement::kFirst) {
    ops.insert(ops.begin(), invalid);
  } else if (failure == FailurePlacement::kMiddle) {
    ops.insert(ops.begin() + 280, invalid);
  } else if (failure == FailurePlacement::kFinal) {
    ops.emplace_back(invalid);
  }
  return transaction;
}

bool run(FailurePlacement failure) {
  lv_init();
  auto* display = lv_sdl_window_create(320, 640);
  CHECK(display != nullptr);
  lv_display_set_default(display);
  qlf::fakes::FakeWakeup wakeup(kOwner);
  std::array<qlf::OwnerTask, 128> tasks_storage{};
  qlf::OwnerTaskQueue tasks(tasks_storage.data(), tasks_storage.size(), 128,
                            &wakeup);
  CHECK(tasks.bindOwner(kOwner).ok());
  qls::LvglPageRootBackend roots(lv_screen_active());
  qls::EmptySurfaceContentLifecycle content;
  CoreResults core_results;
  auto bridge = std::make_unique<qli::CoreMountBridge>(kOwner, core_results);
  auto* bridge_raw = bridge.get();
  SurfaceRouter surface_results(*bridge_raw);
  qls::SurfaceHostAdapter surfaces(
      tasks, kOwner, roots, content, surface_results,
      qls::simulatorSurfaceHostLimits());
  const auto surface_id = surface(failure != FailurePlacement::kNone
                                       ? "srf:long-list-failure"
                                       : "srf:long-list-success");
  CHECK(surfaces.post(qls::CreateSurfaceHost{
      request("req:long-surface"), surface_id, {320, 640}}));
  CHECK(tasks.pump(kOwner, 16).ok());
  CHECK(surfaces.service(kOwner, 16).error == qlf::LocalError::kNone);
  qlm::LvglMountBackend backend(roots);
  qlm::MountHost mounts(tasks, kOwner, surfaces, backend, *bridge_raw,
                        qlm::simulatorMountHostLimits());
  bridge_raw->bind(mounts, surfaces);
  const auto admitted = bridge_raw->post(makeTransaction(surface_id, failure));
  if (!admitted) {
    std::fprintf(stderr, "batch admission error=%s message=%s retryable=%d\n",
                 qc::to_wire(admitted.error().code).data(),
                 std::string(admitted.error().message).c_str(),
                 admitted.error().retryable ? 1 : 0);
  }
  CHECK(admitted);
  for (std::size_t turn = 0; turn < 16 && core_results.results.empty(); ++turn) {
    CHECK(mounts.service(kOwner, 128).ok());
    CHECK(surfaces.service(kOwner, 128).error == qlf::LocalError::kNone);
    CHECK(bridge_raw->service(kOwner, 128).ok());
  }
  CHECK(core_results.results.size() == 1);
  if (failure != FailurePlacement::kNone) {
    CHECK(!core_results.results.front().mounted);
    CHECK(mounts.liveObjectCount() == 0);
    CHECK(mounts.liveFontCount() == 0);
  } else {
    CHECK(core_results.results.front().mounted);
    CHECK(core_results.results.front().error == std::nullopt);
    CHECK(mounts.liveObjectCount() == 61);
    auto* root = static_cast<lv_obj_t*>(roots.nativeObject(
        qls::PageRootHandle{1}));
    CHECK(root != nullptr && !lv_obj_is_hidden(root));
  }
  bridge_raw->close();
  CHECK(mounts.finishClose(kOwner).ok());
  CHECK(mounts.liveObjectCount() == 0);
  CHECK(mounts.liveFontCount() == 0);
  surfaces.close();
  CHECK(surfaces.finishClose(kOwner).ok());
  CHECK(tasks.beginStop(kOwner, qlf::StopPolicy::kCancel).ok());
  CHECK(tasks.finishStop(kOwner).ok());
  CHECK(tasks.depth() == 0);
  bridge.reset();
  lv_deinit();
  return true;
}

}  // namespace

int main() {
  return run(FailurePlacement::kNone) &&
                 run(FailurePlacement::kFirst) &&
                 run(FailurePlacement::kMiddle) &&
                 run(FailurePlacement::kFinal)
             ? 0
             : 1;
}
