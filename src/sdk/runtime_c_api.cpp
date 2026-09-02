#include "quickapp/lvgl/sdk/runtime.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstring>
#include <limits>
#include <new>
#include <string_view>
#include <utility>
#include <vector>

#include "quickapp/core/package/package_loader.h"

namespace {

constexpr std::uint64_t kDefaultMaxRpkBytes = 32ULL * 1024ULL * 1024ULL;
constexpr std::uint32_t kDefaultMaxSurfaces = 1;
constexpr std::uint32_t kSourceHeaderSize = sizeof(qak_rpk_source_t);
constexpr std::uint32_t kSurfaceHeaderSize = sizeof(qak_surface_t);
constexpr std::uint32_t kInputHeaderSize = sizeof(qak_input_t);

qak_result_t result(qak_status_t status, bool retryable = false) noexcept {
  return {status, retryable ? UINT32_C(1) : UINT32_C(0)};
}

bool hasSize(std::uint32_t actual, std::uint32_t expected) noexcept {
  return actual == 0 || actual >= expected;
}

bool isZip(const qak_bytes_t& bytes) noexcept {
  return bytes.data != nullptr && bytes.size >= 4 &&
         bytes.data[0] == static_cast<std::uint8_t>('P') &&
         bytes.data[1] == static_cast<std::uint8_t>('K') &&
         ((bytes.data[2] == 3 && bytes.data[3] == 4) ||
          (bytes.data[2] == 5 && bytes.data[3] == 6) ||
          (bytes.data[2] == 7 && bytes.data[3] == 8));
}

bool fitsSize(qak_bytes_t bytes) noexcept {
  return bytes.size <= static_cast<std::uint64_t>(
                           std::numeric_limits<std::size_t>::max());
}

bool validPath(const qak_bytes_t& path) noexcept {
  if (path.data == nullptr || path.size == 0 || path.size > 512 ||
      path.data[0] == static_cast<std::uint8_t>('/') ||
      path.data[0] == static_cast<std::uint8_t>('\\')) {
    return false;
  }
  for (size_t index = 0; index < path.size; ++index) {
    if (path.data[index] == 0 ||
        path.data[index] == static_cast<std::uint8_t>('\\'))
      return false;
  }
  for (size_t begin = 0; begin < path.size;) {
    size_t end = begin;
    while (end < path.size && path.data[end] != static_cast<std::uint8_t>('/')) ++end;
    if (end == begin ||
        (end - begin == 1 && path.data[begin] == '.')) return false;
    if (end - begin == 2 && path.data[begin] == '.' &&
        path.data[begin + 1] == '.') return false;
    begin = end == path.size ? path.size : end + 1;
  }
  return true;
}

bool allZero(const std::uint8_t* bytes, size_t size) noexcept {
  return std::all_of(bytes, bytes + size, [](std::uint8_t value) {
    return value == 0;
  });
}

bool matchesExpectedDigest(const std::vector<std::uint8_t>& bytes,
                           const qak_rpk_source_t& source) noexcept {
  if (source.has_expected_sha256 == 0) return true;
  if (source.has_expected_sha256 != 1 ||
      allZero(source.expected_sha256, sizeof(source.expected_sha256))) {
    return false;
  }
  const std::string hex = quickapp::core::package::sha256_hex(bytes);
  if (hex.size() != 64) return false;
  for (size_t index = 0; index < 32; ++index) {
    const auto digit = [](char value) -> int {
      if (value >= '0' && value <= '9') return value - '0';
      if (value >= 'a' && value <= 'f') return value - 'a' + 10;
      if (value >= 'A' && value <= 'F') return value - 'A' + 10;
      return -1;
    };
    const int high = digit(hex[index * 2]);
    const int low = digit(hex[index * 2 + 1]);
    if (high < 0 || low < 0 ||
        source.expected_sha256[index] !=
            static_cast<std::uint8_t>((high << 4) | low)) {
      return false;
    }
  }
  return true;
}

}  // namespace

