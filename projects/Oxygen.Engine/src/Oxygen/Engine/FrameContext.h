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
// - Fail-Safe Defaults: Invalid operations are ignored rather than causing
// errors
// - Cross-Module Data: GameState contains authoritative data shared across
// modules
//
// ACCESS CONTROL MODEL:
// - EngineState: Engine-internal state (fences, profiler, graphics backend)
//   - Mutation: Requires EngineTag capability token
//   - Read access: Always allowed (atomic members are thread-safe)
// - GameState: Authoritative cross-module game/simulation data
//   - Mutation: Only during ordered phases (Category A in AsyncEngine model)
//   - Parallel access: Must use CreateSnapshot() for immutable data
//   - Content: Entity transforms, animation, particles, materials, physics, AI,
//   audio
// - Snapshots: Double-buffered immutable state for parallel consumption
//   - GameStateSnapshot: Heavy application data copied from GameState (data
//   storage layer)
//     * Immutable containers (vectors) that own the actual game data
//     * Thread-safe sharing via shared_ptr<const GameStateSnapshot>
//     * Double-buffered for lock-free parallel access during snapshot
//     transitions
//   - FrameSnapshot: Lightweight coordination context with efficient views into
//   data (access layer)
//     * Contains spans pointing into GameStateSnapshot data (no data ownership)
//     * Optimized for parallel task consumption with Structure-of-Arrays layout
//     * Different tasks receive different view combinations from same
//     underlying data
//   - Creation: Engine-only via PublishSnapshots(EngineTag)
//   - Access: Thread-safe, lock-free via GetGameStateSnapshot() (modules) and
//   GetFrameSnapshot() (tasks)
//
//
// PARALLEL TASK ARCHITECTURE:
// - Modules receive FrameContext and call GetGameStateSnapshot() for heavy game
// data
// - Parallel tasks call GetFrameSnapshot() for lightweight coordination context
// - Both access methods are lock-free and see atomically consistent data
// - FrameSnapshot provides spans into GameStateSnapshot data for cache-friendly
// iteration
//   * GameStateSnapshot owns the immutable data containers (vectors, etc.)
//   * FrameSnapshot provides non-owning views (spans) optimized for parallel
//   access
//   * Multiple view patterns from same data: culling (transforms+spatial),
//   animation (skeletons), etc.
// - Parallel tasks write to private output buffers, integrated after barrier
//
// PHASE EXECUTION MODEL:
// - Ordered Phases (A): Coordinator-only mutation of authoritative state
// - Parallel Phases (B): Snapshot-only access, no direct state mutation
// - Async Pipelines (C): Multi-frame coroutines with validation on completion
// - Detached Services (D): Background work with fire-and-forget semantics
//
// RATIONALE:
// This design prevents race conditions, ensures deterministic execution,
// and provides clear ownership boundaries between engine internals and
// application modules while maintaining high performance through lock-free
// snapshot distribution to parallel workers with efficient data layouts.
//------------------------------------------------------------------------------

#pragma once

#include <algorithm>
#include <array>
#include <atomic>
#include <cassert>
#include <chrono>
#include <cstdint>
#include <memory>
#include <mutex>
#include <optional>
#include <shared_mutex>
#include <span>
#include <string>
#include <vector>

#include <Oxygen/Base/Macros.h>
#include <Oxygen/Base/ObserverPtr.h>
#include <Oxygen/Composition/Typed.h>
#include <Oxygen/Core/PhaseRegistry.h>
#include <Oxygen/Core/Types/Frame.h>
#include <Oxygen/Engine/EngineTag.h>
#include <Oxygen/Graphics/Common/Surface.h>
#include <Oxygen/Renderer/Types/View.h>

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
 human-readable message. Used for basic error propagation from modules
 to the engine frame loop without exceptions.

 ### Key Features

 - **Typed Source**: Uses TypeId for compile-time and runtime type safety
 - **Simple Message**: Human-readable error description

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

//===-------------------------------------------------------------------------//

//! Draw batch for render graph execution
/*!
 Represents a single draw call with all necessary parameters.
 Used for batching and organizing draw calls in the render graph.
 */
// struct DrawBatch {
//   uint32_t index { 0 }; //!< Draw index for bindless access
//   uint32_t index_count { 0 }; //!< Number of indices to draw
//   uint32_t instance_count { 1 }; //!< Number of instances to draw
//   uint32_t start_index { 0 }; //!< First index location
//   int32_t base_vertex { 0 }; //!< Value added to vertex index
//   uint32_t start_instance { 0 }; //!< First instance location
//   uint32_t material_index { 0 }; //!< Material index for this batch
//   uint32_t mesh_index { 0 }; //!< Mesh index for this batch
// };

//! Light data for scene illumination
/*!
 Represents lighting information for scene rendering.
 Used for various light types including directional, point, and spot lights.
 */
// struct LightData {
//   uint32_t light_type { 0 }; //!< Type of light (directional, point, spot,
//   etc.) float intensity { 1.0f }; //!< Light intensity multiplier float
//   color[3] { 1.0f, 1.0f, 1.0f }; //!< RGB color values float position[3] {
//   0.0f, 0.0f,
//     0.0f }; //!< Light position (for point/spot lights)
//   float direction[3] { 0.0f, -1.0f,
//     0.0f }; //!< Light direction (for directional/spot)
//   float range { 10.0f }; //!< Light range (for point/spot lights)
//   float spot_angle_inner { 30.0f }; //!< Inner cone angle (for spot lights)
//   float spot_angle_outer { 45.0f }; //!< Outer cone angle (for spot lights)
//   uint32_t shadow_enabled { 0 }; //!< Whether shadows are enabled for this
//   light uint32_t padding[3] { 0, 0, 0 }; //!< Padding for alignment
// };

// Forward declarations for parallel task data
// struct BoundingBox {
//   std::array<float, 3> min_bounds;
//   std::array<float, 3> max_bounds;
//   std::array<float, 3> center;
//   float radius;
//   bool is_valid;
//   uint32_t primitive_count;
//   uint32_t entity_id;
//   float surface_area;
// };
// struct EntityId;
// struct BoneMatrix {
//   uint32_t bone_id;
//   std::array<std::array<float, 4>, 4>
//     transform_matrix; // 4x4 transformation matrix
//   std::array<float, 3> translation;
//   std::array<float, 4> rotation; // quaternion
//   std::array<float, 3> scale;
// };
// struct SkeletonHierarchy;
// struct AnimationState {
//   uint32_t skeleton_id;
//   uint32_t current_clip_id;
//   float current_time;
//   float duration;
//   bool is_looping;
//   bool is_playing;
//   float blend_weight;
//   uint32_t next_clip_id;
//   float blend_duration;
//   float blend_time;
//   std::array<float, 3> root_motion_translation;
//   std::array<float, 4> root_motion_rotation; // quaternion
//   std::array<float, 3> root_motion_scale;
// };
// struct ParticleEmitterState {
//   uint32_t emitter_id;
//   std::array<float, 3> position;
//   std::array<float, 3> velocity;
//   float emission_rate;
//   float particle_lifetime;
//   std::array<float, 3> particle_color;
//   float particle_size;
//   uint32_t max_particles;
//   bool active;
// };
// struct ParticleSystemConfig;
// struct BVHTree;
// struct SpatialIndex;
// struct CullingVolume;
// struct MaterialProperties {
//   uint32_t material_id;
//   float roughness;
//   float metallic;
//   float specular;
//   float opacity;
//   std::array<float, 3> albedo;
//   std::array<float, 3> emissive;
//   std::array<float, 3> normal_scale;
// };
// using TextureHandle = ResourceHandle;
// using ShaderHandle = ResourceHandle;
// struct EntityTransform {
//   uint32_t entity_id;
//   std::array<float, 3> position;
//   std::array<float, 4> rotation; // quaternion
//   std::array<float, 3> scale;
//   std::array<std::array<float, 4>, 4> world_matrix; // 4x4 transformation
//   matrix bool is_dirty; uint32_t parent_id; uint32_t hierarchy_depth;
// };

