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

struct InputEvents final {
  std::vector<qcore::package::EventType> types;
  std::vector<std::string> values;

  static void callback(void* context, const qcore::SurfaceId&, const qcore::NodeId&,
                       qcore::package::EventType type, const char* value,
                       std::uint64_t) noexcept {
    auto* self = static_cast<InputEvents*>(context);
    if (self == nullptr) return;
    self->types.push_back(type);
    self->values.emplace_back(value == nullptr ? "" : value);
  }
};

struct SwitchEvents final {
  std::vector<bool> values;

  static void callback(void* context, const qcore::SurfaceId&, const qcore::NodeId&,
                       bool checked, std::uint64_t) noexcept {
    auto* self = static_cast<SwitchEvents*>(context);
    if (self != nullptr) self->values.push_back(checked);
  }
};

struct SliderEvents final {
  std::vector<double> values;
  std::vector<bool> from_user;

  static void callback(void* context, const qcore::SurfaceId&, const qcore::NodeId&,
                       double value, bool is_from_user, std::uint64_t) noexcept {
    auto* self = static_cast<SliderEvents*>(context);
    if (self == nullptr) return;
    self->values.push_back(value);
    self->from_user.push_back(is_from_user);
  }
};

struct PickerEvents final {
  std::vector<qlm::MountHost::PickerEvent> events;
  std::vector<std::int32_t> selected;
  std::vector<std::string> values;

  static void callback(void* context, const qcore::SurfaceId&, const qcore::NodeId&,
                       qlm::MountHost::PickerEvent event, std::int32_t selected,
                       const char* value, std::uint64_t) noexcept {
    auto* self = static_cast<PickerEvents*>(context);
    if (self == nullptr) return;
    self->events.push_back(event);
    self->selected.push_back(selected);
    self->values.emplace_back(value == nullptr ? "" : value);
  }
};

