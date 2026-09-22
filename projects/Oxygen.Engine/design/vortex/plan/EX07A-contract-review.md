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

The next evaluation-record prerequisite adds
`Lighting/Internal/LightPhotometry.{h,cpp}`: checked per-component tint/EV
resolution into directional lux and point/spot candela, double-precision
normalization, and stable squared-half-angle cone parameters. Zero flux/tint
avoids exponent evaluation; nonzero overflow or positive underflow outside the
normal FP32 domain returns a typed error for the whole RGB result. Cone support
that cannot survive FP32 transport is rejected rather than widened. The approved
90-degree soft endpoint and hard cones below 90 degrees are supported.

Twelve focused CPU cases include an independent cosine-domain angular integral,
extreme compensation, tinted results whose untinted scalar would overflow,
normal-float endpoints and narrow cones lost by float cosine. LightingService
passes **17 Debug / 17 Release** tests; all three added C++ files are oxytidy-clean
with no suppressions or coverage gaps. Results are
[Debug](../../../out/build-ninja/analysis/vortex/exposure-lightbench/ex07a/photometry-debug.json)
and [Release](../../../out/build-ninja/analysis/vortex/exposure-lightbench/ex07a/photometry-release.json).
This prerequisite is now connected in the working evaluation-record migration
below. These CPU checks do not qualify the EX07B oracle, GPU source integration
or BRDF.

### Evaluation-record checkpoint

This checkpoint replaces the local/directional evaluation records with the
80/64-byte contracts, the frame header with the 96-byte contract, and deferred
draw constants with the 80-byte contract. Selection indices and atmosphere-array
slots have distinct strong types. Physical conversion runs once during
preparation; deferred packets borrow those resolved records. Invalid preparation
returns a typed failure and invalidates prior CPU publication. GPU lookup checks
the full frame identity and expected view-publication generation carried by the
64-byte view root. Scene lifetimes have non-recycled identities.

The disconnected, truncating culler and `PositionalLightData` decoder are
retired, including catalog/prewarm entries. `LightCullingConfig` is CPU-only.
The active complete-list baseline is preserved; replacement spatial recording
remains open. The obsolete private deferred-packet copy in SceneRenderer is
also removed. No old/new wire compatibility adapter is introduced.

Validation passes **390 test cases** across Debug and Release: LightingService
23/23, native ABI/lookup 17/17, SceneBasic 115/115, HDR lighting 2/2,
plus Debug SceneRendererDeferredCore 64, ShadowService 11 and SceneAsyncTraversal

1. The deferred-matrix probe uses actual 256-byte-aligned CBVs. Four fresh
   RenderDoc forward captures (both, point, spot and neither local light) decode
   the 80-byte records and 96-byte header, check integer identities/reserved zeros
   and matching build status, and verify the expected lighting presence/absence.
   Evidence is under the existing `ex07a` directory: `final-Oxygen.*.json`,
   `records-hdr-{debug,release}.json`, `scene-async-debug.json`,
   `deferred-cbv-debug.json` and `forward-records-*-report.txt`/`*_capture.rdc`.
   The HDR oracle now accounts for lumen-to-candela conversion and finite-range
   attenuation. Invalid negative flux rejects the view while retaining the prior
   exposure history; this does not qualify all GPU failure/history paths.

The default MultiView visual check exposed light-volume far clipping: camera
far depth 100 m versus point/spot support of 300/250 m. Both draws initially
left the entire HDR target unchanged. Disabling Z clipping for local-light
proxies restores their contribution while retaining XY/W clipping, culling and
the selected depth test. The repeat capture changes 1,540/933 sampled pixels
for point/spot respectively; the user confirmed the visible spotlight fix.
`multiview-spot-report2.txt` and `multiview-spot-fixed-report.txt` preserve the
before/after evidence.

Oxytidy completed 77 contexts across the then-current 35 changed C++ files,
including headers and tests, with no failed contexts or coverage gaps and 415
reported warnings. Subsequent targeted fixes removed identified new diagnostics.
The expanded 38-file run included the HDR fixture changes but was invalidated
by inputs changing during analysis; it is not a clean final lint certificate.
No blanket warning suppression was added. The checkpoint retains this lint
limitation rather than delaying the requested commit for a broader warning audit.

