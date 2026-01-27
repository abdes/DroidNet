//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//
#pragma once

#include <algorithm>
#include <chrono>
#include <memory>
#include <optional>
#include <string_view>

#include <Oxygen/Base/Macros.h>
#include <Oxygen/Base/ObserverPtr.h>
#include <Oxygen/Composition/Composition.h>
#include <Oxygen/Config/EngineConfig.h>
#include <Oxygen/Core/FrameContext.h>
#include <Oxygen/Core/Types/Frame.h>
#include <Oxygen/Engine/ModuleManager.h>
#include <Oxygen/Engine/api_export.h>
#include <Oxygen/OxCo/Co.h>
#include <Oxygen/OxCo/Event.h>
#include <Oxygen/OxCo/LiveObject.h>
#include <Oxygen/OxCo/Nursery.h>

namespace oxygen::content {
class IAssetLoader;
class AssetLoader;
}

namespace oxygen::engine {
class EngineModule;
class TimeManager;
} // namespace oxygen::engine

namespace oxygen {
namespace time {
  class SimulationClock;
  class PresentationClock;
  class NetworkClock;
  class AuditClock;
  class PhysicalClock;
} // namespace time

class Platform;
class Graphics;

//! Async engine simulator orchestrating frame phases.
class AsyncEngine final : public co::LiveObject, public Composition {
  OXYGEN_TYPED(AsyncEngine)
public:
  OXGN_NGIN_API AsyncEngine(std::shared_ptr<Platform> platform,
    std::weak_ptr<Graphics> graphics, EngineConfig config = {}) noexcept;

  OXGN_NGIN_API ~AsyncEngine() override;

  OXYGEN_MAKE_NON_COPYABLE(AsyncEngine)
  OXYGEN_MAKE_NON_MOVABLE(AsyncEngine)

  auto ActivateAsync(co::TaskStarted<> started = {}) -> co::Co<> override
  {
    return OpenNursery(nursery_, std::move(started));
  }

  //! Starts internal coroutine frame loop (returns immediately).
  OXGN_NGIN_API auto Run() -> void override;

  [[nodiscard]] auto IsRunning() const -> bool override
  {
    return nursery_ != nullptr;
  }

  OXGN_NGIN_API auto Stop() -> void override;

  //! Completion event that becomes triggered after the simulator finishes
  //! running the requested number of frames. Can be awaited or polled.
  //! Example:
  //!   if(sim.Completed()) { /* already finished */ }
  //!   co_await sim.Completed(); // suspend until finished (if not yet)
  [[nodiscard]] auto Completed() noexcept -> auto& { return completed_; }
  [[nodiscard]] auto Completed() const noexcept
  {
    return completed_.Triggered();
  }

  [[nodiscard]] auto GetGraphics() noexcept -> std::weak_ptr<Graphics>
  {
    return gfx_weak_;
  }

  [[nodiscard]] auto GetGraphics() const noexcept -> std::weak_ptr<Graphics>
  {
    return gfx_weak_;
  }

  [[nodiscard]] auto GetPlatform() noexcept -> Platform&
  {
    return *platform_; // Guaranteed non-null
  }

  [[nodiscard]] auto GetPlatform() const noexcept -> const Platform&
  {
    return *platform_; // Guaranteed non-null
  }

  // Register a module (takes ownership). Modules are sorted by priority.
  OXGN_NGIN_API auto RegisterModule(
    std::unique_ptr<engine::EngineModule> module) noexcept -> bool;

  // Optional: unregister by name. Returns true if removed.
  OXGN_NGIN_API auto UnregisterModule(std::string_view name) noexcept -> void;

  // Lookup a module by name (delegates to ModuleManager)
  OXGN_NGIN_NDAPI auto GetModule(std::string_view name) const noexcept
    -> std::optional<std::reference_wrapper<engine::EngineModule>>
  {
    if (!module_manager_) {
      return std::nullopt;
    }
    return module_manager_->GetModule(name);
  }

  // Typed lookup by module class T (must provide ClassTypeId())
  template <typename T>
  [[nodiscard]] auto GetModule() const noexcept
    -> std::optional<std::reference_wrapper<T>>
  {
    if (!module_manager_) {
      return std::nullopt;
    }
    return module_manager_->GetModule<T>();
  }

  // Expose subscription API to consumers via AsyncEngine as a thin forwarder
  // so modules and external services can subscribe for future attachments.
  // This returns ModuleManager::Subscription which is an RAII move-only handle.
  using ModuleSubscription = ::oxygen::engine::ModuleManager::Subscription;

  OXGN_NGIN_API auto SubscribeModuleAttached(
    ::oxygen::engine::ModuleAttachedCallback cb, bool replay_existing = false)
    -> ModuleSubscription;

  //! Get current engine configuration
  OXGN_NGIN_NDAPI auto GetEngineConfig() const noexcept -> const EngineConfig&;

  //! Access the optional AssetLoader service created during initialization.
  OXGN_NGIN_NDAPI auto GetAssetLoader() const noexcept
    -> observer_ptr<content::IAssetLoader>;

  //! Set the engine target frames-per-second at runtime. 0 = uncapped.
  //! Value will be clamped to range [0, 240]. Thread-safety is caller's
  //! responsibility.
  OXGN_NGIN_API auto SetTargetFps(uint32_t fps) noexcept -> void;

  // Clock accessors
  OXGN_NGIN_NDAPI auto GetPhysicalClock() const noexcept
    -> const time::PhysicalClock&;

