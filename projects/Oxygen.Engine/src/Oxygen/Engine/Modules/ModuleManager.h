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
#include <Oxygen/Composition/Typed.h>
#include <Oxygen/Composition/TypedObject.h>
#include <Oxygen/Core/PhaseRegistry.h>
#include <Oxygen/Engine/Modules/EngineModule.h>
#include <Oxygen/Engine/api_export.h>
#include <Oxygen/OxCo/Co.h>
#include <Oxygen/OxCo/Nursery.h>

namespace oxygen {
class AsyncEngine;
} // namespace oxygen

namespace oxygen::engine {

// Forward
class FrameContext;

// Modern, lean ModuleManager. Minimal thread-safety: registration must happen
// outside of frame execution or be externally synchronized by the caller.
class ModuleManager : public Object {
  OXYGEN_TYPED(ModuleManager)

public:
  OXGN_NGIN_API explicit ModuleManager(observer_ptr<AsyncEngine> engine);

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

  OXGN_NGIN_API auto ExecuteParallelTasks(
    FrameContext& ctx, const UnifiedSnapshot& snapshot) -> co::Co<>;

  [[nodiscard]] auto GetModules() const -> decltype(auto)
  {
    return modules_
      | std::views::transform(
        [](const std::unique_ptr<EngineModule>& ptr) -> EngineModule& {
          return *ptr;
        });
  }

private:
  observer_ptr<AsyncEngine> engine_;

  std::vector<std::unique_ptr<EngineModule>> modules_;

  // Precomputed non-owning pointers for each phase to speed up per-frame
  // dispatch and to avoid repeated bitmask checks.
  EnumIndexedArray<core::PhaseId, std::vector<EngineModule*>> phase_cache_ {};

  // Rebuild the phase cache. Called after registration changes.
  auto RebuildPhaseCache() noexcept -> void;

  // Find module by TypeId for error handling
  auto FindModuleByTypeId(TypeId type_id) const noexcept -> EngineModule*;

  // Handle module errors - remove non-critical failed modules, report critical
  // failures
  auto HandleModuleErrors(FrameContext& ctx, core::PhaseId phase) noexcept
    -> void;
};

} // namespace oxygen::engine