Full shadow
projection/frame migration, multi-directional selection, BRDF moment publication
and complete same-submission failure/history handling remain open. The new
record layouts do not claim finite-emitter integration or BRDF parity.

### Cascade-record checkpoint

`ShadowCascadeBinding` now uses the approved 128-byte layout in the active CPU
writer, depth producer and surface/volumetric shader readers. Surface descriptors
and array layers are integer fields with distinct C++ types; bias, inverse
resolution, transitions and fade endpoints have explicit fields. The old
float-packed cascade metadata is removed. Assertions cover every member offset,
alignment, size and copy/layout traits.

The native ABI suite passes **18 Debug / 18 Release** cases; ShadowService passes
**11 Debug / 11 Release** cases. The new probe decodes two adjacent records from
a nonzero starting index, checks every field, transforms four basis vectors to
check matrix orientation, and preserves high-bit descriptor/layer values and
invalid sentinels. Reserved words remain zero. The production shader archive
also rebuilds successfully.

The `consumer-visual` MultiView RenderDoc capture checks eight cascade records
across two directional-light draws, including matching descriptors actually read
by the pixel shader. Reports are `cascade-{abi,service}-{debug,release}.json` and
`cascade-record-report.txt` beside `cascade-record_capture.rdc` under `ex07a`.
The capture validates record publication and consumption; it does not certify
coverage policy, multiview resource lifetime or physical BRDF correctness.
Oxytidy ran on all six changed C++ sources/headers with tests included and no
failed contexts or coverage gaps. Edited-line findings were fixed without new
suppressions; existing whole-file findings remain in the reports.

The enclosing shadow header still contains inline arrays and is temporarily
3,392 bytes because its cascade elements are now 128 bytes. The required
112-byte header with separately published arrays, local projection migration,
multi-directional selection and the remaining EX07A gates remain open. This is
an in-place migration checkpoint, with no alternate old cascade representation.

### Local-projection record checkpoint

The old `SpotShadowBinding` and `PointShadowBinding` types are removed and
replaced in the active setup, depth-pass and shader consumers by
`ProjectedLocalShadowRecord` (128 bytes) and `CubeLocalShadowRecord` (448 bytes).
Descriptors, array layers and source-selection indices use their distinct strong
types. The cube layer identifies the first face directly, without float decoding
or deriving it from the light index. Assertions cover every field offset and the
record size/alignment/copy traits.

Both sides retain linear reversed depth. For the projected record, perspective
clip W supplies axial receiver distance; the depth producer gets the same
direction from the projection matrix's homogeneous row. The CPU regression
compares that W against an independent dot product. Slope bias remains in the
depth-pass owner; it is no longer packed into a sampling vector.

The native suite passes **19 Debug / 19 Release** cases, including all six cube
matrices and every projected/cube lane across adjacent records with nonzero
starting indices. ShadowService passes **11 Debug / 11 Release** cases, including
selection indices with skipped nonmatching lights. Both shader archives build.
Oxytidy completed all eight changed C++ sources/headers, including tests, with
no failed contexts or coverage gaps. The final ABI test run is clean; remaining
whole-file diagnostics are recorded without adding suppressions.

`local-shadow-{abi,service}-{debug,release}.json` records the suite results.
`local-shadow-binding-report.txt` verifies the production RenderDoc upload's
surface/layer/selection identity against the corresponding canonical light and
the pixel shader's actual descriptor reads. `local-shadow-coverage-report.txt`
records 1,540 positive point-light samples and 933 spot-light samples, matching
the earlier default-scene contribution check. The capture is
`local-shadow-record_capture.rdc`, all under the existing `ex07a` directory.

The 112-byte shadow header and separate array publication remain unimplemented.
Existing local range/near-plane floors, cone clamping, fixed capacities and
point/spot routing still require the approved support/failure migration; this
record checkpoint does not qualify finite-source or 90-degree spot shadows.

### Shadow-header and array-publication checkpoint

The 3,392-byte inline payload is replaced by the approved **112-byte
`ShadowFrameBindings`**. `ShadowFrameData` owns the CPU preparation/inspection
vectors; directional families, cascades, projected local records and cube local
records are uploaded separately through the existing transient-buffer lifetime
owner. The old `DirectionalShadowFrameData` interface is removed. A header is
published only after every required record array succeeds; allocation failure
leaves the view without a published shadow header, and SceneRenderer rejects
that view's recording.

