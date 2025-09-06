# Oxygen Renderer Architecture Design

## Executive Summary

This document defines the architecture for restructuring the Oxygen Engine renderer to properly integrate with the engine's frame phase lifecycle and existing systems. The design builds upon existing `RenderItemData`, `RenderContext`, `PreparedSceneFrame`, and frame phase orchestration, eliminating invented data structures and manager proliferation.

The architecture aligns with the engine's **ABCD execution model** defined in `PhaseRegistry.h` and properly supports snapshotting for parallel task execution during the `kParallelTasks` phase.

## Frame Phase Integration

### Engine Frame Phases (from PhaseRegistry.h)

The renderer operates within the engine's canonical frame phases:

- **kSnapshot** (Phase 8) - Fully synchronous module phase. Modules (incl. Renderer) run on the main thread, prepare snapshot-visible data; Engine consolidates module contributions and publishes snapshots last.
- **kParallelTasks** (Phase 9) - Scene preparation runs in parallel on worker threads
- **kPostParallel** (Phase 10) - Integration of parallel work results
- **kFrameGraph** (Phase 11) - Render graph construction and optimization
- **kCommandRecord** (Phase 12) - GPU command recording from prepared data
- **kPresent** (Phase 13) - Graphics system presents frames (Renderer sets presentable surfaces)

### Snapshotting Architecture

The engine's `FrameContext` is the **central coordination mechanism** providing snapshotting for safe parallel access:

**FrameContext** - Central frame execution context with strict access control

- **Access Control Model**: EngineState (engine-internal), GameState (cross-module), Snapshots (immutable parallel)
- **Capability-Based Access**: EngineTag tokens restrict critical operations
- **Phase-Dependent Validation**: Operations restricted by execution phase
- **Atomic Snapshot Publication**: Double-buffered with lock-free parallel access

**GameStateSnapshot** - Heavy authoritative data (owned containers)

- Scene node transforms, entity hierarchy, **prepared scene data from ScenePrep**
- Material properties, animation states, physics simulation state, audio data
- Owned via `FrameContext.GetGameStateSnapshot()` - modules access heavy data
- Thread-safe sharing via `shared_ptr<const GameStateSnapshot>`

**FrameSnapshot** - Lightweight coordination views (non-owning spans)

- Views into GameStateSnapshot data optimized for parallel access
- Structure-of-Arrays layouts for cache-friendly iteration
- Different view patterns: culling, animation, rendering
- Accessed via `FrameContext.GetFrameSnapshot()` - parallel tasks access coordination views

Renderer-visible additions to FrameSnapshot (this design):

- `RendererSnapshotViews` – snapshot-safe, item-centric, non-owning spans that expose exactly what renderer parallel tasks need. These may share storage with the arrays later used by passes, but are conceptually independent of PreparedSceneFrame (CPU-oriented vs GPU/draw-oriented). Includes:
  - visible_items: `std::span<const RenderItemData>` (immutable)
  - transform_handles: `std::span<const TransformHandle>` (aligned to visible_items)
  - world_bounding_spheres: `std::span<const glm::vec4>` (aligned to visible_items)
  - pass_masks: `std::span<const PassMask>` (aligned to visible_items)
  - Optional transforms: world_matrices/normal_matrices spans, if finalized pre-snapshot

The Renderer prepares these views in its kSnapshot handler and installs them into the FrameSnapshot. The Engine then consolidates across modules and calls `PublishSnapshots(...)`. Passes later receive `PreparedSceneFrame` via `RenderContext.prepared_frame` (draw-centric GPU-facing data).

**PreparedSceneFrame** - Finalized SoA render data (existing system)

- Immutable spans over renderer-owned arrays
- Per-draw metadata in GPU-facing layout
- World/normal transform matrices indexed by draw
- Partition ranges for different render passes

Publication model in this design (and why it differs from snapshot views):

- PreparedSceneFrame is draw-centric (GPU/draw commands, deduped matrices in draw order, partitions). It is wired into `RenderContext.prepared_frame` for pass execution.
- FrameSnapshot stores `RendererSnapshotViews`, which are item-centric and CPU-friendly. They may optionally reference the same matrix storage but do not expose GPU draw metadata.
- During kFrameGraph/kCommandRecord, passes consume `PreparedSceneFrame` only. Parallel tasks never rely on GPU draw metadata.

## Module Definitions

### 1. ScenePrep Module - Scene Data Extraction Pipeline

**Location:** `src/Oxygen/Renderer/ScenePrep/`

**Phase Integration:** Executes during `kSnapshot` (synchronous, main thread) as part of snapshot creation process

**Responsibility:** Extract and filter scene data for rendering, contributes to authoritative snapshot

**Core Coordinator:**

- **ScenePrepCoordinator** - orchestrates extraction pipeline during snapshot creation

**Worker Components:**

- **EntityExtractor** - extract entities from live scene systems with frustum culling
- **LodSelector** - select appropriate level-of-detail for entities
- **VisibilityTester** - perform occlusion culling and visibility tests
- **RenderItemEmitter** - create `RenderItemData` structures from visible entities
- **MaterialFilter** - apply material-based filtering (transparent, opaque, etc.)
- **SortingWorker** - sort RenderItems by depth, material, etc.

**Input:** FrameContext reference (for mutable GameState access), camera parameters, culling volumes
**Output:** Prepared scene data contributed to FrameContext's mutable GameState

**Key Principle:** ScenePrep is CRITICAL TO SNAPSHOT CREATION - mutates FrameContext.GameState before PublishSnapshots()

### 2. Resources Module - GPU Resource Management

Location: `src/Oxygen/Renderer/Resources/`

Phase Integration:

- kSnapshot (if needed) – finalize item-centric arrays used by `renderer_snapshot` views (no GPU/draw metadata exposure).
- kPostParallel – integrate parallel results and process snapshot data to build the immutable `PreparedSceneFrame` for this frame.

Responsibility: Manage GPU-facing SoA for the current frame and bindless/bookkeeping using snapshot data and ScenePrep outputs.

Core Coordinator:

- ResourceCoordinator – orchestrates GPU resource lifecycle and handle allocation (part of this design now).

Worker Components (part of this design and grounded by existing systems):