//! Navigation mesh for AI pathfinding and navigation
/*!
 Represents a navigation mesh used for AI pathfinding, navmesh queries,
 and spatial navigation in the game world.
 */
// struct NavigationMesh {
//   uint32_t mesh_id { 0 }; //!< Unique identifier for this nav mesh
//   std::vector<std::array<float, 3>> vertices; //!< Mesh vertex positions
//   std::vector<std::array<uint32_t, 3>> triangles; //!< Triangle indices (3
//   per triangle) std::vector<std::array<uint32_t, 3>> adjacency; //!< Adjacent
//   triangle indices for each triangle std::vector<float> area_costs; //!<
//   Movement cost per triangle area std::vector<uint32_t> area_types; //!< Area
//   type flags (walkable, water, etc.) std::array<float, 6> bounds; //!<
//   Bounding box: [min_x, min_y, min_z, max_x, max_y, max_z] float agent_radius
//   { 0.5f }; //!< Agent radius this mesh was built for float agent_height
//   { 2.0f }; //!< Agent height this mesh was built for float max_slope { 45.0f
//   }; //!< Maximum walkable slope in degrees uint32_t generation { 0 }; //!<
//   Version/generation for cache invalidation bool is_valid { false }; //!<
//   Whether this nav mesh is valid for queries
// };

//! Lightweight per-frame snapshot passed to parallel tasks.
//! Contains engine-level coordination data and efficient read-only views into
//! heavy data structures organized for parallel task consumption (SoA layout,
//! cache-friendly iteration). Used by parallel tasks that need coordination
//! context and efficient views into game data.
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

  // Core immutable data views for parallel tasks (from PlantUML spec)
  // Organized as Structure-of-Arrays for efficient parallel iteration
  // struct TransformData {
  //   std::span<const EntityTransform> world_transforms;
  //   std::span<const BoundingBox> bounding_boxes;
  //   std::span<const EntityId> entity_ids;
  // } transforms;

  // struct SkeletonData {
  //   std::span<const BoneMatrix> bone_matrices;
  //   std::span<const SkeletonHierarchy> hierarchies;
  //   std::span<const AnimationState> animation_states;
  // } skeletons;

  // struct ParticleEmitterData {
  //   std::span<const ParticleEmitterState> emitter_states;
  //   std::span<const Matrix4x4> emitter_transforms;
  //   std::span<const ParticleSystemConfig> configs;
  // } particle_emitters;

  // struct SpatialData {
  //   // Scene BVH / Spatial Indices for culling tasks
  //   observer_ptr<const BVHTree> scene_bvh;
  //   observer_ptr<const SpatialIndex> spatial_index;
  //   std::span<const CullingVolume> culling_volumes;
  // } spatial;

  // struct MaterialData {
  //   // Static portion of material parameters for uniform prep tasks
  //   std::span<const MaterialProperties> static_properties;
  //   std::span<const TextureHandle> texture_handles;
  //   std::span<const ShaderHandle> shader_handles;
  // } materials;

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
// Template-based common data structures to eliminate duplication
// between GameState and GameStateSnapshot
//------------------------------------------------------------------------------

// Policy classes for pointer mutability control
struct MutablePolicy {
  template <typename T> using Ptr = observer_ptr<T>;
};

struct ImmutablePolicy {
  template <typename T> using Ptr = observer_ptr<const T>;
};

// Common game data structure template with policy-based pointer types
template <typename PointerPolicy> struct GameDataCommon {
  // using BVHPtr = typename PointerPolicy::template Ptr<BVHTree>;
  // using SpatialIndexPtr = typename PointerPolicy::template Ptr<SpatialIndex>;
  // using NavMeshPtr = typename PointerPolicy::template Ptr<NavigationMesh>;
  // using PhysicsStatePtr =
  //   typename PointerPolicy::template Ptr<PhysicsWorldState>;

  // // Cross-module game data that parallel tasks need
  // struct SceneData {
  //   std::vector<EntityTransform>
  //     entity_transforms; // World transforms for all entities
  //   std::vector<BoundingBox> entity_bounds; // Bounding volumes for culling
  //   std::vector<EntityId> entity_ids; // Entity identifiers
  //   BVHPtr scene_bvh; // Scene spatial acceleration structure
  //   SpatialIndexPtr spatial_index; // Spatial indexing for queries

  //   // Create snapshot views for FrameSnapshot
  //   void PopulateSnapshot(FrameSnapshot::TransformData& transforms,
  //     FrameSnapshot::SpatialData& spatial) const noexcept
  //   {
  //     if (!entity_transforms.empty()) {
  //       transforms.world_transforms = std::span(entity_transforms);
  //       transforms.bounding_boxes = std::span(entity_bounds);
  //       transforms.entity_ids = std::span(entity_ids);
  //     }
  //     spatial.scene_bvh = scene_bvh;
  //     spatial.spatial_index = spatial_index;
  //   }
  // } scene;

  // struct AnimationData {
  //   std::vector<AnimationState> animation_states; // Current animation states
  //   std::vector<BoneMatrix> bone_matrices; // Skeletal pose data
  //   std::vector<SkeletonHierarchy> skeleton_hierarchies; // Bone
  //   relationships std::vector<float> blend_weights; // Animation blending
  //   weights

  //   // Create snapshot views for FrameSnapshot
  //   void PopulateSnapshot(FrameSnapshot::SkeletonData& skeletons) const
  //   noexcept
  //   {
  //     if (!bone_matrices.empty()) {
  //       skeletons.bone_matrices = std::span(bone_matrices);
  //       skeletons.hierarchies = std::span(skeleton_hierarchies);
  //       skeletons.animation_states = std::span(animation_states);
  //     }
  //   }
  // } animation;

  // struct ParticleData {
  //   std::vector<ParticleEmitterState>
  //     emitter_states; // Per-system particle state
  //   std::vector<ParticleSystemConfig>
  //     system_configs; // Particle system configurations
  //   std::vector<Matrix4x4> emitter_transforms; // Emitter world transforms

  //   // Create snapshot views for FrameSnapshot
  //   void PopulateSnapshot(
  //     FrameSnapshot::ParticleEmitterData& particles) const noexcept
  //   {
  //     if (!emitter_states.empty()) {
  //       particles.emitter_states = std::span(emitter_states);
  //       particles.emitter_transforms = std::span(emitter_transforms);
  //       particles.configs = std::span(system_configs);
  //     }
  //   }
  // } particles;

  // struct MaterialData {
  //   std::vector<MaterialProperties>
  //     material_properties; // Dynamic material parameters
  //   std::vector<TextureHandle> texture_bindings; // Texture resource handles
  //   std::vector<ShaderHandle> shader_bindings; // Shader resource handles

  //   // Create snapshot views for FrameSnapshot
  //   void PopulateSnapshot(FrameSnapshot::MaterialData& materials) const
  //   noexcept
  //   {
  //     if (!material_properties.empty()) {
  //       materials.static_properties = std::span(material_properties);
  //       materials.texture_handles = std::span(texture_bindings);
  //       materials.shader_handles = std::span(shader_bindings);
  //     }
  //   }
  // } materials;

  // struct PhysicsData {
  //   std::vector<Matrix4x4> rigid_body_transforms; // Physics object
  //   transforms std::vector<float> collision_shapes; // Collision geometry
  //   data PhysicsStatePtr physics_world_state; // Opaque physics world
  //   snapshot
  // } physics;

  // struct AIData {
  //   NavMeshPtr nav_mesh; // Navigation mesh for pathfinding
  //   std::vector<float> ai_perception_data; // Cached perception queries
  //   std::vector<EntityId> ai_entities; // Entities with AI components
  // } ai;

  // struct AudioData {
  //   std::vector<Matrix4x4> audio_source_transforms; // 3D audio source
  //   positions std::vector<float> audio_parameters; // Audio system parameters
  // } audio;

  // Copy assignment from another policy type (for snapshot creation)
  template <typename OtherPolicy>
  auto CopyFrom(const GameDataCommon<OtherPolicy>& other) -> void
  {
    // // Copy vector data (deep copy)
    // scene.entity_transforms = other.scene.entity_transforms;
    // scene.entity_bounds = other.scene.entity_bounds;
    // scene.entity_ids = other.scene.entity_ids;

    // animation.animation_states = other.animation.animation_states;
    // animation.bone_matrices = other.animation.bone_matrices;
    // animation.skeleton_hierarchies = other.animation.skeleton_hierarchies;
    // animation.blend_weights = other.animation.blend_weights;

    // particles.emitter_states = other.particles.emitter_states;
    // particles.system_configs = other.particles.system_configs;
    // particles.emitter_transforms = other.particles.emitter_transforms;

    // materials.material_properties = other.materials.material_properties;
    // materials.texture_bindings = other.materials.texture_bindings;
    // materials.shader_bindings = other.materials.shader_bindings;

    // physics.rigid_body_transforms = other.physics.rigid_body_transforms;
    // physics.collision_shapes = other.physics.collision_shapes;

    // ai.ai_perception_data = other.ai.ai_perception_data;
    // ai.ai_entities = other.ai.ai_entities;

    // audio.audio_source_transforms = other.audio.audio_source_transforms;
    // audio.audio_parameters = other.audio.audio_parameters;

    // // Convert pointer types for observer_ptr fields
    // if constexpr (std::is_same_v<PointerPolicy, ImmutablePolicy>) {
    //   scene.scene_bvh
    //     = observer_ptr<const BVHTree>(other.scene.scene_bvh.get());
    //   scene.spatial_index
    //     = observer_ptr<const SpatialIndex>(other.scene.spatial_index.get());
    //   physics.physics_world_state = observer_ptr<const PhysicsWorldState>(
    //     other.physics.physics_world_state.get());
    //   ai.nav_mesh = observer_ptr<const
    //   NavigationMesh>(other.ai.nav_mesh.get());
    // } else {
    //   scene.scene_bvh = other.scene.scene_bvh;
    //   scene.spatial_index = other.scene.spatial_index;
    //   physics.physics_world_state = other.physics.physics_world_state;
    //   ai.nav_mesh = other.ai.nav_mesh;
    // }
  }
};

