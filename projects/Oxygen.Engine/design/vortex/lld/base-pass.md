# Base Pass Module LLD

**Phase:** 3 — Deferred Core
**Deliverable:** D.5
**Status:** `ready`

## 1. Scope and Context

### 1.1 What This Covers

`BasePassModule` — the stage-9 owner responsible for GBuffer MRT writes,
emissive accumulation into SceneColor, velocity completion for skinned/WPO
geometry, and the material shader contract that all Vortex material shaders
must satisfy. Phase 3 implements deferred mode only; forward mode is a
future extension.

### 1.2 Stage Position

| Position | Stage | Notes |
| -------- | ----- | ----- |
| Predecessor | Stage 3 (DepthPrepass) — depth + partial velocity written | |
| Predecessors (reserved) | Stages 4-8 (occlusion, light grid, shadows — stubs) | |
| **This** | **Stage 9 — BasePass** | GBuffer MRT + velocity completion |
| Successor | Stage 10 (RebuildSceneTextures) — state transition | |

### 1.3 Architectural Authority

- [ARCHITECTURE.md §6.2](../ARCHITECTURE.md) — stage table, row 9
- [ARCHITECTURE.md §6.3.1](../ARCHITECTURE.md) — deferred-core invariants
- [ARCHITECTURE.md §5.1.3](../ARCHITECTURE.md) — velocity distribution (stages 3, 9, 19)
- [DESIGN.md §6](../DESIGN.md) — BasePass design (GBuffer layout, config)
- UE5 reference: `RenderBasePass` family (~3.35 k lines)

### 1.4 Required Invariants For This Module

This module must preserve the following invariants from
[ARCHITECTURE.md §6.3.1](../ARCHITECTURE.md):

- `SceneRenderer` owns per-view iteration; `BasePassModule::Execute(...)`
  consumes the current view only
- masked opaque materials remain part of the deferred opaque contract and write
  GBuffers in this stage
- stage 10 remains the canonical `RebuildWithGBuffers()` plus routing-refresh
  boundary; BasePass must not invent a narrower alternate API
- velocity completion here is the stage-9 contribution within the distributed
  3/9/19 ownership model

## 2. Interface Contracts

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

### 2.2 Public API

```cpp
namespace oxygen::vortex {

struct BasePassConfig {
  bool write_velocity{true};        // Complete velocity for skinned/WPO
  bool early_z_pass_done{true};     // Skip depth writes if prepass ran
  ShadingMode shading_mode{ShadingMode::kDeferred};
};

class BasePassModule {
 public:
  explicit BasePassModule(Renderer& renderer,
                          const SceneTexturesConfig& config);
  ~BasePassModule();

  BasePassModule(const BasePassModule&) = delete;
  auto operator=(const BasePassModule&) -> BasePassModule& = delete;

  /// Stage 9 entry point. Per-view execution.
  void Execute(RenderContext& ctx, SceneTextures& scene_textures);

  void SetConfig(const BasePassConfig& config);

 private:
  Renderer& renderer_;
  BasePassConfig config_;

  std::unique_ptr<BasePassMeshProcessor> mesh_processor_;
};

}  // namespace oxygen::vortex
```

### 2.3 ShadingMode Branching

```cpp
enum class ShadingMode : std::uint8_t {
  kDeferred,   // GBuffer base pass (Phase 3 default and only mode)
  kForward,    // Forward shading — future extension, not implemented
};
```

Phase 3 implements the deferred path only. The forward branch remains a
declared seam for later phases, but it is not a valid Phase 3 runtime mode and
must not silently degrade at execution time.

### 2.4 Ownership and Lifetime

| Owner | Owned By | Lifetime |
| ----- | -------- | -------- |
| `BasePassModule` | `SceneRenderer` (unique_ptr) | Same as SceneRenderer |
| `BasePassMeshProcessor` | `BasePassModule` (unique_ptr) | Same as module |
| PSOs (deferred GBuffer) | Renderer PSO cache | Persistent |

## 3. GBuffer MRT Layout

### 3.1 Render Target Configuration

