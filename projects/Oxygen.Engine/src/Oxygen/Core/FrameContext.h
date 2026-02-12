//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

//------------------------------------------------------------------------------
// Encapsulated frame context for the AsyncEngine example with strict access
// control and phase-dependent mutation restrictions. This implementation
// enforces the AsyncEngine execution model through capability tokens and
// compile-time access restrictions.
//
// ROLES & CAPABILITIES:
//
// - EngineTag methods are engine-only and may assume main-thread execution in
//   non-parallel phases.
// - Typed data is keyed by strong TypeId (T::ClassTypeId()); no RTTI.
//
// CONCURRENCY & PHASES:
//
// - ParallelTasks never access FrameContext; they operate on a passed
//   UnifiedSnapshot only.
// - PublishSnapshots runs on the main thread and is not concurrent.
// - Phase checks gate all mutators. PhaseCanMutateGameState governs module data
//   staging; staging is also allowed during kSnapshot.
// - Shape changes to surfaces_/views_/staged_module_data_ occur on the main
//   thread; readers may take shared locks for clarity.
// - presentable_flags_: elements updated atomically; container shape updated
//   only on the main thread by the engine coordinator.
//
// SNAPSHOT CONTRACT:
// - Parallel Tasks only consume the UnifiedSnapshot passed to them by the
//   engine. They do not see the FrameContext and cannot read from it or write
//   to it. This is a base contract for the safety of the engine's snapshot
//   publishing.
// - UnifiedSnapshot is double-buffered; engine updates visible index and
//   snapshot_version_ at publish time.
// - snapshot_version_ is monotonic and intended for tracing/validation.
// - Input snapshot pointer is published via atomic shared_ptr (release-store /
//   acquire-load).
//------------------------------------------------------------------------------

#pragma once

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <concepts>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <ranges>
#include <shared_mutex>
#include <string>
#include <type_traits>
#include <unordered_map>
#include <variant>
#include <vector>

#include <Oxygen/Base/Macros.h>
#include <Oxygen/Base/ObserverPtr.h>
#include <Oxygen/Composition/Named.h>
#include <Oxygen/Composition/Typed.h>
#include <Oxygen/Config/EngineConfig.h>
#include <Oxygen/Core/EngineTag.h>
#include <Oxygen/Core/PhaseRegistry.h>
#include <Oxygen/Core/Types/Frame.h>
#include <Oxygen/Core/Types/Scissors.h>
#include <Oxygen/Core/Types/View.h>
#include <Oxygen/Core/Types/ViewPort.h>
#include <Oxygen/Core/api_export.h>

namespace oxygen::scene {
class Scene;
}

namespace oxygen {
class Graphics;
namespace graphics {
  class Surface;
  class Framebuffer;
} // namespace graphics
} // namespace oxygen

// Forward declaration for ThreadPool from the OxCo module
namespace oxygen::co {
class ThreadPool;
}

namespace oxygen::engine {

struct AssetRegistry;
struct ShaderCompilationDb;

struct ResourceIntegrationData; // FIXME: placeholder for future
struct FrameProfiler; // FIXME: placeholder for future

//=== Timing data ===---------------------------------------------------------//

//! Module-accessible timing data.
struct ModuleTimingData {
  //! Variable timestep delta time, affected by time scaling and pause state
  /*!
   Time elapsed since the last frame for variable timestep systems like
   rendering, UI, and effects. This value is scaled by time_scale and becomes
   zero when the game is paused.
  */
  time::CanonicalDuration game_delta_time { std::chrono::nanoseconds(0) };

  //! Fixed timestep delta time for deterministic simulation systems
  /*!
   Constant time step used for physics, networking, and other systems requiring
   deterministic behavior. Typically, 16.67ms (60Hz). This value is never
   affected by time scaling or pause state.
  */
  time::CanonicalDuration fixed_delta_time { std::chrono::nanoseconds(
    16'666'667) };

  //! Current time scaling factor applied to game_delta_time
  /*!
   Multiplier for game time progression. Values > 1.0 speed up time,
   values < 1.0 slow down time, and 0.0 effectively pauses game time.
   Does not affect fixed_delta_time.
  */
  float time_scale { 1.0f };

  //! Whether game time progression is currently paused
  /*!
   When true, game_delta_time becomes zero regardless of actual frame time.
   Fixed timestep systems continue running normally to maintain deterministic
   behavior and network synchronization.
  */
  bool is_paused { false };

  //! Interpolation factor for smooth rendering between fixed timestep updates
  /*!
   Value in range [0,1] indicating how far between the last and next fixed
   timestep update the current frame represents. Used for smooth visual
   interpolation of physics objects and other fixed-timestep data.
  */
  float interpolation_alpha { 0.0f };

