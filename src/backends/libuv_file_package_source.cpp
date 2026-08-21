#include "quickapp/lvgl/backends/libuv_file_package_source.h"

#include <cassert>
#include <climits>
#include <cstring>
#include <fcntl.h>
#include <limits>
#include <new>
#include <utility>

namespace quickapp::lvgl::backends {

runtime::RuntimeCallResult LibuvFilePackageSource::open(
    uv_loop_t& loop, const char* path, std::size_t max_in_flight,
    runtime::PackageCompletionPort& completion_port,
    std::unique_ptr<runtime::PackageSource>& output) noexcept {
  output.reset();
  if (path == nullptr || path[0] == '\0' || max_in_flight == 0 ||
      max_in_flight > kMaxOperations) {
    return runtime::RuntimeCallResult::fail(
        core::RuntimeErrorCode::kAbiInvalidArgument);
  }
  uv_fs_t open_request{};
  const int open_result =
      uv_fs_open(&loop, &open_request, path, O_RDONLY, 0, nullptr);
  uv_fs_req_cleanup(&open_request);
  if (open_result < 0) {
    return runtime::RuntimeCallResult::fail(
        open_result == UV_ENOENT ? core::RuntimeErrorCode::kPackageNotFound
                                 : core::RuntimeErrorCode::kPackageIoError);
  }

  uv_fs_t stat_request{};
  const int stat_result =
      uv_fs_fstat(&loop, &stat_request, open_result, nullptr);
  const std::int64_t signed_file_size = stat_request.statbuf.st_size;
  std::uint64_t file_size = 0;
  if (stat_result == 0 && signed_file_size >= 0) {
    file_size = static_cast<std::uint64_t>(signed_file_size);
  }
  uv_fs_req_cleanup(&stat_request);
  if (stat_result < 0 || signed_file_size < 0) {
    uv_fs_t close_request{};
    (void)uv_fs_close(&loop, &close_request, open_result, nullptr);
    uv_fs_req_cleanup(&close_request);
    return runtime::RuntimeCallResult::fail(
        core::RuntimeErrorCode::kPackageIoError);
  }

  auto* source = new (std::nothrow) LibuvFilePackageSource(
      loop, open_result, file_size, max_in_flight, completion_port);
  if (source == nullptr) {
    uv_fs_t close_request{};
    (void)uv_fs_close(&loop, &close_request, open_result, nullptr);
    uv_fs_req_cleanup(&close_request);
    return runtime::RuntimeCallResult::fail(
        core::RuntimeErrorCode::kOutOfMemory);
  }
  output.reset(source);
  return runtime::RuntimeCallResult::ok();
}

LibuvFilePackageSource::LibuvFilePackageSource(
    uv_loop_t& loop, uv_file file, std::uint64_t file_size,
    std::size_t max_in_flight,
    runtime::PackageCompletionPort& completion_port) noexcept
    : loop_(&loop),
      file_(file),
      file_size_(file_size),
      max_in_flight_(max_in_flight),
      completion_port_(&completion_port) {
  for (Operation& operation : operations_) {
    operation.owner = this;
    operation.request.data = &operation;
  }
}

LibuvFilePackageSource::~LibuvFilePackageSource() {
  assert(closed_ && inFlight() == 0 &&
         "LibuvFilePackageSource requires explicit close");
}

std::uint64_t LibuvFilePackageSource::size() const noexcept {
  return file_size_;
}

runtime::PackageReadAdmission LibuvFilePackageSource::readAt(
    std::uint64_t offset, std::size_t length,
    runtime::PackageReadCompletion completion) noexcept {
  if (completion.callback == nullptr) {
    return runtime::PackageReadAdmission::kInvalid;
  }
  if (closing_ || closed_) {
    return deliverImmediate(completion, runtime::PackageReadResult::fail(),
                            runtime::PackageReadAdmission::kClosed);
  }
  if (offset > file_size_ || length > file_size_ - offset ||
      length > UINT_MAX ||
      offset > static_cast<std::uint64_t>(
                   std::numeric_limits<std::int64_t>::max())) {
    return deliverImmediate(completion, runtime::PackageReadResult::fail(),
                            runtime::PackageReadAdmission::kAccepted);
  }
  if (length == 0) {
    runtime::ImmutableBytes empty;
    return deliverImmediate(completion,
                            runtime::PackageReadResult::ok(std::move(empty)),
                            runtime::PackageReadAdmission::kAccepted);
  }
  Operation* operation = freeOperation();
  if (operation == nullptr) {
    return runtime::PackageReadAdmission::kCapacityExhausted;
  }
  const runtime::RuntimeCallResult allocation =
      runtime::ImmutableBytes::allocate(length, operation->bytes);
  if (!allocation.success) {
    return deliverImmediate(completion,
                            runtime::PackageReadResult::fail(allocation.error),
                            runtime::PackageReadAdmission::kAccepted);
  }

  operation->completion = completion;
  operation->requested_length = length;
  operation->buffer = uv_buf_init(
      reinterpret_cast<char*>(operation->bytes.mutableData()),
      static_cast<unsigned int>(length));
  operation->state = OperationState::kReading;
  operation->request.data = operation;
  const int result = uv_fs_read(loop_, &operation->request, file_,
                                &operation->buffer, 1,
                                static_cast<std::int64_t>(offset),
                                &LibuvFilePackageSource::onRead);
  if (result < 0) {
    uv_fs_req_cleanup(&operation->request);
    operation->bytes.reset();
    operation->state = OperationState::kFree;
    return deliverImmediate(completion, runtime::PackageReadResult::fail(),
                            runtime::PackageReadAdmission::kAccepted);
  }
  return runtime::PackageReadAdmission::kAccepted;
}

std::size_t LibuvFilePackageSource::serviceCompletions(
    std::size_t max_count) noexcept {
  std::size_t delivered = 0;
  for (Operation& operation : operations_) {
    if (delivered == max_count) {
      break;
    }
    if (operation.state != OperationState::kPendingDelivery) {
      continue;
    }
    const runtime::CompletionPostStatus status =
        completion_port_->tryPost(operation.delivery);
    if (status == runtime::CompletionPostStatus::kAccepted) {
      operation.delivery = {};
      operation.completion = {};
      operation.state = OperationState::kFree;
      ++delivered;
    }
  }
  return delivered;
}

foundation::LocalResult LibuvFilePackageSource::close() noexcept {
  if (closed_) {
    return foundation::LocalResult::success();
  }
  closing_ = true;
  if (inFlight() != 0) {
    return foundation::LocalResult::failure(foundation::LocalError::kBusy);
  }
  uv_fs_t close_request{};
  const int result = uv_fs_close(loop_, &close_request, file_, nullptr);
  uv_fs_req_cleanup(&close_request);
  if (result < 0) {
    return foundation::LocalResult::failure(
        foundation::LocalError::kBackendFailed);
  }
  closed_ = true;
  return foundation::LocalResult::success();
}

std::size_t LibuvFilePackageSource::inFlight() const noexcept {
  std::size_t count = 0;
  for (const Operation& operation : operations_) {
    if (operation.state != OperationState::kFree) {
      ++count;
    }
  }
  return count;
}

bool LibuvFilePackageSource::closed() const noexcept {
  return closed_;
}

LibuvFilePackageSource::Operation*
LibuvFilePackageSource::freeOperation() noexcept {
  std::size_t active = 0;
  Operation* free = nullptr;
  for (Operation& operation : operations_) {
    if (operation.state == OperationState::kFree && free == nullptr) {
      free = &operation;
    } else if (operation.state != OperationState::kFree) {
      ++active;
    }
  }
  return active < max_in_flight_ ? free : nullptr;
}

runtime::PackageReadAdmission LibuvFilePackageSource::deliverImmediate(
    runtime::PackageReadCompletion completion,
    runtime::PackageReadResult&& result,
    runtime::PackageReadAdmission accepted_status) noexcept {
  runtime::PackageReadDelivery delivery{completion, std::move(result)};
  switch (completion_port_->tryPost(delivery)) {
    case runtime::CompletionPostStatus::kAccepted:
      return accepted_status;
    case runtime::CompletionPostStatus::kBusy:
      return runtime::PackageReadAdmission::kBusy;
    case runtime::CompletionPostStatus::kFull:
      return runtime::PackageReadAdmission::kCapacityExhausted;
    case runtime::CompletionPostStatus::kStopping:
      return runtime::PackageReadAdmission::kClosed;
  }
  return runtime::PackageReadAdmission::kInvalid;
}

void LibuvFilePackageSource::onRead(uv_fs_t* request) noexcept {
  auto* operation = static_cast<Operation*>(request->data);
  const bool complete = request->result >= 0 &&
                        static_cast<std::size_t>(request->result) ==
                            operation->requested_length;
  uv_fs_req_cleanup(request);
  operation->delivery.completion = operation->completion;
  if (complete) {
    operation->delivery.result =
        runtime::PackageReadResult::ok(std::move(operation->bytes));
  } else {
    operation->bytes.reset();
    operation->delivery.result = runtime::PackageReadResult::fail();
  }
  operation->state = OperationState::kPendingDelivery;
}

LibuvFilePackageSourceFactory::LibuvFilePackageSourceFactory(
    uv_loop_t& loop, std::size_t max_in_flight,
    runtime::PackageCompletionPort& completion_port) noexcept
    : loop_(&loop),
      max_in_flight_(max_in_flight),
      completion_port_(&completion_port) {}

runtime::RuntimeCallResult LibuvFilePackageSourceFactory::create(
    std::string_view artifact,
    std::unique_ptr<runtime::PackageSource>& output) noexcept {
  output.reset();
  if (artifact.empty() || artifact.size() >= 1024) {
    return runtime::RuntimeCallResult::fail(
        core::RuntimeErrorCode::kPackageNotFound);
  }
  char path[1024]{};
  std::memcpy(path, artifact.data(), artifact.size());
  path[artifact.size()] = '\0';
  return LibuvFilePackageSource::open(*loop_, path, max_in_flight_,
                                      *completion_port_, output);
}

}  // namespace quickapp::lvgl::backends