// Type aliases for mutable and immutable game data
using GameDataMutable = GameDataCommon<MutablePolicy>;
using GameDataImmutable = GameDataCommon<ImmutablePolicy>;

//------------------------------------------------------------------------------

//! Minimal per-view description used by coordinator and parallel tasks.
/*!
 Contains view-specific rendering information including camera matrices
 and surface targets. Designed to be lightweight and copyable for
 efficient distribution to parallel workers.

 ### Key Features

 - **Lightweight**: Small, copyable structure for performance
 - **Type-Safe**: Opaque surface handles prevent accidental mixing
 - **Immutable**: View data frozen after FrameStart phase

 @note No 1:1 mapping between surfaces and views
 @see GameStateSnapshot::views, FrameContext::SetViews
*/
struct ViewInfo {
  std::string view_name; // TODO: consider adding this to View class
  std::optional<View> view; // view-specific camera/matrices
  // Typed opaque handle for surface/device-backed render targets. This
  // preserves the previous type-erasure but gives a distinct compile-time
  // alias so callers don't mix handles accidentally.
  struct SurfaceHandle {
    std::shared_ptr<void> ptr;
  };
  SurfaceHandle surface; // opaque surface/target handle
};
// Note: There is no 1:1 mapping between surfaces and views. A surface may
// have zero or many associated views. Each view references exactly one
// surface (or null). Surfaces and views are expected to be finalized by
// the coordinator during FrameStart and remain frozen afterwards.

//! Opaque input snapshot container for type-erased input data.
struct InputSnapshot {
  std::shared_ptr<const void> blob; // host-specific input snapshot
};

//! Read-only immutable snapshot of authoritative game state.
/*!
 Contains heavy application data that forms the DATA STORAGE LAYER.
 Owns actual game data containers and provides thread-safe access
 via shared_ptr. Used by modules needing access to heavy game/scene data.

 ### Key Features

 - **Data Ownership**: Contains actual game data vectors and containers
 - **Thread-Safe**: Immutable once published, safe for parallel access
 - **Versioned**: Monotonic version tracking for consistency validation
 - **Cross-Module**: Shared data accessible across all engine modules

 ### Architecture Notes

 This is the authoritative snapshot of all game state at a specific
 frame. GameStateSnapshot owns the data, while FrameSnapshot provides
 efficient views into this data for parallel task consumption.

 @see FrameSnapshot, FrameContext::GetGameStateSnapshot
*/
struct GameStateSnapshot {
  std::vector<ViewInfo> views; // per-view transforms & targets
  std::shared_ptr<const InputSnapshot> input; // input snapshot at capture time
  // std::vector<LightData> lights; // per-frame lights (copied)
  // std::vector<DrawBatch> drawBatches; // batches computed during build

  // Cross-module game data using immutable policy
  GameDataImmutable gameData;

  // Convenience accessors for backwards compatibility
  // const auto& scene() const { return gameData.scene; }
  // const auto& animation() const { return gameData.animation; }
  // const auto& particles() const { return gameData.particles; }
  // const auto& materials() const { return gameData.materials; }
  // const auto& physics() const { return gameData.physics; }
  // const auto& ai() const { return gameData.ai; }
  // const auto& audio() const { return gameData.audio; }

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

//! Central frame execution context with strict access control.
/*!
 FrameContext provides three logical groups of data used during frame
 execution: Immutable (app-lifetime read-only), EngineState (per-frame,
 mutated by engine/coordinator), and GameState (authoritative game data).

 ### Key Features

 - **Access Control**: Engine capability tokens restrict critical operations
 - **Phase Validation**: Operations restricted by execution phase
 - **Thread Safety**: Lock-free snapshot access for parallel workers
 - **Data Encapsulation**: Private state with controlled public interface

 ### Access Control Model

 - **EngineState**: Engine-internal state requiring EngineTag capability
 - **GameState**: Cross-module data with phase-dependent mutation rules
 - **Snapshots**: Immutable data published atomically for parallel access

 ### Usage Patterns

 ```cpp
 // Engine coordinator publishes state
 auto version = context.PublishSnapshots(engine_tag);

 // Modules access heavy data
 auto game_snapshot = context.GetGameStateSnapshot();

 // Parallel tasks access coordination context
 const auto* frame_snapshot = context.GetFrameSnapshot();
 ```

 ### Architecture Notes

 ATOMIC SNAPSHOT DESIGN: GameStateSnapshot and FrameSnapshot are kept
 separate for clean API boundaries but published together atomically using
 private unified storage. This maintains separation of concerns while
 ensuring consistent lock-free access.

 @warning Critical operations require EngineTag capability token
 @see GameStateSnapshot, FrameSnapshot, EngineTag
*/
//
// ATOMIC SNAPSHOT DESIGN: GameStateSnapshot and FrameSnapshot are kept separate
// for clean API boundaries but published together atomically using private
// unified storage. This maintains separation of concerns while ensuring
// consistent lock-free access.
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
  FrameContext()
  {
    // Initialize both unified snapshot slots
    snapshotBuffers_[0].gameSnapshot = std::make_shared<GameStateSnapshot>();
    snapshotBuffers_[1].gameSnapshot = std::make_shared<GameStateSnapshot>();
    visibleSnapshotIndex_.store(0u, std::memory_order_release);
    std::atomic_store(
      &atomicInputSnapshot_, std::shared_ptr<const InputSnapshot> {});
  }