  OXGN_NGIN_NDAPI auto GetSimulationClock() const noexcept
    -> const time::SimulationClock&;
  OXGN_NGIN_NDAPI auto GetSimulationClock() noexcept -> time::SimulationClock&;
  OXGN_NGIN_NDAPI auto GetPresentationClock() const noexcept
    -> const time::PresentationClock&;
  OXGN_NGIN_NDAPI auto GetPresentationClock() noexcept
    -> time::PresentationClock&;
  OXGN_NGIN_NDAPI auto GetNetworkClock() const noexcept
    -> const time::NetworkClock&;
  OXGN_NGIN_NDAPI auto GetNetworkClock() noexcept -> time::NetworkClock&;
  OXGN_NGIN_NDAPI auto GetAuditClock() const noexcept
    -> const time::AuditClock&;
  OXGN_NGIN_NDAPI auto GetAuditClock() noexcept -> time::AuditClock&;

private:
  auto Shutdown() -> co::Co<>;

  // Ordered phases (Category A) - now with module integration
  auto PhaseFrameStart(engine::FrameContext& context) -> co::Co<>;
  auto PhaseInput(engine::FrameContext& context)
    -> co::Co<>; // async (simulated work)
  auto PhaseNetworkReconciliation(engine::FrameContext& context)
    -> co::Co<>; // network packet application & reconciliation
  auto PhaseRandomSeedManagement(engine::FrameContext& context)
    -> void; // synchronous random seed management for determinism
  auto PhaseFixedSim(engine::FrameContext& context)
    -> co::Co<>; // cooperative parallelism within deterministic phase
  auto PhaseGameplay(engine::FrameContext& context)
    -> co::Co<>; // async (simulated work)
  auto PhaseSceneMutation(engine::FrameContext& context)
    -> co::Co<>; // async (simulated work) - B2 barrier
  auto PhaseTransforms(engine::FrameContext& context)
    -> co::Co<>; // async (simulated work)

  // Synchronous snapshot: modules run OnSnapshot first; engine publishes last
  auto PhaseSnapshot(engine::FrameContext& context)
    -> co::Co<const engine::UnifiedSnapshot&>;

  auto ParallelTasks(engine::FrameContext& context,
    const engine::UnifiedSnapshot& snapshot) -> co::Co<>;
  auto PhasePostParallel(engine::FrameContext& context)
    -> co::Co<>; // async (simulated work)

  auto PhaseGuiUpdate(engine::FrameContext& context)
    -> co::Co<>; // async UI processing

  auto PhasePreRender(engine::FrameContext& context)
    -> co::Co<>; // async (simulated work)

  auto PhaseRender(engine::FrameContext& context)
    -> co::Co<>; // async (simulated work)

  auto PhaseCompositing(engine::FrameContext& context)
    -> co::Co<>; // async (simulated work)

  auto PhasePresent(engine::FrameContext& context)
    -> void; // synchronous presentation

  // Poll futures from async jobs (Assets, PSOs, BLAS, LightMaps)
  auto PhaseAsyncPoll(engine::FrameContext& context) -> void;

  auto PhaseBudgetAdapt() -> void;

  auto PhaseFrameEnd(engine::FrameContext& context) -> co::Co<>;

  // Detached services (Category D)
  auto InitializeDetachedServices() -> void;

  // auto SimulateWork(std::chrono::microseconds cost) const
  //   -> co::Co<>; // yields via thread pool
  // auto SimulateWork(std::string name, std::chrono::microseconds cost) const
  //   -> co::Co<>; // named task (thread pool)
  // auto SimulateWorkOrdered(std::chrono::microseconds cost) const
  //   -> co::Co<>; // runs inline (Category A)

  //! Internal coroutine performing the per-frame sequence and yielding.
  auto FrameLoop() -> co::Co<>;

  //! Advances the frame counters to the next frame, and decides if the frame
  //! loop should continue or not.
  auto NextFrame() -> bool;

  // Professional timing system integration
  auto UpdateFrameTiming(engine::FrameContext& context) -> void;

  // void SetRenderGraphBuilder(engine::FrameContext& context);
  // void ClearRenderGraphBuilder(engine::FrameContext& context);

  // std::unique_ptr<RenderGraphBuilder> render_graph_builder_;
  // std::vector<SyntheticTaskSpec> parallel_specs_ {};
  // std::vector<ParallelResult> parallel_results_ {};
  // std::mutex parallel_results_mutex_;
  // std::vector<AsyncJobState> async_jobs_ {};

  bool shutdown_requested_ { false };
  EngineConfig config_; // Engine configuration
  co::Nursery* nursery_ { nullptr };
  frame::SequenceNumber frame_number_ { 0 };
  frame::Slot frame_slot_ { 0 };

  engine::FrameSnapshot snapshot_ {};
  // Persistent across frames for stable view IDs
  engine::FrameContext frame_context_;

  std::shared_ptr<Platform> platform_;
  std::weak_ptr<Graphics> gfx_weak_;

  // Module management system
  std::unique_ptr<engine::ModuleManager> module_manager_;

  std::unique_ptr<content::AssetLoader> asset_loader_;

  // Time system integration
  time::PhysicalTime frame_start_ts_ {};
  time::PhysicalTime next_frame_deadline_ {};
  engine::TimeManager* time_manager_ { nullptr }; // Owned by Composition

  // Signals completion when FrameLoop exits.
  co::Event completed_ {};
};

} // namespace oxygen
