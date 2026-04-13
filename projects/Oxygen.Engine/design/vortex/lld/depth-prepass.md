# Depth Prepass Module LLD

**Phase:** 3 — Deferred Core
**Deliverable:** D.4
**Status:** `ready`

## 1. Scope and Context

### 1.1 What This Covers

`DepthPrepassModule` — the stage-3 owner responsible for the depth-only
rendering pass, partial velocity writes for static geometry, and
`SceneDepth`/`PartialDepth` product population. This is the first GPU pass
in the Vortex frame that writes to render targets.

### 1.2 Stage Position

| Position | Stage | Notes |
| -------- | ----- | ----- |
| Predecessor | Stage 2 (InitViews) | Current-view prepared-scene payload published |
| **This** | **Stage 3 — DepthPrepass** | Depth-only + partial velocity |
| Successor | Stage 5 (Occlusion/HZB — reserved) or Stage 9 (BasePass) | |

### 1.3 Architectural Authority

- [ARCHITECTURE.md §6.2](../ARCHITECTURE.md) — stage table, row 3
- [ARCHITECTURE.md §6.3.1](../ARCHITECTURE.md) — deferred-core invariants
- [ARCHITECTURE.md §7.3.2](../ARCHITECTURE.md) — SceneDepth, PartialDepth products
- [ARCHITECTURE.md §5.1.3](../ARCHITECTURE.md) — distributed velocity writes
- UE5 reference: `RenderPrePass` family (~1.35 k lines)

### 1.4 Required Invariants For This Module

This module must preserve the following invariants from
[ARCHITECTURE.md §6.3.1](../ARCHITECTURE.md):

- `SceneRenderer` owns per-view iteration; `DepthPrepassModule::Execute(...)`
  consumes the current view only
- velocity remains the stage-3 partial contribution within the distributed
  3/9/19 ownership model
- masked opaque participation here is depth-policy work only; it does not
  remove masked materials from the deferred opaque/base-pass contract

## 2. Interface Contracts

### 2.1 File Placement

Per [PROJECT-LAYOUT.md](../PROJECT-LAYOUT.md):

```text
src/Oxygen/Vortex/
└── SceneRenderer/
    └── Stages/
        └── DepthPrepass/
            ├── DepthPrepassModule.h
            ├── DepthPrepassModule.cpp
            ├── DepthPrepassMeshProcessor.h
            └── DepthPrepassMeshProcessor.cpp
```

### 2.2 Public API

```cpp
namespace oxygen::vortex {

/// Configuration for the depth prepass stage.
struct DepthPrepassConfig {
  DepthPrePassMode mode{DepthPrePassMode::kOpaqueAndMasked};
  bool write_velocity{true};   // Write partial velocity for static geometry
};

class DepthPrepassModule {
 public:
  explicit DepthPrepassModule(Renderer& renderer,
                               const SceneTexturesConfig& config);
  ~DepthPrepassModule();

  DepthPrepassModule(const DepthPrepassModule&) = delete;
  auto operator=(const DepthPrepassModule&) -> DepthPrepassModule& = delete;

  /// Stage 3 entry point. Per-view execution.
  void Execute(RenderContext& ctx, SceneTextures& scene_textures);

  /// Runtime policy control.
  void SetConfig(const DepthPrepassConfig& config);

  /// Query completeness state after execution.
  [[nodiscard]] auto GetCompleteness() const -> DepthPrePassCompleteness;

 private:
  Renderer& renderer_;
  DepthPrepassConfig config_;
  DepthPrePassCompleteness completeness_{DepthPrePassCompleteness::kDisabled};

  std::unique_ptr<DepthPrepassMeshProcessor> mesh_processor_;
};

}  // namespace oxygen::vortex
```

### 2.3 DepthPrePassPolicy Integration

Reuses the Vortex policy types defined in `SceneRenderer/DepthPrePassPolicy.h`
(migrated in Phase 1):

```cpp
// Already exists in SceneRenderer/DepthPrePassPolicy.h
enum class DepthPrePassMode : std::uint8_t {
  kDisabled,
  kOpaqueAndMasked,
};

enum class DepthPrePassCompleteness : std::uint8_t {
  kDisabled,
  kIncomplete,
  kComplete,
};
```

- `kDisabled`: Stage 3 is a no-op. Downstream must not rely on SceneDepth.
- `kOpaqueAndMasked`: Full opaque + masked geometry depth pass.
- `kIncomplete`: Prepass ran but not all geometry was covered (e.g., if
  a future policy intentionally excludes a participating opaque class).
- `kComplete`: All opaque geometry has valid depth, downstream can skip
  depth writes in BasePass.

### 2.4 Ownership and Lifetime