  //! Construct with immutable dependencies that live for application lifetime.
  explicit FrameContext(const Immutable& imm)
    : immutable_(imm)
  {
    snapshotBuffers_[0].gameSnapshot = std::make_shared<GameStateSnapshot>();
    snapshotBuffers_[1].gameSnapshot = std::make_shared<GameStateSnapshot>();
    visibleSnapshotIndex_.store(0u, std::memory_order_release);
    std::atomic_store(
      &atomicInputSnapshot_, std::shared_ptr<const InputSnapshot> {});
  }

  OXYGEN_MAKE_NON_COPYABLE(FrameContext)
  OXYGEN_MAKE_NON_MOVABLE(FrameContext)

  ~FrameContext() = default;

  // Per-frame metadata (access via getters). Use engine-only Advance* to
  // move these values forward monotonically.

  //! Get the current frame index (monotonic counter).
  auto GetFrameSequenceNumber() const noexcept { return frameIndex_; }

  //! Get the current frame slot (for multi-buffered resources).
  auto GetFrameSlot() const noexcept { return frameSlot_; }

  //! Get the current epoch value (for resource lifecycle management).
  auto GetEpoch() const noexcept { return engine_state_.epoch; }

  auto SetFrameSequenceNumber(
    frame::SequenceNumber frameNumber, EngineTag) noexcept -> void
  {
    frameIndex_ = frameNumber;
  }

  //! Engine-only: Set the current frame slot. Requires EngineTag capability.
  auto SetFrameSlot(frame::Slot slot, EngineTag) noexcept -> void
  {
    frameSlot_ = slot;
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

  // Engine-only graphics backend management
  // RATIONALE: Graphics backend lifecycle is engine-managed; external modules
  // should not modify the graphics reference directly to avoid resource leaks
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
    return engine_state_.frameFenceValue.load(std::memory_order_acquire);
  }

  //! Engine-only: Update fence value after GPU submission.
  auto SetFrameFenceValue(uint64_t value, EngineTag) noexcept -> void
  {
    engine_state_.frameFenceValue.store(value, std::memory_order_release);
  }

  // Engine-only resource integration data management
  // RATIONALE: AsyncUploads lifecycle is tied to engine GPU scheduling
  auto SetAsyncUploads(
    observer_ptr<ResourceIntegrationData> uploads, EngineTag) noexcept -> void
  {
    engine_state_.asyncUploads = uploads;
  }

  auto GetAsyncUploads() const noexcept { return engine_state_.asyncUploads; }

  // Engine-only profiler management
  auto SetProfiler(observer_ptr<FrameProfiler> profiler, EngineTag) noexcept
    -> void
  {
    engine_state_.profiler = profiler;
  }

  auto GetProfiler() const noexcept { return engine_state_.profiler; }

  // Phase-aware GameState accessors
  // RATIONALE: GameState mutation is only allowed during ordered phases.
  // Parallel phases must use CreateSnapshot() to get immutable data.

  // TODO: really bad API - fix it
  // Entity command buffer access (coordinator phases only)
  auto GetEntityCommandBuffer() const noexcept -> EntityCommandBuffer*
  {
    // Entity command buffer should only be accessed during ordered phases
    // where structural changes are allowed
    if (engine_state_.currentPhase == core::PhaseId::kParallelTasks) {
      return nullptr; // Prevent access during parallel execution
    }
    return game_state_.entityCmds;
  }

  // Engine-only entity command buffer management
  auto SetEntityCommandBuffer(EntityCommandBuffer* cmds, EngineTag) noexcept
    -> void
  {
    AssertMutationAllowed();
    game_state_.entityCmds = cmds;
  }

  // // Light data access (snapshot recommended for parallel tasks)
  // auto GetLights() const noexcept -> std::span<const LightData>
  // {
  //   return game_state_.lights;
  // }

  // // Coordinator-only light data mutation
  // // RATIONALE: Light changes affect culling and must be finalized before
  // // parallel work
  // void SetLights(std::vector<LightData> lights) noexcept
  // {
  //   AssertMutationAllowed();
  //   game_state_.lights = std::move(lights);
  // }

  // void AddLight(const LightData& light) noexcept
  // {
  //   AssertMutationAllowed();
  //   game_state_.lights.push_back(light);
  // }

  // void ClearLights() noexcept
  // {
  //   AssertMutationAllowed();
  //   game_state_.lights.clear();
  // }

  // // Draw batch access (built during ordered phases, consumed by parallel
  // // recording)
  // auto GetDrawBatches() const noexcept -> std::span<const DrawBatch>
  // {
  //   return game_state_.drawBatches;
  // }

  // // Coordinator-only draw batch management
  // void SetDrawBatches(std::vector<DrawBatch> batches) noexcept
  // {
  //   AssertMutationAllowed();
  //   game_state_.drawBatches = std::move(batches);
  // }

  // void AddDrawBatch(const DrawBatch& batch) noexcept
  // {
  //   AssertMutationAllowed();
  //   game_state_.drawBatches.push_back(batch);
  // }

  // void ClearDrawBatches() noexcept
  // {
  //   AssertMutationAllowed();
  //   game_state_.drawBatches.clear();
  // }

  // User context handle access
  auto GetUserContext() const noexcept { return game_state_.userContext; }

  // Engine-only user context management
  auto SetUserContext(
    GameStateSnapshot::UserContextHandle context, EngineTag) noexcept -> void
  {
    AssertMutationAllowed();
    game_state_.userContext = std::move(context);
  }

  //------------------------------------------------------------------------
  // Cross-module game data accessors
  // RATIONALE: These provide controlled access to game data that is shared
  // across modules and phases, with proper phase validation.
  //------------------------------------------------------------------------

  // // Scene data access - transforms, bounds, spatial structures
  // auto GetEntityTransforms() const noexcept -> std::span<const
  // EntityTransform>
  // {
  //   return game_state_.gameData.scene.entity_transforms;
  // }

  // void SetEntityTransforms(std::vector<EntityTransform> transforms) noexcept
  // {
  //   AssertMutationAllowed();
  //   game_state_.gameData.scene.entity_transforms = std::move(transforms);
  // }

  // void AddEntityTransform(const EntityTransform& transform) noexcept
  // {
  //   AssertMutationAllowed();
  //   game_state_.gameData.scene.entity_transforms.push_back(transform);
  // }

  // auto GetEntityBounds() const noexcept -> std::span<const BoundingBox>
  // {
  //   return game_state_.gameData.scene.entity_bounds;
  // }

  // void SetEntityBounds(std::vector<BoundingBox> bounds) noexcept
  // {
  //   AssertMutationAllowed();
  //   game_state_.gameData.scene.entity_bounds = std::move(bounds);
  // }

  // // Spatial structures (BVH, spatial index) - engine managed
  // void SetSceneBVH(observer_ptr<BVHTree> bvh, EngineTag) noexcept
  // {
  //   game_state_.gameData.scene.scene_bvh = bvh;
  // }

  // auto GetSceneBVH() const noexcept
  // {
  //   return observer_ptr<const BVHTree>(
  //     game_state_.gameData.scene.scene_bvh.get());
  // }

  // // Animation data access
  // auto GetAnimationStates() const noexcept -> std::span<const AnimationState>
  // {
  //   return game_state_.gameData.animation.animation_states;
  // }

  // void SetAnimationStates(std::vector<AnimationState> states) noexcept
  // {
  //   AssertMutationAllowed();
  //   game_state_.gameData.animation.animation_states = std::move(states);
  // }

  // auto GetBoneMatrices() const noexcept -> std::span<const BoneMatrix>
  // {
  //   return game_state_.gameData.animation.bone_matrices;
  // }

  // void SetBoneMatrices(std::vector<BoneMatrix> matrices) noexcept
  // {
  //   AssertMutationAllowed();
  //   game_state_.gameData.animation.bone_matrices = std::move(matrices);
  // }

