#include <array>
#include <atomic>
#include <cmath>
#include <cstdio>
#include <limits>
#include <string>
#include <string_view>
#include <vector>

#include "quickapp/core/foundation/port.h"
#include "quickapp/lvgl/foundation/fakes.h"
#include "quickapp/lvgl/measure/font_measure.h"

namespace qlf = quickapp::lvgl::foundation;
namespace qlm = quickapp::lvgl::measure;
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

class GenerationPort final
    : public qcore::CoreIngressPort<qlm::PlatformFontGenerationChanged> {
 public:
  qcore::EnqueueResult post(qlm::PlatformFontGenerationChanged&& value) noexcept override {
    if (closed_ || busy_ || values_.size() == capacity_) {
      return qcore::EnqueueResult::failure(qcore::RuntimeError::simple(
          qcore::RuntimeErrorCode::kQueueOverflow, "generation queue full"));
    }
    values_.push_back(value);
    return qcore::EnqueueResult::success(qcore::Accepted{});
  }
  void close() noexcept override { closed_ = true; }
  void setBusy(bool value) noexcept { busy_ = value; }
  void setCapacity(std::size_t value) noexcept { capacity_ = value; }
  [[nodiscard]] std::size_t size() const noexcept { return values_.size(); }
  [[nodiscard]] std::uint64_t take() noexcept {
    const auto value = values_.front().platform_font_generation;
    values_.erase(values_.begin());
    return value;
  }

 private:
  std::vector<qlm::PlatformFontGenerationChanged> values_;
  std::size_t capacity_{8};
  bool busy_{false};
  bool closed_{false};
};

qlm::MeasureRequest request(std::string_view text,
                            std::string_view generation = "") {
  return {"req:measure", "srf:one", "node:text", 1,
          generation.empty() ? 1U : 2U, qlm::MeasureRole::kText, text,
          "system-default", 16, 400,
          {qlm::ConstraintKind::kUnconstrained, 0},
          {qlm::ConstraintKind::kUnconstrained, 0}};
}

bool closePublisher(qlm::FontSnapshotPublisher& publisher,
                    GenerationPort& notifications) {
  publisher.closeAdmission();
  notifications.setBusy(false);
  const auto result = publisher.tryFinalizeClose(kOwner);
  CHECK(result.ok());
  CHECK(publisher.pendingReaders() == 0);
  CHECK(publisher.closed());
  return true;
}