  //! Current measured frame rate for adaptive quality control
  /*!
   Smoothed frame rate measurement used by systems for adaptive quality
   decisions. Systems can reduce visual fidelity when FPS drops below
   target thresholds.
  */
  float current_fps { 0.0f };

  //! Number of fixed timestep updates executed this frame
  /*!
   Count of fixed timestep iterations performed during the current frame.
   Values > 1 indicate the engine is catching up after frame drops.
   Useful for performance monitoring and adaptive quality decisions.
  */
  uint32_t fixed_steps_this_frame { 0 };
};

//=== Error Reporting System ===----------------------------------------------//

//! Frame error information for module error reporting.
/*!
 Simple error structure containing source module type information and
 human-readable message. Used for basic error propagation from modules to the
 engine frame loop without exceptions.
 ### Usage Examples

 ```cpp
 // Report error from typed module
 context.ReportError<MyModule>("Failed to initialize graphics");

 // Clear errors from specific module type
 context.ClearErrorsFromSource<MyModule>();
 ```
 @see FrameContext::ReportError, FrameContext::HasErrors
*/
struct FrameError {
  TypeId source_type_id { kInvalidTypeId }; //!< Source module type identifier
  std::string message; //!< Human-readable error message
  //!< Optional unique identifier for error source
  std::optional<std::string> source_key;
};

// Unique identifier for a surface
using SurfaceIdTag = struct SurfaceIdTag;
using SurfaceId = oxygen::NamedType<uint64_t, SurfaceIdTag, oxygen::Comparable,
  oxygen::Hashable, oxygen::Printable>;

struct ViewMetadata {
  std::string name;
  std::string purpose; // e.g. "primary", "shadow", "reflection"
  bool with_atmosphere { false };
};

// Complete context for a view, including its output
struct ViewContext {
  ViewId id {}; // Unique identifier assigned by FrameContext::AddView
  View view;
  ViewMetadata metadata;

  // Render target (set by Renderer/Compositor)
  observer_ptr<graphics::Framebuffer> output {};
};

//=== ModuleData Facade Architecture ===--------------------------------------//

/*!
 Type-safe module data storage with mutation policy facade pattern.

 Provides strict access control and type safety for module-specific data
 contributions to the frame context. Uses template policies to control
 mutability and enforce proper phase-based access patterns.

 ### Invariants

 - Keys are `TypeId` values obtained from `T::ClassTypeId()`.
 - If `Has<T>()` is true, the stored pointer is non-null and points to a
   value of exactly `std::decay_t<T>`.
 - No RTTI is used; access is performed via `static_cast` under the strong
   type contract.
 - `Keys()` returns the exact set of staged type ids; order is unspecified.

 ### Usage Examples

 ```cpp
 // Stage typed data into FrameContext (see StageModuleData)
 context.StageModuleData<MyType>(MyType{});

 // Read during allowed phases
 auto view = context.GetStagingModuleData().Get<MyType>();
 if (view) {
   // Access via reference_wrapper
   auto& value = view->get();
   // ...
 }
 ```

 @see FrameContext::StageModuleData, ModuleDataImmutable, ModuleDataMutable
*/
template <typename MutationPolicy> class ModuleData {
public:
  //! Default constructor
  ModuleData() = default;

  OXYGEN_MAKE_NON_COPYABLE(ModuleData)
  OXYGEN_MAKE_NON_MOVABLE(ModuleData)

  ~ModuleData() = default;

  //! Move constructor from mutable to immutable (one-way conversion)
  template <typename OtherPolicy>
    requires(!MutationPolicy::is_mutable && OtherPolicy::is_mutable)
  explicit ModuleData(ModuleData<OtherPolicy>&& other) noexcept
    : data_(std::move(other.data_))
  {
    (void)std::move(other);
  }

  //! Check if data of type T exists
  template <IsTyped T> [[nodiscard]] auto Has() const noexcept -> bool
  {
    return data_.contains(T::ClassTypeId());
  }

  //! Get list of all type IDs that have staged data
  [[nodiscard]] auto Keys() const noexcept -> std::vector<TypeId>
  {
    std::vector<TypeId> keys;
    keys.reserve(data_.size());
    for (const auto& type_id : data_ | std::views::keys) {
      keys.push_back(type_id);
    }
    return keys;
  }

  //! Get typed data with mutation policy enforcement
  template <IsTyped T>
  [[nodiscard]] auto Get() const noexcept ->
    typename MutationPolicy::template ViewType<T>
  {
    const auto iter = data_.find(T::ClassTypeId());
    if (iter == data_.end()) {
      return MutationPolicy::template GetDefault<T>();
    }

    // Invariant: If a type id is present, the stored pointer is non-null and
    // points to a value of exactly std::decay_t<T>. No RTTI or dynamic_cast is
    // used; we rely on the type system contract (T::ClassTypeId()) and static
    // casts for performance.
    using StoredT = std::decay_t<T>;
    auto* typed_ptr = static_cast<StoredT*>(iter->second.get());

    // Single call: policy decides mutability (const or non-const view) based on
    // MutationPolicy. Passing a non-const reference allows ImmutablePolicy to
    // bind to const&, and MutablePolicy to return a mutable reference wrapper.
    return MutationPolicy::template CreateView<StoredT>(*typed_ptr);
  }

private:
  template <typename OtherPolicy> friend class ModuleData;
  friend class FrameContext;

  // Store moved-in values directly using type-erased container to avoid an
  // extra allocation and pointer indirection. The contract is: callers
  // must stage values of the exact stored type (decay_t<T>) under the
  // key T::ClassTypeId(). Storage is shared_ptr<void> to avoid RTTI. Access
  // uses static_cast under the strong type contract.
  std::unordered_map<TypeId, std::shared_ptr<void>> data_;

  template <IsTyped T> [[nodiscard]] static auto GetTypeId() noexcept -> TypeId
  {
    // Use the project's strong-type id accessor instead of C++ RTTI.
    return T::ClassTypeId();
  }
};

//! Mutation policies for data access control
struct MutablePolicy {
  template <typename T>
  using ViewType = std::optional<std::reference_wrapper<T>>;

