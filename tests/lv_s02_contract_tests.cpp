#include <SDL3/SDL.h>
#include <fcntl.h>
#include <unistd.h>

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <memory>
#include <string_view>
#include <thread>
#include <utility>

#include "quickapp/js/engine/quickjs_engine_provider.h"
#include "quickapp/lvgl/backends/embedded_backends.h"
#include "quickapp/lvgl/backends/libuv_file_package_source.h"
#include "quickapp/lvgl/backends/libuv_loop_backend.h"
#include "quickapp/lvgl/backends/sdl_backends.h"
#include "quickapp/lvgl/foundation/backend_lifecycle.h"
#include "quickapp/lvgl/foundation/fakes.h"
#include "quickapp/lvgl/runtime/composition.h"
#include "quickapp/lvgl/runtime/runtime_host.h"
#include "quickapp/lvgl/runtime/trace_adapter.h"

namespace qlf = quickapp::lvgl::foundation;
namespace qfake = quickapp::lvgl::foundation::fakes;
namespace qlr = quickapp::lvgl::runtime;
namespace qlb = quickapp::lvgl::backends;
namespace qjs = quickapp::js;
namespace qcore = quickapp::core;

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

constexpr std::array<std::string_view, 3> kComponents{"View", "Text",
                                                       "Button"};
constexpr std::array<std::string_view, 3> kCapabilities{
    "system.router", "system.prompt", "system.device"};

constexpr std::array<qlr::ModuleDescriptor, 20> kEmbeddedModules{{
    {"kernel.bridge", qlr::ModuleCategory::kKernel, "1"},
    {"kernel.render", qlr::ModuleCategory::kKernel, "1"},
    {"kernel.event", qlr::ModuleCategory::kKernel, "1"},
    {"kernel.lifecycle", qlr::ModuleCategory::kKernel, "1"},
    {"kernel.runtime-tree", qlr::ModuleCategory::kKernel, "1"},
    {"kernel.transaction", qlr::ModuleCategory::kKernel, "1"},
    {"runtime.js-framework", qlr::ModuleCategory::kRuntime, "1"},
    {"engine.quickjs", qlr::ModuleCategory::kEngine, "1"},
    {"platform.lvgl.host", qlr::ModuleCategory::kPlatform, "1"},
    {"platform.lvgl.trace", qlr::ModuleCategory::kPlatform, "1"},
    {"backend.lvgl.builtin.loop", qlr::ModuleCategory::kBackend, "1"},
    {"backend.lvgl.embedded.display", qlr::ModuleCategory::kBackend, "1"},
    {"backend.lvgl.embedded.input", qlr::ModuleCategory::kBackend, "1"},
    {"backend.lvgl.package.memory", qlr::ModuleCategory::kBackend, "1"},
    {"component.view", qlr::ModuleCategory::kComponent, "1"},
    {"component.text", qlr::ModuleCategory::kComponent, "1"},
    {"component.button", qlr::ModuleCategory::kComponent, "1"},
    {"capability.router", qlr::ModuleCategory::kCapability, "1"},
    {"capability.prompt", qlr::ModuleCategory::kCapability, "1"},
    {"capability.device", qlr::ModuleCategory::kCapability, "1"},
}};

