# Base Pass Module LLD

**Phase:** 3 — Deferred Core
**Deliverable:** D.5
**Status:** `ready`

## Mandatory Vortex Rule

- For Vortex planning and implementation, `Oxygen.Renderer` is legacy dead
  code. It is not production, not a reference implementation, not a fallback,
  and not a simplification path for any Vortex task.
- Every Vortex task must be designed and implemented as a new Vortex-native
  system that targets maximum parity with UE5.7, grounded in
  `F:\Epic Games\UE_5.7\Engine\Source\Runtime` and
  `F:\Epic Games\UE_5.7\Engine\Shaders`.
- No Vortex task may be marked complete until its parity gate is closed with
  explicit evidence against the relevant UE5.7 source and shader references.
- If maximum parity cannot yet be achieved, the task remains incomplete until
  explicit human approval records the accepted gap and the reason the parity
  gate cannot close.

## 1. Scope and Context

### 1.1 What This Covers

`BasePassModule` is the stage-9 owner responsible for:

- masked/opaque deferred base-pass drawing
- GBuffer MRT writes
- emissive accumulation into `SceneColor`
- the active desktop deferred opaque-velocity policy
- truthful masked/deformed/skinned/WPO-capable velocity production

Phase 3 implements deferred mode only. Forward mode remains a future extension.

### 1.2 Stage Position

| Position | Stage | Notes |
| -------- | ----- | ----- |
| Predecessor | Stage 3 (DepthPrepass) — depth-only under the active opaque-velocity policy | |
| Predecessors (reserved) | Stages 4-8 (occlusion, light grid, shadows — stubs) | |
| **This** | **Stage 9 — BasePass** | GBuffer MRT + opaque velocity production |
| Successor | Stage 10 (RebuildSceneTextures) — state transition | |

### 1.3 Architectural Authority

- [ARCHITECTURE.md §5.1.3](../ARCHITECTURE.md) — UE family mapping
- [ARCHITECTURE.md §6.2](../ARCHITECTURE.md) — runtime stage table
- [ARCHITECTURE.md §6.3.1](../ARCHITECTURE.md) — deferred-core invariants
- [ARCHITECTURE.md §7.3.3](../ARCHITECTURE.md) — setup milestones
- [PLAN.md §5](../PLAN.md) — Phase 3 work items and exit gate
- [sceneprep-refactor.md](sceneprep-refactor.md) — prepared-scene publication contract
- UE5 reference: `RenderBasePass`, `BasePassVertexShader.usf`, `BasePassPixelShader.usf`

### 1.4 Physics Boundary

This LLD is about renderer-owned motion vectors, not simulation-owned physics
velocity.

| Concern | Owner | Meaning |
| ------- | ----- | ------- |
| `SceneVelocity` | Vortex renderer | Screen-space motion-vector texture used by temporal rendering, post processing, and capture validation |
| linear/angular body velocity | `src/Oxygen/Physics` + `src/Oxygen/PhysicsModule` | Simulation-space rigid/character state and solver inputs/outputs |

Rules:

1. `BasePassModule` must never call Physics-domain APIs directly.
2. Physics authority reaches Vortex only through scene/animation/deformation
   state that has already been reconciled before rendering.
3. Vortex history caches store render-consumable transforms/deformation inputs,
   not physics handles or backend-native physics state.
4. `SceneVelocity` must never be described as a physics product in docs or
   code comments.

### 1.5 Required Invariants

This module must preserve the following invariants from
[ARCHITECTURE.md §6.3.1](../ARCHITECTURE.md):

- `SceneRenderer` owns per-view iteration; `BasePassModule::Execute(...)`
  consumes the current view only
- masked opaque materials remain part of the deferred opaque contract and must
  alpha-clip truthfully before GBuffer/velocity writes
- stage 10 remains the SceneRenderer-owned
  `PublishDeferredBasePassSceneTextures(ctx)` boundary, which invokes
  `RebuildWithGBuffers()` and then refreshes routing/publication state