  template <typename T>
  static auto CreateView(T& data) noexcept
    -> std::optional<std::reference_wrapper<T>>
  {
    return std::ref(data);
  }

  template <typename T>
  static auto GetDefault() noexcept -> std::optional<std::reference_wrapper<T>>
  {
    return std::nullopt;
  }

  static constexpr bool is_mutable = true;
};

struct ImmutablePolicy {
  template <typename T>
  using ViewType = std::optional<std::reference_wrapper<const T>>;

  template <typename T>
  static auto CreateView(const T& data) noexcept
    -> std::optional<std::reference_wrapper<const T>>
  {
    return std::cref(data);
  }

  template <typename T>
  static auto GetDefault() noexcept
    -> std::optional<std::reference_wrapper<const T>>
  {
    return std::nullopt;
  }

  static constexpr bool is_mutable = false;
};

//! Type aliases for module data
using ModuleDataMutable = ModuleData<MutablePolicy>;
using ModuleDataImmutable = ModuleData<ImmutablePolicy>;

//=== FrameSnapshot ==========================================================//

//! Per-frame snapshot passed to parallel tasks. Contains engine-level
//! coordination data and efficient read-only views into heavy data structures
//! organized for parallel task consumption. Additional data can be contributed
//! to ModuleData specifically for snapshot, but **only** during PhaseSnapshot.
struct FrameSnapshot {
  // Basic frame identification and timing
  frame::SequenceNumber frame_index { 0 };
  uint64_t epoch { 0 };
  std::chrono::steady_clock::time_point frame_start_time;
  std::chrono::microseconds frame_budget { 16667 }; // ~60 FPS default

  // Module-accessible timing data for parallel tasks
  ModuleTimingData timing;

  // Engine coordination context for adaptive scheduling
  struct BudgetContext {
    std::chrono::milliseconds cpu_budget { 16 };
    std::chrono::milliseconds gpu_budget { 16 };
    bool is_over_budget { false };
    bool should_degrade_quality { false };
  } budget;

  // Module coordination hints for quality vs performance tradeoffs
  struct ExecutionHints {
    bool skip_expensive_tasks { false };
    bool prefer_quality_over_speed { false };
    uint32_t max_parallel_tasks { 0 }; // 0 = use default
    uint32_t lod_bias { 0 }; // LOD adjustment hint
  } hints;

  // Task group coordination (for structured concurrency)
  struct TaskGroupInfo {
    uint32_t expected_task_count { 0 };
    std::chrono::microseconds timeout { 10000 }; // 10ms default timeout
    bool cancellation_requested { false };
  } task_group;