The header carries the full lighting scene/selection/frame/view generations and
build-status descriptor. Shader loads verify those identities. Directional
surface and volumetric lookups resolve their family through selection-indexed
shadow references. The shadow-mask debug view resolves the family source from
its record, independently of atmospheric assignment. Contact fields are present
and disabled; this checkpoint does not create a contact-shadow product.

Debug and Release each pass **20 native ABI, 13 ShadowService, 23 LightingService
and 64 SceneRendererDeferredCore tests** (240 test executions total). The new
header probe checks all 28 words across adjacent records; service tests check
array counts/descriptors, full-width generations, filtered source indices and
injected staging-map failure without header publication. Shader archives build
in both configurations. Oxytidy covered all 22 changed C++ files, including
headers and tests, with no failed contexts or coverage gaps. Diagnostics on
changed code were repaired; whole-file warning reports remain available without
new suppressions.

`shadow-header-{LightingGpuAbi,ShadowService,LightingService,SceneRendererDeferredCore}-{debug,release}.json`
records the suites. `shadow-header_capture.rdc` and `shadow-header-report.txt`
verify the 112-byte header, exact array strides/counts, source identity, shared
validity dependency and distinct view generations at six production draws
across two views. The checked-in analyzer is
`tools/vortex/AnalyzeRenderDocShadowRecords.py`; use the existing RenderDoc runner
with the MultiView `consumer-visual` recipe. This proves publication/consumption,
not all coverage, lifetime or physical-response obligations.

All canonical record layouts have now migrated. Multi-directional CPU selection,
BRDF moment publication, complete local support/routing, capacity rejection,
contact-product connection and remaining failure/lifetime interfaces still need
their owning work before the full EX07A gate can be assessed. No EX07B reference
qualification or EX07C end-to-end correctness closure is claimed.

### Directional-array checkpoint

`FrameLightSelection` now owns the complete ordered directional collection from
its scene resolver, with each source's native node identity and resolved
atmosphere-slot identity. The optional primary-only record is removed. Checked
photometric resolution, forward publication and deferred packets consume the
same collection. Deferred lighting emits one fullscreen contribution per source;
its diagnostic surface descriptors are a collection as well. Shadow resolution
hints retain the existing strongly typed scene enum through preparation and
allocation.

Shadow families keep source-selection indices after filtering out unshadowed
lights. Each source has its own conventional surface and requested cascade
count/resolution, with unused source allocations pruned from the cache. Cascades
are flattened for GPU publication with explicit family offsets. There is no
shared singleton directional surface or optional-primary fallback. The existing
local-fog primary-source lookup now resolves atmosphere slot 0 explicitly rather
than assuming directional-array element 0. The obsolete scene warning about more
than two ordinary directional lights is removed; the two atmosphere slots remain
separate from direct-light capacity.

Debug and Release each pass **25 LightingService, 14 ShadowService, 66
SceneRendererDeferredCore and 20 native ABI tests** (250 test executions).
Eight Scene directional-resolver tests also pass in Debug. The exposure benchmark
target builds against the migrated resource-inspection API; no performance
result is claimed. Cases cover an unassigned source alone, Secondary alone
without promotion, mixed unassigned/assigned sources, duplicate atmosphere claims
in prepared input, filtered shadow identities and unequal per-source cascade
counts/resolutions. Oxytidy covered all 32 changed C++ sources/headers, including
tests and the demo, with targeted follow-up checks after repairs. Whole-file
findings remain recorded; no new suppressions were added.

The opt-in MultiView `--directional-array-proof true` recipe creates three
sources: Primary, an unshadowed unassigned fill, and Secondary. The deferred
capture measures a positive HDR contribution from each source in both views and
verifies distinct 2048/1024 shadow surfaces with 2/3 cascades. The forward capture
checks all three records and two filtered shadow references at five scene draws.
The actual selection order is Secondary/None/Primary; atmosphere slots remain
1/invalid/0, proving they are not array positions. Evidence is
`directional-array-*-{debug,release}.json`, `directional-resolver-debug.json`,
`directional-array-report.txt` and `directional-array-forward-report.txt` beside
the corresponding captures under `ex07a`. The analyzers are checked in under
`tools/vortex/AnalyzeRenderDocDirectionalArray*.py`.

