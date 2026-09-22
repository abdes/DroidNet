# Lighting GPU ABI

Status: **EX07A production record migration implemented; remaining interface and
failure/lifetime qualification in_progress.** This companion to
[LightingService](lighting-service.md#2-canonical-data-and-interfaces) specifies
CPU/HLSL wire layouts. The [A checkpoint](../plan/EX07A-contract-review.md) owns
approved decisions, evidence and remaining gates.

The records below have size/alignment/every-offset assertions and native D3D12
upload/decode/readback coverage. The native suite passes 20 cases in Debug and
Release, including integer high-bit values, sentinels, reserved fields,
nonzero element indices, adjacent records and nonsymmetric matrix transforms.
Deferred draw constants are additionally decoded through actual aligned CBVs.

The production shadow header is now 112 bytes and routes separate directional,
cascade, projected-local and cube-local arrays. Its full frame/view/scene/selection
identities match lighting publication, including the shared build-status
resource. Record-allocation failure prevents header publication. The production
capture verifies all three light kinds in two views. Contact fields remain
inactive until the contact product is connected.

The current CPU publisher emits complete light lists without a compact-index
buffer. The CPU source selection is now an ordered directional collection, and each
shadowed source publishes an independently indexed family and surface. Spatial
culling, complete support and failure/lifetime behavior, and BRDF moment
publication remain open. These
ABI and binding proofs do not qualify physical lighting or close EX07A.

## Encoding rules

All offsets and strides below are bytes. `float` is IEEE binary32; `uint` is
uint32. Vectors occupy their scalar byte count; C++ alignment is 16 bytes for
records/metadata/bindings and 4 bytes for index/range elements. No C++ bool,
native enum storage, float-encoded integer or implicit `glm` aligned-vector mode
belongs in the wire ABI. Assert standard layout, trivial copyability, alignment,
size and **every** member offset. HLSL structured-buffer layout must match the
table, independently of constant-buffer packing rules.

The tables describe wire scalar types, not permission to use interchangeable
integers in C++ APIs. Array indices use Oxygen `NamedType<uint32_t, Tag, ...>`
with explicit construction and `.get()` at array/shader boundaries; no implicit
conversion or compatibility alias. `Types/LightingIndices.h` owns distinct
`LightSelectionIndex`, `ShadowRecordIndex`, `ShadowCascadeIndex`, `ShadowArrayLayer`
and `LightListOffset` types and symbolic sentinels. Selection indices are scoped
to the owning local/directional array and revision; projection kind selects a
shadow reference's record array. Descriptors remain `ShaderVisibleIndex` and
must not be substituted for array indices. Prove each wrapper's 4-byte size and
alignment, standard layout and trivial copyability, and retain all field-offset
and GPU decoding checks. Enclose structure layout assertions in
`NOLINTBEGIN(*-magic-numbers)` / `NOLINTEND(*-magic-numbers)` comments: their literal
offsets and sizes intentionally specify the independent wire contract.

Descriptors and absent indices use `0xffffffff`; count zero never means a valid
descriptor. Reserved bytes are zero. CPU generations remain uint64; a GPU
`uint2` stores low word then high word. There is no truncated float generation.
Matrices retain Oxygen's existing column-major upload and `mul(matrix, vector)`
convention; sentinel tests must use a nonsymmetric matrix.

## Shared local evaluation record

Evolve `ForwardLocalLightRecord` in place, using it for both shading families
and culling. Target stride **80**, alignment **16**:

| Offset | Type / member                       | Meaning                                                               |
| -----: | ----------------------------------- | --------------------------------------------------------------------- |
|      0 | float3 `position_ws`                | World metres, matching view reconstruction origin                     |
|     12 | float `range_m`                     | Authored finite range >=0; zero has no influence                      |
|     16 | float3 `intensity_rgb_cd`           | Compensated, tinted point/spot candela RGB, resolved once             |
|     28 | float `source_radius_m`             | Physical sphere/disk radius >=0                                       |
|     32 | float3 `emitted_direction_ws`       | Unit emitted axis; point stores (0,-1,0), unused                      |
|     44 | float `inverse_range_m`             | Checked positive reciprocal, or zero for zero range                   |
|     48 | float `inner_cone_sin_half_squared` | sin(inner/2)^2; point 0                                               |
|     52 | float `outer_cone_sin_half_squared` | sin(outer/2)^2; point 0                                               |
|     56 | uint `kind`                         | Point=0, Spot=1; reject unknown values                                |
|     60 | uint `flags`                        | Bit 0 authored shadow request, bit 1 contact request; other bits zero |
|     64 | uint `selection_index`              | Index in immutable local selection                                    |
|     68 | uint3 `reserved`                    | Zero; no attenuation selector/exponent                                |

Source lux/lumens, compensation, source node handle/generation and shadow tuning
remain in the CPU selection. The resolved evaluation records are derived once
from that selection. Bounds read this record, or an explicitly derived buffer
with the same index. Shadow map indices are per-view and do not live here.

Resolve compensation, tint and unit conversion together per RGB component,
using checked double/log-domain arithmetic before float narrowing. This avoids
overflowing an intermediate untinted scalar when the final tinted intensity is
representable. Zero flux or a zero tint component stays exactly zero without
evaluating 0*infinity. Raw authored flux/lux, tint and EV remain in CPU selection;
shaders apply neither tint nor light compensation a second time. View S/P is
absent. Shader helpers must name photometric illuminance/luminance correctly;
the old 1:1 `LuxToIrradiance` watt label is not a radiometric conversion.

Zero flux and zero tint are valid. Validate conversion/compensation in adequate
precision before float narrowing, including zero with extreme compensation;
do not evaluate `0 * infinity`. Preserve the existing PBR radiance domain and
range-loss handling at accumulation, not merely finite input fields.

## Shared directional evaluation record

Evolve `DirectionalLightForwardData` in place. Target stride **64**,
alignment **16**, stored in an explicitly counted array:

| Offset | Type / member                     | Meaning                                                              |
| -----: | --------------------------------- | -------------------------------------------------------------------- |
|      0 | float3 `direction_to_source_ws`   | Unit vector opposite native emitted-ray axis                         |
|     12 | uint `atmosphere_light_slot`      | Primary=0, Secondary=1, None=0xffffffff; independent of array order  |
|     16 | float3 `illuminance_rgb_lux`      | Compensated, tinted perpendicular-receiver illuminance RGB           |
|     28 | uint `flags`                      | Shadow/contact bits as in local record; others zero                  |
|     32 | float3 `ground_transmittance_rgb` | Derived atmospheric transmittance; (1,1,1) when absent               |
|     44 | uint `atmosphere_mode_flags`      | Existing authority/per-pixel/baked-ground bits 0/1/2; None uses zero |
|     48 | uint `selection_index`            | Index in immutable directional selection                             |
|     52 | uint3 `reserved`                  | Zero                                                                 |

Disk diameter and disk RGBA scale belong to atmosphere publication, not surface
light size. Remove constant-one diffuse/specular multipliers, unused light
function/IES/rectangle placeholders and duplicate sun/environment flags. These
are not authored supported controls in this path. Removing them must include
all shader diagnostics/fog/helper consumers, not just the base pass.

## Per-view publication

Evolve `LightingFrameBindings`, routed once through `ViewFrameBindings`.
Target stride **96**, alignment **16**:

The 64-byte `ViewFrameBindings` root retains its fourteen descriptors at offsets
0 through 52. Its former final two reserved words become
`uint2 lighting_view_generation` at offset 56, low word first. This is the
expected lighting-publication identity, not another descriptor. Compare it
with the lighting header and compare the complete frame sequence with finalized
view constants before evaluating the publication. Scene lifetime identities
must not reuse the small scene ID carried by node handles.

| Offset | Type / member                     | Meaning                                                                                            |
| -----: | --------------------------------- | -------------------------------------------------------------------------------------------------- |
|      0 | uint `directional_records_srv`    | Shared directional array                                                                           |
|      4 | uint `local_records_srv`          | Shared local array                                                                                 |
|      8 | uint `cluster_ranges_srv`         | Per-view 8-byte range array                                                                        |
|     12 | uint `local_indices_srv`          | Compact uint local-selection indices                                                               |
|     16 | uint `directional_count`          | Exact selected directional count                                                                   |
|     20 | uint `local_count`                | Exact selected local count                                                                         |
|     24 | uint `cluster_count`              | Checked product of grid dimensions                                                                 |
|     28 | uint `index_capacity`             | Allocated index elements; executed count comes from GPU status                                     |
|     32 | uint `directional_shadow_map_srv` | One shadow-reference element per directional                                                       |
|     36 | uint `local_shadow_map_srv`       | One shadow-reference element per local light                                                       |
|     40 | uint `build_status_srv`           | Required validity/count product for enabled lighting                                               |
|     44 | uint `grid_metadata_srv`          | One per-view metadata record                                                                       |
|     48 | uint2 `scene_generation`          | Reject reused-scene products                                                                       |
|     56 | uint2 `selection_revision`        | Accepted immutable selection revision                                                              |
|     64 | uint2 `frame_sequence`            | Current renderer frame sequence                                                                    |
|     72 | uint2 `view_generation`           | View lifetime/resource generation, not just ViewId                                                 |
|     80 | uint `publication_state`          | Disabled=0, Empty=1, Recorded=2, Failed=3; this describes the recorded package, not GPU completion |
|     84 | uint `brdf_moments_srv`           | Shared RG32Float loss/B texture                                                                    |
|     88 | uint `brdf_mean_moments_srv`      | Shared RG32Float mean-loss/mean-B texture                                                          |
|     92 | uint `brdf_model_revision`        | 1 for the specified compensated correlated-GGX model                                               |

Remove the embedded directional, `has_directional_light`, directional-index
indirection, compatibility slots and duplicate grid fields. No alternate forward
package or old binding is published. Disabled/empty products have zero counts
and invalid unused descriptors. Empty local counts need no grid dispatch;
directionals can still be present. All required generations/statuses match the
current view product before any enabled consumer reads arrays.

`LightGridMetadata`: target stride **64**, alignment **16**:

| Offset | Type / member              | Meaning                                                        |
| -----: | -------------------------- | -------------------------------------------------------------- |
|      0 | uint3 `grid_size`          | ceil(content width/64), ceil(content height/64), 32            |
|     12 | uint `pixel_size_shift`    | 6                                                              |
|     16 | float2 `content_origin_px` | Resolved content origin in target pixels                       |
|     24 | float2 `content_extent_px` | Resolved content extent                                        |
|     32 | float3 `grid_z_params`     | Perspective B/O/S; zero for linear orthographic slices         |
|     44 | float `far_depth_m`        | Finite resolved view far depth, strictly above near            |
|     48 | float `near_depth_m`       | Perspective positive near; orthographic signed near, below far |
|     52 | uint `projection_kind`     | Perspective=0, Orthographic=1                                  |
|     56 | uint2 `reserved`           | Zero                                                           |

Pixel lookup subtracts `content_origin_px` before division. Partial tiles clamp
to content edges. Grid depth is camera-forward distance, not reverse-Z
device depth. Cell zero covers the near boundary and the last cell covers the
far boundary; the culler and lookup use identical conservative boundaries. The
inverse projection comes from the finalized view constants. Do not use the old
shader's arbitrary 2,000,000 m last-slice extent or a negated spot axis.

Perspective requires `0<near<far` and uses the stated logarithmic mapping.
Orthographic retains its native signed near/far support (`far>near`, both finite):
use 32 linear slices over that interval and zero unused logarithmic parameters.
Its view depth is signed camera-forward distance. Do not reject or omit valid
receivers merely because orthographic near or receiver depth is nonpositive.
Lookup and culling use the same clipped interval and conservative boundaries.

`LightCullingConfig` becomes CPU grid geometry/configuration helpers only; remove
its duplicate bindless routing and nominal 32-light success limit. Keep one wire
metadata source. No CPU payload retains a competing viewport/depth description.

## Complete lists and GPU validity

The service-owned grid recorder uses **48-byte**, 16-aligned pass constants
(256-byte CBV allocation alignment), replacing the old positional-culler CBV:

|   Offset | Type / member                                  | Meaning                                                    |
| -------: | ---------------------------------------------- | ---------------------------------------------------------- |
|        0 | uint `lighting_bindings_srv`                   | Current view's canonical input package                     |
| 4, 8, 12 | uint `ranges_uav`, `indices_uav`, `status_uav` | Outputs; indices may be invalid only when capacity is zero |
|   16, 20 | uint `counts_uav`, `offsets_uav`               | Per-cell uint counts and uint2 checked prefix offsets      |
|       24 | uint `subpass`                                 | Reset=0, Count=1, Scan=2, Scatter=3, Finalize=4            |
|       28 | uint `work_count`                              | Bounded logical work in this dispatch                      |
|       32 | uint2 `work_offset`                            | Low/high logical starting element                          |
|       40 | uint `scan_stride`                             | Positive scan step where applicable, otherwise zero        |
|       44 | uint `scan_phase`                              | Upsweep=0, Downsweep=1; zero outside Scan                  |

The owning recorder binds that view's finalized view constants explicitly.
There is no second inverse-projection/viewport copy in the culling CBV. Validate
each subpass's resource domains and dispatch bounds. Reset/count/scan/scatter/
readiness transitions have explicit UAV ordering; a scan cannot rely on an
unbounded inter-workgroup spin. Count/offset scratch is charged to the same
resource budget. Spatial algorithm improvements may refine implementation, but
must update this owner before changing a wire contract.

`ClusterLightRange`: stride **8**, alignment **4**; `uint offset` at 0,
`uint count` at 4. A normal range addresses `[offset, offset+count)` in the
compact list, with checked count/capacity. Each eligible local index appears at
most once per cell. Empty is `(0,0)`.

The correctness-preserving overflow representation is
`(0xffffffff, local_count)`: enumerate the complete local record array without
reading compact indices. This is a list encoding in the same evaluator, not a
second shading algorithm. It cannot hide truncation, remove shadow requests or
count as spatial rejection. Report fallback cells and evaluation cost. Use it
when a complete compact range will not fit; never publish its partial prefix.

GPU `LightGridBuildStatus`: target stride **32**, alignment **16**:

| Offset | Type / member                | Meaning                                                                   |
| -----: | ---------------------------- | ------------------------------------------------------------------------- |
|      0 | uint `state`                 | Pending=0, Valid=1, Failed=2                                              |
|      4 | uint `reason`                | None=0, InvalidBounds=1, InvalidIndex=2, Capacity=3, GenerationMismatch=4 |
|      8 | uint `written_index_count`   | Actual bounded compact index elements                                     |
|     12 | uint `fallback_cell_count`   | Complete-list cells, not lost-light count                                 |
|     16 | uint2 `required_index_count` | Exact logical requirement, low/high words; checked carry, no uint32 wrap  |
|     24 | uint2 `selection_revision`   | Must match immutable bindings                                             |

Status starts Pending and becomes Valid only after every cell and other required
lighting/shadow input is ready for the same generation. Failure is sticky;
the final readiness operation must not overwrite an earlier producer failure.
UAV ordering separates reset, build subpasses, final status and read consumers.
Bounds/index/generation errors produce Failed, not a normal fallback. CPU-owned
failure records also carry the full scene/frame/view identity and required/
available byte quantities. GPU readback is asynchronous diagnostics only.

Output must consult this validity in the same command dependency chain. Invalid
SceneColor is neither metered nor inserted into reusable radiance histories. CPU
cannot decide that a merely submitted GPU build succeeded. Implementation must
define and test how the existing post-process/composition owners consume this
status; emitting black inside a lighting helper is insufficient.

## Shadow association and deferred draws

`LightShadowReference`: stride **16**, alignment **16**:
`uint projection_kind` at 0 (None=0, Cascaded2D=1, LocalCube=2, LocalProjected2D=3),
`uint record_index` at 4 (absent=`0xffffffff`), `uint selection_index` at 8,
`uint coverage_state` at 12 (NoRequest=0, NoInfluence=1, Complete=2,
OutsideAuthoredCoverage=3). ShadowService alone produces both map arrays from
source identity and the same selection. A required-but-missing map is failure,
not a None entry. Exact zero energy/range and an entirely out-of-view authored
CSM coverage interval do not require maps; the explicit coverage state records
that distinction. NoRequest includes only no authored request or explicitly
disabled shadow evaluation, never exhaustion. Contact sampling still uses the
shared contact product and per-light flags/receiver eligibility.

Projection kind is independent of light kind: point and finite/wide spot lights
may use LocalCube. A punctual spot may use LocalProjected2D only when its complete
support is representable by that projection. All finite disks and 90-degree
spots use the existing six-face conventional representation initially. Preserve
source identity, requested per-face quality and all required faces; no cone clamp.

Replace the current 3,328-byte singleton/inline-array `ShadowFrameBindings`
with this **112-byte**, 16-aligned family header:

| Offset | Type / member                       | Meaning                                                 |
| -----: | ----------------------------------- | ------------------------------------------------------- |
|      0 | uint `directional_records_srv`      | Indexed directional shadow families                     |
|      4 | uint `directional_record_count`     | Number of shadow families, not atmosphere slots         |
|      8 | uint `projected_local_records_srv`  | Projected local records                                 |
|     12 | uint `projected_local_record_count` | Exact count                                             |
|     16 | uint `cube_local_records_srv`       | Cube local records, for either local kind               |
|     20 | uint `cube_local_record_count`      | Exact count                                             |
|     24 | uint `cascade_records_srv`          | Flat directional cascade records                        |
|     28 | uint `cascade_record_count`         | Exact count                                             |
|     32 | uint `contact_depth_srv`            | ContactShadowCasterDepth, invalid when unused           |
|     36 | uint `view_status_srv`              | Same validity dependency as lighting/output             |
|     40 | uint `contact_enabled`              | Exactly 0 or 1                                          |
|     44 | uint `sampling_flags`               | Bit 0: reversed-Z conventional 3x3 PCF; other bits zero |
|     48 | float2 `contact_content_origin_px`  | Resolved content origin                                 |
|     56 | float2 `contact_content_extent_px`  | Resolved content extent                                 |
|     64 | uint2 `scene_generation`            | Matching scene                                          |
|     72 | uint2 `selection_revision`          | Matching immutable selection                            |
|     80 | uint2 `frame_sequence`              | Matching frame                                          |
|     88 | uint2 `view_generation`             | Matching view/resource lifetime                         |
|     96 | uint2 `contact_texture_extent_px`   | Actual texture dimensions                               |
|    104 | uint2 `reserved`                    | Zero                                                    |

`DirectionalShadowRecord`: **16 bytes**, 16-aligned; uint `selection_index` at 0,
`first_cascade` at 4, `cascade_count` at 8, `reserved` (zero) at 12. Count is
1..4 for an actual shadow family. All surface descriptors/layers are explicit
in its cascade records; never derive the family from the atmosphere slot.

Intersect authored manual cascade intervals with the view near/far and authored
maximum-distance domain. Omit empty intervals and publish the ordered effective
count; this does not discard any receiver in the requested coverage. Retain the
requested count/splits in CPU diagnostics. Do not manufacture tiny duplicate
cascades by clamping several endpoints to the same value.

Cascade intervals use camera-forward view depth for both projections. Preserve
authored split coordinates: perspective near is positive; orthographic near may
be signed. The first manual interval starts at the actual view near plane, and
generated intervals span the actual clipped near/far domain. Do not clamp signed
orthographic receiver depth to zero or shift authored split coordinates.
OutsideAuthoredCoverage applies to conventional maps only: a requested contact
term still runs outside CSM distance coverage when the light contributes.

`ShadowCascadeBinding`: **128 bytes**, 16-aligned:

|   Offset | Type / member                                                         |
| -------: | --------------------------------------------------------------------- |
|        0 | float4x4 `light_view_projection`                                      |
|   64, 68 | float `split_near`, `split_far` (projection extent including overlap) |
|   72, 76 | float `depth_bias`, `normal_bias_m`                                   |
|   80, 84 | uint `surface_srv`, `array_layer`                                     |
|       88 | uint2 `reserved0` (zero)                                              |
|       96 | float2 `inverse_resolution`                                           |
| 104, 108 | float `world_texel_size`, `transition_width`                          |
| 112, 116 | float `fade_begin`, `fade_end`                                        |
|      120 | uint2 `reserved1` (zero)                                              |

`ProjectedLocalShadowRecord`: **128 bytes**, 16-aligned:

|            Offset | Type / member                                                            |
| ----------------: | ------------------------------------------------------------------------ |
|                 0 | float4x4 `light_view_projection`                                         |
|                64 | float3 `shadow_origin_ws`                                                |
|            76, 80 | float `near_plane_m`, `far_plane_m`                                      |
|        84, 88, 92 | float `normal_bias_m`, `depth_bias`, `world_texel_size`                  |
| 96, 100, 104, 108 | uint `surface_srv`, `array_layer`, `selection_index`, `reserved0` (zero) |
|               112 | float2 `inverse_resolution`                                              |
|               120 | uint2 `reserved1` (zero)                                                 |

`CubeLocalShadowRecord`: **448 bytes**, 16-aligned:

|             Offset | Type / member                                                                  |
| -----------------: | ------------------------------------------------------------------------------ |
|                  0 | float4x4 `face_light_view_projection[6]` (64-byte matrix stride)               |
|                384 | float3 `shadow_origin_ws`                                                      |
|           396, 400 | float `near_plane_m`, `far_plane_m`                                            |
|      404, 408, 412 | float `normal_bias_m`, `depth_bias`, `world_texel_size`                        |
| 416, 420, 424, 428 | uint `surface_srv`, `first_array_layer`, `selection_index`, `reserved0` (zero) |
|                432 | float2 `inverse_resolution`                                                    |
|                440 | uint2 `reserved1` (zero)                                                       |

Both local records describe derived shadow projection, not another authored
light. `far_plane_m` covers the physical finite-emitter influence, not just the
old center range. Preserve the current local depth producer/receiver's linear
reversed-depth convention together; do not compare perspective NDC depth with a
linear-depth map. Select near clipping from validated support/precision needs;
the current independent 0.1 m range floor cannot silently alter authored range.
Apply authored depth bias once in its depth-pass owner; receiver normal/texel
offsets stay separate. Float-packed layer/index metadata is removed.

Cube face order remains +X,-X,+Y,-Y,+Z,-Z, with existing world-axis bases and
matching face selection. For local linear reversed depth, use the selected
projection's positive axial distance (clip w) and its published far distance;
the projection's NDC z is only a frustum test, not the stored-depth encoding.

Each descriptor identifies an allocation/resolution bucket with checked layers.
Grow or create another bucket within D1/D6 budgets and backend array limits;
do not reintroduce a fixed scene-light cap. Share a local map only when all source,
caster, quality and lifetime inputs match, not just dimensions. Directional CSM
and contact products remain view-specific. Tables and resources retain leases
through every queued consumer and submitted upload, even if a later recorder
is discarded. Retired resources count against the budget until safe release.

Deferred packets carry the selection index plus proxy transform/geometry, not
another physical-light authority. Per-draw constants: matrix at 0
(64 bytes); uint kind at 64 (Directional=0, Point=1, Spot=2, existing ambient
bridge=3), selection index at 68, geometry SRV at 72, vertex count at 76. Payload
stride **80**, allocation alignment **256**. No repeated intensity/cone/shadow
counter. Both forward and deferred load the same record and view shadow map.
The ambient bridge retains its existing environment owner and never loads a
physical light at an invalid selection index.

## Sentinel gate and migration obligations

Before production consumer cutover, exercise actual D3D12 upload, a decode
shader including these production contracts, and Graphics-owned readback:

- Two adjacent records of each light kind, unique finite exactly representable
  values per scalar lane and a nonzero starting element verify offsets/stride.
- High-bit uint patterns (including values above 2^24), both atmosphere slots,
  None, invalid descriptors/indices, reserved zeros and split uint64 generations
  verify integer decoding. Raw ABI sentinels are test data, not accepted scene
  flag values; scene validation is a separate test.
- A nonsymmetric transform verifies matrix order; output one uint bit pattern
  per decoded scalar. Compare against independently specified table offsets,
  not a memcpy of the same C++ struct or a production conversion helper.
- Nonzero content origin, partial tiles and complete-list range sentinel verify
  metadata/range/status decoding. Include every shadow-family record above,
  multiple directionals, and both point and spot identities using cube records.
- Build and run Debug/Release on native D3D12, check shader catalog/reflection
  dependencies, and show a deliberately changed lane/offset makes the test fail.

Use owning `Vortex/Test` and existing Graphics offscreen/readback fixtures;
timing remains in a separate `Benchmarks` executable. No new command is advertised
until its target exists. These tests are obligations, not current passing evidence.

Migrate C++ writers and HLSL readers together: selection/evaluation builder,
publisher, deferred packet/constants, culler, cluster lookup, all forward and
deferred/debug/fog consumers, shadow maps and `ViewFrameBindings`. Remove
`PositionalLightData.hlsli`, old single-primary accessors/compatibility slots and
stale reflection/layout expectations. Do not retain old and new shipping payloads
as a compatibility bridge. Source/packed scene records have a separate strict
migration recorded in the property inventory; GPU layout is not a disk format.
