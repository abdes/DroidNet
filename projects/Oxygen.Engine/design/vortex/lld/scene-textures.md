# SceneTextures Low-Level Design

**Phase:** 2 — SceneTextures and SceneRenderer Shell
**Deliverable:** D.1
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

## Exposure HDR domain and format inventory

The exposure delivery uses qualified RGBA16F and bootstrap/recovery RGBA32F
radiance products, with RGBA32F SceneColor accumulation in both modes, through
existing texture descriptors, lease keys and PSO format keys. This section is
the source-to-consumer migration checklist, audited at `7c44dffa8`.
The slice-5 source refresh distinguishes active SceneRenderer-owned radiance
from caller-owned display outputs; the unused internal composition allocator
is not the runtime HDR owner in this checkout.
Paths below are relative to `src/Oxygen`; shader paths begin under
`Graphics/Direct3D12/Shaders/Vortex/`. Entries describe required migration,
not completed implementation.

| Product ID / product | Producer and current storage | Consumers / required domain | Format action |
| --- | --- | --- | --- |
| 1 SceneColor: base emissive | `Materials/GBufferMaterialOutput.hlsli`, `Vortex/SceneRenderer/SceneTextures.cpp`, RGBA16F at audit baseline | Deferred/forward lighting, fog, resolve, meter, tonemap; write P times scene RGB, preserve coverage | RGBA32F accumulation in both modes |
| 2 SceneColor: deferred direct | `Services/Lighting/DeferredLight*.hlsl` and `DeferredLightingCommon.hlsli` | Add P-scaled BRDF radiance to product 1; no exposure in photometric light packets | Same SceneColor lease and blend-compatible PSOs |
| 3 SceneColor: indirect | `Services/Lighting/DeferredShadingCommon.hlsli`, `DeferredLightDirectional.hlsl`, forward environment SH/IBL consumers | Canonical scene-referred IBL converted to P at destination | Same SceneColor; SH remains float buffers |
| 4 Forward lit/unlit/opaque/masked/translucent | `Stages/Translucency/ForwardMesh_PS.hlsl`, `ForwardDirectLighting.hlsli`, material evaluation | One P on scene radiance; remove old GetExposure division/sRGB branch on HDR path; alpha/coverage unscaled | Same SceneColor and forward PSO formats |
| 5 Sky view LUT | `Vortex/Environment/Passes/AtmosphereSkyViewLutPass.cpp`, RGBA16F; `Services/Environment/AtmosphereSkyViewLut.hlsl` | `Sky.hlsl`; per-view P plus generation, remove exposure cancellation | Dual texture, SRV/UAV descriptors and allocation keys |
| 6 Camera aerial perspective | `AtmosphereCameraAerialPerspectivePass.cpp`, RGBA16F 3D; matching shader | `AerialPerspective.hlsli` / scene shading; RGB pre-exposed, transmittance unchanged | Dual 3D texture and binding formats |
| 7 Sky/background radiance | `Services/Environment/Sky.hlsl`, sky sphere/cubemap sampling | SceneColor, including sun disks; atmosphere/source radiance converted exactly once to destination P | Same SceneColor; display background excluded |
| 8 Height fog | `Services/Environment/Fog.hlsl` | SceneColor; scale added inscattering by P, retain attenuation | Same SceneColor |
| 9 Local fog compose | `Services/Environment/LocalFogVolumeCompose.hlsl` | SceneColor; same radiance/attenuation separation | Same SceneColor |
| 10 Volumetric fog/history | `Vortex/Environment/Passes/VolumetricFogPass.cpp`, RGBA16F 3D; `VolumetricFog.hlsl` | Fog compose and temporal reprojection; RGB carries stored P, alpha is transmittance | Dual current/history 3D textures; convert prior RGB by P_current/P_stored before interpolation |
| 11 Resolved HDR / composition handoff | `Vortex/SceneRenderer/ResolveSceneColor.cpp`, existing resolved-color artifact; Stage 22 writes caller-owned composite targets | Stage 22 reads resolved HDR; auxiliary/offscreen final-color handoff is display-mapped when post processing runs | Checked conversion from FP32 accumulation to qualified RGBA16F or recovery RGBA32F; bounded post-tonemap targets retain their format |
| 12 Bloom products | `Vortex/PostProcess/Internal/BloomChain.cpp` currently only forwards an externally supplied SRV; `BloomDownsample.hlsl` / `BloomUpsample.hlsl` exist | Tonemap; threshold scene-referred, RGB in source P, final S/P once | Any allocated radiance chain must inherit source HDR mode; no existing owned chain allocation found in this audit |
| 13 Static processed sky cubemap | `StaticSkyLightProcessor.cpp`, `IblProcessor.cpp`, normalized RGBA16F plus source_radiance_scale | Sky/IBL consumers restore resource scale; independent of view P/S | Preserve existing normalization; qualify narrowing and select RGBA32F if required source signal cannot fit; upload packing and descriptor must agree |
| 14 Canonical atmosphere transmittance | `AtmosphereLutCache.cpp`, RGBA32F | All atmosphere integrators; dimensionless | Shared FP32 storage approved for EX05-16; never P-scaled |
| 15 Canonical multiple scattering | `AtmosphereLutCache.cpp`, RGBA32F; `AtmosphereMultiScatteringLut.hlsl` | Sky-view/AP integrators; unit-illuminance transfer, not exposed radiance | Shared FP32 storage approved for EX05-16; preserve tiny transfers before multiplying physical illuminance |
| 16 Distant sky / diffuse SH | `AtmosphereLutCache.cpp` float4 buffer; static sky float SH buffer | Environment/lighting; scene-referred with explicit resource normalization where present | FP32 buffers retained, no view-dependent scaling |
| 17 Diagnostic colors | `BasePassWireframe.hlsl`, `ForwardWireframe_PS.hlsl`, `ForwardDebug_PS.hlsl`, base/debug visualization | Temporary unit-gain output; preserve persistent exposure and pending events | Remove inverse-old-exposure workaround; HDR diagnostic writes use current P, display overlays bypass scene metering |

Read every wildcard family entry against the ShaderBake catalog when migrating;
cataloged shaders and their CPU dispatches must agree. Static sky processing
already normalizes by its source maximum (`source_radiance_scale`); do not
mistake that canonical resource scale for a view exposure or remove it blindly.
Dynamic captured-scene/specular sky and TAA/TSR have no active producer in this
audited checkout; their ED-M08/future contracts must carry domain metadata when
activated, but this inventory does not claim their implementation or validation.

Material texture samples use the cooked resource's typed SRV encoding. The
texture binder preserves that format: sRGB views decode in hardware, while
UNORM and float views yield linear samples. Shared forward/deferred material
evaluation must not apply another sRGB conversion or clamp emissive samples to
one. The existing material asset and 112-byte GPU material layouts suffice.

SceneColor remains RGBA32F for accumulation in both modes. The exact dual-format
allocation set is composition HDR/optional
resolved HDR, sky-view LUT, camera AP volume, volumetric-fog current/history,
any active bloom radiance allocation, and the canonical processed cubemap when
its own normalization fails qualification. Other entries write into these
allocations or remain dimensionless/FP32. Do not promote depth, normals,
GBuffers, velocity, shadow maps or display targets. Canonical atmosphere transfers
use the separate shared FP32 contract below.

### Producer-range qualification

The EX05-16 matrix exercises the active producers in the inventory, with evidence
linked from `design/vortex/IMPLEMENTATION_STATUS.md`. Native expected values use
double arithmetic, with 2e-5 relative plus 2^-120 absolute RGB tolerance. Direct
deferred lighting additionally propagates the R8 specular half-code uncertainty
through the analytic BRDF; this is separate from the FP16 image-error budget.
Material emission is checked after alpha rejection and before other lighting can
cancel an unsupported value. Deferred emission reports product 1; forward
emission reports product 4. Each deferred direct-light and indirect contribution
reports product 2 or 3 before blending; forward checks individual light and IBL
terms before summation. Zero-coverage translucent fragments produce no source
status. Base-pass, deferred-light and translucency owners transition the existing
status UAV. Final accumulated-image scans remain necessary for cumulative range
failures; an invalid source retains the last valid Auto exposure state even if
the final accumulated pixel is positive.

