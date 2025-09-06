//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

//------------------------------------------------------------------------------
// FrameContext.h
//
// Encapsulated frame context for the AsyncEngine example with strict access
// control and phase-dependent mutation restrictions. This implementation
// enforces the AsyncEngine execution model through capability tokens and
// compile-time access restrictions.
//
// DESIGN PRINCIPLES:
// - Data Encapsulation: All mutable state is private with controlled access
// - Phase-Dependent Access: Operations are restricted based on execution phase
// - Engine Capability Model: Critical operations require EngineTag capability
// - Thread-Safety: Parallel workers access immutable snapshots exclusively
//
// ACCESS CONTROL MODEL:
//
// PARALLEL TASK ARCHITECTURE:
//
// PHASE EXECUTION MODEL:
//
//------------------------------------------------------------------------------

#pragma once

#include <algorithm>
#include <any>
#include <array>
#include <atomic>
#include <cassert>
#include <chrono>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <shared_mutex>
#include <span>
#include <string>
#include <typeindex>
#include <unordered_map>
#include <vector>

#include <Oxygen/Base/Macros.h>
#include <Oxygen/Base/ObserverPtr.h>
#include <Oxygen/Composition/Typed.h>
#include <Oxygen/Core/PhaseRegistry.h>
#include <Oxygen/Core/Types/Frame.h>
#include <Oxygen/Engine/EngineTag.h>
#include <Oxygen/Engine/api_export.h>
#include <Oxygen/Graphics/Common/Surface.h>
#include <Oxygen/Renderer/Types/View.h>

namespace oxygen::scene {
class Scene;
}

namespace oxygen {
class Graphics;
} // namespace oxygen

// Forward declaration for ThreadPool from the OxCo module
namespace oxygen::co {
class ThreadPool;
}

namespace oxygen::engine {

struct EngineConfig;
struct AssetRegistry;
struct ShaderCompilationDb;
struct CommandList;
struct DescriptorHeapPools;
struct ResourceIntegrationData;
struct FrameProfiler;
struct EntityCommandBuffer;
struct PhysicsWorldState;
struct ResourceRegistry;
class RenderGraphBuilder;

struct EngineProps;

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
  std::optional<std::string>
    source_key; //!< Optional unique identifier for error source
};

//=== ModuleData Facade Architecture ===--------------------------------------//