- the active desktop deferred opaque-velocity policy is stage-9 base-pass
  velocity
- no bool-only completion claim is allowed; velocity publication must be
  output-backed

## 2. Ownership Model

### 2.1 File Placement

Per [PROJECT-LAYOUT.md](../PROJECT-LAYOUT.md):

```text
src/Oxygen/Vortex/
└── SceneRenderer/
    └── Stages/
        └── BasePass/
            ├── BasePassModule.h
            ├── BasePassModule.cpp
            ├── BasePassMeshProcessor.h
            └── BasePassMeshProcessor.cpp
```

### 2.2 Cross-Frame Owners

| State | Owner | Why |
| ----- | ----- | --- |
| previous rigid transform history | renderer-owned motion-history cache keyed by `scene::NodeHandle` | identity-stable across frames; not allocation-order coupled |
| previous deformation history | renderer-owned deformation-history cache using instance identity for current producer state and LOD-aware render identity for publications/history | authoritative previous skinned/morph/WPO-capable deformation state with explicit invalidation, LOD-aware publication, and stale trimming |
| previous view / projection / stable projection / jitter | `Renderer` per published runtime view identity | same lifetime as published runtime views |
| current/previous transform GPU arrays | `TransformUploader`-class publication helpers | upload layer only, not authority owner |
| current/previous deformation GPU arrays | deformation-history publication helpers fed by the renderer-owned deformation cache | upload layer only; not the lifetime owner |
| stage-local PSO/framebuffer state | `BasePassModule` | stage-local cached runtime state |

### 2.4 History Lifetime Rules

Required ownership rules:

1. `PrimitiveRigidMotionHistoryCache`
   - key: `NodeHandle`
   - stores current/previous rigid transforms plus frame-seen/update stamps
2. `PrimitiveDeformationHistoryCache`
   - current-state source identity:
     - `NodeHandle`
     - producer family
     - deformation contract hash
   - renderer publication/history identity:
     - `NodeHandle`
     - geometry asset key
     - LOD index
     - submesh index
     - producer family
     - deformation contract hash
   - stores current/previous deformation payload publications plus frame-seen
     and frame-updated stamps

Required invalidation rules:

- rigid history invalidates on node destruction or scene change
- deformation history invalidates on:
  - geometry identity change
  - skeleton topology / skinning stream layout change
  - morph/deformation layout change
  - material deformation contract change (including WPO contract changes)

Required roll/trim rules:

- current -> previous rolls once per successful frame
- stale entries trim by seen-frame age
- history must not be updated per view
- one frame may legitimately materialize multiple renderer publication/history
  identities for the same current-state source identity when active views pick
  different LODs

### 2.3 Public API Shape

The runtime contract should move away from boolean seams.

```cpp
namespace oxygen::vortex {

enum class OpaqueVelocityPolicy : std::uint8_t {
  kDisabled,
  kBasePass,     // active Phase-3 desktop deferred policy
  kDepthPass,    // future policy option
  kSeparatePass, // future policy option
};

enum class OpaqueVelocityCoverage : std::uint8_t {
  kNone,
  kOpaqueFullParity,
};

struct BasePassConfig {
  OpaqueVelocityPolicy velocity_policy{OpaqueVelocityPolicy::kBasePass};
  bool early_z_pass_done{true};
  ShadingMode shading_mode{ShadingMode::kDeferred};
};

struct BasePassExecutionResult {
  bool published_base_pass_products{false};
  bool bound_velocity_target{false};
  uint32_t eligible_velocity_draw_count{0};
  uint32_t emitted_velocity_draw_count{0};
  OpaqueVelocityCoverage velocity_coverage{OpaqueVelocityCoverage::kNone};
};

class BasePassModule {
 public:
  void Execute(RenderContext& ctx, SceneTextures& scene_textures);
  void SetConfig(const BasePassConfig& config);
  [[nodiscard]] auto GetLastResult() const -> const BasePassExecutionResult&;
};

}  // namespace oxygen::vortex
```

