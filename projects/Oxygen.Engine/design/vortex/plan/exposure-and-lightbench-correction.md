# Exposure correctness and LightBench qualification

Status: `planned`; implementation and runtime qualification have not started.

Date: 2026-09-16

This is a corrective work package, not a newly numbered VTX milestone. It
addresses the four established issues below. Historical VTX-M03, VTX-M06A,
and VTX-M07 evidence retains its original scope. This document defines work
to execute; its creation does not authorize or claim implementation.

Paths in code blocks and inline code are relative to the Oxygen.Engine root
unless explicitly identified as repository-root paths.

## 1. Goal

Correct exposure documentation and fixed-exposure propagation, implement
game-facing automatic-exposure initialization and history control, and make
LightBench a reproducible, measurable client of the production renderer.

Execute the numbered slices in section 8 in order. Slices 3 and 4 use existing
native test/capture facilities for their gates; they do not depend on the new
measurement facility in slice 6 or the acceptance runner in slice 8. Each slice
must satisfy its gate before dependent work proceeds. Record failures as failures;
do not adjust reference inputs or tolerances to conceal renderer defects.

## 2. Scope

| Issue | Required outcome | Design owner |
| --- | --- | --- |
| Exposure documentation drift | One current mathematical contract, corrected examples, explicit defaults and historical scope | Engine PBR specification |
| Fixed-exposure floor | Valid resolved exposure reaches tonemapping without the undocumented `1e-4` floor | Core exposure math and Vortex PostProcess |
| Automatic startup and reset | Explicit per-view initialization, transitions, resets, and GPU history lifetime usable by games | View lifecycle and PostProcessService |
| Inadequate LightBench | Complete experiments, useful startup image, independent reference calculations, valid measurements, and repeatable execution | LightBench; reusable instruments owned by renderer diagnostics |

Deliver code, owning design updates, tests, native GPU evidence, and accurate
status together. The existing exposure parity plan remains the owner of its
broader program; link this bounded work package from that plan.

## 3. Non-scope

- A new exposure calibration convention or alternate demo renderer.
- Full pre-exposure migration, local exposure, histogram-quality redesign,
  or closing the entire UE5.7 exposure parity program.
- New GI, shadow, material, or photometric conversion implementations hidden
  inside the bench work. A discovered dependency must be recorded and scoped
  explicitly before implementation; affected experiments remain unqualified.
- General editor UX redesign or unrelated repository cleanup.
- Changes to production behavior merely to make reference images look better.

## 4. Current state and evidence boundary

The investigation established the following from current source and local
settings. These are not measurements of the user's original running frame.

1. `src/Oxygen/Core/Types/PostProcess.h` implements:

   ```text
   bias = 2^compensation_ev * (exposure_key / 12.5)
   fixed_exposure = bias / 2^ev100
   average_luminance_from_ev = 0.18 * 2^ev100
   auto_exposure = (target_luminance * bias) / adapted_average_luminance
   ```

   EV100 is logarithmic; `exposure_key` is dimensionless; the result is a
   linear multiplier. The calibration constant 12.5 is not an EV setting.
   The default auto target is 0.18. Core math and the current editor-linked
   specification agree, but older PBR prose and examples disagree.
2. `Vortex/PostProcess/Passes/ExposurePass.cpp::Execute` clamps fixed output
   to at least `1e-4`. With key 12.5 and zero compensation, settings beyond
   EV100 13.287712 lose their intended attenuation. EV14 becomes approximately
   0.712288 stops too bright. The conversion helper itself is not the defect.
3. The automatic exposure buffer is seeded from `config.fixed_exposure`.
   DemoShell's `ResetAutoExposure(initial_ev)` is empty. A stored manual EV
   can therefore affect automatic startup even though it is not multiplied
   into the final automatic exposure on every frame.
4. LightBench's default point light is tangent to its cards. The saved spot
   configuration misses gray/white card centers. The demo does not create a
   directional reference light. Presets change visibility rather than a
   complete experiment. Saved settings can change the starting conditions.
5. The opaque deferred path does not multiply scene color by both manual
   pre-scene exposure and automatic exposure. Do not retain that rejected
   hypothesis as an explanation for LightBench's opaque objects.

Primary source areas:

- `Examples/LightBench/LightScene.*`, `MainModule.cpp`, `LightBenchPanel.cpp`.
- `Examples/DemoShell/Services/PostProcessSettingsService.*`,
  `EnvironmentSettingsService.*`, and `SettingsService.*`.
- `src/Oxygen/Vortex/CompositionView.h`, `SceneRenderer/SceneRenderer.cpp`,
  `PostProcess/PostProcessService.*`, and `PostProcess/Passes/ExposurePass.*`.
