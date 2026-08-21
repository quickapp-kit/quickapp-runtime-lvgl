#include "quickapp/lvgl/event/input_adapter.h"

#include <array>
#include <string>

namespace quickapp::lvgl::event {

foundation::LocalResult SdlInputAdapter::pump(foundation::OwnerToken owner,
                                             std::size_t budget) noexcept {
  if (budget == 0) return foundation::LocalResult::success();
  std::array<foundation::RawInputSample, 32> samples{};
  const auto drained = raw_.drain(owner, samples.data(),
                                  std::min(budget, samples.size()));
  if (!drained.ok()) return foundation::LocalResult::failure(drained.error);
  for (std::size_t index = 0; index < drained.sample_count; ++index) {
    const auto& sample = samples[index];
    if (sample.action != foundation::RawInputAction::kUp) continue;
    const auto target = mounts_.nodeAt(surface_, sample.physical_x,
                                       sample.physical_y);
    if (!target) continue;
    auto request = core::RequestId::parse(
        "req:p-" + std::to_string(sample.sample_sequence));
    if (!request) return foundation::LocalResult::failure(
        foundation::LocalError::kCapacityExhausted);
    auto posted = sink_.post(core::event::PlatformInputMessage{
        std::move(request).value(), surface_, target.value(),
        core::package::EventType::kClick, sample.timestamp_ns, {}});
    if (!posted) return foundation::LocalResult::failure(
        foundation::LocalError::kCapacityExhausted);
  }
  return foundation::LocalResult::success();
}

}  // namespace quickapp::lvgl::event
