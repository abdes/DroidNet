# DepthPrePass

Populates the scene depth buffer before main shading, producing a shared depth
products contract that enables depth-equal rendering, hierarchical Z culling,
clustered light culling, and VSM page allocation. The depth prepass is the
foundational pass in Oxygen's Forward+ pipeline; all downstream depth consumers
depend on its output contract.

## Purpose

- Render opaque and masked (alpha-tested) geometry to establish complete scene
  depth before any material shading occurs.
- Publish a `DepthPrePassOutput` contract that downstream passes consume
  instead of private depth assumptions.
- Drive the `ScreenHzbBuildPass` to produce a dual closest+furthest HZB
  pyramid from the populated depth buffer.
- Enable depth-equal rendering in `ShaderPass`, eliminating redundant fragment
  shader invocations on occluded geometry.

Transparent geometry is excluded. It does not participate in depth writes and
must not occlude later blending operations.

## Depth Convention

Oxygen uses **reversed-Z** as the engine-wide depth convention:

| Property | Value |
| -------- | ----- |
| Near plane NDC | 1.0 |
| Far plane NDC | 0.0 |
| Depth clear value | 0.0 |
| Depth comparison | `kGreaterOrEqual` |
| Projection | Right-handed, reversed-Z [0,1] |

Near surfaces produce depth values close to 1.0; far surfaces produce values
close to 0.0. This opposes the float32 precision distribution against the
perspective hyperbolic distribution, yielding nearly uniform depth precision
across the entire range.

All consumers must read the `reverse_z` and `ndc_depth_range` fields from
`DepthPrePassOutput` rather than assuming a convention.

### Related Shadow Domains

`DepthPrePass` owns the **scene** depth contract only. Shadow-domain raster
passes intentionally do not consume `DepthPrePassOutput`, but they must still
follow the same engine-wide reversed-Z convention so that depth math, compare
ops, and debugging expectations remain coherent across the renderer.

- Conventional directional shadow maps are separate depth products, not scene
  depth products.
- Conventional shadow depth therefore does **not** publish or consume
  `DepthPrePassOutput`.
- Conventional shadow depth still uses reversed-Z semantics:
  - far-plane clear at `0.0`
  - `GREATER_EQUAL` depth comparisons
  - near surfaces mapping toward `1.0`, far surfaces toward `0.0`
- VSM owns its own depth/HZB products and validation surface; any VSM-specific
  failures are tracked separately from the `DepthPrePass` contract.

## Prepass Policy

The pipeline selects a depth prepass mode per frame through `DepthPrePassMode`:

| Mode | Behavior |
| ---- | -------- |
| `kDisabled` | No depth prepass executes; downstream depth consumers are skipped |
| `kOpaqueAndMasked` | Full opaque + masked geometry prepass; produces complete depth |

The mode flows from `FramePlanBuilder` into `ForwardPipeline` at plan time.

### Completeness Contract

The pass publishes `DepthPrePassCompleteness` alongside the output:

| Completeness | Meaning | Downstream effect |
| ------------ | ------- | ----------------- |
| `kDisabled` | No prepass ran | Consumers must not run |
| `kIncomplete` | Prepass ran but depth is partial | Consumers use `kGreaterOrEqual` fallback |
| `kComplete` | All opaque+masked geometry rendered | Consumers may use `kEqual`; HZB is valid |

Downstream passes must not infer completeness from pass registration or pass
execution alone. They must read the published completeness value.

## Configuration

| Field | Type | Required | Purpose |
| ----- | ---- | -------- | ------- |
| `depth_texture` | `shared_ptr<Texture>` | Yes | Target depth buffer; defines format and dimensions |
| `debug_name` | `string` | No | Pass identifier (default: `"DepthPrePass"`) |

### Viewport and Scissor

Optional via `SetViewport()` / `SetScissors()`. When unset, the pass covers
the full `depth_texture`. When set, the pass resolves an effective depth rect
from the intersection of viewport bounds and scissor rect, then uses that rect
for both rasterization and depth clear.

The coordinate contract is **depth-target local space**, not composition-
placement space. `ForwardPipeline` normalizes `CompositionView.view`
coordinates into depth-target local space before configuring the pass. For
offscreen per-view targets, the composition placement offset is stripped and
any explicit scissor is localized to the depth texture.