- GeometryUploader – builds per-draw metadata and resource indices from geometry assets (uses `Graphics::ResourceRegistry`).
- MaterialBinder – allocates material slots and updates material buffers (bindless table management policy lives here).
- TransformUploader – batches unique transform uploads and produces deduplicated matrices (aligns with existing `TransformManager`).
- HandleAllocator – versioned/recyclable logical handles for stable indices.
- ResidencyTracker – decides what to (re)upload this frame.
- DescriptorManager – issues descriptor table updates for bindless access.

Inputs: FrameContext snapshots (read-only) and ScenePrep collected items; Outputs: updated bindless handles, uploaded buffers, descriptor tables, and the finalized arrays wired into `PreparedSceneFrame`.

Dependencies: `Graphics::ResourceRegistry` for low-level GPU object/view creation.

### 3. RenderGraph Module - Render Pass Execution

Location: `src/Oxygen/Renderer/` (coroutine-based graph)

Phase Integration:

- kFrameGraph – graph assembly (application contributes passes, renderer seeds context)
- kCommandRecord – pass execution and GPU command recording

Execution model (existing, clarified):

- The RenderGraph is a coroutine invoked via `Renderer::ExecuteRenderGraph(RenderContext&)`.
- Passes register themselves in `RenderContext` using `RegisterPass<T>()` so other passes can `GetPass<T>()` for dependencies.
- GPU work is recorded through the renderer/graphics APIs and `ResourceRegistry`.

Render passes (current examples):

- DepthPrePass, ShaderPass, TransparentPass. Additional passes (e.g., Forward/Deferred/Post) can be added following the same pattern.

Input: `RenderContext` with `prepared_frame` (SoA views), `scene_constants`, optional `material_constants`.
Output: Rendered surfaces; passes mark presentable surfaces in `FrameContext`.

Dependencies: `RenderContext` pass registry and `Graphics::ResourceRegistry`.

## Data Flow Architecture

### Phase 8: kSnapshot (Synchronous module phase; engine publishes last)

```text
Renderer (Main thread - synchronous kSnapshot handler):
  -> ScenePrepCoordinator extracts and processes scene data
  -> EntityExtractor: Query visible entities from scene systems
  -> LodSelector: Select appropriate detail levels
  -> VisibilityTester: Apply occlusion culling
  -> MaterialFilter: Separate opaque/transparent
  -> SortingWorker: Sort by depth/material
  -> Creates RenderItemData structures
  -> Contributes RenderItemData to FrameContext's mutable GameState

Renderer (still in kSnapshot):
  -> Installs `renderer_snapshot` views into FrameSnapshot (item-centric, CPU-friendly)

Engine Core (after all modules finish kSnapshot):
  -> Consolidates module contributions
  -> Calls FrameContext.PublishSnapshots(EngineTag)
  -> Atomic double-buffered publication for lock-free parallel access
Result: Immutable snapshots available via `FrameContext.GetFrameSnapshot()` including `renderer_snapshot` (no GPU/draw metadata).
```

### Phase 9: kParallelTasks (Animation, Particles, Physics, etc. via FrameSnapshot)

```text
Parallel Workers (NO access to FrameContext - use immutable snapshots only):
  -> Animation workers: Access FrameSnapshot.renderer_snapshot (e.g., matrices if present) and game data views
  -> Particle workers: Access FrameSnapshot for spatial data and renderer_snapshot for matrices
  -> Physics workers: Access FrameSnapshot for collision data and transforms
  -> AI workers: Access FrameSnapshot for visibility and spatial information
  -> All workers consume GameStateSnapshot + FrameSnapshot.renderer_snapshot atomically (no access to RenderContext or PreparedSceneFrame)
  -> Each worker produces its own results (updated animations, particles, etc.)
Result: Per-worker outputs ready for integration (renderer_snapshot is immutable)
```

### Phase 10: kPostParallel (Renderer Integration of Parallel Results)

```text
Renderer (Main thread - collects parallel worker results):
  -> Queries animation workers for updated transform data
  -> Queries particle workers for new particle render data
  -> Queries physics workers for updated collision/movement data
  -> Integrates all parallel results and updates internal buffers
  -> Prepares finalized arrays for the next snapshot publication:
     * RenderContext.prepared_frame points to updated renderer data
     * Updates scene_constants buffer with new camera/lighting data
     * Updates material_constants buffer with material properties
     * Prepares bindless handle mappings and descriptor tables
Result: Renderer has arrays ready; engine will publish snapshots next, installing renderer_snapshot into FrameSnapshot
```

### Phase 11: kFrameGraph (Coroutine-based pass orchestration)

```text
Main Thread:
  -> Modules register render passes using the existing coroutine-based model
  -> Pass coroutines express dependencies via awaits/yields and shared resources
  -> Renderer orchestrates pass execution using RenderContext and prepared_frame
  -> Resource lifetimes are scoped to pass/coroutine stages; no separate builder
Result: Pass set ready for coroutine execution (aligned with current code)
```

### Phase 12: kCommandRecord (Coroutine-based RenderGraph using RenderContext)

```text
Parallel Command Recording:
  -> RenderGraph executor launches pass batches
  -> Each pass accesses RenderContext.prepared_frame directly
  -> Passes use RenderContext.scene_constants and material_constants
  -> PassExecutor functions record GPU commands via TaskExecutionContext
  -> Commands recorded to per-thread command lists
  -> GPU commands submitted to graphics system
  -> At end: Renderer transfers ONLY presentable surface flags to FrameContext
Result: Rendering complete, surfaces ready for presentation, RenderContext data preserved
```

### Phase 13: kPresent (Surface Flags Transfer, Renderer Data Preserved)

```text
Renderer → FrameContext (Essential present data only):
  -> Renderer transfers ONLY presentable surface flags to FrameContext
  -> RenderContext data is preserved (NOT wiped) - it belongs to renderer
  -> Only the minimal presentation information crosses the boundary

Graphics System:
  -> Reads presentable surface flags from FrameContext
  -> Performs actual presentation to display/swapchain
  -> Handles swapchain management and timing
Result: Frame presented, RenderContext data preserved for renderer's next frame cycle
```

## Data Structure Lifecycle

This section explains how the existing data structures flow through the rendering pipeline, who creates and updates them, and when these operations occur within the frame phases.

### Data Flow Timeline

