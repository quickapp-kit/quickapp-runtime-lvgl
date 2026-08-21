#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>

#include "quickapp/core/foundation/error.h"

namespace quickapp::lvgl::runtime {

enum class ProfileId : std::uint8_t {
  kSimulatorDev,
  kEmbeddedMin,
};

enum class ManifestTarget : std::uint8_t {
  kLvglSimulator,
  kLvglEmbedded,
};

enum class BuildMode : std::uint8_t {
  kDebug,
  kRelease,
};

enum class ObservationLevel : std::uint8_t {
  kBaseline,
  kDiagnostic,
};

enum class ModuleCategory : std::uint8_t {
  kKernel,
  kRuntime,
  kEngine,
  kPlatform,
  kBackend,
  kComponent,
  kCapability,
  kDiagnostic,
};

struct ProfileLimits final {
  std::size_t owner_task_capacity;
  std::size_t raw_input_capacity;
  std::size_t max_tasks_per_pump;
  std::size_t max_raw_samples_per_pump;
  std::size_t max_timer_callbacks_per_pump;
  std::size_t max_display_submissions_per_pump;
  std::size_t max_in_flight_package_reads;
  std::size_t admission_retry_attempts_per_source_per_turn;
};

struct BuildProfileDefinition final {
  ProfileId id;
  std::string_view profile_id;
  ManifestTarget target;
  BuildMode build_mode;
  ObservationLevel observation_level;
  ProfileLimits limits;
};

struct ModuleDescriptor final {
  std::string_view module_id;
  ModuleCategory category;
  std::string_view version;
};

struct BuildInventoryView final {
  std::span<const ModuleDescriptor> linked_modules;
  std::span<const std::string_view> components;
  std::span<const std::string_view> capabilities;
  std::uint64_t binary_bytes{0};
};

enum class CompositionIssue : std::uint8_t {
  kNone,
  kInvalidModuleId,
  kDuplicateModule,
  kMissingKernel,
  kInvalidJsFrameworkCount,
  kInvalidEngineCount,
  kEngineDescriptorMismatch,
  kMissingProfileModule,
  kForbiddenProfileModule,
  kMissingV1Component,
  kMissingV1Capability,
  kInvalidBinaryBytes,
};

struct CompositionValidation final {
  const BuildProfileDefinition* profile{nullptr};
  CompositionIssue issue{CompositionIssue::kNone};
  bool isolated_evidence{true};
  bool product_manifest{false};

  [[nodiscard]] bool ok() const noexcept {
    return profile != nullptr && issue == CompositionIssue::kNone;
  }
};

enum class LaunchTarget : std::uint8_t {
  kAndroid,
  kLvgl,
  kIos,
};

struct RuntimeLaunchProfileView final {
  std::string_view artifact;
  std::string_view entry_route;
  bool params_is_runtime_value_object{true};
  std::uint32_t viewport_width{0};
  std::uint32_t viewport_height{0};
  bool viewport_is_logical_px{true};
  std::string_view trace_output;
  bool trace_disabled{true};
  LaunchTarget target{LaunchTarget::kLvgl};
  bool has_unknown_fields{false};
};

struct RuntimeCallResult final {
  bool success{false};
  core::RuntimeErrorCode error{core::RuntimeErrorCode::kPlatformRejected};
  bool retryable{false};

  [[nodiscard]] static constexpr RuntimeCallResult ok() noexcept {
    return {true, core::RuntimeErrorCode::kPlatformRejected, false};
  }

  [[nodiscard]] static constexpr RuntimeCallResult fail(
      core::RuntimeErrorCode code, bool can_retry = false) noexcept {
    return {false, code, can_retry};
  }
};

[[nodiscard]] const BuildProfileDefinition& profileDefinition(
    ProfileId id) noexcept;

}  // namespace quickapp::lvgl::runtime
