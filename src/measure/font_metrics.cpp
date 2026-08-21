#include "quickapp/lvgl/measure/font_metrics.h"

#include <algorithm>
#include <limits>

#include "quickapp/lvgl/font/system_default_font_asset.h"

namespace quickapp::lvgl::measure {

FontMetricsProfileLimits simulatorFontMetricsLimits() noexcept { return {16}; }

FontMetricsProfileLimits embeddedFontMetricsLimits() noexcept { return {4}; }

bool FontFamilyMetrics::addRange(GlyphRange range) noexcept {
  if (range.first > range.last || range.advance_design_units < 0 ||
      range_count == ranges.size()) {
    return false;
  }
  ranges[range_count++] = range;
  return true;
}

bool FontFamilyMetrics::addKerning(KerningPair pair) noexcept {
  if (kerning_count == kerning.size()) {
    return false;
  }
  kerning[kerning_count++] = pair;
  return true;
}

bool FontFamilyMetrics::contains(std::uint32_t code_point) const noexcept {
  for (std::size_t index = 0; index < range_count; ++index) {
    if (code_point >= ranges[index].first && code_point <= ranges[index].last) {
      return true;
    }
  }
  return false;
}

std::int32_t FontFamilyMetrics::advance(
    std::uint32_t code_point) const noexcept {
  for (std::size_t index = 0; index < range_count; ++index) {
    if (code_point >= ranges[index].first && code_point <= ranges[index].last) {
      return ranges[index].advance_design_units;
    }
  }
  return 0;
}

std::int32_t FontFamilyMetrics::kerningAdjustment(
    std::uint32_t left, std::uint32_t right) const noexcept {
  for (std::size_t index = 0; index < kerning_count; ++index) {
    if (kerning[index].left == left && kerning[index].right == right) {
      return kerning[index].adjustment_design_units;
    }
  }
  return 0;
}

FontMetricsSnapshot FontMetricsSnapshot::makeV1(
    std::uint64_t generation, std::size_t max_families) noexcept {
  FontMetricsSnapshot snapshot;
  snapshot.generation_ = generation == 0 ? 1 : generation;
  snapshot.max_families_ =
      std::min(max_families, static_cast<std::size_t>(kMaxFontFamilies));
  FontFamilyMetrics family;
  (void)family.token.assign(font::kSystemDefaultFontToken);
  (void)family.asset_digest.assign(font::systemDefaultFontDigest());
  family.weight = font::kSystemDefaultFontWeight;
  family.units_per_em = 1000;
  family.line_height_design_units = 1448;
  family.baseline_design_units = 900;
  family.space_advance_design_units = 300;
  (void)family.addRange({0x20, 0x20, 300});
  (void)family.addRange({0x21, 0x7e, 600});
  (void)family.addRange({0x2000, 0x206f, 1000});
  (void)family.addRange({0x3000, 0x303f, 1000});
  (void)family.addRange({0x3400, 0x4dbf, 1000});
  (void)family.addRange({0x4e00, 0x9fff, 1000});
  (void)family.addRange({0xff01, 0xff5e, 1000});
  (void)snapshot.addFamily(family);
  return snapshot;
}

bool FontMetricsSnapshot::addFamily(FontFamilyMetrics family) noexcept {
  if (family.token.length == 0 || family.units_per_em == 0 ||
      family.line_height_design_units < 0 ||
      family_count_ == families_.size() || family_count_ == max_families_) {
    return false;
  }
  if (findFamily(family.token.view(), family.weight) != nullptr) {
    return false;
  }
  families_[family_count_++] = family;
  return true;
}

const FontFamilyMetrics* FontMetricsSnapshot::findFamily(
    std::string_view token, std::uint16_t weight) const noexcept {
  for (std::size_t index = 0; index < family_count_; ++index) {
    if (families_[index].weight == weight &&
        families_[index].token.view() == token) {
      return &families_[index];
    }
  }
  return nullptr;
}

FontSnapshotPublisher::FontSnapshotPublisher(
    core::CoreIngressPort<PlatformFontGenerationChanged>& notifications) noexcept
    : notifications_(notifications) {}

foundation::LocalResult FontSnapshotPublisher::initialize(
    foundation::OwnerToken owner, FontMetricsSnapshot initial) noexcept {
  if (!owner.valid() || initial.generation() == 0 ||
      initialized_.load(std::memory_order_acquire)) {
    return foundation::LocalResult::failure(foundation::LocalError::kInvalidState);
  }
  owner_ = owner;
  max_families_ = initial.maxFamilies();
  snapshots_[0] = initial;
  snapshots_[1] = FontMetricsSnapshot{};
  active_slot_.store(0, std::memory_order_release);
  generation_.store(initial.generation(), std::memory_order_release);
  initialized_.store(true, std::memory_order_release);
  return foundation::LocalResult::success();
}

bool FontSnapshotPublisher::isOwner(foundation::OwnerToken owner) const noexcept {
  return owner.valid() && owner == owner_;
}

foundation::LocalResult FontSnapshotPublisher::acquire(ReadGuard& guard) noexcept {
  if (guard.get() != nullptr || !initialized_.load(std::memory_order_acquire) ||
      closed_.load(std::memory_order_acquire)) {
    return foundation::LocalResult::failure(foundation::LocalError::kInvalidState);
  }
  for (std::size_t attempt = 0; attempt < 2; ++attempt) {
    const std::size_t slot = active_slot_.load(std::memory_order_acquire);
    readers_[slot].fetch_add(1, std::memory_order_acquire);
    if (slot == active_slot_.load(std::memory_order_acquire) &&
        !closed_.load(std::memory_order_acquire)) {
      guard = ReadGuard(this, slot, &snapshots_[slot]);
      return foundation::LocalResult::success();
    }
    readers_[slot].fetch_sub(1, std::memory_order_release);
  }
  return foundation::LocalResult::failure(foundation::LocalError::kBusy);
}

foundation::LocalResult FontSnapshotPublisher::publish(
    foundation::OwnerToken owner, FontMetricsSnapshot next) noexcept {
  if (!isOwner(owner)) {
    return foundation::LocalResult::failure(foundation::LocalError::kWrongThread);
  }
  if (!accepting_.load(std::memory_order_acquire) ||
      !initialized_.load(std::memory_order_acquire) ||
      closed_.load(std::memory_order_acquire) ||
      next.maxFamilies() != max_families_) {
    return foundation::LocalResult::failure(foundation::LocalError::kInvalidState);
  }
  if (notification_pending_.load(std::memory_order_acquire)) {
    return foundation::LocalResult::failure(foundation::LocalError::kBusy);
  }
  const std::size_t active = active_slot_.load(std::memory_order_acquire);
  const std::size_t next_slot = 1U - active;
  if (readers_[next_slot].load(std::memory_order_acquire) != 0) {
    return foundation::LocalResult::failure(foundation::LocalError::kBusy);
  }
  const std::uint64_t current = generation_.load(std::memory_order_acquire);
  if (current == std::numeric_limits<std::uint64_t>::max()) {
    return foundation::LocalResult::failure(foundation::LocalError::kInvalidState);
  }
  next.setGeneration(current + 1);
  snapshots_[next_slot] = next;
  active_slot_.store(static_cast<std::uint8_t>(next_slot),
                    std::memory_order_release);
  generation_.store(current + 1, std::memory_order_release);
  notification_pending_.store(true, std::memory_order_release);
  (void)serviceNotifications(owner);
  return foundation::LocalResult::success();
}

foundation::LocalResult FontSnapshotPublisher::serviceNotifications(
    foundation::OwnerToken owner) noexcept {
  if (!isOwner(owner)) {
    return foundation::LocalResult::failure(foundation::LocalError::kWrongThread);
  }
  if (!notification_pending_.load(std::memory_order_acquire)) {
    return foundation::LocalResult::success();
  }
  PlatformFontGenerationChanged notification{generation()};
  const core::EnqueueResult result = notifications_.post(std::move(notification));
  if (result) {
    notification_pending_.store(false, std::memory_order_release);
    return foundation::LocalResult::success();
  }
  if (result.error().code == core::RuntimeErrorCode::kQueueOverflow) {
    return foundation::LocalResult::failure(foundation::LocalError::kBusy);
  }
  notification_pending_.store(false, std::memory_order_release);
  return foundation::LocalResult::failure(foundation::LocalError::kBackendFailed);
}

foundation::LocalResult FontSnapshotPublisher::tryFinalizeClose(
    foundation::OwnerToken owner) noexcept {
  if (!isOwner(owner)) {
    return foundation::LocalResult::failure(foundation::LocalError::kWrongThread);
  }
  closeAdmission();
  (void)serviceNotifications(owner);
  if (notification_pending_.load(std::memory_order_acquire) ||
      pendingReaders() != 0) {
    return foundation::LocalResult::failure(foundation::LocalError::kBusy);
  }
  closed_.store(true, std::memory_order_release);
  snapshots_[0] = FontMetricsSnapshot{};
  snapshots_[1] = FontMetricsSnapshot{};
  initialized_.store(false, std::memory_order_release);
  return foundation::LocalResult::success();
}

void FontSnapshotPublisher::closeAdmission() noexcept {
  accepting_.store(false, std::memory_order_release);
}

std::size_t FontSnapshotPublisher::pendingReaders() const noexcept {
  return readers_[0].load(std::memory_order_acquire) +
         readers_[1].load(std::memory_order_acquire);
}

void FontSnapshotPublisher::release(std::size_t slot) noexcept {
  readers_[slot].fetch_sub(1, std::memory_order_release);
}

void FontSnapshotPublisher::ReadGuard::release() noexcept {
  if (owner_ != nullptr) {
    owner_->release(slot_);
    owner_ = nullptr;
    snapshot_ = nullptr;
  }
}

}  // namespace quickapp::lvgl::measure