An empty viewport/scissor intersection is a contract violation and is rejected.

## Output Contract: `DepthPrePassOutput`

Every downstream consumer accesses depth through this contract. No pass may
create private depth SRVs or assumptions outside this surface.

| Field | Type | Description |
| ----- | ---- | ----------- |
| `depth_texture` | `const Texture*` | Populated depth buffer |
| `canonical_srv_index` | `ShaderVisibleIndex` | Shader-visible SRV for bindless depth reads |
| `width`, `height` | `uint32_t` | Depth texture dimensions |
| `viewport` | `ViewPort` | Effective viewport used for rasterization |
| `scissors` | `Scissors` | Effective scissor rect |
| `valid_rect` | `Scissors` | Viewport/scissor intersection defining valid depth region |
| `ndc_depth_range` | `NdcDepthRange` | `ZeroToOne` (engine standard) |
| `reverse_z` | `bool` | `true` (engine standard) |
| `completeness` | `DepthPrePassCompleteness` | Published completeness for downstream gating |

### Canonical SRV

A single canonical SRV is created per depth texture and cached in the
descriptor registry. All consumers bind this SRV. No pass creates a duplicate
depth SRV.

## Scene Depth Derivatives

The depth prepass output feeds `ScreenHzbBuildPass`, which produces a dual
closest+furthest HZB pyramid published through `SceneDepthDerivatives`:

| Product | Format | Description |
| ------- | ------ | ----------- |
| Raw depth | `D32_Float` | Full-resolution scene depth from `DepthPrePassOutput` |
| Closest HZB | `R32Float` or `RG32Float` channel | Conservative nearest-surface depth per mip |
| Furthest HZB | `R32Float` or `RG32Float` channel | Conservative farthest-surface depth per mip |

The HZB is double-buffered per view to support previous-frame occlusion queries.

Under reversed-Z, closest depth is the numerically largest value and furthest
depth is the numerically smallest. The channels are named by semantic meaning
(`closest`, `furthest`), not by reduction operation.

### Derivative Consumers

| Consumer | Product consumed | Usage |
| -------- | ---------------- | ----- |
| `ShaderPass` | `DepthPrePassOutput` (DSV) | Depth-equal test when complete; `kGreaterOrEqual` fallback |
| `TransparentPass` | `DepthPrePassOutput` (DSV) | Read-only depth test (`kGreaterOrEqual`); not depth-equal eligible |
| `SkyPass` | `DepthPrePassOutput` (DSV + SRV) | Read-only depth test + shader depth reads for atmosphere |
| `GroundGridPass` | `DepthPrePassOutput` (SRV) | Shader depth reads for grid intersection; no depth test |
| `LightCullingPass` | Closest + furthest HZB | Tile depth bounds via HZB mip lookup plus clustered slice-range rejection |
| `VsmPageRequestGeneratorPass` | `DepthPrePassOutput` (SRV) | Per-pixel depth analysis for VSM page requests; optional coarse-page flags remain a separate VSM policy |
| `VsmInstanceCulling` | Previous-frame furthest HZB | Occlusion culling against projected AABB bounds |
| `ScreenHzbBuildPass` | `DepthPrePassOutput` (SRV) | Source for mip-0 of the HZB pyramid |

## Downstream Depth Exploitation

### Depth-Equal in ShaderPass

When `completeness` is `kComplete`, `ShaderPass` binds the depth buffer as a
read-only DSV with `CompareOp::kEqual`. Visible opaque fragments match the
prepass depth exactly, while occluded or otherwise mismatched fragments fail
the fixed-function depth test against the prepass result.

When `completeness` is `kIncomplete`, `ShaderPass` falls back to
`CompareOp::kGreaterOrEqual` with read-only DSV.

**Correctness requirement**: depth-equal demands bitwise-identical depth values
between the prepass and `ShaderPass`. Both passes must use the same vertex
transform codepath to produce identical `SV_POSITION` output. Any divergence
(different constant buffer layout, shader permutation affecting position
computation, or compiler reordering) causes flickering depth-test failures.

### Non-Candidates for Depth-Equal

- **TransparentPass**: renders with blending at varying depths over opaque
  geometry; depth-equal is inapplicable.
- **SkyPass**: renders at the far plane; the prepass does not write sky depth.

### Compute Consumers