**Phase 8 (kSnapshot):** ScenePrep creates RenderItemData, contributes to GameState; FrameSnapshot contains renderer_snapshot views only (no PreparedSceneFrame exposure)
**Phase 9 (kParallelTasks):** Animation, particles, and other parallel work using FrameSnapshot views (including RenderContext.prepared_frame)
**Phase 10 (kPostParallel):** Resources processes parallel results, updates data that RenderContext.prepared_frame points to
**Phase 11-12 (kFrameGraph/kCommandRecord):** RenderGraph consumes `RenderContext.prepared_frame` directly
**Phase 13 (kPresent):** Only presentable surface flags transferred from RenderContext to FrameContext

### RenderItemData Lifecycle

**Created by:** ScenePrep workers during `kSnapshot` phase
**Created when:** Entity extraction from live scene systems, contributed to FrameContext.GameState
**Updated by:** Never updated - immutable after snapshot publication via FrameContext.PublishSnapshots()
**Consumed by:** Resources module during `kPostParallel` phase (via FrameContext.GetGameStateSnapshot())

```cpp
struct RenderItemData {
  std::uint32_t lod_index = 0;                    // Set by LodSelector
  std::uint32_t submesh_index = 0;                // Set by EntityExtractor

  // Asset references - Set by RenderItemEmitter from scene nodes
  std::shared_ptr<const oxygen::data::GeometryAsset> geometry;
  std::shared_ptr<const oxygen::data::MaterialAsset> material;
  MaterialHandle material_handle { 0U };          // Populated by Resources module

  // Cached scene state - Set by VisibilityTester
  glm::vec4 world_bounding_sphere;
  TransformHandle transform_handle { 0U };        // Set by TransformUploader

  // Rendering flags - Set by MaterialFilter
  bool cast_shadows = true;
  bool receive_shadows = true;
};
```

**Lifecycle Steps:**

1. **EntityExtractor** creates initial structure with geometry/material assets
2. **LodSelector** sets `lod_index` based on distance/importance
3. **VisibilityTester** sets `world_bounding_sphere` and spatial data
4. **MaterialFilter** sets rendering flags (`cast_shadows`, `receive_shadows`)
5. **ScenePrep** contributes prepared data to FrameContext.GameState
6. **Engine Core** calls FrameContext.PublishSnapshots(EngineTag) creating immutable snapshots
7. **Resources module** accesses data via FrameContext.GetGameStateSnapshot() and populates handle fields

### PreparedSceneFrame Lifecycle

**Part of RenderContext:** PreparedSceneFrame exists as observer_ptr field in RenderContext with std::span fields
**Data location:** Spans point to data owned by renderer (implementation detail, not in FrameContext)
**Updated during:** kPostParallel phase - Resources updates the underlying data that spans point to
**Accessed via:** FrameSnapshot views during kParallelTasks, RenderContext.prepared_frame during kCommandRecord
**Transfer to FrameContext:** NEVER - only presentable surface flags transferred, not PreparedSceneFrame itself

```cpp
struct PreparedSceneFrame {
  // Populated by GeometryUploader - finalized vertex/index data
  std::span<const std::byte> draw_metadata_bytes;  // GPU-facing draw commands

  // Populated by TransformUploader - deduped transform matrices
  std::span<const float> world_matrices;           // 16 floats per transform
  std::span<const float> normal_matrices;          // 16 floats per normal transform

  // Populated by renderer when organizing draws for passes (implementation-defined)
  struct PartitionRange {
    PassMask pass_mask {};     // Which passes use this range
    uint32_t begin = 0;        // First draw in range
    uint32_t end = 0;          // One past last draw in range
  };
  std::span<const PartitionRange> partitions;     // Pass-specific draw ranges
};
```

**Lifecycle Steps:**

1. **GeometryUploader** creates `draw_metadata_bytes` from geometry assets
2. **TransformUploader** creates `world_matrices` and `normal_matrices` with deduplication
3. **MaterialBinder** ensures materials are uploaded and handles allocated
4. Renderer organizes `partitions` for efficient pass execution
5. **ResourceCoordinator** assembles final `PreparedSceneFrame` structure

### RenderContext Lifecycle

**Created by:** RenderGraph system before pass execution
**Created when:** Start of `kCommandRecord` phase
**Updated by:** Each render pass registers itself for cross-pass dependencies
**Consumed by:** All render passes during execution

```cpp
struct RenderContext {
  uint64_t frame_index = 0;                       // Set by Engine Core
  float frame_time = 0.0f;                        // Set by Engine Core

  // Set by Resources module - GPU constant buffers
  std::shared_ptr<const graphics::Buffer> scene_constants;
  std::shared_ptr<const graphics::Buffer> material_constants;

  // Set by ResourceCoordinator - points to current frame's prepared data
  observer_ptr<const PreparedSceneFrame> prepared_frame { nullptr };

  // Used by passes for cross-pass communication
  template <typename PassT> auto GetPass() const -> PassT*;
  template <typename PassT> auto RegisterPass(PassT* pass) const -> void;
};
```

**Lifecycle Steps:**

1. **Engine Core** sets timing information (`frame_index`, `frame_time`)
2. **ResourceCoordinator** sets `prepared_frame` pointer to current frame data
3. **MaterialBinder** uploads and sets `material_constants` buffer
4. **SceneConstantsUploader** sets `scene_constants` buffer (camera, lighting)
5. **Each render pass** registers itself via `RegisterPass()` for dependencies
6. **Dependent passes** access previous passes via `GetPass<PassType>()`

### Data Lifecycle Diagram - Renderer ↔ FrameContext Integration

**Key Data Structures:**

```text
RenderItemData → FrameSnapshot → ParallelTasks → RenderContext.prepared_frame → GPU Commands → Present Flags
     ↑              ↑              ↑                     ↑                          ↑              ↑
  ScenePrep    Engine Core    Parallel Tasks         Resources                RenderGraph    FrameContext
  (Phase 8)     (Phase 8)      (Phase 9)            (Phase 10)               (Phase 12)     (Phase 13)
```

## **Enhanced FrameContext for Renderer Integration**

Based on the commented code in FrameContext.h, here are the specific modifications needed:

### **GameState Extensions (Authoritative Data)**

