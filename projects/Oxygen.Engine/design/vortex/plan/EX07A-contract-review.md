# EX07A contract review

Status: **design/remediation contract frozen on 2026-09-22; EX07A implementation and ABI qualification remain in_progress.**
Reviewed against clean `editor` at `09aa65362` on 2026-09-22. No lighting
implementation, GPU qualification or timing baseline is claimed by this record.
The approved [EX07 plan](EX07-lighting-correctness-and-scalability.md) remains
the execution authority; this is its A checkpoint, not a replacement plan.

## Review result

The current paths cannot be connected unchanged. All D1-D6 product decisions are approved. The
[GPU contract](../lld/lighting-gpu-abi.md) gives exact layouts and
replacement obligations. The [property inventory](../lld/lighting-properties.md)
tracks retained fields, ingress, persistence, consumers and required tests.
LightingService continues to own the execution/failure contract.

Paths in the following table are engine-relative unless prefixed `repo:`.
These are source findings, not experimentally measured failures.

| Finding                                                                                                                                                                        | Source evidence                                                                                                                                                                                       | Required disposition / tracking                                                                                                                                                |
| ------------------------------------------------------------------------------------------------------------------------------------------------------------------------------ | ----------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- | ------------------------------------------------------------------------------------------------------------------------------------------------------------------------------ |
| The two local payloads have incompatible 96-byte layouts. Forward encodes kind/flags as float values; the culler expects integer flags and a different range/intensity layout. | `src/Oxygen/Vortex/Lighting/Types/ForwardLocalLightRecord.h`; shaders `Vortex/Contracts/Lighting/{ForwardLocalLightRecord,PositionalLightData}.hlsli`                                                 | One typed record; delete the positional decoder and float casts in the same migration. EX07-04/08.                                                                             |
| Stage 6 uploads a shared full local list for every cell; no spatial dispatch is recorded.                                                                                      | `Lighting/Internal/{LightGridBuilder,ForwardLightPublisher}.cpp`; `SceneRenderer.cpp`, `Vortex.Stage6.ForwardLightData`                                                                               | Separate shared preparation from per-view recording. The complete list is usable as a correctness starting point, not a measured culling improvement. EX07-08/09/13.           |
| The separate culler truncates lists and uses the opposite spot-axis sign from the forward spot test. Lookup does not subtract a content origin.                                | shaders `Services/Lighting/{LightCulling.hlsl,ClusterLookup.hlsli,ForwardDirectLighting.hlsli}`                                                                                                       | Replace the old ABI and overflow path before connecting the shader. Prove sign, bounds, projection and content-relative lookup. EX07-04/08.                                    |
| Direct selection contains only atmosphere slot 0; ordinary and Secondary directionals are absent.                                                                              | `SceneRenderer/SceneRenderer.cpp::BuildFrameLightSelection`; `Types/FrameLightSelection.h`                                                                                                            | One ordered directional collection with identity; no optional-primary fallback. EX07-01/04.                                                                                    |
| Resolver still accepts duplicate environment/sun authorities and implicitly fills Primary. Its gather uses subtree-pruning `VisibleFilter`.                                    | `src/Oxygen/Scene/Light/DirectionalLightResolver.cpp::{CollectDirectionalLights,ResolveCanonicalAtmosphereLights,ValidationErrorMessage}`                                                             | Validate stored slot claims including inactive lights; gather independently visible children; explicit assignments only. EX07-01/12.                                           |
| Compensation, attenuation model and contact-shadow flag do not reach the frame selection. Local source radius is uploaded but neither direct-light shader consumes it.         | `BuildFrameLightSelection`; local record; `DeferredLightPacketBuilder.cpp`; forward/deferred shader helpers                                                                                           | D2 removes model/exponent; other retained controls need validation, transport and actual effects. See property inventory. EX07-02/03/04/12.                                    |
| Forward and deferred local attenuation differ; neither implements the specified flux/distance chain. Forward local diffuse lacks deferred's `INV_PI`.                          | `ForwardDirectLighting.hlsli::AccumulateLocalLightsClustered`; `DeferredLightingCommon.hlsli::ComputeLocalLightDistanceAttenuation`; `DeferredShadingCommon.hlsli::EvaluateCookTorranceLighting`      | Shared physical/BRDF interpretation; independent packed-material oracle must expose the discrepancy. EX07-02/03/05/06.                                                         |
| Deferred assigns local shadow indices by another counter; forward local evaluation does not sample local shadows. Point/spot setup stops at 4/8.                               | `DeferredLightPacketBuilder.cpp`; forward helper; `Shadows/Internal/{PointShadowSetup,SpotShadowSetup}.cpp`; `Types/ShadowFrameBindings.h`                                                            | Identity-qualified maps from ShadowService, used by both families. Exhaustion must reject/fail explicitly. EX07-11.                                                            |
| Directional source/packed records retain old booleans and cannot carry the explicit slot, per-pixel transmittance or disk scale.                                               | `Cooker/Import/Schemas/oxygen.scene-descriptor.schema.json`; `Data/PakFormat_world.h::DirectionalLightRecord`                                                                                         | Strict source/packed/tool/fixture/editor/script cutover, not inferred slot reconstruction. EX07-12.                                                                            |
| Managed source DTOs carry fewer settings than editor persistence; local runtime attach commands omit attenuation model and shadow tuning.                                      | repo: `projects/Oxygen.Managed.Assets/src/Import/Scenes/*LightSource.cs`; `Oxygen.Editor.World/src/Serialization/*LightData.cs`; `Oxygen.Editor.Runtime/src/Engine/RuntimeAttach{Point,Spot}Light.cs` | Migrate source writer and native commands together; preserve retained non-default values and remove D2 fields. EX07-12.                                                        |
| Publisher may publish counts after a child allocation failed. Deferred missing-input paths return without a failed-view result.                                                | `ForwardLightPublisher.cpp::Publish`; `SceneRenderer.cpp::RenderDeferredLighting`; `LightingService.h`                                                                                                | Structured failure plus current-submission output gating; missing enabled inputs are not successful black. EX07-08/10.                                                         |
| Records/descriptors are reset by frame slot, but lighting has no own recorded/discarded publication state.                                                                     | `Upload/TransientStructuredBuffer.cpp::{OnFrameStart,ResetSlot}`; `LightingService.cpp`; `Internal/PerViewStructuredPublisher.h`                                                                      | Verify the renderer's slot-fence guarantee; retain through the final consumer and invalidate discarded products. This inspection alone does not prove a lifetime bug. EX07-10. |

