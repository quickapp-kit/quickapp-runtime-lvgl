#include <array>
#include <cstdio>
#include <cmath>
#include <string>
#include <utility>
#include <variant>
#include <vector>

#include <lvgl.h>
#include <lvgl/drivers/sdl/lv_sdl_window.h>

#include "quickapp/core/foundation/port.h"
#include "quickapp/lvgl/font/system_default_font_asset.h"
#include "quickapp/lvgl/foundation/fakes.h"
#include "quickapp/lvgl/foundation/owner_task_queue.h"
#include "quickapp/lvgl/measure/font_measure.h"
#include "quickapp/lvgl/mount/lvgl_mount_backend.h"
#include "quickapp/lvgl/mount/mount_host.h"
#include "quickapp/lvgl/surface/lvgl_page_root_backend.h"
#include "quickapp/lvgl/surface/surface_host.h"

namespace qcore = quickapp::core;
namespace qlf = quickapp::lvgl::foundation;
namespace qfake = quickapp::lvgl::foundation::fakes;
namespace qlm = quickapp::lvgl::mount;
namespace qmeasure = quickapp::lvgl::measure;
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

qcore::MountAttemptId mountAttempt(std::string value) {
  return qcore::MountAttemptId::parse(std::move(value)).value();
}

class SurfaceResults final
    : public qcore::CoreIngressPort<qls::SurfaceResult> {
 public:
  qcore::EnqueueResult post(qls::SurfaceResult&& result) noexcept override {
    results_.push_back(std::move(result));
    return qcore::EnqueueResult::success(qcore::Accepted{});
  }
  void close() noexcept override { closed_ = true; }
  [[nodiscard]] std::size_t size() const noexcept { return results_.size(); }
  [[nodiscard]] bool closed() const noexcept { return closed_; }
  [[nodiscard]] std::vector<qls::SurfaceResult> take() noexcept {
    std::vector<qls::SurfaceResult> result;
    result.swap(results_);
    return result;
  }

 private:
  std::vector<qls::SurfaceResult> results_;
  bool closed_{false};
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

class FontGenerationResults final
    : public qcore::CoreIngressPort<qmeasure::PlatformFontGenerationChanged> {
 public:
  qcore::EnqueueResult post(
      qmeasure::PlatformFontGenerationChanged&&) noexcept override {
    return qcore::EnqueueResult::success(qcore::Accepted{});
  }
  void close() noexcept override {}
};

bool testCase001VisibleAndResources() {
  lv_init();
  lv_display_t* display = lv_sdl_window_create(320, 240);
  CHECK(display != nullptr);
  lv_display_set_default(display);

  qfake::FakeWakeup wakeup(kOwner);
  std::array<qlf::OwnerTask, 128> task_storage{};
  qlf::OwnerTaskQueue tasks(task_storage.data(), task_storage.size(), 128,
                            &wakeup);
  CHECK(tasks.bindOwner(kOwner).ok());

  qls::LvglPageRootBackend page_roots(lv_screen_active());
  qls::EmptySurfaceContentLifecycle content;
  SurfaceResults surface_results;
  qls::SurfaceHostAdapter surfaces(
      tasks, kOwner, page_roots, content, surface_results,
      qls::simulatorSurfaceHostLimits());
  const auto surface_id = surface("srf:case001");
  CHECK(surfaces.post(qls::CreateSurfaceHost{request("req:create"), surface_id,
                                             {320, 240}}));
  CHECK(tasks.pump(kOwner, 16).ok());
  CHECK(surfaces.service(kOwner, 16).error == qlf::LocalError::kNone);
  CHECK(surface_results.size() == 1);

  qlm::LvglMountBackend lvgl_backend(page_roots);
  MountResults mount_results;
  qlm::MountHost mount(tasks, kOwner, surfaces, lvgl_backend, mount_results,
                       qlm::simulatorMountHostLimits());

  qlm::MountTransaction transaction(
      surface_id, 0, mountAttempt("mnt:case001"),
      qlm::BoundedText::from("req:instantiate"), qlm::MountMode::kFull);
  const auto root = node("node:root");
  const auto title = node("node:title");
  const auto button = node("node:button");
  transaction.operations[0] = qlm::CreateHost{root, qcore::package::HostComponentType::kView};
  transaction.operations[1] = qlm::SetHostLayout{root, {0, 0, 320, 240}};
  transaction.operations[2] = qlm::CreateHost{title, qcore::package::HostComponentType::kText};
  transaction.operations[3] = qlm::SetHostProp{title, qlm::BoundedText::from("text"),
                                               qlm::BoundedText::from("欢迎体验快应用开发")};
  transaction.operations[4] = qlm::SetHostProp{
      title, qlm::BoundedText::from("fontSize"), std::int32_t{40}};
  transaction.operations[5] = qlm::SetHostLayout{title, {16, 16, 288, 50}};
  transaction.operations[6] = qlm::CreateHost{button, qcore::package::HostComponentType::kButton};
  transaction.operations[7] = qlm::SetHostProp{button, qlm::BoundedText::from("text"),
                                               qlm::BoundedText::from("跳转到详情页")};
  transaction.operations[8] = qlm::SetHostProp{
      button, qlm::BoundedText::from("fontSize"), std::int32_t{30}};
  transaction.operations[9] = qlm::SetHostProp{button, qlm::BoundedText::from("enabled"), true};
  transaction.operations[10] = qlm::SetHostProp{
      button, qlm::BoundedText::from("backgroundColor"),
      qlm::BoundedText::from("#123456")};
  transaction.operations[11] = qlm::SetHostProp{
      button, qlm::BoundedText::from("color"), qlm::BoundedText::from("#ffffff")};
  transaction.operations[12] = qlm::SetHostProp{
      button, qlm::BoundedText::from("borderRadius"), std::int32_t{4}};
  transaction.operations[13] = qlm::SetHostProp{
      button, qlm::BoundedText::from("textAlign"), qlm::BoundedText::from("center")};
  transaction.operations[14] = qlm::SetHostLayout{button, {16, 80, 288, 60}};
  transaction.operations[15] = qlm::InsertHostChild{title, root, 0};
  transaction.operations[16] = qlm::InsertHostChild{button, root, 1};
  transaction.operation_count = 17;

  CHECK(mount.post(std::move(transaction)));
  CHECK(mount.service(kOwner, 16).ok());
  CHECK(mount_results.size() == 1);
  CHECK(mount_results[0].status == qlm::MountResultStatus::kMounted);
  CHECK(mount_results[0].source_id.view() == "req:instantiate");
  CHECK(mount_results[0].live_objects == 4);  // 3 Runtime nodes + private label.
  CHECK(mount.liveObjectCount() == 3);
  CHECK(mount.liveFontCount() == 2);

  CHECK(surfaces.markFullMountCommitted(kOwner, surface_id).ok());
  CHECK(surfaces.post(qls::PresentRootSurfaceHost{request("req:present"),
                                                   surface_id}));
  CHECK(tasks.pump(kOwner, 16).ok());
  CHECK(surfaces.service(kOwner, 16).error == qlf::LocalError::kNone);
  CHECK(surface_results.size() == 2);
  auto surface_results_copy = surface_results.take();
  CHECK(std::holds_alternative<qls::PresentRootSurfaceHostResult>(
      surface_results_copy.back()));

  const auto root_handle = page_roots.nativeObject(qls::PageRootHandle{1});
  CHECK(root_handle != nullptr);
  CHECK(!lv_obj_is_hidden(static_cast<lv_obj_t*>(root_handle)));
  auto* host_root = lv_obj_get_child(static_cast<lv_obj_t*>(root_handle), 0);
  auto* title_object = lv_obj_get_child(host_root, 0);
  auto* button_object = lv_obj_get_child(host_root, 1);
  auto* button_label = lv_obj_get_child(button_object, 0);
  CHECK(host_root != nullptr && title_object != nullptr &&
        button_object != nullptr && button_label != nullptr);
  CHECK(!lv_obj_is_hidden(title_object));
  CHECK(std::string_view(lv_label_get_text(title_object)) ==
        "欢迎体验快应用开发");
  CHECK(std::string_view(lv_label_get_text(button_label)) == "跳转到详情页");
  const lv_font_t* title_font =
      lv_obj_get_style_text_font(title_object, LV_PART_MAIN);
  const lv_font_t* button_font =
      lv_obj_get_style_text_font(button_label, LV_PART_MAIN);
  CHECK(title_font != nullptr && button_font != nullptr);
  CHECK(title_font->line_height > button_font->line_height);
  lv_font_glyph_dsc_t cjk_glyph{};
  CHECK(lv_font_get_glyph_dsc(title_font, &cjk_glyph, 0x4e2d, 0));

  FontGenerationResults generation_results;
  qmeasure::FontSnapshotPublisher font_publisher(generation_results);
  CHECK(font_publisher
            .initialize(kOwner, qmeasure::FontMetricsSnapshot::makeV1(1))
            .ok());
  qmeasure::FontMeasureAdapter measure(font_publisher);
  const qmeasure::MeasureRequest measure_request{
      "req:cjk", "srf:case001", "node:title", 1, 1,
      qmeasure::MeasureRole::kText, "中", "system-default", 40, 400,
      {qmeasure::ConstraintKind::kUnconstrained, 0},
      {qmeasure::ConstraintKind::kUnconstrained, 0}};
  const auto measured = measure.measure(measure_request);
  CHECK(measured.measured);
  CHECK(std::abs(measured.width - cjk_glyph.adv_w) < 0.01);
  CHECK(std::abs(measured.height - title_font->line_height) < 1.0);
  const auto font_snapshot = qmeasure::FontMetricsSnapshot::makeV1(1);
  const auto* family = font_snapshot.findFamily("system-default", 400);
  CHECK(family != nullptr && family->asset_digest.view() ==
                                 quickapp::lvgl::font::systemDefaultFontDigest());
  font_publisher.closeAdmission();
  CHECK(font_publisher.tryFinalizeClose(kOwner).ok());

  qlm::MountTransaction move_button(
      surface_id, 1, mountAttempt("mnt:case001-move"),
      qlm::BoundedText::from("txn:move"), qlm::MountMode::kIncremental);
  move_button.operations[0] = qlm::MoveHost{button, root, 0};
  move_button.operation_count = 1;
  CHECK(mount.post(std::move(move_button)));
  CHECK(mount.service(kOwner, 16).ok());
  CHECK(mount_results.size() == 2);
  CHECK(mount_results[1].status == qlm::MountResultStatus::kMounted);
  CHECK(mount.liveObjectCount() == 3);

  qlm::MountTransaction remove_title(
      surface_id, 2, mountAttempt("mnt:case001-update"),
      qlm::BoundedText::from("txn:update"), qlm::MountMode::kIncremental);
  remove_title.operations[0] = qlm::RemoveHost{title};
  remove_title.operation_count = 1;
  CHECK(mount.post(std::move(remove_title)));
  CHECK(mount.service(kOwner, 16).ok());
  CHECK(mount_results.size() == 3);
  CHECK(mount_results[2].status == qlm::MountResultStatus::kMounted);
  CHECK(mount_results[2].live_objects == 3);  // root + button + private label.
  CHECK(mount.liveObjectCount() == 2);
  CHECK(mount.liveFontCount() == 1);

  const auto second_surface_id = surface("srf:case001-second");
  CHECK(surfaces.post(qls::CreateSurfaceHost{request("req:create-second"),
                                             second_surface_id, {320, 240}}));
  CHECK(tasks.pump(kOwner, 16).ok());
  CHECK(surfaces.service(kOwner, 16).error == qlf::LocalError::kNone);
  const auto second_root = node("node:second-root");
  qlm::MountTransaction second_surface_mount(
      second_surface_id, 0, mountAttempt("mnt:case001-second"),
      qlm::BoundedText::from("txn:second"), qlm::MountMode::kFull);
  second_surface_mount.operations[0] = qlm::CreateHost{
      second_root, qcore::package::HostComponentType::kView};
  second_surface_mount.operation_count = 1;
  CHECK(mount.post(std::move(second_surface_mount)));
  CHECK(mount.service(kOwner, 16).ok());
  CHECK(mount_results.size() == 4);
  CHECK(mount_results[3].status == qlm::MountResultStatus::kMounted);
  CHECK(mount.liveObjectCount() == 3);

  qlm::MountTransaction reload_first_surface(
      surface_id, 3, mountAttempt("mnt:case001-reload"),
      qlm::BoundedText::from("txn:reload"), qlm::MountMode::kFull);
  reload_first_surface.operations[0] = qlm::CreateHost{
      node("node:reloaded-root"), qcore::package::HostComponentType::kView};
  reload_first_surface.operation_count = 1;
  CHECK(mount.post(std::move(reload_first_surface)));
  CHECK(mount.service(kOwner, 16).ok());
  CHECK(mount_results.size() == 5);
  CHECK(mount_results[4].status == qlm::MountResultStatus::kMounted);
  CHECK(mount_results[4].live_objects == 1);
  CHECK(mount.liveObjectCount() == 2);
  CHECK(mount.liveFontCount() == 0);

  mount.close();
  CHECK(mount.finishClose(kOwner).ok());
  CHECK(mount.liveObjectCount() == 0);
  CHECK(mount.liveFontCount() == 0);
  surfaces.close();
  CHECK(surfaces.finishClose(kOwner).ok());
  CHECK(tasks.beginStop(kOwner, qlf::StopPolicy::kCancel).ok());
  CHECK(tasks.finishStop(kOwner).ok());
  CHECK(tasks.depth() == 0);
  lv_deinit();
  return true;
}

bool testMountRejectionBackpressureAndClose() {
  lv_init();
  lv_display_t* display = lv_sdl_window_create(160, 120);
  CHECK(display != nullptr);
  lv_display_set_default(display);

  qfake::FakeWakeup wakeup(kOwner);
  std::array<qlf::OwnerTask, 64> task_storage{};
  qlf::OwnerTaskQueue tasks(task_storage.data(), task_storage.size(), 64,
                            &wakeup);
  CHECK(tasks.bindOwner(kOwner).ok());

  qls::LvglPageRootBackend page_roots(lv_screen_active());
  qls::EmptySurfaceContentLifecycle content;
  SurfaceResults surface_results;
  qls::SurfaceHostAdapter surfaces(
      tasks, kOwner, page_roots, content, surface_results,
      qls::simulatorSurfaceHostLimits());
  const auto surface_id = surface("srf:mount-negative");
  CHECK(surfaces.post(qls::CreateSurfaceHost{request("req:negative-create"),
                                             surface_id, {160, 120}}));
  CHECK(tasks.pump(kOwner, 16).ok());
  CHECK(surfaces.service(kOwner, 16).error == qlf::LocalError::kNone);

  qlm::LvglMountBackend lvgl_backend(page_roots);
  MountResults mount_results;
  qlm::MountHost mount(tasks, kOwner, surfaces, lvgl_backend, mount_results,
                       qlm::simulatorMountHostLimits());
  CHECK(mount.service(qlf::OwnerToken{2}, 1).error ==
        qlf::LocalError::kWrongThread);
  CHECK(mount.finishClose(qlf::OwnerToken{2}).error ==
        qlf::LocalError::kWrongThread);

  qlm::MountTransaction illegal_full(
      surface_id, 0, mountAttempt("mnt:illegal-full"),
      qlm::BoundedText::from("txn:illegal-full"), qlm::MountMode::kFull);
  illegal_full.operations[0] =
      qlm::MoveHost{node("node:missing"), node("node:parent"), 0};
  illegal_full.operation_count = 1;
  CHECK(mount.post(std::move(illegal_full)));
  CHECK(mount.service(kOwner, 1).ok());
  CHECK(mount_results.size() == 1);
  CHECK(mount_results[0].status == qlm::MountResultStatus::kFailed);
  CHECK(mount_results[0].source_id.view() == "txn:illegal-full");
  CHECK(mount.liveObjectCount() == 0);

  qlm::MountTransaction unsupported_property(
      surface_id, 0, mountAttempt("mnt:unsupported-property"),
      qlm::BoundedText::from("txn:unsupported-property"),
      qlm::MountMode::kFull);
  const auto unsupported_node = node("node:unsupported");
  unsupported_property.operations[0] = qlm::CreateHost{
      unsupported_node, qcore::package::HostComponentType::kText};
  unsupported_property.operations[1] = qlm::SetHostProp{
      unsupported_node, qlm::BoundedText::from("fontWeight"),
      std::int32_t{16}};
  unsupported_property.operation_count = 2;
  CHECK(mount.post(std::move(unsupported_property)));
  CHECK(mount.service(kOwner, 1).ok());
  CHECK(mount_results.size() == 2);
  CHECK(mount_results[1].status == qlm::MountResultStatus::kFailed);
  CHECK(mount.liveObjectCount() == 0);
  CHECK(mount.liveFontCount() == 0);

  const std::size_t results_before_capacity = mount_results.size();
  for (std::size_t index = 0; index < qlm::MountHost::kStorageCapacity; ++index) {
    const std::string suffix = std::to_string(index);
    qlm::MountTransaction pending(
        surface_id, index + 1, mountAttempt("mnt:pending-" + suffix),
        qlm::BoundedText::from("txn:pending"), qlm::MountMode::kIncremental);
    CHECK(mount.post(std::move(pending)));
  }
  CHECK(mount.pendingCount() == qlm::MountHost::kStorageCapacity);
  qlm::MountTransaction overflow(
      surface_id, 99, mountAttempt("mnt:overflow"),
      qlm::BoundedText::from("txn:overflow"), qlm::MountMode::kIncremental);
  CHECK(!mount.post(std::move(overflow)));

  mount.close();
  CHECK(mount.finishClose(kOwner).error == qlf::LocalError::kBusy);
  qlm::MountTransaction after_close(
      surface_id, 100, mountAttempt("mnt:after-close"),
      qlm::BoundedText::from("txn:after-close"), qlm::MountMode::kIncremental);
  CHECK(!mount.post(std::move(after_close)));
  CHECK(mount.service(kOwner, 64).ok());
  CHECK(mount_results.size() ==
        results_before_capacity + qlm::MountHost::kStorageCapacity);
  CHECK(mount.pendingCount() == 0);
  CHECK(mount.finishClose(kOwner).ok());
  CHECK(mount.liveObjectCount() == 0);
  CHECK(mount.liveFontCount() == 0);

  surfaces.close();
  CHECK(surfaces.finishClose(kOwner).ok());
  CHECK(tasks.beginStop(kOwner, qlf::StopPolicy::kCancel).ok());
  CHECK(tasks.finishStop(kOwner).ok());
  CHECK(tasks.depth() == 0);
  lv_deinit();
  return true;
}

}  // namespace

int main() {
  return testCase001VisibleAndResources() &&
                 testMountRejectionBackpressureAndClose()
             ? 0
             : 1;
}