- `src/Oxygen/Graphics/Direct3D12/Shaders/Vortex/Services/PostProcess/`.

The existing scalar-calibration test in `Vortex/Test/PostProcessService_test.cpp`
does not prove pass-constant propagation or the resulting GPU output.

The review also verified that the current adaptation shader uses exponential
interpolation, `alpha = 1 - exp(-k * delta_time)`, despite EV/s labels on authored
speeds. Here `k` is a response coefficient; instantaneous stop rate depends on
the remaining error. The existing exposure improvement plan owns the hybrid
EV-response replacement. Record this distinction and use the current recurrence
for this package's lifecycle tests; do not silently change the temporal algorithm.

## 5. Existing behavior to preserve

- Keep the current exposure equations, authored units, and native enum values.
- Preserve distinct scene-system and DemoShell defaults unless a separately
  justified design decision changes them. Do not homogenize defaults as cleanup.
- `ViewStateHandle` remains the persistent identity; `ViewId` remains the
  current publication/composition identity. Do not create a second cache key.
- View producers own identity and transition intent; Renderer Core validates
  routing; PostProcessService owns exposure processing and history resources.
- Streaming, spawning, ordinary light changes, and compatible resolution
  changes preserve adaptation. Camera cuts carry an explicit transition policy.
- Preserve independent views and the documented exposure-source sharing contract.
  Existing code is not proof that this contract is correctly implemented.
- Exposure disablement means a multiplier of one. Tone-curve selection remains
  independent of exposure enablement.
- Use production materials, lighting, composition, and public runtime APIs.
- Preserve user-authored files and unrelated working-tree changes. Automated
  experiments must not overwrite personal demo settings.

## 6. UE5.7 references and bounded parity

Before changing lifecycle behavior, inspect the local UE5.7 sources under
`F:/Epic Games/UE_5.7/Engine`:

- `Source/Runtime/Renderer/Private/PostProcess/PostProcessEyeAdaptation.cpp`
  and its header: initialization, force-target behavior, history, and dispatch.
- `Shaders/Private/PostProcessEyeAdaptation.usf`: histogram solve, target versus
  adapted exposure, and initialization behavior.
- Follow the referenced view-state and camera-cut definitions to establish
  ownership, invalidation, and exposure-source behavior.

These locations exist locally. The plan review inspected
`PostProcessEyeAdaptation.cpp::GetEyeAdaptationParameters` (force-target conditions),
`SceneRendering.cpp::FViewInfo::ShouldUpdateEyeAdaptationBuffer` (update ownership),
and `PostProcessEyeAdaptation.usf::EyeAdaptationCommon` (target/current scale and
metered luminance). Complete the symbol-level mapping in the owning LLD before
implementation; this inspection is not runtime parity evidence.

UE forces its target on camera cuts and permits a borrowing view to initialize
an absent source buffer. Oxygen's explicit cut policies and documented
previous-frame, order-independent sharing contract are deliberate differences.
Record the selected bootstrap policy and these differences; do not claim
byte-identical UE behavior or quietly change Oxygen's view contract.

UE exposes an explicit luminance-normalization factor through
`LuminanceMaxFromLensAttenuation`; Oxygen's accepted equation is an engine
calibration convention. Preserve it, document the scene-color unit/normalization,
and do not equate a dimensionless key of 12.5 with proof of ISO photometric
calibration. Absolute cd/m2 claims require the light/material/color-space unit
chain to be established independently. Bounded lifecycle correspondence does
not imply full exposure or physical-lighting parity.

## 7. Contract truth table

| Surface | Producer and consumer | Valid / disabled behavior | Invalid / stale behavior |
| --- | --- | --- | --- |
| Resolved fixed exposure | Core math and SceneRenderer -> ExposurePass/TonemapPass | Finite representable positive multiplier; disabled exposure supplies one | Reject invalid authored combinations through the defined configuration error path; never silently apply a brightness floor |
| Exposure transition request | Game/editor/view producer -> Renderer Core -> PostProcessService | View-state identity, request generation, policy, optional seed; consumed once | Reject an invalid identity; retain an unprocessed request until processing is possible; discard obsolete generations explicitly; do not acknowledge an update that was never submitted |
| GPU exposure history | ExposurePass -> next adaptation update and tonemapper | Validity, target/current values, frame identity; ordinary frames retain state | First use or explicit reset initializes according to policy; destruction retires resources after GPU completion |
| Stateless exposure | Producer without persistent state -> PostProcessService | Document supported fixed and stateless automatic behavior | Never allocate hidden persistent history or silently claim temporal adaptation |
| Measurement result | Renderer measurement facility -> diagnostics consumer | Source domain, frame sequence, view identity, samples and validity | Reject stale or mismatched results; unavailable readback is not a zero measurement |
| Experiment verdict | LightBench reference calculation + matching GPU result | Experiment revision, independent expectation, declared tolerance | Modified/unsupported/invalid experiments cannot report a reference pass |

