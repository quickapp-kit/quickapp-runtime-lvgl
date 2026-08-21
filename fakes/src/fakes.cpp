#include "quickapp/lvgl/foundation/fakes.h"

#include <algorithm>
#include <limits>
#include <utility>

namespace quickapp::lvgl::foundation::fakes {
namespace {

struct FakeCallable {
  FakeTaskState* state{nullptr};
  bool armed{true};

  explicit FakeCallable(FakeTaskState& target) noexcept : state(&target) {}
  FakeCallable(const FakeCallable&) = delete;
  FakeCallable& operator=(const FakeCallable&) = delete;
  FakeCallable(FakeCallable&& other) noexcept
      : state(other.state), armed(other.armed) {
    other.armed = false;
  }
  FakeCallable& operator=(FakeCallable&&) = delete;

  ~FakeCallable() noexcept {
    if (armed && state != nullptr) {
      ++state->destructions;
      if (state->current_owner != nullptr) {
        state->destruction_owner =
            state->current_owner(state->current_owner_context);
      }
    }
  }

  void operator()() noexcept {
    if (state != nullptr) {
      ++state->executions;
    }
  }
};

std::size_t bytesPerPixel(PixelFormat format) noexcept {
  switch (format) {
    case PixelFormat::kRgb565:
      return 2;
    case PixelFormat::kRgb888:
      return 3;
    case PixelFormat::kRgba8888:
      return 4;
  }
  return 0;
}

}  // namespace

FakeClock::FakeClock(std::uint64_t resolution_ns) noexcept
    : resolution_ns_(resolution_ns == 0 ? 1 : resolution_ns) {}

std::uint64_t FakeClock::nowNs() const noexcept {
  return now_ns_.load(std::memory_order_relaxed);
}

std::uint64_t FakeClock::resolutionNs() const noexcept {
  return resolution_ns_;
}

LocalResult FakeClock::setNowNs(std::uint64_t value) noexcept {
  std::uint64_t current = nowNs();
  if (value < current) {
    return LocalResult::failure(LocalError::kInvalidArgument);
  }
  return now_ns_.compare_exchange_strong(current, value,
                                         std::memory_order_relaxed,
                                         std::memory_order_relaxed)
             ? LocalResult::success()
             : LocalResult::failure(LocalError::kBusy);
}

LocalResult FakeClock::advanceNs(std::uint64_t delta) noexcept {
  std::uint64_t current = nowNs();
  if (delta > std::numeric_limits<std::uint64_t>::max() - current) {
    return LocalResult::failure(LocalError::kInvalidArgument);
  }
  return now_ns_.compare_exchange_strong(current, current + delta,
                                         std::memory_order_relaxed,
                                         std::memory_order_relaxed)
             ? LocalResult::success()
             : LocalResult::failure(LocalError::kBusy);
}

FakeWakeup::FakeWakeup(OwnerToken owner, bool wait_supported) noexcept
    : owner_(owner), wait_supported_(wait_supported) {}

WakeResult FakeWakeup::notify() noexcept {
  if (closed()) {
    return WakeResult::kStopping;
  }
  notify_count_.fetch_add(1, std::memory_order_relaxed);
  return notify_result_.load(std::memory_order_relaxed);
}

WakeResult FakeWakeup::waitUntil(OwnerToken caller,
                                 std::uint64_t deadline_ns) noexcept {
  if (caller != owner_) {
    return WakeResult::kWrongThread;
  }
  if (closed() || stopping_.load(std::memory_order_relaxed)) {
    return WakeResult::kStopping;
  }
  if (!wait_supported_) {
    return WakeResult::kUnsupported;
  }
  wait_count_.fetch_add(1, std::memory_order_relaxed);
  last_deadline_ns_.store(deadline_ns, std::memory_order_relaxed);
  return next_wait_result_.exchange(WakeResult::kDeadline,
                                    std::memory_order_relaxed);
}

void FakeWakeup::requestStop() noexcept {
  stopping_.store(true, std::memory_order_relaxed);
}

LocalResult FakeWakeup::close(OwnerToken caller) noexcept {
  if (caller != owner_) {
    return LocalResult::failure(LocalError::kWrongThread);
  }
  closed_.store(true, std::memory_order_relaxed);
  stopping_.store(true, std::memory_order_relaxed);
  return LocalResult::success();
}

void FakeWakeup::setNotifyResult(WakeResult result) noexcept {
  notify_result_.store(result, std::memory_order_relaxed);
}

void FakeWakeup::setNextWaitResult(WakeResult result) noexcept {
  next_wait_result_.store(result, std::memory_order_relaxed);
}

std::uint64_t FakeWakeup::notifyCount() const noexcept {
  return notify_count_.load(std::memory_order_relaxed);
}

std::uint64_t FakeWakeup::waitCount() const noexcept {
  return wait_count_.load(std::memory_order_relaxed);
}

std::uint64_t FakeWakeup::lastDeadlineNs() const noexcept {
  return last_deadline_ns_.load(std::memory_order_relaxed);
}

bool FakeWakeup::closed() const noexcept {
  return closed_.load(std::memory_order_relaxed);
}

OwnerTask makeFakeTask(FakeTaskState& state) noexcept {
  return OwnerTask::make(FakeCallable(state));
}

FakeDisplay::FakeDisplay(OwnerToken owner,
                         DisplayCapabilities capabilities) noexcept
    : owner_(owner), capabilities_(capabilities) {}

LocalResult FakeDisplay::open(OwnerToken caller, const DisplayConfig& config,
                              DisplayCapabilities& capabilities) noexcept {
  if (caller != owner_) {
    return LocalResult::failure(LocalError::kWrongThread);
  }
  if (state_ != LifecycleState::kConstructed) {
    return LocalResult::failure(LocalError::kInvalidState);
  }
  if (config.physical_width_px == 0 || config.physical_height_px == 0 ||
      config.max_dirty_regions == 0 ||
      capabilities_.physical_width_px == 0 ||
      capabilities_.physical_height_px == 0 ||
      capabilities_.max_dirty_regions == 0) {
    state_ = LifecycleState::kClosed;
    return LocalResult::failure(LocalError::kInvalidArgument);
  }
  if (faults_.fail_open) {
    state_ = LifecycleState::kClosed;
    return LocalResult::failure(LocalError::kBackendFailed);
  }
  capabilities = capabilities_;
  state_ = LifecycleState::kRunning;
  return LocalResult::success();
}

LocalResult FakeDisplay::present(OwnerToken caller,
                                 const DisplayFrameView& frame) noexcept {
  if (caller != owner_) {
    return LocalResult::failure(LocalError::kWrongThread);
  }
  if (state_ != LifecycleState::kRunning) {
    return LocalResult::failure(LocalError::kInvalidState);
  }
  const LocalResult validation = validateFrame(frame);
  if (!validation.ok()) {
    return validation;
  }

  ++present_count_;
  if (faults_.fail_present_call != 0 &&
      present_count_ == faults_.fail_present_call) {
    return LocalResult::failure(LocalError::kBackendFailed);
  }

  std::uint64_t checksum = 1469598103934665603ULL;
  for (std::size_t i = 0; i < frame.byte_length; ++i) {
    checksum ^= std::to_integer<std::uint8_t>(frame.pixels[i]);
    checksum *= 1099511628211ULL;
  }
  last_frame_checksum_ = checksum;
  last_frame_sequence_ = frame.frame_sequence;
  return LocalResult::success();
}

LocalResult FakeDisplay::close(OwnerToken caller) noexcept {
  if (caller != owner_) {
    return LocalResult::failure(LocalError::kWrongThread);
  }
  if (state_ == LifecycleState::kClosed) {
    return LocalResult::success();
  }
  ++close_count_;
  state_ = LifecycleState::kClosed;
  if (faults_.fail_close) {
    return LocalResult::failure(LocalError::kBackendFailed);
  }
  return LocalResult::success();
}

LifecycleState FakeDisplay::state() const noexcept {
  return state_;
}

void FakeDisplay::setFaults(FakeDisplayFaults faults) noexcept {
  faults_ = faults;
}

std::size_t FakeDisplay::presentCount() const noexcept {
  return present_count_;
}

std::uint64_t FakeDisplay::lastFrameChecksum() const noexcept {
  return last_frame_checksum_;
}

std::uint64_t FakeDisplay::lastFrameSequence() const noexcept {
  return last_frame_sequence_;
}

std::size_t FakeDisplay::closeCount() const noexcept {
  return close_count_;
}

LocalResult FakeDisplay::validateFrame(
    const DisplayFrameView& frame) const noexcept {
  const std::size_t bytes_per_pixel = bytesPerPixel(frame.pixel_format);
  if (frame.pixels == nullptr || frame.width_px == 0 || frame.height_px == 0 ||
      bytes_per_pixel == 0 || frame.dirty_region_count == 0 ||
      frame.dirty_regions == nullptr ||
      frame.dirty_region_count > capabilities_.max_dirty_regions ||
      frame.width_px > capabilities_.physical_width_px ||
      frame.height_px > capabilities_.physical_height_px ||
      frame.pixel_format != capabilities_.pixel_format) {
    return LocalResult::failure(LocalError::kInvalidArgument);
  }
  const std::size_t minimum_stride =
      static_cast<std::size_t>(frame.width_px) * bytes_per_pixel;
  if (frame.stride_bytes < minimum_stride ||
      frame.height_px >
          std::numeric_limits<std::size_t>::max() / frame.stride_bytes ||
      frame.byte_length <
          frame.stride_bytes * static_cast<std::size_t>(frame.height_px)) {
    return LocalResult::failure(LocalError::kInvalidArgument);
  }
  for (std::size_t i = 0; i < frame.dirty_region_count; ++i) {
    const Rect& rect = frame.dirty_regions[i];
    if (rect.width == 0 || rect.height == 0 || rect.x >= frame.width_px ||
        rect.y >= frame.height_px ||
        rect.width > frame.width_px - rect.x ||
        rect.height > frame.height_px - rect.y) {
      return LocalResult::failure(LocalError::kInvalidArgument);
    }
  }
  return LocalResult::success();
}

FakeInput::FakeInput(OwnerToken owner, RawInputSample* storage,
                     std::size_t capacity,
                     TryCriticalSection* critical_section) noexcept
    : owner_(owner),
      storage_(storage),
      capacity_(capacity),
      critical_section_(critical_section == nullptr
                            ? &local_critical_section_
                            : critical_section) {}

LocalResult FakeInput::open(OwnerToken caller, const InputConfig& config,
                            InputCapabilities& capabilities) noexcept {
  TryCriticalSectionGuard guard(*critical_section_);
  if (!guard.acquired()) {
    return LocalResult::failure(LocalError::kBusy);
  }
  if (!isOwner(caller)) {
    return LocalResult::failure(LocalError::kWrongThread);
  }
  if (state_.load(std::memory_order_relaxed) !=
      LifecycleState::kConstructed) {
    return LocalResult::failure(LocalError::kInvalidState);
  }
  if (storage_ == nullptr || capacity_ == 0 ||
      config.max_samples_per_drain == 0 ||
      config.max_samples_per_drain > capacity_) {
    state_.store(LifecycleState::kClosed, std::memory_order_release);
    return LocalResult::failure(LocalError::kInvalidArgument);
  }
  if (fail_open_.load(std::memory_order_relaxed)) {
    state_.store(LifecycleState::kClosed, std::memory_order_release);
    return LocalResult::failure(LocalError::kBackendFailed);
  }
  max_samples_per_drain_ = config.max_samples_per_drain;
  capabilities = {capacity_, true};
  state_.store(LifecycleState::kRunning, std::memory_order_release);
  return LocalResult::success();
}

LocalResult FakeInput::beginStop(OwnerToken caller) noexcept {
  TryCriticalSectionGuard guard(*critical_section_);
  if (!guard.acquired()) {
    return LocalResult::failure(LocalError::kBusy);
  }
  if (!isOwner(caller)) {
    return LocalResult::failure(LocalError::kWrongThread);
  }
  const LifecycleState current_state =
      state_.load(std::memory_order_relaxed);
  if (current_state == LifecycleState::kClosed) {
    return LocalResult::success();
  }
  if (current_state == LifecycleState::kStopping) {
    return LocalResult::success();
  }
  if (current_state != LifecycleState::kRunning) {
    return LocalResult::failure(LocalError::kInvalidState);
  }
  state_.store(LifecycleState::kStopping, std::memory_order_release);
  return LocalResult::success();
}

DrainResult FakeInput::drain(OwnerToken caller, RawInputSample* output,
                             std::size_t output_capacity) noexcept {
  TryCriticalSectionGuard guard(*critical_section_);
  if (!guard.acquired()) {
    return {LocalError::kBusy, 0, overflowCount(), coalescedCount()};
  }
  if (!isOwner(caller)) {
    return {LocalError::kWrongThread, 0, overflowCount(), coalescedCount()};
  }
  const LifecycleState current_state =
      state_.load(std::memory_order_relaxed);
  if ((current_state != LifecycleState::kRunning &&
       current_state != LifecycleState::kStopping) ||
      (output == nullptr && output_capacity != 0)) {
    return {current_state == LifecycleState::kRunning ||
                    current_state == LifecycleState::kStopping
                ? LocalError::kInvalidArgument
                : LocalError::kInvalidState,
            0, overflowCount(), coalescedCount()};
  }
  const std::size_t count =
      std::min({size_, output_capacity, max_samples_per_drain_});
  for (std::size_t i = 0; i < count; ++i) {
    output[i] = storage_[head_];
    head_ = (head_ + 1) % capacity_;
  }
  size_ -= count;
  depth_snapshot_.store(size_, std::memory_order_release);
  return {LocalError::kNone, count, overflowCount(), coalescedCount()};
}

DrainResult FakeInput::discardPending(OwnerToken caller) noexcept {
  TryCriticalSectionGuard guard(*critical_section_);
  if (!guard.acquired()) {
    return {LocalError::kBusy, 0, overflowCount(), coalescedCount()};
  }
  if (!isOwner(caller)) {
    return {LocalError::kWrongThread, 0, overflowCount(), coalescedCount()};
  }
  const LifecycleState current_state =
      state_.load(std::memory_order_relaxed);
  if (current_state != LifecycleState::kRunning &&
      current_state != LifecycleState::kStopping) {
    return {LocalError::kInvalidState, 0, overflowCount(),
            coalescedCount()};
  }
  const std::size_t discarded = size_;
  head_ = 0;
  size_ = 0;
  depth_snapshot_.store(0, std::memory_order_release);
  return {LocalError::kNone, discarded, overflowCount(), coalescedCount()};
}

LocalResult FakeInput::close(OwnerToken caller) noexcept {
  TryCriticalSectionGuard guard(*critical_section_);
  if (!guard.acquired()) {
    return LocalResult::failure(LocalError::kBusy);
  }
  if (!isOwner(caller)) {
    return LocalResult::failure(LocalError::kWrongThread);
  }
  if (state_.load(std::memory_order_relaxed) == LifecycleState::kClosed) {
    return LocalResult::success();
  }
  close_count_.fetch_add(1, std::memory_order_relaxed);
  head_ = 0;
  size_ = 0;
  depth_snapshot_.store(0, std::memory_order_release);
  state_.store(LifecycleState::kClosed, std::memory_order_release);
  if (fail_close_.load(std::memory_order_relaxed)) {
    return LocalResult::failure(LocalError::kBackendFailed);
  }
  return LocalResult::success();
}

LifecycleState FakeInput::state() const noexcept {
  return state_.load(std::memory_order_acquire);
}

LocalResult FakeInput::pushRaw(const RawInputSample& sample) noexcept {
  TryCriticalSectionGuard guard(*critical_section_);
  if (!guard.acquired()) {
    return LocalResult::failure(LocalError::kBusy);
  }
  if (state_.load(std::memory_order_relaxed) !=
      LifecycleState::kRunning) {
    return LocalResult::failure(LocalError::kInvalidState);
  }
  if (size_ == capacity_) {
    const std::size_t last_index = (head_ + size_ - 1) % capacity_;
    RawInputSample& last = storage_[last_index];
    if (sample.action == RawInputAction::kMove &&
        last.action == RawInputAction::kMove &&
        sample.device_id == last.device_id &&
        sample.contact_id == last.contact_id) {
      last = sample;
      coalesced_count_.fetch_add(1, std::memory_order_relaxed);
      return LocalResult::success();
    }
    overflow_count_.fetch_add(1, std::memory_order_relaxed);
    return LocalResult::failure(LocalError::kCapacityExhausted);
  }
  const std::size_t tail = (head_ + size_) % capacity_;
  storage_[tail] = sample;
  ++size_;
  peak_depth_ = std::max(peak_depth_, size_);
  depth_snapshot_.store(size_, std::memory_order_release);
  peak_depth_snapshot_.store(peak_depth_, std::memory_order_release);
  return LocalResult::success();
}

void FakeInput::setFailOpen(bool fail) noexcept {
  fail_open_.store(fail, std::memory_order_relaxed);
}

void FakeInput::setFailClose(bool fail) noexcept {
  fail_close_.store(fail, std::memory_order_relaxed);
}

std::size_t FakeInput::depth() const noexcept {
  return depth_snapshot_.load(std::memory_order_acquire);
}

std::size_t FakeInput::peakDepth() const noexcept {
  return peak_depth_snapshot_.load(std::memory_order_acquire);
}

std::uint64_t FakeInput::overflowCount() const noexcept {
  return overflow_count_.load(std::memory_order_relaxed);
}

std::uint64_t FakeInput::coalescedCount() const noexcept {
  return coalesced_count_.load(std::memory_order_relaxed);
}

std::size_t FakeInput::closeCount() const noexcept {
  return close_count_.load(std::memory_order_relaxed);
}

bool FakeInput::isOwner(OwnerToken caller) const noexcept {
  return owner_.valid() && caller == owner_;
}

}  // namespace quickapp::lvgl::foundation::fakes