bool testSliderAndPickerHostLifecycle() {
  lv_init();
  lv_display_t* display = lv_sdl_window_create(320, 240);
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
  const auto surface_id = surface("srf:controls-002");
  CHECK(surfaces.post(qls::CreateSurfaceHost{request("req:controls-002-create"),
                                             surface_id, {320, 240}}));
  CHECK(tasks.pump(kOwner, 16).ok());
  CHECK(surfaces.service(kOwner, 16).error == qlf::LocalError::kNone);

  qlm::LvglMountBackend lvgl_backend(page_roots);
  MountResults mount_results;
  qlm::MountHost mount(tasks, kOwner, surfaces, lvgl_backend, mount_results,
                       qlm::simulatorMountHostLimits());
  const auto root = node("node:controls-002-root");
  const auto slider = node("node:controls-002-slider");
  const auto picker = node("node:controls-002-picker");
  qlm::MountTransaction transaction(
      surface_id, 0, mountAttempt("mnt:controls-002"),
      qlm::BoundedText::from("controls-002"), qlm::MountMode::kFull);
  transaction.operations[0] =
      qlm::CreateHost{root, qcore::package::HostComponentType::kView};
  transaction.operations[1] =
      qlm::CreateHost{slider, qcore::package::HostComponentType::kSlider};
  transaction.operations[2] =
      qlm::SetHostProp{slider, qlm::BoundedText::from("min"), 0.0};
  transaction.operations[3] =
      qlm::SetHostProp{slider, qlm::BoundedText::from("max"), 100.0};
  transaction.operations[4] =
      qlm::SetHostProp{slider, qlm::BoundedText::from("step"), 5.0};
  transaction.operations[5] =
      qlm::SetHostProp{slider, qlm::BoundedText::from("value"), 40.0};
  transaction.operations[6] =
      qlm::CreateHost{picker, qcore::package::HostComponentType::kPicker};
  transaction.operations[7] =
      qlm::SetHostProp{picker, qlm::BoundedText::from("mode"),
                       qlm::BoundedText::from("text")};
  transaction.operations[8] =
      qlm::SetHostProp{picker, qlm::BoundedText::from("range"),
                       qlm::BoundedText::from("安静|标准|性能")};
  transaction.operations[9] =
      qlm::SetHostProp{picker, qlm::BoundedText::from("selected"), 1.0};
  transaction.operations[10] = qlm::SetHostLayout{root, {0, 0, 320, 240}};
  transaction.operations[11] = qlm::SetHostLayout{slider, {16, 24, 288, 32}};
  transaction.operations[12] = qlm::SetHostLayout{picker, {16, 80, 288, 40}};
  transaction.operations[13] = qlm::InsertHostChild{slider, root, 0};
  transaction.operations[14] = qlm::InsertHostChild{picker, root, 1};
  transaction.operation_count = 15;
  CHECK(mount.post(std::move(transaction)));
  CHECK(mount.service(kOwner, 32).ok());
  CHECK(mount_results.size() == 1);
  CHECK(mount_results[0].status == qlm::MountResultStatus::kMounted);
  CHECK(mount.liveObjectCount() == 3);

  auto* slider_object = static_cast<lv_obj_t*>(mount.nativeObject(surface_id, slider));
  auto* picker_object = static_cast<lv_obj_t*>(mount.nativeObject(surface_id, picker));
  CHECK(slider_object != nullptr && picker_object != nullptr);
  CHECK(lv_slider_get_value(slider_object) == 40000);
  CHECK(lv_dropdown_get_selected(picker_object) == 1);
  std::array<char, qlm::kMaxPropertyText> selected_text{};
  lv_dropdown_get_selected_str(picker_object, selected_text.data(), selected_text.size());
  CHECK(std::string(selected_text.data()) == "标准");

  SliderEvents slider_events;
  PickerEvents picker_events;
  CHECK(mount.installSliderHandler(surface_id, slider,
                                   &SliderEvents::callback, &slider_events));
  CHECK(mount.installPickerHandler(surface_id, picker,
                                   &PickerEvents::callback, &picker_events));

  lv_slider_set_value(slider_object, 45000, LV_ANIM_OFF);
  lv_obj_send_event(slider_object, LV_EVENT_VALUE_CHANGED, nullptr);
  CHECK(slider_events.values.size() == 1);
  CHECK(std::abs(slider_events.values.front() - 45.0) < 0.001);
  CHECK(!slider_events.from_user.front());

  CHECK(mount.confirmPicker(surface_id, picker));
  CHECK(picker_events.events.size() == 2);
  CHECK(picker_events.events[0] == qlm::MountHost::PickerEvent::kChange);
  CHECK(picker_events.events[1] == qlm::MountHost::PickerEvent::kConfirm);
  CHECK(picker_events.selected.back() == 1);
  CHECK(picker_events.values.back() == "标准");

  qlm::MountTransaction picker_update(
      surface_id, 1, mountAttempt("mnt:controls-002-update"),
      qlm::BoundedText::from("controls-002-update"), qlm::MountMode::kIncremental);
  picker_update.operations[0] =
      qlm::SetHostProp{picker, qlm::BoundedText::from("selected"), 2.0};
  picker_update.operation_count = 1;
  CHECK(mount.post(std::move(picker_update)));
  CHECK(mount.service(kOwner, 16).ok());
  CHECK(mount_results.size() == 2);
  CHECK(mount_results[1].status == qlm::MountResultStatus::kMounted);
  CHECK(lv_dropdown_get_selected(picker_object) == 2);
  CHECK(mount.confirmPicker(surface_id, picker));
  CHECK(picker_events.selected.back() == 2);
  CHECK(picker_events.values.back() == "性能");
  CHECK(mount.cancelPicker(surface_id, picker));
  CHECK(picker_events.events.back() == qlm::MountHost::PickerEvent::kCancel);

  CHECK(mount.releaseSurface(kOwner, surface_id).ok());
  CHECK(mount.liveObjectCount() == 0);
  CHECK(mount.liveFontCount() == 0);
  CHECK(!mount.confirmPicker(surface_id, picker));
  CHECK(!mount.cancelPicker(surface_id, picker));
  mount.close();
  CHECK(mount.finishClose(kOwner).ok());
  surfaces.close();
  CHECK(surfaces.finishClose(kOwner).ok());
  CHECK(tasks.beginStop(kOwner, qlf::StopPolicy::kCancel).ok());
  CHECK(tasks.finishStop(kOwner).ok());
  lv_deinit();
  return true;
}