constexpr std::array<qlr::ModuleDescriptor, 20> kSimulatorModules{{
    {"kernel.bridge", qlr::ModuleCategory::kKernel, "1"},
    {"kernel.render", qlr::ModuleCategory::kKernel, "1"},
    {"kernel.event", qlr::ModuleCategory::kKernel, "1"},
    {"kernel.lifecycle", qlr::ModuleCategory::kKernel, "1"},
    {"kernel.runtime-tree", qlr::ModuleCategory::kKernel, "1"},
    {"kernel.transaction", qlr::ModuleCategory::kKernel, "1"},
    {"runtime.js-framework", qlr::ModuleCategory::kRuntime, "1"},
    {"engine.quickjs", qlr::ModuleCategory::kEngine, "1"},
    {"platform.lvgl.host", qlr::ModuleCategory::kPlatform, "1"},
    {"platform.lvgl.trace", qlr::ModuleCategory::kPlatform, "1"},
    {"backend.lvgl.libuv.loop", qlr::ModuleCategory::kBackend, "1"},
    {"backend.lvgl.sdl.display", qlr::ModuleCategory::kBackend, "1"},
    {"backend.lvgl.sdl.input", qlr::ModuleCategory::kBackend, "1"},
    {"backend.lvgl.package.file", qlr::ModuleCategory::kBackend, "1"},
    {"component.view", qlr::ModuleCategory::kComponent, "1"},
    {"component.text", qlr::ModuleCategory::kComponent, "1"},
    {"component.button", qlr::ModuleCategory::kComponent, "1"},
    {"capability.router", qlr::ModuleCategory::kCapability, "1"},
    {"capability.prompt", qlr::ModuleCategory::kCapability, "1"},
    {"capability.device", qlr::ModuleCategory::kCapability, "1"},
}};

template <std::size_t Size>
qlr::BuildInventoryView inventory(
    const std::array<qlr::ModuleDescriptor, Size>& modules) noexcept {
  return {modules, kComponents, kCapabilities, 1};
}

class FixedCompletionPort final : public qlr::PackageCompletionPort {
 public:
  static constexpr std::size_t kCapacity = 8;

  qlr::CompletionPostStatus tryPost(
      qlr::PackageReadDelivery& delivery) noexcept override {
    if (stopping_) {
      return qlr::CompletionPostStatus::kStopping;
    }
    if (busy_) {
      return qlr::CompletionPostStatus::kBusy;
    }
    if (size_ == entries_.size()) {
      return qlr::CompletionPostStatus::kFull;
    }
    entries_[(head_ + size_) % entries_.size()] = std::move(delivery);
    delivery = {};
    ++size_;
    peak_ = peak_ < size_ ? size_ : peak_;
    return qlr::CompletionPostStatus::kAccepted;
  }

  std::size_t drain(std::size_t budget = kCapacity) noexcept {
    std::size_t count = 0;
    while (count < budget && size_ != 0) {
      qlr::PackageReadDelivery delivery = std::move(entries_[head_]);
      entries_[head_] = {};
      head_ = (head_ + 1) % entries_.size();
      --size_;
      if (delivery.completion.callback != nullptr) {
        delivery.completion.callback(delivery.completion.context,
                                     std::move(delivery.result));
      }
      ++count;
    }
    return count;
  }

  void setBusy(bool value) noexcept { busy_ = value; }
  [[nodiscard]] std::size_t depth() const noexcept { return size_; }
  [[nodiscard]] std::size_t peak() const noexcept { return peak_; }

 private:
  std::array<qlr::PackageReadDelivery, kCapacity> entries_{};
  std::size_t head_{0};
  std::size_t size_{0};
  std::size_t peak_{0};
  bool busy_{false};
  bool stopping_{false};
};

struct ReadCapture final {
  std::size_t calls{0};
  bool success{false};
  qcore::RuntimeErrorCode error{qcore::RuntimeErrorCode::kPackageIoError};
  qlr::ImmutableBytes bytes;
};

void captureRead(void* context, qlr::PackageReadResult&& result) noexcept {
  auto& capture = *static_cast<ReadCapture*>(context);
  ++capture.calls;
  capture.success = result.success;
  capture.error = result.error;
  capture.bytes = std::move(result.bytes);
}

bool bytesEqual(const qlr::ImmutableBytes& bytes,
                std::string_view expected) noexcept {
  return bytes.size() == expected.size() &&
         (expected.empty() ||
          std::memcmp(bytes.data(), expected.data(), expected.size()) == 0);
}

