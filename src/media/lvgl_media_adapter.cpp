#include "quickapp/lvgl/media/lvgl_media_adapter.h"

#include <cmath>

namespace quickapp::lvgl::media {

core::package::VideoControlResult LvglMediaAdapter::control(
    const core::package::VideoControlRequest& request) const noexcept {
  using core::RuntimeError;
  using core::RuntimeErrorCode;
  using core::package::VideoControlResult;
  using core::package::VideoControlStatus;

  if (request.request_id.wire().empty() || request.surface_id.wire().empty() ||
      request.node_id.wire().empty() ||
      (request.kind == core::package::VideoControlKind::kSeek &&
       (!std::isfinite(request.position_seconds) ||
        request.position_seconds < 0))) {
    return {request.request_id,
            request.surface_id,
            request.node_id,
            request.kind,
            VideoControlStatus::kFailed,
            RuntimeError::simple(RuntimeErrorCode::kAbiInvalidArgument,
                                 "invalid video control request")};
  }

  return {request.request_id,
          request.surface_id,
          request.node_id,
          request.kind,
          VideoControlStatus::kUnsupported,
          RuntimeError::simple(RuntimeErrorCode::kHostFeatureUnsupported,
                               "LVGL has no video decoder or playback backend")};
}

}  // namespace quickapp::lvgl::media