  // // Particle system data access
  // auto GetParticleEmitterStates() const noexcept
  //   -> std::span<const ParticleEmitterState>
  // {
  //   return game_state_.gameData.particles.emitter_states;
  // }

  // void SetParticleEmitterStates(
  //   std::vector<ParticleEmitterState> states) noexcept
  // {
  //   AssertMutationAllowed();
  //   game_state_.gameData.particles.emitter_states = std::move(states);
  // }

  // // Material data access
  // auto GetMaterialProperties() const noexcept
  //   -> std::span<const MaterialProperties>
  // {
  //   return game_state_.gameData.materials.material_properties;
  // }

  // void SetMaterialProperties(
  //   std::vector<MaterialProperties> properties) noexcept
  // {
  //   AssertMutationAllowed();
  //   game_state_.gameData.materials.material_properties =
  //   std::move(properties);
  // }

  // // Physics data access - engine managed for thread safety
  // void SetPhysicsWorldState(
  //   observer_ptr<PhysicsWorldState> world_state, EngineTag) noexcept
  // {
  //   game_state_.gameData.physics.physics_world_state = world_state;
  // }

  // auto GetPhysicsWorldState() const noexcept
  // {
  //   return observer_ptr<const PhysicsWorldState>(
  //     game_state_.gameData.physics.physics_world_state.get());
  // }

  // // AI/Navigation data access
  // void SetNavigationMesh(
  //   observer_ptr<NavigationMesh> nav_mesh, EngineTag) noexcept
  // {
  //   game_state_.gameData.ai.nav_mesh = nav_mesh;
  // }

  // auto GetNavigationMesh() const noexcept
  // {
  //   return observer_ptr<const NavigationMesh>(
  //     game_state_.gameData.ai.nav_mesh.get());
  // }

  // Engine-only thread pool management
  // RATIONALE: Thread pool lifecycle is engine-managed to ensure proper
  // shutdown sequencing and worker thread coordination
  auto SetThreadPool(co::ThreadPool* pool, EngineTag) noexcept -> void
  {
    engine_state_.threadPool = observer_ptr { pool };
  }

  auto GetThreadPool() const noexcept { return engine_state_.threadPool; }

  auto SetRenderGraphBuilder(
    observer_ptr<RenderGraphBuilder> builder, EngineTag) noexcept -> void
  {
    engine_state_.graph_builder_ = std::move(builder);
  }

  auto GetRenderGraphBuilder() const noexcept -> auto&
  {
    return engine_state_.graph_builder_;
  }

  //! Lock-free access to GameStateSnapshot for modules.
  /*!
   Returns the current published GameStateSnapshot containing heavy
   application data. This is the DATA STORAGE LAYER that owns actual
   game data containers.

   ### Usage Examples

   ```cpp
   auto snapshot = context.GetGameStateSnapshot();
   for (const auto& light : snapshot->lights) {
     // Process light data
   }
   ```

   @return Shared pointer to immutable game state snapshot
   @note Thread-safe for parallel access
   @see GetFrameSnapshot, PublishSnapshots
  */
  auto GetGameStateSnapshot() const noexcept
  {
    uint32_t idx = visibleSnapshotIndex_.load(std::memory_order_acquire);
    return snapshotBuffers_[static_cast<size_t>(idx)].gameSnapshot;
  }

  //! Lock-free access to FrameSnapshot for parallel tasks.
  /*!
   Returns the current FrameSnapshot containing lightweight coordination
   context and efficient views into game data. This is the ACCESS LAYER
   optimized for parallel task consumption.

   ### Usage Examples

   ```cpp
   const auto* frame = context.GetFrameSnapshot();
   for (const auto& transform : frame->transforms.world_transforms) {
     // Process transform data in parallel task
   }
   ```

   @return Pointer to immutable frame coordination snapshot
   @note Thread-safe for parallel access, optimized for SoA iteration
   @see GetGameStateSnapshot, PublishSnapshots
  */
  auto GetFrameSnapshot() const noexcept -> auto*
  {
    uint32_t idx = visibleSnapshotIndex_.load(std::memory_order_acquire);
    return &snapshotBuffers_[static_cast<size_t>(idx)].frameSnapshot;
  }

  //! Engine-only: Publish current game state as atomic snapshots.
  /*!
   Publishes the current authoritative GameState into the next snapshot
   buffer, assigns a new monotonic version, and atomically flips the
   visible index for lock-free readers. This creates both GameStateSnapshot
   and FrameSnapshot atomically.

   ### Performance Characteristics

   - Time Complexity: O(n) where n is game data size
   - Memory: Allocates new snapshot data
   - Optimization: Double-buffered to minimize lock contention

   ### Usage Examples

   ```cpp
   // Engine coordinator publishes state after mutations
   auto version = context.PublishSnapshots(engine_tag);
   // Parallel workers can now access consistent snapshot
   ```

   @param tag EngineTag capability token (engine-only access)
   @return New monotonic snapshot version number
   @note Only engine coordinator should call this method
   @warning Must be called during appropriate execution phases
   @see GetGameStateSnapshot, GetFrameSnapshot
  */
  auto PublishSnapshots(EngineTag) noexcept -> uint64_t;

  // Input helpers: PhaseInput should call SetInputSnapshot() during Input phase
  // to publish the input snapshot that subsequent phases and parallel tasks
  // will observe via CreateSnapshot().
  // Engine-only: only engine internals with an EngineTag may publish the
  // input snapshot. Callers should obtain an EngineTag from
  // internal::EngineTagFactory::Get() when called from engine code.
  auto SetInputSnapshot(
    std::shared_ptr<const InputSnapshot> inp, EngineTag) noexcept -> void
  {
    // Coordinator-only: publish the input snapshot atomically for readers.
    std::atomic_store(&atomicInputSnapshot_, std::move(inp));
  }

  auto GetInputSnapshot() const noexcept
  {
    return std::atomic_load(&atomicInputSnapshot_);
  }

  // Return the most recently published snapshot version. 0 means "no
  // snapshot published yet".
  auto GetPublishedSnapshotVersion() const noexcept
  {
    return snapshotVersion_.load(std::memory_order_acquire);
  }

  //----------------------------------------------------------------------
  // View helpers
  // Coordinator should populate per-frame views before SnapshotBuild.
  // ACCESS CONTROL: View management is phase-dependent and requires proper
  // ordering to ensure snapshot consistency.
  //----------------------------------------------------------------------

  // Phase-aware view access
  // RATIONALE: Views must be stable during snapshot creation and parallel work.
  // Direct access is restricted based on current execution phase.
  auto GetViews() const noexcept -> std::span<const ViewInfo>
  {
    return game_state_.views;
  }

  // Coordinator-only view management with phase validation
  // RATIONALE: Views affect rendering setup and must be finalized during
  // appropriate phases (FrameStart/SceneMutation/FrameGraph) before parallel
  // tasks begin
  auto SetViews(std::vector<ViewInfo> v) noexcept -> void
  {
    std::unique_lock lock(snapshotLock_);
    // PHASE RESTRICTION: Views can only be modified during early ordered phases
    // when scene structure is being established, not during parallel execution
    if (!IsViewMutationAllowed()) {
      // TODO: Replace with project-specific assertion/logging
      // Example: LOG_ERROR("View mutation attempted during invalid phase: {}",
      // GetCurrentPhase()); For now, assert in debug builds to catch violations
      // during development
#if !defined(NDEBUG)
      // This is an instrumentation point for detecting phase violations
      // In debug builds, this should trigger to help identify timing issues
      assert(false
        && "SetViews() called during invalid phase - views must be set before "
           "CommandRecord phase");
#endif
      return; // Still return to avoid crash in release builds
    }
    game_state_.views = std::move(v);
  }