Opaque producer checks require completed depth. The completed-prepass shader
permutation forces early depth while retaining alpha rejection. Without a complete
prepass, the first color/depth draw omits source-status writes; BasePass replays
its existing draw commands against finished, read-only depth with every color
write disabled. This adds no texture or exposure mode. It costs an additional
raster traversal only on that fallback path and preserves no-prepass masked holes
and draw-order-independent visibility. Matching framebuffer/PSO keys include
depth-read-only state and the depth-complete shader permutation.
Stage 10 publishes depth written by the base pass even when Stage 3 was disabled;
deferred lighting must not lose its depth input in that layout.
Dedicated validation entries have no color outputs. The deferred entry evaluates
only alpha/material emission, excluding GBuffer packing and velocity. The forward
entry validates emission and its individual light/IBL sources, excluding final
color composition, pre-exposure conversion and aerial perspective. Its light
evaluation is required to detect cancellation between light contributions; an
emission-only replay cannot qualify that boundary.

| Products | Range proof |
| --- | --- |
| 1, 4 | Cooked typed material/emissive samples through deferred/forward, opaque/masked/translucent paths; exact sRGB decode ownership |
| 2, 4 | Directional/point/spot lit-surface matrix at P endpoints; independent current-BRDF calculation, including partial coverage and explicit unsupported output |
| 3, 13, 16 | Static diffuse SH from isotropic cubemaps, including simultaneous 2^-24 and 2^30 components; normalized half/float cubemap upload; distant-sky linearity and dual-source additivity |
| 5–10, 14–15 | Actual atmosphere/fog producers, tiny canonical transfers, analytic thin scattering for both lights, local composition/injection and history-domain checks |
| 11, 17 | Whole-image input/final range scans, checked conversion, diagnostic domain and consumer-composition certificates |
| 12 | External matching-P handoff and disabled behavior are Debug-qualified by EX05-09, including checked scene fallback. Owned filter entries remain inactive placeholders; their thresholds/format/error work is tracked by issue 12. |

Distant-sky range checks use unit-light native anchors and the linear transfer
equation; they do not establish absolute atmosphere-model accuracy. Direct-light
tests qualify the current equations; physical unit corrections remain Slice 7.
Specular IBL publishes no prefiltered/BRDF products, and dynamic sky capture and
TAA/TSR remain inactive. Source TODOs at `IblProcessor`, `BloomChain` and
`SceneRenderer::PrepareExposureDomain` identify their required exposure work.

### Per-view FP16 suitability

Admission is configured before the frame exposure binding and radiance writes.
The environment owner describes the current required dimensions using the same
cache parameters and fog-grid resolver as allocation. Scene extent, required
product identity, coverage/transmittance semantics and AP consumer gain form the
layout revision. The late actual-product comparison remains a failure backstop.
Source-handle/lifetime/settings/transition changes invalidate a borrower's prior
precision certificate without changing numerical exposure authority.

The view HDR format controls environment intermediates and resolved color;
SceneColor-family lease keys remain FP32 when post processing is active. A half
resolve carries its conversion report and independently leased FP32 fallback.
Unconditional texture-only consumers receive the retained FP32 source. Artifact
ownership keeps the texture and registered views together and schedules retirement
through the existing reclaimer only after the last retained extraction is released.
Color allocation reuse follows `RetainedTexturePool` reader/fence retirement;
attachment-family reuse separately waits for its lease and submitted frame.
Retained fallback readers do not own the attachment family.

Resolved/extracted textures and their registered shader-visible views retire
together through the existing GPU-frame reclaimer. Switching views, resizing,
or replacing an artifact retains its registry entries until that frame slot is
safe to reclaim. Earlier views' submitted shader indices remain stable through
later view preparation and rendering.
Ground-grid and wireframe draw constants use the existing frame-retained
per-view structured publisher. Each submitted payload is immutable through its
frame slot's retirement, including repeated views and families larger than a
fixed ring. Their HLSL consumers use the matching structured-buffer descriptors.

SceneColor accumulation remains RGBA32F in both resource modes. This contract
was approved on 2026-09-17. Fixed-function output-merger blending does
not expose its cumulative pre-store value to the pixel shader. The
implementation keeps the existing SceneColor allocation RGBA32F for accumulation
and performs checked conversion into the existing resolved-color allocation:
RGBA16F when qualified, RGBA32F during recovery. This adds no telemetry target,
but retains one FP32 allocation in normal mode: eight extra bytes per SceneColor
texel versus the original all-FP16 normal-mode inventory (15.82 MiB at 1080p,
63.28 MiB at 4K). Other qualified view-radiance products can still use FP16.

Use the conventional raster blend path. Ordered-UAV blending is not part of
this contract. Checking an individual FP16 blend store would not independently
bound accumulated rounding error or recover information lost by earlier stores.
Normal-mode admission and checked conversion use the completed per-view
certificate; FP32 accumulation alone does not authorize a half-format resolve.

Exposure validity does not imply FP16 eligibility. At audited radiance write
boundaries, test finite FP32 values before narrowing, including cumulative
additive lighting and blending. Detect accumulation overflow as well as
individual-fragment overflow; an already clipped texture cannot prove pre-store
safety. FP32 reference frames evaluate candidate-P narrowing with the same
coverage/content/sample conventions as metering.

Required metering signals are samples with nonzero quantized mask/profile/
coverage weight which affect the ordinary or synthetic-dark solve. Candidate
narrowing must preserve finite/dark/zero classification when it affects that
solve, and positive retained luminance within 1/512 EV per sample. Samples
excluded from the solve need no exact RGB preservation. Still count rejected
nonfinite signals separately. For image preservation, compare recovered scene
RGB and S-scaled foreground contributions against the PBR error budgets;
below-budget values alone do not pin FP32. Quantization of individual terms
must be budgeted across the complete composition, not allowed the entire final
image budget at every additive pass. The final image/probe is the acceptance
authority.

Eligibility requires every required product to pass, with half its error budget
and max absolute stored RGB <=16376 (65504/4, two stops of overflow margin),
for two consecutive successfully submitted and completed frames. Track the
streak on GPU, reset it on failure/settings/event/layout changes, and report it
through the existing bounded completed status. Absence of a required product
or incomplete producer checks is ineligible, never a successful empty check.

Retain FP32 while ineligible and continue normal exposure/adaptation; do not
generate repeated remeter events. Pin the qualified GPU candidate P generation
for the first FP16 frame. Its lease remains alive until status consumption and
all readers finish. Ignore stale identities/revisions/layouts. Sharing uses the
owner's gain but evaluates each consumer's required products independently.
An unforeseen normal-frame range failure retains valid history and schedules
FP32 recovery after status completion. Explicit scene out-of-domain failure in
FP32 is reported; it is not an endless format/reinitialization loop.

The sky-view, camera AP and volumetric-fog producers now report finite/range
failures before their stores through the existing per-view completed status.
Their checks use the actual destination format. A finite source that clips to
65504 in a typed FP16 store remains distinguishable from a nonfinite source;
post-store finiteness alone cannot make that distinction. The solve invalidates
the affected meter and retains ordinary Auto history. Quantization/image-error
certificates remain separate from these source-range checks and govern admission.

### Benchmark format control

`DiagnosticsService::SetHdrFp32ReferenceEnabled` requests FP32 view-radiance
storage for a diagnostic comparison. Configure it before preparing the views.
It retains ordinary exposure, adaptation, transition generations and all range
and suitability checks. This remains the format-only diagnostic. The separate
`DiagnosticsService::SetHdrPrecisionControl(kFp32Only)` baseline is qualified in
EX051-04: it pins FP32/P1, omits admission certificates and publishes an
unconditional FP32 artifact while preserving normal exposure and range checks.

