#include <array>
#include <atomic>
#include <cstdint>
#include <cstdio>
#include <optional>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include "quickapp/core/foundation/port.h"
#include "quickapp/lvgl/foundation/fakes.h"
#include "quickapp/lvgl/foundation/owner_task_queue.h"
#include "quickapp/lvgl/surface/surface_host.h"

namespace qlf = quickapp::lvgl::foundation;
namespace qfake = quickapp::lvgl::foundation::fakes;
namespace qls = quickapp::lvgl::surface;
namespace qcore = quickapp::core;

namespace {

#define CHECK(expression)                                                     \
  do {                                                                        \
    if (!(expression)) {                                                      \
      std::fprintf(stderr, "CHECK failed at %s:%d: %s\n", __FILE__, __LINE__, \
                   #expression);                                             \
      return false;                                                           \
    }                                                                         \
  } while (false)

constexpr qlf::OwnerToken kOwner{1};

qcore::RequestId request(std::string value) {
  return qcore::RequestId::parse(std::move(value)).value();
}

qcore::SurfaceId surface(std::string value) {
  return qcore::SurfaceId::parse(std::move(value)).value();
}

class ResultPort final : public qcore::CoreIngressPort<qls::SurfaceResult> {
 public:
  qcore::EnqueueResult post(qls::SurfaceResult&& result) noexcept override {
    if (closed_) {
      return qcore::EnqueueResult::failure(qcore::RuntimeError::simple(
          qcore::RuntimeErrorCode::kPlatformRejected, "result port closed"));
    }
    if (busy_ || results_.size() == capacity_) {
      return qcore::EnqueueResult::failure(qcore::RuntimeError::simple(
          qcore::RuntimeErrorCode::kQueueOverflow, "result port full"));
    }
    results_.push_back(std::move(result));
    return qcore::EnqueueResult::success(qcore::Accepted{});
  }

  void close() noexcept override { closed_ = true; }
  void setBusy(bool busy) noexcept { busy_ = busy; }
  void setCapacity(std::size_t capacity) noexcept { capacity_ = capacity; }
  [[nodiscard]] std::size_t size() const noexcept { return results_.size(); }
  [[nodiscard]] std::vector<qls::SurfaceResult> take() noexcept {
    std::vector<qls::SurfaceResult> result;
    result.swap(results_);
    return result;
  }

 private:
  std::vector<qls::SurfaceResult> results_;
  std::size_t capacity_{64};
  bool busy_{false};
  bool closed_{false};
};

class FakeRootBackend final : public qls::PageRootBackend {
 public:
  qls::PageRootCreateResult createHidden(qls::SurfaceViewport) noexcept override {
    std::size_t slot = roots_.size();
    for (std::size_t index = 0; index < roots_.size(); ++index) {
      if (!roots_[index]) {
        slot = index;
        break;
      }
    }
    if (fail_create_ || slot == roots_.size()) {
      return {qlf::LocalError::kBackendFailed, {}};
    }
    const auto handle = qls::PageRootHandle{static_cast<std::uint32_t>(slot + 1)};
    roots_[slot] = true;
    hidden_[slot] = true;
    return {qlf::LocalError::kNone, handle};
  }

  bool valid(qls::PageRootHandle handle) const noexcept override {
    return handle.valid() && handle.value <= roots_.size() &&
           roots_[handle.value - 1];
  }

  void setHiddenNoFail(qls::PageRootHandle handle, bool hidden) noexcept override {
    if (valid(handle)) {
      hidden_[handle.value - 1] = hidden;
      ++visibility_operations_;
    }
  }

  void destroyNoFail(qls::PageRootHandle handle) noexcept override {
    if (valid(handle)) {
      roots_[handle.value - 1] = false;
      hidden_[handle.value - 1] = true;
      ++destroy_operations_;
    }
  }

  void resetNoFail(qls::PageRootHandle handle) noexcept override {
    destroyNoFail(handle);
    ++reset_operations_;
  }

  void failCreate(bool value) noexcept { fail_create_ = value; }
  [[nodiscard]] bool hidden(qls::PageRootHandle handle) const noexcept {
    return valid(handle) && hidden_[handle.value - 1];
  }
  [[nodiscard]] std::size_t visibilityOperations() const noexcept {
    return visibility_operations_;
  }
  [[nodiscard]] std::size_t resetOperations() const noexcept {
    return reset_operations_;
  }

 private:
  std::array<bool, 16> roots_{};
  std::array<bool, 16> hidden_{};
  std::size_t visibility_operations_{0};
  std::size_t destroy_operations_{0};
  std::size_t reset_operations_{0};
  bool fail_create_{false};
};

class FakeContent final : public qls::SurfaceContentLifecyclePort {
 public:
  qlf::LocalResult canRelease(const qcore::SurfaceId&) noexcept override {
    return fail_release_ ? qlf::LocalResult::failure(qlf::LocalError::kBusy)
                          : qlf::LocalResult::success();
  }
  void releaseNoFail(const qcore::SurfaceId&) noexcept override { ++released_; }
  void resetNoFail(const qcore::SurfaceId&) noexcept override { ++resets_; }
  void failRelease(bool value) noexcept { fail_release_ = value; }
  [[nodiscard]] std::size_t released() const noexcept { return released_; }
  [[nodiscard]] std::size_t resets() const noexcept { return resets_; }

 private:
  bool fail_release_{false};
  std::size_t released_{0};
  std::size_t resets_{0};
};

bool pump(qls::SurfaceHostAdapter& adapter, qlf::OwnerTaskQueue& tasks,
          ResultPort& results) {
  const auto pumped = tasks.pump(kOwner, 64);
  CHECK(pumped.ok());
  (void)adapter.service(kOwner, 64);
  CHECK(adapter.pendingOperationCount() == 0);
  CHECK(results.size() != 0);
  return true;
}

bool testSurfaceStateMachineAndAtomicCommands() {
  qfake::FakeWakeup wakeup(kOwner);
  std::array<qlf::OwnerTask, 64> task_storage{};
  qlf::OwnerTaskQueue tasks(task_storage.data(), task_storage.size(), 64, &wakeup);
  CHECK(tasks.bindOwner(kOwner).ok());
  FakeRootBackend roots;
  FakeContent content;
  ResultPort results;
  qls::SurfaceHostAdapter host(tasks, kOwner, roots, content, results,
                               qls::simulatorSurfaceHostLimits());

  const auto first = surface("srf:first");
  const auto second = surface("srf:second");
  CHECK(host.post(qls::CreateSurfaceHost{request("req:create-first"), first,
                                         {320, 240}}));
  CHECK(pump(host, tasks, results));
  auto output = results.take();
  CHECK(std::get<qls::CreateSurfaceHostResult>(output.front()).status ==
        qls::SurfaceResultStatus::kCreated);
  const auto replay = host.post(qls::CreateSurfaceHost{
      request("req:create-first"), first, {320, 240}});
  CHECK(!replay &&
        replay.error().code == qcore::RuntimeErrorCode::kPlatformRejected);
  CHECK(host.markFullMountCommitted(kOwner, first).ok());
  CHECK(host.post(qls::PresentRootSurfaceHost{request("req:present-first"), first}));
  CHECK(pump(host, tasks, results));
  output = results.take();
  CHECK(std::get<qls::PresentRootSurfaceHostResult>(output.front()).status ==
        qls::SurfaceResultStatus::kPresented);

  CHECK(host.post(qls::CreateSurfaceHost{request("req:create-second"), second,
                                         {320, 240}}));
  CHECK(pump(host, tasks, results));
  (void)results.take();
  CHECK(host.markFullMountCommitted(kOwner, second).ok());
  CHECK(host.post(qls::PresentPushSurfaceHost{request("req:push"), second, first}));
  CHECK(pump(host, tasks, results));
  output = results.take();
  CHECK(std::get<qls::PresentPushSurfaceHostResult>(output.front()).status ==
        qls::SurfaceResultStatus::kPresented);
  CHECK(host.post(qls::SetSurfaceVisibility{
      request("req:hide"), first, qls::SurfaceVisibility::kHidden}));
  CHECK(pump(host, tasks, results));
  output = results.take();
  CHECK(std::get<qls::SetSurfaceVisibilityResult>(output.front()).status ==
        qls::SurfaceResultStatus::kCompleted);
  const std::size_t operations_before_noop = roots.visibilityOperations();
  CHECK(host.post(qls::SetSurfaceVisibility{
      request("req:hide-noop"), first, qls::SurfaceVisibility::kHidden}));
  CHECK(pump(host, tasks, results));
  (void)results.take();
  CHECK(roots.visibilityOperations() == operations_before_noop);

  content.failRelease(true);
  CHECK(host.post(qls::CloseSurfaceHost{request("req:close"), second, first}));
  CHECK(pump(host, tasks, results));
  output = results.take();
  CHECK(std::get<qls::CloseSurfaceHostResult>(output.front()).status ==
        qls::SurfaceResultStatus::kFailed);
  CHECK(content.released() == 0);
  content.failRelease(false);

  CHECK(host.post(qls::SetSurfaceVisibility{
      request("req:show"), first, qls::SurfaceVisibility::kVisible}));
  CHECK(pump(host, tasks, results));
  (void)results.take();
  CHECK(host.post(qls::DestroySurfaceHost{request("req:destroy"), first}));
  CHECK(pump(host, tasks, results));
  output = results.take();
  CHECK(std::get<qls::DestroySurfaceHostResult>(output.front()).status ==
        qls::SurfaceResultStatus::kDestroyed);
  CHECK(host.liveRootCount() == 1);

  CHECK(host.post(qls::DestroySurfaceHost{request("req:destroy-second"), second}));
  CHECK(pump(host, tasks, results));
  (void)results.take();
  CHECK(host.liveRootCount() == 0);

  for (std::size_t iteration = 0; iteration < 10'000; ++iteration) {
    const auto loop_surface = surface("srf:loop");
    const auto suffix = std::to_string(iteration);
    CHECK(host.post(qls::CreateSurfaceHost{
        request("req:loop-create-" + suffix), loop_surface, {100, 100}}));
    CHECK(pump(host, tasks, results));
    (void)results.take();
    CHECK(host.markFullMountCommitted(kOwner, loop_surface).ok());
    CHECK(host.post(qls::PresentRootSurfaceHost{
        request("req:loop-present-" + suffix), loop_surface}));
    CHECK(pump(host, tasks, results));
    (void)results.take();
    CHECK(host.post(qls::SetSurfaceVisibility{
        request("req:loop-hide-" + suffix), loop_surface,
        qls::SurfaceVisibility::kHidden}));
    CHECK(pump(host, tasks, results));
    (void)results.take();
    CHECK(host.post(qls::SetSurfaceVisibility{
        request("req:loop-show-" + suffix), loop_surface,
        qls::SurfaceVisibility::kVisible}));
    CHECK(pump(host, tasks, results));
    (void)results.take();
    CHECK(host.post(qls::DestroySurfaceHost{
        request("req:loop-destroy-" + suffix), loop_surface}));
    CHECK(pump(host, tasks, results));
    (void)results.take();
    CHECK(host.liveRootCount() == 0 && host.pendingOperationCount() == 0);
  }
  host.close();
  CHECK(host.finishClose(kOwner).ok());
  CHECK(tasks.beginStop(kOwner, qlf::StopPolicy::kDrain).ok());
  CHECK(tasks.finishStop(kOwner).ok());
  return true;
}

bool testSurfaceBackpressureConflictAndReset() {
  qfake::FakeWakeup wakeup(kOwner);
  std::array<qlf::OwnerTask, 64> task_storage{};
  qlf::OwnerTaskQueue tasks(task_storage.data(), task_storage.size(), 64, &wakeup);
  CHECK(tasks.bindOwner(kOwner).ok());
  FakeRootBackend roots;
  FakeContent content;
  ResultPort results;
  results.setBusy(true);
  qls::SurfaceHostAdapter host(tasks, kOwner, roots, content, results,
                               qls::embeddedSurfaceHostLimits());
  const auto first = surface("srf:busy");
  auto first_create = qls::CreateSurfaceHost{request("req:busy-create"), first,
                                             {100, 100}};
  CHECK(host.post(std::move(first_create)));
  const auto conflict = host.post(qls::DestroySurfaceHost{
      request("req:conflict"), first});
  CHECK(!conflict &&
        conflict.error().code == qcore::RuntimeErrorCode::kPlatformRejected);
  CHECK(tasks.pump(kOwner, 64).ok());
  (void)host.service(kOwner, 64);
  CHECK(host.pendingResultCount() == 1);
  results.setBusy(false);
  CHECK(host.service(kOwner, 64).delivered == 1);
  (void)results.take();
  CHECK(host.liveRootCount() == 1);
  content.failRelease(true);
  CHECK(host.post(qls::DestroySurfaceHost{request("req:reset"), first}));
  CHECK(pump(host, tasks, results));
  const auto output = results.take();
  CHECK(std::get<qls::DestroySurfaceHostResult>(output.front()).status ==
        qls::SurfaceResultStatus::kFailed);
  CHECK(host.liveRootCount() == 0);
  CHECK(host.resetCount() == 1);
  host.close();
  CHECK(host.finishClose(kOwner).ok());
  CHECK(tasks.beginStop(kOwner, qlf::StopPolicy::kDrain).ok());
  CHECK(tasks.finishStop(kOwner).ok());
  return true;
}

bool testSurfaceMultiProducerAdmission() {
  qfake::FakeWakeup wakeup(kOwner);
  std::array<qlf::OwnerTask, 64> task_storage{};
  qlf::OwnerTaskQueue tasks(task_storage.data(), task_storage.size(), 64, &wakeup);
  CHECK(tasks.bindOwner(kOwner).ok());
  FakeRootBackend roots;
  FakeContent content;
  ResultPort results;
  qls::SurfaceHostAdapter host(tasks, kOwner, roots, content, results,
                               qls::simulatorSurfaceHostLimits());
  std::array<std::atomic<bool>, 4> accepted{};
  std::array<std::thread, 4> producers;
  for (std::size_t index = 0; index < producers.size(); ++index) {
    producers[index] = std::thread([&, index] {
      const auto id = surface("srf:producer-" + std::to_string(index));
      const auto result = host.post(qls::CreateSurfaceHost{
          request("req:producer-" + std::to_string(index)), id, {80, 80}});
      accepted[index].store(static_cast<bool>(result), std::memory_order_release);
    });
  }
  for (auto& producer : producers) {
    producer.join();
  }
  std::size_t accepted_count = 0;
  for (const auto& value : accepted) {
    accepted_count += value.load(std::memory_order_acquire) ? 1U : 0U;
  }
  CHECK(accepted_count != 0);
  CHECK(tasks.pump(kOwner, 64).ok());
  (void)host.service(kOwner, 64);
  CHECK(results.size() == accepted_count);
  (void)results.take();
  host.close();
  CHECK(host.finishClose(kOwner).ok());
  CHECK(host.liveRootCount() == 0 && host.pendingOperationCount() == 0);
  CHECK(tasks.beginStop(kOwner, qlf::StopPolicy::kDrain).ok());
  CHECK(tasks.finishStop(kOwner).ok());
  return true;
}

}  // namespace

int main() {
  const bool ok = testSurfaceStateMachineAndAtomicCommands() &&
                  testSurfaceBackpressureConflictAndReset() &&
                  testSurfaceMultiProducerAdmission();
  std::puts(ok ? "LV-S03 contract tests: PASS" : "LV-S03 contract tests: FAIL");
  return ok ? 0 : 1;
}
