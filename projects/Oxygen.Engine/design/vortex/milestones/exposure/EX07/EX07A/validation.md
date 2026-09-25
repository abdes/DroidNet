# EX07A completion audit

Status: **EX07A validated on 2026-09-23; overall EX07 closed on 2026-09-25.**

Current progression is recorded in the [tracker](../README.md#stages-and-ownership):
All A–F stages are closed; the [F report](../EX07F/validation.md) records
final engine and editor acceptance. The audit below preserves
A's evidence and stage ownership at its closure.

The [six-step EX07 plan](../README.md#six-ordered-implementation-steps)
and its contract/property deliverables define the scope. This audit does not
replace them or move unresolved implementation into a new exclusion. A requires
the agreed contract, canonical record/interface migration and native ABI proof.
The independent reference qualification belongs to B; physical/property/ingress
repairs and full failure/lifetime qualification belong to C; measured baselines,
optimization and final integration belong to D–F.

## Requirement-to-evidence audit

| Requirement                                                         | Authoritative evidence inspected                                                                                                                                                     | Result                                                                                                                                                                                                                                       |
| ------------------------------------------------------------------- | ------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------ | -------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| Finalized review decisions and documentation committed              | `e910bd554`; [D1–D6](../../../../lld/lighting-decisions.md#lighting-model-and-capacity-decisions), LightingService/PBR owners                                                        | Passed. No unresolved product choice; numerical/model qualification is not inferred from approval.                                                                                                                                           |
| Complete authored-property review and strict migration obligations  | [LP01–LP32 inventory](../../../../lld/lighting-properties.md), ingress-owner map, validation/invalidation keys, scene-v7 offsets and suite ownership                                 | Contract defined. Existing source/packed/editor/script deficiencies remain explicit C repairs, including obsolete attenuation/sun fields.                                                                                                    |
| One canonical CPU/HLSL wire contract                                | [ABI tables](../../../../lld/lighting-gpu-abi.md); 15 production payloads in the native decode shader, matching C++ layout assertions                                                | Implemented and native-tested. No second positional-light payload, embedded-primary lighting header or old local shadow record remains in the Vortex path.                                                                                   |
| Actual upload/decode/readback proof, not same-size assertions alone | `Test/Lighting/{LightGridAbi,LightEvaluationAbi,ShadowRecordAbi,LightGridLookup}_test.cpp`; `Test/Shaders/LightingGpuAbiProbe.hlsl`                                                  | 22 native decode/lookup cases plus the CPU index-contract check pass Debug/Release, including adjacent/nonzero records, high-bit words, sentinels, reserved fields, matrix transforms and the changed-upload-lane negative control.          |
| Strong index types and symbolic invalid values                      | `Types/LightingIndices.h`, CPU record members, native sentinel tests                                                                                                                 | Implemented. Node handles stay CPU-side; selection indices address immutable GPU arrays.                                                                                                                                                     |
| Source identity, directional array and shadow ownership             | SceneRenderer selection; `ShadowReferenceBuilder`; immutable shadow-reference attachment; directional deferred/forward/fog captures                                                  | Implemented foundation. All selected directionals retain their identity; shadow indices come from produced records. Full source mutation/ingress qualification remains C.                                                                    |
| Content-relative grid semantics from actual producer parameters     | `LightGridBuilder`, `LightCullingConfig`, production `ClusterLookup.hlsli`, native lookup tests                                                                                      | Repaired and tested. CPU-produced span/curve/scale values pass actual D3D12 near/far and 170 interior/outside checks. The old implementation failed all three far-plane cases. Live forward capture verifies the published encoding.         |
| Unambiguous preparation identities and atomic rejection             | New LightingService tests for zero scene generation, duplicate/invalid view IDs and recovery; negative control fails both new tests                                                  | Repair implemented; final configuration results are recorded in the identity-admission checkpoint. Scene-less empty publications and view ID zero remain valid.                                                                              |
| No leftover compatibility interfaces in migrated paths              | Deleted positional/culler contracts, singleton sun/AP accessors, obsolete atmosphere shadow-authority fields and transient-upload compatibility state; canonical diagnostic decoders | Those migrations are verified. The unused directional `source_radius` angle copy and local explicit padding are removed; atmosphere angular size and local finite-emitter radius retain their owning paths.                                  |
| Numeric capacity policy backed by actual requirements               | D1/D6; versioned `ShadowAllocationRequirements_test.cpp`; `allocation-requirements-current.json`                                                                                     | Freeze evidence passes. Both configurations reproduce 18 D32S8/D32 queries on the RTX 3080. This does not establish a runtime admission ledger, resident frame peak or timing budget.                                                        |
| Scheduling, failure/recovery and fence-lifetime contract            | [LightingService sections 3–4](../../../../lld/lighting-service.md#3-gpu-execution-and-synchronization), review scheduling sequence and fault obligations                            | Contract defined. Complete lists are the active baseline. GPU spatial culling, sticky GPU failure/output gating and all delayed/discarded-submission outcomes are not qualified by the current baseline.                                     |
| Required capture and human evidence                                 | Recent directional/local/fog/AP/readability reports; [flicker validation](validation.md); manually verified local-light repair                                                       | Applicable checkpoint evidence exists. It is scoped to the observed behavior, not full photometric/BRDF or performance qualification.                                                                                                        |
| Final coherent closure record                                       | This audit, owning status/LLDs, complete final-code results and catalog checks                                                                                                       | Passed. The source audit checks every declared member of all 15 wire records, all 32 inventory IDs and retired interfaces; four catalog tests pass in each configuration. Current evidence and later-stage obligations are reconciled below. |

The current 24-case native test executable contains 22 D3D12 decode/lookup
cases, one D3D12 allocation query and one CPU index-contract case. Current
reproducible results are under
`out/build-ninja/analysis/vortex/exposure-lightbench/ex07a`:

- `grid-depth-LightingGpuAbi-{debug,release}.json`: all 24 cases per configuration.
- `closure-catalog-{debug,release}.json`: four catalog checks per configuration.
- `closure-contract-source-audit.json`: record/field coverage, source hashes,
  property IDs, removed interfaces and closure disposition.
- `allocation-requirements-current.json`: reproducible D32S8/D32 allocation matrix.
- `shadow-owner-{deferred,forward,fog,local}-report.txt`: canonical runtime routing.
- `ap-interface-report.txt`: 72 AP draws / 1,152 pixels; maximum error 5.96e-8.
- `readability-canonical-verdict.json`: original materials, identical HDR inputs
  across the meter-profile change and rejected white negative controls.
- `transient-cleanup-*-{debug,release}.json`: multi-allocation and same-frame
  descriptor retention plus SceneRenderer integration.

These files were inspected during the audit. The lost historical standalone
depth-format probe is explicitly identified in the
[memory review](../EX07E/shadow-memory.md#native-evidence-and-limits); it is
not substituted for a current reproducible rendering gate.

## Disposition and handoff

A's frozen contracts, canonical record/interface migration, native ABI evidence,
applicable captures and user checkpoints pass their scoped exit gate. The
`grid-depth-checkpoint.json` records the last production-source repair and its
276 Debug/Release test executions. The subsequent catalog tests add eight
executions; source/document reconciliation changes no production behavior.

At A closure, the handoff to B required independently qualified physical/BRDF/finite-source references,
known-input GPU probes, deterministic image/reference fixtures and bounded
instrumentation. EX07-01–14 and EX07-GATE stayed open at that checkpoint according
to their owning scopes. In particular, A does **not** qualify the final BRDF, finite-emitter/wide-
spot support, strict scene-v7/editor/script migration, dynamic memory admission,
all failure/lifetime paths, spatial culling or measured performance. These were
required B–F work, subsequently qualified by the linked F closure.

## Per-frame diagnostics heap leak

The EX07A shutdown investigation identified CPU heap leaks in the shared
`SceneRenderer::RenderCurrentView` diagnostics construction. CDB matched all nine
surviving allocations in a one-frame VortexBasic run to conditional
`std::vector<std::string>` member initializers. Each allocation was a 16-byte
MSVC `std::_Container_proxy`. VortexBasic leaked 27 blocks in three frames;
LightBench leaked 2,700 blocks (43,200 bytes) in 300 frames.

## Reproduction and fix

MSVC 19.51.36257 x64, toolset directory `14.51.36231`, reproduces the defect
without the engine using `/std:c++latest /EHsc /Od /MDd /Z7`. The standalone
[reproducer](../../../../../../out/build-ninja/analysis/vortex/exposure-lightbench/ex07a/conditional-vector-repro.cpp)
measures CRT heap differences across three iterations of each construction:

```text
mode=0 normal_blocks=3 normal_bytes=48
mode=1 normal_blocks=0 normal_bytes=0
mode=2 normal_blocks=0 normal_bytes=0
```

Mode 0 initializes an aggregate vector member with
`enabled ? std::vector<std::string>{"output"} : std::vector<std::string>{}`.
Mode 1 selects `std::initializer_list<std::string>` operands instead. Mode 2
uses a named aggregate and conditionally populates its vector. Both condition
outcomes are exercised. The precise compiler construction/destruction defect
has not been isolated; the allocation/lifetime failure is reproduced.

The production fix uses mode 1 for all nine conditional vector initializers.
Each vector is constructed directly from the selected list, retaining owning
strings and the existing diagnostic contents. No leak reporting or iterator
checks are disabled. Other compiler versions and Release configurations have
not been qualified for this compiler defect.

## Validation

- Debug SceneRendererDeferredCore: 64 tests passed.
- Debug DiagnosticsFrameLedger: 5 tests passed.
- Patched Debug VortexBasic: normal exit, no CRT leak dump after 3 and 300 frames.
- Patched Debug LightBench: normal exit, no CRT leak dump after 300 frames.
- Oxytidy ran on the changed C++ file: 88 warnings. A baseline analysis of the
  committed source through a Clang virtual-file overlay reports the identical
  88 check/message pairs, with zero added warnings and no coverage gaps. This
  checkpoint does not claim whole-file lint cleanliness. No suppressions were
  added; `heap-tidy-comparison.json` records the comparison.

The 300-frame runs used CDB with the normal D3D12 backend and debug layer:

```powershell
& 'C:/Program Files (x86)/Windows Kits/10/Debuggers/x64/cdb.exe' -g -G `
  -logo <debugger-log> <Debug-demo-executable> `
  --frames 300 --fps 60 --vsync=false --debug-layer=true --aftermath=false
```

Evidence under `out/build-ninja/analysis/vortex/exposure-lightbench/ex07a/`:
`leaked-allocation-stacks.txt`, `vortexbasic-3-before.cdb.log`,
`lightbench-cdb-shutdown.log`, `vortexbasic-3-after.cdb.log`,
`vortexbasic-300-after.cdb.log`, `lightbench-300-after.cdb.log`, corresponding
stdout logs with runtime exit code zero, `heap-scenerenderer-debug.json`,
`heap-ledger-debug.json`, and `tidy-heap/`.

This closes the observed frame-scaled CPU leak. It does not close the remaining
EX07A lighting ABI migration or establish GPU resource lifetime correctness.

## Offscreen lighting flicker: same-frame descriptor lifetime

On 2026-09-22 the user observed alternating lit/black geometry in MultiView's
lower offscreen panes in Release, with the grid still visible. The reproduction
used `--directional-array-proof true --offscreen-proof-layout true
--pip-wireframe false -v=-1` in the pending directional-array checkpoint.

`ValidatedOffscreenSceneSession::ExecuteInsideFrame` restarts scene preparation
for offscreen execution and restores the main scene preparation afterward.
Those starts use the same physical frame sequence and slot.
`TransientStructuredBuffer::OnFrameStart` previously unregistered that slot's
SRVs every time, including descriptors referenced by earlier queued draws.
Their reuse could substitute a later lighting publication; generation checks
then rejected those inputs and the geometry received no lighting.

The allocator now preserves allocations when both sequence and slot are
unchanged. Advancing the slot to a different sequence still retires its old
descriptors under the existing caller-owned frame-fence contract.

The committed regression starts frame zero, publishes an SRV, repeats the same
frame start, and checks the original registry entry remains live while another
allocation is published. It also checks normal retirement on sequence advance.
It failed before the fix with the original SRV absent from the registry.
All **13 transient-buffer tests pass in Debug and Release** after the fix.
Oxytidy covered the changed source, header and test with no failed contexts;
its six remaining findings concern unchanged code. No suppression was added.

The user reviewed the Ninja Release application with the exact reproduction
options and confirmed: **both lower views are stable**. This live check matters
because capture instrumentation can change the timing of premature descriptor
reuse. Logs and test JSON are under
`out/build-ninja/analysis/vortex/exposure-lightbench/ex07a/repeated-frame-*`.

This closes the reported flicker and same-frame descriptor-reset defect. It is
not a blanket qualification of all upload, deferred-CBV or shadow-resource
lifetime paths in EX07.

The separate persistent deferred-CBV overwrite was subsequently repaired with
immutable frame-owned batches; see the
[deferred-constant checkpoint](#deferred-constant-lifetime-checkpoint).
That repair has its own failing-before/passing-after regression and capture proof.

## EX07A contract review

Status: **EX07A validated on 2026-09-23; overall EX07 subsequently closed on 2026-09-25.**
Historical checkpoints below preserve their original scope; [F acceptance](../EX07F/validation.md) owns final closure.
The [completion audit](validation.md) owns the current A disposition;
implementation sections below record historical checkpoints and their scoped evidence.
The initial source review used clean `editor` at `09aa65362` on 2026-09-22.
Its findings predate the implementation checkpoints recorded below. Each
checkpoint states its validation scope; full physical and performance
qualification is not claimed.
The approved [EX07 plan](../README.md) remains
the execution authority; this is its A checkpoint, not a replacement plan.

## Review result

The initial paths required the repairs below. D1-D6 describe the selected product
design, including the current model-2 source and BRDF decisions. The
[GPU contract](../../../../lld/lighting-gpu-abi.md) gives exact layouts and
replacement obligations. The [property inventory](../../../../lld/lighting-properties.md)
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

The [property inventory](../../../../lld/lighting-properties.md) owns exact field/default/
transport obligations, the [wire contract](../../../../lld/lighting-gpu-abi.md) owns byte
layout and the [PBR owner](../../../../../renderer-core/physically-based-rendering.md) owns
equations/tolerances. These are implementation requirements, not claims that
current source has been repaired.

### Scheduling and caller outcome

The [LightingService LLD](../../../../lld/lighting-service.md#3-gpu-execution-and-synchronization)
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
Debug results (historical `vortex.lightinggpuabi-debug.json`; original artifact unavailable)
and Release results (historical `vortex.lightinggpuabi-release.json`; original artifact unavailable)
are partial ABI evidence. Evaluation records, full bindings, projection records,
matrix probes, catalog/capture analysis and complete producer/consumer migration
remain open. The spatial culler has not been connected to these records.
The grid-metadata producer test also required repairing Core `ResolvedView`
validation: finite signed orthographic near planes are accepted; perspective
near remains positive and every far plane remains finite and above near. Core
view and LightingService suites each pass 5 tests in both configurations.
Checkpoint manifest (historical `abi-foundation-checkpoint.json`; original artifact unavailable)
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

Consumer checkpoint evidence (historical `lookup-consumer-checkpoint.json`; original artifact unavailable)
records source hashes and results. This is still a complete-list baseline, not
spatial culling. Culler/lookup cell equivalence, full binding/evaluation/shadow
record migration, same-submission failure presentation, physical BRDF parity and
full orthographic rendering remain open.

The next evaluation-record prerequisite adds
`Core/Lighting/LightPhotometry.{h,cpp}`: checked per-component tint/EV
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
Debug (historical `photometry-debug.json`; original artifact unavailable)
and Release (historical `photometry-release.json`; original artifact unavailable).
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
for point/spot respectively; manual checks confirmed the visible spotlight fix.
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
[separate flicker fix](validation.md) records the failing
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

### Fractional content-rectangle lookup checkpoint

The native lookup probe exposed a final-tile error: clamping a content-relative
coordinate to `extent - 1` erased a last cluster narrower than one pixel. For a
64.75-pixel rectangle beginning at `(13.25, 7.25)`, valid raster sample centers in
the final row/column incorrectly decoded to cluster zero. The probe returned
`[0,0,0,0,0,0]` instead of `[0,1,2,3,3,0]` before the repair.

Lookup now clamps to the full floating-point extent, then bounds the integer
cluster coordinates by the published grid dimensions. This preserves fractional
edge tiles and retains safe behavior at and beyond the upper boundary. The
complete native suite passes **21 Debug / 21 Release** cases; the changed test
is oxytidy-clean. Evidence: `fractional-tile-before.log` and
`fractional-tile-{debug,release}.json` under `ex07a`. This is coordinate
qualification, not proof of a spatial culler or a rendered-performance gain.

### Atmosphere-source fog shadow checkpoint

Removed the obsolete slot-0-only shadow flag, atmosphere shadow-authority slot,
replicated cascade counts and their hash/publication/test readers. These values
are not replaced by compatibility fields: the canonical lighting selection and
shadow family records own source/projection identity. Both active atmosphere
sources now resolve their own selection before volumetric shadow sampling; the
existing renderer toggle controls both through one uint pass constant.

`FogDirectionalShadows_test.cpp` dispatches the production fog shader with
Secondary/ordinary/Primary selection order and independently reordered shadow
families/cascades. It checks isolated and combined sources, independently swapped
visibility, disabled shadows and an absent request against homogeneous-medium
Beer-Lambert/isotropic-scattering values. The negative control restores Primary-
only shadow sampling and fails the isolated/combined Secondary occlusion cases;
restoring the production fix passes. Debug and Release each pass all **63
environment-service tests and 12 native fog tests** (150 executions). All 13
changed C++ files were processed by oxytidy: no new-test or changed-line findings;
122 existing whole-file warnings remain, with no new warning suppressions except
the requested structure-layout magic-number guard.

The `consumer-visual --visual-fog volume --directional-array-proof true` capture
passes `AnalyzeRenderDocFogDirectionalShadows.py`: both views' fog dispatches
access Secondary selection 0 (three cascades, 1024 resolution) and Primary
selection 2 (two cascades, 2048 resolution), using matching publication identities
and distinct surfaces. Native tests prove numerical visibility; the capture
proves live producer/consumer wiring. No performance or full visual-parity claim.
Evidence under `ex07a`: `fog-shadow-{environment,regressions}-{debug,release}.json`,
`fog-shadow-negative.log`, `fog-directional-report.txt`,
`fog-shadow-tidy-verified/` and `fog-shadow-checkpoint.json`.

Ordinary role-None fog scattering, unified physical atmosphere resolution and
broader fog qualification remain separate open repairs. This checkpoint removes
one conflicting shadow contract; EX07A remains in progress.

### Shadow-reference authority checkpoint

The audit found that `ForwardLightPublisher` still independently predicted shadow
record indices from light-kind counters. The canonical ABI already assigns this
authority to ShadowService. Removed those counter loops and upload buffers;
ShadowService now validates actual projection records and publishes the dense
selection-reference arrays with the shadow package. Lighting attaches their
per-view descriptors in an immutable replacement header after generation/status
checks. Missing projections invalidate the view instead of silently selecting an
unshadowed path. Zero-energy/range requests retain explicit NoInfluence coverage.

Focused cases cover reordered projections (including a spot associated with a
cube), absent/duplicate maps, stale attachment and the current fifth point-shadow
request. Explicit failure closes the silent-loss path; it does not implement
budgeted growth or claim the inherited 4/8 limits are accepted product limits.
Debug/Release each pass **26 lighting, 17 shadow and 68 SceneRenderer tests**
(222 executions). All 13 changed C++ files were processed by oxytidy; the new
builder and revised publisher are clean, with no changed-line findings or new
suppressions. Existing whole-file warnings remain outside this checkpoint.
RenderDoc passes the directional deferred, forward, fog and local-shadow
analyzers: consumers follow the actual per-view references with matching
identities, and all three deferred directional contributions remain visible.
The Release native offscreen proof runs 180 frames with the debug layer, exits
zero, and has no warnings/errors. No renewed visual confirmation was needed for
this ownership change; the captured runtime wiring and existing manually verified
stable view layout are distinct evidence.

Evidence under `ex07a`: `shadow-owner-*-{debug,release}.json`,
`shadow-owner-{deferred,forward,fog,local}-report.txt`,
`shadow-owner-release-180.log`, `shadow-owner-tidy-final/`,
`shadow-owner-publisher-tidy/` and `shadow-owner-checkpoint.json`. This closes
independent map-index prediction, not wide-spot support, resource-budget growth,
full submission/failure recovery, or EX07A as a whole.

### Source identity and aerial-perspective interface checkpoint

Local selections now retain the same strongly typed source-node identity as
directionals. Point/spot scene traversal publishes the visited handle, and the
existing local-shadow integration tests compare the selection against each
actual source node. GPU records continue to carry immutable selection indices;
the native decode suite verifies that adding CPU identity does not change wire
layouts.

Removed `GetSunDirectionWS`/`HasSunLight` and the unused sun-direction argument
from both aerial-perspective helpers and their opaque/translucent callers. This
also removes the translucent path's synthetic Primary direction. The camera-
volume LUT already owns both atmosphere-source contributions; no second light
authority replaces these fields. Direct DXC compiles pass; optimized output is
not byte-identical, so equivalence is not inferred from binary hashes.

Debug and Release each pass **21 native ABI/lookup, 26 lighting, 17 shadow,
68 SceneRenderer and one native AP matrix test** (266 test executions).
The native AP fixture and RenderDoc analyzer pass 72 composition draws / 1,152
pixels across FP16/FP32 inputs, with maximum absolute error 5.96e-8 against the
independent transfer equation. Both 233-module shader archives rebuild, and the
180-frame Release native offscreen proof exits zero without warnings/errors.
Oxytidy covers all three changed C++ files with no changed-line findings; existing
whole-file diagnostics remain. Evidence under `ex07a`:
`source-identity-*-{debug,release}.json`, `ap-interface-{debug,release}.json`,
`ap-interface-report.txt`, `source-identity-tidy/`,
`source-identity-release-180.log` and `source-identity-checkpoint.json`.
This checkpoint does not qualify full source-mutation/round-trip behavior or a
separate forward raster image oracle. EX07A remains in progress.

### Transient-upload compatibility removal checkpoint

Removed `SlotData`'s deprecated single-allocation/SRV/native-view fields and the
unused `ReleaseSlotView` API. An allocation is now a local RAII value until it is
moved into the frame slot's retained allocation list. The existing same-frame
sequence guard and per-allocation view retirement remain the active lifecycle.
No replacement compatibility storage or API was added.

Debug/Release each pass all **13 transient-buffer and 68 SceneRenderer tests**
(162 executions), including multiple allocations, slot reset and repeated frame
start descriptor retention. The 180-frame Release offscreen directional proof
exits zero with no warnings/errors. Oxytidy covers both changed files: only the
existing pointer-arithmetic and missing-nodiscard diagnostics remain; no warning
was suppressed. Evidence under `ex07a`: `transient-cleanup-*-{debug,release}.json`,
`transient-cleanup-release-180.log`, `transient-cleanup-tidy-final/` and
`transient-cleanup-checkpoint.json`.

The closure audit still needs to retire/migrate stale diagnostic decoders and
refresh missing native allocation-query evidence. This cleanup does not qualify
all delayed/discarded submission paths or close EX07A.

### Diagnostic-decoder and allocation-evidence checkpoint

`AnalyzeRenderDocLitReadability.py` now follows the 80-byte deferred constants'
selection index into the 64-byte directional array through the 96-byte lighting
header. Its paired assertion consumes resolved RGB lux; the retired tint/scalar
payload is not retained as an alternate input. Fresh frame-42/54 `atmosphere-lit`
captures pass the original material-color, histogram, remeter, identical-HDR and
white-negative-control checks in both views.

The environment-header probe now follows the actual view root to the 112-byte
header. The unreferenced `ProbeRenderDocDirectionalCsm.py` is retired: its embedded
28-word cascades, descriptor-position assumption and fixed camera no longer
match the renderer. `AnalyzeRenderDocShadowRecords.py` and the directional-array
analyzers provide the tested canonical header/family/cascade inspection.

A versioned native test now reproduces the backend shadow-allocation query for
D32S8/D32: eight large descriptors plus a six-layer 32x32 alignment control per
format. The RTX 3080 reproduces 512/832 MiB medium and 2,048/3,328 MiB maximum
D32S8 sets, with half-sized D32 large arrays; both small controls occupy 64 KiB.
It allocates no shadow textures. The [memory review](../EX07E/shadow-memory.md)
contains the reproducible command and explicitly distinguishes the missing
historical GPU format probe from this fresh allocation evidence.

Debug and Release each pass all **22 native cases** (44 executions), and their
allocation matrices match exactly. The new C++ query is oxytidy-clean. Evidence under `ex07a`: `allocation-refresh-*.json`,
`allocation-requirements-current.json`, `readability-canonical-*-report.json`,
`readability-canonical-verdict.json` and `environment-header-migration.txt`.

### Identity-admission checkpoint and completion audit

The [requirement-to-evidence audit](validation.md) found that CPU
preparation accepted nonempty selections with scene generation zero even though
the production shader rejects them. Duplicate view IDs also overwrote entries
in the publication map. Preparation now rejects zero scene identity for nonempty
selections, invalid view IDs and duplicate view IDs before publication, with
checked view-count narrowing. Scene-less empty publications and view ID zero
remain valid. Failed preparation clears prior CPU routes; recovery publishes
fresh view generations.

Both new tests fail under the previous implementation and pass after the repair.
Debug/Release each pass **28 lighting and 68 SceneRenderer tests** (192
executions); the 180-frame Release offscreen proof exits zero without warnings or
errors. Both changed C++ files are oxytidy-clean. Evidence under `ex07a`:
`identity-admission-negative.log`, `identity-admission-*-{debug,release}.json`,
`identity-admission-release-180.log`, `identity-admission-tidy-final/` and
`identity-admission-checkpoint.json`.

The audit keeps EX07A open for actual producer-derived perspective-depth
qualification, the identified unused CPU selection fields and final coherent
reconciliation. Later B–F obligations remain explicit and are not waived by the
ABI or preparation tests.

### Producer-derived depth mapping and selection cleanup checkpoint

The new native probe fed the actual CPU helper's parameters into production HLSL.
Near/far pairs for ordinary, narrow and large close-plane intervals returned
`[0,30,0,0,0,25]`, rather than `[0,31,0,31,0,31]`, before the repair. Removed the
legacy near offset, far padding and unused CPU lookup helper. The existing
64-byte grid metadata now carries explicit `(span, curve, slice scale)` values
and uses the near-relative logarithmic equation frozen in its owning ABI LLD.
The normalized form avoids cancellation between large affine coefficients.

Local-grid preparation rejects non-normal/unrepresentable spans explicitly;
views with no local lights need no grid encoding and still publish valid empty
results. The native tests cover endpoints and 170 interior/outside values across
ordinary, narrow, large, small and wide intervals against an independent double
reference. Removed the unused directional angle copy (`source_radius`) and local
CPU padding; local emitter radius and the atmosphere disk keep their owners.

Debug/Release each pass **24 native, 29 lighting, 17 shadow and 68 SceneRenderer
cases** (276 executions). All six changed C++ files were processed by oxytidy,
with no changed-line findings or added suppressions; existing whole-file
warnings remain. Both shader archives build. The forward RenderDoc proof checks
the live view's 0.05–160 m metadata, endpoint slices 0/31 and the canonical light/
shadow references. The 180-frame Release offscreen proof exits zero without
warnings/errors. This does not claim a spatial-culling or performance gain.
Evidence under `ex07a`: `grid-depth-negative.log`,
`grid-depth-*-{debug,release}.json`, `grid-depth-forward-report.txt`,
`grid-depth-tidy-final/`, `grid-depth-release-180.log` and
`grid-depth-checkpoint.json`.

The two concrete follow-ups from the completion audit are repaired. Final A
contract/catalog/document reconciliation remains; the active goal now covers
all EX07 slices and cannot complete at A alone.

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
clean. The check report (historical `contract-review-checks.json`; original artifact unavailable)
is documentation evidence only. The final contract checks (historical `final-contract-checks.json`; original artifact unavailable)
additionally cover changed-document local links, 19 declared layout sizes,
BOM/LF preservation and all six approvals. At the documentation checkpoint, no
engine/editor build, production test, GPU sentinel, native capture or performance
measurement had been run; the implementation checkpoint above supersedes that
statement for the new ABI target only. A
standalone C++20 /W4 /WX allocation-query program and CPU mathematical checks
were run as described below.

The additional CPU mathematical check (historical `mathematical-checks.json`; original artifact unavailable)
passed 1,440 reciprocal-pair cases (maximum absolute difference 2.78e-17),
the alpha=1 analytic unit-reflectance furnace check (9.63e-13), reflected-energy
bounds over the sampled domain and spot solid-angle integration (1.12e-15
relative), analytic finite-source irradiance (1.90e-14 relative) and projected
directional-disk illuminance (1.68e-12 relative). The importance-sampled alpha=1 directional-moment check differs from
its analytic value by at most 1.11e-4. This is a draft-model consistency check,
**not** the <=1e-5 production-reference uncertainty certificate required by B.
The [allocation query](../../../../../../out/build-ninja/analysis/vortex/exposure-lightbench/ex07a/allocation-requirements-current.json)
measures resource requirements on the reference adapter without allocating maps.
Neither artifact qualifies rendered images, GPU ABI, live resource lifetimes or
performance. The allocation-query source is now versioned in the native test target. The older model-check source/results are historical and unavailable; B must supply a new reproducible qualified reference.

Local UE5.7 source inspected as implementation reference, not an Oxygen oracle:
`F:/Epic Games/UE_5.7/Engine/Source/Runtime/Renderer/Private/LightGridInjection.cpp`
(`PrepareForwardLightData`, `ComputeLightGrid`), `LightRendering.cpp`
(`RenderLights`), and shaders `DeferredLightingCommon.ush`
(`GetLocalLightAttenuation`, `GetCapsule`), `BRDF.ush` (`SphereMaxNoH`). Do not copy
its bounded-list overflow behavior or distance regularizer into Oxygen's agreed
physical/failure contract.

**Design disposition:** D1-D6 are approved and the target mathematical, wire, persistence, resource and failure contracts are frozen in their owning documents. No product decision remains queued.

**EX07A gate: passed.** The [completion audit](validation.md) records
canonical CPU/HLSL layouts and every-member assertions, migrated interfaces,
native GPU decoding and catalog checks, applicable runtime captures, approved
capacity/failure contracts and reproducible evidence at A closure. At that
checkpoint the spatial culler had not been connected and EX07B–F remained ahead.
For current progress, use the [tracker](../README.md#stages-and-ownership):
A–E are closed; the [D baseline register](../EX07D/validation.md) and
[accepted E comparisons](../EX07E/validation.md) remain credited.
[F engine acceptance](../EX07F/validation.md) and EX07-GATE are closed, including final editor approval.
