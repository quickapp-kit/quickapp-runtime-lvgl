#pragma once

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <string_view>

#include "quickapp/core/foundation/port.h"
#include "quickapp/lvgl/foundation/types.h"

namespace quickapp::lvgl::measure {

constexpr std::size_t kMaxFontFamilies = 16;
constexpr std::size_t kMaxFontRanges = 32;
constexpr std::size_t kMaxKerningPairs = 64;
constexpr std::size_t kFontTokenBytes = 64;
constexpr std::size_t kAssetDigestBytes = 65;

template <std::size_t Capacity>
struct FixedText final {
  std::array<char, Capacity> bytes{};
  std::uint16_t length{0};

  [[nodiscard]] bool assign(std::string_view value) noexcept {
    if (value.size() > Capacity || value.size() > UINT16_MAX) {
      return false;
    }
    for (std::size_t index = 0; index < value.size(); ++index) {
      bytes[index] = value[index];
    }
    length = static_cast<std::uint16_t>(value.size());
    return true;
  }

  [[nodiscard]] std::string_view view() const noexcept {
    return {bytes.data(), length};
  }
};

using FontToken = FixedText<kFontTokenBytes>;
using AssetDigest = FixedText<kAssetDigestBytes>;

struct FontMetricsProfileLimits final {
  std::size_t max_families{16};
};

[[nodiscard]] FontMetricsProfileLimits simulatorFontMetricsLimits() noexcept;
[[nodiscard]] FontMetricsProfileLimits embeddedFontMetricsLimits() noexcept;

struct GlyphRange final {
  std::uint32_t first{0};
  std::uint32_t last{0};
  std::int32_t advance_design_units{0};
};

struct KerningPair final {
  std::uint32_t left{0};
  std::uint32_t right{0};
  std::int32_t adjustment_design_units{0};
};

struct FontFamilyMetrics final {
  FontToken token;
  AssetDigest asset_digest;
  std::uint16_t weight{400};
  std::uint32_t units_per_em{1000};
  std::int32_t line_height_design_units{1448};
  std::int32_t baseline_design_units{900};
  std::int32_t space_advance_design_units{300};
  std::array<GlyphRange, kMaxFontRanges> ranges{};
  std::size_t range_count{0};
  std::array<KerningPair, kMaxKerningPairs> kerning{};
  std::size_t kerning_count{0};

  [[nodiscard]] bool addRange(GlyphRange range) noexcept;
  [[nodiscard]] bool addKerning(KerningPair pair) noexcept;
  [[nodiscard]] bool contains(std::uint32_t code_point) const noexcept;
  [[nodiscard]] std::int32_t advance(std::uint32_t code_point) const noexcept;
  [[nodiscard]] std::int32_t kerningAdjustment(std::uint32_t left,
                                               std::uint32_t right) const noexcept;
};

class FontMetricsSnapshot final {
 public:
  static FontMetricsSnapshot makeV1(
      std::uint64_t generation,
      std::size_t max_families = kMaxFontFamilies) noexcept;

  [[nodiscard]] bool addFamily(FontFamilyMetrics family) noexcept;
  [[nodiscard]] const FontFamilyMetrics* findFamily(
      std::string_view token, std::uint16_t weight) const noexcept;
  [[nodiscard]] std::uint64_t generation() const noexcept { return generation_; }
  [[nodiscard]] std::size_t maxFamilies() const noexcept {
    return max_families_;
  }
  void setGeneration(std::uint64_t generation) noexcept { generation_ = generation; }

 private:
  std::uint64_t generation_{0};
  std::size_t max_families_{kMaxFontFamilies};
  std::array<FontFamilyMetrics, kMaxFontFamilies> families_{};
  std::size_t family_count_{0};
};

struct PlatformFontGenerationChanged final {
  std::uint64_t platform_font_generation{0};
};

class FontSnapshotPublisher final {
 public:
  class ReadGuard final {
   public:
    ReadGuard() noexcept = default;
    ReadGuard(const ReadGuard&) = delete;
    ReadGuard& operator=(const ReadGuard&) = delete;
    ReadGuard(ReadGuard&& other) noexcept { moveFrom(other); }
    ReadGuard& operator=(ReadGuard&& other) noexcept {
      if (this != &other) {
        release();
        moveFrom(other);
      }
      return *this;
    }
    ~ReadGuard() noexcept { release(); }

    [[nodiscard]] const FontMetricsSnapshot* get() const noexcept {
      return snapshot_;
    }

   private:
    friend class FontSnapshotPublisher;
    ReadGuard(FontSnapshotPublisher* owner, std::size_t slot,
              const FontMetricsSnapshot* snapshot) noexcept
        : owner_(owner), slot_(slot), snapshot_(snapshot) {}
    void moveFrom(ReadGuard& other) noexcept {
      owner_ = other.owner_;
      slot_ = other.slot_;
      snapshot_ = other.snapshot_;
      other.owner_ = nullptr;
      other.snapshot_ = nullptr;
    }
    void release() noexcept;

    FontSnapshotPublisher* owner_{nullptr};
    std::size_t slot_{0};
    const FontMetricsSnapshot* snapshot_{nullptr};
  };

  explicit FontSnapshotPublisher(
      core::CoreIngressPort<PlatformFontGenerationChanged>& notifications) noexcept;

  [[nodiscard]] foundation::LocalResult initialize(
      foundation::OwnerToken owner, FontMetricsSnapshot initial) noexcept;
  [[nodiscard]] foundation::LocalResult acquire(ReadGuard& guard) noexcept;
  [[nodiscard]] foundation::LocalResult publish(
      foundation::OwnerToken owner, FontMetricsSnapshot next) noexcept;
  [[nodiscard]] foundation::LocalResult serviceNotifications(
      foundation::OwnerToken owner) noexcept;
  [[nodiscard]] foundation::LocalResult tryFinalizeClose(
      foundation::OwnerToken owner) noexcept;
  void closeAdmission() noexcept;

  [[nodiscard]] std::uint64_t generation() const noexcept {
    return generation_.load(std::memory_order_acquire);
  }
  [[nodiscard]] bool closed() const noexcept {
    return closed_.load(std::memory_order_acquire);
  }
  [[nodiscard]] std::size_t pendingReaders() const noexcept;
  [[nodiscard]] bool notificationPending() const noexcept {
    return notification_pending_.load(std::memory_order_acquire);
  }

 private:
  void release(std::size_t slot) noexcept;
  [[nodiscard]] bool isOwner(foundation::OwnerToken owner) const noexcept;

  core::CoreIngressPort<PlatformFontGenerationChanged>& notifications_;
  FontMetricsSnapshot snapshots_[2]{};
  std::atomic<std::uint8_t> active_slot_{0};
  std::atomic<std::size_t> readers_[2]{};
  std::atomic<std::uint64_t> generation_{0};
  std::atomic<bool> initialized_{false};
  std::atomic<bool> accepting_{true};
  std::atomic<bool> closed_{false};
  std::atomic<bool> notification_pending_{false};
  foundation::OwnerToken owner_{};
  std::size_t max_families_{0};
};

}  // namespace quickapp::lvgl::measure
