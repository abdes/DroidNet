# LightCullingPass

Builds a clustered light grid that maps every screen-space cluster to the
subset of positional lights (point and spot) affecting it. Downstream shading
passes look up each pixel's cluster and iterate only the lights assigned to it
instead of evaluating every light in the scene. The light culling pass is the
sole producer of the clustered light grid in Oxygen's Forward+ pipeline; all
forward shading consumers depend on its output contract.

## Purpose

- Cull positional lights against a 3D cluster grid derived analytically from
  the view frustum.
- Publish a `LightCullingConfig` contract embedded in `LightingFrameBindings`
  that downstream shading passes consume to locate the grid and index list.
- Provide per-cluster `(offset, count)` entries into a flat light index list so
  that shading loops iterate only relevant lights.
- Enable Forward+ single-pass lighting in `ShaderPass` and `TransparentPass`
  without per-pixel light searches.

Directional lights are excluded. They affect all pixels uniformly and are
evaluated separately in the shading loop via `ForwardDirectLighting.hlsli`.

## Depth Independence

LightCullingPass builds the cluster grid **analytically** from the view frustum
geometry. It does not consume `DepthPrePassOutput`, raw scene depth, scene HZB,
or `SceneDepthDerivatives`. Scene depth enters the pipeline only at shading
time, when the fragment shader uses its interpolated depth to select a
precomputed cluster cell.

This is an intentional architectural decision aligned with UE5's non-Lumen
`LightGridInjection` path: the grid shape is determined by the camera frustum
and fixed Z-slice mapping, not by the actual depth distribution in the scene.

## Cluster Grid Shape

The grid is engine-owned with fixed shipping constants. There are no runtime
tuning knobs for cell size, slice count, or Z-range.

| Parameter | Value | Derivation |
| --------- | ----- | ---------- |
| XY cell size | 64 px | `kLightGridPixelSize = 1 << 6` |
| Z slice count | 32 | `kLightGridSizeZ` |
| Max lights per cell | 32 | `kMaxCulledLightsPerCell` |
| Thread group size | 4×4×4 | `kThreadGroupSize` |
| Slice distribution scale | 4.05 | `kSliceDistributionScale` (UE-style) |
| Near offset | 0.095 m | `kNearOffsetMeters` |
| Far plane padding | 0.1 m | `kFarPlanePadMeters` |

### Grid Dimensions

Grid dimensions are computed per frame from the viewport resolution:

```cpp
x = ceil(viewport_width  / 64)
y = ceil(viewport_height / 64)
z = 32
total_clusters = x × y × z
```

At 1920×1080: 30 × 17 × 32 = 16,320 clusters.

### Z-Slice Mapping

Z slices use UE-style `LightGridZParams = (B, O, S)` for logarithmic depth
distribution adapted to Oxygen's meter-scale world:

```cpp
slice = log2(depth × B + O) × S
```

The inverse (used during grid construction to find cell depth bounds):

```cpp
depth = (exp2(slice / S) - O) / B
```

Where:

| Symbol | Formula |
| ------ | ------- |
| `n` | `near_plane + kNearOffsetMeters` |
| `f` | `max(far_plane + kFarPlanePadMeters, n + 0.001)` |
| `O` | `(f - n × exp2((kLightGridSizeZ - 1) / S)) / (f - n)` |
| `B` | `(1 - O) / n` |
| `S` | `kSliceDistributionScale` (4.05) |

Slice 0 is clamped to depth 0.0 (camera origin). The last slice extends to a
far sentinel (2,000,000 m) to capture all geometry beyond the far plane.

## Configuration

| Field | Type | Required | Purpose |
| ----- | ---- | -------- | ------- |
| `debug_name` | `string` | No | Pass identifier (default: `"LightCullingPass"`) |

The pass takes no depth texture, no viewport override, and no algorithm-shape
parameters. Grid shape is fully determined by the shipping constants and the
current view's viewport and near/far planes.

## Light Data Source

Positional lights are collected by `LightManager` during scene traversal:

| Gate | Rule |
| ---- | ---- |
| Visibility | Node must be `kVisible` |
| Affects world | `affects_world` must be `true` |
| Mobility | Baked lights are excluded |

`LightManager` uploads a `StructuredBuffer<PositionalLightData>` each frame.
The pass reads the buffer's SRV index and light count from `LightManager`
during `PrepareResources`.

### PositionalLightData Layout (96 bytes)

