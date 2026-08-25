#include <cmath>
#include <cstdint>
#include <cstdio>
#include <string_view>

#include <lvgl.h>

#include "quickapp/core/foundation/port.h"
#include "quickapp/lvgl/font/system_default_font_asset.h"
#include "quickapp/lvgl/measure/font_measure.h"

namespace qcore = quickapp::core;
namespace qfont = quickapp::lvgl::font;
namespace qmeasure = quickapp::lvgl::measure;
namespace qfoundation = quickapp::lvgl::foundation;

namespace {

#define CHECK(expression)                                                     \
  do {                                                                        \
    if (!(expression)) {                                                      \
      std::fprintf(stderr, "CHECK failed at %s:%d: %s\n", __FILE__, __LINE__, \
                   #expression);                                             \
      return false;                                                           \
    }                                                                         \
  } while (false)

constexpr qfoundation::OwnerToken kOwner{1};

class GenerationResults final
    : public qcore::CoreIngressPort<qmeasure::PlatformFontGenerationChanged> {
 public:
  qcore::EnqueueResult post(
      qmeasure::PlatformFontGenerationChanged&&) noexcept override {
    return qcore::EnqueueResult::success(qcore::Accepted{});
  }
  void close() noexcept override {}
};

bool probe() {
  lv_init();
  lv_mem_monitor_t before{};
  lv_mem_monitor(&before);

  const auto bytes = qfont::systemDefaultFontBytes();
  CHECK(!bytes.empty());
  lv_font_t* native = lv_tiny_ttf_create_data_ex(
      bytes.data(), bytes.size(), 40, LV_FONT_KERNING_NONE, 2);
  CHECK(native != nullptr);
  lv_font_glyph_dsc_t glyph{};
  CHECK(lv_font_get_glyph_dsc(native, &glyph, 0x4e2d, 0));
  CHECK(glyph.adv_w == 40);
  const void* glyph_bitmap = lv_font_get_glyph_bitmap(&glyph, nullptr);
  CHECK(glyph_bitmap != nullptr);
  lv_font_glyph_release_draw_data(&glyph);

  lv_font_glyph_dsc_t punctuation{};
  CHECK(lv_font_get_glyph_dsc(native, &punctuation, 0xff1f, 0));
  const void* punctuation_bitmap =
      lv_font_get_glyph_bitmap(&punctuation, nullptr);
  CHECK(punctuation_bitmap != nullptr);
  lv_font_glyph_release_draw_data(&punctuation);

  GenerationResults generation_results;
  qmeasure::FontSnapshotPublisher publisher(generation_results);
#if QUICKAPP_LVGL_EMBEDDED_PROFILE
  const auto profile_limits = qmeasure::embeddedFontMetricsLimits();
  const auto measure_limits = qmeasure::embeddedMeasureLimits();
#else
  const auto profile_limits = qmeasure::simulatorFontMetricsLimits();
  const auto measure_limits = qmeasure::simulatorMeasureLimits();
#endif
  const auto snapshot =
      qmeasure::FontMetricsSnapshot::makeV1(1, profile_limits.max_families);
  const auto* family = snapshot.findFamily(qfont::kSystemDefaultFontToken,
                                           qfont::kSystemDefaultFontWeight);
  CHECK(family != nullptr &&
        family->asset_digest.view() == qfont::systemDefaultFontDigest());
  CHECK(publisher.initialize(kOwner, snapshot).ok());
  qmeasure::FontMeasureAdapter measure(publisher, measure_limits);
  const qmeasure::MeasureRequest request{
      "req:profile-cjk", "srf:profile", "node:cjk", 1, 1,
      qmeasure::MeasureRole::kText, "中", qfont::kSystemDefaultFontToken,
      40, qfont::kSystemDefaultFontWeight,
      {qmeasure::ConstraintKind::kUnconstrained, 0},
      {qmeasure::ConstraintKind::kUnconstrained, 0}};
  const auto result = measure.measure(request);
  CHECK(result.measured);
  CHECK(std::abs(result.width - glyph.adv_w) < 0.01);
  CHECK(std::abs(result.height - native->line_height) < 1.0);

  // These are the characters added by the three Showcase applications. The
  // measure path and LVGL TinyTTF must agree for every glyph, not only "中".
  constexpr std::string_view kShowcaseGlyphs =
      "一下不专事二今任休作例保做关内划刷号同后地天始容巡工已布常异录待"
      "息扰排控摘日时显晨暂本板查样核检次正步段水注泵清版现留目看短确"
      "示站第行被要计认议记载进醒重间阀项饮";
  std::size_t cursor = 0;
  while (cursor < kShowcaseGlyphs.size()) {
    const std::size_t start = cursor;
    const auto first = static_cast<std::uint8_t>(kShowcaseGlyphs[cursor++]);
    CHECK((first & 0xf0U) == 0xe0U);
    CHECK(cursor + 1 < kShowcaseGlyphs.size());
    const auto second = static_cast<std::uint8_t>(kShowcaseGlyphs[cursor++]);
    const auto third = static_cast<std::uint8_t>(kShowcaseGlyphs[cursor++]);
    CHECK((second & 0xc0U) == 0x80U && (third & 0xc0U) == 0x80U);
    const std::uint32_t code_point =
        ((first & 0x0fU) << 12) | ((second & 0x3fU) << 6) | (third & 0x3fU);
    lv_font_glyph_dsc_t showcase_glyph{};
    CHECK(lv_font_get_glyph_dsc(native, &showcase_glyph, code_point, 0));
    CHECK(lv_font_get_glyph_bitmap(&showcase_glyph, nullptr) != nullptr);
    const qmeasure::MeasureRequest showcase_request{
        "req:showcase-glyph", "srf:profile", "node:showcase-glyph", 1, 1,
        qmeasure::MeasureRole::kText,
        kShowcaseGlyphs.substr(start, cursor - start),
        qfont::kSystemDefaultFontToken, 40, qfont::kSystemDefaultFontWeight,
        {qmeasure::ConstraintKind::kUnconstrained, 0},
        {qmeasure::ConstraintKind::kUnconstrained, 0}};
    const auto showcase_measure = measure.measure(showcase_request);
    CHECK(showcase_measure.measured);
    CHECK(std::abs(showcase_measure.width - showcase_glyph.adv_w) < 0.01);
    lv_font_glyph_release_draw_data(&showcase_glyph);
  }

  publisher.closeAdmission();
  CHECK(publisher.tryFinalizeClose(kOwner).ok());
  lv_tiny_ttf_destroy(native);
  lv_mem_monitor_t after{};
  lv_mem_monitor(&after);
  CHECK(after.free_size == before.free_size);
  lv_deinit();
  return true;
}

}  // namespace

int main() { return probe() ? 0 : 1; }
