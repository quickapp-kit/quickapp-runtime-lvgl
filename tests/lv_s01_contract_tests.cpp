#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <thread>

#include "quickapp/lvgl/foundation/backend_lifecycle.h"
#include "quickapp/lvgl/foundation/fakes.h"
#include "quickapp/lvgl/foundation/owner_task_queue.h"

namespace qlf = quickapp::lvgl::foundation;
namespace fake = quickapp::lvgl::foundation::fakes;

namespace {

#define CHECK(expression)                                                     \
  do {                                                                        \
    if (!(expression)) {                                                      \
      std::fprintf(stderr, "CHECK failed at %s:%d: %s\n", __FILE__, __LINE__, \
                   #expression);                                              \
      return false;                                                           \
    }                                                                         \
  } while (false)

constexpr qlf::OwnerToken kOwner{1};
constexpr qlf::OwnerToken kWrongOwner{2};
thread_local qlf::OwnerToken gCurrentOwnerToken{};

qlf::OwnerToken readCurrentOwner(void*) noexcept {
  return gCurrentOwnerToken;
}

qlf::DisplayCapabilities displayCapabilities() noexcept {
  return {4, 4, qlf::PixelFormat::kRgba8888, 2};
}

qlf::DisplayConfig displayConfig() noexcept {
  return {4, 4, qlf::PixelFormat::kRgba8888, 2};
}

qlf::RawInputSample sample(std::uint64_t sequence,
                           qlf::RawInputAction action,
                           std::uint32_t contact = 1) noexcept {
  return {1, contact, action, static_cast<std::int32_t>(sequence),
          static_cast<std::int32_t>(sequence), 0, false, sequence * 10,
          sequence};
}

bool testOwnerBindingBoundedFifoAndFairPump() {
  fake::FakeWakeup wakeup(kOwner);
  std::array<qlf::OwnerTask, 4> storage{};
  qlf::OwnerTaskQueue queue(storage.data(), storage.size(), 2, &wakeup);
  CHECK(queue.bindOwner(kOwner).ok());
  CHECK(queue.bindOwner(kOwner).error == qlf::LocalError::kInvalidState);
  CHECK(queue.pump(kWrongOwner).error == qlf::LocalError::kWrongThread);

  std::array<fake::FakeTaskState, 5> states{};
  for (std::size_t i = 0; i < 4; ++i) {
    qlf::OwnerTask task = fake::makeFakeTask(states[i]);
    CHECK(queue.post(std::move(task)).status == qlf::PostStatus::kAccepted);
    CHECK(!task.valid());
  }
  CHECK(queue.depth() == 4);
  CHECK(queue.peakDepth() == 4);
  CHECK(wakeup.notifyCount() == 1);

  qlf::OwnerTask rejected = fake::makeFakeTask(states[4]);
  CHECK(queue.post(std::move(rejected)).status == qlf::PostStatus::kFull);
  CHECK(rejected.valid());
  CHECK(queue.pump(kOwner).executed == 2);
  CHECK(queue.depth() == 2);
  CHECK(queue.pump(kOwner).executed == 2);
  CHECK(queue.depth() == 0);
  for (std::size_t i = 0; i < 4; ++i) {
    CHECK(states[i].executions == 1);
    CHECK(states[i].destructions == 1);
  }
  rejected.reset();
  CHECK(states[4].executions == 0);
  CHECK(states[4].destructions == 1);

  CHECK(queue.beginStop(kOwner, qlf::StopPolicy::kCancel).ok());
  CHECK(queue.finishStop(kOwner).ok());
  CHECK(queue.finishStop(kOwner).ok());
  return true;
}

bool testMultiProducerAndWakeupBoundary() {
  fake::FakeWakeup wakeup(kOwner);
  std::array<qlf::OwnerTask, 64> storage{};
  qlf::OwnerTaskQueue queue(storage.data(), storage.size(), storage.size(),
                            &wakeup);
  CHECK(queue.bindOwner(kOwner).ok());

  std::atomic<std::size_t> executions{0};
  std::atomic<std::size_t> accepted{0};
  std::atomic<std::size_t> busy{0};
  std::atomic<bool> producer_failed{false};
  auto producer = [&]() {
    for (std::size_t i = 0; i < 20; ++i) {
      auto task = qlf::OwnerTask::make([&executions]() noexcept {
        executions.fetch_add(1, std::memory_order_relaxed);
      });
      bool posted = false;
      for (std::size_t attempt = 0; attempt < 1'000; ++attempt) {
        const qlf::PostOutcome outcome = queue.post(std::move(task));
        if (outcome.status == qlf::PostStatus::kAccepted) {
          accepted.fetch_add(1, std::memory_order_relaxed);
          posted = true;
          break;
        }
        if (outcome.status != qlf::PostStatus::kBusy || !task.valid()) {
          producer_failed.store(true, std::memory_order_relaxed);
          break;
        }
        busy.fetch_add(1, std::memory_order_relaxed);
        std::this_thread::yield();
      }
      if (!posted) {
        producer_failed.store(true, std::memory_order_relaxed);
      }
    }
  };

  std::thread first(producer);
  std::thread second(producer);
  first.join();
  second.join();
  const std::size_t accepted_count = accepted.load();
  const qlf::PumpResult producer_pump = queue.pump(kOwner, 64);

  wakeup.setNotifyResult(qlf::WakeResult::kFailed);
  auto task = qlf::OwnerTask::make([]() noexcept {});
  const qlf::PostOutcome outcome = queue.post(std::move(task));
  CHECK(outcome.status == qlf::PostStatus::kAccepted);
  CHECK(outcome.wake_result == qlf::WakeResult::kFailed);
  CHECK(wakeup.notifyCount() >= 2);
  CHECK(queue.pump(kOwner).executed == 1);
  CHECK(queue.beginStop(kOwner, qlf::StopPolicy::kCancel).ok());
  CHECK(queue.finishStop(kOwner).ok());
  CHECK(!producer_failed.load());
  CHECK(accepted_count == 40);
  CHECK(producer_pump.executed == 40);
  CHECK(executions.load() == 40);
  CHECK(wakeup.notifyCount() >= 2);
  (void)busy;
  return true;
}

bool testClockAndCooperativeWakeup() {
  fake::FakeClock clock(1'000);
  CHECK(clock.nowNs() == 0);
  CHECK(clock.resolutionNs() == 1'000);
  CHECK(clock.setNowNs(5'000).ok());
  CHECK(clock.advanceNs(2'000).ok());
  CHECK(clock.nowNs() == 7'000);
  CHECK(clock.setNowNs(6'999).error == qlf::LocalError::kInvalidArgument);
  CHECK(clock.advanceNs(UINT64_MAX).error ==
        qlf::LocalError::kInvalidArgument);

  fake::FakeClock concurrent_clock;
  std::atomic<bool> clock_retry_failed{false};
  auto set_clock = [&concurrent_clock, &clock_retry_failed](
                       std::uint64_t value) {
    for (std::size_t attempt = 0; attempt < 1'000; ++attempt) {
      const qlf::LocalResult result = concurrent_clock.setNowNs(value);
      if (result.ok() || result.error == qlf::LocalError::kInvalidArgument) {
        return;
      }
      if (result.error != qlf::LocalError::kBusy) {
        break;
      }
      std::this_thread::yield();
    }
    clock_retry_failed.store(true, std::memory_order_relaxed);
  };
  std::thread low(set_clock, 50);
  std::thread high(set_clock, 100);
  low.join();
  high.join();
  CHECK(!clock_retry_failed.load());
  CHECK(concurrent_clock.nowNs() == 100);

  fake::FakeWakeup no_wait(kOwner, false);
  CHECK(no_wait.waitUntil(kOwner, 100) == qlf::WakeResult::kUnsupported);
  CHECK(no_wait.waitUntil(kWrongOwner, 100) ==
        qlf::WakeResult::kWrongThread);
  no_wait.setNotifyResult(qlf::WakeResult::kFailed);
  CHECK(no_wait.notify() == qlf::WakeResult::kFailed);
  no_wait.requestStop();
  CHECK(no_wait.waitUntil(kOwner, 100) == qlf::WakeResult::kStopping);
  CHECK(no_wait.close(kWrongOwner).error == qlf::LocalError::kWrongThread);
  CHECK(no_wait.close(kOwner).ok());
  CHECK(no_wait.close(kOwner).ok());
  return true;
}

bool testDisplayOwnershipValidationAndFailure() {
  fake::FakeDisplay display(kOwner, displayCapabilities());
  qlf::DisplayCapabilities capabilities{};
  CHECK(display.open(kWrongOwner, displayConfig(), capabilities).error ==
        qlf::LocalError::kWrongThread);
  CHECK(display.open(kOwner, displayConfig(), capabilities).ok());

  std::array<std::byte, 64> pixels{};
  for (std::size_t i = 0; i < pixels.size(); ++i) {
    pixels[i] = static_cast<std::byte>(i);
  }
  const qlf::Rect dirty{0, 0, 4, 4};
  const qlf::DisplayFrameView frame{
      pixels.data(), pixels.size(), 16, 4, 4, qlf::PixelFormat::kRgba8888,
      &dirty, 1, 7};

  CHECK(display.present(kWrongOwner, frame).error ==
        qlf::LocalError::kWrongThread);
  CHECK(display.present(kOwner, frame).ok());
  CHECK(display.presentCount() == 1);
  CHECK(display.lastFrameSequence() == 7);
  const std::uint64_t checksum = display.lastFrameChecksum();
  pixels.fill(std::byte{0});
  CHECK(display.lastFrameChecksum() == checksum);

  qlf::DisplayFrameView invalid = frame;
  invalid.byte_length = 1;
  CHECK(display.present(kOwner, invalid).error ==
        qlf::LocalError::kInvalidArgument);

  display.setFaults({false, 2, false});
  CHECK(display.present(kOwner, frame).error ==
        qlf::LocalError::kBackendFailed);
  CHECK(display.close(kWrongOwner).error == qlf::LocalError::kWrongThread);
  CHECK(display.close(kOwner).ok());
  CHECK(display.close(kOwner).ok());
  CHECK(display.closeCount() == 1);
  return true;
}

bool testInputBoundedCoalescingAndDrain() {
  std::array<qlf::RawInputSample, 3> storage{};
  fake::FakeInput input(kOwner, storage.data(), storage.size());
  qlf::InputCapabilities capabilities{};
  CHECK(input.open(kWrongOwner, {2}, capabilities).error ==
        qlf::LocalError::kWrongThread);
  CHECK(input.open(kOwner, {2}, capabilities).ok());
  CHECK(capabilities.capacity == 3);

  CHECK(input.pushRaw(sample(1, qlf::RawInputAction::kDown)).ok());
  CHECK(input.pushRaw(sample(2, qlf::RawInputAction::kMove)).ok());
  CHECK(input.pushRaw(sample(3, qlf::RawInputAction::kMove)).ok());
  CHECK(input.depth() == 3);
  CHECK(input.pushRaw(sample(4, qlf::RawInputAction::kMove)).ok());
  CHECK(input.coalescedCount() == 1);
  CHECK(input.pushRaw(sample(5, qlf::RawInputAction::kUp)).error ==
        qlf::LocalError::kCapacityExhausted);
  CHECK(input.overflowCount() == 1);

  std::array<qlf::RawInputSample, 3> output{};
  CHECK(input.drain(kWrongOwner, output.data(), output.size()).error ==
        qlf::LocalError::kWrongThread);
  const qlf::DrainResult first_drain =
      input.drain(kOwner, output.data(), output.size());
  CHECK(first_drain.ok());
  CHECK(first_drain.sample_count == 2);
  CHECK(output[0].action == qlf::RawInputAction::kDown);
  CHECK(output[1].sample_sequence == 2);
  CHECK(input.depth() == 1);
  const qlf::DrainResult second_drain =
      input.drain(kOwner, output.data() + 2, 1);
  CHECK(second_drain.ok());
  CHECK(second_drain.sample_count == 1);
  CHECK(output[2].sample_sequence == 4);
  CHECK(input.depth() == 0);

  CHECK(input.pushRaw(sample(5, qlf::RawInputAction::kUp)).ok());
  CHECK(input.beginStop(kWrongOwner).error == qlf::LocalError::kWrongThread);
  CHECK(input.beginStop(kOwner).ok());
  CHECK(input.pushRaw(sample(6, qlf::RawInputAction::kDown)).error ==
        qlf::LocalError::kInvalidState);
  const qlf::DrainResult discarded = input.discardPending(kOwner);
  CHECK(discarded.ok());
  CHECK(discarded.sample_count == 1);
  CHECK(input.close(kWrongOwner).error == qlf::LocalError::kWrongThread);
  CHECK(input.close(kOwner).ok());
  CHECK(input.close(kOwner).ok());
  CHECK(input.depth() == 0);
  return true;
}

bool testLifecycleDrainAndDeterministicClose() {
  fake::FakeWakeup wakeup(kOwner);
  fake::FakeDisplay display(kOwner, displayCapabilities());
  std::array<qlf::RawInputSample, 4> input_storage{};
  fake::FakeInput input(kOwner, input_storage.data(), input_storage.size());
  std::array<qlf::OwnerTask, 4> task_storage{};
  qlf::OwnerTaskQueue tasks(task_storage.data(), task_storage.size(), 2,
                            &wakeup);
  qlf::BackendLifecycleCoordinator lifecycle(tasks, wakeup, display, input);
  CHECK(lifecycle.open(kOwner, displayConfig(), {4}).ok());

  std::array<fake::FakeTaskState, 3> task_states{};
  for (auto& state : task_states) {
    auto task = fake::makeFakeTask(state);
    CHECK(tasks.post(std::move(task)).status == qlf::PostStatus::kAccepted);
  }
  CHECK(input.pushRaw(sample(1, qlf::RawInputAction::kDown)).ok());
  CHECK(lifecycle.beginStop(kWrongOwner, qlf::StopPolicy::kDrain).error ==
        qlf::LocalError::kWrongThread);
  CHECK(lifecycle.beginStop(kOwner, qlf::StopPolicy::kDrain).ok());
  CHECK(tasks.depth() == 0);
  CHECK(lifecycle.discardedInputSamples() == 1);
  for (const auto& state : task_states) {
    CHECK(state.executions == 1);
    CHECK(state.destructions == 1);
  }
  auto late_task = qlf::OwnerTask::make([]() noexcept {});
  CHECK(tasks.post(std::move(late_task)).status ==
        qlf::PostStatus::kStopping);
  CHECK(late_task.valid());
  late_task.reset();

  CHECK(lifecycle.finishStop(kOwner).ok());
  CHECK(lifecycle.finishStop(kOwner).ok());
  CHECK(lifecycle.state() == qlf::LifecycleState::kClosed);
  CHECK(tasks.state() == qlf::LifecycleState::kClosed);
  CHECK(display.state() == qlf::LifecycleState::kClosed);
  CHECK(input.state() == qlf::LifecycleState::kClosed);
  CHECK(wakeup.closed());
  return true;
}

bool testLifecycleCancelAndOpenRollback() {
  {
    fake::FakeWakeup wakeup(kOwner);
    fake::FakeDisplay display(kOwner, displayCapabilities());
    std::array<qlf::RawInputSample, 2> input_storage{};
    fake::FakeInput input(kOwner, input_storage.data(), input_storage.size());
    std::array<qlf::OwnerTask, 2> task_storage{};
    qlf::OwnerTaskQueue tasks(task_storage.data(), task_storage.size(), 2,
                              &wakeup);
    qlf::BackendLifecycleCoordinator lifecycle(tasks, wakeup, display, input);
    CHECK(lifecycle.open(kOwner, displayConfig(), {2}).ok());
    fake::FakeTaskState state{};
    auto task = fake::makeFakeTask(state);
    CHECK(tasks.post(std::move(task)).status == qlf::PostStatus::kAccepted);
    CHECK(lifecycle.beginStop(kOwner, qlf::StopPolicy::kCancel).ok());
    CHECK(state.executions == 0);
    CHECK(state.destructions == 1);
    CHECK(lifecycle.finishStop(kOwner).ok());
  }

  {
    fake::FakeWakeup wakeup(kOwner);
    fake::FakeDisplay display(kOwner, displayCapabilities());
    std::array<qlf::RawInputSample, 2> input_storage{};
    fake::FakeInput input(kOwner, input_storage.data(), input_storage.size());
    input.setFailOpen(true);
    std::array<qlf::OwnerTask, 2> task_storage{};
    qlf::OwnerTaskQueue tasks(task_storage.data(), task_storage.size(), 2,
                              &wakeup);
    qlf::BackendLifecycleCoordinator lifecycle(tasks, wakeup, display, input);
    CHECK(lifecycle.open(kOwner, displayConfig(), {2}).error ==
          qlf::LocalError::kBackendFailed);
    CHECK(lifecycle.state() == qlf::LifecycleState::kClosed);
    CHECK(tasks.state() == qlf::LifecycleState::kClosed);
    CHECK(display.state() == qlf::LifecycleState::kClosed);
    CHECK(input.state() == qlf::LifecycleState::kClosed);
    CHECK(wakeup.closed());
  }
  return true;
}

bool testCloseFailureStillReleasesEverything() {
  fake::FakeWakeup wakeup(kOwner);
  fake::FakeDisplay display(kOwner, displayCapabilities());
  std::array<qlf::RawInputSample, 2> input_storage{};
  fake::FakeInput input(kOwner, input_storage.data(), input_storage.size());
  input.setFailClose(true);
  std::array<qlf::OwnerTask, 2> task_storage{};
  qlf::OwnerTaskQueue tasks(task_storage.data(), task_storage.size(), 2,
                            &wakeup);
  qlf::BackendLifecycleCoordinator lifecycle(tasks, wakeup, display, input);
  CHECK(lifecycle.open(kOwner, displayConfig(), {2}).ok());
  CHECK(lifecycle.beginStop(kOwner, qlf::StopPolicy::kCancel).ok());
  CHECK(lifecycle.finishStop(kOwner).error ==
        qlf::LocalError::kBackendFailed);
  CHECK(lifecycle.state() == qlf::LifecycleState::kClosed);
  CHECK(tasks.state() == qlf::LifecycleState::kClosed);
  CHECK(display.state() == qlf::LifecycleState::kClosed);
  CHECK(input.state() == qlf::LifecycleState::kClosed);
  CHECK(wakeup.closed());
  return true;
}

bool testBoundedContentionAndOwnerDestruction() {
  qlf::AtomicTryCriticalSection queue_section;
  fake::FakeWakeup wakeup(kOwner);
  std::array<qlf::OwnerTask, 4> storage{};
  qlf::OwnerTaskQueue queue(storage.data(), storage.size(), 2, &wakeup,
                            &queue_section);

  CHECK(queue_section.tryEnter());
  CHECK(queue.bindOwner(kOwner).error == qlf::LocalError::kBusy);
  queue_section.leave();
  CHECK(queue.bindOwner(kOwner).ok());

  fake::FakeTaskState executed_state{};
  executed_state.current_owner = readCurrentOwner;
  std::thread producer([&]() {
    gCurrentOwnerToken = kWrongOwner;
    auto task = fake::makeFakeTask(executed_state);
    const qlf::PostOutcome outcome = queue.post(std::move(task));
    if (outcome.status != qlf::PostStatus::kAccepted) {
      executed_state.executions = 100;
    }
  });
  producer.join();
  gCurrentOwnerToken = kOwner;
  CHECK(queue.pump(kOwner).executed == 1);
  CHECK(executed_state.executions == 1);
  CHECK(executed_state.destructions == 1);
  CHECK(executed_state.destruction_owner == kOwner);

  fake::FakeTaskState rejected_state{};
  rejected_state.current_owner = readCurrentOwner;
  gCurrentOwnerToken = kWrongOwner;
  auto rejected = fake::makeFakeTask(rejected_state);
  CHECK(queue_section.tryEnter());
  const qlf::PostOutcome busy_post = queue.post(std::move(rejected));
  CHECK(busy_post.status == qlf::PostStatus::kBusy);
  CHECK(rejected.valid());
  CHECK(queue.pump(kOwner).error == qlf::LocalError::kBusy);
  CHECK(queue.beginStop(kOwner, qlf::StopPolicy::kCancel).error ==
        qlf::LocalError::kBusy);
  CHECK(queue.state() == qlf::LifecycleState::kRunning);
  queue_section.leave();
  rejected.reset();
  CHECK(rejected_state.destruction_owner == kWrongOwner);

  fake::FakeTaskState cancelled_state{};
  cancelled_state.current_owner = readCurrentOwner;
  auto cancelled = fake::makeFakeTask(cancelled_state);
  CHECK(queue.post(std::move(cancelled)).status ==
        qlf::PostStatus::kAccepted);
  gCurrentOwnerToken = kOwner;
  CHECK(!queue.destructionInvariantHolds());
  CHECK(queue.beginStop(kOwner, qlf::StopPolicy::kCancel).ok());
  CHECK(cancelled_state.executions == 0);
  CHECK(cancelled_state.destructions == 1);
  CHECK(cancelled_state.destruction_owner == kOwner);
  CHECK(queue.finishStop(kOwner).ok());
  CHECK(queue.destructionInvariantHolds());

  qlf::AtomicTryCriticalSection input_section;
  std::array<qlf::RawInputSample, 2> input_storage{};
  fake::FakeInput input(kOwner, input_storage.data(), input_storage.size(),
                        &input_section);
  qlf::InputCapabilities capabilities{};
  CHECK(input.open(kOwner, {2}, capabilities).ok());
  CHECK(input_section.tryEnter());
  CHECK(input.pushRaw(sample(1, qlf::RawInputAction::kDown)).error ==
        qlf::LocalError::kBusy);
  std::array<qlf::RawInputSample, 1> output{};
  CHECK(input.drain(kOwner, output.data(), output.size()).error ==
        qlf::LocalError::kBusy);
  CHECK(input.beginStop(kOwner).error == qlf::LocalError::kBusy);
  CHECK(input.depth() == 0);
  input_section.leave();
  CHECK(input.pushRaw(sample(1, qlf::RawInputAction::kDown)).ok());
  CHECK(input.beginStop(kOwner).ok());
  CHECK(input.discardPending(kOwner).sample_count == 1);
  CHECK(input.close(kOwner).ok());
  return true;
}

bool testLifecycleContentionRetryConverges() {
  qlf::AtomicTryCriticalSection queue_section;
  qlf::AtomicTryCriticalSection input_section;
  fake::FakeWakeup wakeup(kOwner);
  fake::FakeDisplay display(kOwner, displayCapabilities());
  std::array<qlf::RawInputSample, 2> input_storage{};
  fake::FakeInput input(kOwner, input_storage.data(), input_storage.size(),
                        &input_section);
  std::array<qlf::OwnerTask, 2> task_storage{};
  qlf::OwnerTaskQueue tasks(task_storage.data(), task_storage.size(), 1,
                            &wakeup, &queue_section);
  qlf::BackendLifecycleCoordinator lifecycle(tasks, wakeup, display, input);
  CHECK(lifecycle.open(kOwner, displayConfig(), {2}).ok());

  fake::FakeTaskState state{};
  state.current_owner = readCurrentOwner;
  auto task = fake::makeFakeTask(state);
  CHECK(tasks.post(std::move(task)).status == qlf::PostStatus::kAccepted);

  CHECK(queue_section.tryEnter());
  CHECK(lifecycle.beginStop(kOwner, qlf::StopPolicy::kDrain).error ==
        qlf::LocalError::kBusy);
  CHECK(lifecycle.state() == qlf::LifecycleState::kRunning);
  queue_section.leave();

  CHECK(input_section.tryEnter());
  CHECK(lifecycle.beginStop(kOwner, qlf::StopPolicy::kDrain).error ==
        qlf::LocalError::kBusy);
  CHECK(lifecycle.state() == qlf::LifecycleState::kStopping);
  CHECK(tasks.state() == qlf::LifecycleState::kStopping);
  CHECK(state.executions == 0);
  input_section.leave();

  gCurrentOwnerToken = kOwner;
  CHECK(lifecycle.beginStop(kOwner, qlf::StopPolicy::kDrain).ok());
  CHECK(state.executions == 1);
  CHECK(state.destructions == 1);
  CHECK(state.destruction_owner == kOwner);
  CHECK(lifecycle.finishStop(kOwner).ok());
  CHECK(tasks.destructionInvariantHolds());
  return true;
}

bool testDestructorContractIsVisible() {
  std::array<qlf::OwnerTask, 1> storage{};
  fake::FakeTaskState state{};
  state.current_owner = readCurrentOwner;

#ifdef NDEBUG
  {
    fake::FakeWakeup wakeup(kOwner);
    qlf::OwnerTaskQueue queue(storage.data(), storage.size(), 1, &wakeup);
    CHECK(queue.bindOwner(kOwner).ok());
    auto task = fake::makeFakeTask(state);
    CHECK(queue.post(std::move(task)).status == qlf::PostStatus::kAccepted);
    CHECK(!queue.destructionInvariantHolds());
  }
  CHECK(state.destructions == 0);
  gCurrentOwnerToken = kOwner;
  storage[0].reset();
  CHECK(state.destructions == 1);
  CHECK(state.destruction_owner == kOwner);
#else
  fake::FakeWakeup wakeup(kOwner);
  qlf::OwnerTaskQueue queue(storage.data(), storage.size(), 1, &wakeup);
  CHECK(queue.bindOwner(kOwner).ok());
  CHECK(!queue.destructionInvariantHolds());
  CHECK(queue.beginStop(kOwner, qlf::StopPolicy::kCancel).ok());
  CHECK(queue.finishStop(kOwner).ok());
  CHECK(queue.destructionInvariantHolds());
#endif
  return true;
}

bool testConstrainedStaticStorageRepeatedLifecycle() {
  for (std::size_t iteration = 0; iteration < 10'000; ++iteration) {
    fake::FakeWakeup wakeup(kOwner, false);
    fake::FakeDisplay display(kOwner, displayCapabilities());
    std::array<qlf::RawInputSample, 1> input_storage{};
    fake::FakeInput input(kOwner, input_storage.data(), input_storage.size());
    std::array<qlf::OwnerTask, 1> task_storage{};
    qlf::OwnerTaskQueue tasks(task_storage.data(), task_storage.size(), 1,
                              &wakeup);
    qlf::BackendLifecycleCoordinator lifecycle(tasks, wakeup, display, input);
    CHECK(lifecycle.open(kOwner, displayConfig(), {1}).ok());
    CHECK(wakeup.waitUntil(kOwner, iteration) ==
          qlf::WakeResult::kUnsupported);
    fake::FakeTaskState task_state{};
    auto task = fake::makeFakeTask(task_state);
    CHECK(tasks.post(std::move(task)).status == qlf::PostStatus::kAccepted);
    CHECK(tasks.pump(kOwner).executed == 1);
    CHECK(lifecycle.beginStop(kOwner, qlf::StopPolicy::kDrain).ok());
    CHECK(lifecycle.finishStop(kOwner).ok());
    CHECK(task_state.executions == 1);
    CHECK(task_state.destructions == 1);
    CHECK(tasks.depth() == 0);
    CHECK(input.depth() == 0);
  }
  return true;
}

struct TestCase {
  const char* name;
  bool (*run)();
};

}  // namespace

int main() {
  const TestCase tests[] = {
      {"owner/bounded/fair", testOwnerBindingBoundedFifoAndFairPump},
      {"multi-producer/wakeup", testMultiProducerAndWakeupBoundary},
      {"clock/cooperative", testClockAndCooperativeWakeup},
      {"display", testDisplayOwnershipValidationAndFailure},
      {"input", testInputBoundedCoalescingAndDrain},
      {"lifecycle/drain", testLifecycleDrainAndDeterministicClose},
      {"lifecycle/cancel/rollback", testLifecycleCancelAndOpenRollback},
      {"close failure", testCloseFailureStillReleasesEverything},
      {"bounded contention/owner destruction",
       testBoundedContentionAndOwnerDestruction},
      {"lifecycle contention retry", testLifecycleContentionRetryConverges},
      {"destructor contract", testDestructorContractIsVisible},
      {"constrained repeated", testConstrainedStaticStorageRepeatedLifecycle},
  };

  for (const auto& test : tests) {
    if (!test.run()) {
      std::fprintf(stderr, "FAILED: %s\n", test.name);
      return 1;
    }
    std::printf("PASS: %s\n", test.name);
  }
  return 0;
}
