#include "quickapp/lvgl/mount/mount_types.h"

#include <algorithm>
#include <cstring>

namespace quickapp::lvgl::mount {

BoundedText BoundedText::from(std::string_view value) noexcept {
  BoundedText result;
  result.size = std::min(value.size(), result.data.size());
  result.truncated = value.size() > result.data.size();
  std::memcpy(result.data.data(), value.data(), result.size);
  return result;
}

}  // namespace quickapp::lvgl::mount