| Owner | Owned By | Lifetime |
| ----- | -------- | -------- |
| `DepthPrepassModule` | `SceneRenderer` (unique_ptr) | Same as SceneRenderer |
| `DepthPrepassMeshProcessor` | `DepthPrepassModule` (unique_ptr) | Same as module |
| PSO / root signature | Renderer cache | Persistent (shared) |

## 3. Data Flow and Dependencies

### 3.1 Inputs

| Source | Data | Purpose |
| ------ | ---- | ------- |
| InitViewsModule (stage 2) | Current-view `PreparedSceneFrame` payload | Determines what to draw without scene re-traversal |
| SceneTextures | SceneDepth DSV | Render target |
| SceneTextures | Velocity UAV (optional) | Partial velocity output |
| Renderer | Depth-only PSO cache | Pipeline state |
| GeometryUploader | Vertex/index buffer bindings | Mesh data |

### 3.2 Outputs

| Product | Format | Written To |
| ------- | ------ | ---------- |
| SceneDepth | D32_FLOAT_S8X24_UINT (DSV) | SceneTextures::GetSceneDepth() |
| PartialDepth | Copy of SceneDepth post-pass | SceneTextures::GetPartialDepth() |
| Velocity (partial) | R16G16_FLOAT | SceneTextures::GetVelocity() — static only |

### 3.3 SceneTextures State Transitions

```text
Before stage 3:
  SceneDepth     = allocated, cleared
  PartialDepth   = allocated, undefined
  Velocity       = allocated, cleared (if write_velocity)

After stage 3:
  SceneDepth     = written (opaque + masked depth)
  PartialDepth   = copy of SceneDepth (extraction product)
  Velocity       = partially written (static geometry only)
  SetupMode flag = SCENE_TEXTURE_FLAG_SCENE_DEPTH set
                   SCENE_TEXTURE_FLAG_PARTIAL_DEPTH set
                   SCENE_TEXTURE_FLAG_VELOCITY partially valid
```

### 3.4 Execution Flow

```text
DepthPrepassModule::Execute(ctx, scene_textures)
  │
  ├─ if config_.mode == kDisabled → return
  │
  ├─ Read current view prepared-scene payload from ctx
  │     │
  │     ├─ Set render targets:
  │     │     DSV = scene_textures.GetSceneDepth() (depth-stencil view)
  │     │     No color RTs bound (depth-only pass)
  │     │
  │     ├─ mesh_processor_->BuildDrawCommands(current_view_prepared_scene, include_masked)
  │     │     Consume published prepared-scene ranges / items
  │     │     Filter/refine: opaque + masked participants only
  │     │     Sort/refine: front-to-back within the prepared-scene contract
  │     │
  │     ├─ for each draw command:
  │     │     ├─ Bind depth-only PSO (VS only, no PS for opaque)
  │     │     ├─ Bind vertex/index buffers from GeometryUploader
  │     │     └─ DrawIndexedInstanced(...)
  │     │
  │     ├─ if config_.mode == kOpaqueAndMasked:
  │     │     ├─ Masked geometry sub-pass (with alpha-test PS)
  │     │     └─ Participates in the same opaque depth contract as masked
  │     │        materials later do in BasePass
  │     │
  │     └─ if config_.write_velocity:
  │           └─ Write zero-velocity for static geometry to Velocity buffer
  │
  ├─ Copy SceneDepth → PartialDepth (resource copy)
  │
  ├─ SceneRenderer updates SceneTextures setup mode flags and refreshes
  │  shared routing metadata after this stage boundary
  │
  └─ completeness_ = kComplete (or kIncomplete if masked fails)
```

## 4. Resource Management

### 4.1 GPU Resources

| Resource | Type | Lifetime | Notes |
| -------- | ---- | -------- | ----- |
| SceneDepth texture | Owned by SceneTextures | Per frame | This module writes, does not allocate |
| PartialDepth texture | Owned by SceneTextures | Per frame | Copy target |
| Velocity texture | Owned by SceneTextures | Per frame | Partial write target |
| Depth-only PSO | Renderer PSO cache | Persistent | Shared across frames |
| Masked-depth PSO | Renderer PSO cache | Persistent | With alpha-test PS |

### 4.2 Render Target Binding

```text
Depth-only sub-pass:
  OM: DSV = SceneDepth (DEPTH_WRITE)
      RTV = none

Masked sub-pass (if kOpaqueAndMasked):
  OM: DSV = SceneDepth (DEPTH_WRITE)
      RTV = none (alpha test in PS, discard if < threshold)

Velocity sub-pass (if write_velocity):
  OM: DSV = SceneDepth (DEPTH_READ, stencil for velocity mask)
      UAV = Velocity (R16G16_FLOAT, partial write)
```

## 5. Shader Contracts

### 5.1 Depth-Only Vertex Shader

Uses `VortexDepthPrepassVS` registered in EngineShaderCatalog.