The view still selects its qualified GPU candidate. The frame resolver's
48-byte constants use control bit `0x4` to preserve that candidate P when FP32
storage is requested. An absent or invalid candidate, a unit-exposure diagnostic
or a missing shared source still uses P = 1. Ordinary production FP32 recovery
is unchanged. Frame flags report the actual FP32 storage mode; no GPU layout or
authored exposure mode is added. The CPU snapshot and GPU frame lease pin this
choice before producers execute.

Controlled fixtures verify qualified candidate P, exposure/history preservation,
invalid-candidate fallbacks and forward/deferred format switches. Workload
qualification and remaining reference coverage are recorded in
[EX051-04](../IMPLEMENTATION_STATUS.md#321-slice-51-performance-qualification-and-correction).

### Scene reference-product collection

SceneRenderer collects the actual allocations after HDR rendering: FP32
accumulated SceneColor (qualification ID 11 for its planned resolve), sky-view
LUT (5), camera aerial perspective (6), and volumetric fog (10). SceneColor's
raster/additive producers share one allocation and are not counted as separate
narrowing stages. Required intermediates follow the view's feature participation
and authored environment state; a missing publication or failed producer remains
a required missing product. Absent features add no requirement.

Environment publication pairs each view's descriptors with its submitted texture
leases. Sky-view, aerial-perspective and fog producers publish success only after
observing command-list submission. Failed recording/submission retires newly
registered textures and cannot replace valid fog history.

The reference evaluator divides its image allowance among the collected
allocations and tests the scene's actual coverage/mask policy. Layout identity
changes when required IDs, dimensions, coverage or transmittance treatment change;
format selection itself must not change that identity. Layout state is per
persistent view and is removed with its lifetime.

Prospective qualification may reuse the current aerial-perspective or fog
gradient certificate's scene-referred maximum. The submitted texture, SRV and
transmittance contract must match, and the GPU record must be complete, valid
and finite. These immutable producer records use the same outward-rounded
interval endpoints as the ordinary maximum scan. Invalid or incomplete records
execute that scan unchanged. Sky retains its scan because its gradient contract
does not validate unused alpha; SceneColor and candidate-bound scans are also
unchanged. Reuse removes texture reads and bound arithmetic, while retaining the
dispatch so the GPU can select the original scan without a CPU readback.

For prospective AP/fog products with transmittance, but without metering,
coverage or composed-image flags, candidate-bound gathering also performs the
point suitability check. The two operations share the texel, producer bounds
and RGB conversion calculations. Other products and current-scale checks keep
their separate path. All lanes still participate in candidate-bound reduction,
including lanes outside the product extent or rejected by a point check.

Early point-check flags and counters are commutative; first-failure selection is
not. During this fused dispatch, the suitability report's reserved word at byte
44 temporarily holds pending product bits. At each product's original check-loop
position, publication selects its failure only if no earlier failure exists,
then clears its bit. The word is zero again before the report is published.
Composed SceneColor checks remain after complete candidate-bound publication.
Report and producer status use their distinct UAVs during fusion, with the
existing memory barriers; the fused path does not alias either through an SRV.
No GPU layout, allocation or format policy changes.

The current implementation submits these reference checks and completed reports while
rendering in either HDR mode. Matching completed eligibility authorizes the
normal-mode allocations described above; pre-store and composition/temporal error
checks continue after admission. Stateless views retain FP32 without layout or
admission history.

EX051-04 provides the qualified FP32-only control without prospective half
qualification. EX051-09 will replace the current production attempt cadence with an explicit
operating policy; every FP16 use still requires its current-frame protection.

### Lifetime and memory accounting

Product metadata carries stored P and exposure-record generation through
SceneTextures publication, extraction and reuse. Rescale only RGB on a reused
pre-exposed history. Compatible resize/format changes recreate dependent
textures without erasing exposure. Old leases, descriptors and histories retire
at their last GPU fence; do not free them on a CPU acknowledgment alone.

RGBA32F adds eight bytes/texel over RGBA16F. One 1920x1080 allocation adds
15.8203125 MiB; 3840x2160 adds 63.28125 MiB. Sum the actual SceneColor,
resolve/composition allocations, each LUT/volume dimension, bloom mip texels,
simultaneously live views and retained lease/history generations. Do not multiply
by a guessed fixed frame count or count aliased SceneColor consumers as separate
textures. Bandwidth increases for each actual read/write of a promoted product;
record those passes and target-device timings in slice-5/10 reports.

The original Slice 5 EX05-21 fixture observes D3D12 texture placement requirements
at every texture creation and at named lifecycle checkpoints. Weak references and native
resource identity avoid extending lifetimes or double-counting aliases. It reports
raw texel bytes separately from device placement requirements, and pool allocation
counts separately from leased families. The pool retains reusable allocations
after view/consumer release; those bytes remain part of the cached footprint.
Caller output targets, buffers, descriptor heaps and unrelated backend allocations
are outside this texture report.

Slice 5.1 extends this same untimed fixture to include buffer and combined
placement peaks. Each successful tracked texture or buffer creation updates the
high-water mark over unique live native resources; weak observations include
idle, unregistered cache allocations without extending their lifetime. Record
the triggering lifecycle phase and descriptor population. These are placement
requirements within the declared trace, not committed heap usage, residency or
an arbitrary-concurrency worst case. Keep fixed fixture allocations, caller
outputs and diagnostic readbacks identifiable; status inspection must not create
allocations inside the engine trace.

The qualification schedule repeats three cut/resize/remove/re-add cycles with
two fixed view-size layouts and delayed consumers holding complete extraction
records. Public view removal and reuse must create the appropriate new lifetime.
Record actual P and formats in production and the existing format-only FP32
control, with identical scene, temporal setting, events and reader delays.
After readers and their retirement slots are released, already visited descriptor
populations must stabilize across cycles; intentionally cached SceneTextures
families remain counted. A pre-H5/current peak comparison requires this same
extended harness on both revisions. Historical texture-only reports below and
H5's steady snapshots cannot serve as that lifecycle-peak denominator.

The extension and R091 idle-frame lease release are committed as `8903305`.
Seven focused Debug cases and eight Release cases on each side pass. The matched
pre-H5/current comparison uses the same R091 fix and executed accounting body;
all five peak categories and retired populations match within each control.
See the [common-fix protocol](../../../out/build-ninja/analysis/vortex/exposure-lightbench/slice51/lifecycle-memory/protocol-shared-idle-fix.json)
and the [current Release memory table](../../../out/build-ninja/analysis/vortex/exposure-lightbench/slice51/lifecycle-memory/audit-current-Release-memory-table.json).

The following tables preserve the original Slice 5 texture-only checkpoint.

The Debug and Release qualification uses a full-resolution main view and a second view at
half width/height, an emissive surface, vacuum atmosphere and zero-extinction fog.
Both use the default LUT/volume dimensions. Temporal reuse is a separate control:
its retained uncertainty conservatively rejects the prospective half scene
certificate in this fixture, while the non-temporal control qualifies for FP16.
The controls therefore vary temporal work as well as format; their difference is
not an isolated format-only cost.

| Main / second view | Temporal fog / steady HDR mode | Two-view HDR MiB (Debug) | Two-view HDR MiB (Release) | Creation-time peak MiB | Cached after retirement MiB |
| --- | --- | ---: | ---: | ---: | ---: |
| 1920x1080 / 960x540 | Off / FP16 | 229.328 | 229.328 | 294.953 | 86.578 |
| 1920x1080 / 960x540 | On / FP32 retention | 374.453 | 381.703 | 384.016 | 86.578 |
| 3840x2160 / 1920x1080 | Off / FP16 | 827.328 | 827.328 | 1075.828 | 322.828 |
| 3840x2160 / 1920x1080 | On / FP32 retention | 1375.641 | 1401.203 | 1410.578 | 322.828 |

All four cases retain two outputs across settings invalidation requiring FP32,
then release them and cross the actual fence/slot-retirement boundaries. Four cached families
remain, with no leased family after retirement. The two canonical LUTs keep one
shared generation. For one SceneColor allocation, the measured FP32 footprint
is 33.750 MiB at 1080p and 127.500 MiB at 4K; querying the same descriptor in FP16
gives 16.875 and 63.750 MiB. These device placement deltas differ from raw texel
arithmetic and are specific to the captured adapter/descriptor flags.

Four inspected captures verify selected sources, pinned P/S and final consumption.
The traffic report counts primary qualification loads, checked resolve or copy,
tonemap, and full sky/AP/fog UAV stores from their actual shapes/formats. It keeps
filtered taps, side/buffer reads, blending and compression outside those byte
totals. Logical transfer quantities and warm replay event durations are reported
separately from physical DRAM bandwidth or whole-frame latency. Release repeats
all four allocation/retirement cases. Creation-time peaks and retired cached
footprints match Debug; intermediate retained temporal snapshots differ and
are listed separately above.

| Main resolution / temporal fog | Explicit qualification (ms) | Conversion or copy (ms) | Tonemap (ms) | Fog compute (ms) |
| --- | ---: | ---: | ---: | ---: |
| 1080p / Off | 3.273248 | 0.089936 | 0.105456 | 0.189168 |
| 1080p / On | 3.188288 | 0.091696 | 0.134368 | 2.657600 |
| 4K / Off | 7.728880 | 0.232096 | 0.375344 | 0.723744 |
| 4K / On | 9.343680 | 0.322688 | 0.504320 | 10.411648 |

These are sums of warm per-event replay medians across both views on the
captured adapter. Explicit qualification includes clear, maximum gathering,
candidate selection, product checks and finalization; checks embedded in other
rendering passes remain in those passes' costs. Normal cases convert to FP16;
temporal cases retain/copy FP32. Modeled primary texture reads range from
399,716,096 to 1,605,690,368 bytes per captured frame, and modeled writes from
75,008,000 to 419,553,280 bytes, with the exclusions above.
[Release allocation phases](../../out/build-ninja/analysis/vortex/exposure-lightbench/lifecycle/accounting-summary-Release.json)
and [traffic/timing results](../../out/build-ninja/analysis/vortex/exposure-lightbench/lifecycle/accounting-performance-Release.json).
[Native reports, capture analysis, exact commands and scope](../../out/build-ninja/analysis/vortex/exposure-lightbench/lifecycle/accounting-manifest.json).

### Slice 5.1 workload memory inventory

EX051-03 inventories existing descriptors and evidence; it introduces no new
allocation measurement. All eight recipes use the same scene/post-process
owners. C01/C02 have two views; M01/M04/I01 have one; M02/M03/I02 have two.
Temporal histories participate in C02/M02/M04/I01/I02. I01/I02 additionally
enable actual shadow rendering. Actual retained generations and placement,
rather than a fixed multiplier per view, determine the total.

| Population / owner | Descriptor-based expectation | Existing observation / retirement rule |
| --- | --- | --- |
| Live scene attachments / `SceneTextures` | FP32 accumulation: 16 bytes/texel; depth, partial depth, four GBuffers and enabled velocity/custom depth retain the formats in section 5.1. Main/secondary dimensions follow the recipe. | Deduplicate native identities; count placement separately from raw texels. Family cache and leased counts are distinct. |
| Resolved color and queued fallback / `ResolveSceneColor` | 8 bytes/texel for admitted half resolve, 16 for FP32 resolve; a conditional extraction additionally retains original FP32 color and its immutable P/state/report. | EX051-10A removes the measured whole-family fallback retention below. Queued readers and descriptor retirement remain mandatory and are qualified by its checkpoint. |
| Depth extracts / `ExtractSceneDepth` | Resolved and previous depth can alias one artifact; count one native allocation, not two logical outputs. | H4/H5 and the lifecycle fixture verify both delayed aliases after source-family reuse. |
| Environment current products and history / environment passes | Sky-view dimensions use the existing quality descriptor; AP is 64 x 64 x 32 for steady recipes. Fog dimensions come from the existing viewport/grid resolver. Qualified radiance uses 8 bytes/texel, FP32 uses 16. | Temporal recipes retain actual previous fog products, stored P and certificates until their readers/fences finish. Reprojection on does not imply half admission. |
| Shared canonical atmosphere cache | One shared 256 x 64 and one 32 x 32 RGBA32F table: 136 KiB raw per generation. | Count each unique cache generation once across views; include placement alignment and overlapping retired generations. |
| Exposure states, reports and reduction scratch / `ExposurePass` | Histogram allocation is `kHistogramWordCount * sizeof(uint32_t)` when metering needs it; frame/state/status/conversion records and structured constant publishers use their declared buffer descriptors. No full-resolution telemetry texture. | Count actual leased generations and cached buffers, including idle allocations. Status readback reuse is bounded by frame slots; a retained consumer can extend its frame/state leases. |
| Caller outputs and fixture transport | One full and optional half-size FP32 output; lifecycle fixture also has two 1 x 1 delayed outputs and depth readbacks. | Keep named fixture/transport allocations separate from engine placement; diagnostic inspection is excluded from the creation trace. |
| Other rendering products | Mixed scene assets, HZB and I01/I02 shadow surfaces use their own descriptors and native identities. | Included when observed after trace start; do not attribute ordinary rendering resources to exposure or call a partial trace total device residency. |

`ExposureBaselineScenario::Snapshot` supplies untimed before/after native texture
and buffer placement and creation counts for steady recipes. Zero warm creation
churn is the expectation; snapshots are not lifecycle high-water marks.
`ExposureAllocationScenario` supplies creation-time texture/buffer/combined/
engine/HDR peaks over three cut/resize/remove/re-add cycles, with delayed color
and depth readers. The existing 4K temporal-off entry point required by 10A is
`ExposureLightingGpuTest.DISABLED_ProductionHdrAllocationAccounting`, with
`OXYGEN_EXPOSURE_TIMING_WIDTH=3840` and precision `production`.

The [current Release table](../../../out/build-ninja/analysis/vortex/exposure-lightbench/slice51/lifecycle-memory/audit-current-Release-memory-table.json)
indexes eight raw cases by source hash. The
[matched 4K production audit](../../../out/build-ninja/analysis/vortex/exposure-lightbench/slice51/lifecycle-memory/audit-matched-3840-production-Release.json)
and the other matched audits linked in EX051-10 close the common-fix comparison;
the earlier table's pending-comparison prose is superseded. Their trace is
queue-drained except for explicit delayed consumers; it is neither a worst-case
concurrency bound nor heap commitment/residency. Fixed-descriptor populations
must stabilize after reader/fence retirement. The measured unrelated-attachment
excess below is a correction target, not an acceptable permanent cache budget.
Final timings/memory acceptance belong to EX051-13, and post-10A placement must
be measured before claiming its reduction.

### Allocation contract

SceneTexturesConfig and SceneTextureLeaseKey carry the actual SceneColor format;
only RGBA16F and RGBA32F are accepted. The pool separates and reuses those
physical families without changing depth, GBuffers or velocity formats. The
scene path keeps accumulation RGBA32F. Its per-view radiance mode independently
selects the resolved-color, sky-view LUT, camera AP volume and volumetric-fog
formats; it must not infer eligibility from the accumulation texture's format.
Canonical transmittance and unit-illuminance multiple scattering use FP32 in both
modes, as approved on 2026-09-18. Their exposure-independent transfer values can
be far below the FP16 minimum yet yield required radiance after illumination.
The same two resources remain shared across views: default 256x64 and 32x32
RGBA32F tables add 136 KiB of raw texels per retained cache generation. Allocation
alignment and overlapping generations must be counted separately. This does not
change the per-view FP16/FP32 radiance selection contract.
Compatible fog history remains readable across a format change through its own
correctly typed descriptor. Runtime admission and stored-P conversion remain
separate requirements; dual-format allocation alone does not prove eligibility.

## 1. Scope and Context

### 1.1 What This System Is

`SceneTextures` is the canonical scene-product family for Vortex. It owns and
manages the shared GPU texture resources that flow across the frame: GBuffers,
scene depth, scene color, velocity, stencil, and custom depth. It is the
deferred renderer's central data product.

### 1.2 Why It Is Needed

The legacy Forward+ renderer has no unified scene-texture product family.
Deferred rendering requires a single authoritative owner of the GBuffer
attachments and related screen-space products that multiple stages and services
read and write during a frame.

### 1.3 What It Replaces

No direct legacy equivalent. The closest analog is the scattered per-pass
framebuffer setup in the legacy `ForwardPipeline`. Vortex consolidates all
shared scene attachments under one product family.

### 1.4 Architectural Authority

- [ARCHITECTURE.md §7.3](../ARCHITECTURE.md) — four-part scene-texture
  contract (authoritative)
- [ARCHITECTURE.md §7.3.2](../ARCHITECTURE.md) — canonical product family
- [ARCHITECTURE.md §7.3.3](../ARCHITECTURE.md) — setup mode rules
- [ARCHITECTURE.md §7.3.4](../ARCHITECTURE.md) — binding package rules

## 2. Four-Part Contract

Per ARCHITECTURE.md §7.3.1, `SceneTextures` has four separate architectural
concerns. This LLD designs each part.

| Contract Part | Class | Owner |
| ------------- | ----- | ----- |
| Concrete product family | `SceneTextures` | `SceneRenderer` |
| Setup state | `SceneTextureSetupMode` | `SceneRenderer` |
| Shader-facing binding package | `SceneTextureBindings` | Generated from SceneTextures + setup mode |
| Extracted handoff set | `SceneTextureExtracts` | `SceneRenderer` post-render cleanup |

## 3. Interface Contracts

### 3.1 SceneTexturesConfig

Configuration for initial allocation. Immutable after construction.

```cpp
namespace oxygen::vortex {

struct SceneTexturesConfig {
  glm::uvec2 extent{0, 0};                  // Viewport dimensions
  bool enable_velocity{true};                // SceneVelocity allocation
  bool enable_custom_depth{false};           // Separate custom depth/stencil path
  std::uint32_t gbuffer_count{4};            // A-D active; E-F reserved in ABI for Phase 7E
  std::uint32_t msaa_sample_count{1};        // 1 = no MSAA
};

} // namespace oxygen::vortex
```

**File:** `SceneRenderer/SceneTextures.h`

### 3.2 GBufferIndex

Typed index vocabulary for GBuffer access.

```cpp
enum class GBufferIndex : std::uint8_t {
  kNormal = 0,        // World normal (encoded)         → R10G10B10A2_UNORM
  kMaterial = 1,      // Metallic, specular, roughness  → R8G8B8A8_UNORM
  kBaseColor = 2,     // Base color, AO                 → R8G8B8A8_SRGB
  kCustomData = 3,    // Custom data / shading model    → R8G8B8A8_UNORM
  kShadowFactors = 4, // Shadow factors (reserved)
  kWorldTangent = 5,  // World tangent (reserved)

  kCount = 6,
  kActiveCount = 4,  // Phase-1: A-D only
};
```

**File:** `SceneRenderer/SceneTextures.h`

### 3.3 SceneTextures

Concrete product family. Owns GPU texture resources.

```cpp
class SceneTextures {
public:
  OXGN_VRTX_API explicit SceneTextures(Graphics& gfx, const SceneTexturesConfig& config);
  OXGN_VRTX_API ~SceneTextures();

  // Non-copyable, non-movable (owns GPU resources)
  SceneTextures(const SceneTextures&) = delete;
  auto operator=(const SceneTextures&) -> SceneTextures& = delete;
  SceneTextures(SceneTextures&&) = delete;
  auto operator=(SceneTextures&&) -> SceneTextures& = delete;

  // --- Core products (always valid after construction) ---

  OXGN_VRTX_NDAPI auto GetSceneColor() const
    -> graphics::Texture&;
  OXGN_VRTX_NDAPI auto GetSceneDepth() const
    -> graphics::Texture&;
  OXGN_VRTX_NDAPI auto GetPartialDepth() const
    -> graphics::Texture&;
  OXGN_VRTX_NDAPI auto GetSceneColorResource() const
    -> const std::shared_ptr<graphics::Texture>&;
  OXGN_VRTX_NDAPI auto GetSceneDepthResource() const
    -> const std::shared_ptr<graphics::Texture>&;
  OXGN_VRTX_NDAPI auto GetStencil() const -> SceneTextureAspectView;

  // --- GBuffer products (allocated at construction; consumable publication
  //     begins only after SceneRenderer completes Stage 10) ---

  OXGN_VRTX_NDAPI auto GetGBuffer(GBufferIndex index) const
    -> graphics::Texture&;
  OXGN_VRTX_NDAPI auto GetGBufferResource(GBufferIndex index) const
    -> const std::shared_ptr<graphics::Texture>&;
  OXGN_VRTX_NDAPI auto GetGBufferNormal() const -> graphics::Texture&;
  OXGN_VRTX_NDAPI auto GetGBufferMaterial() const -> graphics::Texture&;
  OXGN_VRTX_NDAPI auto GetGBufferBaseColor() const -> graphics::Texture&;
  OXGN_VRTX_NDAPI auto GetGBufferCustomData() const -> graphics::Texture&;
  OXGN_VRTX_NDAPI auto GetGBufferCount() const noexcept
    -> std::uint32_t;

  // --- Optional products (null when not enabled) ---

  OXGN_VRTX_NDAPI auto GetVelocity() const -> graphics::Texture*;
  OXGN_VRTX_NDAPI auto GetVelocityResource() const
    -> const std::shared_ptr<graphics::Texture>&;
  OXGN_VRTX_NDAPI auto GetCustomDepth() const -> graphics::Texture*;
  OXGN_VRTX_NDAPI auto GetCustomStencil() const -> SceneTextureAspectView;

  // --- Lifecycle ---

  OXGN_VRTX_API void Resize(glm::uvec2 new_extent);
  OXGN_VRTX_API void RebuildWithGBuffers();

  // --- Query ---

  OXGN_VRTX_NDAPI auto GetExtent() const noexcept -> glm::uvec2;
  OXGN_VRTX_NDAPI auto GetConfig() const noexcept
    -> const SceneTexturesConfig&;

private:
  struct RegisteredTexture {
    std::shared_ptr<graphics::Texture> resource;
  };

  Graphics& gfx_;
  SceneTexturesConfig config_;

  // Core products — allocated at construction
  RegisteredTexture scene_color_;
  RegisteredTexture scene_depth_;
  RegisteredTexture partial_depth_;

  // GBuffer products — allocated at construction, valid after rebuild
  std::array<RegisteredTexture,
    static_cast<size_t>(GBufferIndex::kCount)> gbuffers_;

  // Optional products — null when not enabled
  RegisteredTexture velocity_;
  RegisteredTexture custom_depth_;
};
```

**File:** `SceneRenderer/SceneTextures.h` + `SceneRenderer/SceneTextures.cpp`

Getter semantics:

- `GetSceneColorResource()`, `GetSceneDepthResource()`, and
  `GetGBufferResource(...)` expose the shared-resource handle used by
  publication and extraction code
- `GetStencil()` exposes the scene stencil family through a
  `SceneTextureAspectView` into the scene depth/stencil resource owned by
  `SceneTextures`
- `GetCustomStencil()` exposes the custom stencil family only when the optional
  custom depth/stencil path is enabled, also through `SceneTextureAspectView`
- there is no raw stencil/custom-stencil texture getter in the Vortex contract;
  stage/services consume stencil families through the aspect-view accessors and
  through published `SceneTextureBindings`
- `RebuildWithGBuffers()` is only the family-local readiness helper; the
  consumable publication boundary is
  `SceneRenderer::PublishDeferredBasePassSceneTextures(ctx)`

### 3.4 SceneTextureSetupMode

Tracks which products are set up and bindable at a given runtime point.

```cpp
class SceneTextureSetupMode {
public:
  enum class Flag : std::uint32_t {
    kNone            = 0,
    kSceneDepth      = 1 << 0,
    kPartialDepth    = 1 << 1,
    kSceneVelocity   = 1 << 2,   // partial or complete
    kGBuffers        = 1 << 3,   // A-D valid for reads
    kSceneColor      = 1 << 4,   // written by base pass
    kStencil         = 1 << 5,
    kCustomDepth     = 1 << 6,
  };

  void Set(Flag flag);
  void Clear(Flag flag);
  void Reset();  // back to kNone at frame start

  [[nodiscard]] auto IsSet(Flag flag) const -> bool;
  [[nodiscard]] auto GetFlags() const -> std::uint32_t;

private:
  std::uint32_t flags_{0};
};
```

**Ownership:** `SceneRenderer` owns the single instance and updates it at
stage boundaries. Passes consume the mode; they do not update it.

**Setup milestones (ARCHITECTURE.md §7.3.3):**

| After Stage | Flags Set |
| ----------- | --------- |
| 3 (depth prepass) | `kSceneDepth`, `kPartialDepth`, optionally `kSceneVelocity` (partial) |
| 9 (base pass) | raw `GBuffer*` / `SceneColor` attachments may be written, but they remain unpublished for standard scene-texture consumers |
| 10 (rebuild) | `SceneColor` + GBuffer-valid published state for deferred consumers |
| 22 (post-process) | All production products valid |
| 23 (cleanup) | Extraction set queued |

**File:** `SceneRenderer/SceneTextures.h` (same header, separate class)

### 3.5 SceneTextureBindings

Bindless routing metadata, derived from SceneTextures + setup mode. This is
NOT a UBO or parameter block — it is metadata that tells shaders how to reach
scene products through bindless handles.

```cpp
struct SceneTextureBindings {
  // Bindless SRV indices for scene products
  std::uint32_t scene_color_srv{kInvalidIndex};
  std::uint32_t scene_depth_srv{kInvalidIndex};
  std::uint32_t partial_depth_srv{kInvalidIndex};
  std::uint32_t velocity_srv{kInvalidIndex};
  std::uint32_t stencil_srv{kInvalidIndex};
  std::uint32_t custom_depth_srv{kInvalidIndex};
  std::uint32_t custom_stencil_srv{kInvalidIndex};

  // GBuffer SRV indices: A-D active in Phase 3, E/F reserved as stable
  // invalid slots until Phase 7E.
  std::array<std::uint32_t,
    static_cast<size_t>(GBufferIndex::kCount)> gbuffer_srvs{};

  // UAV indices for write access (stage-specific)
  std::uint32_t scene_color_uav{kInvalidIndex};
  std::uint32_t velocity_uav{kInvalidIndex};

  // Validity tracking (mirrors setup mode for shader safety)
  std::uint32_t valid_flags{0};

  static constexpr std::uint32_t kInvalidIndex =
    std::numeric_limits<std::uint32_t>::max();
};
```

**Ownership:** `SceneRenderer` owns the current `SceneTextureBindings`
instance as the canonical routing metadata for the current frame/view setup
state.

**Generation:** `SceneRenderer` regenerates `SceneTextureBindings` whenever
`SceneTextureSetupMode` changes. It uses the graphics layer's descriptor
allocation to create SRV views for set-up textures and writes
`kInvalidIndex` for products not yet set up. `stencil_srv` routes the scene
stencil family from the scene depth/stencil resource. `custom_stencil_srv`
routes the custom stencil family only when the optional custom depth/stencil
path is enabled. The binding package now reserves the full logical GBuffer
family at the ABI edge:

- slots `0-3` = Phase 3 active `GBufferA-D`
- slot `4` = reserved `GBufferShadowFactors` / `GBufferE` for Phase `7E`
- slot `5` = reserved `GBufferWorldTangent` / `GBufferF` for Phase `7E`

The reserved slots must remain present but invalid until those later phases
publish real products through them.

**Publication:** `SceneTextureBindings` is published into `ViewFrameBindings`
through Renderer Core publication helpers so that passes can access scene
products through the standard per-view binding stack. Publication is an
explicit renderer-side step; passes never synthesize or own shared
scene-texture routing metadata. The semantic publication seams are:

- `PublishDepthPrepassProducts()` for stage-3 depth/partial-depth publication
- `PublishBasePassVelocity()` for stage-9 velocity-only publication
- `PublishDeferredBasePassSceneTextures(ctx)` for the full Stage-10
  `SceneColor` / GBuffer / stencil publication boundary
- `PublishCustomDepthProducts()` for the later custom-depth/custom-stencil
  publication milestone

**File:** `SceneRenderer/SceneTextures.h`

### 3.6 SceneTextureExtracts

Handoff artifacts produced during post-render cleanup.

```cpp
struct SceneTextureExtractRef {
  std::shared_ptr<const graphics::Texture> retained_texture;
  graphics::Texture* texture{nullptr};  // extracted/handoff artifact
  bool valid{false};
  std::shared_ptr<const postprocess::FrameExposureResources> exposure;
  graphics::Texture* fallback{nullptr}; // original FP32 accumulation, if conditional
  std::shared_ptr<const graphics::Texture> source_color;
};

struct SceneTextureExtracts {
  // Resolved outputs for external consumers
  SceneTextureExtractRef resolved_scene_color;
  SceneTextureExtractRef resolved_scene_depth;

  // History textures for next-frame reuse
  SceneTextureExtractRef prev_scene_depth;
  SceneTextureExtractRef prev_velocity;
};
```

**Current ownership:** `SceneRenderer` produces the resolved artifacts at stage 21 and
finalizes the handoff/history set during `PostRenderCleanup` (stage 23).
Queued consumers retain the complete extract record. It owns the artifact and
registered views, frame-pinned exposure and conversion report, and the independent
color lease protecting the original FP32 accumulation. View removal, subsequent
format changes and frame-slot reuse cannot replace those inputs. After submission,
the existing GPU-frame reclaimer protects resources until the consumer fence
retires, even when the final extraction owner has been released.

For EX051-13 GPU attribution, the unconditional FP32 color snapshot and depth
snapshot are ordinary renderer output handoffs: `ResolveSceneColor` creates them
even without a prepared exposure. They remain separately reported under
`Vortex.ResolveSceneColor`. Exposure-specific checked narrowing is instead timed
by `Vortex.PostProcess.Exposure.ConvertSceneColor`; it does not execute in the
approved FP32 production mode. Status readback copies are timed under
`Vortex.PostProcess.Exposure.StatusReadback` and included in the exposure union.
The final source/scope audit in the PostProcess owner establishes this boundary
without changing a budget or rerunning the completed GPU matrix.

`ResolvedSceneDepth` and `PrevSceneDepth` are two read-only handoffs of the same
immutable stage-21 snapshot. Stage 23 copies the complete extraction reference,
including its retained ownership, instead of allocating and copying another
depth texture. Neither handoff aliases the mutable live scene-depth attachment.
An invalid or null resolved snapshot produces an empty previous-depth handoff.
Either reader can outlive the other, another view/frame, resize or renderer
destruction; the shared wrapper unregisters the resource and its views only after
the final reader releases it and the existing GPU-frame reclaimer retires it.
Velocity retains its separate stage-23 snapshot.

Accumulated color, resolved color, shared depth and previous velocity each use a private
`RetainedTexturePool`; sky-view, aerial-perspective and volumetric-fog outputs
use the same ownership mechanism in their producing passes. Each pool holds at
most one idle allocation per persistent view and complete allocation descriptor.
Active and queued readers hold the wrapper, so these idle slots do not bound
the separate in-flight population. Stateless products bypass idle retention.
Descriptor changes, view removal, inactivity and owner destruction invalidate
eligibility; late releases cannot recreate an invalidated entry.

Only the final wrapper release schedules the existing GPU-frame retirement.
After retirement, unregister the underlying resource and views, and retain it
only when no external underlying owner remains and all queue records agree on a
known resource state. Reuse registers the resource again and adopts that saved
state; unknown or conflicting states require a fresh allocation. Reuse never
assumes the descriptor's initial state and introduces no CPU wait. The next
producer overwrites its output, except that a rejected checked FP16 conversion
may leave stale texels: its conversion report and retained FP32 fallback remain
mandatory for every conditional consumer.

### EX051-10A independent SceneColor fallback ownership

**Status: validated in the focused Debug owning checks.** Conditional
color now owns an independent `RetainedTexturePool` lease through `source_color`.
`SceneTextures::GetSceneColorResource()` supplies the underlying resource to
framebuffers; `GetSceneColorLease()` supplies immutable extraction ownership.
These are distinct ownership records for the same allocation. Deferred
framebuffer owners therefore do not initiate a second color-lease retirement;
the pool still refuses reuse while underlying resource owners remain.
`SceneTextureLease::Retire()` releases writable color ownership and independently
marks attachments unavailable until their frame's reclaimer callback. No CPU
wait is added. FP32 accumulation and GPU-selected checked-half fallback remain.

The [10A checkpoint](../../../out/build-ninja/analysis/vortex/exposure-lightbench/slice51/color-ownership/checkpoint-manifest.json)
records 56/56 passing checks and 644 unchanged frozen inputs. The 4K temporal-off
trace has engine/combined/HDR peaks of 3747.207/3987.770/1603.203 MiB. In all three
cycles, six warmed attachment families keep identical resource identities and
descriptors through retained-color resize; all six retire to zero leases.
The three-frame attachment population is independent of fallback readers.
Queued consumers preserve five HDR outputs and both depth aliases, including
release before submission/completion. Color/descriptor reuse is additionally
covered by the 16 existing retained-pool tests and the focused family-reuse test.
The earlier draft's extra color-retirement interval was diagnosed and corrected;
its raw evidence remains separate. These are Debug lifecycle observations, not
a matched-Release comparison with the historical table below. 05 owns the new
format-benefit measurements and 13 owns final acceptance.

**Source entry points:** trace `SceneRenderer::BuildSceneTextureLeaseKey` and
family acquisition in
[SceneRenderer.cpp](../../../src/Oxygen/Vortex/SceneRenderer/SceneRenderer.cpp),
reuse eligibility in
[SceneTextureLeasePool.cpp](../../../src/Oxygen/Vortex/SceneRenderer/SceneTextureLeasePool.cpp),
the extraction's `source_color` in
[ResolveSceneColor.cpp](../../../src/Oxygen/Vortex/SceneRenderer/ResolveSceneColor.cpp),
and `SceneTextureExtractRef` in
[SceneTextures.h](../../../src/Oxygen/Vortex/SceneRenderer/SceneTextures.h).
Carry the independent color lease through the existing extraction/consumer API;
keep attachment-family and color-allocation reuse decisions separate.

The pre-10A matched Release lifecycle measurements show the cost of whole-family
retention. Each case renders two views; the secondary is half the main extent.

| Main resolution / temporal fog | Production engine peak MiB | Format-only FP32 engine peak MiB | Excess production peak MiB |
| --- | ---: | ---: | ---: |
| 1080p / off | 978.457 | 733.770 | 244.688 |
| 1080p / on | 908.957 | 757.832 | 151.125 |
| 4K / off | 3585.957 | 2684.207 | 901.750 |
| 4K / on | 3335.457 | 2771.082 | 564.375 |

Temporal-off production leaves six cached families after retirement; the FP32
control leaves two. The extra families occupy 302.25 MiB at 1080p and
1,128.75 MiB at 4K. Of those totals, 216 MiB and 806.25 MiB are depth, partial
depth, GBuffer, velocity and custom-depth attachments unrelated to the fallback.
Temporal-on production briefly selects FP16 and leaves four families versus two;
the extra cached bytes are 151.125 MiB and 564.375 MiB respectively.

Required ownership and reuse behavior:

1. Acquire FP32 SceneColor through an independent allocation lease. The active
   view uses a writable color allocation that has no retained readers.
2. A conditional extraction retains that color lease, its registered views, the
   resolved artifact, exposure records and conversion report. It does not retain
   the attachment family merely to keep SceneColor alive.
3. Family reuse may recycle depth/GBuffer/velocity/custom-depth attachments after
   their own readers and GPU fences retire. Retained color readers do not block it.
4. SceneColor reuse checks its own lease ownership and completion. Replacing
   `source_lease` with an ordinary texture pointer alone is insufficient: family
   reuse must not overwrite color still owned by an extraction.
5. Preserve actual resource-state adoption, resize/format keys, delayed/offscreen
   readers and view-lifetime invalidation. View removal must retire only the
   owners that have finished; no CPU wait is added to make a resource reusable.

Use the existing queued color/depth consumer checks and one existing 4K
temporal-off lifecycle case. Verify old color output after family reuse, resource
and descriptor lifetime through submission/completion, and absence of extra
unrelated attachment generations caused solely by color fallback ownership.
Measure the post-change format benefit through EX051-05's four pairs.

Stage 22 tonemap consumes conditional resolved color by binding both sources and
the same conversion report: an accepted conversion selects the half artifact;
a rejected conversion selects the retained FP32 source. Texture-only access via
`GetResolvedSceneColorTexture()` returns an owning reference to the unconditional
FP32 source. These handoff contracts do not authorize caching a raw pointer to a
live `SceneTextures` attachment.

Auxiliary and offscreen composition publish the producer's post-process output.
It is already display-mapped with that producer's exposure. Composition copies
that output without applying the consuming view's exposure again. The consumer
may independently render its own scene in a different HDR format.

**File:** `SceneRenderer/SceneTextures.h`

## 4. Data Flow and Dependencies

### 4.1 Product Lifecycle Within a Frame

```text
Frame Start
  └─ SceneTextures exist (allocated at construction or after resize)
  └─ SceneTextureSetupMode::Reset() → kNone
  └─ SceneTextureBindings invalidated

Stage 3 (Depth Prepass)
  └─ Writes: SceneDepth, PartialDepth, partial SceneVelocity
  └─ SetupMode += kSceneDepth | kPartialDepth | kSceneVelocity (partial)
  └─ Bindings regenerated with depth SRVs

Stage 9 (Base Pass)
  └─ Writes: GBufferNormal/Material/BaseColor/CustomData (MRT), SceneColor (emissive), velocity completion
  └─ No `SceneTextureSetupMode` promotion for `SceneColor` / `kGBuffers` yet
  └─ Standard scene-texture bindings still treat SceneColor / GBuffers as unavailable here

Stage 10 (Rebuild)
  └─ SceneRenderer-owned `PublishDeferredBasePassSceneTextures(ctx)`
  └─ Calls `SceneTextures::RebuildWithGBuffers()` — family-local helper
  └─ SetupMode += kGBuffers | kSceneColor | kStencil
  └─ Bindings regenerated with full GBuffer SRVs
  └─ Shared routing metadata republished for the current view
  └─ SceneColor + GBuffers now readable by downstream stages through the canonical publication stack

Stage 12 (Deferred Lighting)
  └─ Reads: GBufferNormal/Material/BaseColor/CustomData, SceneDepth, shadow data, IBL
  └─ Writes: SceneColor (accumulated lighting)

Stage 21 (Resolve scene color)
  └─ Exposure solve reads FP32 SceneColor before color resolution; pins final S
  └─ Resolves: SceneColor -> resolved_scene_color artifact
  └─ Copies: SceneDepth -> resolved_scene_depth artifact
  └─ Resolved artifacts now become the explicit handoff source for composition/tools

Stage 22 (Post-Process)
  └─ Reads: SceneColor, SceneDepth, Velocity
  └─ Consumes the SceneRenderer-owned Stage-21 handoff bundle and prepared exposure
  └─ Does not remeter the resolved texture

Stage 23 (Cleanup)
  └─ SceneTextureExtracts finalized
  └─ History textures handed off for next frame
```

### 4.2 Dependency Direction

| Component | Depends On | Depended On By |
| --------- | ---------- | -------------- |
| SceneTextures | Graphics layer (IGraphics, Texture) | SceneRenderer, all stage modules, all services |
| SceneTextureSetupMode | None | SceneTextureBindings generation, stage/service consumers |
| SceneTextureBindings | SceneTextures + SetupMode + Graphics (descriptor alloc) | Renderer Core publication helpers → RenderContext/ViewFrameBindings → passes |
| SceneTextureExtracts | SceneTextures + explicit resolve/copy artifacts | Renderer Core handoff surfaces |

## 5. Resource Management

### 5.1 GPU Resources

| Product | Format | Size | Lifecycle |
| ------- | ------ | ---- | --------- |
| SceneColor accumulation | `R32G32B32A32_FLOAT` in scene rendering | `extent.x × extent.y` | Independent per-view color lease, bound to the active family; readers and GPU fences govern reuse |
| Resolved HDR | Qualified `R16G16B16A16_FLOAT` or recovery/retained `R32G32B32A32_FLOAT` | `extent.x × extent.y` | Existing resolve artifact; checked conversion and retained FP32 fallback carry frame-pinned exposure metadata |
| SceneDepth | `D32_FLOAT_S8X24_UINT` | `extent.x × extent.y` | Persistent; carries scene depth + scene stencil family |
| PartialDepth | `R32_FLOAT` | `extent.x × extent.y` | Persistent |
| Stencil | Scene/custom stencil family | `extent.x × extent.y` | Routed from the stencil aspect of `SceneDepth`, and from `CustomDepth` when the optional custom path is enabled |
| GBufferNormal | `R10G10B10A2_UNORM` | `extent.x × extent.y` | Persistent |
| GBufferMaterial | `R8G8B8A8_UNORM` | `extent.x × extent.y` | Persistent |
| GBufferBaseColor | `R8G8B8A8_SRGB` | `extent.x × extent.y` | Persistent |
| GBufferCustomData | `R8G8B8A8_UNORM` | `extent.x × extent.y` | Persistent |
| Velocity | `R16G16_FLOAT` | `extent.x × extent.y` | Persistent (if enabled) |
| CustomDepth | `D32_FLOAT_S8X24_UINT` | `extent.x × extent.y` | Persistent (if enabled); carries custom depth + optional custom stencil family |

### 5.2 Allocation Strategy

The low-level `SceneTexturesConfig` accepts RGBA16F and RGBA32F color formats;
the production scene renderer always selects RGBA32F for accumulation. FP16
admission selects the resolved HDR and qualified environment products, as
specified by the exposure inventory above.

All textures are allocated at construction time based on
`SceneTexturesConfig`. `GBufferShadowFactors` / `GBufferWorldTangent` remain
inactive in Phase 3, but their slots are now reserved in the published
`SceneTextureBindings` ABI as stable invalid entries. The current public config
contract still requires exactly four active GBuffers and rejects any
`gbuffer_count` other than `4`.

### 5.3 Resize Behavior

`Resize(new_extent)` destroys and recreates all textures at the new
dimensions. This is a full reallocation, not a view re-creation. `SceneTextures`
does not itself own setup/binding/publication state; after any resize,
`SceneRenderer` frame-start logic resets `SceneTextureSetupMode`,
invalidates `SceneTextureBindings`, and republishes nothing until the
appropriate later milestones run again.

**When called:** At frame start if viewport changed. The SceneRenderer checks
viewport dimensions against current extent and calls `Resize` if they differ.

### 5.4 RebuildWithGBuffers

`RebuildWithGBuffers()` is NOT a reallocation. It is a readiness check that:

1. Validates the active GBuffer family is allocated/present
2. Confirms the family is ready for Stage-10 promotion
3. Does **not** mutate `SceneTextureSetupMode`
4. Does **not** regenerate `SceneTextureBindings`
5. Does **not** publish or republish per-view routing metadata

The canonical Stage-10 owner is `SceneRenderer`. It calls
`RebuildWithGBuffers()` as a family-local helper, then promotes
`SceneTextureSetupMode`, regenerates `SceneTextureBindings`, and triggers the
current-view routing republish after the rebuild boundary.

That means Stage 9 may write the raw attachments, but standard scene-texture
consumers still treat `SceneColor`, the active GBuffers, and the scene stencil
route as unavailable until `PublishDeferredBasePassSceneTextures(ctx)` runs.

## 6. Shader Contracts

SceneTextures is consumed by shaders through `SceneTextureBindings`. The
shader-side contract is defined in the shader-contracts LLD
([shader-contracts.md](shader-contracts.md)). The CPU-side binding generation
is defined in §3.5 above.

Key shader-facing files:

- `Shaders/Vortex/Contracts/SceneTextures.hlsli` — accessor functions
- `Shaders/Vortex/Contracts/SceneTextureBindings.hlsli` — bindless index
  declarations

The shader-side binding contract must expose the scene/custom stencil family in
a way that matches the CPU-side routing metadata. Phase 2 may keep the custom
stencil route inactive when `enable_custom_depth == false`, but it must not
design the stencil family as scene-depth-only.

## 7. Stage Integration

SceneTextures is owned by `SceneRenderer` and passed by reference to every
stage module and subsystem service that needs it:

```cpp
// Stage module dispatch signature
void XxxModule::Execute(RenderContext& ctx, SceneTextures& scene_textures);

// Subsystem service domain methods
void LightingService::RenderDeferredLighting(
  RenderContext& ctx, const SceneTextures& scene_textures);
```

**Null-safe behavior:** SceneTextures is never null — it exists from
SceneRenderer construction. Pointer-returning optional products (`GetVelocity()`
and `GetCustomDepth()`) return `nullptr` when disabled. Aspect-view accessors
(`GetStencil()` and `GetCustomStencil()`) return a `SceneTextureAspectView`
whose `.texture` is null / `IsValid()` is false when the optional route is not
available.

## 8. Testability Approach

### 8.1 Unit Tests

1. **Construction:** Create `SceneTextures` with various configs. Verify all
   expected textures are allocated and unexpected ones are null.
2. **Resize:** Resize and verify extent changes, textures recreated.
3. **SetupMode:** Set and query flags. Verify milestone transitions.
4. **Stencil/custom-depth contract:** Verify the scene depth resource exposes
   the scene stencil family, and the optional custom depth path exposes custom
   depth plus custom stencil routing when enabled.
5. **RebuildWithGBuffers:** Call after setup, verify it does not by itself
   publish `SceneColor` / GBuffer bindings, and verify Stage 10 binding
   regeneration is still required before downstream use.
6. **Config validation:** Invalid configs (zero extent) produce errors.

### 8.2 Integration Tests

1. **Frame lifecycle:** SceneTextures survives a full frame cycle with
   setup mode progression.
2. **Bindings generation:** After each setup milestone, verify bindings
   have valid SRV indices for set-up products and `kInvalidIndex` for others.
3. **Extraction semantics:** Verify `SceneTextureExtracts` describes explicit
   handoff artifacts rather than aliasing live in-frame attachments.

### 8.3 RenderDoc Validation

At frame 10 baseline:

- Verify SceneColor, SceneDepth, and GBuffer textures appear in the resource
  list with expected formats and dimensions.
- Verify the first active subset is present and queryable: `SceneColor`,
  `SceneDepth`, `PartialDepth`, `GBufferNormal`/`Material`/`BaseColor`/`CustomData`, `Stencil`, `Velocity`, and
  `CustomDepth`.
- Verify stencil-family routing exists through the scene depth/stencil resource
  and, when enabled, through the custom depth/stencil resource.

## 9. Open Questions

None. The Phase 2 contract is fully specified after clarifying:

- the first active subset includes explicit `Stencil` and `CustomDepth`
  coverage
- the stencil family spans scene and optional custom stencil routing
- `SceneRenderer` owns both setup state and shared bindless routing metadata
- extraction describes explicit handoff artifacts rather than live scene
  attachments