The LLD must specify CPU/HLSL layouts, upload-buffer lifetime, barriers, and
publication semantics for any changed payload. A CPU fallback scalar must not
be labeled as the actual GPU automatic exposure.

### 7.1 Shared history has a single writer

`CompositionView::exposure_source_view_id` and the multiview LLD specify
previous-frame exposure consumption from another view's persistent state.
`ExposurePass::Execute` currently selects `exposure_view_state_handle` as the
update key and dispatches a histogram from the current view's scene signal.
Using a shared key alone therefore does not implement read-only sharing.

Specify and implement these constraints in slice 4:

- Only the owning view meters into and updates its exposure history, at most
  once per logical frame. A dependent view must not meter its image into the
  source's state or consume the source's current-frame write by render order.
- Dependent views consume the published previous-frame source result. Preserve
  an immutable previous generation until all readers finish; use existing
  frame/fence resource ownership rather than assuming one mutable buffer is safe.
- Define source creation without history, inactivity, removal, cycles, and
  conflicting resets. An explicit diagnostic must identify fallback behavior.
- Define whether a consumer's reset is rejected or operates on its own dormant
  history; it must not reset the source implicitly. Only the source owner can
  request a source-history reset.
- Render source and consumer in both orders and at different luminance levels.
  Results must be independent of scheduling order and consumer scene content.

### 7.2 Settings ownership is part of experiment correctness

`DemoShell::OnSceneActivated` reapplies saved post-process values independently
of `force_environment_override`. That flag alone cannot protect a reference
experiment. Slice 5 must define precedence for camera, environment, rendering,
and post-process services, and slice 7 must enforce it in one coherent activation
transaction. Reference inputs become authoritative before any frame is measured.
Changing this policy for LightBench must not change other demos' persistence.

### 7.3 EV seeding and exposure continuity are different operations

Let `T` be the authored automatic target, `b` the current compensation/key bias,
and `s_previous` the last effective exposure multiplier.

- Explicit `SeedFromEv100(ev)` starts from adapted average luminance
  `L_seed = 0.18 * 2^ev`, then derives `s_seed = T * b / L_seed`.
- Preserving the previous multiplier when entering Auto requires
  `L_seed = T * b / s_previous`. Copying the old manual EV is insufficient
  when the automatic target differs from 0.18 or bias changed at the transition.
- Entering Manual or ManualCamera honors the new authored settings immediately.
  Do not silently animate a fixed mode to enforce visual continuity. A separately
  requested cinematic exposure blend is outside this correction.
- Apply the automatic metering range and numerical policy explicitly. If it
  prevents exact continuity, report that constraint and test the defined result;
  do not claim an exact match while clamping it.

Define the consumed settings revision and event ordering at the transition so
both CPU and GPU use the same `T`, `b`, range, and previous result. Resolve legal
zero target and other degenerate cases in slice 2 before applying formulas that
take logarithms or reciprocals.

### 7.4 Histogram validity and request completion

A valid solve requires finite accepted input samples and positive retained
weight after percentile trimming. A nonzero raw histogram count is insufficient.
Finite black samples are valid scene input and follow the documented dark-bin
and metering-range behavior; missing, empty, or nonfinite input is a different
state. Define sample rejection and report the retained weight explicitly.

If no valid solve is available, retain valid history, keep an initialization or
reset pending, and report the cause. A view without history uses the explicitly
specified initialization fallback, independent of an inactive manual setting.
Do not publish a fictitious measured target or acknowledge the reset as applied.

Distinguish CPU request submission from GPU consumption. Track requested and
consumed generations in the frame-safe state; the GPU consumes a metered reset
only when a valid solve initializes the published history. Repeated frames may
carry the same request without applying it twice. Do not add a blocking CPU
readback to decide whether adaptation may proceed.

Specify the supported metering extent and counter capacity. The current shader
adds up to 255 weight units per pixel into 32-bit unsigned counters and a 32-bit
total. More than 16,843,009 full-weight samples can overflow; full-frame 8K is
already beyond that limit. Define a bounded overflow-safe metering sample budget
or accumulation strategy in slice 2. Preserve view-rect/mask semantics and prove
the chosen weighting. An out-of-domain request must be diagnosed, not interpreted
as a valid dark frame. Test the limit and an 8K display configuration explicitly.

### 7.5 Numerical domains and supported zero target