class EngineSession final : public qlr::JsRuntimeFactoryPort {
 public:
  qlr::RuntimeCallResult start(qjs::JsEngineProvider& provider,
                               qcore::TraceSink&) noexcept override {
    if (engine_ != nullptr) {
      return qlr::RuntimeCallResult::fail(
          qcore::RuntimeErrorCode::kLifecycleBusy, true);
    }
    qjs::JsEngineConfig config;
    config.expectedEngine = provider.describe();
    config.limits.maxHeapBytes = 8ULL * 1024ULL * 1024ULL;
    config.limits.maxStackBytes = 256ULL * 1024ULL;
    config.limits.maxPendingTasks = 64;
    config.limits.maxMicrotasksPerTurn = 32;
    engine_ = provider.create(config);
    if (engine_ == nullptr) {
      return qlr::RuntimeCallResult::fail(qcore::RuntimeErrorCode::kOutOfMemory);
    }
    auto result = engine_->createContext();
    if (!result.ok()) {
      engine_.reset();
      return qlr::RuntimeCallResult::fail(qcore::RuntimeErrorCode::kJsException);
    }
    context_ = std::move(result).value();
    live_ = 1;
    return qlr::RuntimeCallResult::ok();
  }

  qlr::RuntimeCallResult stop() noexcept override {
    if (engine_ == nullptr) {
      return qlr::RuntimeCallResult::ok();
    }
    const bool ok = engine_->destroyContext(context_).ok();
    engine_.reset();
    live_ = 0;
    return ok ? qlr::RuntimeCallResult::ok()
              : qlr::RuntimeCallResult::fail(
                    qcore::RuntimeErrorCode::kJsException);
  }

  [[nodiscard]] std::size_t liveServices() const noexcept override {
    return live_;
  }

 private:
  std::unique_ptr<qjs::JsEnginePort> engine_;
  qjs::JsContextRef context_;
  std::size_t live_{0};
};

class LightweightEngineSession final : public qlr::JsRuntimeFactoryPort {
 public:
  qlr::RuntimeCallResult start(qjs::JsEngineProvider&,
                               qcore::TraceSink&) noexcept override {
    live_ = 1;
    return qlr::RuntimeCallResult::ok();
  }
  qlr::RuntimeCallResult stop() noexcept override {
    live_ = 0;
    return qlr::RuntimeCallResult::ok();
  }
  [[nodiscard]] std::size_t liveServices() const noexcept override {
    return live_;
  }

 private:
  std::size_t live_{0};
};

class FakeCore final : public qlr::CoreRuntimePort {
 public:
  qcore::RuntimeResult<qcore::RequestId> allocateRequestId() noexcept override {
    return ids_.next();
  }

  qlr::RuntimeCallResult start(
      const qlr::RuntimeLaunchProfileView& profile, qlr::PackageSource& source,
      qlr::JsRuntimeFactoryPort& js_factory,
      qjs::JsEngineProvider& provider, qcore::TraceSink& sink,
      qlr::CoreStartCompletion completion) noexcept override {
    ++start_calls;
    saw_artifact = profile.artifact;
    saw_package_size = source.size();
    js_factory_ = &js_factory;
    const qlr::RuntimeCallResult js_result = js_factory.start(provider, sink);
    const qlr::CoreStartResult result{present_root && js_result.success,
                                      js_result};
    if (completion.callback != nullptr && callbacks_on_worker) {
      std::thread worker([completion, result]() noexcept {
        completion.callback(completion.context, result);
      });
      worker.join();
    } else if (completion.callback != nullptr) {
      completion.callback(completion.context, result);
    }
    return qlr::RuntimeCallResult::ok();
  }