struct ScrollEvents final {
  std::vector<qcore::package::EventType> types;
  std::vector<std::int32_t> offsets;
  std::vector<std::int32_t> contents;
  std::vector<std::int32_t> viewports;

  static void callback(void* context, const qcore::SurfaceId&,
                       const qcore::NodeId&, qcore::package::EventType type,
                       std::int32_t offset, std::int32_t content,
                       std::int32_t viewport, std::uint64_t) noexcept {
    auto* self = static_cast<ScrollEvents*>(context);
    if (self == nullptr) return;
    self->types.push_back(type);
    self->offsets.push_back(offset);
    self->contents.push_back(content);
    self->viewports.push_back(viewport);
  }
};

bool testScrollAndListHostLifecycle() {
  lv_init();
  lv_display_t* display = lv_sdl_window_create(320, 240);
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
  const auto surface_id = surface("srf:list-001");
  CHECK(surfaces.post(qls::CreateSurfaceHost{request("req:list-create"),
                                             surface_id, {320, 240}}));
  CHECK(tasks.pump(kOwner, 16).ok());
  CHECK(surfaces.service(kOwner, 16).error == qlf::LocalError::kNone);

  qlm::LvglMountBackend lvgl_backend(page_roots);
  MountResults mount_results;
  qlm::MountHost mount(tasks, kOwner, surfaces, lvgl_backend, mount_results,
                       qlm::simulatorMountHostLimits());
  const auto root = node("node:list-root");
  const auto scroll = node("node:list-scroll");
  const auto list = node("node:list-content");
  const auto item_a = node("node:list-a");
  const auto item_b = node("node:list-b");
  const auto item_c = node("node:list-c");
  qlm::MountTransaction transaction(
      surface_id, 0, mountAttempt("mnt:list-001"),
      qlm::BoundedText::from("list-001"), qlm::MountMode::kFull);
  transaction.operations[0] =
      qlm::CreateHost{root, qcore::package::HostComponentType::kView};
  transaction.operations[1] =
      qlm::CreateHost{scroll, qcore::package::HostComponentType::kScroll};
  transaction.operations[2] =
      qlm::CreateHost{list, qcore::package::HostComponentType::kList};
  transaction.operations[3] =
      qlm::CreateHost{item_a, qcore::package::HostComponentType::kView};
  transaction.operations[4] =
      qlm::CreateHost{item_b, qcore::package::HostComponentType::kView};
  transaction.operations[5] =
      qlm::CreateHost{item_c, qcore::package::HostComponentType::kView};
  transaction.operations[6] = qlm::SetHostLayout{root, {0, 0, 320, 240}};
  transaction.operations[7] = qlm::SetHostLayout{scroll, {12, 12, 200, 80}};
  transaction.operations[8] = qlm::SetHostLayout{list, {0, 0, 180, 240}};
  transaction.operations[9] = qlm::SetHostLayout{item_a, {0, 0, 180, 70}};
  transaction.operations[10] = qlm::SetHostLayout{item_b, {0, 80, 180, 70}};
  transaction.operations[11] = qlm::SetHostLayout{item_c, {0, 160, 180, 70}};
  transaction.operations[12] = qlm::InsertHostChild{scroll, root, 0};
  transaction.operations[13] = qlm::InsertHostChild{list, scroll, 0};
  transaction.operations[14] = qlm::InsertHostChild{item_a, list, 0};
  transaction.operations[15] = qlm::InsertHostChild{item_b, list, 1};
  transaction.operations[16] = qlm::InsertHostChild{item_c, list, 2};
  transaction.operation_count = 17;
  CHECK(mount.post(std::move(transaction)));
  CHECK(mount.service(kOwner, 32).ok());
  CHECK(mount_results.size() == 1);
  CHECK(mount_results[0].status == qlm::MountResultStatus::kMounted);
  CHECK(mount.liveObjectCount() == 6);

  auto* scroll_object = static_cast<lv_obj_t*>(mount.nativeObject(surface_id, scroll));
  auto* list_object = static_cast<lv_obj_t*>(mount.nativeObject(surface_id, list));
  CHECK(scroll_object != nullptr && list_object != nullptr);
  lv_obj_update_layout(lv_screen_active());
  CHECK((lv_obj_get_scroll_dir(scroll_object) & LV_DIR_VER) != 0);
  CHECK(lv_obj_get_height(scroll_object) == 80);
  CHECK(lv_obj_get_height(list_object) == 240);
  CHECK(lv_obj_get_scroll_bottom(scroll_object) > 0);

  void* item_b_native = mount.nativeObject(surface_id, item_b);
  qlm::MountTransaction move_item(
      surface_id, 1, mountAttempt("mnt:list-001-move"),
      qlm::BoundedText::from("list-001-move"), qlm::MountMode::kIncremental);
  move_item.operations[0] = qlm::MoveHost{item_b, list, 0};
  move_item.operation_count = 1;
  CHECK(mount.post(std::move(move_item)));
  CHECK(mount.service(kOwner, 16).ok());
  CHECK(mount_results.size() == 2);
  CHECK(mount_results[1].status == qlm::MountResultStatus::kMounted);
  CHECK(mount.nativeObject(surface_id, item_b) == item_b_native);
  CHECK(mount.liveObjectCount() == 6);

  ScrollEvents events;
  CHECK(mount.installScrollHandler(surface_id, scroll,
                                   &ScrollEvents::callback, &events));
  lv_obj_scroll_to_y(scroll_object, 100, LV_ANIM_OFF);
  CHECK(lv_obj_get_scroll_y(scroll_object) > 0);
  CHECK(!events.types.empty());
  CHECK(events.offsets.back() > 0);
  CHECK(events.contents.back() > events.viewports.back());
  lv_obj_send_event(scroll_object, LV_EVENT_SCROLL_END, nullptr);
  CHECK(std::find(events.types.begin(), events.types.end(),
                  qcore::package::EventType::kScrollEnd) != events.types.end());

  lv_obj_scroll_to_y(scroll_object, 0, LV_ANIM_OFF);
  lv_obj_send_event(scroll_object, LV_EVENT_SCROLL, nullptr);
  CHECK(std::find(events.types.begin(), events.types.end(),
                  qcore::package::EventType::kScrollTop) != events.types.end());
  const auto bottom = lv_obj_get_scroll_bottom(scroll_object);
  lv_obj_scroll_to_y(scroll_object, bottom + lv_obj_get_scroll_y(scroll_object),
                     LV_ANIM_OFF);
  lv_obj_send_event(scroll_object, LV_EVENT_SCROLL, nullptr);
  CHECK(std::find(events.types.begin(), events.types.end(),
                  qcore::package::EventType::kScrollBottom) != events.types.end());

  CHECK(mount.releaseSurface(kOwner, surface_id).ok());
  CHECK(mount.liveObjectCount() == 0);
  CHECK(mount.liveFontCount() == 0);
  CHECK(!mount.installScrollHandler(surface_id, scroll,
                                    &ScrollEvents::callback, &events));
  mount.close();
  CHECK(mount.finishClose(kOwner).ok());
  surfaces.close();
  CHECK(surfaces.finishClose(kOwner).ok());
  CHECK(tasks.beginStop(kOwner, qlf::StopPolicy::kCancel).ok());
  CHECK(tasks.finishStop(kOwner).ok());
  lv_deinit();
  return true;
}