  // Add individual view with phase validation
  auto AddView(const ViewInfo& view) noexcept -> void
  {
    std::unique_lock lock(snapshotLock_);
    if (!IsViewMutationAllowed()) {
      // TODO: Replace with project-specific assertion/logging
      // Example: LOG_ERROR("View mutation attempted during invalid phase: {}",
      // GetCurrentPhase());
#if !defined(NDEBUG)
      assert(false
        && "AddView() called during invalid phase - views must be added before "
           "CommandRecord phase");
#endif
      return;
    }
    game_state_.views.push_back(view);
  }

  // Clear views with phase validation
  auto ClearViews() noexcept -> void
  {
    std::unique_lock lock(snapshotLock_);
    if (!IsViewMutationAllowed()) {
      // TODO: Replace with project-specific assertion/logging
      // Example: LOG_ERROR("View mutation attempted during invalid phase: {}",
      // GetCurrentPhase());
#if !defined(NDEBUG)
      assert(false
        && "ClearViews() called during invalid phase - views must be cleared "
           "before CommandRecord phase");
#endif
      return;
    }
    game_state_.views.clear();
  }

  //------------------------------------------------------------------------
  // Phase tracking and validation helpers (debugging aid)
  // ACCESS CONTROL: Phase management is engine-only to ensure proper
  // coordination and prevent external modules from bypassing safety barriers.
  //------------------------------------------------------------------------

  auto SetCurrentPhase(core::PhaseId p, EngineTag) noexcept -> void
  {
    engine_state_.currentPhase = p;
  }

  auto GetCurrentPhase() const noexcept { return engine_state_.currentPhase; }

private:
  //------------------------------------------------------------------------
  // Private helper methods
  //------------------------------------------------------------------------

  // Common phase validation for view and surface mutations
  // RATIONALE: Centralizes the phase validation logic to ensure consistency
  // and reduce code duplication across multiple mutation methods
  auto IsStructuralMutationAllowed() const noexcept -> bool
  {
    return engine_state_.currentPhase == core::PhaseId::kFrameStart
      || engine_state_.currentPhase == core::PhaseId::kSceneMutation;
  }

  // View-specific mutation validation allowing up to FrameGraph phase
  // RATIONALE: Views must be available before render graph compilation starts,
  // so they can be defined during FrameGraph phase when modules contribute
  // passes
  auto IsViewMutationAllowed() const noexcept -> bool
  {
    return engine_state_.currentPhase == core::PhaseId::kFrameStart
      || engine_state_.currentPhase == core::PhaseId::kSceneMutation
      || engine_state_.currentPhase == core::PhaseId::kFrameGraph;
  }

  // Debug-time assertion helpers that validate whether the current phase is
  // permitted to mutate the requested state layer. These use the centralized
  // `PhaseRegistry` metadata predicates so validation stays consistent with
  // the canonical registry. In release builds these are no-ops.
  auto AssertMutationAllowed() const noexcept -> void
  {
#if !defined(NDEBUG)
    using namespace oxygen::core::meta;
    const auto p = engine_state_.currentPhase;
    // Require that the phase can mutate either GameState or FrameState or
    // EngineState depending on the callsite expectations. This generic
    // assertion is used by APIs that accept multiple mutation targets.
    assert(PhaseCanMutateGameState(p) || PhaseCanMutateFrameState(p)
      || PhaseCanMutateEngineState(p));
#endif
  }

  auto AssertEngineStateMutationAllowed() const noexcept -> void
  {
#if !defined(NDEBUG)
    using namespace oxygen::core::meta;
    assert(PhaseCanMutateEngineState(engine_state_.currentPhase));
#endif
  }

  auto AssertGameStateMutationAllowed() const noexcept -> void
  {
#if !defined(NDEBUG)
    using namespace oxygen::core::meta;
    assert(PhaseCanMutateGameState(engine_state_.currentPhase));
#endif
  }

public:
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
  // EngineTag to make accidental external mutation harder.
  // RATIONALE: Frame timing affects adaptive scheduling and budget decisions
  // that must be coordinated by the engine to maintain frame rate targets
  auto SetFrameTiming(const FrameTiming& t, EngineTag) noexcept -> void
  {
    AssertEngineStateMutationAllowed();
    metrics_.timing = t;
  }

  auto GetFrameTiming() const noexcept { return metrics_.timing; }

  // Engine-only: set/get the recorded frame start time (used for snapshot
  // coordination). Setter requires EngineTag to prevent accidental updates
  // from modules.
  auto SetFrameStartTime(
    std::chrono::steady_clock::time_point t, EngineTag) noexcept -> void
  {
    AssertEngineStateMutationAllowed();
    frameStartTime_ = t;
  }

  auto GetFrameStartTime() const noexcept { return frameStartTime_; }

  // Engine-only budget statistics for adaptive scheduling
  // RATIONALE: Budget management is part of engine performance control
  // and should not be modified by application modules directly
  auto SetBudgetStats(const BudgetStats& stats, EngineTag) noexcept -> void
  {
    AssertEngineStateMutationAllowed();
    metrics_.budget = stats;
  }

  auto GetBudgetStats() const noexcept { return metrics_.budget; }

  // Combined metrics access for unified performance monitoring
  // RATIONALE: Provides consolidated access to all performance-related data
  // for monitoring and adaptive scheduling decisions
  auto SetMetrics(const Metrics& metrics, EngineTag) noexcept -> void
  {
    AssertEngineStateMutationAllowed();
    metrics_ = metrics;
  }

  auto GetMetrics() const noexcept { return metrics_; }

  // Return a thread-safe copy of the surface list. Coordinator callers
  // may prefer to call AddSurface/RemoveSurfaceAt/ClearSurfaces instead
  // of mutating the vector directly.
  // RATIONALE: Surface list access is always safe via copy, but direct
  // modification requires phase validation to ensure snapshot consistency.
  auto GetSurfaces() const noexcept
    -> std::vector<std::shared_ptr<graphics::Surface>>
  {
    std::shared_lock lock(snapshotLock_);
    return engine_state_.surfaces;
  }

  // Coordinator-safe surface mutation APIs. These acquire the snapshot
  // lock and update the list; game modules should use these during
  // ordered phases (FrameStart / SceneMutation) only.
  // PHASE RESTRICTION: Surface modifications are only allowed during early
  // setup phases when the frame structure is being established.
  auto AddSurface(const std::shared_ptr<graphics::Surface>& s) noexcept -> void
  {
    std::unique_lock lock(snapshotLock_);
    // RATIONALE: Surfaces must be stable before parallel work begins to
    // ensure consistent command recording and resource allocation
    if (!IsStructuralMutationAllowed()) {
      return; // Silently ignore invalid phase modifications
    }
    engine_state_.surfaces.push_back(s);
    // Keep presentable flags in sync - new surfaces start as not presentable
    engine_state_.presentable_flags.push_back(0);
  }

  auto RemoveSurfaceAt(size_t index) noexcept
  {
    std::unique_lock lock(snapshotLock_);
    if (!IsStructuralMutationAllowed()) {
      return false; // Phase validation failed
    }
    if (index >= engine_state_.surfaces.size()) {
      return false; // Index out of bounds
    }
    engine_state_.surfaces.erase(
      engine_state_.surfaces.begin() + static_cast<std::ptrdiff_t>(index));
    // Keep presentable flags in sync
    if (index < engine_state_.presentable_flags.size()) {
      engine_state_.presentable_flags.erase(
        engine_state_.presentable_flags.begin()
        + static_cast<std::ptrdiff_t>(index));
    }
    return true;
  }

  auto ClearSurfaces(EngineTag) noexcept -> void
  {
    std::unique_lock lock(snapshotLock_);
    if (!IsStructuralMutationAllowed()) {
      return; // Phase validation failed
    }
    engine_state_.surfaces.clear();
    // Keep presentable flags in sync
    engine_state_.presentable_flags.clear();
  }