```cpp
// Add to GameDataCommon template in FrameContext.h
struct RendererGameData {
  // ScenePrep contributions (Phase 8: kSnapshot)
  std::vector<RenderItemData> render_items;        // Visible geometry for rendering
  std::vector<LightData> lights;                   // Scene lighting data
  std::vector<DrawBatch> draw_batches;             // Pre-computed draw calls
  std::vector<ViewInfo> render_views;              // Camera matrices, render targets

  // Spatial/culling data from visibility tests
  std::vector<BoundingBox> render_bounds;          // Entity bounding boxes
  std::vector<CullingVolume> culling_volumes;      // Frustum/portal culling data

  // Material data for uniform preparation
  std::vector<MaterialProperties> material_props;  // Static material parameters
  std::vector<TextureHandle> texture_handles;      // Material texture references
  std::vector<ShaderHandle> shader_handles;        // Material shader references
};
```

### **FrameSnapshot Extensions (Parallel Access Views)**

```cpp
// Add to FrameSnapshot in FrameContext.h – lightweight renderer views
// Item-centric, CPU-friendly; may alias storage later used by PreparedSceneFrame.
struct RendererSnapshotViews {
  // Core item views
  std::span<const TransformHandle> transform_handles;
  std::span<const BoundingSphere>  world_bounding_spheres;
  std::span<const PassMask>        pass_masks;

  // Optional, if finalized pre-snapshot (16 floats per matrix)
  std::span<const float>           world_matrices;
  std::span<const float>           normal_matrices;
};

RendererSnapshotViews renderer_snapshot; // member of FrameSnapshot
```

### **Renderer Coordination (NOT a big state object)**

The Renderer acts as a **coordinator** that orchestrates module execution and sets up integration points:

```cpp
class Renderer {
public:
  // Phase orchestration - calls modules in correct order
  auto ExecuteScenePrep(FrameContext& frame_ctx) -> void;
  auto ExecuteResources(FrameContext& frame_ctx) -> void;
  auto ExecuteRenderGraph(RenderContext& render_ctx) -> void;

  // Integration – set prepared_frame from Resources output (before graph)
  auto SetupRenderContext(RenderContext& ctx, const PreparedSceneFrame* frame) -> void {
    ctx.prepared_frame.reset(frame);
  }

  // Individual modules manage their own state - NO central RendererInternalState
};
```

**Key Principle:** Each module (ScenePrep, Resources, RenderGraph) manages its own state. Renderer just coordinates execution and integration points.

### **FrameContext API Extensions**

```cpp
// Add to FrameContext class public interface
class FrameContext {
public:
  // Renderer snapshot views (installed just before PublishSnapshots)
  auto SetRendererSnapshotViews(RendererSnapshotViews views, EngineTag) -> void;

  // Presentation coordination (engine-internal)
  auto MarkSurfacePresentable(size_t surface_index, EngineTag) -> void;
  auto GetPresentableSurfaces() const -> std::span<const bool>;

  // Note: No central renderer state - modules manage their own state
  // Renderer coordinates using existing RenderContext integration points
};
```

## **Renderer Data Flow Summary**

```plantuml
@startuml
!theme plain
skinparam backgroundColor white

package "ScenePrep Module" {
  [ScenePrep Workers] as ScenePrep
}

package "Resources Module" {
  [Resource Coordinator] as Resources
}

package "RenderGraph Module" {
  [RenderGraph Executor] as RenderGraph
}

package "Graphics Module" {
  [Graphics System] as Graphics
}

package "FrameContext" {
  database "GameState" as GameState
  database "FrameSnapshot" as FrameSnapshot
  database "Present Flags" as PresentFlags
}

package "RenderContext" {
  database "PreparedSceneFrame" as PreparedFrame
  note top of PreparedFrame : observer_ptr field\nSpans point to renderer data
}

ScenePrep --> GameState : "Phase 8:\nContribute RenderItemData"
GameState --> FrameSnapshot : "Phase 8:\nCreate module views"
Resources --> FrameSnapshot : "Phase 8:\nRenderer installs renderer_snapshot"
FrameSnapshot --> Resources : "Phase 10:\nAccess snapshot data"
Resources --> PreparedFrame : "Phase 10:\nUpdate underlying data"
PreparedFrame --> RenderGraph : "Phase 12:\nConsume via RenderContext"
RenderGraph --> PresentFlags : "Phase 13:\nTransfer only present flags"
PresentFlags --> Graphics : "Phase 13:\nPresent surfaces"

@enduml
```

**Phase 8 (kSnapshot):** ScenePrep creates RenderItemData and contributes to GameState. FrameSnapshot contains `renderer_snapshot` (item-centric views). It does NOT expose `PreparedSceneFrame` or draw metadata.

**Phase 9 (kParallelTasks):** Animation, particles, physics workers access FrameSnapshot (including `renderer_snapshot`), never `RenderContext` or `PreparedSceneFrame`.

**Phase 10 (kPostParallel):** Renderer collects parallel results, updates the underlying data that `PreparedSceneFrame` spans point to

**Phase 12 (kCommandRecord):** RenderGraph consumes RenderContext.prepared_frame directly. At end: ONLY presentable surface flags transferred to FrameContext, ALL other renderer data stays in RenderContext

**Phase 13 (kPresent):** Graphics layer reads presentable surface flags from FrameContext and executes present

## **Timeline-Based Data Lifecycle Visualization**