`HasCompletedVelocityForDynamicGeometry()` is not an acceptable final contract.

## 3. Publication Seams Required Before Stage 9

### 3.1 Prepared-Scene Inputs

`PreparedSceneFrame` must publish explicit current/previous motion inputs.

Required publication families:

- current world transforms
- previous world transforms
- current normal matrices
- current deformation data
- previous deformation data
- draw-metadata / partitions
- per-draw velocity eligibility metadata

Illustrative shape:

```cpp
struct PreparedSceneFrame {
  // Existing prepared-scene arrays...

  std::span<const float> current_world_matrices;
  std::span<const float> previous_world_matrices;
  std::span<const float> current_normal_matrices;

  std::span<const std::byte> current_deformation_bytes;
  std::span<const std::byte> previous_deformation_bytes;

  ShaderVisibleIndex bindless_current_worlds_slot;
  ShaderVisibleIndex bindless_previous_worlds_slot;
  ShaderVisibleIndex bindless_current_normals_slot;
  ShaderVisibleIndex bindless_current_deformation_slot;
  ShaderVisibleIndex bindless_previous_deformation_slot;
};
```

### 3.2 Draw-Binding Inputs

`DrawFrameBindings` must carry explicit current/previous slots rather than one
overloaded `transforms_slot`.

Illustrative shape:

```cpp
struct DrawFrameBindings {
  uint draw_metadata_slot;
  uint current_worlds_slot;
  uint previous_worlds_slot;
  uint current_normals_slot;
  uint current_deformation_slot;
  uint previous_deformation_slot;
  uint material_shading_constants_slot;
  uint instance_data_slot;
};
```

### 3.3 Per-Draw Velocity Flags

Velocity classification must live in draw metadata or a keyed auxiliary payload,
not be reconstructed from `render_items[draw_index]`.

Required per-draw truth:

- `kOutputVelocity`
- `kIsMasked`
- `kHasPreviousTransform`
- `kHasPreviousDeformation`
- `kUsesWorldPositionOffset`
- `kUsesPreviousWorldPositionOffset`
- `kHasPixelAnimation`
- `kUsesTemporalResponsiveness`
- `kUsesMotionVectorWorldOffset`

If a draw lacks the required previous-frame input for a declared producer class,
the implementation is incomplete and must not claim parity.

### 3.4 View Constants

`ViewConstants` must provide current and previous view data for the whole frame.

Required fields:

- current view matrix
- current projection matrix
- current stable projection matrix
- current inverse view-projection
- previous view matrix
- previous projection matrix
- previous stable projection matrix
- previous inverse view-projection
- current and previous pixel jitter

Required lifetime rules:

- key: published runtime view identity
- roll point: end of successful frame after scene rendering completes
- invalidate on:
  - first frame for the logical view
  - logical-view recreation / republishing under a new identity
  - resize / view-rect change
  - camera cut
  - projection or stable-projection discontinuity beyond the configured epsilon
  - scene switch for the view
- invalid previous-view history seeds previous = current and requires zero /
  fallback camera-motion velocity for that frame

## 4. GBuffer + Velocity MRT Layout

### 4.1 Render Target Configuration

| RT Slot | Target | Content | Format |
| ------- | ------ | ------- | ------ |
| SV_Target0 | GBufferNormal | Encoded world normal | R10G10B10A2_UNORM |
| SV_Target1 | GBufferMaterial | Metallic, specular, roughness, shading model ID | R8G8B8A8_UNORM |
| SV_Target2 | GBufferBaseColor | Base color RGB, AO | R8G8B8A8_SRGB |
| SV_Target3 | GBufferCustomData | Custom data | R8G8B8A8_UNORM |
| SV_Target4 | SceneColor | Emissive accumulation | R16G16B16A16_FLOAT |
| **SV_Target5** | **Velocity** | **Encoded screen-space motion vectors** | **R16G16_FLOAT (or final engine velocity encoding target)** |
| DS | SceneDepth | Depth/stencil | D32_FLOAT_S8X24_UINT |

