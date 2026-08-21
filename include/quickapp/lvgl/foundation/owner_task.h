#pragma once

#include <cstddef>
#include <new>
#include <type_traits>
#include <utility>

namespace quickapp::lvgl::foundation {

class OwnerTask final {
 public:
  static constexpr std::size_t kInlineBytes = 64;

  OwnerTask() noexcept = default;
  OwnerTask(const OwnerTask&) = delete;
  OwnerTask& operator=(const OwnerTask&) = delete;

  OwnerTask(OwnerTask&& other) noexcept { moveFrom(other); }

  OwnerTask& operator=(OwnerTask&& other) noexcept {
    if (this != &other) {
      reset();
      moveFrom(other);
    }
    return *this;
  }

  ~OwnerTask() noexcept { reset(); }

  template <typename Callable>
  [[nodiscard]] static OwnerTask make(Callable&& callable) noexcept {
    using Stored = std::decay_t<Callable>;
    static_assert(sizeof(Stored) <= kInlineBytes,
                  "OwnerTask callable exceeds inline storage");
    static_assert(alignof(Stored) <= alignof(std::max_align_t),
                  "OwnerTask callable alignment is unsupported");
    static_assert(std::is_nothrow_move_constructible_v<Stored>,
                  "OwnerTask callable must be nothrow move constructible");
    static_assert(std::is_nothrow_invocable_v<Stored&>,
                  "OwnerTask callable must be noexcept");

    OwnerTask task;
    new (task.storage_) Stored(std::forward<Callable>(callable));
    task.invoke_ = [](void* storage) noexcept {
      (*static_cast<Stored*>(storage))();
    };
    task.destroy_ = [](void* storage) noexcept {
      static_cast<Stored*>(storage)->~Stored();
    };
    task.move_ = [](void* destination, void* source) noexcept {
      auto* stored_source = static_cast<Stored*>(source);
      new (destination) Stored(std::move(*stored_source));
      stored_source->~Stored();
    };
    return task;
  }

  [[nodiscard]] bool valid() const noexcept { return invoke_ != nullptr; }

  void run() noexcept {
    if (invoke_ == nullptr) {
      return;
    }
    Invoke invoke = invoke_;
    invoke(storage_);
    reset();
  }

  void reset() noexcept {
    if (destroy_ != nullptr) {
      destroy_(storage_);
    }
    invoke_ = nullptr;
    destroy_ = nullptr;
    move_ = nullptr;
  }

 private:
  using Invoke = void (*)(void*) noexcept;
  using Destroy = void (*)(void*) noexcept;
  using Move = void (*)(void*, void*) noexcept;

  void moveFrom(OwnerTask& other) noexcept {
    if (other.move_ == nullptr) {
      return;
    }
    other.move_(storage_, other.storage_);
    invoke_ = other.invoke_;
    destroy_ = other.destroy_;
    move_ = other.move_;
    other.invoke_ = nullptr;
    other.destroy_ = nullptr;
    other.move_ = nullptr;
  }

  alignas(std::max_align_t) std::byte storage_[kInlineBytes]{};
  Invoke invoke_{nullptr};
  Destroy destroy_{nullptr};
  Move move_{nullptr};
};

}  // namespace quickapp::lvgl::foundation