```plantuml
@startuml
!theme plain
skinparam backgroundColor white
skinparam swimlaneBorderColor transparent
skinparam swimlaneTitleBackgroundColor transparent

|RenderContext (Always Present)|
start
note right
**RenderContext - Big Container:**
• PreparedSceneFrame (observer_ptr with spans)
• scene_constants: Buffer
• material_constants: Buffer
• framebuffer, pass registry, etc.

**PreparedSceneFrame spans point to:**
end note

:Renderer Internal Data\ndraw_metadata: arrays\nworld_matrices: arrays\nnormal_matrices: arrays\npartitions: arrays\nbindless_handles: maps\nGPU resources;

:RenderItemData\ngeometry: GeometryAsset\nmaterial: MaterialAsset\ntransform_handle: TransformHandle\nlod_index: uint32_t;

:LightData\nposition: Vector3\nintensity: float\ncolor: Color\ntype: LightType;

|Timeline|
:Phase 8: kSnapshot (sync)\nRenderer runs ScenePrep and\ninstalls renderer_snapshot;\nEngine publishes last;

split
   |FrameContext|
   :GameState Extensions\nrender_items: vector<RenderItemData>\nlights: vector<LightData>\ndraw_batches: vector<DrawBatch>;
split again
   |FrameContext|
  :FrameSnapshot Views\nRenderItemData spans\n+ renderer_snapshot (item-centric views)\n(Installed by Renderer during kSnapshot);\nno PreparedSceneFrame exposure;
end split

|Timeline|
:Phase 9: kParallelTasks\nWorkers access\nFrameSnapshot views\n(renderer_snapshot only);

|Timeline|
:Phase 10: kPostParallel\nResources updates\nrenderer internal data\nthat prepared_frame points to;

|RenderContext (Always Present)|
:RenderContext.prepared_frame\nstill pointing to\nupdated internal data;

|Timeline|
:Phase 12: kCommandRecord\nRenderGraph consumes\nRenderContext.prepared_frame\ndirectly;

|RenderContext (Always Present)|
:GPU Commands Generated\nfrom RenderContext data;

|Timeline|
:Phase 13: kPresent\nONLY present flags\ntransferred to FrameContext;

|FrameContext|
:Present Flags Only\nno renderer data;

stop

@enduml
```

### Key Data Flow Principles

- `RenderItemData` is immutable after ScenePrep phase completion
- `PreparedSceneFrame` is immutable after Resources phase completion
- `RenderContext` allows only pass registration during execution

**Single Ownership:**

- Each data structure has exactly one creator/owner
- Consumers receive read-only access via const pointers/spans
- No shared mutable state between phases

**Phase-Aligned Lifecycle:**

- Data creation aligns with engine phase boundaries
- Each phase produces data consumed by subsequent phases
- No cross-phase data dependencies or circular references

**Handle-Based Indirection:**

- `MaterialHandle` and `TransformHandle` provide stable references
- Handles remain valid across frame boundaries for caching
- GPU resources accessed via handles, not direct pointers

## Phase Execution Flow Diagrams

### Overall Frame Execution

```plantuml
@startuml
!theme plain
skinparam backgroundColor white
skinparam componentStyle rectangle

participant "Engine Core" as Engine
participant "Renderer" as Rend
participant "ScenePrep" as Prep
participant "Resources" as Res
participant "GameState" as GS
participant "FrameSnapshot" as FS
participant "RenderGraph" as Graph
participant "Graphics" as GFX

== Phase 8: kSnapshot ==
Engine -> Rend: OnSnapshot(FrameContext&)
activate Rend
Rend -> Prep: Run ScenePrep (mutate GameState)
activate Prep
Prep --> GS: Contribute RenderItemData
deactivate Prep
Rend -> GS: SetRendererSnapshotViews(renderer_snapshot)
deactivate Rend
Engine -> GS: Consolidate module contributions
Engine -> GS: PublishSnapshots(EngineTag)
GS --> FS: Publish GameStateSnapshot + renderer_snapshot
note right of FS: Immutable views for parallel tasks

== Phase 9: kParallelTasks ==
note over FS: Workers read snapshots only

== Phase 10: kPostParallel ==
Rend -> Res: Integrate parallel results
activate Res
Res -> Res: Process RenderItemData
Res -> GFX: Upload/update GPU resources
Res -> Res: Allocate bindless handles
Res -> Res: Create PreparedSceneFrame
Res --> Rend: Return PreparedSceneFrame
deactivate Res

== Phase 11: kFrameGraph ==
Rend -> Graph: Build render graph
activate Graph
Graph -> Graph: Collect module passes
Graph -> Graph: Analyze dependencies
Graph -> Graph: Optimize resources
Graph -> Graph: Create execution plan
Graph --> Engine: Passes registered (coroutines ready)
deactivate Graph

== Phase 12: kCommandRecord ==
Rend -> Graph: Execute render graph
activate Graph
Graph -> Graph: Launch pass batches
Graph -> GFX: Record GPU commands
Graph -> GFX: Submit command lists
Graph -> Graph: Mark surfaces presentable in FrameContext
Graph --> Engine: Commands submitted, surfaces ready
deactivate Graph

== Phase 13: kPresent ==
Engine -> GFX: Present marked surfaces
note right: Graphics system handles\nactual presentation to display
@enduml
```

## Renderer as an EngineModule

### Role and boundaries

- The Renderer is a single EngineModule that coordinates all renderer-side
  work visible to the engine phases. It owns the renderer subsystems
  (ScenePrep pipeline, Resources, RenderGraph, Upload) but exposes only a
  small, phase-aligned surface to the engine.
- kSnapshot becomes a synchronous module phase. The Renderer participates via
  its OnSnapshot handler (main thread), runs ScenePrep, and installs
  `renderer_snapshot` views before the Engine consolidates and publishes.
  kPresent remains engine-only.
- PreparedSceneFrame, bindless tables, and GPU resources are internal to the
  renderer and never stored in `FrameContext` (only presentable-surface flags
  cross the boundary at the end of the frame).

### Phase participation (EngineModule handlers)

- OnFrameStart: optional lightweight epoch/metrics reset; validate previous
  frame disposal; no heavy work.
- OnSnapshot(FrameContext&): synchronous; run ScenePrep to mutate GameState
  (visibility, LOD, `RenderItemData`, etc.) and install `renderer_snapshot`
  views into `FrameSnapshot`. Must return before Engine publishes.
- OnParallelTasks(const FrameSnapshot&): typically a no-op; may schedule
  strictly read-only helpers (e.g., precompute light clusters) using the
  immutable snapshots. Must not mutate GameState.
- OnPostParallel(FrameContext&): integrates parallel outputs; runs the
  Resources pipeline to produce/refresh the internal arrays that
  `PreparedSceneFrame` spans point to; updates constant buffers and bindless
  tables; keeps `PreparedSceneFrame` immutable afterwards.
- OnFrameGraph(FrameContext&): seeds `RenderContext` (pointers, constants),
  registers passes in the coroutine-based RenderGraph, and finalizes the
  pass set for execution. Avoid heavy allocations here.
- OnCommandRecord(FrameContext&): executes the RenderGraph coroutine plan;
  records GPU commands via Graphics/ResourceRegistry; marks presentable
  surfaces in `FrameContext`. Does not transfer renderer-internal state.