/*!
 Type-safe module data storage with mutation policy facade pattern.

 Provides strict access control and type safety for module-specific data
 contributions to the frame context. Uses template policies to control
 mutability and enforce proper phase-based access patterns.
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
    const auto type_id = GetTypeId<T>();
    return data_.contains(type_id);
  }

  //! Get list of all type IDs that have staged data
  [[nodiscard]] auto Keys() const noexcept -> std::vector<std::type_index>
  {
    std::vector<std::type_index> keys;
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
    const auto type_id = GetTypeId<T>();
    const auto iter = data_.find(type_id);
    if (iter == data_.end()) {
      return MutationPolicy::template GetDefault<T>();
    }

    const auto* typed_storage = std::any_cast<T>(&iter->second);
    assert(typed_storage != nullptr && "Type mismatch in ModuleData storage");
    return MutationPolicy::template CreateView<T>(*typed_storage);
  }

private:
  template <typename OtherPolicy> friend class ModuleData;
  friend class FrameContext;

  std::unordered_map<std::type_index, std::any> data_;

  template <IsTyped T>
  [[nodiscard]] static auto GetTypeId() noexcept -> std::type_index
  {
    return std::type_index(typeid(T));
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

// Note: There is no 1:1 mapping between surfaces and views. A surface may have
// zero or many associated views. Each view references exactly one surface (or
// null). Surfaces and views are expected to be finalized by the coordinator
// during FrameStart and remain frozen afterward.
struct ViewInfo {
  std::string view_name; // TODO: consider adding this to View class
  std::optional<View> view; // view-specific camera/matrices

  // Typed opaque handle for surface/device-backed render targets. This
  // preserves the previous type-erasure but gives a distinct compile-time alias
  // so callers don't mix handles accidentally.
  struct SurfaceHandle {
    std::shared_ptr<void> ptr;
  };
  SurfaceHandle surface; // opaque surface/target handle
};

//! Opaque input snapshot container for type-erased input data.
struct InputSnapshot {
  std::shared_ptr<const void> blob; // host-specific input snapshot
};

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
  std::vector<ViewInfo> views; // per-view transforms & targets
  std::shared_ptr<const InputSnapshot> input; // input snapshot at capture time
  // std::vector<LightData> lights; // per-frame lights (copied)
  // std::vector<DrawBatch> drawBatches; // batches computed during build

  // Cross-module game data using immutable policy
  GameDataImmutable gameData;

  // Typed opaque handle for optional per-frame user/context data.
  struct UserContextHandle {
    std::shared_ptr<const void> ptr;
  };
  UserContextHandle userContext; // optional, read-only opaque data
  // per-frame surfaces (frozen at FrameStart)
  std::vector<std::shared_ptr<graphics::Surface>> surfaces;
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
  //------------------------------------------------------------------------
  // Immutable: read-only for app lifetime
  //------------------------------------------------------------------------
  struct Immutable {
    const EngineConfig* config = nullptr;
    const AssetRegistry* assets = nullptr;
    const ShaderCompilationDb* shaderDatabase = nullptr;
  };

  //! Default constructor initializing empty snapshot buffers.
  OXGN_NGIN_API FrameContext();

  //! Construct with immutable dependencies that live for application lifetime.
  OXGN_NGIN_API explicit FrameContext(const Immutable& imm);

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

  // Engine-only graphics backend management RATIONALE: Graphics backend
  // lifecycle is engine-managed; external modules should not modify the
  // graphics reference directly to avoid resource leaks
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
  // and ScenePrep. Lifetime managed externally; FrameContext only observes.
  // Scene is module-managed (not EngineState); no EngineTag required.
  OXGN_NGIN_API auto SetScene(observer_ptr<scene::Scene> s) noexcept -> void;

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
  OXGN_NGIN_API auto PublishSnapshots(EngineTag) noexcept -> UnifiedSnapshot&;

  //! Stage typed module data for inclusion in next snapshot
  template <IsTyped T> auto StageModuleData(T data) noexcept -> bool
  {
    // Allow staging during mutation phases, or during the Snapshot phase where
    // modules may contribute to the snapshot.
    using namespace oxygen::core::meta;
    using namespace oxygen::core;
    if (!PhaseCanMutateGameState(engine_state_.current_phase)
      && engine_state_.current_phase != PhaseId::kSnapshot) {
      return false;
    }

    std::unique_lock lock(staged_module_mutex_);
    const auto type_id = std::type_index(typeid(T));

    // Check for duplicates
    if (staged_module_data_.data_.contains(type_id)) {
      return false; // Duplicate staging not allowed
    }

    staged_module_data_.data_[type_id] = std::move(data);
    return true;
  }

  //! Get mutable facade for staging module data during mutation phases. The
  //! ModuleDataMutable only has non-mutating APIs, and can still be mutated
  //! only through `StageModuleData`.
  OXGN_NGIN_API auto GetStagingModuleData() noexcept -> ModuleDataMutable&;

  OXGN_NGIN_API auto SetInputSnapshot(
    std::shared_ptr<const InputSnapshot> inp, EngineTag) noexcept -> void;

  auto GetInputSnapshot() const noexcept
  {
    return std::atomic_load(&atomic_input_snapshot_);
  }

  auto GetViews() const noexcept -> std::span<const ViewInfo> { return views_; }

  // Coordinator-only view management with phase validation RATIONALE: Views
  // affect rendering setup and must be finalized during appropriate phases
  // (FrameStart/SceneMutation/FrameGraph) before parallel tasks begin
  OXGN_NGIN_API auto SetViews(std::vector<ViewInfo> v) noexcept -> void;

  // Add individual view with phase validation
  OXGN_NGIN_API auto AddView(const ViewInfo& view) noexcept -> void;

  // Clear views with phase validation
  OXGN_NGIN_API auto ClearViews() noexcept -> void;

  OXGN_NGIN_API auto SetCurrentPhase(core::PhaseId p, EngineTag) noexcept
    -> void;

  auto GetCurrentPhase() const noexcept { return engine_state_.current_phase; }

  // Public lightweight timing and surface lists used by subsystems
  struct FrameTiming {
    std::chrono::microseconds frameDuration { 0 };
    std::chrono::microseconds cpuTime { 0 };
    std::chrono::microseconds gpuTime { 0 };
  };

  // Minimal budget stats used by PhaseBudgetAdapt
  struct BudgetStats {
    std::chrono::milliseconds cpuBudget { 0 };
    std::chrono::milliseconds gpuBudget { 0 };
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
  OXGN_NGIN_API auto SetFrameTiming(const FrameTiming& t, EngineTag) noexcept
    -> void;

  auto GetFrameTiming() const noexcept { return metrics_.timing; }

  // Engine-only: set/get the recorded frame start time (used for snapshot
  // coordination). Setter requires EngineTag to prevent accidental updates from
  // modules.
  OXGN_NGIN_API auto SetFrameStartTime(
    std::chrono::steady_clock::time_point t, EngineTag) noexcept -> void;

  auto GetFrameStartTime() const noexcept { return frame_start_time_; }

  // Engine-only budget statistics for adaptive scheduling RATIONALE: Budget
  // management is part of engine performance control and should not be modified
  // by application modules directly
  OXGN_NGIN_API auto SetBudgetStats(
    const BudgetStats& stats, EngineTag) noexcept -> void;

  auto GetBudgetStats() const noexcept { return metrics_.budget; }

  // Combined metrics access for unified performance monitoring RATIONALE:
  // Provides consolidated access to all performance-related data for monitoring
  // and adaptive scheduling decisions
  OXGN_NGIN_API auto SetMetrics(const Metrics& metrics, EngineTag) noexcept
    -> void;

  auto GetMetrics() const noexcept { return metrics_; }

  // Return a thread-safe copy of the surface list. Coordinator callers may
  // prefer to call AddSurface/RemoveSurfaceAt/ClearSurfaces instead of mutating
  // the vector directly. RATIONALE: Surface list access is always safe via
  // copy, but direct modification requires phase validation to ensure snapshot
  // consistency.
  auto GetSurfaces() const noexcept
    -> std::vector<std::shared_ptr<graphics::Surface>>
  {
    std::shared_lock lock(surfaces_mutex_);
    return surfaces_;
  }

  // Coordinator-safe surface mutation APIs. These acquire the snapshot lock and
  // update the list; game modules should use these during ordered phases
  // (FrameStart / SceneMutation) only. PHASE RESTRICTION: Surface modifications
  // are only allowed during early setup phases when the frame structure is
  // being established.
  OXGN_NGIN_API auto AddSurface(
    const std::shared_ptr<graphics::Surface>& s) noexcept -> void;

  // TODO: think if we want to allow removing surfaces directly using the
  // original pointer, or if we want to manage surface changesets and only allow
  // removal by index.
  OXGN_NGIN_API auto RemoveSurfaceAt(size_t index) noexcept -> bool;

  OXGN_NGIN_API auto ClearSurfaces(EngineTag) noexcept -> void;

  // Engine-only surface management for internal operations RATIONALE: Some
  // surface operations (like swapchain recreation) are engine-internal and
  // should bypass normal phase restrictions
  OXGN_NGIN_API auto SetSurfaces(
    std::vector<std::shared_ptr<graphics::Surface>> surfaces,
    EngineTag) noexcept -> void;

  OXGN_NGIN_API auto SetSurfacePresentable(
    size_t index, bool presentable) noexcept -> void;

  OXGN_NGIN_API auto IsSurfacePresentable(size_t index) const noexcept -> bool;

  auto GetPresentableFlags() const noexcept -> std::vector<uint8_t>
  {
    return presentable_flags_;
  }

  OXGN_NGIN_API auto GetPresentableSurfaces() const noexcept
    -> std::vector<std::shared_ptr<graphics::Surface>>;

  OXGN_NGIN_API auto ClearPresentableFlags(EngineTag) noexcept -> void;

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

   ### Performance Characteristics

   - Time Complexity: O(1) for insertion
   - Memory: Allocates string storage for message
   - Optimization: Thread-safe using shared_mutex

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

  //! Report an error using a TypeId directly
  /*!
   Reports an error to the frame context using the specified TypeId as the
   source. This is useful when reporting errors on behalf of other objects.

   @param source_type_id The TypeId of the source that caused the error
   @param message Descriptive error message
   @param source_key Optional unique identifier for the error source

   @see HasErrors, GetErrors, ClearErrorsFromSource
  */
  OXGN_NGIN_API auto ReportError(TypeId source_type_id, std::string message,
    std::optional<std::string> source_key = std::nullopt) noexcept -> void;

  //! Check if any errors have been reported this frame.
  /*!
   Returns true if any module has reported errors during the current frame.

   @return True if errors exist, false otherwise
   @note Thread-safe for concurrent access
   @see GetErrors, ReportError
  */
  OXGN_NGIN_NDAPI [[nodiscard]] auto HasErrors() const noexcept -> bool;

  //! Get a thread-safe copy of all reported errors.
  /*!
   Returns a copy of all errors reported during the current frame. Safe for
   concurrent access and processing.

   @return Vector containing copies of all frame errors
   @note Thread-safe via copy, no live references to internal data
   @see HasErrors, ClearErrors
  */
  OXGN_NGIN_NDAPI [[nodiscard]] auto GetErrors() const noexcept
    -> std::vector<FrameError>;

  //! Clear errors from a specific typed module source.
  /*!
   Removes all errors reported by the specified module type using compile-time
   type safety.

   @tparam SourceType Module type to clear errors from (must satisfy IsTyped)
   @note Thread-safe for concurrent access
   @see ClearErrorsFromSource(TypeId), ClearAllErrors
  */
  template <IsTyped SourceType> auto ClearErrorsFromSource() noexcept -> void
  {
    ClearErrorsFromSource(SourceType::ClassTypeId());
  }

  //! Clear errors from a specific module source by TypeId.
  /*!
   Removes all errors reported by the specified module type using runtime
   TypeId. Useful for ModuleManager when working with dynamic module
   collections.

   @param source_type_id TypeId of the module type to clear errors from
   @note Thread-safe for concurrent access
   @see ClearErrorsFromSource<SourceType>(), ClearAllErrors
  */
  OXGN_NGIN_API auto ClearErrorsFromSource(TypeId source_type_id) noexcept
    -> void;

  //! Clear errors from a specific module source by TypeId and source key.
  /*!
   Removes all errors reported by the specified module type that also match the
   given source key. Provides granular error clearing for cases where multiple
   modules of the same type exist.

   @param source_type_id TypeId of the module type to clear errors from
   @param source_key Optional source key to match for granular clearing
   @note Thread-safe for concurrent access
   @see ClearErrorsFromSource(TypeId), ClearAllErrors
  */
  OXGN_NGIN_API auto ClearErrorsFromSource(TypeId source_type_id,
    const std::optional<std::string>& source_key) noexcept -> void;

  //! Clear all reported errors.
  /*!
   Removes all errors reported during the current frame from all sources.
   Typically called at frame start to reset error state.

   @note Thread-safe for concurrent access
   @see ClearErrorsFromSource, HasErrors
  */
  OXGN_NGIN_API auto ClearAllErrors() noexcept -> void;