  // Version/generation tracking for async pipeline validation
  struct ValidationContext {
    uint64_t snapshot_version { 0 };
    uint64_t resource_generation { 0 };
    bool allow_stale_resources { false };
  } validation;
};

//------------------------------------------------------------------------------
// Template-based common data structures to eliminate duplication between
// GameState and GameStateSnapshot
//------------------------------------------------------------------------------

// Common game data structure template with SAME mutation policies as ModuleData
template <typename MutationPolicy> struct GameData {
  // TODO: figure out what common game data we have
  // FUTURE (authoritative, cross-module data captured in the snapshot):
  // - World/environment state (sky/atmosphere, exposure, weather, time-of-day)
  // - Physics/global settings (e.g., gravity, solver params, debug toggles)
  // - Animation pose state (final sampled/retargeted poses per entity)
  // - Terrain/heightfields and water state (streaming tiles, LOD, materials)

  // Default constructor
  GameData() = default;

  OXYGEN_MAKE_NON_COPYABLE(GameData)
  OXYGEN_MAKE_NON_MOVABLE(GameData)

  ~GameData() = default;

  // Move constructor from mutable to immutable (one-way conversion)
  template <typename OtherPolicy>
    requires(!MutationPolicy::is_mutable && OtherPolicy::is_mutable)
  explicit GameData(GameData<OtherPolicy>&& other) noexcept
  {
    // TODO: When we have actual data members, convert them here For now, no
    // conversion needed since struct is empty
    (void)std::move(other);
  }
};

// Type aliases - SAME pattern as ModuleData
using GameDataMutable = GameData<MutablePolicy>;
using GameDataImmutable = GameData<ImmutablePolicy>;

// Opaque input snapshot pointer (type-erased). Published once per frame
// by the engine coordinator during PhaseId::kInput.
// Thread-safety: stored/retrieved via atomic shared_ptr with
// release-store/acquire-load semantics.
using InputBlobPtr = std::shared_ptr<const void>;

//! Read-only immutable snapshot of authoritative game state.
/*!
 Contains heavy application data that forms the DATA STORAGE LAYER. Owns actual
 game data containers and provides thread-safe access via shared_ptr. Used by
 modules needing access to heavy game/scene data.

 ### Architecture Notes

 This is the authoritative snapshot of all game state at a specific frame.
 GameStateSnapshot owns the data, while FrameSnapshot provides efficient views
 into this data for parallel task consumption.

 @see FrameSnapshot, FrameContext::GetGameStateSnapshot
*/
struct GameStateSnapshot {
  std::vector<ViewContext> views;
  InputBlobPtr input; // input snapshot at capture time (type-erased)

  // Cross-module game data using immutable policy
  GameDataImmutable gameData;

  // Typed opaque handle for optional per-frame user/context data.
  struct UserContextHandle {
    std::shared_ptr<const void> ptr;
  };
  UserContextHandle userContext; // optional, read-only opaque data
  // per-frame surfaces (frozen at FrameStart)
  std::vector<observer_ptr<graphics::Surface>> surfaces;
  // per-surface presentable flags (1:1 with surfaces)
  std::vector<uint8_t> presentable_flags;

  // Monotonic version assigned at PublishSnapshots() time. Useful for
  // debugging, tracing and ensuring workers observe increasing versions.
  uint64_t version = 0;

  // Additional items include: scripting or UI interactions
};

// Atomic snapshot publication using private unified structure RATIONALE: Keep
// GameStateSnapshot and FrameSnapshot separate for clean APIs but publish them
// together atomically for consistent lock-free access
struct UnifiedSnapshot {
  GameStateSnapshot gameSnapshot; // Value type for proper immutability
  FrameSnapshot frameSnapshot;
  ModuleDataImmutable moduleData;
};

class FrameContext final {
public:
  //! Immutable: read-only for app lifetime.
  struct Immutable {
    const EngineConfig* config = nullptr;
    const AssetRegistry* assets = nullptr;
    const ShaderCompilationDb* shaderDatabase = nullptr;
  };

  //! Default constructor initializing empty snapshot buffers.
  OXGN_CORE_API FrameContext();

  //! Construct with immutable dependencies that live for application lifetime.
  OXGN_CORE_API explicit FrameContext(const Immutable& imm);

  OXYGEN_MAKE_NON_COPYABLE(FrameContext)
  OXYGEN_MAKE_NON_MOVABLE(FrameContext)

  ~FrameContext() = default;

  // Per-frame metadata (access via getters). Use engine-only Advance* to move
  // these values forward monotonically.

  //! Get the current frame index (monotonic counter).
  auto GetFrameSequenceNumber() const noexcept { return frame_index_; }

  //! Get the current frame slot (for multi-buffered resources).
  auto GetFrameSlot() const noexcept { return frame_slot_; }

  //! Get the current epoch value (for resource lifecycle management).
  auto GetEpoch() const noexcept { return engine_state_.epoch; }

  auto SetFrameSequenceNumber(
    frame::SequenceNumber frameNumber, EngineTag) noexcept -> void
  {
    frame_index_ = frameNumber;
  }

  //! Engine-only: Set the current frame slot. Requires EngineTag capability.
  auto SetFrameSlot(frame::Slot slot, EngineTag) noexcept -> void
  {
    frame_slot_ = slot;
  }

  //! Engine-only: Advance epoch by one. Requires EngineTag capability.
  auto AdvanceEpoch(EngineTag) noexcept -> void { ++engine_state_.epoch; }

  // Convenience getters for individual immutable members
  //! Get the engine configuration pointer.
  auto GetEngineConfig() const noexcept { return immutable_.config; }

  //! Get the asset registry pointer.
  auto GetAssetRegistry() const noexcept { return immutable_.assets; }

  //! Get the shader compilation database pointer.
  auto GetShaderCompilationDb() const noexcept
  {
    return immutable_.shaderDatabase;
  }