  qlr::RuntimeCallResult control(
      const qlr::RuntimeLifecycleControl& control,
      qlr::CoreLifecycleCompletion completion) noexcept override {
    ++control_calls;
    if (reject_next_control) {
      reject_next_control = false;
      return qlr::RuntimeCallResult::fail(
          qcore::RuntimeErrorCode::kLifecycleBusy, true);
    }
    qlr::RuntimeCallResult result = qlr::RuntimeCallResult::ok();
    if (complete_next_busy) {
      complete_next_busy = false;
      result = qlr::RuntimeCallResult::fail(
          qcore::RuntimeErrorCode::kLifecycleBusy, true);
    }
    if (control.action == qlr::LifecycleAction::kDestroyAppRuntime &&
        js_factory_ != nullptr) {
      const qlr::RuntimeCallResult stop_result = js_factory_->stop();
      if (!stop_result.success) {
        result = stop_result;
      }
    }
    const qlr::RuntimeLifecycleControlResult completion_result{
        control.request_id, control.action, result};
    if (completion.callback != nullptr && callbacks_on_worker) {
      std::thread worker([completion, completion_result]() noexcept {
        completion.callback(completion.context, completion_result);
      });
      worker.join();
    } else if (completion.callback != nullptr) {
      completion.callback(completion.context, completion_result);
    }
    return qlr::RuntimeCallResult::ok();
  }

  bool present_root{true};
  bool reject_next_control{false};
  bool complete_next_busy{false};
  bool callbacks_on_worker{false};
  std::size_t start_calls{0};
  std::size_t control_calls{0};
  std::string_view saw_artifact;
  std::uint64_t saw_package_size{0};

 private:
  qcore::MonotonicIdAllocator<qcore::RequestIdTag> ids_;
  qlr::JsRuntimeFactoryPort* js_factory_{nullptr};
};

std::uint64_t builtinNow(void*) noexcept { return 100; }
std::uint64_t builtinResolution(void*) noexcept { return 1; }
std::size_t builtinService(void*, std::size_t budget) noexcept {
  return budget == 0 ? 0 : 1;
}

struct CompletionCapture final {
  std::size_t calls{0};
  qlr::RuntimeCallResult result{};
};

void captureCompletion(void* context, qlr::RuntimeCallResult result) noexcept {
  auto& capture = *static_cast<CompletionCapture*>(context);
  ++capture.calls;
  capture.result = result;
}

class AlternatingTraceEndpoint final : public qlr::TraceEndpoint {
 public:
  qlr::TraceDispatchStatus tryEmit(
      const qcore::TraceEventView&) noexcept override {
    ++calls;
    return calls % 2 == 0 ? qlr::TraceDispatchStatus::kDropped
                          : qlr::TraceDispatchStatus::kAccepted;
  }
  std::size_t calls{0};
};

bool testCompositionAndRealEngine() {
  qjs::QuickJsEngineProvider provider;
  const qlr::CompositionValidation embedded =
      qlr::CompositionRoot::validateIsolated(
          qlr::ProfileId::kEmbeddedMin, inventory(kEmbeddedModules), provider);
  CHECK(embedded.ok());
  CHECK(embedded.isolated_evidence);
  CHECK(!embedded.product_manifest);
  CHECK(embedded.profile->profile_id == "lvgl-embedded-min");

  const qlr::CompositionValidation simulator =
      qlr::CompositionRoot::validateIsolated(
          qlr::ProfileId::kSimulatorDev, inventory(kSimulatorModules), provider);
  CHECK(simulator.ok());
  CHECK(simulator.profile->limits.owner_task_capacity == 512);

  auto crossed = kEmbeddedModules;
  crossed[10] = {"backend.lvgl.libuv.loop", qlr::ModuleCategory::kBackend,
                 "1"};
  CHECK(qlr::CompositionRoot::validateIsolated(
            qlr::ProfileId::kEmbeddedMin, inventory(crossed), provider)
            .issue == qlr::CompositionIssue::kMissingProfileModule);

  auto duplicate = kEmbeddedModules;
  duplicate[19] = duplicate[7];
  CHECK(qlr::CompositionRoot::validateIsolated(
            qlr::ProfileId::kEmbeddedMin, inventory(duplicate), provider)
            .issue == qlr::CompositionIssue::kDuplicateModule);

  EngineSession engine;
  qcore::NoopTraceSink sink;
  CHECK(engine.start(provider, sink).success);
  CHECK(engine.liveServices() == 1);
  CHECK(engine.stop().success);
  CHECK(engine.liveServices() == 0);
  return true;
}