Document three separate bounds: accepted authoring values, shader-operational
floating-point range, and observable render-target precision. A value representable
by a CPU float can still be flushed as a subnormal by the shader. Audit the actual
ShaderBake/DXC options and generated shader behavior before selecting operational
bounds. Do not promise positive-subnormal preservation without evidence or
confuse output quantization with a permissible exposure floor.
The relevant compiler reference is Microsoft's
[DXC denormal-mode specification](https://github.com/microsoft/DirectXShaderCompiler/wiki/Denorm-Mode).

The current editor contract permits `AutoExposureTargetLuminance = 0`. Preserve
that legal input in the design: its mathematical output multiplier is zero;
it does not make the metered scene luminance zero. Keep log-domain metering history
valid, and never take a logarithm or reciprocal of the zero output multiplier.
The current positive floors in `ExposurePass`/`Exposure.hlsl` must be reconciled
explicitly with this case. Record the bounded correction in slice 2 rather than
silently rejecting a previously supported authored value.

When continuity requires inversion of a zero previous multiplier, use the
specified remeter/retained-luminance transition policy and report that exact
multiplier continuity is unavailable. Tests must cover zero target, positive
target restoration, equal min/max EV, zero adaptation speed, zero delta time,
nonfinite values, and degenerate percentile intervals. Reject invalid coupled
ranges atomically; distinguish a deliberate fixed metering range from invalid data.

## 8. Ordered implementation slices

### Slice 1 - Reconcile the baseline and documentation authority

- [ ] Re-read the current source, owning LLDs, roadmap/status, and relevant
  uncommitted changes. Preserve this bounded issue list; update evidence if
  source has changed since the investigation.
- [ ] Update [the PBR specification](../../renderer-core/physically-based-rendering.md)
  with the current equations, quantity definitions, supported numerical domain,
  measurement stages, camera conversion, and worked examples.
- [ ] Correct f/11, 1/125 s, ISO100 to EV100 approximately 13.885. Remove its
  incorrect association with EV9.7 without silently changing runtime defaults.
- [ ] Reconcile [the physical-lighting roadmap](../../renderer-core/physical-lighting-roadmap.md),
  preserving historical evidence and recording the established current defects.
- [ ] Correct [the panel design](../../renderer-core/post-process-panel-design.md).
  Link repository-root [environment-authoring.md](../../../../../design/editor/lld/environment-authoring.md)
  to the engine math authority while retaining editor-owned defaults and fields.
- [ ] Register this corrective package in [PLAN.md](../PLAN.md),
  [IMPLEMENTATION_STATUS.md](../IMPLEMENTATION_STATUS.md), and the relevant
  indexes. Do not invalidate unrelated historical milestone evidence.

**Gate:** equations, defaults, examples, historical claims, and source pointers
agree at their stated scope. Maintain a small old-claim-to-current-authority
mapping during review so meaningful requirements are not lost.

### Slice 2 - Specify the engine corrections before changing behavior

- [ ] Expand [PostProcessService LLD](../lld/post-process-service.md) with fixed
  exposure propagation and the complete game-facing history state machine.
- [ ] Define public request routing, per-view generations, first-frame metering,
  explicit EV seeding, manual/auto transitions, camera cuts, scene replacement,
  absent inputs, stateless views, shared exposure sources, and device recovery.
- [ ] Define the distinct EV seed, Manual-to-Auto continuity, and immediate
  authored Manual/ManualCamera behavior in section 7.3, including request
  precedence, settings revisions, and same-frame ordering.
- [ ] Include a state diagram, frame-sequence diagram, CPU/HLSL contracts, and
  resource retirement rules. Reuse existing lifecycle/publication infrastructure.
- [ ] Resolve the single-writer/previous-generation sharing requirements in
  section 7.1. Define the state committed after recording, submission, and GPU
  completion, including retry/cancellation when a frame does not submit.
- [ ] Resolve histogram validity, counter capacity, zero target, and operational
  numeric bounds from sections 7.4 and 7.5. Record any bounded shader changes and
  compatibility effects here before implementation; expose remaining decisions
  rather than presenting this planning checklist as a completed LLD.
- [ ] Extend [multi-view composition](../lld/multi-view-composition.md) only at
  the identity/transition boundary. Keep exposure algorithm details in its owner.
- [ ] Reconcile [the exposure improvement plan](../lld/exposure-improvement-plan.md):
  identify this bounded package and its dependencies without making the full
  pre-exposure/parity program a hidden prerequisite or claiming it complete.
- [ ] Compare the lifecycle with the local UE5.7 references. Present concrete
  alternatives for approval if a required public behavior changes an accepted
  contract or has non-obvious product implications; resolve before coding it.

**Gate:** no unresolved implementation-significant lifecycle, invalid-state,
identity, or ordering decisions. Updating a counter alone is not reset execution.

### Slice 3 - Correct fixed-exposure propagation and prove it

- [ ] Add independent regressions at the pass/constant boundary for EV14/15/16,
  supported limits, compensation, nondefault keys, disabled exposure, and fallback.
- [ ] Validate finite representable exposure at the resolved configuration
  boundary. Audit intermediate overflow; evaluate the accepted equation in log
  space if needed without changing its mathematical meaning.
- [ ] Remove the undocumented fixed-exposure floor in `ExposurePass::Execute`.
  Carry the validated multiplier through the uploaded tonemap constants.
- [ ] Define error handling for invalid authored input; do not substitute another
  arbitrary epsilon or weaken valid authoring ranges to avoid the defect.
- [ ] Prove GPU output with controlled HDR inputs, tone curve None, gamma one,
  and unrelated effects disabled. Compare expected values before clipping.
  Use existing native pass fixtures and RenderDoc inspection for this gate;
  do not wait for the LightBench runner or use unqualified new instrumentation.

**Gate:** uploaded values and GPU results obey the exposure contract. At key
12.5/compensation zero, EV14 is `0.00006103515625`; EV15 and EV16 halve it
successively. Record the fixed fallback separately from successful auto mode.

### Slice 4 - Implement engine-owned initialization and transitions

- [ ] Add explicit per-view history validity and the transition request path
  specified in slice 2. Do not use DemoShell or a frame-global pending float as
  the owner of runtime history.
- [ ] Initialize an unseeded automatic view from its first valid histogram
  result, applying the specified clamps and bypassing adaptation for that event.
- [ ] Support explicit EV100 seeds through the shared luminance conversion;
  preserve a previous multiplier using the separate continuity equation in
  section 7.3. Test nondefault targets and simultaneous bias changes.
- [ ] Implement preserve-history and directional mode-transition behavior. Ordinary
  world updates and compatible viewport changes must not reset adaptation.
- [ ] Make requests generation-safe and single-consumption. Preserve pending
  requests across unavailable inputs; reject stale handles and obsolete events.
- [ ] Enforce owner-only writes and previous-generation reads for shared history.
  Prove source-before-consumer and consumer-before-source ordering produce the
  same exposure, including a reset while the source is temporarily unavailable.
- [ ] Implement GPU initialization/update ordering, frame-safe uploads, barriers,
  view removal, and fence-safe retirement. No blocking readback in the control loop.
- [ ] Wire DemoShell's `ResetAutoExposure(initial_ev)` as a client of the same
  public API used by games. Keep transition events out of persistent scene data.
- [ ] Prove behavior with a native view producer that has no DemoShell dependency.
  Exercise independent views, shared-source policy, view recreation, and frames
  in flight before relying on LightBench to demonstrate the feature.

**Gate:** initial automatic results do not depend on an inactive manual EV;
explicit seeds and transitions behave as specified; GPU history evidence is
associated with the correct logical view. Full pre-exposure parity is not implied.

### Slice 5 - Specify LightBench and its measurement boundary

- [ ] Create `design/renderer-core/lightbench.md` as the single experiment/design
  specification. This execution plan remains the work sequence; do not duplicate
  its checklist into another plan.
- [ ] Specify reference/explore behavior, experiment schema/versioning, complete
  configuration precedence, staged application, reset, and saved-experiment policy.
- [ ] Specify neutral-reference, fixed-exposure, auto-transition, point, and spot
  experiments with geometry, camera, units, material contract, measurement regions,
  independent equations, tolerances, and expected capability requirements.
- [ ] Establish the unit chain for every absolute-lighting reference, including
  the neutral directional case: world metres, working RGB primaries/white point,
  luminance coefficients, authored light-unit conversion, receiver illuminance,
  outgoing luminance, and HDR buffer normalization. Label a renderer-convention
  result accordingly until this mapping justifies an absolute cd/m2 claim.
- [ ] Use the supported production BRDF for initial reference calculations.
  Current deferred G-buffer packing uses `kVortexDefaultSpecular`; roughness does
  not create a Lambertian material. Do not substitute `rho * E / pi` for the full
  material response or add a bench-only shader to force that expectation.
- [ ] Resolve the local-light unit/falloff contract before declaring point/spot
  experiments physically calibrated. Record any discrepancy as a blocking result;
  a required renderer correction needs an explicit scoped amendment.
- [ ] Define neutral-baseline exposure from independently expected luminance and
  the accepted exposure equation, not from observed GPU output or visual tuning.
- [ ] Define a versioned independent oracle and a per-experiment error budget:
  production BRDF contract and viewing direction, packed material/normal values,
  pixel-region integration, depth reconstruction, floating-point precision,
  histogram binning, and readback/output quantization. Freeze tolerances before
  collecting acceptance results; separate model discrepancies from numeric error.
- [ ] Extend [DiagnosticsService LLD](../lld/diagnostics-service.md) with the narrow
  reusable measurement request/result and asynchronous readback contract.
  Renderer results carry frame/view/source identity; LightBench adds experiment
  revision, expectation, and verdict. Diagnostics does not take over rendering.

**Gate:** a fresh implementer can calculate the expected result and identify
invalid samples before running the renderer. Missing capability is explicit.

### Slice 6 - Implement and qualify reusable measurements

- [ ] Reuse existing scene-product extraction and diagnostics infrastructure.
  Add only the bounded region statistics/exposure inspection needed by the bench.
- [ ] Capture measurements at the defined scene-linear and post-exposure stages;
  interpret any pre-exposed input using its published contract.
  The current tonemapper does not materialize a separate post-exposure texture.
  Identify a real extraction point or an opt-in GPU probe at the production
  operation. Label CPU-derived post-exposure values as derived, not measured;
  do not add an always-on intermediate target solely for the bench.
- [ ] Tag results with frame sequence, persistent view identity, source product,
  region, and validity. Retain the experiment-revision association at the consumer.
- [ ] Use bounded asynchronous readback and fence-safe lifetimes. Disabled
  measurement must introduce no measurement dispatches or readback allocations.
- [ ] Exclude overlays and contaminated edge/occlusion samples. Treat missing,
  stale, or insufficient data as invalid rather than zero or pass.
- [ ] Validate the instrument against controlled known GPU inputs before using
  it to judge lighting. Do not read a CPU prediction and call it a GPU measurement.

**Gate:** measured values and their frame/view identity are proven independently
of LightBench. Late results cannot be attached to a newer experiment.

### Slice 7 - Build deterministic experiments and the usable bench

- [ ] Implement the schema-validated experiment definitions and staged application.
  Publish a complete experiment at a frame boundary; reset only relevant history.
- [ ] Enforce the full settings-ownership policy from section 7.2. Verify scene
  activation and UI binding cannot reapply stale post-process/camera values after
  the experiment is applied, and rendering changes cannot silently bypass exposure.
- [ ] Make Neutral Reference the clean-start default: framed cards, a real neutral
  directional source published through the supported light-selection contract,
  fixed exposure, and explicitly controlled environment/post-processing.
- [ ] Place camera and key on the cards' visible side. Separate the presentation
  backdrop/labels from physical illumination and measured scene signal.
- [ ] Replace visibility-only presets with complete experiments. Point receivers
  face the light; spot receivers lie inside the intended cone; distances and cone
  footprints are visible when relevant. Keep exposure fixed in falloff experiments.
- [ ] Keep panel/window preferences separate from experiment state. Offer explicit
  saved-experiment loading; do not silently import old personal settings at startup.
- [ ] Migrate the indoor settings asset into a supported experiment or retire it
  through the documented compatibility policy. Do not delete user-owned settings.
- [ ] Present expected/measured values, effective exposure, and explicit verdicts:
  Pass, Fail, Modified, Invalid measurement, or Unsupported. Editing marks the
  reference modified; resetting restores every relevant input.
- [ ] Create `Examples/LightBench/README.md` with runnable instructions, controls,
  reset/save behavior, interpretation, supported experiments, and result locations.

**Gate:** clean startup is useful and readable; reset restores the same resolved
inputs; conflicting old settings do not change a reference experiment. Obtain
native visual evidence as well as numerical evidence.

### Slice 8 - Automate qualification and close only proven scope

- [ ] Add deterministic experiment selection and batch execution using the same
  definitions/controller as the interactive bench. Honor existing capture CLI.
- [ ] Add `tools/vortex/Run-LightBenchValidation.ps1` and a schema-validated result
  report, reusing existing proof utilities. Run each case in isolated state so it
  cannot persist changes into the user's demo settings.
- [ ] Implement a native game-facing lifecycle case using the public runtime API,
  independent of DemoShell; include it in the runner's acceptance suite.
- [ ] Record resolved parameters, experiment version/revision, build identity and
  dirty state, backend/device, shader identity, frames, tolerances, measurements,
  captures, and pass/fail/unsupported reasons.
- [ ] Run the test/capture matrix below, then update owning docs and status with
  exact results. Update indexes for the new design and operator guide.

**Gate:** no required case is silently skipped or promoted from Unsupported to
Pass. Numerical qualification, visual review, and lifecycle correctness each have
their own evidence. Remaining dependencies prevent the affected closure claim.

## 9. Test plan

Use independent expected values, not a second call to the production helper.

| Area | Minimum evidence |
| --- | --- |
| Fixed exposure | Valid range, EV14/15/16, compensation/key, disabled, invalid inputs, constants and GPU output |
| Automatic initialization | First valid retained histogram weight independent of manual EV, explicit seed, zero target/speed/delta time, valid black image, unavailable/nonfinite/empty input, counter-capacity boundary and 8K display, reset consumed once |
| Gameplay lifecycle | Room-to-outdoor adaptation, explicit camera-cut policies, Manual-to-Auto multiplier continuity with changed target/bias, immediate authored Manual/ManualCamera, streaming and compatible-resolution preservation |
| View lifetime | Independent views, owner-only writes, both source/consumer orders, missing source history, recreated handle, removed view, stateless policy, multiple frames in flight |
| Measurements | Known GPU signal, excluded pixels, no valid samples, stale result, changed experiment, disabled cost |
| Experiments | Schema rejection, complete reset, settings isolation, BRDF oracle, local-light geometry validity, repeatability |

Derive lifecycle-test time expectations from the documented current exponential
recurrence, including its direction selection and clamping. EV/s UI labels are
not an independent temporal oracle. Hybrid adaptation remains in its owning plan.

### 9.1 Numerical proof must survive the actual output path

`Tonemap.hlsl` applies Bayer dithering even with tone curve None and gamma one.
The output format adds quantization. Do not demand exact undithered linear
values from an 8-bit screenshot or silently change production output to make
the assertion pass.

- Use constant scene-linear RGB 4096 for the EV14/15/16 fixed-exposure cases;
  expected values before dithering are 0.25, 0.125, and 0.0625 at key 12.5 and
  compensation zero. These remain comfortably away from clipping and black.
- Inspect the uploaded scalar separately. For stored output, independently
  include the pixel's Bayer term and render-target encoding/rounding, or state
  a bounded tolerance derived from those operations.
- Do not judge very small multipliers through an output whose least significant
  step exceeds the expected signal. Use an appropriate float product/probe and
  independently qualified readback for boundary cases.
- Record numeric precision, G-buffer/material quantization, filtering, and sample
  exclusions in the reference error budget before accepting images. Relative
  error alone is unsuitable near zero; define an absolute floor as well.

### 9.2 Reproducible timing and capture boundaries

For adaptation tests, a frame-rate cap is not a fixed simulation timestep.
Supply controlled deltas through the existing runtime frame contract and record
each delta actually consumed by the exposure pass. Run multiple delta schedules
and compare at equal elapsed time. Define convergence tolerance and maximum
settling time; a fixed warmup-frame count alone is not a settling criterion.
Freeze or explicitly script camera motion, random seeds, asynchronous asset
readiness, and feature settings. A frame rejected for invalid measurement must
not count as a successful checkpoint.

Existing build targets/filters below were checked against source/generated build
metadata. Run from `projects/Oxygen.Engine`; confirm configuration remains current
at execution time. These are future verification commands, not executed evidence.

```powershell
cmake --build out/build-ninja --config Debug --target Oxygen.Vortex.PostProcessService Oxygen.Vortex.SceneRendererDeferredCore --parallel 4
ctest --preset test-debug -R 'Oxygen\.Vortex\.(PostProcessService|SceneRendererDeferredCore)' --output-on-failure
cmake --build out/build-ninja --config Debug --target oxygen-vortex oxygen-graphics-direct3d12 oxygen-examples-lightbench oxygen-examples-multiview --parallel 4
```

When shaders or CPU/HLSL payloads change, build the backend through its normal
ShaderBake dependencies and run the catalog check:

```powershell
cmake --build out/build-ninja --config Debug --target Oxygen.Graphics.Direct3D12.ShaderBakeCatalog.Tests --parallel 4
ctest --preset test-debug -R 'Oxygen\.Graphics\.Direct3D12\.ShaderBakeCatalog' --output-on-failure
```

Add focused tests to the existing composition/lifecycle and DemoShell suites as
their boundaries change. Register bench/schema tests during slice 7. Record the
exact new build targets and CTest filters here before running their gates; do not
invent currently nonexistent target names or execute stale test binaries.

## 10. Runtime and capture proof

Run on native D3D12 with the debug layer and real shader artifacts. A headless
fake backend proves API behavior, not displayed lighting or GPU adaptation.

Required captures/observations:

1. Fixed EV14/15/16: uploaded scalar and unclipped output for known HDR input.
2. Automatic startup: histogram input, initial state, first solve, and tonemap
   consumption for different inactive manual EVs.
3. Seed/reset and camera transitions: exposure state over time, request identity,
   no repeated resets, and no influence on another view.
4. Neutral reference: valid regions, scene-linear measurements, effective
   exposure, final image, and clean-start/reset equivalence.
5. Point/spot: receiver orientation, cone coverage, actual light contract and
   measured results; document physical-calibration failures honestly.

An existing multiview regression command is:

```powershell
./tools/vortex/Run-VortexMultiViewValidation.ps1 -Output out/build-ninja/analysis/vortex/exposure-lightbench/multiview -Frame 5 -RunFrames 65 -Fps 30 -BuildJobs 4
```

It proves the existing multiview suite, not the new lifecycle scenarios by itself.
The following is the required interface of the runner to create in slice 8; it
is not an existing command:

```powershell
./tools/vortex/Run-LightBenchValidation.ps1 -Suite acceptance -Output out/build-ninja/analysis/vortex/exposure-lightbench/acceptance
```

The runner must retain underlying launch arguments and use existing capture
options (`--capture-provider`, `--capture-load`, `--capture-output`,
`--capture-from-frame`, `--capture-frame-count`). Capture first-frame and steady
states deliberately; a single late screenshot does not validate initialization.

## 11. Exit gate

- [ ] Current exposure docs agree at their declared scopes.
- [ ] Fixed exposure is proven through actual GPU consumption.
- [ ] Automatic lifecycle is proven with a game-facing client and independent views.
- [ ] LightBench has complete, versioned, resettable reference experiments.
- [ ] Measurement instrumentation is independently qualified and frame-correct.
- [ ] Required numerical, visual, debug-layer, shader, and regression gates pass.
- [ ] UE5.7 correspondence and any accepted divergences are recorded at bounded scope.
- [ ] Owning design, operational README, and implementation status match evidence.

## 12. Replan triggers

Stop the affected implementation slice and amend its owning design and this plan
when an accepted contract must change, a required renderer capability is absent,
the independent reference cannot be justified, or the public lifecycle behavior
needs a product decision. Present concrete options for non-obvious decisions.

Do not absorb photometric, BRDF, pre-exposure, or editor redesign work silently.
Do not lower tolerances, add compensating lights, or introduce special shaders to
make an unsupported renderer path pass. Record dependencies and continue only
independent, already scoped work.

## 13. Status and documentation updates

- Maintain slice status and artifact pointers in this plan during execution.
- Record verified implementation and outstanding deltas in
  [IMPLEMENTATION_STATUS.md](../IMPLEMENTATION_STATUS.md); preserve prior evidence.
- Keep [PLAN.md](../PLAN.md) and the existing exposure improvement plan linked to
  the same bounded work package. Do not invent a milestone number.
- Update `ARCHITECTURE.md` or `init-views.md` only if their actual ownership or
  publication contracts change. The proposed owners already match the architecture.
- New durable documents are the LightBench specification and operator README.
  This user-requested execution plan replaces the need for a separate bench plan.
- Generated captures/reports belong under the analysis output directory, not in
  new hand-maintained narrative documents for every run.

## 14. Specialized review and disposition

On 2026-09-16, a rendering-architecture subagent reviewed this plan against Oxygen
source, owning documents, local UE5.7 renderer/shader source, and Microsoft's DXC
denormal specification. The primary agent independently reviewed sequencing,
settings activation, shared history, and proof instrumentation.

| Finding | Plan correction |
| --- | --- |
| EV seeding differs from multiplier continuity; blanket continuity conflicts with authored Manual | Section 7.3 and directional transition tests |
| Shared-handle updates can have multiple writers | Section 7.1 owner-only writes, previous-generation reads, and schedule-permutation proof |
| First-valid solve, reset acknowledgment, and counter capacity were underspecified | Section 7.4 and GPU request-consumption/capacity tests |
| Existing authoring permits zero auto target | Section 7.5 preserves the accepted input and requires explicit runtime reconciliation |
| CPU float representability is insufficient for GPU guarantees | Section 7.5 operational/compiler/target domains and section 9.1 precision-aware proof |
| BRDF conformance alone does not establish physical calibration | Section 6 and slice 5 unit-chain and independent-oracle gates |
| New measurement tooling came after earlier GPU proof gates | Existing native fixtures/captures explicitly qualify slices 3 and 4 |
| Scene activation can overwrite experiment post-process settings | Section 7.2 and slice 7 full settings-precedence verification |
| Dither, output quantization, and nonmaterialized post-exposure values affect proof | Slice 6 and section 9.1 measurement-domain requirements |
| Frame-rate caps do not establish deterministic adaptation time | Section 9.2 controlled deltas and convergence criteria |

The review found no basis to replace Oxygen's accepted exposure convention.
It requires honest documentation of its unit mapping and deliberate differences
from UE behavior. Implementation-significant policies listed in slice 2 remain
design work to complete before implementation, not silently resolved decisions.

The same subagent re-reviewed the revised plan and confirmed that the six
implementation-significant findings and the adaptation-rate discrepancy were
addressed, with no remaining actionable blockers in that bounded document review.
This confirms the review disposition, not completion of the pending LLD decisions
or runtime qualification.

Plan preparation and review performed source/document inspection and command-target
checks. No implementation, build, test execution, or runtime qualification is claimed.
