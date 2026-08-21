#include "quickapp/lvgl/runtime/runtime_host.h"

#include <cassert>
#include <utility>

namespace quickapp::lvgl::runtime {
namespace {

RuntimeCallResult localFailure(foundation::LocalError error) noexcept {
  switch (error) {
    case foundation::LocalError::kNone:
      return RuntimeCallResult::ok();
    case foundation::LocalError::kCapacityExhausted:
      return RuntimeCallResult::fail(core::RuntimeErrorCode::kQueueOverflow,
                                     true);
    case foundation::LocalError::kBusy:
      return RuntimeCallResult::fail(core::RuntimeErrorCode::kLifecycleBusy,
                                     true);
    case foundation::LocalError::kUnsupported:
      return RuntimeCallResult::fail(
          core::RuntimeErrorCode::kHostFeatureUnsupported);
    case foundation::LocalError::kInvalidArgument:
    case foundation::LocalError::kWrongThread:
    case foundation::LocalError::kInvalidState:
      return RuntimeCallResult::fail(
          core::RuntimeErrorCode::kAbiInvalidArgument);
    case foundation::LocalError::kBackendFailed:
      return RuntimeCallResult::fail(
          core::RuntimeErrorCode::kPlatformRejected);
  }
  return RuntimeCallResult::fail(core::RuntimeErrorCode::kPlatformRejected);
}

}  // namespace

RuntimeHost::RuntimeHost(RuntimeHostDependencies dependencies) noexcept
    : composition_(dependencies.composition),
      owner_(dependencies.owner),
      owner_tasks_(&dependencies.owner_tasks),
      backend_lifecycle_(&dependencies.backend_lifecycle),
      owner_loop_(&dependencies.owner_loop),
      package_source_factory_(&dependencies.package_source_factory),
      core_runtime_(&dependencies.core_runtime),
      js_runtime_factory_(&dependencies.js_runtime_factory),
      engine_provider_(&dependencies.engine_provider),
      trace_sink_(&dependencies.trace_sink) {
  for (std::size_t index = 0; index < pending_controls_.size(); ++index) {
    pending_controls_[index].host = this;
    pending_controls_[index].index = index;
  }
}

RuntimeHost::~RuntimeHost() noexcept {
  assert((state_ == HostState::kNew || state_ == HostState::kDestroyed) &&
         resourcesReleased() &&
         "RuntimeHost requires explicit deterministic teardown");
}

RuntimeCallResult RuntimeHost::start(
    const RuntimeLaunchProfileView& profile,
    HostCompletion completion) noexcept {
  if (state_ != HostState::kNew || !composition_.ok() ||
      composition_.profile == nullptr || !owner_.valid() ||
      owner_tasks_->capacity() !=
          composition_.profile->limits.owner_task_capacity) {
    return RuntimeCallResult::fail(
        core::RuntimeErrorCode::kRuntimeProfileIncompatible);
  }
  const RuntimeCallResult validation = validateLaunchProfile(profile);
  if (!validation.success) {
    return validation;
  }
  foundation::LocalResult local = owner_loop_->initialize(owner_);
  if (!local.ok()) {
    return localFailure(local.error);
  }

  const foundation::DisplayConfig display_config{
      profile.viewport_width, profile.viewport_height,
      foundation::PixelFormat::kRgba8888,
      composition_.profile->limits.max_display_submissions_per_pump};
  const foundation::InputConfig input_config{
      composition_.profile->limits.max_raw_samples_per_pump};
  local = backend_lifecycle_->open(owner_, display_config, input_config);
  if (!local.ok()) {
    state_ = HostState::kDestroyed;
    return localFailure(local.error);
  }

  RuntimeCallResult source_result = package_source_factory_->create(
      profile.artifact, package_source_);
  if (!source_result.success) {
    state_ = HostState::kFailed;
    teardown_result_ = source_result;
    teardown_started_ = true;
    progressTeardown();
    return source_result;
  }

  launch_profile_ = profile;
  start_completion_ = completion;
  state_ = HostState::kStarting;
  const RuntimeCallResult admission = core_runtime_->start(
      launch_profile_, *package_source_, *js_runtime_factory_,
      *engine_provider_, *trace_sink_, {&RuntimeHost::onCoreStart, this});
  if (!admission.success) {
    state_ = HostState::kFailed;
    teardown_result_ = admission;
    teardown_started_ = true;
    start_completion_ = {};
    progressTeardown();
    return admission;
  }
  start_callback_pending_ = true;
  return RuntimeCallResult::ok();
}

RuntimeCallResult RuntimeHost::control(
    LifecycleAction action, HostCompletion completion) noexcept {
  if (action == LifecycleAction::kDestroyAppRuntime) {
    return destroy(completion);
  }
  if (state_ != HostState::kRunning) {
    return RuntimeCallResult::fail(core::RuntimeErrorCode::kLifecycleBusy,
                                   true);
  }
  return beginControl(action, false, completion);
}

RuntimeCallResult RuntimeHost::destroy(HostCompletion completion) noexcept {
  if (state_ != HostState::kRunning && state_ != HostState::kStarting) {
    return RuntimeCallResult::fail(core::RuntimeErrorCode::kLifecycleBusy,
                                   true);
  }
  const HostState previous_state = state_;
  state_ = HostState::kDestroying;
  destroy_completion_ = completion;
  const RuntimeCallResult result = beginControl(
      LifecycleAction::kDestroyAppRuntime, true, completion);
  if (!result.success) {
    state_ = previous_state;
    destroy_completion_ = {};
  }
  return result;
}

foundation::PostOutcome RuntimeHost::postHostSignal(
    RawHostSignal signal, HostCompletion completion) noexcept {
  foundation::OwnerTask task = foundation::OwnerTask::make(
      [this, signal, completion]() noexcept {
        handleRawSignal(signal, completion);
      });
  const foundation::PostOutcome outcome = owner_tasks_->post(std::move(task));
  if (outcome.status == foundation::PostStatus::kFull) {
    ++queue_overflow_count_;
  }
  return outcome;
}

HostPumpResult RuntimeHost::pumpOnce() noexcept {
  HostPumpResult report;
  if (state_ == HostState::kDestroyed || state_ == HostState::kNew) {
    report.error = foundation::LocalError::kInvalidState;
    return report;
  }

  const foundation::PumpResult tasks = owner_tasks_->pump(owner_);
  report.tasks_executed = tasks.executed;
  if (!tasks.ok() && tasks.error != foundation::LocalError::kInvalidState) {
    report.error = tasks.error;
    return report;
  }

  if (backend_lifecycle_->state() != foundation::LifecycleState::kClosed) {
    const foundation::LocalResult loop_result = owner_loop_->serviceOneTurn(
        owner_, composition_.profile->limits.max_timer_callbacks_per_pump);
    if (!loop_result.ok() &&
        loop_result.error != foundation::LocalError::kUnsupported) {
      report.error = loop_result.error;
    } else {
      report.loop_callbacks = 1;
    }
  }
  if (package_source_ != nullptr) {
    report.package_completions = package_source_->serviceCompletions(
        composition_.profile->limits.max_in_flight_package_reads);
  }
  report.core_completions = processCoreCompletions();
  progressTeardown();
  return report;
}

RuntimeCallResult RuntimeHost::validateLaunchProfile(
    const RuntimeLaunchProfileView& profile) const noexcept {
  if (profile.has_unknown_fields || profile.target != LaunchTarget::kLvgl ||
      profile.artifact.empty() || !profile.params_is_runtime_value_object ||
      profile.viewport_width == 0 || profile.viewport_height == 0 ||
      !profile.viewport_is_logical_px ||
      (!profile.entry_route.empty() && profile.entry_route.front() != '/')) {
    return RuntimeCallResult::fail(
        core::RuntimeErrorCode::kAbiInvalidArgument);
  }
  return RuntimeCallResult::ok();
}

RuntimeHost::PendingControl* RuntimeHost::allocateControlSlot() noexcept {
  for (PendingControl& slot : pending_controls_) {
    if (!slot.active) {
      slot.active = true;
      slot.ready.store(false, std::memory_order_relaxed);
      slot.matched.store(false, std::memory_order_relaxed);
      slot.result_success.store(false, std::memory_order_relaxed);
      slot.result_retryable.store(false, std::memory_order_relaxed);
      slot.result_error.store(core::RuntimeErrorCode::kPlatformRejected,
                              std::memory_order_relaxed);
      return &slot;
    }
  }
  return nullptr;
}

void RuntimeHost::releaseControlSlot(PendingControl& slot) noexcept {
  slot.request_id.reset();
  slot.caller = {};
  slot.destroy = false;
  slot.active = false;
  slot.ready.store(false, std::memory_order_relaxed);
}

void RuntimeHost::handleRawSignal(RawHostSignal signal,
                                  HostCompletion completion) noexcept {
  if (last_raw_signal_.has_value() && *last_raw_signal_ == signal) {
    ++duplicate_signal_count_;
    if (completion.callback != nullptr) {
      completion.callback(completion.context, RuntimeCallResult::ok());
    }
    return;
  }
  RuntimeCallResult result = RuntimeCallResult::ok();
  switch (signal) {
    case RawHostSignal::kResume:
      result = control(LifecycleAction::kEnterForeground, completion);
      break;
    case RawHostSignal::kSuspend:
      result = control(LifecycleAction::kEnterBackground, completion);
      break;
    case RawHostSignal::kShutdown:
      result = destroy(completion);
      break;
  }
  if (result.success) {
    last_raw_signal_ = signal;
  }
  if (!result.success && completion.callback != nullptr) {
    completion.callback(completion.context, result);
  }
}

std::size_t RuntimeHost::processCoreCompletions() noexcept {
  std::size_t completed = 0;
  if (start_ready_.exchange(false, std::memory_order_acq_rel)) {
    ++completed;
    start_callback_pending_ = false;
    const RuntimeCallResult result{
        start_result_success_.load(std::memory_order_relaxed),
        start_result_error_.load(std::memory_order_relaxed),
        start_result_retryable_.load(std::memory_order_relaxed)};
    const bool presented =
        start_presented_.load(std::memory_order_relaxed);
    if (result.success && presented && state_ == HostState::kStarting) {
      state_ = HostState::kRunning;
      completeStart(RuntimeCallResult::ok());
    } else if (state_ != HostState::kDestroying) {
      state_ = HostState::kFailed;
      teardown_result_ = result.success
                             ? RuntimeCallResult::fail(
                                   core::RuntimeErrorCode::
                                       kSurfacePresentationFailed)
                             : result;
      teardown_started_ = true;
    }
  }

  for (PendingControl& slot : pending_controls_) {
    if (!slot.active ||
        !slot.ready.exchange(false, std::memory_order_acq_rel)) {
      continue;
    }
    ++completed;
    RuntimeCallResult result{
        slot.result_success.load(std::memory_order_relaxed),
        slot.result_error.load(std::memory_order_relaxed),
        slot.result_retryable.load(std::memory_order_relaxed)};
    if (!slot.matched.load(std::memory_order_relaxed)) {
      result = RuntimeCallResult::fail(
          core::RuntimeErrorCode::kAbiInvalidArgument);
    }
    const bool destroys_runtime = slot.destroy;
    const HostCompletion caller = slot.caller;
    releaseControlSlot(slot);
    if (destroys_runtime) {
      destroy_slot_.reset();
      destroy_completion_ = caller;
      teardown_result_ = result;
      teardown_started_ = true;
    } else if (caller.callback != nullptr) {
      caller.callback(caller.context, result);
    }
  }
  return completed;
}

void RuntimeHost::progressTeardown() noexcept {
  if (!teardown_started_ || state_ == HostState::kDestroyed) {
    return;
  }
  if (start_callback_pending_) {
    return;
  }
  for (const PendingControl& slot : pending_controls_) {
    if (slot.active) {
      return;
    }
  }
  if (js_runtime_factory_->liveServices() != 0) {
    const RuntimeCallResult js_stop = js_runtime_factory_->stop();
    if (!js_stop.success && teardown_result_.success) {
      teardown_result_ = js_stop;
    }
    if (js_runtime_factory_->liveServices() != 0) {
      return;
    }
  }
  if (package_source_ != nullptr && !package_closed_) {
    (void)package_source_->serviceCompletions(
        composition_.profile->limits.max_in_flight_package_reads);
    const foundation::LocalResult package_close = package_source_->close();
    if (package_close.error == foundation::LocalError::kBusy) {
      return;
    }
    if (!package_close.ok() && teardown_result_.success) {
      teardown_result_ = localFailure(package_close.error);
    }
    package_closed_ = true;
    package_source_.reset();
  }

  if (backend_lifecycle_->state() == foundation::LifecycleState::kRunning &&
      !backend_stop_begun_) {
    const foundation::LocalResult begin = backend_lifecycle_->beginStop(
        owner_, foundation::StopPolicy::kDrain);
    if (begin.error == foundation::LocalError::kBusy) {
      return;
    }
    backend_stop_begun_ = true;
    if (!begin.ok() && teardown_result_.success) {
      teardown_result_ = localFailure(begin.error);
    }
  }
  if (backend_lifecycle_->state() == foundation::LifecycleState::kStopping) {
    const foundation::LocalResult finish =
        backend_lifecycle_->finishStop(owner_);
    if (finish.error == foundation::LocalError::kBusy) {
      return;
    }
    if (!finish.ok() && teardown_result_.success) {
      teardown_result_ = localFailure(finish.error);
    }
  }
  if (backend_lifecycle_->state() != foundation::LifecycleState::kClosed) {
    return;
  }

  if (js_runtime_factory_->liveServices() != 0 &&
      teardown_result_.success) {
    teardown_result_ = RuntimeCallResult::fail(
        core::RuntimeErrorCode::kPlatformRejected);
  }
  state_ = HostState::kDestroyed;
  teardown_started_ = false;
  if (start_completion_.callback != nullptr) {
    completeStart(teardown_result_);
  }
  completeDestroy(teardown_result_);
}

void RuntimeHost::completeStart(RuntimeCallResult result) noexcept {
  const HostCompletion completion = start_completion_;
  start_completion_ = {};
  if (completion.callback != nullptr) {
    completion.callback(completion.context, result);
  }
}

void RuntimeHost::completeDestroy(RuntimeCallResult result) noexcept {
  const HostCompletion completion = destroy_completion_;
  destroy_completion_ = {};
  if (completion.callback != nullptr) {
    completion.callback(completion.context, result);
  }
}

RuntimeCallResult RuntimeHost::beginControl(
    LifecycleAction action, bool destroys_runtime,
    HostCompletion completion) noexcept {
  PendingControl* slot = allocateControlSlot();
  if (slot == nullptr) {
    ++queue_overflow_count_;
    return RuntimeCallResult::fail(core::RuntimeErrorCode::kQueueOverflow,
                                   true);
  }
  core::RuntimeResult<core::RequestId> request =
      core_runtime_->allocateRequestId();
  if (!request.has_value()) {
    const RuntimeCallResult error = RuntimeCallResult::fail(
        request.error().code, request.error().retryable);
    releaseControlSlot(*slot);
    return error;
  }
  slot->request_id.emplace(std::move(request).value());
  slot->action = action;
  slot->destroy = destroys_runtime;
  slot->caller = completion;
  if (destroys_runtime) {
    destroy_slot_ = slot->index;
  }

  const RuntimeLifecycleControl message{*slot->request_id, action};
  const RuntimeCallResult admission = core_runtime_->control(
      message, {&RuntimeHost::onCoreLifecycle, slot});
  if (!admission.success) {
    if (destroys_runtime) {
      destroy_slot_.reset();
    }
    releaseControlSlot(*slot);
    return admission;
  }
  return RuntimeCallResult::ok();
}

void RuntimeHost::onCoreStart(void* context,
                              const CoreStartResult& result) noexcept {
  auto* host = static_cast<RuntimeHost*>(context);
  host->start_presented_.store(result.presented,
                               std::memory_order_relaxed);
  host->start_result_success_.store(result.result.success,
                                    std::memory_order_relaxed);
  host->start_result_retryable_.store(result.result.retryable,
                                      std::memory_order_relaxed);
  host->start_result_error_.store(result.result.error,
                                  std::memory_order_relaxed);
  host->start_ready_.store(true, std::memory_order_release);
  (void)host->owner_loop_->notify();
}

void RuntimeHost::onCoreLifecycle(
    void* context,
    const RuntimeLifecycleControlResult& result) noexcept {
  auto* slot = static_cast<PendingControl*>(context);
  const bool matched =
      slot->request_id.has_value() &&
      *slot->request_id == result.request_id && slot->action == result.action;
  slot->matched.store(matched, std::memory_order_relaxed);
  slot->result_success.store(result.result.success,
                             std::memory_order_relaxed);
  slot->result_retryable.store(result.result.retryable,
                               std::memory_order_relaxed);
  slot->result_error.store(result.result.error, std::memory_order_relaxed);
  slot->ready.store(true, std::memory_order_release);
  (void)slot->host->owner_loop_->notify();
}

bool RuntimeHost::resourcesReleased() const noexcept {
  if (package_source_ != nullptr || js_runtime_factory_->liveServices() != 0) {
    return false;
  }
  for (const PendingControl& slot : pending_controls_) {
    if (slot.active) {
      return false;
    }
  }
  return state_ == HostState::kNew ||
         (state_ == HostState::kDestroyed &&
          backend_lifecycle_->state() == foundation::LifecycleState::kClosed);
}

}  // namespace quickapp::lvgl::runtime