- OnFrameEnd: optional cleanup/accounting; defer costly destruction to
  background when possible.

Notes:

- kSnapshot: synchronous across modules; Renderer does ScenePrep and installs
  `renderer_snapshot`; Engine consolidates across modules and publishes last.
- kPresent: engine-only; Engine Core reads presentable surface flags and
  performs the actual presentation.

### Ownership and collaborators

- Owned by the Renderer module (not separate EngineModules):
  - ScenePrep pipeline (orchestrated by the renderer during the snapshot
    phase through the engine’s pre-publish hook)
  - Resources (GeometryUploader, MaterialBinder, TransformUploader,
    Descriptor/Handle managers, Residency)
  - RenderGraph (coroutine host and pass registry)
  - Upload (staging, batching, transfer queue)

- External dependencies:
  - Engine Core for phase orchestration and snapshot publication
  - `FrameContext`/`FrameSnapshot` for data exchange at phase boundaries
  - `Graphics::ResourceRegistry` for GPU object/view creation and command
    recording support

### Responsibilities per phase (high-level contract)

- During kSnapshot (synchronous, module participation):
  - ScenePrep mutates GameState (visibility, LOD, RenderItemData, etc.).
  - Renderer installs `renderer_snapshot` views into `FrameSnapshot` to
    support parallel workers. No GPU/draw-facing data is exposed here.

- OnParallelTasks:
  - Zero or minimal read-only helpers; parallel producers (Animation,
    Particles, Physics) run as their own modules and later hand results to
    the renderer to integrate.

- OnPostParallel:
  - Collect and integrate parallel results.
  - Build/update arrays, bindless tables, and buffers; assemble
    `PreparedSceneFrame` and freeze it for the rest of the frame.

- OnFrameGraph:
  - Populate `RenderContext` with pointers to immutable prepared data and
    constant buffers; register passes; analyze dependencies through the
    coroutine model (no separate builder/compiled graph abstraction).

- OnCommandRecord:
  - Execute passes; record and submit GPU work; set presentable surface
    flags via `FrameContext`.

### Invariants and safety

- Snapshots are immutable: no GameState mutation after
  `PublishSnapshots(EngineTag)`.
- `renderer_snapshot` is item-centric and CPU-oriented; it never exposes
  GPU draw metadata.
- `PreparedSceneFrame` becomes immutable after OnPostParallel and remains so
  throughout OnCommandRecord.
- Only presentable-surface flags cross back to `FrameContext`; all other
  renderer data lives inside the renderer.

### Priority and mask (guidance)

- Priority: medium-high relative to other visualization modules so that
  post-processing/UI layers that depend on renderer outputs can schedule
  afterwards if needed. Exact number is project-specific.
- Phase mask: enable OnParallelTasks (if used), OnPostParallel, OnFrameGraph,
  OnCommandRecord, OnFrameStart/OnFrameEnd. kSnapshot/kPresent are not in the
  module mask (engine-only).

### Error handling and telemetry

- Use `FrameContext.ReportError` with the module’s type/name to attribute
  failures clearly.
- Track per-phase timings and resource statistics (uploads, residency,
  descriptor changes) to guide budgeting and future optimizations.

### RenderGraph Construction and Execution

```plantuml
@startuml
!theme plain
skinparam backgroundColor white

participant "Module" as Mod
participant "RenderGraph (Coroutine Host)" as Graph
participant "Pass Coroutine" as Pass
participant "TaskExecutionContext" as Context
participant "PassExecutor" as Executor

== Phase 11: kFrameGraph - Register ==
Mod -> Graph: co_register_pass(GeometryPass)
Mod -> Graph: co_register_pass(LightCulling)
note right of Graph: Passes are coroutines\nwith awaits/yields expressing\nimplicit dependencies

== Phase 12: kCommandRecord - Execute ==
loop for each scheduled pass
  Graph -> Pass: co_resume()
  activate Pass
  Pass -> Context: co_await(GetContext())
  Context -> Context: Configure PreparedSceneFrame access
  Context -> Context: Setup resource bindings
  Pass -> Executor: RecordCommands(Context)
  activate Executor
  Executor -> Context: GetCommandRecorder()
  Executor -> Context: GetPreparedFrame()
  Executor -> Context: Record GPU commands
  deactivate Executor
  Pass --> Graph: yield next stage or complete
  deactivate Pass
end

Graph -> Graph: Submit all command lists
Graph -> Graph: Mark surfaces as presentable in FrameContext
note right: Renderer's responsibility ends here.\nGraphics system handles actual presentation.
@enduml
```

### Snapshotting and Parallel Access

```plantuml
@startuml
!theme plain
skinparam backgroundColor white

participant "Engine Core" as Engine
participant "GameState" as Game
participant "GameStateSnapshot" as Snapshot
participant "FrameSnapshot" as Frame
participant "ScenePrep Workers" as Workers

== Phase 8: kSnapshot ==
Engine -> Game: Freeze authoritative state
Engine -> Snapshot: Create heavy data snapshot
note right: Owned containers:\n- Entity transforms\n- Material properties\n- Animation states

Engine -> Frame: Create lightweight views
note right: Non-owning spans:\n- Transform arrays\n- Visibility data\n- Culling volumes

== Phase 9: kParallelTasks ==
Engine --> Workers: Launch with FrameSnapshot
activate Workers

par Worker 1
  Workers -> Frame: GetTransformSpan()
  Workers -> Frame: GetVisibilitySpan()
  Workers -> Workers: Process entities 0-1000
end

par Worker 2
  Workers -> Frame: GetTransformSpan()
  Workers -> Frame: GetVisibilitySpan()
  Workers -> Workers: Process entities 1000-2000
end

par Worker N
  Workers -> Frame: GetTransformSpan()
  Workers -> Frame: GetVisibilitySpan()
  Workers -> Workers: Process entities N*1000-(N+1)*1000
end

Workers --> Engine: Return RenderItemData[]
deactivate Workers

note over Engine, Workers: All workers access same\nimmutable snapshot data\nwithout synchronization
@enduml
```

## Implementation Strategy

### Migration Path

