#include "quickapp/lvgl/feature/lvgl_feature_provider.h"

#include <lvgl.h>

#include <cstddef>
#include <cstdio>
#include <algorithm>
#include <string_view>
#include <utility>

#include "quickapp/lvgl/font/system_default_font_asset.h"

namespace quickapp::lvgl::feature {
namespace {

using core::feature::Method;
using core::feature::ModuleId;
using core::feature::Result;
using core::feature::Status;

constexpr std::int32_t kPromptFontSize = 16;
constexpr std::size_t kPromptFontCacheGlyphCount = 64;

bool isPageMethod(const core::feature::Request& request) noexcept {
  return request.module == ModuleId::kPageHost &&
         (request.method == Method::kSetTitleBar ||
          request.method == Method::kSetMeta);
}

}  // namespace

LvglFeatureProvider::~LvglFeatureProvider() {
  for (const auto& [surface_id, resource] : toast_objects_) {
    static_cast<void>(surface_id);
    if (resource.object != nullptr &&
        lv_obj_is_valid(static_cast<lv_obj_t*>(resource.object))) {
      lv_obj_delete(static_cast<lv_obj_t*>(resource.object));
    }
    if (resource.font != nullptr)
      lv_tiny_ttf_destroy(static_cast<lv_font_t*>(resource.font));
  }
}

Result LvglFeatureProvider::failed(const core::feature::Request& request,
                                   std::string code,
                                   std::string message) const noexcept {
  return {request.request_id, request.surface_id, Status::kFailed, std::nullopt,
          core::feature::Error{std::move(code), std::move(message), false},
          std::nullopt, std::nullopt, std::nullopt, std::nullopt,
          std::nullopt, std::nullopt};
}

Result LvglFeatureProvider::unsupported(
    const core::feature::Request& request) const noexcept {
  return {request.request_id,
          request.surface_id,
          Status::kUnsupported,
          std::nullopt,
          core::feature::Error{"CAPABILITY_UNSUPPORTED",
                               "LVGL provider does not implement this method",
                               false},
          std::nullopt, std::nullopt, std::nullopt, std::nullopt,
          std::nullopt, std::nullopt};
}

std::string LvglFeatureProvider::fileKey(const std::string& surface_id,
                                         const std::string& path) {
  return surface_id + "\n" + path;
}

Result LvglFeatureProvider::invokePrompt(
    const core::feature::Request& request) noexcept {
  if (request.text.empty()) {
    return failed(request, "INVALID_ARGUMENT", "prompt text is empty");
  }
  if (request.text == "__unsupported__") return unsupported(request);
  if (request.text == "__failed__") {
    return failed(request, "PROMPT_FAILED", "deterministic prompt failure");
  }
  if (request.text == "__cancelled__") {
    return {request.request_id, request.surface_id, Status::kCancelled,
            std::nullopt, std::nullopt, std::nullopt, std::nullopt,
            std::nullopt, std::nullopt, std::nullopt, std::nullopt};
  }
  if (parent_object_ == nullptr) {
    return failed(request, "PLATFORM_REJECTED", "LVGL display is unavailable");
  }
  destroyToast(request.surface_id.wire());
  if (!createPromptLabel(request.surface_id.wire(), request.text)) {
    return failed(request, "OUT_OF_MEMORY", "prompt object allocation failed");
  }
  if (request.method == Method::kConfirm) {
    return {request.request_id, request.surface_id, Status::kSuccess,
            std::nullopt, std::nullopt, true, std::nullopt, std::nullopt,
            std::nullopt, std::nullopt, std::nullopt};
  }
  return {request.request_id, request.surface_id, Status::kSuccess,
          std::nullopt, std::nullopt, std::nullopt, std::nullopt,
          std::nullopt, std::nullopt, std::nullopt, std::nullopt};
}

Result LvglFeatureProvider::invokeFetch(
    const core::feature::Request& request) noexcept {
  if (request.method == Method::kFetchCancel) {
    if (!request.cancel_request_id.has_value()) {
      return failed(request, "INVALID_ARGUMENT", "fetch cancel target is missing");
    }
    if (cancel(*request.cancel_request_id, request.surface_id)) {
      return {request.request_id, request.surface_id, Status::kCancelled,
              std::nullopt, std::nullopt, std::nullopt, std::nullopt,
              std::nullopt, std::nullopt, std::nullopt, std::nullopt};
    }
    return failed(request, "STALE_REQUEST", "fetch request is not pending");
  }
  if (request.method != Method::kFetch) return unsupported(request);
  const std::string url = request.url.value_or("");
  if (url.empty()) return failed(request, "INVALID_ARGUMENT", "fetch URL is empty");
  if (fetch_unsupported_.contains(url)) return unsupported(request);
  const auto failed_fetch = fetch_failures_.find(url);
  if (failed_fetch != fetch_failures_.end()) {
    return failed(request, failed_fetch->second.first,
                  failed_fetch->second.second);
  }
  if (request.http_method.empty() || request.http_method == "GET") {
    if (url != "https://example.test/data") {
      return unsupported(request);
    }
    return {request.request_id, request.surface_id, Status::kSuccess,
            std::nullopt, std::nullopt, std::nullopt, 200,
            std::string("{\"ok\":true}"), true, std::nullopt, std::nullopt};
  }
  return failed(request, "FETCH_METHOD", "deterministic provider supports GET only");
}

Result LvglFeatureProvider::invokeFile(
    const core::feature::Request& request) noexcept {
  const std::string path = request.path.value_or("");
  const bool private_scope = path.rfind("private/", 0) == 0 &&
                             path.size() > 8 && path.find("..") == std::string::npos &&
                             path.find('\\') == std::string::npos &&
                             path.find("//") == std::string::npos;
  if (!private_scope) {
    return failed(request, "FILE_PATH", "path is outside application private scope");
  }
  const auto key = fileKey(request.surface_id.wire(), path);
  switch (request.method) {
    case Method::kFileRead: {
      const auto found = private_files_.find(key);
      if (found == private_files_.end()) {
        return failed(request, "FILE_NOT_FOUND", "private file does not exist");
      }
      return {request.request_id, request.surface_id, Status::kSuccess,
              std::nullopt, std::nullopt, std::nullopt, std::nullopt,
              std::nullopt, std::nullopt, found->second, std::nullopt};
    }
    case Method::kFileWrite:
      if (!request.data.has_value()) {
        return failed(request, "INVALID_ARGUMENT", "file data is missing");
      }
      if (request.data->size() > 64 * 1024) {
        return failed(request, "LIMIT_EXCEEDED", "private file exceeds provider limit");
      }
      private_files_[key] = *request.data;
      break;
    case Method::kFileExists:
      return {request.request_id, request.surface_id, Status::kSuccess,
              std::nullopt, std::nullopt, std::nullopt, std::nullopt,
              std::nullopt, std::nullopt, std::nullopt,
              private_files_.contains(key)};
    case Method::kFileDelete:
      private_files_.erase(key);
      break;
    default:
      return unsupported(request);
  }
  return {request.request_id, request.surface_id, Status::kSuccess,
          std::nullopt, std::nullopt, std::nullopt, std::nullopt,
          std::nullopt, std::nullopt, std::nullopt, std::nullopt};
}

Result LvglFeatureProvider::invoke(
    const core::feature::Request& request) noexcept {
  if (request.surface_id.wire().empty() || request.request_id.wire().empty()) {
    return failed(request, "INVALID_ARGUMENT", "feature identity is invalid");
  }

  if (request.module == ModuleId::kSystemPrompt &&
      (request.method == Method::kAlert || request.method == Method::kConfirm)) {
    return invokePrompt(request);
  }
  if (request.module == ModuleId::kSystemFetch) return invokeFetch(request);
  if (request.module == ModuleId::kSystemFile) return invokeFile(request);

  if (request.module == ModuleId::kSystemPrompt &&
      request.method == Method::kShowToast) {
    if (request.text.empty()) {
      return failed(request, "INVALID_ARGUMENT", "toast message is empty");
    }
    destroyToast(request.surface_id.wire());
    if (parent_object_ == nullptr) {
      return failed(request, "PLATFORM_REJECTED", "LVGL display is unavailable");
    }
    if (!createPromptLabel(request.surface_id.wire(), request.text)) {
      return failed(request, "OUT_OF_MEMORY", "toast object allocation failed");
    }
    std::fprintf(stderr,
                 "lvgl.feature.provider module=system.prompt method=showToast status=completed request=%s surface=%s\n",
                 request.request_id.wire().c_str(),
                 request.surface_id.wire().c_str());
    return {request.request_id, request.surface_id, Status::kSuccess, std::nullopt,
            std::nullopt, std::nullopt, std::nullopt, std::nullopt,
            std::nullopt, std::nullopt, std::nullopt};
  }

  if (request.module == ModuleId::kSystemDevice &&
      request.method == Method::kGetInfo) {
    auto* display = lv_display_get_default();
    if (display == nullptr) {
      return failed(request, "PLATFORM_REJECTED", "LVGL display is unavailable");
    }
    const auto width = static_cast<double>(lv_display_get_horizontal_resolution(display));
    const auto height = static_cast<double>(lv_display_get_vertical_resolution(display));
    core::feature::DeviceInfo info{"lvgl-sdl", "1", 1, 1.0, width, height,
                                   width, height, "simulator"};
    std::fprintf(stderr,
                 "lvgl.feature.provider module=system.device method=getInfo status=completed request=%s surface=%s\n",
                 request.request_id.wire().c_str(),
                 request.surface_id.wire().c_str());
    return {request.request_id, request.surface_id, Status::kSuccess,
            std::move(info), std::nullopt, std::nullopt, std::nullopt,
            std::nullopt, std::nullopt, std::nullopt, std::nullopt};
  }

  if (isPageMethod(request)) {
    if (request.method == Method::kSetTitleBar) {
      if (request.text.empty()) {
        return failed(request, "INVALID_ARGUMENT", "title is empty");
      }
      titles_[request.surface_id.wire()] = request.text;
    } else {
      if (!request.text.empty()) titles_[request.surface_id.wire()] = request.text;
      if (request.description.has_value()) {
        meta_descriptions_[request.surface_id.wire()] = *request.description;
      }
      if (request.text.empty() && !request.description.has_value()) {
        return failed(request, "INVALID_ARGUMENT", "meta is empty");
      }
    }
    std::fprintf(stderr, "lvgl.feature.provider module=page.host method=%s status=completed request=%s surface=%s\n",
                 request.method == Method::kSetTitleBar ? "setTitleBar" : "setMeta",
                 request.request_id.wire().c_str(), request.surface_id.wire().c_str());
    return {request.request_id, request.surface_id, Status::kSuccess, std::nullopt,
            std::nullopt, std::nullopt, std::nullopt, std::nullopt,
            std::nullopt, std::nullopt, std::nullopt};
  }

  return unsupported(request);
}

bool LvglFeatureProvider::cancel(const core::RequestId& request_id,
                                 const core::SurfaceId& surface_id) noexcept {
  return pending_fetches_.erase(fileKey(surface_id.wire(), request_id.wire())) != 0;
}

void LvglFeatureProvider::setFetchFailure(std::string url, std::string code,
                                          std::string message) noexcept {
  if (url.empty()) return;
  fetch_failures_[std::move(url)] = {std::move(code), std::move(message)};
}

void LvglFeatureProvider::setFetchUnsupported(std::string url) noexcept {
  if (!url.empty()) fetch_unsupported_.insert(std::move(url));
}

void LvglFeatureProvider::markPendingFetch(const core::RequestId& request_id,
                                           const core::SurfaceId& surface_id) noexcept {
  pending_fetches_.insert(fileKey(surface_id.wire(), request_id.wire()));
}

void LvglFeatureProvider::putPrivateFile(const core::SurfaceId& surface_id,
                                         std::string path,
                                         std::string data) noexcept {
  if (path.rfind("private/", 0) != 0 || path.find("..") != std::string::npos) return;
  private_files_[fileKey(surface_id.wire(), path)] = std::move(data);
}

void LvglFeatureProvider::destroyToast(const std::string& surface_id) noexcept {
  const auto found = toast_objects_.find(surface_id);
  if (found == toast_objects_.end()) return;
  if (found->second.object != nullptr &&
      lv_obj_is_valid(static_cast<lv_obj_t*>(found->second.object))) {
    lv_obj_delete(static_cast<lv_obj_t*>(found->second.object));
  }
  if (found->second.font != nullptr)
    lv_tiny_ttf_destroy(static_cast<lv_font_t*>(found->second.font));
  toast_objects_.erase(found);
}

bool LvglFeatureProvider::createPromptLabel(const std::string& surface_id,
                                            std::string_view text) noexcept {
  const auto bytes = font::systemDefaultFontBytes();
  auto* native_font = lv_tiny_ttf_create_data_ex(
      bytes.data(), bytes.size(), kPromptFontSize, LV_FONT_KERNING_NONE,
      kPromptFontCacheGlyphCount);
  if (native_font == nullptr) return false;
  auto* label = lv_label_create(static_cast<lv_obj_t*>(parent_object_));
  if (label == nullptr) {
    lv_tiny_ttf_destroy(native_font);
    return false;
  }
  const std::string owned_text(text);
  lv_label_set_text(label, owned_text.c_str());
  lv_obj_set_style_text_font(label, native_font, 0);
  lv_obj_set_width(label, LV_SIZE_CONTENT);
  lv_obj_align(label, LV_ALIGN_BOTTOM_MID, 0, -24);
  toast_objects_[surface_id] = PromptResource{label, native_font};
  return true;
}

void LvglFeatureProvider::teardown(const core::SurfaceId& surface_id) noexcept {
  destroyToast(surface_id.wire());
  titles_.erase(surface_id.wire());
  meta_descriptions_.erase(surface_id.wire());
  const std::string prefix = surface_id.wire() + "\n";
  for (auto it = private_files_.begin(); it != private_files_.end();) {
    if (it->first.rfind(prefix, 0) == 0) it = private_files_.erase(it);
    else ++it;
  }
  for (auto it = pending_fetches_.begin(); it != pending_fetches_.end();) {
    if (it->rfind(prefix, 0) == 0) it = pending_fetches_.erase(it);
    else ++it;
  }
}

void LvglFeatureProvider::clear_resources() noexcept {
  for (const auto& [surface_id, resource] : toast_objects_) {
    static_cast<void>(surface_id);
    if (resource.object != nullptr &&
        lv_obj_is_valid(static_cast<lv_obj_t*>(resource.object))) {
      lv_obj_delete(static_cast<lv_obj_t*>(resource.object));
    }
    if (resource.font != nullptr)
      lv_tiny_ttf_destroy(static_cast<lv_font_t*>(resource.font));
  }
  toast_objects_.clear();
  titles_.clear();
  meta_descriptions_.clear();
  fetch_failures_.clear();
  fetch_unsupported_.clear();
  pending_fetches_.clear();
  private_files_.clear();
}

std::size_t LvglFeatureProvider::live_resource_count() const noexcept {
  return toast_objects_.size() + titles_.size() + meta_descriptions_.size() +
         pending_fetches_.size() + private_files_.size();
}

}  // namespace quickapp::lvgl::feature
