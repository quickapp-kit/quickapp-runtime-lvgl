#pragma once

#include <map>
#include <set>
#include <string>

#include "quickapp/core/feature/module_registry.h"

namespace quickapp::lvgl::feature {

class LvglFeatureProvider final : public core::feature::Provider {
 public:
  explicit LvglFeatureProvider(void* parent_object) noexcept
      : parent_object_(parent_object) {}
  ~LvglFeatureProvider() override;

  [[nodiscard]] core::feature::Result invoke(
      const core::feature::Request& request) noexcept override;
  [[nodiscard]] bool cancel(const core::RequestId& request_id,
                            const core::SurfaceId& surface_id) noexcept override;
  void teardown(const core::SurfaceId& surface_id) noexcept override;
  void clear_resources() noexcept;
  [[nodiscard]] std::size_t live_resource_count() const noexcept;

  // Deterministic platform fixtures. They never access the network or host
  // filesystem and are intentionally scoped to this platform provider.
  void setFetchFailure(std::string url, std::string code,
                       std::string message) noexcept;
  void setFetchUnsupported(std::string url) noexcept;
  void markPendingFetch(const core::RequestId& request_id,
                        const core::SurfaceId& surface_id) noexcept;
  void putPrivateFile(const core::SurfaceId& surface_id, std::string path,
                      std::string data) noexcept;

 private:
  [[nodiscard]] core::feature::Result failed(
      const core::feature::Request& request, std::string code,
      std::string message) const noexcept;
  [[nodiscard]] core::feature::Result unsupported(
      const core::feature::Request& request) const noexcept;
  void destroyToast(const std::string& surface_id) noexcept;
  [[nodiscard]] core::feature::Result invokePrompt(
      const core::feature::Request& request) noexcept;
  [[nodiscard]] core::feature::Result invokeFetch(
      const core::feature::Request& request) noexcept;
  [[nodiscard]] core::feature::Result invokeFile(
      const core::feature::Request& request) noexcept;
  [[nodiscard]] static std::string fileKey(const std::string& surface_id,
                                            const std::string& path);

  void* parent_object_{nullptr};
  std::map<std::string, void*, std::less<>> toast_objects_;
  std::map<std::string, std::string, std::less<>> titles_;
  std::map<std::string, std::string, std::less<>> meta_descriptions_;
  std::map<std::string, std::pair<std::string, std::string>, std::less<>>
      fetch_failures_;
  std::set<std::string, std::less<>> fetch_unsupported_;
  std::set<std::string, std::less<>> pending_fetches_;
  std::map<std::string, std::string, std::less<>> private_files_;
};

}  // namespace quickapp::lvgl::feature
