#pragma once

#include <cstdint>
#include <optional>
#include <string_view>

#include "quickapp/core/foundation/error.h"
#include "quickapp/core/foundation/port.h"
#include "quickapp/lvgl/measure/font_metrics.h"

namespace quickapp::lvgl::measure {

enum class MeasureRole : std::uint8_t { kText, kButtonLabel };

enum class ConstraintKind : std::uint8_t {
  kUnconstrained,
  kAtMost,
  kExactly,
};

struct MeasureConstraint final {
  ConstraintKind kind{ConstraintKind::kUnconstrained};
  double value{0};
};

struct MeasureRequest final {
  std::string_view request_id;
  std::string_view surface_id;
  std::string_view node_id;
  std::uint64_t content_revision{0};
  std::uint64_t platform_font_generation{0};
  MeasureRole role{MeasureRole::kText};
  std::string_view text;
  std::string_view font_token;
  double font_size{0};
  std::uint16_t font_weight{400};
  MeasureConstraint width_constraint;
  MeasureConstraint height_constraint;
};

struct MeasureResult final {
  std::string_view request_id;
  std::string_view surface_id;
  std::string_view node_id;
  std::uint64_t content_revision{0};
  std::uint64_t platform_font_generation{0};
  bool measured{false};
  double width{0};
  double height{0};
  std::optional<core::RuntimeError> error;

  [[nodiscard]] static MeasureResult failed(
      const MeasureRequest& request, bool retryable,
      std::string_view message) noexcept;
};

struct MeasureLimits final {
  std::size_t max_text_bytes{65'536};
  std::size_t max_code_points{32'768};
  std::size_t max_lines{4'096};
  double max_font_size{256};
};

[[nodiscard]] MeasureLimits simulatorMeasureLimits() noexcept;
[[nodiscard]] MeasureLimits embeddedMeasureLimits() noexcept;

class FontMeasureAdapter final
    : public core::PlatformMeasurePort<MeasureRequest, MeasureResult> {
 public:
  explicit FontMeasureAdapter(FontSnapshotPublisher& publisher,
                              MeasureLimits limits = {}) noexcept
      : publisher_(publisher), limits_(limits) {}

  [[nodiscard]] MeasureResult measure(
      const MeasureRequest& request) noexcept override;

 private:
  [[nodiscard]] MeasureResult measureWithSnapshot(
      const MeasureRequest& request,
      const FontMetricsSnapshot& snapshot) noexcept;

  FontSnapshotPublisher& publisher_;
  MeasureLimits limits_;
};

}  // namespace quickapp::lvgl::measure
