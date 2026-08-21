#pragma once

#include <cstdint>
#include <optional>
#include <variant>

#include "quickapp/core/foundation/error.h"
#include "quickapp/core/foundation/id.h"

namespace quickapp::lvgl::surface {

struct SurfaceViewport final {
  double width{0};
  double height{0};
};

struct CreateSurfaceHost final {
  core::RequestId request_id;
  core::SurfaceId surface_id;
  SurfaceViewport viewport;
};

struct PresentRootSurfaceHost final {
  core::RequestId request_id;
  core::SurfaceId surface_id;
};

struct PresentPushSurfaceHost final {
  core::RequestId request_id;
  core::SurfaceId surface_id;
  core::SurfaceId source_surface_id;
};

enum class SurfaceVisibility : std::uint8_t { kVisible, kHidden };

struct SetSurfaceVisibility final {
  core::RequestId request_id;
  core::SurfaceId surface_id;
  SurfaceVisibility visibility;
};

struct CloseSurfaceHost final {
  core::RequestId request_id;
  core::SurfaceId surface_id;
  core::SurfaceId reveal_surface_id;
};

struct DestroySurfaceHost final {
  core::RequestId request_id;
  core::SurfaceId surface_id;
};

using SurfaceCommand =
    std::variant<CreateSurfaceHost, PresentRootSurfaceHost,
                 PresentPushSurfaceHost, SetSurfaceVisibility,
                 CloseSurfaceHost, DestroySurfaceHost>;

enum class SurfaceResultStatus : std::uint8_t {
  kCreated,
  kPresented,
  kCompleted,
  kDestroyed,
  kFailed,
};

struct CreateSurfaceHostResult final {
  core::RequestId request_id;
  core::SurfaceId surface_id;
  SurfaceResultStatus status;
  std::optional<core::RuntimeError> error;
};

struct PresentRootSurfaceHostResult final {
  core::RequestId request_id;
  core::SurfaceId surface_id;
  SurfaceResultStatus status;
  std::optional<core::RuntimeError> error;
};

struct PresentPushSurfaceHostResult final {
  core::RequestId request_id;
  core::SurfaceId surface_id;
  core::SurfaceId source_surface_id;
  SurfaceResultStatus status;
  std::optional<core::RuntimeError> error;
};

struct SetSurfaceVisibilityResult final {
  core::RequestId request_id;
  core::SurfaceId surface_id;
  SurfaceVisibility visibility;
  SurfaceResultStatus status;
  std::optional<core::RuntimeError> error;
};

struct CloseSurfaceHostResult final {
  core::RequestId request_id;
  core::SurfaceId surface_id;
  core::SurfaceId reveal_surface_id;
  SurfaceResultStatus status;
  std::optional<core::RuntimeError> error;
};

struct DestroySurfaceHostResult final {
  core::RequestId request_id;
  core::SurfaceId surface_id;
  SurfaceResultStatus status;
  std::optional<core::RuntimeError> error;
};

using SurfaceResult =
    std::variant<CreateSurfaceHostResult, PresentRootSurfaceHostResult,
                 PresentPushSurfaceHostResult, SetSurfaceVisibilityResult,
                 CloseSurfaceHostResult, DestroySurfaceHostResult>;

}  // namespace quickapp::lvgl::surface