`LightCullingPass`, `VsmPageRequestGeneratorPass`, and `ScreenHzbBuildPass`
gate their dispatch on `completeness != kDisabled`. When depth is incomplete,
each consumer documents whether it falls back to conservative behavior or skips
dispatch entirely.

`LightCullingPass` now consumes the shared closest+furthest HZB and derives
per-tile depth bounds from a single HZB mip lookup instead of the removed
raw-depth tile-reduction path. The clustered path uses the same shared bounds
to reject cluster slices outside the tile depth range.

`VsmPageRequestGeneratorPass` remains on the canonical raw-depth SRV for
per-pixel page marking. Its `enable_coarse_pages` behavior is a separate VSM
coverage policy layered on top of the page-request results; it is not a
consumer of `SceneDepthDerivatives`.

## Pipeline State

### Fixed Properties

| Property | Value |
| -------- | ----- |
| Depth test | Enabled, `kGreaterOrEqual` (reversed-Z) |
| Depth write | Enabled |
| Stencil | Disabled |
| Rasterizer | Solid fill, front-face CCW |
| Color targets | None (depth-only framebuffer) |
| Root signature | Bindless table (t0-unbounded) + ViewConstants (b1) + RootConstants (b2) |
| Depth clear | 0.0 (reversed-Z far plane) |

### PSO Permutation Matrix

2 axes x 2 values = 4 PSO variants:

| Alpha Mode | Sidedness | Rasterizer Cull | Pixel Shader |
| ---------- | --------- | --------------- | ------------ |
| Opaque | Single-sided | Back-face | Empty (fixed-function depth write) |
| Opaque | Double-sided | None | Empty (fixed-function depth write) |
| Masked | Single-sided | Back-face | Alpha test with `clip()` discard |
| Masked | Double-sided | None | Alpha test with `clip()` discard |

Variant selection is per-partition at runtime based on `PassMask` flags.

### Shader Contract

**Vertex shader** (all variants): reads `g_DrawIndex` root constant to fetch
`DrawMetadata`, resolves vertex and transform buffers via bindless indices,
transforms position through Local -> World -> View -> Clip. Outputs
`SV_POSITION` and `TEXCOORD0` (UV for masked alpha test).

**Pixel shader**: opaque variants use an empty PS (fixed-function depth write).
Masked variants sample opacity via the material's alpha texture or constant and
discard fragments below the cutoff threshold.

The vertex shader must produce identical `SV_POSITION` output as `ShaderPass`
to satisfy the depth-equal bitwise-match requirement.

## Execution Model

1. `PrepareResources`: transition depth texture to `kDepthWrite`, upload pass
   constants.
2. `Execute`: create or reuse DSV, resolve effective depth rect from
   viewport/scissor configuration, clear depth to 0.0 inside the valid rect
   only.
3. Iterate partitions: filter by `kOpaque` / `kMasked` pass mask, select PSO
   variant, emit draw ranges.
4. Publish `DepthPrePassOutput` with populated fields and completeness.
5. Register pass instance for cross-pass queries.

Per-draw indirection: CPU binds `g_DrawIndex` root constant; GPU fetches
`DrawMetadata[g_DrawIndex]` and resolves all buffers via bindless indices.

## Resource State Transitions

| Phase | Depth texture state |
| ----- | ------------------- |
| Frame init | `kCommon` |
| DepthPrePass | `kDepthWrite` |
| ShaderPass / TransparentPass / SkyPass | `kDepthRead` (read-only DSV) |
| ScreenHzbBuildPass / LightCullingPass / VsmPageRequestGen | `kShaderResource` (SRV) |

D3D12 permits simultaneous read-only DSV + SRV binding when depth writes are
disabled. `ShaderPass` may co-bind the depth buffer as both DSV and SRV when
shader depth reads are needed.

## Ownership Boundaries

| Concern | Owner |
| ------- | ----- |
| Depth texture lifetime | `ForwardPipeline` (creates per composition target) |
| Canonical depth SRV | `DepthPrePass` (created and cached in descriptor registry) |
| Depth clear and population | `DepthPrePass` |
| Completeness publication | `DepthPrePass` |
| HZB production | `ScreenHzbBuildPass` |
| HZB consumption | `VsmInstanceCulling` and other verified HZB consumers via `SceneDepthDerivatives` |
| Depth convention | Engine-wide (reversed-Z); consumers read from output metadata |