  //! Engine-only: Set graphics backend reference. Requires EngineTag
  //! capability.
  auto SetGraphicsBackend(std::weak_ptr<Graphics> graphics, EngineTag) noexcept
    -> void
  {
    engine_state_.graphics = std::move(graphics);
  }

  //! Thread-safe fence value access (atomic read).
  auto GetFrameFenceValue() const noexcept
  {
    return engine_state_.frame_fence_value.load(std::memory_order_acquire);
  }

  //! Engine-only: Update fence value after GPU submission.
  auto SetFrameFenceValue(uint64_t value, EngineTag) noexcept -> void
  {
    engine_state_.frame_fence_value.store(value, std::memory_order_release);
  }

  // Engine-only resource integration data management RATIONALE: AsyncUploads
  // lifecycle is tied to engine GPU scheduling
  auto SetAsyncUploads(
    observer_ptr<ResourceIntegrationData> uploads, EngineTag) noexcept -> void
  {
    engine_state_.async_uploads = uploads;
  }

  auto GetAsyncUploads() const noexcept { return engine_state_.async_uploads; }

  // Engine-only profiler management
  auto SetProfiler(observer_ptr<FrameProfiler> profiler, EngineTag) noexcept
    -> void
  {
    engine_state_.profiler = profiler;
  }

  auto GetProfiler() const noexcept { return engine_state_.profiler; }

  // Scene pointer (engine-managed). Provided for modules like TransformsModule
  // and ScenePrep. Lifetime is owned by the engine; FrameContext observes it.
  // Scene is module-managed (not EngineState); no EngineTag required.
  OXGN_CORE_API auto SetScene(observer_ptr<scene::Scene> s) noexcept -> void;

  [[nodiscard]] auto GetScene() const noexcept -> observer_ptr<scene::Scene>
  {
    return scene_;
  }

  // Engine-only thread pool management RATIONALE: Thread pool lifecycle is
  // engine-managed to ensure proper shutdown sequencing and worker thread
  // coordination
  auto SetThreadPool(co::ThreadPool* pool, EngineTag) noexcept -> void
  {
    engine_state_.thread_pool = observer_ptr { pool };
  }

  auto GetThreadPool() const noexcept { return engine_state_.thread_pool; }

  // Render graph builder is no longer exposed via FrameContext.
  // PublishSnapshots now returns a reference to the freshly populated
  // UnifiedSnapshot (engine-only). Consumers should not access snapshots via
  // global getters; the engine passes the snapshot reference to parallel tasks
  // directly.
  OXGN_CORE_API auto PublishSnapshots(EngineTag) noexcept -> UnifiedSnapshot&;

  //! Stage typed module data for inclusion in next snapshot
  /*!
   Stage a typed value for the upcoming snapshot using the project's
   `TypeId` system. The value is stored under `T::ClassTypeId()` with exact
   type `std::decay_t<T>`; no RTTI is used.

   @tparam T Typed payload; must satisfy `IsTyped` and be movable.
   @param data The value to move into the staging store.
   @return void

   ### Behavior

   - Allowed phases: any phase that can mutate GameState, and
     `PhaseId::kSnapshot`.
   - Misuse: aborts the process via `CHECK_F` when called in a disallowed
     phase.
   - Duplicate key: throws `std::invalid_argument` if the `TypeId` is already
     staged for this frame.

   @see GetStagingModuleData, StageModuleDataErased
  */
  template <IsTyped T>
    requires std::movable<std::decay_t<T>>
  auto StageModuleData(T data) -> void
  {
    using StoredT = std::decay_t<T>;
    // Allocate concrete object and delegate to type-erased helper implemented
    // in the .cpp to hide synchronization, phase checks, and duplicate logic.
    auto ptr = std::make_shared<StoredT>(std::move(data));
    StageModuleDataErased(
      T::ClassTypeId(), std::static_pointer_cast<void>(std::move(ptr)));
  }

  //! Get mutable facade for staging module data during mutation phases. The
  //! ModuleDataMutable only has non-mutating APIs, and can still be mutated
  //! only through `StageModuleData`.
  OXGN_CORE_API auto GetStagingModuleData() noexcept -> ModuleDataMutable&;

  OXGN_CORE_API auto SetInputSnapshot(InputBlobPtr inp, EngineTag) noexcept
    -> void;

  auto GetInputSnapshot() const noexcept
  {
    return atomic_input_snapshot_.load(std::memory_order_acquire);
  }

  auto GetViews() const noexcept
  {
    return std::ranges::views::transform(
      views_, [](const auto& pair) { return std::cref(pair.second); });
  }

  // Register a new view and allocate a stable ViewId
  // Returns the ViewId that should be used for subsequent updates/removal
  // Phase: Must be called before kSnapshot
  OXGN_CORE_API auto RegisterView(ViewContext view) noexcept -> ViewId;