| RT Slot | Target | Content | Format |
| ------- | ------ | ------- | ------ |
| SV_Target0 | GBufferA | Encoded world normal (octahedral) | R10G10B10A2_UNORM |
| SV_Target1 | GBufferB | Metallic, specular, roughness, shading model ID | R8G8B8A8_UNORM |
| SV_Target2 | GBufferC | Base color RGB, AO | R8G8B8A8_SRGB |
| SV_Target3 | GBufferD | Custom data (subsurface, cloth, etc.) | R8G8B8A8_UNORM |
| SV_Target4 | SceneColor | Emissive accumulation | R16G16B16A16_FLOAT |
| DS | SceneDepth | Depth/stencil | D32_FLOAT_S8X24_UINT |

### 3.2 Depth Write Behavior

- If `early_z_pass_done == true`: depth test enabled, depth write
  **disabled** (reads prepass depth, does not overwrite). Uses
  `D3D12_DEPTH_WRITE_MASK_ZERO`.
- If `early_z_pass_done == false`: depth test and write both enabled.
  BasePass writes depth directly (fallback when prepass is disabled).

### 3.3 GBufferIndex Enum

```cpp
enum class GBufferIndex : std::uint8_t {
  kA = 0,  // World normal (encoded)
  kB = 1,  // Metallic, specular, roughness
  kC = 2,  // Base color
  kD = 3,  // Custom data / shading model
  kE = 4,  // Precomputed shadow factors (reserved, Phase 7+)
  kF = 5,  // World tangent (reserved, Phase 7+)
};
```

Phase 3 uses kA through kD (4 GBuffers). kE and kF are reserved slots.

## 4. Data Flow and Dependencies

### 4.1 Inputs

| Source | Data | Purpose |
| ------ | ---- | ------- |
| InitViewsModule (stage 2) | Current-view `PreparedSceneFrame` payload | Determines what to draw without scene re-traversal |
| DepthPrepassModule (stage 3) | SceneDepth (read), completeness state | Early-Z optimization |
| SceneTextures | GBuffer RTVs, SceneColor RTV, SceneDepth DSV | Render targets |
| GeometryUploader | Vertex/index buffer bindings | Mesh data |
| Material system | Per-material bindless resource indices | Texture/parameter bindings |

### 4.2 Outputs

| Product | Target | Notes |
| ------- | ------ | ----- |
| GBufferA–D | SceneTextures GBuffer textures | Full opaque scene material data |
| SceneColor | SceneTextures SceneColor | Emissive accumulation (additive) |
| Velocity (completed) | SceneTextures Velocity | Skinned/WPO geometry contributions added |

### 4.3 SceneTextures State Transitions

```text
Before stage 9:
  SceneDepth   = valid (from stage 3)
  GBufferA–D   = allocated, cleared
  SceneColor   = allocated, cleared
  Velocity     = partially written (static only, from stage 3)

After stage 9:
  SceneDepth   = unchanged (read-only, or written if no prepass)
  GBufferA–D   = written (full opaque scene)
  SceneColor   = written (emissive contribution)
  Velocity     = complete (static + skinned/WPO)

After stage 10 (inline):
  SetupMode flags updated: GBUFFERS, SCENE_COLOR set
  GBuffers now bindable for downstream reads (deferred lighting)
```

### 4.4 Execution Flow

```text
BasePassModule::Execute(ctx, scene_textures)
  │
  ├─ Read current view prepared-scene payload from ctx
  │     │
  │     ├─ Set render targets:
  │     │     RTV[0] = GBufferA
  │     │     RTV[1] = GBufferB
  │     │     RTV[2] = GBufferC
  │     │     RTV[3] = GBufferD
  │     │     RTV[4] = SceneColor
  │     │     DSV    = SceneDepth (DEPTH_READ if early_z, DEPTH_WRITE otherwise)
  │     │
  │     ├─ mesh_processor_->BuildDrawCommands(
  │     │     current_view_prepared_scene)
  │     │     Consume published prepared-scene partitions / items
  │     │     Filter/refine: opaque + masked deferred participants only
  │     │     Group: by material / mesh without rebuilding coarse routing
  │     │
  │     ├─ for each draw command:
  │     │     ├─ Bind GBuffer PSO (material-specific variant)
  │     │     ├─ Bind material resources (textures, constants)
  │     │     ├─ Bind vertex/index buffers from GeometryUploader
  │     │     └─ DrawIndexedInstanced(...)
  │     │
  │     └─ if config_.write_velocity:
  │           └─ Velocity completion pass for skinned/WPO geometry
  │              (writes to Velocity UAV for dynamic primitives)
  │
  └─ [stage 10 follows immediately in SceneRenderer]
```