Under `OpaqueVelocityPolicy::kBasePass`, stage 9 binds the velocity target as a
real MRT. No fake “completion” path is allowed.

### 4.2 Clear / State Rules

- If `early_z_pass_done == true`: depth test enabled, depth write disabled.
- If `early_z_pass_done == false`: depth test and depth write both enabled.
- Stage 9 owns the velocity clear under the active base-pass velocity policy.
- `SceneVelocity` must be `RENDER_TARGET` during stage 9 and
  `SHADER_RESOURCE` after the pass.

### 4.3 Motion-Vector-World-Offset Auxiliary Path

UE-grade parity requires an explicit auxiliary path for materials that declare
motion-vector world offset semantics.

Contract:

- owner: `BasePassModule` as part of the stage-9 opaque velocity family
- transient resource owner: stage-local transient texture
  `VelocityMotionVectorWorldOffset`
- shader family owner: stage-9 base-pass velocity family
- publication point: only after the auxiliary merge/update step completes

Stage-9 internal order:

1. primary GBuffer + velocity MRT pass
2. optional motion-vector-world-offset auxiliary pass for the flagged subset
3. merge/update step producing final `SceneVelocity`
4. only then may stage 9 report velocity publication complete

## 5. Execution Contract

### 5.1 BasePassMeshProcessor

`BasePassMeshProcessor` consumes published draw metadata and emits stage-9 draw
commands without re-traversing the scene.

Required grouping dimensions:

- material / shader family
- geometry / LOD
- masked vs opaque permutation
- velocity-capable vs non-velocity-capable draw classification

### 5.2 Stage Execution Flow

```text
BasePassModule::Execute(ctx, scene_textures)
  ├─ validate deferred mode + current-view prepared-scene payload
  ├─ build draw commands from PreparedSceneFrame + draw metadata
  ├─ bind framebuffer with GBufferNormal/Material/BaseColor/CustomData
  │  + SceneColor + Velocity + SceneDepth
  ├─ clear color MRTs and Velocity (depth preserved if early-Z complete)
  ├─ for each draw:
  │    ├─ choose opaque or ALPHA_TEST permutation
  │    ├─ bind material resources + current/previous draw bindings
  │    ├─ vertex shader computes:
  │    │    - current position
  │    │    - previous position from previous rigid/deformation inputs
  │    │    - current/previous WPO when material requires it
  │    ├─ pixel shader:
  │    │    - alpha-clips first for masked materials
  │    │    - writes GBuffer + SceneColor
  │    │    - writes encoded velocity when draw flags require it
  │    └─ draw
  ├─ if any draw/material requires motion-vector world offset:
  │    ├─ run stage-9 auxiliary offset pass for the flagged subset
  │    └─ merge/update the auxiliary result into final SceneVelocity
  ├─ transition outputs to final states
  └─ publish BasePassExecutionResult
```

### 5.3 Previous-Frame Producer Rules

To claim UE5.7-grade parity for opaque velocity, stage 9 must support:

1. rigid transform delta
2. skinned deformation delta
3. morph/deformation delta
4. current and previous WPO
5. masked alpha-clip before velocity write
6. material temporal-responsiveness / pixel-animation velocity encoding if the
   material contract exposes those semantics

If any of these producer classes are missing, docs/tests must keep the item
open. This LLD does not authorize scope-narrowing by omission.

## 6. Shader Contract

### 6.1 Vertex Shader Responsibilities

The stage-9 vertex shader family must be able to compute both current and
previous clip-space positions.

Required inputs:

- current rigid transform
- previous rigid transform
- current deformation payload
- previous deformation payload
- current and previous view/projection data
- material WPO evaluation hooks for current and previous positions

### 6.2 Pixel Shader Responsibilities

