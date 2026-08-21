#pragma once

#include <atomic>

namespace quickapp::lvgl::foundation {

class TryCriticalSection {
 public:
  virtual ~TryCriticalSection() = default;
  [[nodiscard]] virtual bool tryEnter() noexcept = 0;
  virtual void leave() noexcept = 0;
};

class AtomicTryCriticalSection final : public TryCriticalSection {
 public:
  [[nodiscard]] bool tryEnter() noexcept override {
    return !held_.test_and_set(std::memory_order_acquire);
  }

  void leave() noexcept override {
    held_.clear(std::memory_order_release);
  }

 private:
  std::atomic_flag held_ = ATOMIC_FLAG_INIT;
};

class TryCriticalSectionGuard final {
 public:
  explicit TryCriticalSectionGuard(TryCriticalSection& section) noexcept
      : section_(section), acquired_(section_.tryEnter()) {}

  ~TryCriticalSectionGuard() noexcept {
    if (acquired_) {
      section_.leave();
    }
  }

  TryCriticalSectionGuard(const TryCriticalSectionGuard&) = delete;
  TryCriticalSectionGuard& operator=(const TryCriticalSectionGuard&) = delete;

  [[nodiscard]] bool acquired() const noexcept { return acquired_; }

 private:
  TryCriticalSection& section_;
  bool acquired_{false};
};

}  // namespace quickapp::lvgl::foundation
