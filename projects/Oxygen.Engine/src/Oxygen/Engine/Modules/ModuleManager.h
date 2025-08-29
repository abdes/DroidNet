//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//
#pragma once

#include <functional>
#include <memory>
#include <optional>
#include <ranges>
#include <string_view>
#include <vector>

#include <Oxygen/Base/EnumIndexedArray.h>
#include <Oxygen/Core/PhaseRegistry.h>
#include <Oxygen/Engine/Modules/EngineModule.h>
#include <Oxygen/Engine/api_export.h>
#include <Oxygen/OxCo/Co.h>
#include <Oxygen/OxCo/Nursery.h>

namespace oxygen::engine {

// Forward
class FrameContext;

// Modern, lean ModuleManager. Minimal thread-safety: registration must happen
// outside of frame execution or be externally synchronized by the caller.
class ModuleManager {
public:
  OXGN_NGIN_API explicit ModuleManager(observer_ptr<Engine> engine);

  OXYGEN_MAKE_NON_COPYABLE(ModuleManager)
  OXYGEN_MAKE_NON_MOVABLE(ModuleManager)

  OXGN_NGIN_API ~ModuleManager();

  // Register a module (takes ownership). Modules are sorted by priority.
  OXGN_NGIN_API auto RegisterModule(
    std::unique_ptr<EngineModule> module) noexcept -> bool;

  // Optional: unregister by name. Returns true if removed.
  OXGN_NGIN_API auto UnregisterModule(std::string_view name) noexcept -> void;

  [[nodiscard]] auto GetModuleCount() const noexcept -> size_t
  {
    return modules_.size();
  }

  OXGN_NGIN_NDAPI auto GetModule(std::string_view name) const noexcept
    -> std::optional<std::reference_wrapper<const EngineModule>>;

  // Execute a single phase. This is the canonical entry point used by the
  // engine coordinator. Implementations for many phase types are trivial
  // here; modules implement the actual behavior.
  OXGN_NGIN_API auto ExecutePhase(core::PhaseId phase, FrameContext& ctx)
    -> co::Co<>;

  [[nodiscard]] auto GetModules() const -> decltype(auto)
  {
    return modules_
      | std::views::transform(
        [](const std::unique_ptr<EngineModule>& ptr) -> EngineModule& {
          return *ptr;
        });
  }

private:
  observer_ptr<Engine> engine_;

  std::vector<std::unique_ptr<EngineModule>> modules_;

  // Precomputed non-owning pointers for each phase to speed up per-frame
  // dispatch and to avoid repeated bitmask checks.
  EnumIndexedArray<core::PhaseId, std::vector<EngineModule*>> phase_cache_ {};

  // Rebuild the phase cache. Called after registration changes.
  auto RebuildPhaseCache() noexcept -> void;
};

} // namespace oxygen::engine
