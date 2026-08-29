#include <cassert>
#include <cstdio>

#include "quickapp/lvgl/media/lvgl_media_adapter.h"

namespace {

using quickapp::core::NodeId;
using quickapp::core::RequestId;
using quickapp::core::SurfaceId;
using quickapp::core::package::VideoControlKind;
using quickapp::core::package::VideoControlRequest;
using quickapp::core::package::VideoControlStatus;

VideoControlRequest request(VideoControlKind kind) {
  return {RequestId::parse("req:lvgl-video").value(),
          SurfaceId::parse("srf:lvgl-video").value(),
          NodeId::parse("node:lvgl-video").value(), kind, 0};
}

void testUnsupportedAndTeardown() {
  quickapp::lvgl::media::LvglMediaAdapter adapter;
  for (const auto kind : {VideoControlKind::kPlay, VideoControlKind::kPause,
                          VideoControlKind::kSeek}) {
    const auto result = adapter.control(request(kind));
    assert(result.status == VideoControlStatus::kUnsupported);
    assert(result.error.has_value());
    assert(quickapp::core::to_wire(result.error->code) ==
           "HOST_FEATURE_UNSUPPORTED");
    assert(adapter.liveResourceCount() == 0);
  }

  auto invalid = request(VideoControlKind::kSeek);
  invalid.position_seconds = -1;
  const auto invalid_result = adapter.control(invalid);
  assert(invalid_result.status == VideoControlStatus::kFailed);
  assert(invalid_result.error.has_value());
  assert(quickapp::core::to_wire(invalid_result.error->code) ==
         "ABI_INVALID_ARGUMENT");

  adapter.teardown(SurfaceId::parse("srf:lvgl-video").value());
  adapter.clear();
  assert(adapter.liveResourceCount() == 0);
}

}  // namespace

int main() {
  testUnsupportedAndTeardown();
  std::puts("LVGL B7 video unsupported tests passed");
  return 0;
}