bool testMemoryPackageSourceAndBackpressure() {
  constexpr std::string_view kData = "0123456789";
  FixedCompletionPort port;
  qlr::ImmutableBytes storage;
  CHECK(qlr::ImmutableBytes::copyOf(
            reinterpret_cast<const std::byte*>(kData.data()), kData.size(),
            storage)
            .success);
  qlr::MemoryPackageSource source(std::move(storage), port);

  ReadCapture range;
  CHECK(source.readAt(2, 4, {&captureRead, &range}) ==
        qlr::PackageReadAdmission::kAccepted);
  CHECK(range.calls == 0);
  CHECK(port.drain() == 1);
  CHECK(range.calls == 1 && range.success && bytesEqual(range.bytes, "2345"));

  ReadCapture zero;
  CHECK(source.readAt(10, 0, {&captureRead, &zero}) ==
        qlr::PackageReadAdmission::kAccepted);
  CHECK(port.drain() == 1);
  CHECK(zero.calls == 1 && zero.success && zero.bytes.size() == 0);

  port.setBusy(true);
  ReadCapture busy;
  CHECK(source.readAt(0, 1, {&captureRead, &busy}) ==
        qlr::PackageReadAdmission::kBusy);
  CHECK(busy.calls == 0);
  port.setBusy(false);

  ReadCapture invalid;
  CHECK(source.readAt(9, 2, {&captureRead, &invalid}) ==
        qlr::PackageReadAdmission::kAccepted);
  CHECK(port.drain() == 1);
  CHECK(invalid.calls == 1 && !invalid.success);
  CHECK(source.close().ok());

  ReadCapture closed;
  CHECK(source.readAt(0, 1, {&captureRead, &closed}) ==
        qlr::PackageReadAdmission::kClosed);
  CHECK(port.drain() == 1);
  CHECK(closed.calls == 1 && !closed.success);
  CHECK(port.peak() == 1);
  return true;
}

bool pumpFile(qlb::LibuvLoopBackend& loop, qlr::PackageSource& source,
              FixedCompletionPort& port, ReadCapture& capture) {
  for (std::size_t turn = 0; turn < 64 && capture.calls == 0; ++turn) {
    CHECK(loop.serviceOneTurn(kOwner, 8).ok());
    (void)source.serviceCompletions(8);
    (void)port.drain();
  }
  return capture.calls == 1;
}

bool testLibuvFileIdentityShortReadAndClose() {
  char path[] = "/tmp/quickapp-lv-s02-XXXXXX";
  const int descriptor = mkstemp(path);
  CHECK(descriptor >= 0);
  constexpr std::string_view kOriginal = "original-package";
  CHECK(write(descriptor, kOriginal.data(), kOriginal.size()) ==
        static_cast<ssize_t>(kOriginal.size()));
  CHECK(close(descriptor) == 0);

  char moved[128]{};
  CHECK(std::snprintf(moved, sizeof(moved), "%s.old", path) > 0);

  qlb::LibuvLoopBackend loop;
  CHECK(loop.initialize(kOwner).ok());
  FixedCompletionPort port;
  std::unique_ptr<qlr::PackageSource> source;
  CHECK(qlb::LibuvFilePackageSource::open(
            *loop.nativeLoop(), path, 2, port, source)
            .success);
  CHECK(rename(path, moved) == 0);
  const int replacement = open(path, O_CREAT | O_WRONLY | O_TRUNC, 0600);
  CHECK(replacement >= 0);
  constexpr std::string_view kReplacement = "replacement";
  CHECK(write(replacement, kReplacement.data(), kReplacement.size()) ==
        static_cast<ssize_t>(kReplacement.size()));
  CHECK(close(replacement) == 0);

  ReadCapture identity;
  CHECK(source->readAt(0, kOriginal.size(), {&captureRead, &identity}) ==
        qlr::PackageReadAdmission::kAccepted);
  CHECK(pumpFile(loop, *source, port, identity));
  CHECK(identity.success && bytesEqual(identity.bytes, kOriginal));

  CHECK(truncate(moved, 2) == 0);
  ReadCapture short_read;
  CHECK(source->readAt(0, kOriginal.size(), {&captureRead, &short_read}) ==
        qlr::PackageReadAdmission::kAccepted);
  CHECK(source->close().error == qlf::LocalError::kBusy);
  CHECK(pumpFile(loop, *source, port, short_read));
  CHECK(!short_read.success);
  CHECK(source->close().ok());
  source.reset();
  for (std::size_t attempt = 0; attempt < 8 && !loop.closed(); ++attempt) {
    (void)loop.close(kOwner);
  }
  CHECK(loop.closed());
  CHECK(unlink(path) == 0);
  CHECK(unlink(moved) == 0);
  return true;
}