  // Engine-only surface management for internal operations
  // RATIONALE: Some surface operations (like swapchain recreation) are
  // engine-internal and should bypass normal phase restrictions
  auto SetSurfaces(std::vector<std::shared_ptr<graphics::Surface>> surfaces,
    EngineTag) noexcept -> void
  {
    std::unique_lock lock(snapshotLock_);
    engine_state_.surfaces = std::move(surfaces);
    // Reset presentable flags to match new surface count
    engine_state_.presentable_flags.assign(engine_state_.surfaces.size(), 0);
  }

  //------------------------------------------------------------------------
  // Surface presentable flag management
  // RATIONALE: Modules can mark surfaces as ready for presentation during
  // render graph assembly and command recording phases. The engine queries
  // these flags during the Present phase to determine which surfaces to
  // present.
  //------------------------------------------------------------------------

  //! Mark a surface as presentable or not presentable.
  /*!
   Sets the presentable flag for the surface at the specified index.
   Modules should call this during FrameGraph or CommandRecord phases
   to indicate when their rendering work is complete and the surface
   is ready for presentation.

   ### Usage Examples

   ```cpp
   // Mark surface 0 as ready for presentation
   context.SetSurfacePresentable(0, true);

   // Mark surface 1 as not ready (e.g., rendering failed)
   context.SetSurfacePresentable(1, false);
   ```

   @param index Surface index (must be < GetSurfaces().size())
   @param presentable True to mark as presentable, false otherwise
   @note Thread-safe for concurrent access during parallel phases
   @see IsSurfacePresentable, GetPresentableSurfaces
  */
  auto SetSurfacePresentable(size_t index, bool presentable) noexcept -> void
  {
    std::shared_lock lock(snapshotLock_);
    // Allow flag setting during later phases when rendering work completes
    if (index >= engine_state_.surfaces.size()
      || index >= engine_state_.presentable_flags.size()) {
      return; // Index out of bounds - silently ignore
    }

    // Use atomic store for thread-safe access during parallel phases
    std::atomic_ref<uint8_t> flag_ref(engine_state_.presentable_flags[index]);
    flag_ref.store(presentable ? 1u : 0u, std::memory_order_release);
  }

  //! Check if a surface is marked as presentable.
  /*!
   Returns whether the surface at the specified index is marked as
   ready for presentation.

   ### Usage Examples

   ```cpp
   if (context.IsSurfacePresentable(0)) {
     // Surface 0 is ready for presentation
   }
   ```

   @param index Surface index (must be < GetSurfaces().size())
   @return True if surface is presentable, false otherwise or if index invalid
   @note Thread-safe for concurrent access
   @see SetSurfacePresentable, GetPresentableSurfaces
  */
  auto IsSurfacePresentable(size_t index) const noexcept -> bool
  {
    std::shared_lock lock(snapshotLock_);
    if (index >= engine_state_.presentable_flags.size()) {
      return false; // Index out of bounds
    }

    // Use atomic load for thread-safe access
    std::atomic_ref<const uint8_t> flag_ref(
      engine_state_.presentable_flags[index]);
    return flag_ref.load(std::memory_order_acquire) != 0;
  }

  //! Get a thread-safe copy of the presentable flags.
  /*!
   Returns a copy of the current presentable flags vector for inspection.
   Useful for debugging or when you need to process multiple flags atomically.

   @return Copy of presentable flags (1:1 correspondence with surfaces)
   @note Thread-safe via copy
   @see GetPresentableSurfaces for a more convenient filtered view
  */
  auto GetPresentableFlags() const noexcept -> std::vector<uint8_t>
  {
    std::shared_lock lock(snapshotLock_);
    return engine_state_.presentable_flags;
  }

  //! Get a filtered view of only presentable surfaces.
  /*!
   Returns a vector containing only the surfaces that are marked as
   presentable. This is the primary method the engine uses during
   the Present phase to determine what to present.

   ### Usage Examples

   ```cpp
   // Engine Present phase
   auto presentable = context.GetPresentableSurfaces();
   if (!presentable.empty()) {
     graphics_layer.PresentSurfaces(presentable);
   }
   ```

   @return Vector of surfaces marked as presentable
   @note Creates a filtered copy - thread-safe but not zero-cost
   @see SetSurfacePresentable, IsSurfacePresentable
  */
  auto GetPresentableSurfaces() const noexcept
    -> std::vector<std::shared_ptr<graphics::Surface>>
  {
    std::shared_lock lock(snapshotLock_);
    std::vector<std::shared_ptr<graphics::Surface>> presentable_surfaces;

    const size_t surface_count = std::min(
      engine_state_.surfaces.size(), engine_state_.presentable_flags.size());

    presentable_surfaces.reserve(surface_count); // Reserve max possible size

    for (size_t i = 0; i < surface_count; ++i) {
      // Use atomic load for thread-safe access
      std::atomic_ref<const uint8_t> flag_ref(
        engine_state_.presentable_flags[i]);
      if (flag_ref.load(std::memory_order_acquire) != 0) {
        presentable_surfaces.push_back(engine_state_.surfaces[i]);
      }
    }

    return presentable_surfaces;
  }

  //! Reset all presentable flags to non-presentable.
  /*!
   Clears all presentable flags, marking all surfaces as not ready
   for presentation. Typically called at frame start to reset state.

   @note Requires EngineTag capability for engine-only access
   @see SetSurfacePresentable
  */
  auto ClearPresentableFlags(EngineTag) noexcept -> void
  {
    std::unique_lock lock(snapshotLock_);
    std::fill(engine_state_.presentable_flags.begin(),
      engine_state_.presentable_flags.end(), 0);
  }

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
   Reports an error with compile-time type safety. The source module type
   is automatically determined from the template parameter.

   ### Performance Characteristics

   - Time Complexity: O(1) for insertion
   - Memory: Allocates string storage for message
   - Optimization: Thread-safe using shared_mutex

   ### Usage Examples

   ```cpp
   // Report error from ModuleManager
   context.ReportError<ModuleManager>("Module initialization failed");

   // Report error from GraphicsModule
   context.ReportError<GraphicsModule>("Failed to create render target");
   ```

   @tparam SourceType Module type reporting the error (must satisfy IsTyped)
   @param message Human-readable error description
   @note Thread-safe for concurrent access
   @see HasErrors, GetErrors, ClearErrorsFromSource
  */
  template <IsTyped SourceType>
  auto ReportError(std::string message,
    std::optional<std::string> source_key = std::nullopt) noexcept -> void
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

   ### Usage Examples

   ```cpp
   context.ReportError(module->GetTypeId(), "Module failed during execution");
   ```

   @see HasErrors, GetErrors, ClearErrorsFromSource
  */
  auto ReportError(TypeId source_type_id, std::string message,
    std::optional<std::string> source_key = std::nullopt) noexcept -> void
  {
    std::unique_lock lock { error_mutex_ };
    frame_errors_.emplace_back(FrameError {
      .source_type_id = source_type_id,
      .message = std::move(message),
      .source_key = std::move(source_key),
    });
  }

  //! Check if any errors have been reported this frame.
  /*!
   Returns true if any module has reported errors during the current frame.

   ### Usage Examples

   ```cpp
   if (context.HasErrors()) {
     // Handle error condition
     auto errors = context.GetErrors();
     for (const auto& error : errors) {
       // Process error
     }
   }
   ```

   @return True if errors exist, false otherwise
   @note Thread-safe for concurrent access
   @see GetErrors, ReportError
  */
  [[nodiscard]] auto HasErrors() const noexcept -> bool
  {
    std::shared_lock lock { error_mutex_ };
    return !frame_errors_.empty();
  }

  //! Get a thread-safe copy of all reported errors.
  /*!
   Returns a copy of all errors reported during the current frame.
   Safe for concurrent access and processing.

   ### Usage Examples

   ```cpp
   auto errors = context.GetErrors();
   for (const auto& error : errors) {
     LOG_ERROR("Module error: {}", error.message);
   }
   ```

   @return Vector containing copies of all frame errors
   @note Thread-safe via copy, no live references to internal data
   @see HasErrors, ClearErrors
  */
  [[nodiscard]] auto GetErrors() const noexcept -> std::vector<FrameError>
  {
    std::shared_lock lock { error_mutex_ };
    return frame_errors_;
  }