  // Update an existing view's data
  // Phase: Must be called before kSnapshot
  OXGN_CORE_API auto UpdateView(ViewId id, ViewContext view) noexcept -> void;

  // Remove a view from the frame context
  // Phase: Must be called before kSnapshot
  OXGN_CORE_API auto RemoveView(ViewId id) noexcept -> void;

  // Set the output framebuffer for a view (Renderer/Compositor only)
  OXGN_CORE_API auto SetViewOutput(
    ViewId id, observer_ptr<graphics::Framebuffer> output) noexcept -> void;

  // Get the full context for a view
  OXGN_CORE_API auto GetViewContext(ViewId id) const -> const ViewContext&;

  // Clear all views with phase validation (Engine only)
  OXGN_CORE_API auto ClearViews(EngineTag) noexcept -> void;

  OXGN_CORE_API auto SetCurrentPhase(core::PhaseId p, EngineTag) noexcept
    -> void;

  auto GetCurrentPhase() const noexcept { return engine_state_.current_phase; }

  // Public lightweight timing and surface lists used by subsystems
  struct FrameTiming {
    std::chrono::microseconds frame_duration { 0 };
    std::chrono::microseconds pacing_duration { 0 };
    EnumIndexedArray<core::PhaseId, std::chrono::microseconds> stage_timings {};
  };

  // Minimal budget stats used by PhaseBudgetAdapt
  struct BudgetStats {
    std::chrono::milliseconds cpu_budget { 0 };
    std::chrono::milliseconds gpu_budget { 0 };
    // other adaptive counters may be added as needed
  };

  // Combined metrics for performance tracking and adaptive scheduling
  struct Metrics {
    FrameTiming timing;
    BudgetStats budget;
  };

  // Engine-only: timing is managed by the engine coordinator. Require an
  // EngineTag to make accidental external mutation harder. RATIONALE: Frame
  // timing affects adaptive scheduling and budget decisions that must be
  // coordinated by the engine to maintain frame rate targets
  OXGN_CORE_API auto SetFrameTiming(const FrameTiming& t, EngineTag) noexcept
    -> void;

  OXGN_CORE_API auto SetPhaseDuration(core::PhaseId phase,
    std::chrono::microseconds duration, EngineTag) noexcept -> void;

  auto GetFrameTiming() const noexcept { return metrics_.timing; }

  // Engine-only: set/get the recorded frame start time (used for snapshot
  // coordination). Setter requires EngineTag to prevent accidental updates from
  // modules.
  OXGN_CORE_API auto SetFrameStartTime(
    std::chrono::steady_clock::time_point t, EngineTag) noexcept -> void;

  auto GetFrameStartTime() const noexcept { return frame_start_time_; }

  //=== Professional Timing System Access ===-------------------------------//

  // Engine-only: set module timing data for current frame
  OXGN_CORE_API auto SetModuleTimingData(
    const ModuleTimingData& timing, EngineTag) noexcept -> void;

  // Module access to timing data - clean, focused API
  OXGN_CORE_NDAPI auto GetModuleTimingData() const noexcept
    -> const ModuleTimingData&;
  OXGN_CORE_NDAPI auto GetGameDeltaTime() const noexcept
    -> time::CanonicalDuration;
  OXGN_CORE_NDAPI auto GetFixedDeltaTime() const noexcept
    -> time::CanonicalDuration;
  OXGN_CORE_NDAPI auto GetInterpolationAlpha() const noexcept -> float;
  OXGN_CORE_NDAPI auto GetTimeScale() const noexcept -> float;
  OXGN_CORE_NDAPI auto IsGamePaused() const noexcept -> bool;
  OXGN_CORE_NDAPI auto GetCurrentFPS() const noexcept -> float;

  // Engine-only budget statistics for adaptive scheduling RATIONALE: Budget
  // management is part of engine performance control and should not be modified
  // by application modules directly
  OXGN_CORE_API auto SetBudgetStats(
    const BudgetStats& stats, EngineTag) noexcept -> void;

  auto GetBudgetStats() const noexcept { return metrics_.budget; }

  // Combined metrics access for unified performance monitoring RATIONALE:
  // Provides consolidated access to all performance-related data for monitoring
  // and adaptive scheduling decisions
  OXGN_CORE_API auto SetMetrics(const Metrics& metrics, EngineTag) noexcept
    -> void;

  auto GetMetrics() const noexcept { return metrics_; }

  // Return a thread-safe copy of the surface list. Coordinator callers may
  // prefer to call AddSurface/RemoveSurfaceAt/ClearSurfaces instead of mutating
  // the vector directly. RATIONALE: Surface list access is always safe via
  // copy, but direct modification requires phase validation to ensure snapshot
  // consistency.
  auto GetSurfaces() const noexcept
    -> std::vector<observer_ptr<graphics::Surface>>
  {
    std::shared_lock lock(surfaces_mutex_);
    return surfaces_;
  }