bool testSimulatorAndEmbeddedBackends() {
  qlb::LibuvLoopBackend loop;
  CHECK(loop.initialize(kOwner).ok());
  CHECK(loop.notify() == qlf::WakeResult::kNotified);
  CHECK(loop.serviceOneTurn(kOwner, 8).ok());

  qlb::SdlDisplayBackend display(kOwner);
  qlf::DisplayCapabilities display_capabilities{};
  CHECK(display.open(kOwner, {4, 4, qlf::PixelFormat::kRgba8888, 2},
                     display_capabilities)
            .ok());
  qlb::SdlRawInputBackend input(kOwner, display);
  qlf::InputCapabilities input_capabilities{};
  CHECK(input.open(kOwner, {8}, input_capabilities).ok());

  std::array<std::byte, 64> pixels{};
  const qlf::Rect dirty{0, 0, 4, 4};
  CHECK(display.present(kOwner,
                        {pixels.data(), pixels.size(), 16, 4, 4,
                         qlf::PixelFormat::kRgba8888, &dirty, 1, 1})
            .ok());

  SDL_Event event{};
  event.type = SDL_EVENT_MOUSE_BUTTON_DOWN;
  event.button.windowID = display.windowId();
  event.button.button = SDL_BUTTON_LEFT;
  event.button.x = 1;
  event.button.y = 2;
  CHECK(SDL_PushEvent(&event));
  event.type = SDL_EVENT_MOUSE_BUTTON_UP;
  CHECK(SDL_PushEvent(&event));
  std::array<qlf::RawInputSample, 4> samples{};
  const qlf::DrainResult drained = input.drain(kOwner, samples.data(), 4);
  CHECK(drained.ok() && drained.sample_count == 2);
  CHECK(samples[0].action == qlf::RawInputAction::kDown);
  CHECK(samples[1].action == qlf::RawInputAction::kUp);
  CHECK(input.beginStop(kOwner).ok());
  CHECK(input.discardPending(kOwner).ok());
  CHECK(input.close(kOwner).ok());
  CHECK(display.close(kOwner).ok());
  for (std::size_t attempt = 0; attempt < 8 && !loop.closed(); ++attempt) {
    (void)loop.close(kOwner);
  }
  CHECK(loop.closed());

  qlb::BuiltinLoopCallbacks callbacks{};
  callbacks.now_ns = &builtinNow;
  callbacks.resolution_ns = &builtinResolution;
  callbacks.service = &builtinService;
  qlb::BuiltinLoopBackend embedded(callbacks);
  CHECK(embedded.initialize(kOwner).ok());
  CHECK(embedded.notify() == qlf::WakeResult::kUnsupported);
  CHECK(embedded.waitUntil(kOwner, 200) == qlf::WakeResult::kUnsupported);
  CHECK(embedded.serviceOneTurn(kOwner, 8).ok());
  CHECK(embedded.servicedCallbacks() == 1);
  CHECK(embedded.close(kOwner).ok());
  return true;
}