## 5. Material Shader Contract

### 5.1 HLSL Output Structure

All Vortex material shaders in deferred mode **must** output `GBufferOutput`:

```hlsl
struct GBufferOutput {
  float4 GBufferA : SV_Target0;  // encoded normal
  float4 GBufferB : SV_Target1;  // metallic, specular, roughness, shading model
  float4 GBufferC : SV_Target2;  // base color, AO
  float4 GBufferD : SV_Target3;  // custom data
  float4 Emissive : SV_Target4;  // emissive → SceneColor
};
```

### 5.2 Material Pixel Shader Template

```hlsl
// Stages/BasePass/BasePassGBuffer.hlsl

#include "../../Materials/GBufferMaterialOutput.hlsli"
#include "../../Contracts/ViewFrameBindings.hlsli"

struct BasePassVSOutput {
  float4 position     : SV_Position;
  float3 worldNormal  : NORMAL;
  float2 uv           : TEXCOORD0;
  float3 worldPos     : TEXCOORD1;
  uint   instanceId   : INSTANCE_ID;
};

GBufferOutput BasePassGBufferPS(BasePassVSOutput input) {
  // Sample material textures via bindless handles
  MaterialSurface surface;
  surface.worldNormal = normalize(input.worldNormal);
  surface.baseColor   = SampleBaseColor(input.uv, input.instanceId);
  surface.metallic    = SampleMetallic(input.uv, input.instanceId);
  surface.specular    = 0.5;  // default specular
  surface.roughness   = SampleRoughness(input.uv, input.instanceId);
  surface.ao          = SampleAO(input.uv, input.instanceId);
  surface.emissive    = SampleEmissive(input.uv, input.instanceId);
  surface.shadingModel = SHADING_MODEL_DEFAULT_LIT;
  surface.customData  = float4(0, 0, 0, 0);

  return PackGBufferOutput(surface);
}
```

### 5.3 Vertex Shader

```hlsl
BasePassVSOutput BasePassGBufferVS(BasePassVSInput input) {
  BasePassVSOutput output;
  float4x4 worldMatrix = LoadInstanceTransform(input.instanceId);
  float4 worldPos = mul(worldMatrix, float4(input.position, 1.0));
  output.position = mul(ViewProjection, worldPos);
  output.worldNormal = mul((float3x3)worldMatrix, input.normal);
  output.uv = input.uv;
  output.worldPos = worldPos.xyz;
  output.instanceId = input.instanceId;
  return output;
}
```

### 5.4 Catalog Registration

| Entrypoint | Profile | Permutations |
| ---------- | ------- | ------------ |
| `VortexBasePassVS` | vs_6_0 | None (Phase 3) |
| `VortexBasePassPS` | ps_6_0 | `SHADING_MODE_FORWARD` (future) |

## 6. Mesh Processor

### 6.1 BasePassMeshProcessor

```cpp
namespace oxygen::vortex {

class BasePassMeshProcessor {
 public:
  explicit BasePassMeshProcessor(Renderer& renderer);

  /// Build GBuffer draw commands from the current view's prepared-scene
  /// payload. Groups by material to minimize PSO/binding state changes.
  void BuildDrawCommands(
      const PreparedSceneFrame& prepared_scene,
      ShadingMode mode);

  [[nodiscard]] auto GetDrawCommands() const
      -> std::span<const DrawCommand>;

 private:
  Renderer& renderer_;
  std::vector<DrawCommand> draw_commands_;
};

}  // namespace oxygen::vortex
```

### 6.2 Material Grouping Strategy

1. Start from the published prepared-scene routing for the current view.
2. Refine / group by material index (primary) then by mesh (secondary, for
   instancing).
3. Within each material group, emit one PSO bind + material resource bind,
   then N draw calls for N meshes using that material.
4. Instanced draws where mesh + material match across multiple instances.

## 7. Stage Integration

### 7.1 Dispatch Contract

SceneRenderer calls `BasePassModule::Execute(ctx, scene_textures)` at
stage 9.

