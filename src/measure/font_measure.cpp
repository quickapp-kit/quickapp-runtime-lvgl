#include "quickapp/lvgl/measure/font_measure.h"

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>

namespace quickapp::lvgl::measure {
namespace {

constexpr std::int64_t kQ26Shift = 6;
constexpr std::int64_t kQ26One = 1LL << kQ26Shift;

bool isWhitespace(std::uint32_t code_point) noexcept {
  return code_point == 0x20 || code_point == 0x09 || code_point == 0x0b ||
         code_point == 0x0c || code_point == 0x0d || code_point == 0x85 ||
         code_point == 0xa0 || (code_point >= 0x2000 && code_point <= 0x200a);
}

bool isCjk(std::uint32_t code_point) noexcept {
  return (code_point >= 0x3400 && code_point <= 0x4dbf) ||
         (code_point >= 0x4e00 && code_point <= 0x9fff) ||
         (code_point >= 0xf900 && code_point <= 0xfaff) ||
         (code_point >= 0xff01 && code_point <= 0xff5e);
}

bool isBreakOpportunity(std::uint32_t code_point) noexcept {
  return isWhitespace(code_point) || isCjk(code_point);
}

bool nextCodePoint(std::string_view text, std::size_t& cursor,
                   std::uint32_t& code_point) noexcept {
  if (cursor >= text.size()) {
    return false;
  }
  const auto byte = [&text](std::size_t index) {
    return static_cast<std::uint8_t>(text[index]);
  };
  const std::uint8_t first = byte(cursor++);
  if (first <= 0x7f) {
    code_point = first;
    return true;
  }
  std::size_t continuation_count = 0;
  std::uint32_t value = 0;
  if (first >= 0xc2 && first <= 0xdf) {
    continuation_count = 1;
    value = first & 0x1f;
  } else if (first >= 0xe0 && first <= 0xef) {
    continuation_count = 2;
    value = first & 0x0f;
  } else if (first >= 0xf0 && first <= 0xf4) {
    continuation_count = 3;
    value = first & 0x07;
  } else {
    return false;
  }
  if (cursor + continuation_count > text.size()) {
    return false;
  }
  for (std::size_t index = 0; index < continuation_count; ++index) {
    const std::uint8_t next = byte(cursor++);
    if ((next & 0xc0) != 0x80) {
      return false;
    }
    value = (value << 6) | (next & 0x3f);
  }
  if ((continuation_count == 2 && value < 0x800) ||
      (continuation_count == 3 && value < 0x10000) ||
      (value >= 0xd800 && value <= 0xdfff) || value > 0x10ffff) {
    return false;
  }
  code_point = value;
  return true;
}

bool toQ26(double value, std::int64_t& output) noexcept {
  if (!std::isfinite(value) || value < 0 ||
      value > static_cast<double>(std::numeric_limits<std::int64_t>::max() /
                                  kQ26One)) {
    return false;
  }
  const double scaled = value * static_cast<double>(kQ26One);
  if (scaled > static_cast<double>(std::numeric_limits<std::int64_t>::max())) {
    return false;
  }
  output = static_cast<std::int64_t>(std::llround(scaled));
  return true;
}

bool scaleDesignUnits(std::int32_t design_units, std::int64_t font_size_q26,
                      std::uint32_t units_per_em,
                      std::int64_t& output) noexcept {
  if (units_per_em == 0) {
    return false;
  }
  const __int128 product = static_cast<__int128>(design_units) *
                           static_cast<__int128>(font_size_q26);
  const __int128 scaled =
      (product + static_cast<__int128>(units_per_em / 2)) /
      static_cast<__int128>(units_per_em);
  if (scaled < static_cast<__int128>(std::numeric_limits<std::int64_t>::min()) ||
      scaled > static_cast<__int128>(std::numeric_limits<std::int64_t>::max())) {
    return false;
  }
  output = static_cast<std::int64_t>(scaled);
  return true;
}

bool addQ26(std::int64_t left, std::int64_t right,
            std::int64_t& output) noexcept {
  if ((right > 0 && left > std::numeric_limits<std::int64_t>::max() - right) ||
      (right < 0 && left < std::numeric_limits<std::int64_t>::min() - right)) {
    return false;
  }
  output = left + right;
  return output >= 0;
}

}  // namespace

MeasureLimits simulatorMeasureLimits() noexcept { return {}; }

MeasureLimits embeddedMeasureLimits() noexcept {
  return {4'096, 2'048, 256, 256};
}

MeasureResult MeasureResult::failed(const MeasureRequest& request,
                                    bool retryable,
                                    std::string_view message) noexcept {
  return {request.request_id,
          request.surface_id,
          request.node_id,
          request.content_revision,
          request.platform_font_generation,
          false,
          0,
          0,
          core::RuntimeError::simple(core::RuntimeErrorCode::kMeasureFailed,
                                     message, retryable)};
}

MeasureResult FontMeasureAdapter::measure(
    const MeasureRequest& request) noexcept {
  if (request.text.size() > limits_.max_text_bytes ||
      !std::isfinite(request.font_size) || request.font_size <= 0 ||
      request.font_size > limits_.max_font_size || request.font_weight == 0 ||
      request.font_weight > 1000 || request.platform_font_generation == 0) {
    return MeasureResult::failed(request, false, "invalid measure request");
  }
  FontSnapshotPublisher::ReadGuard guard;
  const foundation::LocalResult acquired = publisher_.acquire(guard);
  if (!acquired.ok()) {
    return MeasureResult::failed(request, true, "font snapshot unavailable");
  }
  const FontMetricsSnapshot* snapshot = guard.get();
  if (snapshot == nullptr ||
      snapshot->generation() != request.platform_font_generation) {
    return MeasureResult::failed(request, true, "font generation is stale");
  }
  return measureWithSnapshot(request, *snapshot);
}

MeasureResult FontMeasureAdapter::measureWithSnapshot(
    const MeasureRequest& request, const FontMetricsSnapshot& snapshot) noexcept {
  const FontFamilyMetrics* family =
      snapshot.findFamily(request.font_token, request.font_weight);
  if (family == nullptr) {
    return MeasureResult::failed(request, false, "font family is unsupported");
  }
  std::int64_t font_size_q26 = 0;
  if (!toQ26(request.font_size, font_size_q26)) {
    return MeasureResult::failed(request, false, "font size is invalid");
  }
  std::int64_t width_limit = std::numeric_limits<std::int64_t>::max();
  if (request.width_constraint.kind != ConstraintKind::kUnconstrained &&
      !toQ26(request.width_constraint.value, width_limit)) {
    return MeasureResult::failed(request, false, "width constraint is invalid");
  }
  std::int64_t height_limit = std::numeric_limits<std::int64_t>::max();
  if (request.height_constraint.kind != ConstraintKind::kUnconstrained &&
      !toQ26(request.height_constraint.value, height_limit)) {
    return MeasureResult::failed(request, false, "height constraint is invalid");
  }

  if (request.text.empty()) {
    const std::int64_t result_width =
        request.width_constraint.kind == ConstraintKind::kExactly ? width_limit : 0;
    const std::int64_t result_height = request.height_constraint.kind ==
                                               ConstraintKind::kExactly
                                           ? height_limit
                                           : 0;
    return {request.request_id,
            request.surface_id,
            request.node_id,
            request.content_revision,
            request.platform_font_generation,
            true,
            static_cast<double>(result_width) / kQ26One,
            static_cast<double>(result_height) / kQ26One,
            std::nullopt};
  }

  std::size_t validation_cursor = 0;
  std::size_t code_point_count = 0;
  std::size_t line_count = 1;
  std::uint32_t code_point = 0;
  while (validation_cursor < request.text.size()) {
    if (!nextCodePoint(request.text, validation_cursor, code_point)) {
      return MeasureResult::failed(request, false, "text is not valid UTF-8");
    }
    ++code_point_count;
    if (code_point == '\n') {
      ++line_count;
    }
    if (code_point_count > limits_.max_code_points ||
        line_count > limits_.max_lines) {
      return MeasureResult::failed(request, false, "text measure limit exceeded");
    }
  }

  std::int64_t line_height = 0;
  if (!scaleDesignUnits(family->line_height_design_units, font_size_q26,
                        family->units_per_em, line_height)) {
    return MeasureResult::failed(request, false, "line height overflow");
  }

  const bool wraps = request.width_constraint.kind != ConstraintKind::kUnconstrained;
  std::size_t cursor = 0;
  std::size_t line_start = 0;
  std::size_t last_break = std::string_view::npos;
  std::int64_t line_width = 0;
  std::int64_t width_at_break = 0;
  std::int64_t max_width = 0;
  std::size_t lines = 1;
  std::uint32_t previous = 0;
  bool has_previous = false;

  while (cursor < request.text.size()) {
    const std::size_t before = cursor;
    if (!nextCodePoint(request.text, cursor, code_point)) {
      return MeasureResult::failed(request, false, "text is not valid UTF-8");
    }
    if (code_point == '\r') {
      const std::size_t after_cr = cursor;
      std::uint32_t following = 0;
      std::size_t probe = cursor;
      if (probe < request.text.size() && nextCodePoint(request.text, probe, following) &&
          following == '\n') {
        cursor = probe;
      } else {
        cursor = after_cr;
      }
      max_width = std::max(max_width, line_width);
      line_width = 0;
      ++lines;
      line_start = cursor;
      last_break = std::string_view::npos;
      width_at_break = 0;
      has_previous = false;
      continue;
    }
    if (code_point == '\n') {
      max_width = std::max(max_width, line_width);
      line_width = 0;
      ++lines;
      line_start = cursor;
      last_break = std::string_view::npos;
      width_at_break = 0;
      has_previous = false;
      continue;
    }
    const bool tab = code_point == '\t';
    if (tab) {
      code_point = 0x20;
    }
    if (!family->contains(code_point)) {
      return MeasureResult::failed(request, false, "glyph is unsupported");
    }
    std::int64_t advance = 0;
    const auto design_advance = tab ? family->space_advance_design_units
                                    : family->advance(code_point);
    if (!scaleDesignUnits(design_advance, font_size_q26,
                          family->units_per_em, advance)) {
      return MeasureResult::failed(request, false, "glyph advance overflow");
    }
    if (tab && (!addQ26(advance, advance, advance) ||
                !addQ26(advance, advance, advance))) {
      return MeasureResult::failed(request, false, "tab advance overflow");
    }
    if (has_previous) {
      std::int64_t adjustment = 0;
      if (!scaleDesignUnits(family->kerningAdjustment(previous, code_point),
                            font_size_q26, family->units_per_em, adjustment)) {
        return MeasureResult::failed(request, false, "kerning overflow");
      }
      if (adjustment > 0) {
        return MeasureResult::failed(request, false, "invalid kerning metrics");
      }
      if (!addQ26(advance, adjustment, advance)) {
        return MeasureResult::failed(request, false, "kerning made advance invalid");
      }
    }
    std::int64_t candidate = 0;
    if (!addQ26(line_width, advance, candidate)) {
      return MeasureResult::failed(request, false, "line width overflow");
    }
    if (wraps && candidate > width_limit && line_width > 0) {
      if (last_break != std::string_view::npos && last_break > line_start) {
        max_width = std::max(max_width, width_at_break);
        ++lines;
        cursor = last_break;
      } else {
        max_width = std::max(max_width, line_width);
        ++lines;
        cursor = before;
      }
      if (lines > limits_.max_lines) {
        return MeasureResult::failed(request, false, "line limit exceeded");
      }
      line_start = cursor;
      line_width = 0;
      last_break = std::string_view::npos;
      width_at_break = 0;
      has_previous = false;
      continue;
    }
    line_width = candidate;
    previous = code_point;
    has_previous = true;
    if (isBreakOpportunity(code_point)) {
      last_break = cursor;
      width_at_break = line_width;
    }
  }
  max_width = std::max(max_width, line_width);

  std::int64_t natural_height = 0;
  const __int128 height_product = static_cast<__int128>(line_height) *
                                  static_cast<__int128>(lines);
  if (height_product > std::numeric_limits<std::int64_t>::max()) {
    return MeasureResult::failed(request, false, "text height overflow");
  }
  natural_height = static_cast<std::int64_t>(height_product);

  std::int64_t result_width = max_width;
  if (request.width_constraint.kind == ConstraintKind::kAtMost) {
    result_width = std::min(result_width, width_limit);
  } else if (request.width_constraint.kind == ConstraintKind::kExactly) {
    result_width = width_limit;
  }
  std::int64_t result_height = natural_height;
  if (request.height_constraint.kind == ConstraintKind::kAtMost) {
    result_height = std::min(result_height, height_limit);
  } else if (request.height_constraint.kind == ConstraintKind::kExactly) {
    result_height = height_limit;
  }
  if (result_width < 0 || result_height < 0) {
    return MeasureResult::failed(request, false, "negative measured size");
  }
  return {request.request_id,
          request.surface_id,
          request.node_id,
          request.content_revision,
          request.platform_font_generation,
          true,
          static_cast<double>(result_width) / kQ26One,
          static_cast<double>(result_height) / kQ26One,
          std::nullopt};
}

}  // namespace quickapp::lvgl::measure
