//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//
#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <ranges>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include <Oxygen/Base/EnumIndexedArray.h>
#include <Oxygen/Base/ObserverPtr.h>
#include <Oxygen/Composition/Typed.h>
#include <Oxygen/Composition/TypedObject.h>
#include <Oxygen/Core/EngineModule.h>
#include <Oxygen/Core/PhaseRegistry.h>
#include <Oxygen/Engine/ModuleEvent.h>
#include <Oxygen/Engine/api_export.h>
#include <Oxygen/OxCo/Co.h>
#include <Oxygen/OxCo/Nursery.h>

namespace oxygen {
class AsyncEngine;
namespace console {
  class Console;
} // namespace console
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

  // Register a module (takes ownership). 'modules_' preserves attach order;
  // per-phase execution order is computed from priorities in the phase cache.
  OXGN_NGIN_API auto RegisterModule(
    std::unique_ptr<EngineModule> module) noexcept -> bool;

  // Optional: unregister by name. Returns true if removed.
  OXGN_NGIN_API auto UnregisterModule(std::string_view name) noexcept -> void;

  [[nodiscard]] auto GetModuleCount() const noexcept -> size_t
  {
    return modules_.size();
  }

  OXGN_NGIN_NDAPI auto GetModule(std::string_view name) const noexcept
    -> std::optional<std::reference_wrapper<EngineModule>>;

  // Typed lookup by module class T (must provide ClassTypeId())
  template <IsTyped ModuleT>
  [[nodiscard]] auto GetModule() const noexcept
    -> std::optional<std::reference_wrapper<ModuleT>>
  {
    for (const auto& m : modules_) {
      if (m && m->GetTypeId() == ModuleT::ClassTypeId()) {
        // Cast the stored EngineModule reference to the requested ModuleT
        // and return a reference_wrapper to the derived type.
        return std::make_optional(std::ref(static_cast<ModuleT&>(*m)));
      }
    }
    return std::nullopt;
  }

  // Execute a single phase. This is the canonical entry point used by the
  // engine coordinator. Implementations for many phase types are trivial
  // here; modules implement the actual behavior.
  OXGN_NGIN_API auto ExecutePhase(
    core::PhaseId phase, observer_ptr<FrameContext> ctx) -> co::Co<>;

  OXGN_NGIN_API auto ExecuteParallelTasks(observer_ptr<FrameContext> ctx,
    const UnifiedSnapshot& snapshot) -> co::Co<>;

  OXGN_NGIN_API auto ApplyConsoleCVars(
    observer_ptr<const console::Console> console) noexcept -> void;

  [[nodiscard]] auto GetModules() const -> decltype(auto)
  {
    return modules_
      | std::views::transform(
        [](const std::unique_ptr<EngineModule>& ptr) -> EngineModule& {
          return *ptr;
        });
  }

  // Minimal synchronous subscription API for module attach notifications.
  // SubscribeModuleAttached returns a move-only Subscription RAII object
  // that will automatically unsubscribe on destruction. If replay_existing
  // is true the callback is invoked synchronously for already-attached
  // modules (in attach order) after the subscription is registered.

  class Subscription {
  public:
    Subscription() noexcept = default;
    OXGN_NGIN_API Subscription(Subscription&& other) noexcept;
    OXGN_NGIN_API Subscription& operator=(Subscription&& other) noexcept;
    OXGN_NGIN_API ~Subscription() noexcept;

    // Explicitly cancel early; otherwise destructor unsubscribes.
    OXGN_NGIN_API void Cancel() noexcept;

  private:
    friend class ModuleManager;
    uint64_t id_ { 0 };
    observer_ptr<ModuleManager> owner_ { nullptr };
    std::weak_ptr<int> alive_token_ {};
  };

  // Subscribe to module attach events. Default replay_existing=false.
  OXGN_NGIN_API auto SubscribeModuleAttached(
    ModuleAttachedCallback cb, bool replay_existing = false) -> Subscription;

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
  auto HandleModuleErrors(
    observer_ptr<FrameContext> ctx, core::PhaseId phase) noexcept -> void;

  // Internal subscription bookkeeping (private API)
  void UnsubscribeSubscription(uint64_t id) noexcept;

  // Subscriber storage (thread-safety: minimal mutex guarding)
  std::mutex subscribers_mutex_;
  std::unordered_map<uint64_t, ModuleAttachedCallback> attached_subscribers_ {};
  uint64_t next_subscriber_id_ { 1 };
  std::shared_ptr<int> alive_token_ {};
};

} // namespace oxygen::engine