bool runHostCycle(qjs::QuickJsEngineProvider& provider,
                  qlr::JsRuntimeFactoryPort& engine_session,
                  const qlr::CompositionValidation& composition,
                  bool verify_controls, bool present_root = true) {
  qlb::BuiltinLoopCallbacks callbacks{};
  callbacks.now_ns = &builtinNow;
  callbacks.resolution_ns = &builtinResolution;
  qlb::BuiltinLoopBackend loop(callbacks);
  std::array<qlf::OwnerTask, 64> task_storage{};
  qlf::OwnerTaskQueue tasks(task_storage.data(), task_storage.size(), 16,
                            &loop);
  qfake::FakeDisplay display(
      kOwner, {2, 2, qlf::PixelFormat::kRgba8888, 1});
  std::array<qlf::RawInputSample, 16> input_storage{};
  qfake::FakeInput input(kOwner, input_storage.data(), input_storage.size());
  qlf::BackendLifecycleCoordinator lifecycle(tasks, loop, display, input);
  FixedCompletionPort completion_port;
  const std::array<std::byte, 4> package{std::byte{1}, std::byte{2},
                                         std::byte{3}, std::byte{4}};
  qlr::MemoryPackageSourceFactory package_factory(
      "memory://case-001", package.data(), package.size(), completion_port);
  FakeCore core;
  core.callbacks_on_worker = verify_controls;
  core.present_root = present_root;
  qcore::NoopTraceSink trace;
  qlr::RuntimeHost host({composition, kOwner, tasks, lifecycle, loop,
                         package_factory, core, engine_session, provider,
                         trace});
  CompletionCapture started;
  const qlr::RuntimeLaunchProfileView launch{
      "memory://case-001", "/", true, 2, 2, true, "", true,
      qlr::LaunchTarget::kLvgl, false};
  CHECK(host.start(launch, {&captureCompletion, &started}).success);
  CHECK(host.state() == qlr::HostState::kStarting);
  CHECK(host.pumpOnce().error == qlf::LocalError::kNone);
  if (!present_root) {
    CHECK(host.state() == qlr::HostState::kDestroyed);
    CHECK(started.calls == 1 && !started.result.success);
    CHECK(started.result.error ==
          qcore::RuntimeErrorCode::kSurfacePresentationFailed);
    CHECK(host.resourcesReleased());
    CHECK(engine_session.liveServices() == 0);
    CHECK(tasks.depth() == 0);
    CHECK(input.depth() == 0);
    return true;
  }
  CHECK(host.state() == qlr::HostState::kRunning);
  CHECK(started.calls == 1 && started.result.success);
  CHECK(core.start_calls == 1 && core.saw_artifact == launch.artifact);
  CHECK(core.saw_package_size == package.size());

  if (verify_controls) {
    CompletionCapture background;
    core.complete_next_busy = true;
    CHECK(host.control(qlr::LifecycleAction::kEnterBackground,
                       {&captureCompletion, &background})
              .success);
    CHECK(host.pumpOnce().error == qlf::LocalError::kNone);
    CHECK(background.calls == 1 && !background.result.success &&
          background.result.error == qcore::RuntimeErrorCode::kLifecycleBusy);

    CompletionCapture resume;
    CHECK(host.postHostSignal(qlr::RawHostSignal::kResume,
                              {&captureCompletion, &resume})
              .status == qlf::PostStatus::kAccepted);
    CHECK(host.pumpOnce().error == qlf::LocalError::kNone);
    CHECK(host.pumpOnce().error == qlf::LocalError::kNone);
    CHECK(resume.calls == 1 && resume.result.success);
    const std::size_t controls = core.control_calls;
    CHECK(host.postHostSignal(qlr::RawHostSignal::kResume).status ==
          qlf::PostStatus::kAccepted);
    CHECK(host.pumpOnce().error == qlf::LocalError::kNone);
    CHECK(core.control_calls == controls);
    CHECK(host.duplicateSignalCount() == 1);

    for (std::size_t index = 0; index < 64; ++index) {
      CHECK(host.postHostSignal(qlr::RawHostSignal::kResume).status ==
            qlf::PostStatus::kAccepted);
    }
    CHECK(host.postHostSignal(qlr::RawHostSignal::kResume).status ==
          qlf::PostStatus::kFull);
    CHECK(host.queueOverflowCount() == 1);
    for (std::size_t turn = 0; turn < 4; ++turn) {
      const qlr::HostPumpResult report = host.pumpOnce();
      CHECK(report.error == qlf::LocalError::kNone);
      CHECK(report.tasks_executed == 16);
    }
    CHECK(tasks.depth() == 0);
  }

  CompletionCapture destroyed;
  CHECK(host.destroy({&captureCompletion, &destroyed}).success);
  for (std::size_t turn = 0;
       turn < 8 && host.state() != qlr::HostState::kDestroyed; ++turn) {
    CHECK(host.pumpOnce().error == qlf::LocalError::kNone);
  }
  CHECK(host.state() == qlr::HostState::kDestroyed);
  CHECK(destroyed.calls == 1 && destroyed.result.success);
  CHECK(host.resourcesReleased());
  CHECK(engine_session.liveServices() == 0);
  CHECK(tasks.depth() == 0);
  CHECK(input.depth() == 0);
  CHECK(completion_port.depth() == 0);
  return true;
}