1. **Study existing systems** - Understand `RenderItemData`, `PreparedSceneFrame`, phase execution
2. **Enhance ScenePrep** to work with `FrameSnapshot` during `kParallelTasks` phase
3. **Create Resources module** to process `RenderItemData` and create `PreparedSceneFrame`
4. **Integrate with RenderGraph** to consume `PreparedSceneFrame` via `RenderContext`
5. **Remove Manager classes** once new pipeline is working
6. **Optimize parallel execution** with proper batching and worker coordination

### Module Interfaces

**ScenePrepCoordinator:**

```cpp
class ScenePrepCoordinator {
public:
  // Called during kSnapshot phase with FrameContext access
  auto PrepareSceneData(
    FrameContext& frame_context,
    const CameraParameters& camera,
    const CullingVolumes& culling
  ) -> void;  // Mutates frame_context GameState directly

private:
  EntityExtractor entity_extractor_;
  LodSelector lod_selector_;
  VisibilityTester visibility_tester_;
  RenderItemEmitter render_item_emitter_;
  MaterialFilter material_filter_;
  SortingWorker sorting_worker_;
};
```

**ResourceCoordinator:**

```cpp
class ResourceCoordinator {
public:
  // Called during kPostParallel phase with FrameContext snapshot access
  auto ProcessPreparedSceneData(
    const FrameContext& frame_context  // Accesses immutable snapshots
  ) -> PreparedSceneFrame;

  auto UploadPendingResources() -> void;
  auto UpdateBindlessTables() -> void;

private:
  GeometryUploader geometry_uploader_;
  MaterialBinder material_binder_;
  TransformUploader transform_uploader_;
  HandleAllocator handle_allocator_;
  ResidencyTracker residency_tracker_;
  DescriptorManager descriptor_manager_;
};
```

## Benefits of This Architecture

### Proper Phase Integration

- **Aligns with engine frame lifecycle** defined in `PhaseRegistry.h`
- **Supports snapshotting** for safe parallel task execution
- **Maintains phase mutation restrictions** (no GameState mutation during parallel)
- **Uses existing coordination infrastructure** instead of reinventing

### Leverages Existing Systems

- **Built on actual data structures** (`RenderItemData`, `PreparedSceneFrame`, `RenderContext`)
- **Uses existing RenderGraph infrastructure** with `TaskExecutionContext` and `PassExecutor`
- **Integrates with Graphics ResourceRegistry** for GPU operations
- **Maintains asset loading and scene systems** without disruption

### Eliminates Manager Proliferation

- **One coordinator per module** with clear architectural purpose
- **Workers handle specific tasks** within module boundaries
- **No artificial management layers** or bridge/adapter classes

### Enables Advanced Rendering

- **Proper render graph execution** with existing pass infrastructure
- **Resource dependency tracking** between passes
- **Parallel command recording** during `kCommandRecord` phase
- **Foundation for multi-view rendering** scenarios

This architecture provides a clean integration with the existing engine while enabling modern rendering techniques through proper phase coordination and data flow.

```text
Scene + View → ScenePrep → RenderItemData[] → Resources → RenderCommand[] → RenderGraph → Output
                    ↓                              ↓
               per-frame data               persistent GPU resources
```

### Detailed Data Contracts

#### ScenePrep → Resources

```cpp
struct RenderItemData {
    // Asset references (NOT handles)
    std::shared_ptr<const GeometryAsset> geometry;
    std::shared_ptr<const MaterialAsset> material;

    // Transform data
    glm::mat4 world_transform;

    // Rendering metadata
    uint32_t lod_index;
    uint32_t submesh_index;
    bool cast_shadows;
    bool receive_shadows;
};
```

#### Resources → RenderGraph

```cpp
struct RenderCommand {
    // Stable shader indices
    uint32_t vertex_buffer_index;
    uint32_t index_buffer_index;
    uint32_t material_buffer_index;
    uint32_t transform_buffer_index;

    // Draw parameters
    uint32_t index_count;
    uint32_t instance_count;
    uint32_t first_index;
    uint32_t vertex_offset;

    // Render state
    PassMask pass_mask;
    bool cast_shadows;
    bool receive_shadows;
};
```

## Directory Structure

```text
src/Oxygen/Renderer/
├── ScenePrep/                          # Scene data extraction
│   ├── ExtractionPipeline.h                # Scene traversal orchestration
│   ├── VisibilityExtractor.h               # Visibility and flag extraction
│   ├── LODSelector.h                       # Level of detail selection
│   ├── FrustumCuller.h                     # Frustum culling
│   ├── ItemEmitter.h                       # RenderItemData emission
│   ├── RenderItemData.h                    # Extraction output data
│   └── ExtractionTypes.h                   # ScenePrep-specific types
│
├── Resources/                          # GPU resource coordination
│   ├── ResourceCoordinator.h               # Resource lifecycle coordinator (MANAGER)
│   ├── Geometry/
│   │   ├── GeometryCache.h                 # Geometry asset → GPU buffer mapping
│   │   ├── VertexBufferCache.h             # Vertex buffer management
│   │   └── IndexBufferCache.h              # Index buffer management
│   ├── Materials/
│   │   ├── MaterialCache.h                 # Material asset → constant buffer mapping
│   │   ├── ConstantBufferPool.h            # Material constant buffer pool
│   │   └── TextureCache.h                  # Material texture management
│   ├── Transforms/
│   │   ├── TransformBuffer.h               # Transform matrix GPU buffer
│   │   └── InstanceDataBuffer.h            # Per-instance data buffer
│   ├── Core/
│   │   ├── ShaderIndexAllocator.h          # Stable shader index allocation
│   │   ├── ContentHasher.h                 # Asset content hashing
│   │   ├── AliasingResolver.h              # Resource aliasing resolution
│   │   └── LifetimeTracker.h               # Resource reference counting
│   └── RenderCommand.h                     # Resource output data
│
├── Passes/                              # Render passes (coroutine-based graph)
│   ├── DepthPrePass.h
│   ├── ShaderPass.h
│   └── TransparentPass.h
│
├── Upload/                             # GPU upload coordination
│   ├── UploadCoordinator.h                 # Upload coordination (MANAGER)
│   ├── UploadBatcher.h                     # Upload batching
│   ├── StagingAllocator.h                  # Staging buffer allocation
│   ├── TransferQueue.h                     # GPU transfer queue
│   └── CompletionTracker.h                 # Upload completion tracking
│
└── Core/                               # Top-level coordination
    ├── Renderer.h                          # Main renderer interface (coroutine graph)
    └── RendererConfig.h                    # Renderer configuration
```