The pixel shader must:

1. perform masked alpha-clip before writing any MRT output
2. write the canonical `GBufferOutput`
3. write encoded velocity for eligible draws
4. preserve material-side velocity metadata such as temporal responsiveness /
   pixel-animation flags when the material contract exposes them
5. participate in the motion-vector-world-offset auxiliary path when the
   material contract requires it

Illustrative output shape:

```hlsl
struct BasePassOutput {
  float4 gbuffer_normal      : SV_Target0;
  float4 gbuffer_material    : SV_Target1;
  float4 gbuffer_base_color  : SV_Target2;
  float4 gbuffer_custom_data : SV_Target3;
  float4 emissive_scene_color: SV_Target4;
  float2 velocity            : SV_Target5;
};
```

## 7. Stage Integration

### 7.1 Dispatch Contract

SceneRenderer calls `BasePassModule::Execute(ctx, scene_textures)` at stage 9.

Immediately after stage 9, `SceneRenderer` performs the inline stage-10 rebuild
boundary:

```cpp
PublishDeferredBasePassSceneTextures(ctx);
```

`SceneVelocity` may be published only from the output-backed stage-9 result.
That result becomes authoritative only after the optional
motion-vector-world-offset auxiliary merge/update step completes.

### 7.2 Null-Safe Behavior

When `base_pass_` is null: no GBuffer writes, no SceneColor emissive
accumulation, and no `SceneVelocity` publication occur.

## 8. Testability and Validation

Required proof surfaces:

1. **Unit tests**
   - draw metadata carries truthful velocity flags
   - BasePassMeshProcessor selects masked vs opaque permutations correctly
   - stage-9 result reports actual MRT binding / eligible draws / emitted draws
2. **History-cache tests**
   - previous rigid transforms roll forward once per frame
   - stale entries trim safely
   - current/previous view history rolls forward once per published runtime
     view identity
3. **Shader / pipeline tests**
   - velocity MRT is part of the framebuffer layout under base-pass policy
   - masked permutation drives `ALPHA_TEST`
4. **Runtime capture proof**
   - `VortexBasic` must be expanded into the authoritative opaque-velocity
     validation scene. The current scene is not sufficient because it exercises
     only rigid opaque motion.
   - Required `VortexBasic` expansion plan:
     - animated rigid opaque geometry
     - masked cutout geometry
     - skinned/deforming geometry
     - WPO material animation
     - deterministic screen-space placement for each producer so capture
       analyzers can sample producer-specific velocity regions
     - stable material/mesh labels in the capture/debug names for each producer
   - RenderDoc proof must show nonzero velocity for each supported producer
   - The runtime validator and product analyzer must be widened accordingly:
     - one producer-specific check for rigid opaque
     - one producer-specific check for masked alpha-tested geometry
     - one producer-specific check for skinned/deforming geometry
     - one producer-specific check for WPO-driven motion
5. **Truthfulness tests**
   - no test may pass purely because a bool says velocity is complete

## 9. Resolved Design Decisions

1. **Temporal responsiveness / pixel animation:** These are part of the first
   parity implementation. UE5.7 base-pass velocity encodes them directly in the
   primary velocity output via `EncodeVelocityToTexture(...)`, so Vortex must do
   the same in the initial opaque-velocity parity wave.
2. **Motion-vector world offset:** This is also part of the parity target and
   is not deferred. UE5.7 handles it through an explicit auxiliary
   motion-vector-world-offset path (`VelocityShader.usf` +
   `VelocityUpdate.usf`) rather than pretending the plain base-pass velocity
   write is sufficient. Vortex must therefore provide an explicit auxiliary
   offset path and merge/update step for materials that declare motion-vector
   world offset semantics; it must not leave this as a hidden later expansion.
3. **Validation surface:** `VortexBasic` remains the Phase-3 runtime validation
   surface, but it must be expanded as described in §8. It is not currently
   sufficient in its present form.
