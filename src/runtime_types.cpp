#include "quickapp/lvgl/runtime/runtime_types.h"

namespace quickapp::lvgl::runtime {
namespace {

constexpr BuildProfileDefinition kSimulatorProfile{
    ProfileId::kSimulatorDev,
    "lvgl-simulator-dev",
    ManifestTarget::kLvglSimulator,
    BuildMode::kDebug,
    ObservationLevel::kDiagnostic,
    {512, 128, 64, 32, 32, 1, 16, 1},
};

constexpr BuildProfileDefinition kEmbeddedProfile{
    ProfileId::kEmbeddedMin,
    "lvgl-embedded-min",
    ManifestTarget::kLvglEmbedded,
    BuildMode::kRelease,
    ObservationLevel::kBaseline,
    {64, 16, 16, 8, 8, 1, 4, 1},
};

}  // namespace

const BuildProfileDefinition& profileDefinition(ProfileId id) noexcept {
  return id == ProfileId::kSimulatorDev ? kSimulatorProfile
                                        : kEmbeddedProfile;
}

}  // namespace quickapp::lvgl::runtime