```hlsl
// Stages/DepthPrepass/DepthPrepass.hlsl

struct DepthPrepassVSInput {
  float3 position : POSITION;
  uint instanceId : SV_InstanceID;
};

struct DepthPrepassVSOutput {
  float4 clipPosition : SV_Position;
#ifdef HAS_VELOCITY
  float3 prevClipPosition : PREV_POSITION;
#endif
};

DepthPrepassVSOutput DepthPrepassVS(DepthPrepassVSInput input) {
  DepthPrepassVSOutput output;
  // Load instance world matrix from bindless structured buffer
  float4x4 worldMatrix = LoadInstanceTransform(input.instanceId);
  float4 worldPos = mul(worldMatrix, float4(input.position, 1.0));
  output.clipPosition = mul(ViewProjection, worldPos);

#ifdef HAS_VELOCITY
  float4x4 prevWorldMatrix = LoadPrevInstanceTransform(input.instanceId);
  float4 prevWorldPos = mul(prevWorldMatrix, float4(input.position, 1.0));
  output.prevClipPosition = mul(PrevViewProjection, prevWorldPos).xyz;
#endif

  return output;
}
```

### 5.2 Masked Alpha-Test Pixel Shader

```hlsl
// Same file, separate entrypoint
void DepthPrepassPS(DepthPrepassVSOutput input) {
  // Load material alpha from bindless texture
  float alpha = SampleMaterialAlpha(input.clipPosition.xy);
  if (alpha < 0.5) discard;
}
```

### 5.3 Permutations

| Define | Values | Purpose |
| ------ | ------ | ------- |
| `HAS_VELOCITY` | 0/1 | Catalog-managed permutation family for velocity-enabled vs non-velocity variants |

### 5.4 Catalog Registration

```cpp
// One registered vertex entrypoint with catalog-managed HAS_VELOCITY
// permutation family, plus masked alpha-test PS entrypoint.
// VortexDepthPrepassVS
// VortexDepthPrepassPS
```

## 6. Mesh Processor

### 6.1 DepthPrepassMeshProcessor

```cpp
namespace oxygen::vortex {

class DepthPrepassMeshProcessor {
 public:
  explicit DepthPrepassMeshProcessor(Renderer& renderer);

  /// Build depth-only draw commands from the current view's prepared-scene
  /// payload. Helper visibility lists, if any, are local projections over the
  /// prepared frame rather than a second authoritative input contract.
  /// Outputs: sorted draw commands for opaque geometry.
  void BuildDrawCommands(
      const PreparedSceneFrame& prepared_scene,
      bool include_masked);

  /// Access built commands.
  [[nodiscard]] auto GetDrawCommands() const
      -> std::span<const DrawCommand>;

 private:
  Renderer& renderer_;
  std::vector<DrawCommand> draw_commands_;
};

}  // namespace oxygen::vortex
```

### 6.2 Draw Command Structure

```cpp
struct DrawCommand {
  uint32_t index_count;
  uint32_t instance_count;
  uint32_t start_index;
  int32_t base_vertex;
  uint32_t start_instance;
  uint32_t pso_index;          // Index into PSO cache
  uint32_t vb_handle;          // Geometry upload handle
  uint32_t ib_handle;          // Index buffer handle
};
```

## 7. Stage Integration

### 7.1 Dispatch Contract

SceneRenderer calls `DepthPrepassModule::Execute(ctx, scene_textures)` at
stage 3. The module:

1. Checks policy (disabled → early return)
2. Reads the current view's published `PreparedSceneFrame` from RenderContext
3. Issues GPU draw commands
4. Copies depth extract
5. Sets completeness state

### 7.2 Null-Safe Behavior

When `depth_prepass_` is null: no SceneDepth write occurs, PartialDepth
remains undefined, BasePass must write depth itself (early_z_pass_done =
false).

### 7.3 Capability Gate

Requires `kScenePreparation` + `kDeferredShading`. Always instantiated in
Phase 3 when SceneRenderer is active.

## 8. Testability Approach

1. **Unit test:** Mock renderer + known geometry → verify draw command count
   and sort order from DepthPrepassMeshProcessor.
2. **Integration test:** Run InitViews → DepthPrepass. Read back SceneDepth
   texture. Verify non-zero depth values correspond to visible geometry
   positions.
3. **RenderDoc validation:** At frame 10, inspect SceneDepth after stage 3.
   Verify correct depth values, no color writes, and that PartialDepth is
   a bitwise copy.
4. **Policy test:** Set mode=kDisabled, verify SceneDepth remains cleared
   (all 1.0) and completeness=kDisabled.

## 9. Open Questions

1. **Static-geometry velocity path:** UE 5.7 treats depth-prepass and velocity
   as deliberately split concerns with policy-dependent coupling. Vortex Phase 3
   currently keeps the correctness-first contract of writing only the partial
   static contribution here, but the exact single-pass vs split-pass realization
   should stay aligned with the chosen `DepthPrePassPolicy` implementation.