Vortex owner paths in shortened rows are under `src/Oxygen/Vortex`; shader paths
are under `src/Oxygen/Graphics/Direct3D12/Shaders/Vortex`. Exact functions above
are navigation anchors; no runtime result is inferred from their existence.

## Approved decisions

### D1 — admission capacities

**Approved 2026-09-22: A — budgeted dynamic capacity. Implementation pending.**
Use indexed light/shadow records and grow resources within explicit
renderer-wide budgets and backend limits. Account for unique live allocations
across all views, frame generations, retained/discarded submissions and caches.
Report supported count/byte limits and requested/available quantities. Remove
the arbitrary four-point/eight-spot shadow cutoffs; atmosphere's two slots do
not cap ordinary directional lights or shadow families. The 4,096-light
qualification endpoint is not a hard scene-count limit.

Requested shadow resolution follows the explicitly selected quality profile;
it cannot be reduced to fit a budget. Fixed per-kind product count profiles were
not selected. Backend representation/resource limits still apply and must be
published, checked and distinguished from memory availability.

Preserve every admitted light and requested shadow. Known invalid
candidates are rejected atomically; preparation failures invalidate the affected
view without rewriting authored data. No brightest-N selection, silent truncation,
automatic unshadowing or resolution/update-rate reduction is allowed. Existing
valid panes and application UI remain usable under the LLD failure contract.
Unused cached resources may be retired to satisfy a budget only when fence-safe;
in-flight allocations cannot be reclaimed or counted as free.

The complete-list range representation is the correctness-preserving
response to compact-index exhaustion. It changes list encoding, not light power,
BRDF or shadow requests. Count fallback work explicitly; it cannot be reported
as culling improvement or used to waive the performance gate.

The initial 65,536-local/64-directional/64-MiB-index/1-GiB-per-view values were
unapproved engineering proposals. They are withdrawn as freeze recommendations.
In particular, a per-view allowance without a renderer-wide aggregate ceiling
is not a bounded memory contract. Choosing the policy does not approve those
numbers. EX07A must finish backend allocation accounting and freeze numeric
admission profiles before consumer cutover. EX07D separately freezes measured
CPU/GPU/memory performance budgets; it does not postpone the A capacity decision.