struct qak_runtime final {
  qak_runtime_adapter_t adapter{};
  std::uint64_t max_rpk_bytes{kDefaultMaxRpkBytes};
  std::uint32_t max_surfaces{kDefaultMaxSurfaces};
  std::vector<std::uint8_t> source_copy;
  qak_rpk_source_kind_t source_kind{QAK_RPK_SOURCE_MEMORY};
  bool loaded{false};
  bool attached{false};
  bool destroyed{false};
};

namespace {

qak_result_t validateRuntime(qak_runtime_t* runtime) noexcept {
  if (runtime == nullptr) return result(QAK_STATUS_INVALID_ARGUMENT);
  if (runtime->destroyed) return result(QAK_STATUS_DESTROYED);
  return result(QAK_STATUS_OK);
}

}  // namespace

extern "C" qak_result_t qak_runtime_create(
    const qak_runtime_config_t* config, qak_runtime_t** out_runtime) {
  if (out_runtime == nullptr) return result(QAK_STATUS_INVALID_ARGUMENT);
  *out_runtime = nullptr;
  if (config == nullptr || !hasSize(config->struct_size, sizeof(*config)) ||
      config->abi_version != QAK_RUNTIME_ABI_VERSION ||
      config->max_surfaces > 1 || config->adapter == nullptr ||
      !hasSize(config->adapter->struct_size, sizeof(*config->adapter)) ||
      config->adapter->load_rpk == nullptr ||
      config->adapter->attach_surface == nullptr ||
      config->adapter->dispatch_input == nullptr ||
      config->adapter->update_lifecycle == nullptr) {
    return result(QAK_STATUS_INVALID_ARGUMENT);
  }
  const std::uint64_t max_rpk_bytes =
      config->max_rpk_bytes == 0 ? kDefaultMaxRpkBytes : config->max_rpk_bytes;
  if (max_rpk_bytes == 0)
    return result(QAK_STATUS_INVALID_ARGUMENT);
  auto* runtime = new (std::nothrow) qak_runtime_t;
  if (runtime == nullptr) return result(QAK_STATUS_INTERNAL);
  runtime->adapter = *config->adapter;
  runtime->max_rpk_bytes = max_rpk_bytes;
  runtime->max_surfaces =
      config->max_surfaces == 0 ? kDefaultMaxSurfaces : config->max_surfaces;
  *out_runtime = runtime;
  return result(QAK_STATUS_OK);
}

extern "C" qak_result_t qak_runtime_load_rpk(
    qak_runtime_t* runtime, const qak_rpk_source_t* source) {
  const qak_result_t valid = validateRuntime(runtime);
  if (valid.status != QAK_STATUS_OK) return valid;
  if (source == nullptr || !hasSize(source->struct_size, kSourceHeaderSize))
    return result(QAK_STATUS_INVALID_ARGUMENT);
  const bool too_large = source->value.size > runtime->max_rpk_bytes ||
                         !fitsSize(source->value);
  if (too_large) return result(QAK_STATUS_PACKAGE_TOO_LARGE);
  if ((source->kind != QAK_RPK_SOURCE_MEMORY &&
       source->kind != QAK_RPK_SOURCE_PATH) || source->value.data == nullptr ||
      source->value.size == 0 || runtime->loaded) {
    return result(QAK_STATUS_INVALID_ARGUMENT);
  }
  if (source->kind == QAK_RPK_SOURCE_MEMORY) {
    if (!isZip(source->value)) return result(QAK_STATUS_PACKAGE_INVALID);
    try {
      const auto size = static_cast<std::size_t>(source->value.size);
      runtime->source_copy.assign(source->value.data,
                                  source->value.data + size);
    } catch (...) {
      return result(QAK_STATUS_INTERNAL);
    }
    if (!matchesExpectedDigest(runtime->source_copy, *source)) {
      runtime->source_copy.clear();
      return result(QAK_STATUS_PACKAGE_INVALID);
    }
  } else if (!validPath(source->value)) {
    return result(QAK_STATUS_PACKAGE_INVALID);
  } else {
    try {
      const auto size = static_cast<std::size_t>(source->value.size);
      runtime->source_copy.assign(source->value.data,
                                  source->value.data + size);
    } catch (...) {
      return result(QAK_STATUS_INTERNAL);
    }
  }
  qak_rpk_source_t normalized = *source;
  normalized.value = {runtime->source_copy.data(),
                      static_cast<std::uint64_t>(runtime->source_copy.size())};
  const qak_result_t callback = runtime->adapter.load_rpk(
      runtime->adapter.context, &normalized);
  if (callback.status == QAK_STATUS_OK || callback.status == QAK_STATUS_ACCEPTED) {
    runtime->source_kind = source->kind;
    runtime->loaded = true;
  } else {
    runtime->source_copy.clear();
  }
  return callback;
}