Immediately after stage 9, SceneRenderer executes the inline stage 10
SceneTextures rebuild boundary:

```cpp
scene_textures_.RebuildWithGBuffers();
RefreshSceneTextureBindings(ctx);
```

This is the canonical product-setup transition for deferred lighting. It is not
just a resource-state flip; it is the point where GBuffers become readable and
the shared scene-texture routing metadata is refreshed.

### 7.2 Null-Safe Behavior

When `base_pass_` is null: no GBuffer writes occur, SceneColor has no
emissive accumulation, deferred lighting has nothing to shade. This
effectively disables the entire deferred path.

### 7.3 Capability Gate

Requires `kDeferredShading`. Only instantiated when deferred mode is the
active shading path.

## 8. Velocity Completion

### 8.1 Distributed Velocity Model

Per ARCHITECTURE.md §5.1.3, velocity is written across stages:

| Stage | Contributor | Geometry |
| ----- | ----------- | -------- |
| 3 (DepthPrepass) | Partial velocity | Static geometry |
| **9 (BasePass)** | **Velocity completion** | **Skinned + WPO geometry** |
| 19 (reserved) | Late velocity | Particles, cloth (future) |

### 8.2 BasePass Velocity Sub-Pass

After GBuffer MRT draws, a velocity completion pass runs:

1. Refine the current view's prepared-scene payload to dynamic geometry only.
   In the canonical design this is a local refinement over published prepared
   data, not a second scene traversal.
2. For each: bind previous-frame transform, current-frame transform.
3. Compute screen-space velocity = current clip position − previous clip
   position.
4. Write to Velocity UAV/RTV.

This uses the same geometry (already processed by BasePass VS) but with
a velocity-specific PS that outputs to the Velocity target instead of
GBuffers. Implementation detail: either a second sub-pass with different
PSO, or MRT slot 5 (but Phase 3 uses a separate sub-pass to keep GBuffer
PSO compact at 5 MRT).

## 9. Resource Management

### 9.1 GPU Resources

| Resource | Type | Lifetime | Notes |
| -------- | ---- | -------- | ----- |
| GBufferA–D textures | Owned by SceneTextures | Per frame | BasePass writes, does not allocate |
| SceneColor texture | Owned by SceneTextures | Per frame | Emissive additive write |
| Velocity texture | Owned by SceneTextures | Per frame | Completion write |
| GBuffer PSO variants | Renderer PSO cache | Persistent | Per-material-family |

### 9.2 State Transitions

| Texture | Before Stage 9 | During Stage 9 | After Stage 10 |
| ------- | -------------- | -------------- | -------------- |
| GBufferA–D | RENDER_TARGET | RENDER_TARGET (writing) | SHADER_RESOURCE (reading) |
| SceneColor | RENDER_TARGET | RENDER_TARGET (additive) | RENDER_TARGET (still writable) |
| SceneDepth | DEPTH_READ | DEPTH_READ (or DEPTH_WRITE if no prepass) | DEPTH_READ |

## 10. Testability Approach

1. **Unit test:** BasePassMeshProcessor with mock primitives → verify
   material grouping produces minimal PSO changes and correct draw counts.
2. **GBuffer validation:** Render a known scene (colored sphere on gray
   plane). Read back GBuffer textures:
   - GBufferA: normals match expected directions (up for floor, radial for
     sphere)
   - GBufferB: metallic/roughness match material parameters
   - GBufferC: base color matches material albedo
3. **RenderDoc validation:** At frame 10, inspect GBuffer MRT after stage 9.
   Verify all 4 GBuffers + SceneColor contain expected data.
4. **Velocity validation:** Animate an object, verify Velocity texture shows
   non-zero values at the animated object's pixels after stage 9.

## 11. Open Questions

1. **Forward mode detail:** The `kForward` shading mode branch needs full
   design when Phase 5+ addresses forward-rendered special materials. Phase
   3 is deferred-only.
2. **Masked-material sort policy:** Masked materials participate in the
   deferred opaque contract in Phase 3 and therefore write GBuffers in the base
   pass after any participating depth-prepass work. The remaining open point is
   whether the mesh-processor sort key should mirror UE-style prepass-dependent
   masked ordering exactly in Phase 3 or use a simpler stable material-first
   rule until profiling justifies further specialization.
