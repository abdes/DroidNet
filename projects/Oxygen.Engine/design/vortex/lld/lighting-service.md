# LightingService LLD

Current execution and qualification status lives only in
[tracker section 3.4](../IMPLEMENTATION_STATUS.md#34-slice-7-work-items).
The [EX07 plan](../plan/EX07-lighting-correctness-and-scalability.md) owns execution,
workloads and gates. This LLD owns data, execution, publication and failure
semantics. The [A review checkpoint](../plan/EX07A-contract-review.md) records
source evidence, approved decisions and remaining implementation/validation
work. Its [GPU ABI contract](lighting-gpu-abi.md) and
[property inventory](lighting-properties.md) make the migration reviewable before
changing consumers. Target layouts and admission bounds are not current support.

## 1. Scope and responsibility

LightingService owns shared lighting preparation, per-view light access and
deferred direct-light evaluation. EX07 owns review, repair, optimization and
validation of this complete path, including pre-existing defects and necessary
dependencies in scene/editor input, shaders, shadows and resource lifetime.
Both correctness and performance must pass; an existing limitation is repair work.

| Stage/consumer | Responsibility                                                                                                                                                                                                                                |
| -------------- | --------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| Stage 6        | Prepare shared immutable light records and record/publish per-view culling products before their first consumer.                                                                                                                              |
| Stage 12       | Evaluate deferred direct lighting into SceneColor from the canonical frame-light selection. Start with directional fullscreen draws and bounded point/spot volumes; algorithm improvements retain this stage owner.                           |
| Forward        | Consume the same physical records, relevant per-view lists and matching shadows, including supported translucent receivers.                                                                                                                   |
| Stage 13       | Indirect/IBL ownership stays under its [own contract](../plan/editor-v01-captured-sky-ibl.md#1-ownership-and-scope). An existing ambient bridge is an explicitly bounded environment input; never duplicate it when Stage 13 takes ownership. |

Use existing renderer, LightingService, ShadowService, publication, allocator,
recorder and fence-retirement owners. The service does not own scene light
objects or elect another sun. No legacy renderer, compatibility payload, hidden
preview light or separate lighting framework is introduced. Qualification uses
opt-in tests/benchmarks observing the production paths.

The [architecture](../ARCHITECTURE.md),
[directional/shadow authoring contract](../plan/editor-v01-rendering-contract.md#3-independent-directional-array-and-atmosphere-assignments)
and [PBR specification](../../renderer-core/physically-based-rendering.md) remain
authoritative. Ground UE5.7 parity comparisons in the matching local
`PrepareForwardLightData`, `ComputeLightGrid` and `RenderLights` source/shader
families under `F:/Epic Games/UE_5.7/Engine`; record exact references with the
implementation. Compilation, historical sketches and prior milestone evidence
do not close EX07. A parity/quality gap remains open unless its changed scope
is explicitly accepted; routine corrections remain EX07 responsibility.

## Exposure-package light calibration

The [physical equations](../../renderer-core/physically-based-rendering.md#physical-light-conversion)
remain directional lux, point flux divided by 4*pi, spot flux divided by the
integrated squared cone profile, inverse-square attenuation with quartic range
fade, the 1 mm numerical guard and zero contribution at zero separation. Reject
zero-solid-angle spots and preserve the equal-angle hard-cone limit.

Keep authored lux/lumens and per-light compensation in the canonical CPU
selection. Apply validated compensation and physical conversion once at the
shared evaluation-data boundary. Both shading families use the same resolved
values, receiver cosine and production BRDF convention. Keep source radius
separate from the numerical guard. Packed albedo/normal/roughness/specular,
working color space and tint energy enter the independent reference; roughness
does not remove dielectric specular. Light records never contain view exposure.
Apply frame-pinned P once at HDR writes and preserve the
[HDR contract](scene-textures.md#exposure-hdr-domain-and-format-inventory).

Approved EX07A D2 removes the local attenuation-model selector and custom decay
exponent across all APIs, persistence, tools and consumers. There is one physical
punctual-light model; do not retain artistic alternatives or an ignored selector.
D3 retains physical point-sphere/spot-disk extent, conserved flux and a shared
source model for diffuse/specular, qualified against independent integration.
Approved D4 selects height-correlated Smith GGX with multiple-scattering
compensation, retaining Schlick Fresnel, normalized diffuse and existing material
inputs. The [PBR specification](../../renderer-core/physically-based-rendering.md#physical-light-conversion)
owns the exact emitter/BRDF equations, domains and numerical budgets.
Directional CSM's independent distribution exponent remains supported.

Shared family preparation captures source identity/properties before environment
or per-view work. Resolve compensated physical sources once; atmosphere consumes
those values and returns derived transmittance metadata for the same identities.
Publishing that metadata must not create a second intensity/selection authority.
Do not let the first view's shading/capability variant mark preparation complete
for a later lit view without publishing its required products.

## 2. Canonical data and interfaces

### 2.1 Owners

C++ paths are under `src/Oxygen/Vortex/`; shaders are under
`src/Oxygen/Graphics/Direct3D12/Shaders/Vortex/`.

| Owner                                                                                      | Responsibility                                                                               |
| ------------------------------------------------------------------------------------------ | -------------------------------------------------------------------------------------------- |
| `SceneRenderer/SceneRenderer.*`, `Types/FrameLightSelection.h`                             | Resolve eligibility and capture the immutable frame selection.                               |
| `Lighting/LightingService.*`, `Lighting/Types/FrameLightingInputs.h`                       | Prepare shared data, schedule per-view GPU work and expose valid publications/results.       |
| `Lighting/Internal/LightGridBuilder.*`                                                     | Derive grid metadata and conservative assignment inputs; own no scene state.                 |
| `Lighting/Internal/ForwardLightPublisher.*`, `Types/LightingFrameBindings.h`               | Own light/list allocations and per-view binding publication.                                 |
| `Lighting/Types/{DirectionalLightForwardData,ForwardLocalLightRecord,LightGridMetadata}.h` | Canonical records/grid semantics and matching CPU/HLSL ABI.                                  |
| `Lighting/Internal/DeferredLightPacketBuilder.*`, `Lighting/Passes/DeferredLightPass.*`    | Derive deferred work from the selection; own service geometry, PSOs and direct accumulation. |
| `Shadows/`, `Types/ShadowFrameBindings.h`                                                  | Indexed per-light shadow products, status and lifetime.                                      |
| `Contracts/Lighting/`, `Services/Lighting/`                                                | Matching decoders, culling, physical evaluation and shader entry points.                     |

Extend these owners in place; add a service-owned grid-recording pass where
needed for actual GPU work. There is no second public forward-light package
layered over `LightingFrameBindings`.

### 2.2 Frame selection and identity

`FrameLightSelection` contains ordered directional and local-light collections,
scene generation and selection revision. Both directional and local entries retain
`scene::NodeHandle source_node`, including its node generation, for CPU mutation,
errors and shadow association. GPU indices
address this immutable snapshot, not persistent scene identities. Use integer
fields for indices, flags and enums rather than encoding them as floats.

Resolve effective node visibility and `affects_world` before selection. Gather
once for the whole active-view family, including auxiliary/offscreen views;
main-view visibility cannot discard another view's lights. Per-view culling
derives lists without modifying selection. Shadow caster eligibility is separate
and includes contributing off-screen casters. Resolve geometry cast/receive rules
independently of light eligibility.

Inputs include each resolved camera/projection, content rectangle, near/far
conventions and view lifetime. Transform, hierarchy, effective visibility,
participation, parameter and role edits invalidate the correct products. Scene
replacement cannot reuse old identities or assume transform edits will repair
otherwise missing invalidation.

### 2.3 Directional-light authority

Publish all eligible directionals in an explicitly counted array. None/Primary/
Secondary is optional atmospheric assignment, not a direct-light eligibility
filter. Two explicit sources support dual suns or sun-and-moon. A lone Secondary
keeps its slot. Validate assignment conflicts across stored components, including
inactive ones, before accepting a revision.

Each record carries direction-to-source, resolved physical intensity, linear
color, flags and explicit atmosphere/transmittance metadata. Shadow association
references source identity, not atmosphere slot or draw order. Both rendering
families evaluate every eligible record. Count zero is valid with no usable
directional-array binding; it does not invalidate local lights.

Only explicitly requested demo actions infer assignments or inject preview scene
lights. They respect authored assignments and ordinary scene lifetime. Production
selection and shaders never silently elect or manufacture a sun.

### 2.4 One local-light GPU contract

The [wire tables](lighting-gpu-abi.md) specify local/directional records,
bindings, metadata, ranges, status and complete dynamic shadow-family offsets.
Those tables own the implemented layouts, assertions and GPU sentinel checks;
the A checkpoint records their validation. There is no second shipping payload.

`ForwardLocalLightRecord` and its HLSL counterpart own one canonical point/spot
evaluation record. Culling decodes it or an explicitly derived typed bounds
buffer with a one-to-one index mapping. A compact bounds buffer is derived data,
not another authored-light authority.

| Semantic group | Required meaning                                                                                       |
| -------------- | ------------------------------------------------------------------------------------------------------ |
| Influence      | World position, effective range and derived inverse range where consumed.                              |
| Direction/cone | Defined emitted-ray direction, stable `sin²(theta/2)` cone parameters and point/spot kind.             |
| Evaluation     | Linear color, resolved physical intensity, supported attenuation parameters and source-radius meaning. |
| Association    | Typed flags and snapshot index; view-specific shadow lookup is separate from shared physical data.     |

The incompatible historical 96-byte payloads and disconnected culler are
retired. The active local evaluation record is 80 bytes, with integer kind,
flags and selection index. Same-size assertions or pointer casts do not prove
ABI compatibility; retain no alternate legacy payload/consumer.

EX07A freezes each record/binding's field types, units, offsets, stride,
alignment, reserved-zero bytes and invalid sentinels. C++ size/offset assertions
and a GPU sentinel decode test cover both local kinds, directionals and integer
flags/indices before consumer changes. Update writers/readers, catalog/reflection
tests and affected SDK consumers together. The frozen table is the wire authority;
no historical struct sketch or guessed final byte size is normative.

### 2.5 Per-view publication

One `LightingFrameBindings` product routes through `ViewFrameBindings` and identifies:

- Frame/selection and view-lifetime generations plus preparation/publication status.
- Shared directional/local SRVs and explicit counts.
- Per-view cluster `(offset, count)` ranges and the complete local-index list.
- Grid dimensions, viewport-relative origin/extent and depth-slice parameters.
- Per-view light-to-shadow mapping and corresponding shadow publication.
- Validity/capacity data required for bounded reads and failure handling.

Use checked integer arithmetic before narrowing sizes/offsets to GPU fields.
Empty, disabled, pending and failed are distinct. Empty frames clear old bindings.
Counts and descriptors identify the executed product and its retained generation.
Reject nonempty selections without a scene lifetime identity, invalid view IDs
and duplicate IDs in the active-view family before publishing any candidate.
View ID zero is valid; only the typed invalid sentinel is rejected. A scene-less
empty publication remains valid and clears prior light data.
Directionals bypass local spatial culling. Lists are conservative for every
declared consumer; opaque-depth/normal rejection cannot silently exclude valid
translucent, two-sided or normal-mapped receivers.

Stage 12 may reuse validated spatial products as derived data. Canonical selection
and Stage 12 retain authority; Stage 6 does not become a second deferred renderer.
Stage 12 consumes the matching published GBuffer material/normal/base-color/custom
products, scene depth, view reconstruction and shadow bindings. Preserve prior
SceneColor contributions such as emissive: volume draws use additive ONE/ONE
accumulation, and a measured alternative must preserve the same radiance sum.
Missing required inputs fail the view instead of producing successful black output.

### 2.6 Operation and API boundaries

These operations define responsibilities; evolve existing entry points in place:

| Operation                | Contract                                                                                                                                             |
| ------------------------ | ---------------------------------------------------------------------------------------------------------------------------------------------------- |
| Prepare frame            | Validate/capture selection and resolved views, preflight capacities and allocate immutable records/view products; return structured success/failure. |
| Record view grid         | Receive the owning recorder and resolved view explicitly; record complete culling/list work and resource dependencies.                               |
| Publish for consumption  | Route a generation-qualified product with its recorded dependencies; CPU submission is not GPU completion.                                           |
| Record deferred lighting | Consume current view, selection and matching shadows; accumulate each direct contribution once.                                                      |
| Inspect status           | Return owned bounded diagnostics with pending/applied/failed generation and reason; expose no internal renderer headers or GPU ownership.            |

Keep editor-facing headers C++20-compatible and preserve native ownership.
Use established Result, logging and enum `to_string` conventions. Normal
preparation/evaluation introduces no blocking GPU readback.

## 3. GPU execution and synchronization

Stage 6 has shared CPU preparation and per-view GPU execution. Record culling
before the first forward/deferred consumer that needs its result, using finalized
view constants. Uploading CPU vectors alone does not establish spatial culling.

1. Validate selection/views, reserve checked capacities and record source uploads.
2. Record per-view grid/list work on the existing graphics queue initially. Reset
   grid/status outputs or publish valid empty zero-count bindings without dispatch
   when no local lights require a grid. Count/scan/scatter or overflow
   subpasses have explicit UAV ordering.
3. Transition outputs to first-consumer states. Establish producer fence/waits
   for cross-queue uploads. Async compute is a measured optimization with explicit
   ownership/dependencies, not an assumed free overlap.
4. ShadowService records shadow production. List construction need not wait for
   shadow texel writes; lighting sampling does. Keep selection/view generations
   consistent across light and shadow products. ShadowService builds dense
   reference arrays from the actual projection records' selection identities;
   LightingService must not predict indices with per-kind counters. After the
   shadow header/arrays publish successfully, attach these descriptors through
   a new immutable lighting header for that view. Validate the scene, selection,
   frame, view and build-status identities before attachment. Invalidate the CPU
   route on a failed replacement; previously recorded readers keep their own
   allocations until their fences retire. Missing required projections reject
   publication, including requests beyond an allocator's current capacity.
5. Forward and Stage 12 consume immutable data; output/composition and diagnostics
   accept only the corresponding valid view result.
6. Existing callbacks resolve submission/discard. Retain buffers, constants and
   descriptors until the final consumer fence completes.

Share source records, not view lists/projections/shadow mappings merely because
dimensions match. Grid lookup uses content-relative pixels and matching projection-specific
view depth. Test viewport offsets, partial tiles, reverse-Z, orthographic
projection and near/far boundaries.

## 4. Capacity, failure and recovery

The [A capacity decision](../plan/EX07A-contract-review.md#d1--admission-capacities)
records approved D1/D6: a configurable **4 GiB** renderer-wide allocation ceiling
and **128 MiB** aggregate compact-index sublimit included within it. These are
uint64 byte ceilings, not preallocations; backend/driver availability is an
additional admission check. Original per-view/count proposals remain withdrawn.
The [bounded shadow-memory work](../plan/EX07-shadow-memory-review.md) reduces
unused storage and duplication without changing these ceilings. Parent allocator
admission accounts for other engine commitments, pending growth and explicit
headroom; the lighting ceiling is not a reservation. Record unique resource
requirements, committed heap/slack and process-local DXGI usage separately.
Account across all views,
in-flight allocations and caches; do not retain fixed four-point/eight-spot
cutoffs. The
complete-list range sentinel preserves every light when compact indices exhaust
their budget; invalid input/allocation/build failures retain the failure behavior
below. Its memory bound cannot be presented as measured performance evidence.

Profile quantities are 4,294,967,296 and 134,217,728 bytes. The index ceiling
includes live, cached and retired index allocations, not one allowance per view
or frame. It may be configured to zero to use complete-list encoding; total
budget must be positive and index budget must not exceed it. Capacity is a ceiling,
not an instruction to allocate the full amount. Reserve complete required
records/bindings/status/shadow/contact products before optional compact-list
growth. Keep existing valid view leases; admit additional requests in a stable
family order independent of command-recording order, and fail only affected views
unless a shared product fails. Unused cached allocations are reclaimable only
after their fences permit it.

All counts/byte products/offsets are checked in uint64 before GPU narrowing.
The uint32 index sentinel reserves 0xffffffff: valid element indices are at most
0xfffffffe and a counted array has at most 0xffffffff elements, further limited
by its actual backend allocation/descriptor capacity. These are representation
bounds, not a claim that a memory-admitted arbitrary light count meets a timing
target. The plan's 1,024/4,096/dense/multiview workloads retain their measured
performance gates. Bound dispatch dimensions and per-workgroup work; do not
introduce unchecked count-dependent execution or silently truncate it.

Declare total-light, directional, per-view index-memory and shadow capacities
separately. They must accommodate the required qualification envelope. Existing
32-entry cells and four-point/eight-spot shadow arrays are audit inputs, not
automatic approval of final limits. EX07A publishes supported limits/reasons to
callers and freezes them before consumer implementation.

In-envelope inputs require complete lists using conservative sizing, compact
allocation and/or bounded overflow storage. A per-cell cap cannot authorize
discarding contributions. The chosen algorithm must satisfy this failure contract:

| Failure point                                                              | Caller-visible behavior                                                                                                                     | Recovery/retained state                                                                                                        |
| -------------------------------------------------------------------------- | ------------------------------------------------------------------------------------------------------------------------------------------- | ------------------------------------------------------------------------------------------------------------------------------ |
| Invalid edit/assignment, unsupported value or known hard scene-count limit | Reject the whole candidate through common ingress, identifying the light/field, reason and requested/allowed value.                         | Accepted scene/settings revision remains unchanged; corrected input is a new request.                                          |
| Cook/load validation                                                       | Reject the asset/candidate; never reinterpret fields or truncate lights silently.                                                           | Preserve the active scene if a replacement fails.                                                                              |
| CPU frame/view preparation or allocation                                   | Return failure for the affected view generation; publish no valid partial package. Include required/available capacity and failing stage.   | Keep authored data, invalidate stale bindings and retry on scheduled preparation when resources/capacity/input change.         |
| GPU-discovered overflow/build error                                        | Publish GPU invalid status in the same dependency chain; lighting/output reject incomplete data, with bounded asynchronous CPU diagnostics. | Rebuild within the supported capacity or report continuing failure; no synchronous readback stall or successful partial frame. |
| Recorder discard/device loss                                               | Discard pending publication and report failure; retire resources through submission/device lifetime.                                        | Rebuild through existing recovery with fresh view/resource generations.                                                        |

A failed view receives explicit failure presentation while application UI and
independently valid panes remain usable. GPU failure status must reach that
presentation decision in the same submission; later CPU readback is diagnostic.
A last-valid image is allowed only as an explicitly stale presentation placeholder,
never current HDR input, metering evidence or a successful benchmark sample.
Missing contributions cannot be presented as a normal current image.

Default failure presentation is an explicit display-space error tile, with a
brief application-owned status label and details in normal diagnostics. Reuse
the output/compositor path; failure indication must not require another large
allocation or enter HDR metering. Offscreen results carry Failed and a reason,
never a successful capture of placeholder pixels. Stale-image presentation is
opt-in and visibly labelled; it is not the default recovery behavior.

Validity is sticky within a generation. Commit Valid only after every required
producer is complete in the dependency chain; grid completion alone is not
shadow/BRDF readiness. A later failure cannot be reset by another pass. Pending
history writes remain uncommitted until final validity; failed generations
cannot replace the last valid history. Submitted cross-queue uploads retain
their producer fences even if the consuming recorder is discarded. Recovery
uses the existing exposure/history discontinuity policy with fresh resource
generations, without fabricated samples or a synchronous diagnostic readback.

Shadow exhaustion never silently clears requested shadows. Reject a known invalid
candidate or fail view preparation as above. Dynamic camera/culling changes must
not introduce undocumented brightest-N selection or automatic quality reduction.

Log transitions into failure with view/selection generation and cause at the
appropriate warning/error level; update when the cause changes and report recovery
once. Keep structured status/counts available without per-frame log spam. Optional
disabled lighting is valid absence. Fault tests verify the application-visible
outcome and recovery, not only the presence of an error string.

## 5. Authored-property coverage

The [LP01-LP32 inventory](lighting-properties.md) maps current native, script,
source/packed and editor ingress to target consumers, invalidation and owning
tests. Missing transport/consumers are explicitly identified. D2 approves
physical-only local attenuation and removal of the selector/custom exponent;
LP16/LP17 now track that strict migration. D3 approves physical local source
extent; D4 approves the common correlated-GGX/compensation model. Their detailed
equations and bounds are in the PBR owner; implementation and independent
renderer qualification remain open. D5 retains hemispherical soft spots; the
inventory also freezes strict scene-v7 records and atomic ingress obligations.
Directional authority is already settled.

EX07A records every retained field's scene definition, editor/script ingress where
exposed, source/packed representation, default/domain, selection/GPU member,
consumer, mutation invalidation and positive/negative/round-trip tests. A missing
consumer or transport is a defect to repair, not a newly deferred feature.

| Field family                                                         | Required verification                                                                                                                                                  |
| -------------------------------------------------------------------- | ---------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| Lux/lumens, linear color, per-light exposure compensation            | Finite domain, zero light, tint energy and exactly-once `2^EV` compensation; camera exposure does not alter authored light intensity.                                  |
| Removed local attenuation selector/exponent                          | D2-approved strict API/source/packed/tooling removal; reject obsolete fields/layouts and qualify the single physical model.                                            |
| Range, position/direction                                            | Valid bounds, coordinate/sign convention, normalization, parent transforms, matching culling/evaluation support and immediate invalidation.                            |
| Inner/outer cone                                                     | Finite domain, ordering, zero-solid-angle rejection and equal-angle limit; validate paired edits atomically.                                                           |
| Source radius/angular diameter                                       | Preserve supported meanings; source radius is not the numerical distance floor. Directional disk diameter retains its declared atmosphere-only meaning.                |
| Visibility, affects-world, mobility, atmospheric assignment          | Effective eligibility, stable identity/conflicts, supported runtime models and rejection of unsupported authored choices.                                              |
| Cast/receive, shadow resolution/bias/normal bias/contact, CSM fields | Separate light/caster/receiver roles, supported shadow consumers and correct per-light association; use existing shadow/contact contracts and eliminate dead settings. |

Declared exclusions remain under the
[capability contract](../plan/editor-v01-deferred-capabilities.md); this audit
does not add new baking or finite-source directional shading. Distinguish those
explicit exclusions from ignored retained settings. No silent removal or legacy
compatibility route is permitted. Validate complete candidates atomically so
nonfinite values, invalid enums, overflow and unsupported input do not reach shaders.
Cover current-format save/cook/load/PAK and live native/editor/script mutations
as each field is implemented, not only during final package validation.

## 6. Resources, shaders and capabilities

LightingService's publisher owns one immutable GGX moment product for all views.
`BrdfMomentData` verifies the compiled model revision and payload hash;
`BrdfMomentResources` creates the RG32Float directional and mean textures in the
texture descriptor domain. Preparation records and submits their one-time copy
on the graphics queue before publishing light headers. Later view recordings
consume that queue-ordered upload without a CPU wait. Discarding a view cannot
discard the independent model initialization. Upload buffers retire through the
graphics reclaimer, including removal of their known resource states; texture
resources and descriptors retire together. A missing/mismatched model or failed
required allocation prevents the complete lighting publication.

`Lighting/Data/GgxModel1.bin` and its JSON numerical evidence are produced by
`tools/vortex/BuildGgxMomentData.py pack`; its `embed` mode generates the private
build header. Generated bytes are not edited by hand. The shader samples loss/B
with FP32 bilinear weights and uses the same model in both direct families.
The common evaluator returns cosine-weighted lobe responses, keeping correlated
visibility bounded at grazing incidence instead of first forming an overflowing
unweighted BRDF. Full numerical/consumer qualification is tracked in
[section 3.4](../IMPLEMENTATION_STATUS.md#34-slice-7-work-items).

Records and per-view grids/maps/constants use existing upload/frame allocators,
immutable in-flight generations and fence retirement. Growth cannot overwrite an
older frame or unregister a live descriptor. Retain service-owned sphere/cone
proxy geometry and PSO caches. Cache derived data only with complete light/view/
scene invalidation. Account for live, in-flight, retired and cached memory and
require stable warmed allocation behavior.

Register actual culling/lighting entry points, profiles and ABI with
`EngineShaderCatalog`/ShaderBake. Update existing culling, forward and deferred
owners together. Verify recorded dispatches and decoded outputs, not catalog
presence alone. Shared helpers own physical conversion/attenuation; each family
retains material acquisition and stage routing. Debug views describe actual
lists/status and preserve exposure history.

Lighting-data capability governs preparation and forward consumers; deferred
shading additionally governs Stage 12 and shadow capability governs shadow work.
Review existing feature combinations and repair their gating. Intentional absence
of lighting preserves prior contributions such as emissive. Missing required
publication in an enabled path is failure, not that null-safe case. No lights
means valid empty products and zero direct-light draws.

## 7. Implementation and validation gates

The six EX07 steps are contracts, references/instrumentation, correctness repair,
qualified baseline/budgets, scalable optimization and final validation. Before
consumer changes, freeze the property inventory, canonical ABI and execution/
capacity contracts with their tests. CPU/HLSL/SDK changes land together; obsolete
single-light/positional routes are removed rather than maintained in parallel.

Qualify independent physical references and packed materials; GPU ABI; complete
lists versus receiver and unculled-image references; both atmosphere assignments;
supported property mutations; shadow/capacity/failure/recovery; per-view isolation;
editor-authored input; and queued/discarded resource lifetime. Use production paths
and owning tests, never a historical renderer as the acceptance reference. Inspect
native output and required GPU products, with relevant shader catalog, numerical,
integration and debug-layer checks.

Only correctness-qualified workloads become timing baselines. Freeze numeric
CPU/GPU/memory budgets, minimum useful improvement, regression allowances and
noise treatment before optimization candidates. Measure native Release separately
from captures/debug instrumentation. The plan owns counts/durations and final
matrix; timing sources live in `Benchmarks` with a separate executable. Both
final correctness and performance evidence are required for closure.