## Resource Lifecycle Algorithms

### Content Deduplication Algorithm

**Location:** `Resources/Core/ContentHasher.h`

1. **Content Hash Generation:** Hash asset content (vertex data, material params, matrices)
2. **Collision Resolution:** Secondary hash or content comparison for hash conflicts
3. **Reference Tracking:** Count logical resources referencing each unique content
4. **Aliasing Decision:** Map multiple logical assets to single shader index

```pseudocode
ContentHash = Hash(AssetContent)
if ContentCache.contains(ContentHash):
    return existing_shader_index  // Alias to existing resource
else:
    shader_index = ShaderIndexAllocator.Allocate()
    ContentCache[ContentHash] = shader_index
    return shader_index
```

### Resource Aliasing Algorithm

**Location:** `Resources/Core/AliasingResolver.h`

1. **Logical Independence:** Each asset reference gets unique logical handle
2. **Physical Sharing:** Multiple logical handles map to same shader index
3. **Update Propagation:** Content changes update all aliased shader indices
4. **Transparent Aliasing:** Callers see stable handles, aliasing handled internally

### Lifetime Management Algorithm

**Location:** `Resources/Core/LifetimeTracker.h`

1. **Reference Counting:** Track active references per shader index
2. **Eviction Policy:** Priority-based eviction (geometry > materials > transforms)
3. **Graceful Degradation:** LOD reduction before eviction
4. **Index Recycling:** Reuse shader indices with generation safety

## PlantUML Diagrams

### Module Collaboration Diagram

```plantuml
@startuml ModuleCollaboration
!define RECTANGLE class

package "Oxygen Renderer" {
    rectangle ScenePrep {
        +ExtractionPipeline
        +VisibilityExtractor
        +LODSelector
        +FrustumCuller
        +ItemEmitter
    }

    rectangle Resources {
        +ResourceCoordinator
        +GeometryCache
        +MaterialCache
        +TransformBuffer
        +ShaderIndexAllocator
    }

    rectangle RenderGraph {
        +RenderGraphExecutor
        +ForwardPass
        +DeferredGBufferPass
  +ShadowMapPass
    }

    rectangle Upload {
        +UploadCoordinator
        +UploadBatcher
        +StagingAllocator
        +TransferQueue
    }
}

package "External Systems" {
    rectangle Scene
    rectangle Content
    rectangle Graphics
}

Scene --> ScenePrep : Scene nodes
Content --> Resources : Asset data
ScenePrep --> Resources : RenderItemData[]
Resources --> RenderGraph : RenderCommand[]
Resources --> Upload : Upload requests
RenderGraph --> Graphics : GPU commands
Upload --> Graphics : Buffer transfers

@enduml
```

### Frame Execution Sequence

```plantuml
@startuml FrameExecution
participant "FrameCoordinator" as FC
participant "ScenePrep" as SP
participant "Resources" as R
participant "GameState (FrameContext)" as GS
participant "FrameSnapshot" as FS
participant "Upload" as U
participant "RenderGraph" as RG

== kSnapshot ==
FC -> SP: Extract(scene, view)
activate SP
SP -> SP: Traverse scene nodes
SP -> SP: Apply filters, LOD, culling
SP -> SP: Emit RenderItemData
SP --> GS: Contribute RenderItemData
deactivate SP

R -> GS: Read GameStateSnapshot
R -> R: Finalize arrays for this frame
R -> GS: SetRendererSnapshotViews(renderer_snapshot)
FC -> GS: PublishSnapshots(EngineTag)
GS --> FS: Publish GameStateSnapshot + renderer_snapshot views

== kParallelTasks ==
note over FS: Workers read immutable snapshots only

== kPostParallel ==
activate R
R -> U: Schedule uploads
deactivate R

== kCommandRecord ==
FC -> RG: Execute passes (RenderContext.prepared_frame)
activate RG
RG -> RG: Execute forward pass
RG -> RG: Execute shadow pass
RG -> RG: Execute post-process
RG --> FC: Rendered output
deactivate RG

@enduml
```

### Resource Management Flow

```plantuml
@startuml ResourceManagement
participant "ResourceCoordinator" as RC
participant "GeometryCache" as GC
participant "MaterialCache" as MC
participant "TransformBuffer" as TB
participant "ShaderIndexAllocator" as SIA
participant "ContentHasher" as CH

RC -> GC: RegisterGeometry(asset)
activate GC
GC -> CH: Hash(vertex_data, index_data)
CH --> GC: content_hash
GC -> SIA: AllocateIndex(content_hash)
SIA --> GC: shader_index
GC --> RC: shader_index
deactivate GC

RC -> MC: RegisterMaterial(asset)
activate MC
MC -> CH: Hash(material_params)
CH --> MC: content_hash
MC -> SIA: AllocateIndex(content_hash)
SIA --> MC: shader_index
MC --> RC: shader_index
deactivate MC

RC -> TB: RegisterTransform(matrix)
activate TB
TB -> CH: Hash(matrix_data)
CH --> TB: content_hash
TB -> SIA: AllocateIndex(content_hash)
SIA --> TB: shader_index
TB --> RC: shader_index
deactivate TB

@enduml
```

## Key Design Benefits

### 1. Clear Worker vs Coordinator Distinction

- **4 Coordinators Total**: ResourceCoordinator, RenderGraphExecutor, UploadCoordinator, FrameCoordinator
- **All Others Are Workers**: Focused, single-responsibility components
- **No Manager Proliferation**: Only true coordination components are managers

### 2. Explicit Data Contracts

- **Type-Safe Interfaces**: Strong typing between module boundaries
- **Clear Ownership**: Each data structure has clear ownership semantics
- **Minimal Coupling**: Modules only depend on data contracts, not implementations

### 3. Efficient Resource Management

- **Content Deduplication**: Identical assets share GPU resources automatically
- **Stable Shader Indices**: Shader code sees consistent indices across frames
- **Batched Uploads**: Cross-module upload coordination for optimal GPU utilization

### 4. Extensible Architecture

- **New Pass Types**: Easy to add new render passes to RenderGraph
- **New Resource Types**: Easy to extend Resources module with new asset types
- **New Extraction Stages**: Easy to add new extraction steps to ScenePrep
