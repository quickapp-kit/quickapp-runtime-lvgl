#include "quickapp/lvgl/backends/sdl_backends.h"

#include <algorithm>
#include <cassert>
#include <climits>
#include <cmath>
#include <limits>

namespace quickapp::lvgl::backends {
namespace {

SDL_PixelFormat toSdlFormat(foundation::PixelFormat format) noexcept {
  switch (format) {
    case foundation::PixelFormat::kRgb565:
      return SDL_PIXELFORMAT_RGB565;
    case foundation::PixelFormat::kRgb888:
      return SDL_PIXELFORMAT_RGB24;
    case foundation::PixelFormat::kRgba8888:
      return SDL_PIXELFORMAT_RGBA32;
  }
  return SDL_PIXELFORMAT_UNKNOWN;
}

std::int32_t roundedCoordinate(float value) noexcept {
  if (value <= static_cast<float>(std::numeric_limits<std::int32_t>::min())) {
    return std::numeric_limits<std::int32_t>::min();
  }
  if (value >= static_cast<float>(std::numeric_limits<std::int32_t>::max())) {
    return std::numeric_limits<std::int32_t>::max();
  }
  return static_cast<std::int32_t>(std::lround(value));
}

std::uint32_t narrowId(std::uint64_t value) noexcept {
  return static_cast<std::uint32_t>(value & 0xffffffffULL);
}

}  // namespace

SdlDisplayBackend::SdlDisplayBackend(foundation::OwnerToken owner) noexcept
    : owner_(owner) {}

SdlDisplayBackend::~SdlDisplayBackend() {
  assert((state_ == foundation::LifecycleState::kConstructed ||
          state_ == foundation::LifecycleState::kClosed) &&
         "SdlDisplayBackend requires explicit close");
}

foundation::LocalResult SdlDisplayBackend::open(
    foundation::OwnerToken caller,
    const foundation::DisplayConfig& config,
    foundation::DisplayCapabilities& capabilities) noexcept {
  if (caller != owner_) {
    return foundation::LocalResult::failure(
        foundation::LocalError::kWrongThread);
  }
  if (state_ != foundation::LifecycleState::kConstructed ||
      config.physical_width_px == 0 || config.physical_height_px == 0 ||
      config.max_dirty_regions == 0 ||
      config.physical_width_px > static_cast<std::uint32_t>(INT_MAX) ||
      config.physical_height_px > static_cast<std::uint32_t>(INT_MAX)) {
    return foundation::LocalResult::failure(
        foundation::LocalError::kInvalidArgument);
  }
  if (!SDL_InitSubSystem(SDL_INIT_VIDEO | SDL_INIT_EVENTS)) {
    state_ = foundation::LifecycleState::kClosed;
    return foundation::LocalResult::failure(
        foundation::LocalError::kBackendFailed);
  }
  window_ = SDL_CreateWindow(
      "QuickApp Kit LVGL Backend", static_cast<int>(config.physical_width_px),
      static_cast<int>(config.physical_height_px), SDL_WINDOW_HIDDEN);
  if (window_ == nullptr || SDL_GetWindowSurface(window_) == nullptr) {
    if (window_ != nullptr) {
      SDL_DestroyWindow(window_);
      window_ = nullptr;
    }
    SDL_QuitSubSystem(SDL_INIT_VIDEO | SDL_INIT_EVENTS);
    state_ = foundation::LifecycleState::kClosed;
    return foundation::LocalResult::failure(
        foundation::LocalError::kBackendFailed);
  }
  width_ = config.physical_width_px;
  height_ = config.physical_height_px;
  max_dirty_regions_ = config.max_dirty_regions;
  capabilities = {width_, height_, config.preferred_pixel_format,
                  max_dirty_regions_};
  state_ = foundation::LifecycleState::kRunning;
  return foundation::LocalResult::success();
}

foundation::LocalResult SdlDisplayBackend::present(
    foundation::OwnerToken caller,
    const foundation::DisplayFrameView& frame) noexcept {
  if (caller != owner_) {
    return foundation::LocalResult::failure(
        foundation::LocalError::kWrongThread);
  }
  if (state_ != foundation::LifecycleState::kRunning || window_ == nullptr ||
      frame.pixels == nullptr || frame.width_px != width_ ||
      frame.height_px != height_ || frame.dirty_regions == nullptr ||
      frame.dirty_region_count == 0 ||
      frame.dirty_region_count > max_dirty_regions_ ||
      frame.stride_bytes > static_cast<std::size_t>(INT_MAX)) {
    return foundation::LocalResult::failure(
        foundation::LocalError::kInvalidArgument);
  }
  SDL_Surface* surface = SDL_GetWindowSurface(window_);
  if (surface == nullptr || surface->pixels == nullptr || surface->pitch <= 0 ||
      !SDL_ConvertPixels(static_cast<int>(width_), static_cast<int>(height_),
                         toSdlFormat(frame.pixel_format), frame.pixels,
                         static_cast<int>(frame.stride_bytes), surface->format,
                         surface->pixels, surface->pitch)) {
    return foundation::LocalResult::failure(
        foundation::LocalError::kBackendFailed);
  }

  std::array<SDL_Rect, 64> dirty{};
  if (frame.dirty_region_count > dirty.size()) {
    return foundation::LocalResult::failure(
        foundation::LocalError::kCapacityExhausted);
  }
  for (std::size_t index = 0; index < frame.dirty_region_count; ++index) {
    const foundation::Rect& source = frame.dirty_regions[index];
    if (source.width == 0 || source.height == 0 || source.x >= width_ ||
        source.y >= height_ || source.width > width_ - source.x ||
        source.height > height_ - source.y || source.x > INT_MAX ||
        source.y > INT_MAX || source.width > INT_MAX ||
        source.height > INT_MAX) {
      return foundation::LocalResult::failure(
          foundation::LocalError::kInvalidArgument);
    }
    dirty[index] = {static_cast<int>(source.x), static_cast<int>(source.y),
                    static_cast<int>(source.width),
                    static_cast<int>(source.height)};
  }
  if (!SDL_UpdateWindowSurfaceRects(window_, dirty.data(),
                                    static_cast<int>(frame.dirty_region_count))) {
    return foundation::LocalResult::failure(
        foundation::LocalError::kBackendFailed);
  }
  ++presented_frames_;
  return foundation::LocalResult::success();
}

foundation::LocalResult SdlDisplayBackend::close(
    foundation::OwnerToken caller) noexcept {
  if (caller != owner_) {
    return foundation::LocalResult::failure(
        foundation::LocalError::kWrongThread);
  }
  if (state_ == foundation::LifecycleState::kClosed) {
    return foundation::LocalResult::success();
  }
  if (window_ != nullptr) {
    SDL_DestroyWindow(window_);
    window_ = nullptr;
  }
  SDL_QuitSubSystem(SDL_INIT_VIDEO | SDL_INIT_EVENTS);
  state_ = foundation::LifecycleState::kClosed;
  return foundation::LocalResult::success();
}

foundation::LifecycleState SdlDisplayBackend::state() const noexcept {
  return state_;
}

SDL_WindowID SdlDisplayBackend::windowId() const noexcept {
  return window_ == nullptr ? 0 : SDL_GetWindowID(window_);
}

SdlRawInputBackend::SdlRawInputBackend(
    foundation::OwnerToken owner, SdlDisplayBackend& display) noexcept
    : owner_(owner), display_(&display) {}

foundation::LocalResult SdlRawInputBackend::open(
    foundation::OwnerToken caller, const foundation::InputConfig& config,
    foundation::InputCapabilities& capabilities) noexcept {
  if (caller != owner_) {
    return foundation::LocalResult::failure(
        foundation::LocalError::kWrongThread);
  }
  if (state_ != foundation::LifecycleState::kConstructed ||
      display_->state() != foundation::LifecycleState::kRunning ||
      config.max_samples_per_drain == 0 ||
      config.max_samples_per_drain > kMaxSamples) {
    return foundation::LocalResult::failure(
        foundation::LocalError::kInvalidArgument);
  }
  capacity_ = kMaxSamples;
  max_samples_per_drain_ = config.max_samples_per_drain;
  capabilities = {capacity_, true};
  state_ = foundation::LifecycleState::kRunning;
  return foundation::LocalResult::success();
}

foundation::LocalResult SdlRawInputBackend::beginStop(
    foundation::OwnerToken caller) noexcept {
  if (caller != owner_) {
    return foundation::LocalResult::failure(
        foundation::LocalError::kWrongThread);
  }
  if (state_ == foundation::LifecycleState::kStopping ||
      state_ == foundation::LifecycleState::kClosed) {
    return foundation::LocalResult::success();
  }
  if (state_ != foundation::LifecycleState::kRunning) {
    return foundation::LocalResult::failure(
        foundation::LocalError::kInvalidState);
  }
  state_ = foundation::LifecycleState::kStopping;
  return foundation::LocalResult::success();
}

foundation::DrainResult SdlRawInputBackend::drain(
    foundation::OwnerToken caller, foundation::RawInputSample* output,
    std::size_t output_capacity) noexcept {
  if (caller != owner_) {
    return {foundation::LocalError::kWrongThread, 0, overflow_count_,
            coalesced_count_};
  }
  if (state_ != foundation::LifecycleState::kRunning || output == nullptr ||
      output_capacity == 0) {
    return {foundation::LocalError::kInvalidState, 0, overflow_count_,
            coalesced_count_};
  }
  collectEvents();
  const std::size_t count =
      std::min({size_, output_capacity, max_samples_per_drain_});
  for (std::size_t index = 0; index < count; ++index) {
    output[index] = samples_[head_];
    head_ = (head_ + 1) % capacity_;
    --size_;
  }
  return {foundation::LocalError::kNone, count, overflow_count_,
          coalesced_count_};
}

foundation::DrainResult SdlRawInputBackend::discardPending(
    foundation::OwnerToken caller) noexcept {
  if (caller != owner_) {
    return {foundation::LocalError::kWrongThread, 0, overflow_count_,
            coalesced_count_};
  }
  const std::size_t discarded = size_;
  head_ = 0;
  size_ = 0;
  return {foundation::LocalError::kNone, discarded, overflow_count_,
          coalesced_count_};
}

foundation::LocalResult SdlRawInputBackend::close(
    foundation::OwnerToken caller) noexcept {
  if (caller != owner_) {
    return foundation::LocalResult::failure(
        foundation::LocalError::kWrongThread);
  }
  head_ = 0;
  size_ = 0;
  mouse_down_ = false;
  state_ = foundation::LifecycleState::kClosed;
  return foundation::LocalResult::success();
}

foundation::LifecycleState SdlRawInputBackend::state() const noexcept {
  return state_;
}

bool SdlRawInputBackend::pushSample(
    const foundation::RawInputSample& sample) noexcept {
  if (size_ == capacity_) {
    if (sample.action == foundation::RawInputAction::kMove && size_ != 0) {
      const std::size_t tail = (head_ + size_ - 1) % capacity_;
      if (samples_[tail].action == foundation::RawInputAction::kMove &&
          samples_[tail].contact_id == sample.contact_id) {
        samples_[tail] = sample;
        ++coalesced_count_;
        return true;
      }
    }
    ++overflow_count_;
    return false;
  }
  const std::size_t tail = (head_ + size_) % capacity_;
  samples_[tail] = sample;
  ++size_;
  return true;
}

void SdlRawInputBackend::collectEvents() noexcept {
  SDL_Event event{};
  for (std::size_t polled = 0; polled < capacity_ && SDL_PollEvent(&event);
       ++polled) {
    foundation::RawInputSample sample{};
    bool accepted_type = false;
    if ((event.type == SDL_EVENT_MOUSE_BUTTON_DOWN ||
         event.type == SDL_EVENT_MOUSE_BUTTON_UP) &&
        event.button.windowID == display_->windowId() &&
        event.button.button == SDL_BUTTON_LEFT) {
      mouse_down_ = event.type == SDL_EVENT_MOUSE_BUTTON_DOWN;
      sample = {narrowId(event.button.which), 1,
                mouse_down_ ? foundation::RawInputAction::kDown
                            : foundation::RawInputAction::kUp,
                roundedCoordinate(event.button.x),
                roundedCoordinate(event.button.y), 0, false,
                event.button.timestamp, ++sequence_};
      accepted_type = true;
    } else if (event.type == SDL_EVENT_MOUSE_MOTION && mouse_down_ &&
               event.motion.windowID == display_->windowId()) {
      sample = {narrowId(event.motion.which), 1,
                foundation::RawInputAction::kMove,
                roundedCoordinate(event.motion.x),
                roundedCoordinate(event.motion.y), 0, false,
                event.motion.timestamp, ++sequence_};
      accepted_type = true;
    } else if ((event.type == SDL_EVENT_FINGER_DOWN ||
                event.type == SDL_EVENT_FINGER_UP ||
                event.type == SDL_EVENT_FINGER_MOTION ||
                event.type == SDL_EVENT_FINGER_CANCELED) &&
               event.tfinger.windowID == display_->windowId()) {
      foundation::RawInputAction action =
          foundation::RawInputAction::kCancel;
      if (event.type == SDL_EVENT_FINGER_DOWN) {
        action = foundation::RawInputAction::kDown;
      } else if (event.type == SDL_EVENT_FINGER_UP) {
        action = foundation::RawInputAction::kUp;
      } else if (event.type == SDL_EVENT_FINGER_MOTION) {
        action = foundation::RawInputAction::kMove;
      }
      const float x = event.tfinger.x * static_cast<float>(display_->width());
      const float y = event.tfinger.y * static_cast<float>(display_->height());
      const float pressure =
          std::clamp(event.tfinger.pressure, 0.0F, 1.0F) * 65535.0F;
      sample = {narrowId(event.tfinger.touchID),
                narrowId(event.tfinger.fingerID), action,
                roundedCoordinate(x), roundedCoordinate(y),
                static_cast<std::uint16_t>(pressure), true,
                event.tfinger.timestamp, ++sequence_};
      accepted_type = true;
    }
    if (accepted_type) {
      (void)pushSample(sample);
    }
  }
}

}  // namespace quickapp::lvgl::backends