| Register | Fields |
| -------- | ------ |
| 0 | `float3 position_ws`, `float range` |
| 1 | `float3 color_rgb`, `float luminous_flux_lm` |
| 2 | `float3 direction_ws`, `uint32 flags` |
| 3 | `float inner_cone_cos`, `float outer_cone_cos`, `float source_radius`, `float decay_exponent` |
| 4 | `uint attenuation_model`, `uint mobility`, `uint shadow_resolution_hint`, `uint shadow_flags` |
| 5 | `float shadow_bias`, `float shadow_normal_bias`, `float exposure_compensation_ev`, `uint shadow_map_index` |

Flags encoding: bits [1:0] = light type (0 = point, 1 = spot), bit [2] =
`kAffectsWorld`, bit [3] = `kCastsShadows`, bit [4] = `kContactShadows`.

## Output Contract: `LightCullingConfig`

Every downstream consumer accesses the light grid through `LightCullingConfig`,
published per-view inside `LightingFrameBindings`. No pass may create private
light grid lookups or assumptions outside this contract.

| Field | Type | Description |
| ----- | ---- | ----------- |
| `bindless_cluster_grid_slot` | `ClusterGridSlot` | Strong-typed SRV index for the cluster grid buffer |
| `bindless_cluster_index_list_slot` | `ClusterIndexListSlot` | Strong-typed SRV index for the light index list buffer |
| `cluster_dim_x`, `cluster_dim_y`, `cluster_dim_z` | `uint32_t` | Grid dimensions for the current viewport |
| `light_grid_pixel_size_shift` | `uint32_t` | Always 6 (`log2(64)`) |
| `light_grid_z_params_b`, `_o`, `_s` | `float` | UE-style Z-slice mapping parameters |
| `max_lights_per_cell` | `uint32_t` | Always 32 |
| `light_grid_pixel_size_px` | `uint32_t` | Always 64 |

### Availability Check

`LightCullingConfig::IsAvailable()` returns `true` only when both bindless
slots are valid and all dimension fields are non-zero. Consumers must check
availability before reading the grid.

### Strong-Typed Slots

`ClusterGridSlot` and `ClusterIndexListSlot` are distinct types wrapping
`ShaderVisibleIndex` to prevent accidental slot mixups at the API boundary.

### GPU Buffer Layouts

**Cluster Grid** (`uint2` per cluster):

| Element | Content |
| ------- | ------- |
| `.x` | Offset into the light index list |
| `.y` | Number of lights in this cluster (capped at 32) |

Size: `total_clusters × 8` bytes. At 1920×1080: 16,320 × 8 = 127.5 KB.

**Light Index List** (`uint` per entry):

| Element | Content |
| ------- | ------- |
| `[offset + i]` | Global index into the positional lights buffer |

Maximum capacity: `total_clusters × max_lights_per_cell`.
Size at 1920×1080: 16,320 × 32 × 4 = 2,040 KB.

## Shader Contract

### Compute Shader: `LightCulling.hlsl`

Entry point: `CS` with `[numthreads(4, 4, 4)]`.

Each thread processes one cluster identified by `SV_DispatchThreadID`. Threads
with IDs outside the grid dimensions early-out.

**Pass constants** (uploaded via CBV through root constant index):

| Field | Type | Description |
| ----- | ---- | ----------- |
| `light_buffer_index` | `uint` | SRV for `StructuredBuffer<PositionalLightData>` |
| `light_list_uav_index` | `uint` | UAV for `RWStructuredBuffer<uint>` (light index list) |
| `light_count_uav_index` | `uint` | UAV for `RWStructuredBuffer<uint2>` (cluster grid) |
| `inv_projection_matrix` | `float4x4` | For frustum corner reconstruction |
| `screen_dimensions` | `float2` | Viewport width × height |
| `num_lights` | `uint` | Total positional light count |
| `cluster_dim_x/y/z` | `uint` | Grid dimensions |
| `light_grid_pixel_size_shift` | `uint` | Always 6 |
| `light_grid_z_params_b/o/s` | `float` | Z-slice mapping parameters |
| `max_lights_per_cell` | `uint` | Always 32 |

**Algorithm per thread:**

1. Compute the cluster's view-space AABB from its 3D grid index:
   - Convert XY cell bounds to clip space using the pixel size shift.
   - Convert Z slice bounds to linear depth via the inverse Z-params formula.
   - Transform all 8 corners from clip space to view space via `inv_projection_matrix`.
   - Compute the AABB min/max from the transformed corners.
