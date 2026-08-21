#pragma once

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>

#include "quickapp/core/foundation/id.h"
#include "quickapp/core/foundation/observation.h"
#include "quickapp/js/engine/js_engine_port.h"
#include "quickapp/lvgl/foundation/backend_lifecycle.h"
#include "quickapp/lvgl/runtime/loop_backend.h"
#include "quickapp/lvgl/runtime/package_source.h"
#include "quickapp/lvgl/runtime/runtime_types.h"

namespace quickapp::lvgl::runtime {

enum class HostState : std::uint8_t {
  kNew,
  kStarting,
  kRunning,
  kDestroying,
  kFailed,
  kDestroyed,
};

enum class LifecycleAction : std::uint8_t {
  kEnterForeground,
  kEnterBackground,
  kDestroyAppRuntime,
};

enum class RawHostSignal : std::uint8_t {
  kResume,
  kSuspend,
  kShutdown,
};

struct CoreStartResult final {
  bool presented{false};
  RuntimeCallResult result{RuntimeCallResult::fail(
      core::RuntimeErrorCode::kPlatformRejected)};
};

struct RuntimeLifecycleControl final {
  core::RequestId request_id;
  LifecycleAction action;
};

struct RuntimeLifecycleControlResult final {
  core::RequestId request_id;
  LifecycleAction action;
  RuntimeCallResult result;
};

using CoreStartResultCallback = void (*)(void*,
                                         const CoreStartResult&) noexcept;
using CoreLifecycleResultCallback = void (*)(
    void*, const RuntimeLifecycleControlResult&) noexcept;

struct CoreStartCompletion final {
  CoreStartResultCallback callback{nullptr};
  void* context{nullptr};
};

struct CoreLifecycleCompletion final {
  CoreLifecycleResultCallback callback{nullptr};
  void* context{nullptr};
};

class JsRuntimeFactoryPort {
 public:
  virtual ~JsRuntimeFactoryPort() = default;
  [[nodiscard]] virtual RuntimeCallResult start(
      js::JsEngineProvider& provider, core::TraceSink& trace_sink) noexcept = 0;
  [[nodiscard]] virtual RuntimeCallResult stop() noexcept = 0;
  [[nodiscard]] virtual std::size_t liveServices() const noexcept = 0;
};

class CoreRuntimePort {
 public:
  virtual ~CoreRuntimePort() = default;
  [[nodiscard]] virtual core::RuntimeResult<core::RequestId>
  allocateRequestId() noexcept = 0;
  [[nodiscard]] virtual RuntimeCallResult start(
      const RuntimeLaunchProfileView& profile, PackageSource& package_source,
      JsRuntimeFactoryPort& js_runtime_factory,
      js::JsEngineProvider& engine_provider, core::TraceSink& trace_sink,
      CoreStartCompletion completion) noexcept = 0;
  [[nodiscard]] virtual RuntimeCallResult control(
      const RuntimeLifecycleControl& control,
      CoreLifecycleCompletion completion) noexcept = 0;
};

using HostCompletionCallback = void (*)(void*, RuntimeCallResult) noexcept;

struct HostCompletion final {
  HostCompletionCallback callback{nullptr};
  void* context{nullptr};
};

struct HostPumpResult final {
  foundation::LocalError error{foundation::LocalError::kNone};
  std::size_t tasks_executed{0};
  std::size_t loop_callbacks{0};
  std::size_t package_completions{0};
  std::size_t core_completions{0};
};

struct RuntimeHostDependencies final {
  const CompositionValidation& composition;
  foundation::OwnerToken owner;
  foundation::OwnerTaskQueue& owner_tasks;
  foundation::BackendLifecycleCoordinator& backend_lifecycle;
  OwnerLoopBackend& owner_loop;
  PackageSourceFactory& package_source_factory;
  CoreRuntimePort& core_runtime;
  JsRuntimeFactoryPort& js_runtime_factory;
  js::JsEngineProvider& engine_provider;
  core::TraceSink& trace_sink;
};

class RuntimeHost final {
 public:
  static constexpr std::size_t kMaxPendingControls = 8;

  explicit RuntimeHost(RuntimeHostDependencies dependencies) noexcept;
  ~RuntimeHost() noexcept;
  RuntimeHost(const RuntimeHost&) = delete;
  RuntimeHost& operator=(const RuntimeHost&) = delete;