  // Coordinator-safe surface mutation APIs. These acquire the snapshot lock and
  // update the list; game modules should use these during ordered phases
  // (FrameStart / SceneMutation) only. PHASE RESTRICTION: Surface modifications
  // are only allowed during early setup phases when the frame structure is
  // being established. Surface lifetime must be guaranteed for the frame cycle.
  // Remove the surface if it is no longer valid.
  OXGN_CORE_API auto AddSurface(observer_ptr<graphics::Surface> s) noexcept
    -> void;

  // TODO: think if we want to allow removing surfaces directly using the
  // original pointer, or if we want to manage surface changesets and only allow
  // removal by index.
  OXGN_CORE_API auto RemoveSurfaceAt(size_t index) noexcept -> bool;

  OXGN_CORE_API auto ClearSurfaces(EngineTag) noexcept -> void;

  OXGN_CORE_API auto SetSurfacePresentable(
    size_t index, bool presentable) noexcept -> void;

  OXGN_CORE_API auto IsSurfacePresentable(size_t index) const noexcept -> bool;

  auto GetPresentableFlags() const noexcept -> std::vector<uint8_t>
  {
    return presentable_flags_;
  }

  OXGN_CORE_API auto GetPresentableSurfaces() const noexcept
    -> std::vector<observer_ptr<graphics::Surface>>;

  OXGN_CORE_API auto ClearPresentableFlags(EngineTag) noexcept -> void;

  // Acquire a shared_ptr to the graphics backend if still available.
  // Coordinator or recording code should call this and check for null.
  // RATIONALE: Graphics backend may be swapped at runtime, so we use weak_ptr
  // to avoid extending lifetime and provide safe access via lock()
  auto AcquireGraphics() const noexcept
  {
    return engine_state_.graphics.lock();
  }

  //=== Error Reporting Interface ===-----------------------------------------//

  //! Report an error from a typed module source.
  /*!
   Reports an error with compile-time type safety. The source module type is
   automatically determined from the template parameter.

   @tparam SourceType Module type reporting the error (must satisfy IsTyped)
   @param message Human-readable error description
   @param source_key Optional unique identifier for the error source
   @note Thread-safe for concurrent access
   @see HasErrors, GetErrors, ClearErrorsFromSource
  */
  template <IsTyped SourceType>
  auto ReportError(const std::string& message,
    const std::optional<std::string>& source_key = std::nullopt) noexcept
    -> void
  {
    std::unique_lock lock { error_mutex_ };
    frame_errors_.emplace_back(FrameError {
      .source_type_id = SourceType::ClassTypeId(),
      .message = std::move(message),
      .source_key = std::move(source_key),
    });
  }

  //! Report an error using a TypeId directly.
  OXGN_CORE_API auto ReportError(TypeId source_type_id, std::string message,
    std::optional<std::string> source_key = std::nullopt) noexcept -> void;

  //! Check if any errors have been reported this frame.
  OXGN_CORE_NDAPI auto HasErrors() const noexcept -> bool;

  //! Get a thread-safe copy of all reported errors.
  OXGN_CORE_NDAPI auto GetErrors() const noexcept -> std::vector<FrameError>;

  //! Clear errors from a specific typed module source.
  template <IsTyped SourceType> auto ClearErrorsFromSource() noexcept -> void
  {
    ClearErrorsFromSource(SourceType::ClassTypeId());
  }

  //! Clear errors from a specific module source by TypeId.
  OXGN_CORE_API auto ClearErrorsFromSource(TypeId source_type_id) noexcept
    -> void;

  //! Clear errors from a specific module source by TypeId and source key.
  OXGN_CORE_API auto ClearErrorsFromSource(TypeId source_type_id,
    const std::optional<std::string>& source_key) noexcept -> void;

  //! Clear all reported errors.
  OXGN_CORE_API auto ClearAllErrors() noexcept -> void;

private:
  //------------------------------------------------------------------------
  // Private helper methods
  //------------------------------------------------------------------------
  //! Type-erased staging entry point used by the template wrapper.
  OXGN_CORE_API auto StageModuleDataErased(
    TypeId type_id, std::shared_ptr<void> data) -> void;

  // Create and populate both GameStateSnapshot and FrameSnapshot into target
  OXGN_CORE_API auto CreateUnifiedSnapshot(
    UnifiedSnapshot& out, uint64_t version) noexcept -> void;

  // Populate FrameSnapshot within a GameStateSnapshot with coordination
  // context and views. Caller is the engine during kSnapshot on the main
  // thread; phase gating guarantees no concurrent mutation.
  OXGN_CORE_API auto PopulateFrameSnapshot(FrameSnapshot& frame_snapshot,
    const GameStateSnapshot& game_snapshot) const noexcept -> void;

  // (one-off buffer index computation is inlined at call site)