2. Iterate all positional lights and test each against the cell AABB:
   - **Sphere test**: squared distance from light center to AABB vs. light
     range squared. Lights with `kAffectsWorld` unset are skipped.
   - **Cone test** (spot lights only): approximate cone-vs-AABB rejection
     using `IsAabbOutsideInfiniteAcuteConeApprox()`, which tests the AABB
     against the infinite cone's bounding half-plane.
3. Write accepted light indices to `light_list[offset + i]`.
4. Write `uint2(offset, min(count, max_lights_per_cell))` to
   `cluster_grid[cluster_index]`.

The light list offset is deterministic: `cluster_index × max_lights_per_cell`.
No atomics are needed. Overflow is silently capped at `max_lights_per_cell`.

### Cluster Lookup: `ClusterLookup.hlsli`

Shared header consumed by all shading passes to find a pixel's cluster:

```hlsl
uint cluster_idx = ComputeClusterIndex(
    screen_pos, linear_depth, cluster_dims,
    pixel_size_shift, z_params);

CLUSTER_LIGHT_LOOP_BEGIN(grid_slot, list_slot, cluster_idx)
    PositionalLightData light = positional_lights[light_index];
    // shade ...
CLUSTER_LIGHT_LOOP_END
```

Key functions:

| Function | Purpose |
| -------- | ------- |
| `ComputeClusterZSlice()` | Map linear depth to Z-slice index |
| `ComputeClusterIndex()` | Map (screen_pos, depth) to linear cluster index |
| `GetClusterLightInfo()` | Read `uint2(offset, count)` from cluster grid |
| `GetClusterLightIndex()` | Read a light index from the light index list |

`CLUSTER_LIGHT_LOOP_BEGIN` / `CLUSTER_LIGHT_LOOP_END` macros wrap the
iteration pattern for ergonomic use in shading code.

## Pipeline Position

```text
DepthPrePass → LightCullingPass → ShaderPass → TransparentPass → ...
```

The pass appears after `DepthPrePass` in the frame graph for ordering purposes,
but it does not consume `DepthPrePassOutput`. It runs as a compute dispatch
before any shading pass that needs the light grid.

## Execution Model

1. **`PrepareResources`**: Compute grid dimensions from viewport. Acquire
   positional lights from `LightManager`. Ensure cluster grid and light index
   list GPU buffers exist with sufficient capacity (lazy allocation with
   growth-only resizing). Ensure pass constants CBV. Track and transition
   resources to `kUnorderedAccess`.
2. **`Execute`**: Publish `LightCullingConfig` via
   `Renderer::UpdateCurrentViewLightCullingConfig()`. Upload pass constants.
   Dispatch compute shader with
   `ceil(grid_x / 4) × ceil(grid_y / 4) × ceil(grid_z / 4)` thread groups.
   Transition output buffers to `kShaderResource` for downstream consumers.
3. **Zero-light fast path**: When `num_lights == 0`, the pass publishes the
   config and transitions buffers to `kShaderResource` without dispatching.
   Consumers see zero-count entries in every cluster.

### Buffer Lifecycle

Buffers are created lazily when grid dimensions are first known and grow
monotonically — they are never shrunk. When a buffer is replaced (due to
capacity growth), the old buffer is retired through `TimelineGatedSlotReuse`,
which defers descriptor and resource destruction until the GPU timeline confirms
the old buffer is no longer in flight.

## Resource State Transitions

| Phase | Cluster grid state | Light index list state | Positional lights state |
| ----- | ------------------ | ---------------------- | ----------------------- |
| Frame init | `kCommon` | `kCommon` | `kGenericRead` |
| LightCullingPass | `kUnorderedAccess` | `kUnorderedAccess` | `kGenericRead` |
| ShaderPass / TransparentPass | `kShaderResource` | `kShaderResource` | `kGenericRead` |

The pass guards `BeginTrackingResourceState()` with `IsResourceTracked()`
checks to remain idempotent when `PrepareResources()` is called repeatedly
within the same frame.

## Pipeline State

### Fixed Properties

| Property | Value |
| -------- | ----- |
| Pipeline type | Compute |
| Shader | `Lighting/LightCulling.hlsl`, entry `CS` |
| Thread group | 4×4×4 (64 threads) |
| Root signature | Bindless table (t0-unbounded) + ViewConstants (b1) + RootConstants (b2) |
| PSO name | `LightCulling_PSO` |

There is exactly one PSO. No permutations.

## Downstream Consumers

