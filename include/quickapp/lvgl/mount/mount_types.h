#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

#include "quickapp/core/foundation/error.h"
#include "quickapp/core/foundation/id.h"
#include "quickapp/core/package/page_ir.h"

namespace quickapp::lvgl::mount {

// Maximum operations in one platform batch. This is not a Runtime Node limit.
inline constexpr std::size_t kMaxMountOperations = 256;
inline constexpr std::size_t kMaxPropertyText = 512;

struct BoundedText final {
  std::array<char, kMaxPropertyText> data{};
  std::size_t size{0};
  bool truncated{false};

  [[nodiscard]] static BoundedText from(std::string_view value) noexcept;
  [[nodiscard]] std::string_view view() const noexcept {
    return std::string_view(data.data(), size);
  }
};

using HostProperty = std::variant<bool, double, std::int32_t, BoundedText>;

struct HostRect final {
  std::int32_t x{0};
  std::int32_t y{0};
  std::int32_t width{0};
  std::int32_t height{0};
};

struct CreateHost final {
  core::NodeId node_id;
  core::package::HostComponentType type;
};

struct SetHostProp final {
  core::NodeId node_id;
  BoundedText property;
  HostProperty value;
};

struct SetHostLayout final {
  core::NodeId node_id;
  HostRect rect;
};

struct InsertHostChild final {
  core::NodeId node_id;
  core::NodeId parent_node_id;
  std::size_t index{0};
};

struct MoveHost final {
  core::NodeId node_id;
  core::NodeId new_parent_node_id;
  std::size_t index{0};
};

struct RemoveHost final {
  core::NodeId node_id;
};

using MountOperation = std::variant<std::monostate, CreateHost, SetHostProp, SetHostLayout,
                                     InsertHostChild, MoveHost, RemoveHost>;

enum class MountMode : std::uint8_t { kFull, kIncremental };

struct MountTransaction final {
  MountTransaction(core::SurfaceId surface, std::uint64_t rev,
                   core::MountAttemptId attempt, BoundedText source,
                   MountMode mount_mode) noexcept
      : surface_id(std::move(surface)),
        revision(rev),
        mount_attempt_id(std::move(attempt)),
        source_id(source),
        mode(mount_mode),
        operations(kMaxMountOperations) {}
  core::SurfaceId surface_id;
  std::uint64_t revision{0};
  core::MountAttemptId mount_attempt_id;
  BoundedText source_id;
  MountMode mode{MountMode::kIncremental};
  std::vector<MountOperation> operations;
  std::size_t operation_count{0};
  // A logical Core Mount may be delivered as ordered bounded batches.
  std::uint32_t batch_index{0};
  std::uint32_t batch_count{1};
  bool is_final{true};
};

enum class MountResultStatus : std::uint8_t { kMounted, kFailed };

struct MountResult final {
  core::SurfaceId surface_id;
  std::uint64_t revision{0};
  core::MountAttemptId mount_attempt_id;
  BoundedText source_id;
  MountResultStatus status{MountResultStatus::kFailed};
  std::optional<core::RuntimeError> error;
  std::size_t live_objects{0};
  std::uint32_t batch_index{0};
  std::uint32_t batch_count{1};
  bool is_final{true};
};

class MountResultSink {
 public:
  virtual ~MountResultSink() = default;
  virtual void complete(MountResult result) noexcept = 0;
};

}  // namespace quickapp::lvgl::mount