private:
  //------------------------------------------------------------------------
  // Private helper methods
  //------------------------------------------------------------------------
  // Create and populate both GameStateSnapshot and FrameSnapshot into target
  OXGN_NGIN_API auto CreateUnifiedSnapshot(
    UnifiedSnapshot& out, uint64_t version) noexcept -> void;

  // Populate FrameSnapshot within a GameStateSnapshot with coordination context
  // and views
  OXGN_NGIN_API auto PopulateFrameSnapshot(FrameSnapshot& frame_snapshot,
    const GameStateSnapshot& game_snapshot) const noexcept -> void;

  // (one-off buffer index computation is inlined at call site)

  // Populate the immutable GameStateSnapshot value (capture + convert +
  // version)
  OXGN_NGIN_API auto PopulateGameStateSnapshot(
    GameStateSnapshot& out, uint64_t version) noexcept -> void;

  //------------------------------------------------------------------------
  // Private data members with controlled access
  //------------------------------------------------------------------------

  frame::SequenceNumber frame_index_ { 0 };
  frame::Slot frame_slot_ { 0 };
  std::chrono::steady_clock::time_point frame_start_time_ {};

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
  std::vector<std::shared_ptr<graphics::Surface>> surfaces_;

  // Per-surface presentable flags (1:1 correspondence with surfaces vector)
  // uint8_t used for atomic operations and consistency with parallel workers.
  // Can be mutated until PhaseId::kPresent (not included).
  std::vector<uint8_t> presentable_flags_;

  // Active rendering views, in multi-view rendering. There is no 1:1 mapping
  // between views and surfaces. Can be mutated until the PhaseId::kSnapshot
  // phase (not included)
  std::vector<ViewInfo> views_;

  // Active scene (non-owning, may be null). Not part of GameData because the
  // high level scene is manipulated early in the frame render cycle, uses its
  // own optimized component storage, and is too different from what will be
  // snapshot and finally passed for rendering. Can be mutated until
  // PhaseId::KSceneMutation (not included).
  observer_ptr<scene::Scene> scene_ { nullptr }; // active scene (non-owning)

  // Replace the old coarse-grained snapshot_lock_ with clearer, fine-grained
  // locks for the containers that were previously protected by it. This
  // documents intent and reduces contention.
  mutable std::shared_mutex
    staged_module_mutex_; // protects staged_module_data_
  mutable std::shared_mutex
    surfaces_mutex_; // protects surfaces_ + presentable_flags_
  mutable std::shared_mutex views_mutex_; // protects views_
  std::array<UnifiedSnapshot, 2> snapshot_buffers_;
  // Visible snapshot index: _not_ atomic because only the engine thread writes
  // it during PublishSnapshots and workers never read it directly.
  uint32_t visible_snapshot_index_ { 0 };
  // Snapshot version monotonic counter. Not atomic for the same reason as
  // index: only written by engine thread and not read concurrently by workers.
  uint64_t snapshot_version_ { 0 };

  // Lock-free input snapshot pointer (written once per frame by coordinator)
  std::atomic<std::shared_ptr<const InputSnapshot>> atomic_input_snapshot_;

  // Error reporting system state
  mutable std::shared_mutex error_mutex_;
  std::vector<FrameError> frame_errors_;
};

} // namespace oxygen::engine