  //! Clear errors from a specific typed module source.
  /*!
   Removes all errors reported by the specified module type using
   compile-time type safety.

   ### Usage Examples

   ```cpp
   // Clear errors from ModuleManager
   context.ClearErrorsFromSource<ModuleManager>();

   // Clear errors from specific module type
   context.ClearErrorsFromSource<GraphicsModule>();
   ```

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
   Removes all errors reported by the specified module type using
   runtime TypeId. Useful for ModuleManager when working with
   dynamic module collections.

   ### Usage Examples

   ```cpp
   // Clear errors from runtime module type
   auto module_type_id = module.GetTypeId();
   context.ClearErrorsFromSource(module_type_id);
   ```

   @param source_type_id TypeId of the module type to clear errors from
   @note Thread-safe for concurrent access
   @see ClearErrorsFromSource<SourceType>(), ClearAllErrors
  */
  auto ClearErrorsFromSource(TypeId source_type_id) noexcept -> void
  {
    std::unique_lock lock { error_mutex_ };
    frame_errors_.erase(
      std::remove_if(frame_errors_.begin(), frame_errors_.end(),
        [source_type_id](const FrameError& error) {
          return error.source_type_id == source_type_id;
        }),
      frame_errors_.end());
  }

  //! Clear errors from a specific module source by TypeId and source key.
  /*!
   Removes all errors reported by the specified module type that also
   match the given source key. Provides granular error clearing for
   cases where multiple modules of the same type exist.

   ### Usage Examples

   ```cpp
   // Clear errors from specific module instance
   auto module_type_id = module.GetTypeId();
   auto module_name = module.GetName();
   context.ClearErrorsFromSource(module_type_id, module_name);
   ```

   @param source_type_id TypeId of the module type to clear errors from
   @param source_key Optional source key to match for granular clearing
   @note Thread-safe for concurrent access
   @see ClearErrorsFromSource(TypeId), ClearAllErrors
  */
  auto ClearErrorsFromSource(TypeId source_type_id,
    const std::optional<std::string>& source_key) noexcept -> void
  {
    std::unique_lock lock { error_mutex_ };
    frame_errors_.erase(
      std::remove_if(frame_errors_.begin(), frame_errors_.end(),
        [source_type_id, &source_key](const FrameError& error) {
          return error.source_type_id == source_type_id
            && error.source_key == source_key;
        }),
      frame_errors_.end());
  }

  //! Clear all reported errors.
  /*!
   Removes all errors reported during the current frame from all sources.
   Typically called at frame start to reset error state.

   ### Usage Examples

   ```cpp
   // Clear all errors at frame start
   context.ClearAllErrors();
   ```

   @note Thread-safe for concurrent access
   @see ClearErrorsFromSource, HasErrors
  */
  auto ClearAllErrors() noexcept -> void
  {
    std::unique_lock lock { error_mutex_ };
    frame_errors_.clear();
  }

private:
  //------------------------------------------------------------------------
  // Private helper methods
  //------------------------------------------------------------------------

  // Helper to populate FrameSnapshot views from GameStateSnapshot (zero-copy)
  auto PopulateFrameSnapshotViews(FrameSnapshot& frame_snapshot,
    const GameStateSnapshot& game_snapshot) const noexcept -> void;

  // Create and populate both GameStateSnapshot and FrameSnapshot atomically
  auto CreateUnifiedSnapshot() noexcept -> uint64_t;

  // Populate FrameSnapshot within a GameStateSnapshot with coordination context
  // and views
  auto PopulateFrameSnapshot(FrameSnapshot& frame_snapshot,
    const GameStateSnapshot& game_snapshot) const noexcept -> void;

  //------------------------------------------------------------------------
  // Private data members with controlled access
  //------------------------------------------------------------------------

  frame::SequenceNumber frameIndex_ { 0 };
  frame::Slot frameSlot_ { 0 };
  std::chrono::steady_clock::time_point frameStartTime_ {};

  // Immutable dependencies provided at construction and valid for app lifetime
  Immutable immutable_;

  //------------------------------------------------------------------------
  // GameState: authoritative mutable game data. Mutate only from the
  // coordinator (ordered phases). To allow parallel readers, take a
  // snapshot with CreateSnapshot() and hand out the shared_ptr.
  // ACCESS CONTROL: GameState mutation is phase-dependent. Parallel phases
  // must use snapshot APIs only.
  //------------------------------------------------------------------------
  struct GameState {
    EntityCommandBuffer* entityCmds = nullptr;
    std::vector<ViewInfo> views; // per-frame views
    // std::vector<LightData> lights;
    // std::vector<DrawBatch> drawBatches;

    // Cross-module authoritative game data using mutable policy
    GameDataMutable gameData;

    // Use the typed handle to avoid accidental mixing with other opaque
    // pointers. The handle mirrors GameStateSnapshot::UserContextHandle.
    GameStateSnapshot::UserContextHandle userContext;

    // Additional items include: scripting or UI interactions
  } game_state_;

  //------------------------------------------------------------------------
  // EngineState: coordinator-owned per-frame state. Use atomics for values
  // that workers may read concurrently (e.g. fence values).
  // ACCESS CONTROL: Engine-internal state is only accessible through
  // controlled getters/setters. Direct mutation requires EngineTag capability.
  //------------------------------------------------------------------------
  struct EngineState {
    // Graphics backend handle (may be swapped at runtime). Keep a weak_ptr
    // to avoid extending the backend lifetime from the FrameContext.
    std::weak_ptr<Graphics> graphics;

    // Render graph builder made available to modules that participate the
    // Render Graph phase.
    observer_ptr<RenderGraphBuilder> graph_builder_ { nullptr };

    std::atomic<uint64_t> frameFenceValue { 0 };
    observer_ptr<ResourceIntegrationData> asyncUploads = nullptr;
    observer_ptr<FrameProfiler> profiler = nullptr;

    // Frame execution state (use centralized PhaseId)
    core::PhaseId currentPhase = core::PhaseId::kFrameStart;

    // Thread pool pointer for spawning coroutine-aware parallel work
    observer_ptr<co::ThreadPool> threadPool = nullptr;

    // Monotonic epoch for resource lifecycle management
    uint64_t epoch = 0;

    // Per-frame surfaces with phase-dependent mutation control
    std::vector<std::shared_ptr<graphics::Surface>> surfaces;

    // Per-surface presentable flags (1:1 correspondence with surfaces vector)
    // uint8_t used for atomic operations and consistency with parallel workers
    std::vector<uint8_t> presentable_flags;
  } engine_state_;

  // Per-frame performance metrics (timing and budget stats)
  Metrics metrics_ {};

  // Atomic snapshot publication using private unified structure
  // RATIONALE: Keep GameStateSnapshot and FrameSnapshot separate for clean APIs
  // but publish them together atomically for consistent lock-free access
  struct UnifiedSnapshot {
    std::shared_ptr<GameStateSnapshot> gameSnapshot;
    FrameSnapshot frameSnapshot;
  };

  mutable std::shared_mutex snapshotLock_;
  std::array<UnifiedSnapshot, 2> snapshotBuffers_;
  std::atomic<uint32_t> visibleSnapshotIndex_ { 0 };
  std::atomic<uint64_t> snapshotVersion_ { 0 };

  // Lock-free input snapshot pointer (written once per frame by coordinator)
  std::atomic<std::shared_ptr<const InputSnapshot>> atomicInputSnapshot_;

  // Error reporting system state
  mutable std::shared_mutex error_mutex_;
  std::vector<FrameError> frame_errors_;
};

} // namespace oxygen::engine
