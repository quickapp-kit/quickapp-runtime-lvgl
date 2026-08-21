#include "quickapp/lvgl/runtime/package_source.h"

#include <cstring>
#include <limits>
#include <new>
#include <utility>

namespace quickapp::lvgl::runtime {

RuntimeCallResult ImmutableBytes::allocate(std::size_t size,
                                           ImmutableBytes& output) noexcept {
  output.reset();
  if (size == 0) {
    return RuntimeCallResult::ok();
  }
  std::unique_ptr<std::byte[]> bytes(new (std::nothrow) std::byte[size]);
  if (bytes == nullptr) {
    return RuntimeCallResult::fail(core::RuntimeErrorCode::kOutOfMemory);
  }
  output.data_ = std::move(bytes);
  output.size_ = size;
  return RuntimeCallResult::ok();
}

RuntimeCallResult ImmutableBytes::copyOf(const std::byte* data,
                                         std::size_t size,
                                         ImmutableBytes& output) noexcept {
  if (size != 0 && data == nullptr) {
    return RuntimeCallResult::fail(
        core::RuntimeErrorCode::kAbiInvalidArgument);
  }
  const RuntimeCallResult allocation = allocate(size, output);
  if (!allocation.success) {
    return allocation;
  }
  if (size != 0) {
    std::memcpy(output.mutableData(), data, size);
  }
  return RuntimeCallResult::ok();
}

void ImmutableBytes::reset() noexcept {
  data_.reset();
  size_ = 0;
}

PackageReadResult PackageReadResult::ok(ImmutableBytes&& value) noexcept {
  PackageReadResult result;
  result.success = true;
  result.bytes = std::move(value);
  return result;
}

PackageReadResult PackageReadResult::fail(core::RuntimeErrorCode code) noexcept {
  PackageReadResult result;
  result.success = false;
  result.error = code;
  return result;
}

MemoryPackageSource::MemoryPackageSource(
    ImmutableBytes&& storage,
    PackageCompletionPort& completion_port) noexcept
    : storage_(std::move(storage)), completion_port_(&completion_port) {}

std::uint64_t MemoryPackageSource::size() const noexcept {
  return static_cast<std::uint64_t>(storage_.size());
}

PackageReadAdmission MemoryPackageSource::readAt(
    std::uint64_t offset, std::size_t length,
    PackageReadCompletion completion) noexcept {
  if (completion.callback == nullptr) {
    return PackageReadAdmission::kInvalid;
  }
  if (closed_) {
    PackageReadDelivery delivery{
        completion, PackageReadResult::fail()};
    const PackageReadAdmission admission = deliver(delivery);
    return admission == PackageReadAdmission::kAccepted
               ? PackageReadAdmission::kClosed
               : admission;
  }
  if (offset > size() || length > size() - offset) {
    PackageReadDelivery delivery{
        completion, PackageReadResult::fail()};
    return deliver(delivery);
  }

  ImmutableBytes bytes;
  const std::byte* source =
      length == 0
          ? nullptr
          : storage_.data() + static_cast<std::size_t>(offset);
  const RuntimeCallResult copy = ImmutableBytes::copyOf(source, length, bytes);
  PackageReadDelivery delivery{
      completion,
      copy.success ? PackageReadResult::ok(std::move(bytes))
                   : PackageReadResult::fail(copy.error)};
  return deliver(delivery);
}

std::size_t MemoryPackageSource::serviceCompletions(
    std::size_t) noexcept {
  return 0;
}

foundation::LocalResult MemoryPackageSource::close() noexcept {
  closed_ = true;
  return foundation::LocalResult::success();
}

std::size_t MemoryPackageSource::inFlight() const noexcept {
  return 0;
}

bool MemoryPackageSource::closed() const noexcept {
  return closed_;
}

PackageReadAdmission MemoryPackageSource::deliver(
    PackageReadDelivery& delivery) noexcept {
  switch (completion_port_->tryPost(delivery)) {
    case CompletionPostStatus::kAccepted:
      return PackageReadAdmission::kAccepted;
    case CompletionPostStatus::kBusy:
      return PackageReadAdmission::kBusy;
    case CompletionPostStatus::kFull:
      return PackageReadAdmission::kCapacityExhausted;
    case CompletionPostStatus::kStopping:
      return PackageReadAdmission::kClosed;
  }
  return PackageReadAdmission::kInvalid;
}

MemoryPackageSourceFactory::MemoryPackageSourceFactory(
    std::string_view artifact, const std::byte* package_bytes,
    std::size_t package_size,
    PackageCompletionPort& completion_port) noexcept
    : artifact_(artifact),
      package_bytes_(package_bytes),
      package_size_(package_size),
      completion_port_(&completion_port) {}

RuntimeCallResult MemoryPackageSourceFactory::create(
    std::string_view artifact,
    std::unique_ptr<PackageSource>& output) noexcept {
  output.reset();
  if (artifact.empty() || artifact != artifact_ ||
      (package_size_ != 0 && package_bytes_ == nullptr)) {
    return RuntimeCallResult::fail(
        core::RuntimeErrorCode::kPackageNotFound);
  }
  ImmutableBytes storage;
  const RuntimeCallResult copy = ImmutableBytes::copyOf(
      package_bytes_, package_size_, storage);
  if (!copy.success) {
    return copy;
  }
  auto* source = new (std::nothrow)
      MemoryPackageSource(std::move(storage), *completion_port_);
  if (source == nullptr) {
    return RuntimeCallResult::fail(core::RuntimeErrorCode::kOutOfMemory);
  }
  output.reset(source);
  return RuntimeCallResult::ok();
}

}  // namespace quickapp::lvgl::runtime