The user's live Release review exposed a same-frame transient-descriptor reset
that capture timing had concealed. The
[separate flicker fix](EX07A-offscreen-flicker-validation.md) records the failing
regression, repair and the user's confirmation that both lower views are stable.

This checkpoint does not close retained-property ingress migration, general
multi-source fog/shadow integration, finite-source/wide-spot support, memory
admission, deferred-CBV lifetime, BRDF model publication or the full EX07 gate.
The remaining EX07A obligations still require an explicit completion audit.

### Deferred-constant lifetime checkpoint

A queued-view regression demonstrated that the old persistent deferred CBV buffer
was overwritten by a later recording: the first point light's stored X position
changed from 11 to 21. `DeferredLightConstantsPublisher` replaces that buffer and
its mutable descriptor slots. Each recording publishes one aligned batch through
the existing upload arena, with 256-byte CBV slices containing the canonical
80-byte records. Batches and their descriptors remain owned by their frame slot;
repeating a sequence preserves them and slot reuse retires them under the
existing frame-fence contract. Padding is zeroed. The old buffer fields and
unmap/recreate path are removed.

CBV publication failure now propagates through LightingService to rejection of
the view recording. A failure-injection case verifies that no scene binding is
published and that a later successful recording recovers. The old overwrite
regression fails before the repair and passes afterward.

The shared upload ring now declares constant-buffer usage on creation, growth
and trimming. This fixes the warning observed by the user while testing the new
path. D3D12 upload buffers retain their generic-read state and resource flags;
no warning filter or suppression was added. An **180-frame native Release** run
of the user's exact three-light/offscreen layout, with the D3D12 debug layer
enabled, exits successfully without warning/error messages.

Debug and Release each pass **68 SceneRenderer, 18 upload-ring and 20 native ABI
tests** (212 test executions). The production capture decodes the new CBVs,
checks their alignment/source identities and observes all six directional
contributions across two views. The analyzer identifies bindless CBVs by their
binding metadata rather than requiring a dedicated backing-buffer name.
Oxytidy covered all 12 changed C++ files; the new publisher is checked separately
after fixes. Existing whole-file findings remain recorded without suppressions.

Evidence under `ex07a`: `deferred-cbv-before.log`,
`deferred-cbv-{SceneRendererDeferredCore,RingBufferStaging,LightingGpuAbi}-{debug,release}.json`,
`deferred-cbv-release-warning-check.log`, `deferred-cbv_capture.rdc` and
`deferred-cbv-report.txt`. This qualifies the repaired constant lifetime and
failure path, not every EX07 capacity/submission/resource-lifetime scenario.

| Gate                      | Owning suite / required evidence                                                                                                                                                                                                                     | Current result                                                                                                                  |
| ------------------------- | ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- | ------------------------------------------------------------------------------------------------------------------------------- |
| A ABI                     | CPU size/alignment/every-offset assertions; D3D12 upload/decode/readback of two distinct local records and directional records, integer high-bit patterns, sentinels, reserved zeros and nonzero element indices; matching catalog/reflection checks | Canonical wire records have native Debug/Release proof; remaining source-selection, validity and lifetime interfaces stay open. |
| A property completeness   | LP01-LP32 retained/removal mapping, whole-candidate ingress and scene-v7 layout                                                                                                                                                                      | Contract frozen; transport/rendered regression implementation remains open.                                                     |
| A capacity/failure freeze | D1/D6 budgets, checked backend requirements, complete-list fallback and caller/output fault cases                                                                                                                                                    | Policy/profile frozen; D3D12 requirements queried. Runtime fault/lifetime qualification pending.                                |
| B instrument independence | Reuse Graphics offscreen/readback and Vortex exposure lighting fixtures; independent CPU oracle and known GPU signals                                                                                                                                | Not started. Existing HDR tests do not establish photometric correctness.                                                       |
| C ingress/transport       | Scene, Scripting, Cooker, Content, DemoShell and managed/editor owning suites; non-default save/cook/load/PAK plus live edits                                                                                                                        | Required repairs identified; not run for this documentation checkpoint.                                                         |
| C rendering/lifetime      | LightingService, SceneRenderer, Shadows and native GPU fixtures; both paths, invalid presentation/recovery, multiview and delayed/discarded submissions                                                                                              | Not started.                                                                                                                    |

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
