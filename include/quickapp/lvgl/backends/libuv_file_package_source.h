#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

#include <uv.h>

#include "quickapp/lvgl/runtime/package_source.h"

namespace quickapp::lvgl::backends {

class LibuvFilePackageSource final : public runtime::PackageSource {
 public:
  static constexpr std::size_t kMaxOperations = 16;

  static runtime::RuntimeCallResult open(
      uv_loop_t& loop, const char* path, std::size_t max_in_flight,
      runtime::PackageCompletionPort& completion_port,
      std::unique_ptr<runtime::PackageSource>& output) noexcept;

  ~LibuvFilePackageSource() override;

  [[nodiscard]] std::uint64_t size() const noexcept override;
  [[nodiscard]] runtime::PackageReadAdmission readAt(
      std::uint64_t offset, std::size_t length,
      runtime::PackageReadCompletion completion) noexcept override;
  [[nodiscard]] std::size_t serviceCompletions(
      std::size_t max_count) noexcept override;
  [[nodiscard]] foundation::LocalResult close() noexcept override;
  [[nodiscard]] std::size_t inFlight() const noexcept override;
  [[nodiscard]] bool closed() const noexcept override;

 private:
  enum class OperationState : std::uint8_t {
    kFree,
    kReading,
    kPendingDelivery,
  };

  struct Operation final {
    LibuvFilePackageSource* owner{nullptr};
    uv_fs_t request{};
    uv_buf_t buffer{};
    std::size_t requested_length{0};
    OperationState state{OperationState::kFree};
    runtime::PackageReadCompletion completion{};
    runtime::ImmutableBytes bytes;
    runtime::PackageReadDelivery delivery{};
  };

  LibuvFilePackageSource(uv_loop_t& loop, uv_file file,
                         std::uint64_t file_size,
                         std::size_t max_in_flight,
                         runtime::PackageCompletionPort& completion_port)
      noexcept;

  [[nodiscard]] Operation* freeOperation() noexcept;
  [[nodiscard]] runtime::PackageReadAdmission deliverImmediate(
      runtime::PackageReadCompletion completion,
      runtime::PackageReadResult&& result,
      runtime::PackageReadAdmission accepted_status) noexcept;
  static void onRead(uv_fs_t* request) noexcept;

  uv_loop_t* loop_;
  uv_file file_;
  std::uint64_t file_size_;
  std::size_t max_in_flight_;
  runtime::PackageCompletionPort* completion_port_;
  std::array<Operation, kMaxOperations> operations_{};
  bool closing_{false};
  bool closed_{false};
};

class LibuvFilePackageSourceFactory final
    : public runtime::PackageSourceFactory {
 public:
  LibuvFilePackageSourceFactory(
      uv_loop_t& loop, std::size_t max_in_flight,
      runtime::PackageCompletionPort& completion_port) noexcept;

  [[nodiscard]] runtime::RuntimeCallResult create(
      std::string_view artifact,
      std::unique_ptr<runtime::PackageSource>& output) noexcept override;

 private:
  uv_loop_t* loop_;
  std::size_t max_in_flight_;
  runtime::PackageCompletionPort* completion_port_;
};

}  // namespace quickapp::lvgl::backends