  [[nodiscard]] RuntimeCallResult start(
      const RuntimeLaunchProfileView& profile,
      HostCompletion completion) noexcept;
  [[nodiscard]] RuntimeCallResult control(
      LifecycleAction action, HostCompletion completion) noexcept;
  [[nodiscard]] RuntimeCallResult destroy(
      HostCompletion completion) noexcept;
  [[nodiscard]] foundation::PostOutcome postHostSignal(
      RawHostSignal signal, HostCompletion completion = {}) noexcept;
  [[nodiscard]] HostPumpResult pumpOnce() noexcept;

  [[nodiscard]] HostState state() const noexcept { return state_; }
  [[nodiscard]] const CompositionValidation& describeComposition()
      const noexcept {
    return composition_;
  }
  [[nodiscard]] std::uint64_t duplicateSignalCount() const noexcept {
    return duplicate_signal_count_.load(std::memory_order_relaxed);
  }
  [[nodiscard]] std::uint64_t queueOverflowCount() const noexcept {
    return queue_overflow_count_.load(std::memory_order_relaxed);
  }
  [[nodiscard]] bool resourcesReleased() const noexcept;

 private:
  struct PendingControl final {
    RuntimeHost* host{nullptr};
    std::size_t index{0};
    bool active{false};
    bool destroy{false};
    LifecycleAction action{LifecycleAction::kEnterForeground};
    std::optional<core::RequestId> request_id;
    HostCompletion caller;
    std::atomic<bool> ready{false};
    std::atomic<bool> matched{false};
    std::atomic<bool> result_success{false};
    std::atomic<bool> result_retryable{false};
    std::atomic<core::RuntimeErrorCode> result_error{
        core::RuntimeErrorCode::kPlatformRejected};
  };

  [[nodiscard]] RuntimeCallResult validateLaunchProfile(
      const RuntimeLaunchProfileView& profile) const noexcept;
  [[nodiscard]] PendingControl* allocateControlSlot() noexcept;
  void releaseControlSlot(PendingControl& slot) noexcept;
  void handleRawSignal(RawHostSignal signal,
                       HostCompletion completion) noexcept;
  std::size_t processCoreCompletions() noexcept;
  void progressTeardown() noexcept;
  void completeStart(RuntimeCallResult result) noexcept;
  void completeDestroy(RuntimeCallResult result) noexcept;
  [[nodiscard]] RuntimeCallResult beginControl(
      LifecycleAction action, bool destroy,
      HostCompletion completion) noexcept;

  static void onCoreStart(void* context,
                          const CoreStartResult& result) noexcept;
  static void onCoreLifecycle(
      void* context,
      const RuntimeLifecycleControlResult& result) noexcept;

  const CompositionValidation& composition_;
  foundation::OwnerToken owner_;
  foundation::OwnerTaskQueue* owner_tasks_;
  foundation::BackendLifecycleCoordinator* backend_lifecycle_;
  OwnerLoopBackend* owner_loop_;
  PackageSourceFactory* package_source_factory_;
  CoreRuntimePort* core_runtime_;
  JsRuntimeFactoryPort* js_runtime_factory_;
  js::JsEngineProvider* engine_provider_;
  core::TraceSink* trace_sink_;
  std::unique_ptr<PackageSource> package_source_;
  HostState state_{HostState::kNew};
  RuntimeLaunchProfileView launch_profile_{};
  HostCompletion start_completion_{};
  HostCompletion destroy_completion_{};
  std::optional<RawHostSignal> last_raw_signal_;
  std::array<PendingControl, kMaxPendingControls> pending_controls_{};
  std::optional<std::size_t> destroy_slot_;
  bool teardown_started_{false};
  bool start_callback_pending_{false};
  bool package_closed_{false};
  bool backend_stop_begun_{false};
  RuntimeCallResult teardown_result_{RuntimeCallResult::ok()};
  std::atomic<bool> start_ready_{false};
  std::atomic<bool> start_presented_{false};
  std::atomic<bool> start_result_success_{false};
  std::atomic<bool> start_result_retryable_{false};
  std::atomic<core::RuntimeErrorCode> start_result_error_{
      core::RuntimeErrorCode::kPlatformRejected};
  std::atomic<std::uint64_t> duplicate_signal_count_{0};
  std::atomic<std::uint64_t> queue_overflow_count_{0};
};

}  // namespace quickapp::lvgl::runtime