No pass outside `DepthPrePass` may create depth SRVs for the scene depth
texture. No pass may infer depth completeness from pass registration.

## Deferred Work

### Depth-Only PSO Specialization (DP-3)

Two independent sub-optimizations, ordered by complexity.

**Sub-phase A — Null pixel shader for opaque variants**: The opaque PS body is
empty (the `#ifdef ALPHA_TEST` path compiles away). Opaque PSO variants should
be built without `.SetPixelShader()`, letting D3D12 elide PS dispatch entirely
instead of binding a no-op shader stage. This is a ~4-line change in
`DepthPrePass.cpp` with zero functional or depth-equal risk.

**Sub-phase B — Position-only vertex stream**: Currently every depth draw
fetches a 72-byte interleaved `VertexData` struct (position 12 + normal 12 +
texcoord 8 + tangent 12 + bitangent 12 + color 16). Opaque draws consume only
position (12 bytes) — 83% bandwidth waste. The optimization introduces a
parallel 12-byte position-only `StructuredBuffer` per mesh.

#### Position-Only Data Layout

```text
PositionVertex (12 bytes)     vs.     VertexData (72 bytes)
┌──────────────────────┐              ┌──────────────────────┐
│ float3 position (12) │              │ float3 position (12) │
└──────────────────────┘              │ float3 normal   (12) │
                                      │ float2 texcoord  (8) │
                                      │ float3 tangent  (12) │
                                      │ float3 bitangent(12) │
                                      │ float4 color    (16) │
                                      └──────────────────────┘
```

`PositionVertex::position` occupies offset 0 in both structs. Loading from a
12-byte-stride buffer vs. a 72-byte-stride buffer produces bitwise-identical
`float3` values for the same vertex index. This guarantees depth-equal
correctness: the same position bits enter the same transform chain, producing
identical `SV_POSITION` output.

#### Expanded Permutation Matrix (DP-3 Target State)

2 axes × 2 values, with vertex specialization on the opaque axis = 4 variants
(replacing the current 4, not adding to them):

| Alpha Mode | Sidedness | VS | PS | Vertex Buffer | Stride |
| ---------- | --------- | -- | -- | ------------- | ------ |
| Opaque | Single-sided | `VS_PosOnly` | null | position-only | 12 B |
| Opaque | Double-sided | `VS_PosOnly` | null | position-only | 12 B |
| Masked | Single-sided | `VS` | `PS` (alpha test) | full vertex | 72 B |
| Masked | Double-sided | `VS` | `PS` (alpha test) | full vertex | 72 B |

The variant count stays at 4. Opaque variants become cheaper (position-only
fetch + no PS), while masked variants are unchanged.

#### DrawMetadata Extension

`DrawMetadata` gains a `position_only_buffer_index` field. The current struct
is 64 bytes with 12 bytes occupied by `transform_generation`, `submesh_index`,
and `primitive_flags`. The new field fits within the existing 64-byte envelope
or at the next 16-byte boundary (80 bytes) if packing cannot absorb it.

The HLSL mirror in `Renderer/DrawMetadata.hlsli` must be updated in lockstep.
`GeometryUploader` creates and registers the position-only SRV alongside the
existing vertex SRV. `DrawMetadataEmitter` populates the new field.

#### Memory Cost

+12 bytes per vertex for the position-only buffer = +16.7% GPU vertex memory
per mesh. +1 SRV per mesh in the descriptor heap.

#### Implementation Trigger

This optimization matters when vertex fetch dominates the depth prepass cost,
which requires >1M depth prepass triangles with small screen-space area. At
current scene complexity, triangle counts do not approach this threshold.
Sub-phase A (null PS) is trivial and can be done at any time as a cleanup.
Sub-phase B should wait for profiling evidence.

### Stencil Sideband

UE5 writes material classification bits during the prepass for deferred
lighting model dispatch. In Oxygen's forward renderer, the forward pass
already knows the material, making stencil classification low value. Deferred
unless a concrete consumer is identified.

## Related Documentation

- [Data Flow](data_flow.md): overall renderer pipeline and multi-view
  architecture
- [Bindless Conventions](../bindless_conventions.md): descriptor slot management
- [DepthPrePassRemediationPlan](../../../../design/DepthPrePassRemediationPlan.md):
  phased remediation history and validation evidence
