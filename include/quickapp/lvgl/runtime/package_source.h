#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string_view>

#include "quickapp/lvgl/foundation/types.h"
#include "quickapp/lvgl/runtime/runtime_types.h"

namespace quickapp::lvgl::runtime {

class ImmutableBytes final {
 public:
  ImmutableBytes() noexcept = default;
  ImmutableBytes(ImmutableBytes&&) noexcept = default;
  ImmutableBytes& operator=(ImmutableBytes&&) noexcept = default;
  ImmutableBytes(const ImmutableBytes&) = delete;
  ImmutableBytes& operator=(const ImmutableBytes&) = delete;

  [[nodiscard]] static RuntimeCallResult allocate(
      std::size_t size, ImmutableBytes& output) noexcept;
  [[nodiscard]] static RuntimeCallResult copyOf(
      const std::byte* data, std::size_t size,
      ImmutableBytes& output) noexcept;

  [[nodiscard]] const std::byte* data() const noexcept { return data_.get(); }
  [[nodiscard]] std::byte* mutableData() noexcept { return data_.get(); }
  [[nodiscard]] std::size_t size() const noexcept { return size_; }
  [[nodiscard]] bool valid() const noexcept {
    return size_ == 0 || data_ != nullptr;
  }
  void reset() noexcept;

 private:
  std::unique_ptr<std::byte[]> data_;
  std::size_t size_{0};
};

struct PackageReadResult final {
  bool success{false};
  ImmutableBytes bytes;
  core::RuntimeErrorCode error{core::RuntimeErrorCode::kPackageIoError};

  [[nodiscard]] static PackageReadResult ok(ImmutableBytes&& value) noexcept;
  [[nodiscard]] static PackageReadResult fail(
      core::RuntimeErrorCode code =
          core::RuntimeErrorCode::kPackageIoError) noexcept;
};

using PackageReadCallback = void (*)(void* context,
                                     PackageReadResult&& result) noexcept;

struct PackageReadCompletion final {
  PackageReadCallback callback{nullptr};
  void* context{nullptr};
};

struct PackageReadDelivery final {
  PackageReadCompletion completion;
  PackageReadResult result;
};

enum class CompletionPostStatus : std::uint8_t {
  kAccepted,
  kBusy,
  kFull,
  kStopping,
};

class PackageCompletionPort {
 public:
  virtual ~PackageCompletionPort() = default;

  // On kAccepted, the implementation must move the delivery and clear it.
  [[nodiscard]] virtual CompletionPostStatus tryPost(
      PackageReadDelivery& delivery) noexcept = 0;
};

enum class PackageReadAdmission : std::uint8_t {
  kAccepted,
  kBusy,
  kCapacityExhausted,
  kClosed,
  kInvalid,
};

class PackageSource {
 public:
  virtual ~PackageSource() = default;
  [[nodiscard]] virtual std::uint64_t size() const noexcept = 0;
  [[nodiscard]] virtual PackageReadAdmission readAt(
      std::uint64_t offset, std::size_t length,
      PackageReadCompletion completion) noexcept = 0;
  [[nodiscard]] virtual std::size_t serviceCompletions(
      std::size_t max_count) noexcept = 0;
  [[nodiscard]] virtual foundation::LocalResult close() noexcept = 0;
  [[nodiscard]] virtual std::size_t inFlight() const noexcept = 0;
  [[nodiscard]] virtual bool closed() const noexcept = 0;
};

class MemoryPackageSource final : public PackageSource {
 public:
  MemoryPackageSource(ImmutableBytes&& storage,
                      PackageCompletionPort& completion_port) noexcept;

  [[nodiscard]] std::uint64_t size() const noexcept override;
  [[nodiscard]] PackageReadAdmission readAt(
      std::uint64_t offset, std::size_t length,
      PackageReadCompletion completion) noexcept override;
  [[nodiscard]] std::size_t serviceCompletions(
      std::size_t max_count) noexcept override;
  [[nodiscard]] foundation::LocalResult close() noexcept override;
  [[nodiscard]] std::size_t inFlight() const noexcept override;
  [[nodiscard]] bool closed() const noexcept override;

 private:
  [[nodiscard]] PackageReadAdmission deliver(
      PackageReadDelivery& delivery) noexcept;

  ImmutableBytes storage_;
  PackageCompletionPort* completion_port_;
  bool closed_{false};
};

class PackageSourceFactory {
 public:
  virtual ~PackageSourceFactory() = default;
  [[nodiscard]] virtual RuntimeCallResult create(
      std::string_view artifact,
      std::unique_ptr<PackageSource>& output) noexcept = 0;
};

class MemoryPackageSourceFactory final : public PackageSourceFactory {
 public:
  MemoryPackageSourceFactory(std::string_view artifact,
                             const std::byte* package_bytes,
                             std::size_t package_size,
                             PackageCompletionPort& completion_port) noexcept;

  [[nodiscard]] RuntimeCallResult create(
      std::string_view artifact,
      std::unique_ptr<PackageSource>& output) noexcept override;

 private:
  std::string_view artifact_;
  const std::byte* package_bytes_;
  std::size_t package_size_;
  PackageCompletionPort* completion_port_;
};

}  // namespace quickapp::lvgl::runtime