bool testMeasureAndFailureContract() {
  GenerationPort notifications;
  qlm::FontSnapshotPublisher publisher(notifications);
  CHECK(publisher.initialize(kOwner, qlm::FontMetricsSnapshot::makeV1(1)).ok());
  qlm::FontMeasureAdapter measure(publisher);

  auto plain = measure.measure(request("abc"));
  CHECK(plain.measured);
  CHECK(std::abs(plain.width - 28.78125) < 0.02);
  CHECK(std::abs(plain.height - 23.171875) < 0.02);

  auto cjk = measure.measure(request("中"));
  CHECK(cjk.measured && cjk.width >= 15.9 && cjk.width <= 16.1);
  auto button = request("abc");
  button.role = qlm::MeasureRole::kButtonLabel;
  const auto button_result = measure.measure(button);
  CHECK(button_result.measured && button_result.width == plain.width &&
        button_result.height == plain.height);
  const auto tab = measure.measure(request("a\tb"));
  CHECK(tab.measured && std::abs(tab.width - 38.375) < 0.02);
  auto invalid = request(std::string_view("\xE4\xB8", 2));
  CHECK(!measure.measure(invalid).measured);
  auto unsupported = request("abc");
  unsupported.font_token = "missing";
  CHECK(!measure.measure(unsupported).measured);

  auto constrained = request("abc abc");
  constrained.width_constraint = {qlm::ConstraintKind::kAtMost, 32};
  CHECK(measure.measure(constrained).measured);
  constrained.width_constraint = {qlm::ConstraintKind::kExactly, 80};
  constrained.height_constraint = {qlm::ConstraintKind::kExactly, 44};
  const auto exact = measure.measure(constrained);
  CHECK(exact.measured && exact.width == 80 && exact.height == 44);

  auto stale = request("abc", "stale");
  CHECK(!measure.measure(stale).measured);
  auto invalid_number = request("abc");
  invalid_number.font_size = std::numeric_limits<double>::quiet_NaN();
  CHECK(!measure.measure(invalid_number).measured);

  const auto simulator_limits = qlm::simulatorFontMetricsLimits();
  const auto embedded_limits = qlm::embeddedFontMetricsLimits();
  CHECK(simulator_limits.max_families == 16 && embedded_limits.max_families == 4);
  const auto simulator_snapshot =
      qlm::FontMetricsSnapshot::makeV1(1, simulator_limits.max_families);
  const auto embedded_snapshot =
      qlm::FontMetricsSnapshot::makeV1(1, embedded_limits.max_families);
  CHECK(simulator_snapshot.maxFamilies() == 16 &&
        embedded_snapshot.maxFamilies() == 4);
  const auto* simulator_family =
      simulator_snapshot.findFamily("system-default", 400);
  const auto* embedded_family =
      embedded_snapshot.findFamily("system-default", 400);
  CHECK(simulator_family != nullptr && embedded_family != nullptr &&
        simulator_family->asset_digest.view() ==
            embedded_family->asset_digest.view());

  qlm::FontSnapshotPublisher embedded_publisher(notifications);
  CHECK(embedded_publisher
            .initialize(kOwner, embedded_snapshot)
            .ok());
  CHECK(embedded_publisher.publish(kOwner, simulator_snapshot).error ==
        qlf::LocalError::kInvalidState);
  embedded_publisher.closeAdmission();
  CHECK(embedded_publisher.tryFinalizeClose(kOwner).ok());

  qlm::FontMeasureAdapter embedded_measure(publisher,
                                            qlm::embeddedMeasureLimits());
  std::string embedded_ok(2'048, 'a');
  CHECK(embedded_measure.measure(request(embedded_ok)).measured);
  std::string embedded_too_many(2'049, 'a');
  CHECK(!embedded_measure.measure(request(embedded_too_many)).measured);
  CHECK(measure.measure(request("" )).measured);
  CHECK(closePublisher(publisher, notifications));
  return true;
}

bool testGenerationAndSnapshotLifetime() {
  GenerationPort notifications;
  notifications.setCapacity(1);
  qlm::FontSnapshotPublisher publisher(notifications);
  CHECK(publisher.initialize(kOwner, qlm::FontMetricsSnapshot::makeV1(1)).ok());
  qlm::FontSnapshotPublisher::ReadGuard reader;
  CHECK(publisher.acquire(reader).ok());
  CHECK(reader.get()->generation() == 1);
  CHECK(publisher.publish(kOwner, qlm::FontMetricsSnapshot::makeV1(99)).ok());
  CHECK(publisher.generation() == 2);
  CHECK(notifications.size() == 1 && notifications.take() == 2);
  CHECK(reader.get()->generation() == 1);
  reader = {};

  CHECK(publisher.publish(kOwner, qlm::FontMetricsSnapshot::makeV1(99)).ok());
  CHECK(publisher.notificationPending() == false);
  CHECK(notifications.size() == 1 && notifications.take() == 3);
  CHECK(publisher.publish(kOwner, qlm::FontMetricsSnapshot::makeV1(99)).ok());
  CHECK(notifications.size() == 1 && notifications.take() == 4);
  CHECK(publisher.initialize(kOwner, qlm::FontMetricsSnapshot::makeV1(1)).error ==
        qlf::LocalError::kInvalidState);
  CHECK(closePublisher(publisher, notifications));
  return true;
}

bool testNotificationBackpressureAndCloseBusy() {
  GenerationPort notifications;
  notifications.setCapacity(1);
  qlm::FontSnapshotPublisher publisher(notifications);
  CHECK(publisher.initialize(kOwner, qlm::FontMetricsSnapshot::makeV1(1)).ok());
  notifications.setBusy(true);
  CHECK(publisher.publish(kOwner, qlm::FontMetricsSnapshot::makeV1(2)).ok());
  CHECK(publisher.notificationPending());
  CHECK(publisher.publish(kOwner, qlm::FontMetricsSnapshot::makeV1(3)).error ==
        qlf::LocalError::kBusy);
  notifications.setBusy(false);
  CHECK(publisher.serviceNotifications(kOwner).ok());
  CHECK(!publisher.notificationPending());
  CHECK(notifications.size() == 1);

  qlm::FontSnapshotPublisher::ReadGuard reader;
  CHECK(publisher.acquire(reader).ok());
  publisher.closeAdmission();
  CHECK(publisher.tryFinalizeClose(kOwner).error == qlf::LocalError::kBusy);
  reader = {};
  (void)notifications.take();
  CHECK(publisher.tryFinalizeClose(kOwner).ok());
  return true;
}

bool testMeasureAndGenerationStress() {
  GenerationPort notifications;
  qlm::FontSnapshotPublisher publisher(notifications);
  CHECK(publisher.initialize(kOwner, qlm::FontMetricsSnapshot::makeV1(1)).ok());
  qlm::FontMeasureAdapter measure(publisher);
  for (std::size_t index = 0; index < 100'000; ++index) {
    CHECK(measure.measure(request("abc中")).measured);
  }
  for (std::size_t index = 0; index < 10'000; ++index) {
    CHECK(publisher.publish(kOwner, qlm::FontMetricsSnapshot::makeV1(1)).ok());
    CHECK(notifications.size() == 1);
    CHECK(notifications.take() == index + 2);
  }
  CHECK(publisher.pendingReaders() == 0);
  CHECK(!publisher.notificationPending());
  CHECK(closePublisher(publisher, notifications));
  return true;
}

}  // namespace

int main() {
  const bool ok = testMeasureAndFailureContract() &&
                  testGenerationAndSnapshotLifetime() &&
                  testNotificationBackpressureAndCloseBusy() &&
                  testMeasureAndGenerationStress();
  std::puts(ok ? "LV-S06 contract tests: PASS" : "LV-S06 contract tests: FAIL");
  return ok ? 0 : 1;
}