bool testRuntimeHostAndTenThousandCycles() {
  qjs::QuickJsEngineProvider provider;
  const qlr::CompositionValidation composition =
      qlr::CompositionRoot::validateIsolated(
          qlr::ProfileId::kEmbeddedMin, inventory(kEmbeddedModules), provider);
  CHECK(composition.ok());

  EngineSession real_engine;
  CHECK(runHostCycle(provider, real_engine, composition, true));
  EngineSession failed_engine;
  CHECK(runHostCycle(provider, failed_engine, composition, false, false));
  for (std::size_t cycle = 0; cycle < 10'000; ++cycle) {
    LightweightEngineSession lightweight;
    CHECK(runHostCycle(provider, lightweight, composition, false));
  }
  return true;
}

bool testTraceAdapterDoesNotAffectRuntime() {
  AlternatingTraceEndpoint endpoint;
  qlr::LvglTraceSinkAdapter sink(endpoint);
  auto timestamp = qcore::WireUInt::from(1);
  auto sequence = qcore::WireUInt::from(1);
  CHECK(timestamp.has_value() && sequence.has_value());
  qcore::TraceEventView event{1,
                              "observationMarker",
                              "run",
                              qcore::TraceProducer::kPlatform,
                              qcore::MarkerName::kPackageOpenStarted,
                              timestamp.value(),
                              "monotonic",
                              sequence.value(),
                              {}};
  sink.emit(event);
  sink.emit(event);
  CHECK(endpoint.calls == 2);
  CHECK(sink.acceptedCount() == 1);
  CHECK(sink.droppedCount() == 1);
  return true;
}

}  // namespace

int main() {
  struct TestCase final {
    const char* name;
    bool (*run)();
  };
  constexpr std::array<TestCase, 7> tests{{
      {"composition-and-real-engine", &testCompositionAndRealEngine},
      {"memory-package-and-backpressure",
       &testMemoryPackageSourceAndBackpressure},
      {"libuv-file-identity-short-read-close",
       &testLibuvFileIdentityShortReadAndClose},
      {"simulator-and-embedded-backends", &testSimulatorAndEmbeddedBackends},
      {"runtime-host-and-10000-cycles", &testRuntimeHostAndTenThousandCycles},
      {"trace-adapter", &testTraceAdapterDoesNotAffectRuntime},
      {"profile-limits", []() {
         return qlr::profileDefinition(qlr::ProfileId::kEmbeddedMin)
                        .limits.admission_retry_attempts_per_source_per_turn ==
                    1 &&
                qlr::profileDefinition(qlr::ProfileId::kSimulatorDev)
                        .limits.max_in_flight_package_reads == 16;
       }},
  }};
  for (const TestCase& test : tests) {
    if (!test.run()) {
      std::fprintf(stderr, "FAILED: %s\n", test.name);
      return 1;
    }
    std::printf("PASS: %s\n", test.name);
  }
  std::printf("LV-S02: %zu contract groups passed\n", tests.size());
  return 0;
}
