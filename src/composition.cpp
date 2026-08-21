#include "quickapp/lvgl/runtime/composition.h"

#include <array>
#include <cctype>

namespace quickapp::lvgl::runtime {
namespace {

constexpr std::array<std::string_view, 6> kKernelModules{
    "kernel.bridge", "kernel.render", "kernel.event",
    "kernel.lifecycle", "kernel.runtime-tree", "kernel.transaction"};

constexpr std::array<std::string_view, 6> kSimulatorModules{
    "platform.lvgl.host", "platform.lvgl.trace",
    "backend.lvgl.libuv.loop", "backend.lvgl.sdl.display",
    "backend.lvgl.sdl.input", "backend.lvgl.package.file"};

constexpr std::array<std::string_view, 6> kEmbeddedModules{
    "platform.lvgl.host", "platform.lvgl.trace",
    "backend.lvgl.builtin.loop", "backend.lvgl.embedded.display",
    "backend.lvgl.embedded.input", "backend.lvgl.package.memory"};

constexpr std::array<std::string_view, 4> kSimulatorOnlyModules{
    "backend.lvgl.libuv.loop", "backend.lvgl.sdl.display",
    "backend.lvgl.sdl.input", "backend.lvgl.package.file"};

constexpr std::array<std::string_view, 4> kEmbeddedOnlyModules{
    "backend.lvgl.builtin.loop", "backend.lvgl.embedded.display",
    "backend.lvgl.embedded.input", "backend.lvgl.package.memory"};

constexpr std::array<std::string_view, 3> kComponents{"View", "Text",
                                                     "Button"};
constexpr std::array<std::string_view, 3> kCapabilities{
    "system.router", "system.prompt", "system.device"};

bool isAsciiLower(char value) noexcept {
  return value >= 'a' && value <= 'z';
}

bool isAsciiLowerOrDigit(char value) noexcept {
  return isAsciiLower(value) || (value >= '0' && value <= '9');
}

bool isValidModuleId(std::string_view value) noexcept {
  if (value.empty() || !isAsciiLower(value.front())) {
    return false;
  }
  bool segment_start = false;
  for (const char character : value) {
    if (character == '.' || character == '-') {
      if (segment_start) {
        return false;
      }
      segment_start = true;
      continue;
    }
    if (!isAsciiLowerOrDigit(character) ||
        (segment_start && !isAsciiLower(character))) {
      return false;
    }
    segment_start = false;
  }
  return !segment_start && value.find('.') != std::string_view::npos;
}

std::size_t moduleCount(const BuildInventoryView& inventory,
                        std::string_view module_id,
                        ModuleCategory category) noexcept {
  std::size_t count = 0;
  for (const ModuleDescriptor& module : inventory.linked_modules) {
    if (module.module_id == module_id && module.category == category) {
      ++count;
    }
  }
  return count;
}

std::size_t categoryCount(const BuildInventoryView& inventory,
                          ModuleCategory category) noexcept {
  std::size_t count = 0;
  for (const ModuleDescriptor& module : inventory.linked_modules) {
    if (module.category == category) {
      ++count;
    }
  }
  return count;
}

bool contains(std::span<const std::string_view> values,
              std::string_view expected) noexcept {
  for (const std::string_view value : values) {
    if (value == expected) {
      return true;
    }
  }
  return false;
}

template <std::size_t Size>
bool hasAllModules(const BuildInventoryView& inventory,
                   const std::array<std::string_view, Size>& modules) noexcept {
  for (const std::string_view module : modules) {
    bool found = false;
    for (const ModuleDescriptor& candidate : inventory.linked_modules) {
      if (candidate.module_id == module) {
        found = true;
        break;
      }
    }
    if (!found) {
      return false;
    }
  }
  return true;
}

template <std::size_t Size>
bool hasAnyModule(const BuildInventoryView& inventory,
                  const std::array<std::string_view, Size>& modules) noexcept {
  for (const std::string_view module : modules) {
    for (const ModuleDescriptor& candidate : inventory.linked_modules) {
      if (candidate.module_id == module) {
        return true;
      }
    }
  }
  return false;
}

CompositionValidation failure(const BuildProfileDefinition& profile,
                              CompositionIssue issue) noexcept {
  return {&profile, issue, true, false};
}

}  // namespace

CompositionValidation CompositionRoot::validateIsolated(
    ProfileId profile_id, const BuildInventoryView& inventory,
    const js::JsEngineProvider& engine_provider) noexcept {
  const BuildProfileDefinition& profile = profileDefinition(profile_id);
  if (inventory.binary_bytes == 0) {
    return failure(profile, CompositionIssue::kInvalidBinaryBytes);
  }

  for (std::size_t index = 0; index < inventory.linked_modules.size();
       ++index) {
    const ModuleDescriptor& module = inventory.linked_modules[index];
    if (!isValidModuleId(module.module_id)) {
      return failure(profile, CompositionIssue::kInvalidModuleId);
    }
    for (std::size_t other = index + 1;
         other < inventory.linked_modules.size(); ++other) {
      if (module.module_id == inventory.linked_modules[other].module_id) {
        return failure(profile, CompositionIssue::kDuplicateModule);
      }
    }
  }

  for (const std::string_view kernel : kKernelModules) {
    if (moduleCount(inventory, kernel, ModuleCategory::kKernel) != 1) {
      return failure(profile, CompositionIssue::kMissingKernel);
    }
  }
  if (moduleCount(inventory, "runtime.js-framework",
                  ModuleCategory::kRuntime) != 1) {
    return failure(profile, CompositionIssue::kInvalidJsFrameworkCount);
  }
  if (categoryCount(inventory, ModuleCategory::kEngine) != 1) {
    return failure(profile, CompositionIssue::kInvalidEngineCount);
  }

  const js::JsEngineDescriptor engine = engine_provider.describe();
  if (engine.engineId != "quickjs" ||
      engine.engineAbi != "quickapp-kit-js-engine-v1" ||
      engine.moduleId != "engine.quickjs" || engine.engineVersion.empty() ||
      moduleCount(inventory, engine.moduleId, ModuleCategory::kEngine) != 1) {
    return failure(profile, CompositionIssue::kEngineDescriptorMismatch);
  }

  const bool simulator = profile_id == ProfileId::kSimulatorDev;
  const auto& required = simulator ? kSimulatorModules : kEmbeddedModules;
  if (!hasAllModules(inventory, required)) {
    return failure(profile, CompositionIssue::kMissingProfileModule);
  }
  const bool contains_forbidden =
      simulator ? hasAnyModule(inventory, kEmbeddedOnlyModules)
                : hasAnyModule(inventory, kSimulatorOnlyModules);
  if (contains_forbidden) {
    return failure(profile, CompositionIssue::kForbiddenProfileModule);
  }
  for (const std::string_view component : kComponents) {
    if (!contains(inventory.components, component)) {
      return failure(profile, CompositionIssue::kMissingV1Component);
    }
  }
  for (const std::string_view capability : kCapabilities) {
    if (!contains(inventory.capabilities, capability)) {
      return failure(profile, CompositionIssue::kMissingV1Capability);
    }
  }
  return {&profile, CompositionIssue::kNone, true, false};
}

}  // namespace quickapp::lvgl::runtime