#### Capacity arithmetic and correction

At the target 80-byte local/64-byte directional strides, 65,536 local records
would occupy 5 MiB and 64 directionals 4 KiB. These remain illustrative arithmetic,
not a supported count claim. A 64px/32-slice grid has 16,320 cells at 1080p
(127.5 KiB ranges) and 65,280 at 4K (510 KiB). Duplicating all 4,096 indices in
all 4K cells costs 1,020 MiB; complete-list encoding avoids that duplication.

The earlier 256/1,024 MiB shadow figures counted only the 32-bit depth component,
so they must not be used as surface-allocation costs. All three current shadow
targets use `Format::kDepth32Stencil8` in
`src/Oxygen/Vortex/Shadows/Internal/ConventionalShadowTargetAllocator.cpp`.
D3D12 maps this to `R32G8X24_TYPELESS` with `D32_FLOAT_S8X24_UINT` views in
`src/Oxygen/Graphics/Direct3D12/Detail/FormatUtils.cpp`.
The [DXGI format definition](https://learn.microsoft.com/en-us/windows/win32/api/dxgiformat/ne-dxgiformat-dxgi_format)
is 64 bits, including stencil/unused components: **8 nominal bytes per texel**.

| Example request                                                             | Directional bytes | Point bytes | Spot bytes | Nominal total |
| --------------------------------------------------------------------------- | ----------------: | ----------: | ---------: | ------------: |
| 2 directionals x 4 cascades at 2048; 4 point cubes at 1024; 8 spots at 1024 |           256 MiB |     192 MiB |     64 MiB |       512 MiB |
| Same counts, directionals at 4096 and point/spot at 2048                    |         1,024 MiB |     768 MiB |    256 MiB |     2,048 MiB |

These are format/resolution calculations for one set of maps, not measured
backend allocations or total renderer memory. Query the backend's allocation
requirements for actual alignment/placement and account separately for light
buffers, list/status/scratch/contact resources and every distinct live/cached/
retired allocation. Shared or correctly ordered reused resources are counted once;
additional outstanding allocations are counted until their fences retire.
Do not assume either free frame reuse or a fixed frames-in-flight multiplier.

### D2 — physical lighting and artistic attenuation scope

**Approved 2026-09-22: A — physical model only. Implementation pending.**
Point/spot punctual lighting uses the specified inverse-square propagation,
normalized spot flux, range window and numerical guard. There are no Linear or
CustomExponent alternatives. This user decision supersedes the earlier plan's
requirement to retain those controls and the initial artistic-profile proposals.
BRDF surface response and light propagation remain separate physical contracts;
see the independent [PBRT point/spot derivation](https://pbr-book.org/4ed/Light_Sources/Point_Lights).

Required strict cutover in EX07-C:

- Remove `AttenuationModel`, its point/spot getters/setters, and local
  `decay_exponent`/`DecayExponent` fields and accessors from native/script APIs,
  source/packed formats, editor persistence/transport and tooling. Do not keep
  a one-valued selector, ignored parameter, alias or compatibility branch.
- Migrate all repository producers, imported-light translation, examples,
  authored assets, fixtures, serializers, packers/dumpers and consumers together.
  New source schemas reject either obsolete field even when its value was the
  former default. Old packed layouts are rejected, not reinterpreted.
- Keep base lux/lumens, tint, per-light compensation, range and spot cone pairs.
  Forward/deferred and qualification references use the same physical model.
  Source radius follows the approved finite-emitter decision D3. Directional CSM
  `distribution_exponent` is unrelated and remains supported.
- LP16/LP17 become removal obligations: verify absent API/wire fields, obsolete
  source and packed-input rejection, migrated producer output and physical
  point/spot round-trip/rendered results. No artistic-mode tests or GPU fields
  remain in the target contract.

### D3 — local source radius

**Approved 2026-09-22: A — physical source extent. Implementation pending.**
Retain local source radius with the native declared shapes: an emission sphere
for point lights and an emission disk for spots. Conserve authored total flux;
derive diffuse and specular response from the same emitter model. Radius zero
uses the punctual contract and is the limit of the positive-radius model.
See the independent [area-light formulation](https://pbr-book.org/4ed/Light_Sources/Area_Lights).

Required mathematical and implementation closure:

- Define emitter orientation, sidedness and angular emission, and normalize
  emitted flux before applying the declared finite-range approximation. A spot
  disk's emission profile must recover the agreed punctual cone distribution in
  the radius-zero limit; do not assume a Lambertian disk automatically does so.
- Define near-field, emitter-surface/interior, cone and range boundaries. Derive
  conservative bounds from that same model. The initial center-only range/cone
  proposal is not frozen: finite extent must not be clipped from valid receivers.
  Existing shadow projections/caster bounds must cover the corresponding
  receiver influence; source-radius mutations invalidate those derived products.
- Use a physically derived production approximation with independent numerical
  integration and predeclared error bounds for diffuse and specular components,
  total response, flux and the punctual/far-field limits. A highlight-width
  adjustment alone does not implement this approved source model.
  Component approximation budgets must fit the existing physical/material
  acceptance budget; do not relax the total budget to admit an approximation.
- Keep source radius separate from the punctual 1 mm numerical guard. Preserve
  finite output and meaningful limiting behavior without arbitrary near-field
  floors or changing authored light power.
- Preserve the existing fixed-PCF/contact-ray visibility approximation. This
  decision does not add radius-dependent penumbrae or new shadow techniques.
  Correct the native headers' contact-softness promise. Qualify source-response
  integration separately from this declared shadow approximation.

The PBR owner now specifies the exact emitter integrals, domains, stable cone encoding and punctual limit. Approximation implementation and qualification remain B/C work; no runtime capability is claimed by this decision.

### D4 — common BRDF quality target

**Approved 2026-09-22: A — correlated Smith GGX with multiple-scattering
energy compensation. Implementation pending.** Both families currently use GGX distribution,
Schlick Fresnel and a separable Schlick approximation to Smith masking/shadowing,
but they do not implement one numerically consistent BRDF. Source audit adds:

| Observation                                                                                                                                                                                    | Source                                                                                                                                                               | Required remediation                                                                                                                                         |
| ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- | -------------------------------------------------------------------------------------------------------------------------------------------------------------------- | ------------------------------------------------------------------------------------------------------------------------------------------------------------ |
| Forward directional applies 1/pi to the combined diffuse/specular response; its diffuse helper omits 1/pi, while deferred normalizes diffuse alone. Forward local omits diffuse normalization. | `ForwardDirectLighting.hlsli::{EvaluateDirectionalLightDiagnosticTerms,AccumulateLocalLightsClustered}`; `DeferredShadingCommon.hlsli::EvaluateCookTorranceLighting` | Put the normalized lobes in one shared BRDF; apply physical incident light and receiver cosine once. Compare lobes independently.                            |
| Deferred clamps both perceptual roughness and alpha to 0.045, so alpha=r*r cannot fall below 0.045. Its implied minimum perceptual roughness is about 0.212, unlike the forward helper.        | `DeferredShadingCommon.hlsli::{LoadDeferredLightingSurface,DistributionGGX}`; `Stages/Translucency/ForwardPbr.hlsli::DistributionGGX`                                | One named roughness-domain mapping and numerical minimum; stable GGX evaluation and no denominator floor that silently suppresses valid glossy peaks.        |
| Deferred multiplies direct-light response by material ambient occlusion.                                                                                                                       | `DeferredShadingCommon.hlsli::EvaluateCookTorranceLighting`                                                                                                          | Keep ambient/indirect occlusion separate from per-light direct visibility. Unoccluded direct illumination must not disappear when ambient occlusion is zero. |

Paths are under the same shader root as the review table. Preserve the existing
metallic-roughness/specular material inputs and default dielectric F0 mapping;
this decision introduces no new material type or authored control.

Required common model and qualification:

- Use height-correlated Smith GGX, Schlick Fresnel and normalized diffuse with
  multiple-scattering energy compensation. Replace the old separable Schlick
  masking approximation; do not keep selectable old/new BRDF paths.
- Preserve metallic-roughness/specular authoring and the existing dielectric F0
  mapping. Define diffuse/specular energy allocation together: restoring missing
  specular scattering must not create energy in the combined response.
- Derive one numerically stable roughness mapping/minimum and common grazing,
  degenerate-vector and normal-handling rules. Do not hide unit/normalization or
  precision defects with exposure, intensity or material adjustments.
- Qualify the BRDF independently for reciprocal response, nonnegative finite
  output, reflected-energy bounds and a white furnace with unit-reflectance
  conductors across roughness and view angle. Include dielectric/metal mixtures
  and separate diffuse/specular probes; a test calling the production helper is
  not an independent oracle.
- Integrate approved D3 finite emitters against this same BRDF. Align existing
  preintegrated BRDF data and consumers where their shared model changes; check
  direct/indirect consistency without adding a new GI/IBL product family.

The earlier single-scattering option is not the target. Its omitted-scattering
energy loss was not an energy-creation/conservation violation; the approved
upgrade explicitly accounts for that missing response. The PBR owner now fixes the symmetric compensation and reciprocal diffuse coupling, integration-data contract and numerical bounds. Reference/renderer qualification remains open; choosing the model is not a performance result.

The [Filament BRDF derivation](https://google.github.io/filament/main/filament.html)
provides a primary reference for correlated GGX, energy compensation, roughness
mapping and the separation of AO from direct lighting. It is not Oxygen's
independent acceptance oracle. The shared-model cutover includes existing BRDF
integration/LUT/IBL consumers where their contracts require it; do not retain
mismatched direct/indirect models.
Freeze formulas and numerical budgets before shader candidates or captures.

### D5 — hemispherical spot support

**Approved 2026-09-22: A — support the 90-degree soft-cone endpoint.**
The initial property inventory proposed
`outer < pi/2` from the current single-perspective shadow implementation. That
would exclude valid imported content: [KHR_lights_punctual](https://github.com/KhronosGroup/glTF/blob/main/extensions/2.0/Khronos/KHR_lights_punctual/README.md)
permits `inner < outer <= pi/2`. The current glTF adapter retains these angles,
while the deferred proxy clamps outer cosine and the spot shadow setup uses one
perspective projection, which cannot represent a 180-degree full field of view.

Accept the 90-degree outer half-angle with a soft cone. Route conventional spot
shadows through the existing cube/multiple-face technique whenever a single
projection cannot conservatively cover supported source influence, including
finite disk extent. Keep explicit per-light projection metadata and budget all
required faces. Preserve the equal-angle hard-cone extension below 90 degrees.
Rejecting valid hemispherical imports was not selected. This adds routing and
coverage cases, not a new shadow algorithm or an angular clamp.

Do not silently clamp imported cones or keep a truncated proxy/map while the
lighting evaluator accepts the full source. The finite disk emission domain and
exact hard-cone boundary must be explicit in the physical equations. All other
physical/source/BRDF decisions remain settled.

### D6 — default resource envelope

**Approved 2026-09-22: A — 4 GiB total / 128 MiB compact indices.** A standalone D3D12 query on the reference
RTX 3080 (10 GiB, driver 610.62) now confirms allocation requirements for the
current typeless D32S8 resource descriptions. No shadow resources were allocated
and no rendering/performance claim follows from this probe. See
[allocation requirements](../../../out/build-ninja/analysis/vortex/exposure-lightbench/ex07a/allocation-requirements.json).

The queried medium narrow-spot set is 512 MiB; using six-face maps for all eight
spots makes it 832 MiB. Corresponding maximum-resolution sets are 2,048 and
3,328 MiB. Two distinct medium wide-spot view sets cost 1,664 MiB; two overlapping
generations of those sets cost 3,328 MiB, before other lighting resources.
These are conservative separate-map examples, not a requirement to duplicate
view-independent maps. The renderer must charge actual unique allocations.

The subsequent [shadow-memory review](EX07-shadow-memory-review.md) validates
stencil ownership and selects depth-only conventional maps, compatible same-frame
local-map sharing and bounded allocator reuse/growth. The figures above remain
the queried **current D32S8 baseline**, not the optimized target. Native large-map
allocation queries halve those map sizes for D32; the GPU format probe matches
depth/PCF results. Production cutover and timing remain unqualified. Scene/custom
stencil and VSM products retain their own owners. D6's ceilings are unchanged;
they do not establish a comfortable whole-engine working set on the 10 GiB GPU.

Approved configurable defaults, not preallocations:

| Profile                  | Renderer-wide lighting/shadow ceiling | Aggregate compact-index sublimit, included in the ceiling | Tradeoff                                                                                                                                                                                                                   |
| ------------------------ | ------------------------------------: | --------------------------------------------------------: | -------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| Approved default profile |                                 4 GiB |                                                   128 MiB | Covers the queried two-view medium map replacement example with remaining room for other lighting products, or one maximum wide-spot set. Higher allowed memory use, bounded by the driver/renderer allocation budget too. |

The ceiling covers lighting-owned records, grids/lists/scratch/status, BRDF data,
shadow/contact products and distinct cached/retired generations. Charge backend
allocation sizes, not just authored payload bytes. Shared backing/heap usage
must be accounted once at the owning allocator and bounded by the renderer's
parent memory budget; do not ignore heap slack or charge one shared heap to each
view. Driver-budget availability remains an additional admission check, not a
reservation or a guarantee. A smaller device/application may explicitly configure
a different ceiling; requested and effective availability are caller-visible.

Compact-index exhaustion uses complete-list encoding while required products
still fit the total ceiling. The sublimit grants no permission to drop lights.
No scene-light count is invented from these examples: publish checked backend/
representation bounds and the candidate's complete required/available byte costs.
The approved correctness/performance workloads remain required; these budgets
do not qualify them or waive any lifecycle/performance gate.

These decisions concern supported fields, their physical meaning and capacity.
D2 explicitly removes two previously retained controls; all other retained
fields still require real consumers. Scene-owned directional authority,
physical units and the approved performance workload remain unchanged.

## Execution and failure review

### Final source-to-remediation coverage

The initial findings remain source evidence at `09aa65362`; the owning contracts
now resolve their destinations, including these additional checks:

| Source concern                                                                 | Frozen destination / regression obligation                                                                                                        |
| ------------------------------------------------------------------------------ | ------------------------------------------------------------------------------------------------------------------------------------------------- |
| Raw mutable light access and partial editor property batches                   | One validated scene commit with owned C++20 descriptors/results; no partial edit or missed invalidation. LP01-LP32.                               |
| glTF spot candela conversion uses only outer angle                             | Convert with the integral of the complete two-angle profile; preserve imported peak candela, including 90-degree soft cones.                      |
| Local ranges are floored and spot proxy/shadow cones clamped                   | Zero range has no influence; exact finite-emitter support and conservative bounds; no angular/range substitution.                                 |
| Deferred/forward camera-to-receiver view vector used for orthographic surfaces | Constant orthographic V, signed native near/far retained, matching linear grid and CSM depth conventions.                                         |
| First-view-dependent preparation and missing late-view products                | Prepare the complete active family; mixed-capability/reordered/auxiliary views cannot suppress later lighting publication.                        |
| Current disk-scale fourth component is stored/hashed but unused                | Retain RGB scale only; no invented opacity semantics. Strict API/model/source migration.                                                          |
| Physical helpers label a 1:1 lux value as watts or normalize both lobes by pi  | One photometric unit chain and normalized common BRDF; resolved RGB evaluation data, no double tint/EV/P.                                         |
| Old scene and shadow interface sketches conflict with the target               | Scene version 7, explicit slot authority, typed indexed shadow records and obsolete-field/layout rejection; historical evidence stays historical. |

The [property inventory](../lld/lighting-properties.md) owns exact field/default/
transport obligations, the [wire contract](../lld/lighting-gpu-abi.md) owns byte
layout and the [PBR owner](../../renderer-core/physically-based-rendering.md) owns
equations/tolerances. These are implementation requirements, not claims that
current source has been repaired.

### Scheduling and caller outcome

The [LightingService LLD](../lld/lighting-service.md#3-gpu-execution-and-synchronization)
owns this sequence. Its implementation changes must cover:

1. Capture accepted scene generation, selection revision and all active views
   once; derive shared physical records once. Each view uses its finalized
   content rectangle/projection, not the first view's constants.
2. Preflight scene/count/byte arithmetic and all required allocations. A child
   allocation failure prevents valid publication of the complete package.
3. Record upload visibility, per-view reset/count/scan/scatter or complete-list
   fallback, UAV dependencies and transition to shader reads on the graphics
   recorder. A descriptor allocation or CPU upload is not a recorded culler.
4. ShadowService prepares identity-indexed shadow maps/contact depth before
   lighting reads. Culling can precede depth writes; sampling cannot.
5. All enabled light consumers check the matching validity product. Output and
   exposure/history writes must be gated by the same-submission GPU result;
   invalid views produce an explicit failure presentation, with other panes/UI
   operational. A delayed CPU diagnostic cannot make the failed frame valid.
6. Submission/discard callbacks settle pending products. Retire all buffers,
   constants, descriptors and maps at their last-consumer fence, including
   copies/readbacks; retry with fresh resource/view generations after recovery.

CPU status must carry outcome (disabled/empty/pending/applied/failed), reason,
scene/selection/frame/view identity, failing stage, source/field where known and
64-bit required/available quantities with units. Distinguish invalid input,
unsupported enum, assignment conflict, scene-count limit, byte budget,
allocation/descriptor failure, missing input, GPU build failure, discard and
device loss. Use the existing result/enum/logging conventions; no per-frame
warning loop. Last-valid output may be shown only with an explicit stale state
and cannot be metered or reported as a valid timing sample.

## Verification obligations and current evidence

Implementation checkpoint: `ClusterLightRange`, `LightGridMetadata`, `LightGridBuildStatus`,
`LightGridPassConstants`, `LightShadowReference` and `DirectionalShadowRecord`
now have canonical CPU/HLSL definitions and layout assertions. Array indices use
distinct Oxygen `NamedType` wrappers and symbolic sentinels. The new
`Oxygen.Vortex.LightingGpuAbi.Tests` target passes **8 Debug / 8 Release** tests
using Graphics-owned upload, compute and readback, including nonzero element
indices, adjacent records, high-bit words and a deliberately changed upload lane.
[Debug results](../../../out/build-ninja/analysis/vortex/exposure-lightbench/ex07a/vortex.lightinggpuabi-debug.json)
and [Release results](../../../out/build-ninja/analysis/vortex/exposure-lightbench/ex07a/vortex.lightinggpuabi-release.json)
are partial ABI evidence. Evaluation records, full bindings, projection records,
matrix probes, catalog/capture analysis and complete producer/consumer migration
remain open. The spatial culler has not been connected to these records.
The grid-metadata producer test also required repairing Core `ResolvedView`
validation: finite signed orthographic near planes are accepted; perspective
near remains positive and every far plane remains finite and above near. Core
view and LightingService suites each pass 5 tests in both configurations.
[Checkpoint manifest](../../../out/build-ninja/analysis/vortex/exposure-lightbench/ex07a/abi-foundation-checkpoint.json)
records source identities, commands and remaining validation. This dependency
repair does not qualify downstream orthographic shading/culling/shadows.

The subsequent consumer checkpoint closes the two reviewed lookup gaps:

- Forward evaluation uses one iterator for compact, complete and empty ranges.
  Complete ranges enumerate all local records without an index descriptor or
  index-buffer read. The unculled publisher now uses this encoding directly;
  its redundant identity-index allocation/upload is removed.
- Shading/debug lookup consumes canonical grid metadata, subtracts content origin
  and uses signed linear orthographic or logarithmic perspective slices. Callers
  retain signed camera-forward depth. The retained VSM shader reads the same
  metadata/iterator and no longer carries duplicate grid fields; VSM remains
  inactive and is compile-qualified only.
- Native behavior cases cover absent index SRVs, empty/compact/complete ranges,
  31/32/33 and 4,096 lights, rejected malformed ranges, fractional nonzero origins,
  partial tiles and near/far slice boundaries. The ABI/behavior target passes
  **12 Debug / 12 Release**, LightingService passes **5 / 5**, and the two affected
  rendered lighting/HDR-history regression cases pass in both configurations.
  All six changed C++ files are oxytidy-clean with no coverage gaps. The
  forward-light RenderDoc analyzer understands complete ranges; no new capture
  is claimed by this checkpoint.

[Consumer checkpoint evidence](../../../out/build-ninja/analysis/vortex/exposure-lightbench/ex07a/lookup-consumer-checkpoint.json)
records source hashes and results. This is still a complete-list baseline, not
spatial culling. Culler/lookup cell equivalence, full binding/evaluation/shadow
record migration, same-submission failure presentation, physical BRDF parity and
full orthographic rendering remain open.

| Gate                      | Owning suite / required evidence                                                                                                                                                                                                                     | Current result                                                                                                                         |
| ------------------------- | ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- | -------------------------------------------------------------------------------------------------------------------------------------- |
| A ABI                     | CPU size/alignment/every-offset assertions; D3D12 upload/decode/readback of two distinct local records and directional records, integer high-bit patterns, sentinels, reserved zeros and nonzero element indices; matching catalog/reflection checks | Six wire record types have native Debug/Release proof above; evaluation/binding/projection records and consumer migration remain open. |
| A property completeness   | LP01-LP32 retained/removal mapping, whole-candidate ingress and scene-v7 layout                                                                                                                                                                      | Contract frozen; transport/rendered regression implementation remains open.                                                            |
| A capacity/failure freeze | D1/D6 budgets, checked backend requirements, complete-list fallback and caller/output fault cases                                                                                                                                                    | Policy/profile frozen; D3D12 requirements queried. Runtime fault/lifetime qualification pending.                                       |
| B instrument independence | Reuse Graphics offscreen/readback and Vortex exposure lighting fixtures; independent CPU oracle and known GPU signals                                                                                                                                | Not started. Existing HDR tests do not establish photometric correctness.                                                              |
| C ingress/transport       | Scene, Scripting, Cooker, Content, DemoShell and managed/editor owning suites; non-default save/cook/load/PAK plus live edits                                                                                                                        | Required repairs identified; not run for this documentation checkpoint.                                                                |
| C rendering/lifetime      | LightingService, SceneRenderer, Shadows and native GPU fixtures; both paths, invalid presentation/recovery, multiview and delayed/discarded submissions                                                                                              | Not started.                                                                                                                           |

Documentation checks at this checkpoint passed: all 32 inventory IDs occur once,
the new documents' local links/anchors resolve, line endings are LF, the capacity
arithmetic above agrees with the target layouts, and `git diff --check` is
clean. The [check report](../../../out/build-ninja/analysis/vortex/exposure-lightbench/ex07a/contract-review-checks.json)
is documentation evidence only. The [final contract checks](../../../out/build-ninja/analysis/vortex/exposure-lightbench/ex07a/final-contract-checks.json)
additionally cover changed-document local links, 19 declared layout sizes,
BOM/LF preservation and all six approvals. At the documentation checkpoint, no
engine/editor build, production test, GPU sentinel, native capture or performance
measurement had been run; the implementation checkpoint above supersedes that
statement for the new ABI target only. A
standalone C++20 /W4 /WX allocation-query program and CPU mathematical checks
were run as described below.

The additional [CPU mathematical check](../../../out/build-ninja/analysis/vortex/exposure-lightbench/ex07a/mathematical-checks.json)
passed 1,440 reciprocal-pair cases (maximum absolute difference 2.78e-17),
the alpha=1 analytic unit-reflectance furnace check (9.63e-13), reflected-energy
bounds over the sampled domain and spot solid-angle integration (1.12e-15
relative), analytic finite-source irradiance (1.90e-14 relative) and projected
directional-disk illuminance (1.68e-12 relative). The importance-sampled alpha=1 directional-moment check differs from
its analytic value by at most 1.11e-4. This is a draft-model consistency check,
**not** the <=1e-5 production-reference uncertainty certificate required by B.
The [allocation query](../../../out/build-ninja/analysis/vortex/exposure-lightbench/ex07a/allocation-requirements.json)
measures resource requirements on the reference adapter without allocating maps.
Neither artifact qualifies rendered images, GPU ABI, live resource lifetimes or
performance. Their probe sources are stored beside the reports.

Local UE5.7 source inspected as implementation reference, not an Oxygen oracle:
`F:/Epic Games/UE_5.7/Engine/Source/Runtime/Renderer/Private/LightGridInjection.cpp`
(`PrepareForwardLightData`, `ComputeLightGrid`), `LightRendering.cpp`
(`RenderLights`), and shaders `DeferredLightingCommon.ush`
(`GetLocalLightAttenuation`, `GetCapsule`), `BRDF.ush` (`SphereMaxNoH`). Do not copy
its bounded-list overflow behavior or distance regularizer into Oxygen's agreed
physical/failure contract.

**Design disposition:** D1-D6 are approved and the target mathematical, wire, persistence, resource and failure contracts are frozen in their owning documents. No product decision remains queued.

**Remaining EX07A implementation gate:** implement the canonical CPU/HLSL records and assertions, migrate the conflicting interfaces together, and pass native GPU sentinel decoding/catalog checks before connecting the culler. EX07B reference qualification, EX07C repairs and EX07D-F correctness/performance delivery remain open. Documentation/model checks do not close EX07-04/08/10/11/12 or EX07-GATE.