bool testInputAndSwitchHostLifecycle() {
  lv_init();
  lv_display_t* display = lv_sdl_window_create(320, 240);
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
  const auto surface_id = surface("srf:controls-001");
  CHECK(surfaces.post(qls::CreateSurfaceHost{request("req:controls-create"),
                                             surface_id, {320, 240}}));
  CHECK(tasks.pump(kOwner, 16).ok());
  CHECK(surfaces.service(kOwner, 16).error == qlf::LocalError::kNone);

  qlm::LvglMountBackend lvgl_backend(page_roots);
  MountResults mount_results;
  qlm::MountHost mount(tasks, kOwner, surfaces, lvgl_backend, mount_results,
                       qlm::simulatorMountHostLimits());
  const auto root = node("node:controls-root");
  const auto input = node("node:controls-input");
  const auto toggle = node("node:controls-switch");
  qlm::MountTransaction transaction(
      surface_id, 0, mountAttempt("mnt:controls-001"),
      qlm::BoundedText::from("controls-001"), qlm::MountMode::kFull);
  transaction.operations[0] =
      qlm::CreateHost{root, qcore::package::HostComponentType::kView};
  transaction.operations[1] =
      qlm::CreateHost{input, qcore::package::HostComponentType::kInput};
  transaction.operations[2] =
      qlm::SetHostProp{input, qlm::BoundedText::from("value"),
                       qlm::BoundedText::from("QuickApp")};
  transaction.operations[3] =
      qlm::CreateHost{toggle, qcore::package::HostComponentType::kSwitch};
  transaction.operations[4] =
      qlm::SetHostProp{toggle, qlm::BoundedText::from("checked"), true};
  transaction.operations[5] =
      qlm::SetHostProp{toggle, qlm::BoundedText::from("enabled"), true};
  transaction.operations[6] = qlm::SetHostLayout{root, {0, 0, 320, 240}};
  transaction.operations[7] = qlm::SetHostLayout{input, {8, 8, 240, 38}};
  transaction.operations[8] = qlm::SetHostLayout{toggle, {8, 56, 54, 30}};
  transaction.operations[9] = qlm::InsertHostChild{input, root, 0};
  transaction.operations[10] = qlm::InsertHostChild{toggle, root, 1};
  transaction.operation_count = 11;
  CHECK(mount.post(std::move(transaction)));
  CHECK(mount.service(kOwner, 16).ok());
  CHECK(mount_results.size() == 1);
  CHECK(mount_results[0].status == qlm::MountResultStatus::kMounted);
  CHECK(mount.liveObjectCount() == 3);

  InputEvents input_events;
  SwitchEvents switch_events;
  CHECK(mount.installInputHandler(surface_id, input, &InputEvents::callback,
                                  &input_events));
  CHECK(mount.installSwitchHandler(surface_id, toggle, &SwitchEvents::callback,
                                   &switch_events));
  auto* input_object = static_cast<lv_obj_t*>(mount.nativeObject(surface_id, input));
  auto* switch_object = static_cast<lv_obj_t*>(mount.nativeObject(surface_id, toggle));
  CHECK(input_object != nullptr);
  CHECK(switch_object != nullptr);
  CHECK(std::string(lv_textarea_get_text(input_object)) == "QuickApp");
  CHECK(lv_obj_has_state(switch_object, LV_STATE_CHECKED));

  lv_obj_send_event(input_object, LV_EVENT_FOCUSED, nullptr);
  lv_textarea_set_text(input_object, "Changed");
  lv_obj_send_event(input_object, LV_EVENT_VALUE_CHANGED, nullptr);
  CHECK(input_events.types.size() >= 3);
  CHECK(input_events.types[0] == qcore::package::EventType::kFocus);
  CHECK(input_events.types.back() == qcore::package::EventType::kChange);
  CHECK(input_events.values.back() == "Changed");

  lv_obj_clear_state(switch_object, LV_STATE_CHECKED);
  lv_obj_send_event(switch_object, LV_EVENT_VALUE_CHANGED, nullptr);
  CHECK(switch_events.values.size() == 1);
  CHECK(!switch_events.values.front());

  CHECK(mount.releaseSurface(kOwner, surface_id).ok());
  CHECK(mount.liveObjectCount() == 0);
  CHECK(mount.liveFontCount() == 0);
  mount.close();
  CHECK(mount.finishClose(kOwner).ok());
  surfaces.close();
  CHECK(surfaces.finishClose(kOwner).ok());
  CHECK(tasks.beginStop(kOwner, qlf::StopPolicy::kCancel).ok());
  CHECK(tasks.finishStop(kOwner).ok());
  lv_deinit();
  return true;
}

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
                 testMountRejectionBackpressureAndClose() &&
                 testInputAndSwitchHostLifecycle() &&
                 testSliderAndPickerHostLifecycle() &&
                 testScrollAndListHostLifecycle()
             ? 0
             : 1;
}