| Consumer | Binding | Usage |
| -------- | ------- | ----- |
| `ShaderPass` | Cluster grid SRV + light index list SRV (via `LightingFrameBindings`) | Per-pixel cluster lookup for Forward+ point/spot lighting |
| `TransparentPass` | Same as `ShaderPass` | Same cluster lookup for transparent geometry |
| Debug visualizations | Same SRVs | Heat map, depth slice, and cluster index overlays |

### Consumer Lookup Pattern

At shading time, the fragment shader:

1. Computes linear depth from the interpolated position.
2. Calls `ComputeClusterIndex(screen_pos, linear_depth, ...)` to find the
   cluster.
3. Reads `(offset, count)` from the cluster grid.
4. Iterates `count` entries in the light index list starting at `offset`.
5. Fetches each `PositionalLightData` and evaluates the BRDF contribution.

This is the only sanctioned light-grid access pattern. No consumer may
bypass `ClusterLookup.hlsli` or create private grid access helpers.

## Telemetry

The pass tracks internal telemetry accessible via `BuildTelemetryDump()`:

| Metric | Description |
| ------ | ----------- |
| `frames_prepared` | Total frames where `PrepareResources` ran |
| `frames_executed` | Total frames where compute dispatch ran |
| `cluster_buffer_recreate_count` | Times the cluster grid buffer was reallocated |
| `light_list_buffer_recreate_count` | Times the light index list buffer was reallocated |
| `peak_clusters` | High-water mark for total cluster count |
| `peak_light_index_capacity` | High-water mark for light index list capacity |

Deferred-retire telemetry from `TimelineGatedSlotReuse` is also reported.

## Debug Visualizations

Three visualization modes are available through DemoShell:

| Mode | Description |
| ---- | ----------- |
| Heat Map | Per-pixel color based on the cluster's light count (0 = cold, 32 = hot) |
| Depth Slice | Per-pixel color based on the Z-slice index |
| Cluster Index | Per-pixel color based on the linear cluster index |

These are the only supported debug outputs. No tile-mode, depth-overlay, or
HZB-related visualizations exist.

## Ownership Boundaries

| Concern | Owner |
| ------- | ----- |
| Cluster grid and light index list buffers | `LightCullingPass` (creates and grows lazily) |
| Cluster grid SRV + light index list SRV | `LightCullingPass` (created and cached in descriptor registry) |
| Published config lifetime | `LightCullingPass` (published per-view per-frame) |
| Positional light buffer | `LightManager` (frame-scoped upload) |
| Grid shape constants | `LightCullingConfig` (engine-owned, compile-time fixed) |
| Shading-time lookup | `ClusterLookup.hlsli` (shared header, sole lookup authority) |
| Z-slice mapping math | `LightCullingConfig` (CPU) / `ClusterLookup.hlsli` (GPU) in lockstep |
| Debug visualization | DemoShell (`LightCullingDebugPanel`, `LightCullingVm`) |

No pass outside `LightCullingPass` may create or populate cluster grid buffers.
No shader outside `ClusterLookup.hlsli` may implement cluster lookup logic.

## Saturation and Overflow

When a cluster contains more lights than `kMaxCulledLightsPerCell` (32), the
excess lights are silently dropped. The cluster grid entry records the capped
count. There is no runtime warning or fallback path. Saturation is expected to
be rare at typical scene densities; pathological cases are detected through
telemetry (`peak_light_index_capacity`) and RenderDoc inspection of saturated
cluster counts.

## Deferred Work

### Release Benchmarking (LC-5)

The current shipping constants (64 px / 32 slices / 32 lights-per-cell) are
the UE-first candidate set. Final shipping configuration lock requires a
Release-mode benchmark comparing this candidate against at least one
alternative on representative Oxygen scenes. The benchmark must record
`LightCullingPass` GPU time, total frame time, and truncation behavior. This
work is not yet completed.

### VSM Light-Grid Pruning

`VsmPageRequestGeneratorPass` is an intended consumer of the light grid for
per-page light pruning. The current RenderScene capture does not exercise this
path. Live VSM light-grid pruning must be captured and validated before it can
be documented as a confirmed consumer.

## Related Documentation

- [Data Flow](data_flow.md): overall renderer pipeline and multi-view
  architecture
- [Depth Pre-Pass](depth_pre_pass.md): upstream depth population (independent
  of this pass)
- [Shader Pass](shader_pass.md): primary consumer of the light grid
- [Design Overview](design-overview.md): pass ordering in the Forward+ pipeline
- [LightCullingRemediationPlan](../../../../design/LightCullingRemediationPlan.md):
  phased remediation history and validation evidence