  // Populate the immutable GameStateSnapshot value (capture + convert +
  // version). Called by the engine during publish; non-concurrent by
  // invariant, so no extra locks required beyond phase checks.
  OXGN_CORE_API auto PopulateGameStateSnapshot(
    GameStateSnapshot& out, uint64_t version) noexcept -> void;

  //------------------------------------------------------------------------
  // Private data members with controlled access
  //------------------------------------------------------------------------

  frame::SequenceNumber frame_index_ { 0 };
  frame::Slot frame_slot_ { 0 };
  std::chrono::steady_clock::time_point frame_start_time_ {};

  // Module-accessible timing data (clean interface)
  ModuleTimingData module_timing_ {};

  // Immutable dependencies provided at construction and valid for app lifetime
  Immutable immutable_;

  // Cross-module common game data. Mutation allowed only in phases that allow
  // game state mutation.
  GameDataMutable game_data_;

  // Staged opaque module data. Mutation allowed only in phases that allow game
  // state mutation, or in PhaseId::kSnapshot, where Modules can augment the
  // snapshot. Contributions are merged into the next snapshot at
  // PublishSnapshots() time by the engine at the end of the PhaseId::kSnapshot.
  ModuleDataMutable staged_module_data_;

  // Engine-owned per-frame state. Mutation requires EngineTag capability.
  struct EngineState {
    // Graphics backend handle (maybe swapped at runtime). Keep a weak_ptr to
    // avoid extending the backend lifetime from the FrameContext.
    std::weak_ptr<Graphics> graphics;

    std::atomic<uint64_t> frame_fence_value { 0 };
    observer_ptr<ResourceIntegrationData> async_uploads = nullptr;
    observer_ptr<FrameProfiler> profiler = nullptr;

    // Frame execution state (use centralized PhaseId)
    core::PhaseId current_phase = core::PhaseId::kFrameStart;

    // Thread pool pointer for spawning coroutine-aware parallel work
    observer_ptr<co::ThreadPool> thread_pool = nullptr;

    // Monotonic epoch for resource lifecycle management
    uint64_t epoch = 0;
  } engine_state_;

  // Per-frame performance metrics (timing and budget stats). Can be freely
  // mutated at any phase. Not part of snapshot.
  Metrics metrics_ {};

  // Active surfaces. Can be mutated until the PhaseId::kSnapshot phase (not
  // included). Surface destruction must be deferred until frame completes using
  // the Graphics DeferredReclaimer.
  std::vector<observer_ptr<graphics::Surface>> surfaces_;

  // Per-surface presentable flags (1:1 correspondence with surfaces vector)
  // uint8_t used for atomic operations and consistency with parallel workers.
  // Can be mutated until PhaseId::kPresent (not included).
  // NOTE on std::atomic_ref<uint8_t> usage:
  // - Only individual element stores/loads are done atomically by
  //   SetSurfacePresentable/IsSurfacePresentable.
  // - Container shape (size/capacity) is mutated only on the engine main
  //   thread in allowed phases; workers never touch FrameContext.
  // - Do not perform non-atomic reads/writes to these elements in contexts
  //   where atomic writes may occur; use the provided APIs.
  std::vector<uint8_t> presentable_flags_;

  // Active rendering views, in multi-view rendering. There is no 1:1 mapping
  // between views and surfaces. Can be mutated until the PhaseId::kSnapshot
  // phase (not included)
  std::unordered_map<ViewId, ViewContext> views_;

  // Active scene (non-owning, may be null). Not part of GameData because the
  // high level scene is manipulated early in the frame render cycle, uses its
  // own optimized component storage, and is too different from what will be
  // snapshot and finally passed for rendering. Can be mutated until
  // PhaseId::KSceneMutation (not included).
  observer_ptr<scene::Scene> scene_ {}; // active scene (observed)

  // protects staged_module_data_
  mutable std::shared_mutex staged_module_mutex_;
  // protects surfaces_ + presentable_flags_
  mutable std::shared_mutex surfaces_mutex_;
  // protects views_
  mutable std::shared_mutex views_mutex_;

  // Double-buffered unified snapshot for lock-free atomic publication
  std::array<UnifiedSnapshot, 2> snapshot_buffers_;
  // Visible snapshot index: _not_ atomic because only the engine thread writes
  // it during PublishSnapshots and workers never read it directly.
  uint32_t visible_snapshot_index_ { 0 };
  // Snapshot version monotonic counter. Not atomic for the same reason as
  // index: only written by engine thread and not read concurrently by workers.
  uint64_t snapshot_version_ { 0 };

  // Lock-free input snapshot pointer (written once per frame by coordinator)
  std::atomic<InputBlobPtr> atomic_input_snapshot_;

  // Error reporting system state
  mutable std::shared_mutex error_mutex_;
  std::vector<FrameError> frame_errors_;
};

} // namespace oxygen::engine