extern "C" qak_result_t qak_runtime_attach_surface(
    qak_runtime_t* runtime, const qak_surface_t* surface) {
  const qak_result_t valid = validateRuntime(runtime);
  if (valid.status != QAK_STATUS_OK) return valid;
  if (surface == nullptr || !hasSize(surface->struct_size, kSurfaceHeaderSize) ||
      surface->opaque_surface == 0 || surface->width_px == 0 ||
      surface->height_px == 0 || !runtime->loaded || runtime->attached) {
    return result(QAK_STATUS_INVALID_ARGUMENT);
  }
  const qak_result_t callback = runtime->adapter.attach_surface(
      runtime->adapter.context, surface);
  if (callback.status == QAK_STATUS_OK || callback.status == QAK_STATUS_ACCEPTED)
    runtime->attached = true;
  return callback;
}

extern "C" qak_result_t qak_runtime_dispatch_input(
    qak_runtime_t* runtime, const qak_input_t* input) {
  const qak_result_t valid = validateRuntime(runtime);
  if (valid.status != QAK_STATUS_OK) return valid;
  if (input == nullptr || !hasSize(input->struct_size, kInputHeaderSize) ||
      !runtime->loaded || !runtime->attached ||
      (input->kind == QAK_INPUT_TEXT &&
       (input->text.data == nullptr || input->text.size == 0))) {
    return result(QAK_STATUS_INVALID_ARGUMENT);
  }
  return runtime->adapter.dispatch_input(runtime->adapter.context, input);
}

extern "C" qak_result_t qak_runtime_update_lifecycle(
    qak_runtime_t* runtime, qak_lifecycle_signal_t signal) {
  const qak_result_t valid = validateRuntime(runtime);
  if (valid.status != QAK_STATUS_OK) return valid;
  if (!runtime->loaded || !runtime->attached ||
      (signal < QAK_LIFECYCLE_SURFACE_SHOW ||
       signal > QAK_LIFECYCLE_APP_BACKGROUND)) {
    return result(QAK_STATUS_INVALID_ARGUMENT);
  }
  return runtime->adapter.update_lifecycle(runtime->adapter.context, signal);
}

extern "C" qak_result_t qak_runtime_pump(qak_runtime_t* runtime) {
  const qak_result_t valid = validateRuntime(runtime);
  if (valid.status != QAK_STATUS_OK) return valid;
  return runtime->adapter.pump == nullptr
             ? result(QAK_STATUS_OK)
             : runtime->adapter.pump(runtime->adapter.context);
}

extern "C" qak_result_t qak_runtime_destroy(qak_runtime_t* runtime) {
  if (runtime == nullptr) return result(QAK_STATUS_INVALID_ARGUMENT);
  if (runtime->destroyed) return result(QAK_STATUS_OK);
  qak_result_t callback = result(QAK_STATUS_OK);
  if (runtime->adapter.destroy != nullptr)
    callback = runtime->adapter.destroy(runtime->adapter.context);
  runtime->source_copy.clear();
  runtime->loaded = false;
  runtime->attached = false;
  runtime->destroyed = true;
  delete runtime;
  return callback;
}
