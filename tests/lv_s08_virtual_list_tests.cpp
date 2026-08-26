#include <string>
#include <set>
#include <vector>

#include <lvgl.h>
#include <lvgl/drivers/sdl/lv_sdl_window.h>

#include "quickapp/lvgl/list/virtual_list.h"

namespace qv = quickapp::lvgl::list;

bool bindItem(void*, lv_obj_t* object, std::size_t index,
             std::string_view) noexcept {
  lv_obj_set_style_bg_color(object, lv_color_hex(static_cast<std::uint32_t>(
                                                   0x100000U + index)), 0);
  return true;
}

#define CHECK(value) \
  do { \
    if (!(value)) return 1; \
  } while (false)

int main() {
  qv::VirtualListWindow list(8);
  std::vector<std::string> keys;
  for (std::size_t i = 0; i < 1000; ++i) keys.push_back("item-" + std::to_string(i));
  CHECK(list.configure(keys.size(), 40, 200, keys));
  CHECK(list.logicalCount() == 1000);
  CHECK(list.livePhysicalCount() <= 8);
  CHECK(list.contentHeight() == 40000);
  const auto initial = list.window();
  CHECK(initial.first_index == 0 && initial.last_index_exclusive == 6);
  CHECK(list.scrollTo(20000));
  CHECK(list.window().first_index == 500);
  CHECK(list.livePhysicalCount() <= 8);
  const auto hit = list.hitTest(10);
  CHECK(hit.has_value() && hit->logical_index == 500);
  CHECK(list.scrollTo(39999));
  CHECK(list.window().last_index_exclusive == 1000);
  CHECK(list.hitTest(1).has_value());
  CHECK(list.configure(10, 20, 60,
                       {"a", "b", "c", "d", "e", "f", "g", "h", "i", "j"}));
  const auto before = list.window();
  CHECK(list.scrollTo(20));
  const auto after = list.window();
  std::set<std::size_t> slots;
  for (const auto& item : after.items) slots.insert(item.physical_slot);
  CHECK(slots.size() == after.items.size());
  CHECK(after.items.front().key == "b");
  CHECK(after.items.front().physical_slot == before.items[1].physical_slot);
  list.reset();
  CHECK(list.livePhysicalCount() == 0 && list.logicalCount() == 0);

  lv_init();
  auto* display = lv_sdl_window_create(200, 200);
  CHECK(display != nullptr);
  lv_display_set_default(display);
  auto* viewport = lv_obj_create(lv_screen_active());
  CHECK(viewport != nullptr);
  lv_obj_set_size(viewport, 180, 100);
  qv::VirtualListHost host(8);
  std::vector<std::string> host_keys;
  for (std::size_t i = 0; i < 1000; ++i) {
    host_keys.push_back("host-" + std::to_string(i));
  }
  CHECK(host.configure(viewport, 20, 100, std::move(host_keys), bindItem,
                       nullptr));
  CHECK(host.livePhysicalCount() == 6);
  auto* first_object = host.nativeObject(0);
  CHECK(first_object != nullptr);
  CHECK(host.scrollTo(20000));
  CHECK(host.livePhysicalCount() <= 8);
  CHECK(host.nativeObject(0) == first_object);
  CHECK(host.hitTest(1).has_value());
  host.reset();
  CHECK(host.livePhysicalCount() == 0);
  lv_deinit();
  return 0;
}
