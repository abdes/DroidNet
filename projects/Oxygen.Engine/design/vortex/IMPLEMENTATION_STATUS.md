# Vortex Implementation Status

Status: `active milestone ledger`

This file records milestone status and the item-level exposure delivery tracker
in section 3. It is not a commit log. Update each existing item in place with
its implementation evidence, validation evidence, and remaining work.

## 1. Ledger Rules

1. Do not add per-commit evidence rows.
2. Update the existing milestone row when work advances.
3. Keep evidence concise: implementation files/areas, validation commands or
   artifact names, and the remaining exit-gate gap.
4. Do not mark a milestone `validated` unless implementation exists, required
   docs/plans are current, and validation evidence is stated in the row.
5. If scope changes, update the design document and detailed milestone plan
   before claiming progress.
6. For exposure delivery, update **Current work** before starting the next item.
   At each implementation checkpoint, update the affected work-item statuses,
   evidence and remaining gate. A validated component does not close its slice.
7. Batch coherent implementation work and use focused Debug tests between
   checkpoints. Run the broader owning Debug gate for item closure; reserve
   Release validation for slice closure, as requested on 2026-09-18.
8. Keep a TODO at each deferred exposure code boundary, naming the owning item
   or plan and the concrete work required before activating that path.
9. For subsequent broad native gates, freeze the checkpoint and binary/shader/
   fixture hashes, then delegate the specified test run to one validation
   subagent. Its mandate is test execution and reporting only: no edits, builds
   or scope expansion. Continue source implementation, review and documentation
   while it runs; defer only builds, runtime-input changes and competing GPU work.
   Reconcile its result before committing.
   Keep short focused checks local and do not restart an already-running gate.

## 2. Status Vocabulary

| Status | Meaning |
| --- | --- |
| `validated` | Implementation exists, docs/status are current, and fresh validation evidence is recorded here. |
| `landed_needs_validation` | Substantial code exists, but this ledger does not contain fresh closure proof. |
| `in_progress` | Implementation exists but known gaps remain, or scope is actively being corrected. |
| `planned` | Scope is defined; implementation has not started for this milestone. |
| `blocked` | Required design, dependency, or proof surface is missing. |
| `future` | Explicitly deferred beyond the production-complete desktop deferred baseline. |

## 3. Exposure delivery status

The [implementation plan](plan/exposure-and-lightbench-correction.md) owns the
requirements, delivery order and acceptance gates for all ten slices. The summary
table and item-level tracker below are the current progress record. Normative equations, layouts and lifetime rules
remain in their owning LLDs; detailed commands, results and historical checkpoints
remain in the linked local manifests and Git history.

FP32 SceneColor accumulation in both modes is approved and required by the
[allocation contract](lld/scene-textures.md#per-view-fp16-suitability).
Production per-view FP16 admission is enabled and Debug-qualified by EX05-17.
Complete LightBench, MultiView and Slice 5 acceptance remain required; the package
is not complete.

| Slice | Status | Current boundary / remaining gate | Evidence |
| --- | --- | --- | --- |
| 1 — Contracts | validated | Numeric domain, error budgets, layouts and HDR inventory have designated owners. | [Contract checkpoint](plan/exposure-contract-checkpoint.md) |
| 2 — Settings and fixed exposure | validated | Canonical authored input, immutable pass snapshots, fixed/camera gain, per-view settings and public mask acceptance are qualified. | [Fixed gain](../../out/build-ninja/analysis/vortex/exposure-lightbench/fixed-gain/evidence-manifest.json), [frame bindings](../../out/build-ninja/analysis/vortex/exposure-lightbench/frame-binding/evidence-manifest.json), [configuration and mask acceptance](../../out/build-ninja/analysis/vortex/exposure-lightbench/lifecycle/review-r028-manifest.json) |
| 3 — Metering and adaptation | validated | Controlled-input histogram, curve, masks and hybrid adaptation; scene acceptance remains slice 5. | [Metering](../../out/build-ninja/analysis/vortex/exposure-lightbench/metering/evidence-manifest.json) |
| 4 — GPU lifecycle and sharing | validated | Controlled-input/public-event gate, including offscreen routing. Real-scene resource lifetime is tracked in slice 5. | [Lifecycle](../../out/build-ninja/analysis/vortex/exposure-lightbench/lifecycle/discontinuity-manifest.json), [offscreen sharing](../../out/build-ninja/analysis/vortex/exposure-lightbench/lifecycle/offscreen-sharing-manifest.json) |
| 5 — HDR migration and recovery | in_progress | P domains, cumulative error bounds, producer qualification, production format admission and queued mixed-format consumers are validated. Automatic recovery/retention/return is Debug-qualified; remaining scene/MultiView acceptance is open; allocation/transfer costs are Debug-qualified. | [Detailed items](#32-slice-5-work-items) |
| 6 — Authoring and persistence | planned | Native physical-camera persistence and texture-resource-index mask contracts are approved; complete source/cook/load/script/editor/DemoShell round-trip remains. | [Detailed items](#33-slice-6-work-items) |
| 7 — Light units | planned | Directional, point and spot numerical/visual calibration across forward and deferred paths remains. | [Detailed items](#34-slice-7-work-items) |
| 8 — Measurements | planned | Implement instrumentation and qualify it against independent inputs. | [Detailed items](#35-slice-8-work-items) |
| 9 — LightBench and MultiView | planned | Complete all seven experiments and the full native layout/interaction matrix; current MultiView proofs are partial. | [Detailed items](#36-slice-9-work-items) |
| 10 — Automation and acceptance | planned | Run the same experiments through automation, close every acceptance gate and reconcile owner documents. | [Detailed items](#37-slice-10-work-items) |

### 3.1 Current work

- **Completed: EX05-15 — validated.** Current/prospective store errors,
  hardware-filtering bounds, retained temporal uncertainty, sky/AP/fog and
  translucent composition, coverage/depth and final image/meter tolerances are
  integrated. Prepare the scene certificate before checked conversion; finalize
  eligibility afterward so the current conversion verdict participates.
  GPU status is 384 bytes; the completed CPU prefix remains 80 bytes.
- **Final evidence:** all **330 owning tests pass per configuration** in Debug
  and Release: 168 native exposure, 24 post-process, 63 environment, 63 deferred
  scene and 12 offscreen tests. All five prior failures pass without weakening
  accepted-half, prior-gain, headroom or retirement assertions. Both builds pass
  308,016 exact consumer-arithmetic checks. Final Release Fog/Local captures pass
  8,192 exact transfer checks with actual source maxima and per-view S/P checks.
  The four-phase visual cycle preserves colors in object-specific regions and
  restores Clear exactly; twelve negative controls protect the checker.
- **Fog corrections:** fixed history coordinates and jitter-dependent integration
  endpoints. Local-fog constant descriptors now survive all queued views until
  frame-slot retirement. That independent fix is committed as `969cbfdb7`, and
  the user confirmed that the flicker is fixed.
- **Measured cost:** six native timing cases cover 1080p/4K in both builds.
  Release scene replay measures 4.145472 ms (Fog) and 2.546688 ms (Local) for
  explicit qualification/finalization dispatches across 2560x1400 main and
  1152x630 PiP. Embedded checks remain in their rendering-pass costs; these
  sums are not end-to-end frame latency.
  [Completion manifest, hashes, commands and reports](../../out/build-ninja/analysis/vortex/exposure-lightbench/lifecycle/consumer-completion-manifest.json).
- **Completed: EX05-16 — validated in Debug.** Producer-domain/pre-store guards
  and the active producer range matrix are implemented.
  Audit the frozen [2^-24,2^32] scene-radiance envelope at the existing write
  boundaries, including cumulative FP32 SceneColor, sky/AP/fog, forward/deferred
  lighting and canonical environment consumers. Preserve insignificant values
  according to the existing image/meter budgets; do not introduce a per-component
  floor. Extend the existing bounded status for unsupported FP32 radiance and
  qualify bright/dark endpoints plus narrowing failures as one producer matrix.
  EX05-15 and EX05-16 are closed. Release qualification of this final increment
  remains part of the Slice 5 closure gate under the agreed validation cadence.
  **Validated material/cooking increment:** typed material samples now preserve
  HDR radiance and decode sRGB exactly once. Forward shading publishes color/depth
  for HDR resolve while leaving GBuffer bindings absent. The 324-case native
  cooker-through-renderer matrix passes in Debug and Release: emission and unlit
  color across deferred/forward, opaque/masked/partial-alpha paths, typed float/
  UNORM/sRGB textures, P endpoints and explicit unsupported range reporting.
  The matrix isolates material color; the lighting matrix below covers direct
  and indirect contributions separately.
  **User-approved import contract implemented:** `mip_filter_space` is removed
  from API, schemas, CLI, presets and recipes. Color mips use linear FP32 followed
  by final output encoding/quantization. Source encoding, output format and kernel
  remain explicit. Independent tests preserve alpha and dark multi-level mips.
  All 691 owning tests pass per configuration: 340 Vortex, 345 cooker and 6
  DemoShell SkyboxService tests. ImportTool, MultiView and TexturedCube build in
  both configurations. Independently parsed standalone cooked assets preserve
  sRGB byte128 as approximately 0.2158605 linear and HDR 2^32 exactly.
  [Material increment, commands, cases and hashes](../../out/build-ninja/analysis/vortex/exposure-lightbench/lifecycle/material-domain-increment-manifest.json).
  **Validated local-injection correction:** local extinction-weighted source
  coefficients are divided by total extinction before Beer-Lambert opacity.
  This preserves thin-medium signals and the height-only limit. All 27 authored
  upload / production-shader / output cases pass in Debug and Release, including
  mixed media, emission/scattering, P endpoints and product-10 range failures.
  Both builds pass 36 affected native tests and all 63 environment tests.
  A targeted Release capture verifies RGB 0.00102278 at P=1, where the old shader
  produced 5.88e-16. These centered-froxel controls isolate source integration.
  [Injection evidence, oracle, capture and hashes](../../out/build-ninja/analysis/vortex/exposure-lightbench/lifecycle/injection-increment-manifest.json).
  **Completed lighting qualification:** 324 directional/point/spot cases and
  180 static diffuse-SH cases cover deferred/forward, opaque/masked/partial-alpha,
  P endpoints and unsupported output. Ten distant-sky linearity/additivity cases
  and 144 analytic primary/secondary sky cases cover canonical transport.
  Removed the shared secondary-light illuminance cutoff that discarded small
  contributions before exposure. A targeted Debug capture verifies all 192
  zenith texels and the pinned P=2^32 binding against the independent oracle.
  **Visible source checks:** material emission, individual direct lights and
  indirect terms are checked before cancellation can hide an unsupported source.
  Forty source-cancellation/zero-coverage cases retain valid Auto history with
  nonzero elapsed time; forty visibility cases cover both actual raster orders,
  hidden/revealed surfaces, masked holes and complete/disabled prepasses.
  Without a complete prepass, dedicated validation entries replay existing draws
  against read-only finished depth with no color outputs. Deferred replay omits
  GBuffer/velocity work; forward retains necessary per-source lighting while
  omitting final composition/AP. No additional texture is allocated. Stage 10
  now publishes base-pass depth even without Stage 3, preserving deferred lights.
  **Validation:** the owning Debug gate passes 184 native exposure, 24 post-process,
  63 environment, 63 deferred-scene, 12 offscreen and 4 shader-catalog tests (350).
  After the final no-prepass publication correction, all 63 deferred-scene tests
  and both focused native tests (80 cases) pass again. All six compiled replay
  variants have zero color-output instructions. The earlier 344-test Release
  gate predates these final source checks and is not their validation evidence.
  [Lighting evidence, exact commands, capture and hashes](../../out/build-ninja/analysis/vortex/exposure-lightbench/lifecycle/lighting-producer-increment-manifest.json).
  **Completed: EX05-17 — validated in Debug.** Production format selection now
  precedes HDR writes, using the current layout and a matching completed
  candidate. SceneColor remains FP32; qualified sky/AP/fog and resolved color use
  FP16. Checked conversion and tonemap share the report and retained FP32 source.
  Extraction ownership retains textures/descriptors/families until consumer
  release, and pool leases no longer call a destroyed pool on release.
  **Evidence:** all 382 owning Debug tests pass: 188 native exposure, 24
  post-process, 63 environment, 63 deferred scene, 21 publication, 12 offscreen,
  7 lease-pool and 4 shader-catalog tests. Native checks cover eight AP resize/view
  cases, stable half sky/AP/fog qualification, producer-store failure and return,
  borrower source/pending-event invalidation, uninitialized-source fallback,
  settings invalidation, retained contents and conversion-submission failure.
  The layout fixture uses vacuum/zero extinction to isolate exact transfer; it
  does not close nonzero-environment/full recovery acceptance. The inspected
  native capture verifies P=32768, S=0.25, accepted half conversion, the FP32
  fallback binding and final RGB 0.06053922 against 0.0605392157 including dither.
  [Admission evidence, commands, capture and hashes](../../out/build-ninja/analysis/vortex/exposure-lightbench/lifecycle/admission-increment-manifest.json).
  **Completed: EX05-18 — validated in Debug.** The three-view native family
  checks 18 mapped outputs across alternating submission order and mixed-format
  exposure changes. Delayed tonemap consumers retain accepted half, rejected-half
  fallback and full HDR inputs through frame-slot reuse and view removal, releasing
  extraction owners before the consumer fence completes. A black sentinel in the
  rejected half destination distinguishes bright FP32 fallback from incorrect half
  sampling; an unconditional control reads black. The frozen native gate passes
  all 190 tests with all 35 runtime hashes unchanged. The later test-only sentinel
  enhancement passes its focused Debug test (five queued consumers).
  [Queued-consumer evidence and commands](../../out/build-ninja/analysis/vortex/exposure-lightbench/lifecycle/queued-consumer-manifest.json).
  **Completed: EX05-19 — validated in Debug.** Matching normal-mode producer
  failure issues one renderer-owned Auto Remeter. Conversion-only rejection
  invalidates precision while preserving the valid FP32 meter, adaptation and
  generation. Fixed/disabled/borrowed failures do not reset numerical authority;
  newer explicit requests win. Five owner/control cases retain history through
  six unsupported FP32 frames, then repair and return. The actual 46-stop scene
  preserves both endpoints, continues adaptation for eight frames and returns
  after range reduction; sixteen alternating shared-consumer renders retain
  independent format suitability. Render-context timing is explicitly checked.
  **Evidence:** the frozen Debug gate passes all 238 tests (193 native exposure,
  24 post-process, 21 publication), including the final explicit-Preserve control.
  All 37 runtime hashes match before/after. The existing source-cancellation test
  also passes after correcting its fixture to use the owning frame's actual dt;
  its earlier claim of nonzero elapsed time was not established by the old fixture.
  [Recovery evidence and exact commands](../../out/build-ninja/analysis/vortex/exposure-lightbench/lifecycle/recovery-increment-manifest.json).
  **Completed: EX05-09 — validated in Debug.** Disabling bloom now prevents
  an external SRV and nonzero intensity from activating its tonemap contribution.
  The native regression failed before the fix (six cases, 18 RGB checks: .25
  instead of .125) and passes all 24 P/format/toggle/intensity cases afterward.
  Nine checked-color cases include three external-bloom accepted-half/fallback
  combinations. The frozen gate passes 194 native exposure and 24 post-process
  tests (218), with all 36 runtime hashes unchanged. Caller extent/P/lifetime
  requirements are documented; shader, temporal-history and captured-sky TODOs
  identify the deferred activation work. No owned bloom filter or TAA/TSR/capture
  producer is claimed. [Audit, commands and evidence](../../out/build-ninja/analysis/vortex/exposure-lightbench/lifecycle/bloom-handoff-manifest.json).
  **Completed: EX05-21 — validated in Debug.** Four native 1080p/4K ×
  temporal-off/on cases pass with 35 unchanged frozen runtime hashes. Weak native
  resource tracking counts each committed texture once, records peaks at creation,
  and separates raw bytes, placement bytes, retained leases and cached families.
  With two views, qualified-FP16 HDR footprints are 229.328/827.328 MiB at the
  steady checkpoint (1080p/4K main); peaks are 294.953/1075.828 MiB. Temporal-fog
  controls conservatively retain FP32: steady 374.453/1375.641 MiB, peak
  384.016/1410.578 MiB. These controls vary temporal work and format together.
  Four inspected captures verify actual formats, S/P, final pixels, logical
  primary reads and sky/AP/fog/resolve/tonemap writes. Warm Debug event durations
  are recorded separately from logical bytes and physical DRAM bandwidth.
  The report excludes caller outputs, buffers, heaps and unrelated backend
  allocations; Release performance remains at Slice 5 closure.
  [Allocation/traffic/timing evidence and commands](../../out/build-ninja/analysis/vortex/exposure-lightbench/lifecycle/accounting-manifest.json).
  **Next: EX05-22.** Close the remaining scene startup/cut/seed/mode/pause
  combinations, followed by the other open lifecycle and MultiView gates.
  **Deferred feature boundaries:** owned bloom, temporal color and specular/
  captured-sky products retain their source TODOs and feature dependency issues.
  **Domain implementation:** failure kind 32 distinguishes unsupported
  scene RGB from FP16 headroom. The limit is scaled exactly by pinned P, avoiding
  reciprocal rounding at the upper endpoint; pre-environment
  and final SceneColor scans preserve separate opaque inputs and reject invalid
  metering. Submission failure invalidates only the existing precision epoch.
  **Validated environment increment:** the complete affected gate passes in both
  Debug and Release: 177 native exposure, 24 post-process, 63 environment, 63
  deferred-scene and 12 offscreen tests (339 per build). This includes 551 range
  boundary cases, 132 continuous-integral cases, actual canonical and sky LUT producers,
  hardware sampling, authored local-fog upload/decoding/composition, 48 local-fog
  integral/injection cases, final-scan failure/retry and retained Auto history.
  The local-fog draw asserts product 9/kind 32 for an explicit unsupported case;
  the renderer asserts product 7 for invalid sky and product 10 for invalid fog.
  [Commands, results, memory measurements and inspected native capture](../../out/build-ninja/analysis/vortex/exposure-lightbench/lifecycle/domain-producer-increment-manifest.json).
  **Canonical-format correction approved:** the user approved FP32 for the same
  shared transmittance and unit-illuminance multiple-scattering tables. A transfer
  of 1e-12 rounds to zero in FP16, but multiplying by a supported 1.88e9-nit
  source should produce 0.00188, inside the supported scene domain and beyond
  the allowed error if lost. The independent control is
  `lifecycle/domain-canonical-half-control.json`. Allocation/recreation and both
  descriptor formats now use RGBA32F; per-view FP16/FP32 selection is unchanged.
  Default-size device placement measures 278,528 bytes for FP32 versus 139,264
  for equivalent FP16 resources: +136 KiB on this adapter per retained cache
  generation. Switching view modes reuses one canonical generation. Actual LUT
  producer and sampling controls pass in both builds; the complete active producer
  matrix is Debug-qualified by EX05-16.
  **Implemented upstream corrections:** retain local-fog extinction/falloff and
  emission in FP32 within the existing instance buffer (64 bytes, formerly 48).
  Preserve signed height integrals without overflowing a downward-ray intermediate.
  Use the continuous atmosphere scattering integral at zero/tiny extinction,
  eliminating the energy loss from the former 1e-9 denominator floor. Sky,
  height-fog, local-fog and translucent sources report range failures before
  sanitization or attenuation can hide them; volumetric samples are checked before
  their nonnegative clamp and temporal filtering.
  **Measured GPU cost:** warmed Release replay on RTX 3080 measures the final
  SceneColor scan at 0.081152 ms (1080p) and 0.263264 ms (4K). Default FP32 LUT
  refresh dispatches take 0.008912 ms (transmittance) and 0.020768 ms (multiple
  scattering). These are dispatch medians, not end-to-end frame latency or a
  measured FP16-to-FP32 timing delta; native queue intervals are also recorded.
  **Remaining Slice 5 work:** remaining scene lifecycle combinations and
  complete native MultiView acceptance, plus Release closure qualification.
  EX05-19 closes the named recovery/retention/return matrix, not these other gates.
  The Slice 5 gate and Slices 6–10 remain open.

- **Open visual report: all-white MultiView meshes.** The user observed all
  meshes rendered white in test windows. Reinspection of
  `multiview/atmosphere-Debug-42.exposure.png` confirms an earlier captured case
  with washed-out meshes and ground; the exact run the user saw is unidentified.
  This is failed material/readability evidence. The later
  `multiview/opaque-ap-Release-58.exposure.png` has readable colored cards, but
  uses a different, isolated emissive fixture and does not close the original
  lit-scene report. The bounded original-material diagnosis/profile comparison
  below is now qualified; complete EX05-04/29 acceptance remains open.
- **Qualified investigation: EX05-04/29 — captured lit-scene washout.** The
  ordinary Debug main/lit-PiP run retains material colors. The earlier atmosphere
  fixture and the new sunlit original-mesh comparison have separate diagnoses;
  the emissive-card result is not substitute evidence for either.
  **Verified diagnosis of the archived all-white capture:** GPU frame 43 of
  `atmosphere-Debug-42` uses Manual mode (solve mode 0), fixed/current/target gain
  0.25 and a 110,000-lux directional light. Geometry is finite FP32; its minimum
  exposed RGB component is 9.556 in main / 6.260 in PiP. Independent full-geometry
  ACES/gamma/dither evaluation reproduces output within 0.522 byte codes.
  Near-white geometry fractions are 100% / 99.456%. This is display washout from
  that lighting/manual-exposure combination, not unfinished AE. The base-pass
  emissive values are already uniform (0.720215), so this archived fixture also
  cannot establish material identity. [Diagnosis](../../out/build-ninja/analysis/vortex/exposure-lightbench/multiview/white-report-old-diagnosis.json).
  **Average remains a failed readability control.** `atmosphere-lit` keeps
  original non-emissive materials and Auto exposure. Average's <1% per-material
  near-white check fails in main; that threshold has not been relaxed. The
  earlier fixed-Average investigation's frames 43 and 133
  have identical images and current/target gains; remeter generation 1 is applied.
  Simulation is paused (GPU delta=0); the equality to target, not elapsed wall
  time, rules out pending adaptation in those captures. Independent histogram
  reduction reproduces main L=221.154 and PiP L=2529.377. Main contains 44.15%
  background samples (median L=3.22), versus 16.01% in PiP (median L=1.89);
  geometry medians are 4929 / 5144. The full-image trimmed geometric mean yields
  an 11.44-times higher main gain, causing its brighter mapped materials.
  [Settling check](../../out/build-ninja/analysis/vortex/exposure-lightbench/multiview/white-report-auto-settling.json),
  [meter diagnosis](../../out/build-ninja/analysis/vortex/exposure-lightbench/multiview/white-report-auto-meter-diagnosis.json).
  The user's exact observed run/frame is unidentified; these findings apply to
  the named captures. The ordinary current Debug run retains colored meshes at
  frame 97. The new controlled fixture passes in Debug and Release: changing
  Average to Spot and issuing the public remeter leaves HDR inputs byte-identical
  while all three colored materials pass in both views (zero near-white pixels,
  88.5–100% colored). Independent histogram reduction verifies the measured
  target; current/target gains match exactly after remeter. Four paired captures,
  four inspected composites and synthetic white rejection controls pass. Two
  additional AP captures preserve eight baseline view images exactly. The
  debugger reports only the accepted factory warning. This is a configured
  profile comparison, not a production AE change or an Average readability pass.
  [Evidence and commands](../../out/build-ninja/analysis/vortex/exposure-lightbench/multiview/lit-profiles-manifest.json).
- **Qualified AP composition prerequisite.** The `atmosphere` proof
  stages a complete backlit-card recipe before publication, retains scene-owned
  sun settings and uses actual deferred, alpha-one forward and mixed rendering.
  Debug and Release each compare all 4,309,760 HDR pixels across main/PiP with
  zero out-of-budget pixels, maximum RGB error `4.77e-7` and exact alpha agreement.
  All phases have measured AP contribution, zero near-white fraction and readable
  colors. Actual bindings and all six composites were inspected. The debugger
  reports only the accepted live-factory graphics warning; the ordinary MultiView
  baseline remains exact in four view comparisons. [Evidence](../../out/build-ninja/analysis/vortex/exposure-lightbench/multiview/atmosphere-composition-manifest.json).
- **Source-value corrections:** `6267aa93f` preserves distinct emissive material
  identity and correct half subnormal decoding/rounding, including R038. Debug
  and Release each pass 147 Data, 34 MaterialBinder and 146 native exposure tests
  (327 each), with failing-before regressions. [Evidence](../../out/build-ninja/analysis/vortex/exposure-lightbench/multiview/source-values-manifest.json).
- **Remaining boundaries:** the AP prerequisite uses an isolated emissive-card
  fixture. Physical-light calibration, all material families, ordinary AE
  readability and all MultiView layouts retain their separate acceptance rows.
  Consumer error propagation is closed in EX05-15; production format switching
  is Debug-qualified in EX05-17, queued consumers in EX05-18 and automatic
  recovery/retention/return in EX05-19. The other Slice 5 gates remain open.
- **Delivery order:** finish Slice 5's gate before starting Slice 6 integration.
  Slices 6–10 remain required; their detailed items are below.

**How to read the items:** `validated` closes only the named item's stated scope;
`in_progress` means partial work or active investigation, with the gap stated;
`planned` means that delivery item has not started. Existing prerequisites are
identified explicitly and do not close a later slice. A slice closes only when
its gate row is validated. Manifests describe their checkpoint; these rows own
current status, including decisions that supersede older manifest limitations.

### 3.2 Slice 5 work items

[Requirements and gate](plan/exposure-and-lightbench-correction.md#slice-5---complete-pre-exposure-migration-and-numerical-recovery).
**Slice status: in_progress. Gate: not passed.**

| ID | Work item | Status | Completed scope / remaining work | Evidence |
| --- | --- | --- | --- | --- |
| EX05-01 | Early GPU P resolve and immutable frame P/1P binding | validated | GPU-owned P, pinned frame/state leases, submission failure and repeated-resolve behavior are implemented. | [Frame resolve](../../out/build-ninja/analysis/vortex/exposure-lightbench/lifecycle/frame-resolve-manifest.json), [domain](../../out/build-ninja/analysis/vortex/exposure-lightbench/lifecycle/domain-manifest.json) |
| EX05-02 | FP32 SceneColor accumulation and HDR format plumbing | validated | Approved FP32 accumulation contract and format-aware resource/PSO plumbing exist. This is not automatic mode switching. | [Formats](../../out/build-ninja/analysis/vortex/exposure-lightbench/lifecycle/formats-manifest.json), [allocation contract](lld/scene-textures.md#per-view-fp16-suitability) |
| EX05-03 | Meter with 1/P; consume S/P in all exposure modes | validated | Unified gain consumption covers Manual, ManualCamera, Auto, disabled and zero-target behavior in the qualified fixtures. | [Domain](../../out/build-ninja/analysis/vortex/exposure-lightbench/lifecycle/domain-manifest.json), [mode fixtures](../../out/build-ninja/analysis/vortex/exposure-lightbench/multiview/modes-manifest.json) |
| EX05-04 | Deferred emissive/direct/indirect and forward lit/unlit/masked/translucent P domains | in_progress | **Source corrections:** emissive RGB was omitted from material-cache identity; shared half decoding halved subnormals and encoding dropped the carry into minimum normal (R038). All three source-value regressions are corrected; Debug/Release each pass 327 affected tests. This closes the named corrections only. The original non-emissive sunlit materials now have a qualified Average/Spot comparison in both views/builds; identical HDR inputs separate meter-target washout from adaptation. The 324-case cooked-material domain matrix now passes in both builds, including HDR/sRGB sampling and forward resolve publication. Direct/indirect producer endpoints are now qualified by the lighting matrix. Complete mixed-content acceptance remains. | [Cooked-material matrix](../../out/build-ninja/analysis/vortex/exposure-lightbench/lifecycle/material-domain-increment-manifest.json), [Source-value evidence](../../out/build-ninja/analysis/vortex/exposure-lightbench/multiview/source-values-manifest.json), [source regressions](../../src/Oxygen/Data/Test/HalfFloat_test.cpp), [material regression](../../src/Oxygen/Vortex/Test/Resources/MaterialBinder_basic_test.cpp), [scene migration scope](../../out/build-ninja/analysis/vortex/exposure-lightbench/scene/scene-migration-manifest.json), [producer inventory](lld/scene-textures.md#exposure-hdr-domain-and-format-inventory) |
| EX05-05 | Sky/background and sky-view/AP producer-consumer P plumbing | validated | Paired P-domain/binding proofs and R036 low/zero-opacity correction are qualified. The isolated AP fixture adds actual deferred/alpha-one-forward whole-HDR-image agreement and readable mixed presentation in both views/configurations. R040/R041 additionally qualify exactly-once opaque-forward AP and explicit shading routing, with exact full-image agreement. This does not close broader material families or quantization/mode switching (EX05-04/15–19). | [Scene migration](../../out/build-ninja/analysis/vortex/exposure-lightbench/scene/scene-migration-manifest.json), [real products](../../out/build-ninja/analysis/vortex/exposure-lightbench/lifecycle/required-products-manifest.json), [controlled AP correction](../../out/build-ninja/analysis/vortex/exposure-lightbench/lifecycle/review-r036-manifest.json), [native scene AP fixture](../../out/build-ninja/analysis/vortex/exposure-lightbench/multiview/atmosphere-composition-manifest.json), [opaque forward](../../out/build-ninja/analysis/vortex/exposure-lightbench/multiview/opaque-ap-manifest.json) |
| EX05-06 | Height/volumetric fog and RGB history rebasing | validated | Current/stored P conversion and unchanged transmittance are implemented and exercised. Cumulative temporal quantization is not covered by this item. | [Scene migration](../../out/build-ninja/analysis/vortex/exposure-lightbench/scene/scene-migration-manifest.json), [history/resource lifetime](../../out/build-ninja/analysis/vortex/exposure-lightbench/lifecycle/review-lifetime-manifest.json) |
| EX05-07 | Diagnostic colors, wireframe and display overlays | validated | Per-view unit-gain diagnostics, persistent exposure preservation and frame-retained overlay constants are qualified. Full feature-layout acceptance remains EX05-29. | [Diagnostics](../../out/build-ninja/analysis/vortex/exposure-lightbench/multiview/diagnostic-manifest.json) |
| EX05-08 | Canonical processed cubemap narrowing and upload packing | validated | Half/float resource choice, normalization and matching face/mip upload packing are qualified at the static-cubemap scope. | [Cubemap](../../out/build-ninja/analysis/vortex/exposure-lightbench/lifecycle/cubemap-manifest.json) |
| EX05-09 | Bloom and remaining temporal/capture domain audit | validated | Active external bloom handoff qualified: disabled bloom supplies no SRV; matching-P radiance is added before S/P, including checked-half/fallback scene inputs. Debug gate: 218 passed; 24 domain/toggle cases and nine checked-color cases. Owned bloom shaders are inactive UV placeholders, threshold has no active filter, and temporal/captured/specular producers remain feature dependencies with code TODOs. | [Audit and native evidence](../../out/build-ninja/analysis/vortex/exposure-lightbench/lifecycle/bloom-handoff-manifest.json), [consumer contract](lld/post-process-service.md#post-chain-and-qualification) |
| EX05-10 | Checked SceneColor conversion and conditional FP32 tonemap fallback | validated | Whole-image check precedes narrowing; rejected half contents are not sampled; current conversion verdict is separate from future-candidate qualification. Production admission and checked handoff are implemented in EX05-17; queued consumers are Debug-qualified in EX05-18. | [Conversion](../../out/build-ninja/analysis/vortex/exposure-lightbench/lifecycle/conversion-manifest.json), [selection](../../out/build-ninja/analysis/vortex/exposure-lightbench/lifecycle/selection-manifest.json), [separate reports](../../out/build-ninja/analysis/vortex/exposure-lightbench/lifecycle/precision-status-manifest.json) |
| EX05-11 | GPU per-view suitability and two-result stability | validated | Required-product identity, candidate P, own precision history for borrowers, streak reset and bounded status publication are qualified as primitives. | [Eligibility](../../out/build-ninja/analysis/vortex/exposure-lightbench/lifecycle/eligibility-manifest.json) |
| EX05-12 | Completed-status candidate selection and failure invalidation | validated | Lifetime/settings/layout/generation checks, bounded retry queue, stale-result rejection and failed-solve/fallback invalidation are qualified. Successful same-frame reuse retains acknowledgement. | [Status selection](../../out/build-ninja/analysis/vortex/exposure-lightbench/lifecycle/precision-status-manifest.json), [R034 correction](../../out/build-ninja/analysis/vortex/exposure-lightbench/lifecycle/review-r034-manifest.json) |
| EX05-13 | Collect real required scene-reference products | validated | Persistent scene views collect SceneColor, sky-view, AP and fog; missing producers remain missing requirements. Native capture checks 151,424 texels per view in the environment fixture. | [Required products](../../out/build-ninja/analysis/vortex/exposure-lightbench/lifecycle/required-products-manifest.json) |
| EX05-14 | Finite/overflow checks before environment stores | validated | Sky-view/AP/fog report original finite/nonfinite and FP16 headroom failures through the existing status. Auto does not adapt from a producer-failed image. This is not a quantization certificate. | [Pre-store range](../../out/build-ninja/analysis/vortex/exposure-lightbench/lifecycle/prestore-range-manifest.json) |
| EX05-15 | Quantization, cumulative image/meter error and temporal bounds | validated | Hardware-filtered current/candidate certificates cover producer stores, retained history, sky/AP/fog/translucent composition, coverage/depth and final image/meter tolerances. Preparation precedes checked conversion; eligibility follows it. GPU status384 / CPU prefix80; no additional HDR texture. Debug/Release each pass 330 owning tests and 308,016 exact arithmetic checks. Final Release Fog/Local audits pass 8,192 transfers; the four-phase visual/ROI cycle and 12 negative controls pass. User confirmed the local-fog lifetime correction. Controlled 1080p/4K and actual two-view scene costs are recorded. Production format switching and queued consumers are Debug-qualified in EX05-17–18. | [Completion evidence and commands](../../out/build-ninja/analysis/vortex/exposure-lightbench/lifecycle/consumer-completion-manifest.json), [Runtime contract](lld/post-process-service.md#quantization-error-propagation), [Per-view budgets](lld/scene-textures.md#per-view-fp16-suitability) |
| EX05-16 | Full supported-radiance envelope at upstream producers | validated | Active producer matrix and pre-composition source checks qualified in Debug. Final gate: 350 tests; after the no-prepass depth-publication correction, 65 affected tests pass (80 native cases). Lighting matrices cover 324 direct, 180 SH, 144 analytic sky and 10 distant-sky cases; compiled replay and targeted capture are inspected. Release remains at Slice 5 closure. | [Lighting/source closure](../../out/build-ninja/analysis/vortex/exposure-lightbench/lifecycle/lighting-producer-increment-manifest.json), [producer mapping](lld/scene-textures.md#producer-range-qualification), [environment increment](../../out/build-ninja/analysis/vortex/exposure-lightbench/lifecycle/domain-producer-increment-manifest.json), [material increment](../../out/build-ninja/analysis/vortex/exposure-lightbench/lifecycle/material-domain-increment-manifest.json), [injection increment](../../out/build-ninja/analysis/vortex/exposure-lightbench/lifecycle/injection-increment-manifest.json) |
| EX05-17 | Production per-view FP16 admission and allocation switching | validated | Early current-layout/source-compatible candidate selection is enabled. FP32 accumulation, half-capable environment qualification, checked resolve/fallback and supporting ownership are implemented. All 382 owning Debug tests pass; native admission, eight resize/view cases, source changes, fallback retention/failures and inspected S/P capture pass. Vacuum-layout proof is distinct from remaining nonzero/full recovery acceptance. | [Completion evidence](../../out/build-ninja/analysis/vortex/exposure-lightbench/lifecycle/admission-increment-manifest.json), [resource contract](lld/scene-textures.md#per-view-fp16-suitability) |
| EX05-18 | Checked resolve extraction and all conditional-consumer leases | validated | EX05-17 ownership is qualified through a three-view mixed-format auxiliary family (18 outputs) and delayed offscreen HDR consumers across slot reuse/view removal. Five queued tonemap consumers cover accepted half, deterministic rejected-half fallback/control and full formats, with extraction release before the fence. Frozen Debug gate: 190 passed; later sentinel-only focused test passed. Release remains at Slice 5 closure. | [Queued-consumer evidence](../../out/build-ninja/analysis/vortex/exposure-lightbench/lifecycle/queued-consumer-manifest.json), [ownership contract](lld/scene-textures.md#36-scenetextureextracts), [Admission/ownership increment](../../out/build-ninja/analysis/vortex/exposure-lightbench/lifecycle/admission-increment-manifest.json) |
| EX05-19 | Automatic recovery, excessive-range retention and stable return | validated | Producer failures request one implicit Auto Remeter; conversion-only rejection preserves valid adaptation. Fixed/disabled/borrowed failures and newer explicit requests retain authority. Native scene tests cover five owner/control modes, six-frame unsupported retention, 46-stop FP32 adaptation, reduced-range return and contrasting shared formats. Debug gate: 238 passed; 37 frozen hashes unchanged. Release and other lifecycle combinations remain at their slice gates. | [Recovery evidence](../../out/build-ninja/analysis/vortex/exposure-lightbench/lifecycle/recovery-increment-manifest.json), [runtime contract](lld/post-process-service.md#bootstrap-recovery-and-format-eligibility) |
| EX05-20 | Scene/environment descriptors, histories and queued constants retire safely | validated | Same-frame offscreen resources, fog/HZB removal, transient histories and queued HZB constants are qualified. Mode-transition-specific leases are Debug-qualified in EX05-18. | [Environment retirement](../../out/build-ninja/analysis/vortex/exposure-lightbench/lifecycle/review-r027-manifest.json), [fog/HZB lifetime](../../out/build-ninja/analysis/vortex/exposure-lightbench/lifecycle/review-lifetime-manifest.json) |
| EX05-21 | Actual memory and bandwidth accounting | validated | Four opt-in native 1080p/4K × temporal-off/on cases pass; creation-time peaks, retained outputs, cached families and shared canonical LUTs are measured from deduplicated D3D12 resources. Four capture replays verify format/P/S and final pixels, direct logical texture traffic including promoted producer writes, and warm Debug event times. Texture/report exclusions are explicit; Release performance remains at Slice 5 closure. | [Full evidence and scope](../../out/build-ninja/analysis/vortex/exposure-lightbench/lifecycle/accounting-manifest.json), [allocation contract and measured table](lld/scene-textures.md#lifetime-and-memory-accounting) |
| EX05-22 | Scene startup/cuts/seeds/modes/pause lifecycle matrix | in_progress | Native retry/domain cases and paused MultiView event fixtures exist. Complete scene-integrated active adaptation, startup/cut HDR endpoints and recovery combinations remain. | [Scene migration](../../out/build-ninja/analysis/vortex/exposure-lightbench/scene/scene-migration-manifest.json), [mode fixtures](../../out/build-ninja/analysis/vortex/exposure-lightbench/multiview/modes-manifest.json) |
| EX05-23 | Inactive, removed, recreated and replaced-world view lifecycle | in_progress | Short hide/reopen and recreated-handle proofs exist. Long-idle expiration and world replacement in the complete scene matrix remain. | [Lifetime fixture](../../out/build-ninja/analysis/vortex/exposure-lightbench/multiview/lifetime-manifest.json) |
| EX05-24 | Shared exposure and source-loss scene combinations | in_progress | Static prior-owner sharing and a source-loss sequence are qualified. Remaining borrowed zero/manual/disabled and feature/layout combinations are not closed. | [Isolation/sharing](../../out/build-ninja/analysis/vortex/exposure-lightbench/multiview/static-matrix-manifest.json), [source loss](../../out/build-ninja/analysis/vortex/exposure-lightbench/lifecycle/source-loss-manifest.json) |
| EX05-25 | Delayed/stale status, stateless views and device recovery through scenes | in_progress | Controlled status/lifetime/failure primitives and offscreen routes are qualified. Complete scene range/recovery and stateless failure reporting/acceptance remain. | [Status retries](../../out/build-ninja/analysis/vortex/exposure-lightbench/lifecycle/status-retry-manifest.json), [offscreen routes](../../out/build-ninja/analysis/vortex/exposure-lightbench/lifecycle/offscreen-sharing-manifest.json) |
| EX05-26 | Main/lit-PiP isolation, reordering and alone-versus-family equivalence | validated | Paused/static proof inputs have exact gain/meter/image agreement and specified sharing latency. This does not qualify moving-camera adaptation or all layouts. | [Static matrix](../../out/build-ninja/analysis/vortex/exposure-lightbench/multiview/static-matrix-manifest.json) |
| EX05-27 | Viewport, scissor and whole-window resize | validated | The named scripted resize/scissor fixtures pass without cross-view contamination. | [Viewport/scissor](../../out/build-ninja/analysis/vortex/exposure-lightbench/multiview/viewport-manifest.json), [window resize](../../out/build-ninja/analysis/vortex/exposure-lightbench/multiview/window-resize-manifest.json) |
| EX05-28 | MultiView per-view mode, seed, cut and diagnostic events | validated | Paused scripted events and diagnostic restoration are qualified; runtime physical-camera values are covered, persistence is not. | [Modes](../../out/build-ninja/analysis/vortex/exposure-lightbench/multiview/modes-manifest.json), [diagnostics](../../out/build-ninja/analysis/vortex/exposure-lightbench/multiview/diagnostic-manifest.json) |
| EX05-29 | Complete standard/auxiliary/offscreen/feature layout matrix | in_progress | Routing and selected captures exist. Named white captures are diagnosed: archived Manual .25 under 110000-lux sun clips in tonemapping; the new Average main view reaches a background-influenced target. The original-material Average/Spot comparison is qualified in Debug/Release with identical HDR inputs, readable Spot outputs and a retained failing Average control. The user-observed exact window is unidentified. Not every lit pane/layout has complete visual and standalone-equivalence proof. The blue unavailable-shadow diagnostic does not prove the directional-shadow layout. | [Lit-material diagnosis/profile evidence](../../out/build-ninja/analysis/vortex/exposure-lightbench/multiview/lit-profiles-manifest.json), [Failed earlier appearance](../../out/build-ninja/analysis/vortex/exposure-lightbench/multiview/atmosphere-Debug-42.exposure.png), [diagnostic scope/limitations](../../out/build-ninja/analysis/vortex/exposure-lightbench/multiview/diagnostic-manifest.json), [required scenarios](plan/exposure-and-lightbench-correction.md#75-multiview-visual-acceptance) |
| EX05-30 | Native combined interactions and active adaptation | planned | Complete scripted camera movement, reordering, resize, lifecycle and exposure changes while all panes are visible; verify gain, intermediate images and composite. | [Section 7.5](plan/exposure-and-lightbench-correction.md#75-multiview-visual-acceptance) |
| EX05-GATE | Entire Slice 5 acceptance gate | in_progress | **Not passed.** Close all remaining items; prove P invariance, upstream bright/dark preservation, complete scene lifecycle and removal of old overloaded-scalar consumption across every active path. Do not start Slice 6 integration first. | [Gate](plan/exposure-and-lightbench-correction.md#slice-5---complete-pre-exposure-migration-and-numerical-recovery) |

### 3.3 Slice 6 work items

**Slice status: planned. Persistence/authoring integration has not started.**
Approved contracts are requirements, not completed implementation.

| ID | Work item | Status | Exact remaining delivery |
| --- | --- | --- | --- |
| EX06-01 | Native physical-camera persistence | planned | Source/cook/load aperture, shutter and ISO; scene-v6 perspective/orthographic 32/40-byte records; v5 20/28-byte defaults f/11, 125/s, ISO100. Keep the existing editor UI. |
| EX06-02 | Exposure source schemas and component/config parity | planned | Carry all authored exposure fields through JSON schemas and native component/config boundaries; existing canonical runtime types are a prerequisite, not completion of this integration. |
| EX06-03 | Versioned packed exposure record | planned | Implement the approved 144-byte prefix, mask resource index at 116, reserved 120–131, curve count at 132 and keys at 144; preserve enum ordinals/defaults. |
| EX06-04 | Cooker, package remapping and loader | planned | Texture path to source-local index, PAK index remapping and runtime ResourceKey hydration; curve and scalar cook/load support. |
| EX06-05 | Scripting and existing editor/native adapters | planned | Round-trip the same canonical fields and physical-camera values through existing adapters; no new physical-camera editor controls. |
| EX06-06 | Old/new source-cook-load-save/reload tests | planned | Verify every field, mask, curve, black influence and D, resolved-setting equality, old-record defaults and malformed boundaries. |
| EX06-07 | Runtime asynchronous mask acceptance prerequisite | validated | Pending/resident/failed masks and atomic accepted revisions already exist from runtime work, including direct SetConfig loading. Persistence round-trip remains EX06-04/06. [Evidence](../../out/build-ninja/analysis/vortex/exposure-lightbench/lifecycle/review-r028-manifest.json). |
| EX06-08 | DemoShell controls and status | planned | Correct controls/labels; expose requested versus effective settings and resource/metering failures. |
| EX06-09 | Experiment-owned activation policy | planned | Prevent saved camera/scene/post-process reapplication from overriding a LightBench recipe while preserving other demos' behavior and personal settings. |
| EX06-GATE | Authoring/persistence/isolation gate | planned | Identical resolved settings across the full round-trip, deterministic legacy loading and no personal-settings mutation during batch execution. |

### 3.4 Slice 7 work items

**Slice status: planned. Calibration implementation has not started.**

| ID | Work item | Status | Exact remaining delivery |
| --- | --- | --- | --- |
| EX07-01 | Directional reference calibration | planned | Verify the white directional reference against the actual production BRDF/material and exposure equations. |
| EX07-02 | Point units and distance behavior | planned | Flux/(4*pi), inverse-square attenuation, smooth range fade, 0.001 m numerical floor and zero-separation handling. |
| EX07-03 | Spot normalization | planned | Smooth-cone solid-angle normalization, hard-cone limit, zero-angle rejection and angular/range boundary tests. |
| EX07-04 | Shared forward/deferred consumers | planned | Apply the same light-unit helpers and receiver-cosine convention to both active rendering families. |
| EX07-05 | Production material/color-space oracle | planned | Freeze packed albedo, normal, dielectric specular and color-space interpretation; use an independent reference calculation rather than treating roughness as Lambertian. |
| EX07-06 | Numerical integration and calibration tests | planned | Independently verify flux integration, distance/cone boundaries, known fade and all three required light experiments. |
| EX07-GATE | Complete lighting unit chain | planned | Passing directional/point/spot expectations and unit/BRDF contracts; no deferred calibration dependency. |

### 3.5 Slice 8 work items

**Slice status: planned. Reusable benchmark instrumentation has not started.**
Existing test readbacks/RenderDoc analyzers are not the delivered measurement API.

| ID | Work item | Status | Exact remaining delivery |
| --- | --- | --- | --- |
| EX08-01 | Region statistics and identity | planned | Diagnostics/extraction results with frame, view-state, source product/domain, region, validity and sample counts. |
| EX08-02 | Actual consumed exposure/output probe | planned | Recover scene values with stored P and inspect the gain consumed by that frame; opt-in GPU probe at the production operation; label CPU-derived quantities explicitly. |
| EX08-03 | Invalid and contaminated regions | planned | Distinguish zero from late, occluded, insufficient, edge-contaminated, partial-coverage and nonfinite measurements. |
| EX08-04 | Async association and lifetime | planned | Bounded readback/resources, fence-safe ownership and rejection of stale frame/view/experiment revisions. |
| EX08-05 | Disabled-path cost | planned | Verify zero measurement dispatches/readback allocations when disabled and bounded enabled-path cost. |
| EX08-06 | Independent instrument qualification | planned | Known GPU signals, P conversion, actual gain, zero/coverage/invalid cases and delayed-readback changes. |
| EX08-GATE | Instrument qualification before bench verdicts | planned | Instruments must match independent inputs before LightBench uses them to judge rendering. |

### 3.6 Slice 9 work items

**Slice status: planned. LightBench repair and experiment implementation have not
started. Some MultiView prerequisites were delivered in Slice 5, as identified.**

| ID | Work item | Status | Exact remaining delivery |
| --- | --- | --- | --- |
| EX09-01 | One versioned experiment schema/controller | planned | Shared validated definitions and one execution path for interactive and batch use. |
| EX09-02 | Complete staged application and reset | planned | Own geometry, materials, lights, environment, camera, features, exposure/output, measurement regions and temporal transition; apply coherently at a frame boundary. |
| EX09-03 | Neutral Reference experiment | planned | Default startup; three readable framed cards, visible-side camera/key, real white directional light and independently selected material/exposure/output reference. |
| EX09-04 | Fixed Exposure experiment | planned | Known HDR input and EV/key/compensation sweeps through uploaded and consumed gain. |
| EX09-05 | Adaptation experiment | planned | Both luminance-step directions, controlled dt, masks and compensation curves. |
| EX09-06 | Lifecycle experiment | planned | Startup, seeds, cuts, mode changes, pause, sharing, stateless views and recovery. |
| EX09-07 | Point Falloff experiment | planned | Prescribed receiver/distances, visible geometry relationship and independent inverse-square expectations. |
| EX09-08 | Spot Distribution experiment | planned | Aimed receiver, cone overlay, angular response and integrated flux expectations. |
| EX09-09 | HDR Domain experiment | planned | Bright/dark endpoints and mixed opaque/forward/translucent/sky/fog content under varying P. |
| EX09-10 | Focused controls and overlays | planned | Relevant controls, expected/measured values, effective exposure, Pass/Fail/Modified/Invalid states and visible debug overrides; collapsed advanced controls. |
| EX09-11 | Saved experiment migration/loading | planned | Version the indoor preset, load modified experiments explicitly, separate window/panel preferences and preserve personal files. |
| EX09-12 | LightBench native visual acceptance | planned | Inspect all seven experiments, startup, transitions and reset at 1080p, 1440p and resized dimensions; aligned measurement regions and reproducible presented captures. |
| EX09-13 | MultiView operational integration | in_progress | Existing proof CLI, framing fixes and scenario fixtures are prerequisites from Slice 5. Complete controls/measurement integration, combined interactions and all layout/pane acceptance remain. [Current README](../../Examples/MultiView/README.md). |
| EX09-14 | LightBench operating README | planned | `Examples/LightBench/README.md` does not yet exist; write actual launch/run/reset/save instructions, supported tests and interpretation after implementation. |
| EX09-15 | MultiView operating README closure | in_progress | Current README documents proof modes and intentional black cells. Reconcile it with the completed matrix, shared latency, measurement results and final repeatable commands. |
| EX09-GATE | Interactive benchmark and MultiView visual gate | planned | Complete reproducible LightBench and all MultiView layouts/interactions; inspect native presented output of both applications. |

### 3.7 Slice 10 work items

**Slice status: planned. Package-level automation and final acceptance have not
started. Individual Slice 5 capture/assertion scripts are prerequisites only.**

| ID | Work item | Status | Exact remaining delivery |
| --- | --- | --- | --- |
| EX10-01 | Deterministic batch execution | planned | Select/run the same versioned experiments and controller as interactive mode using existing capture CLI. |
| EX10-02 | LightBench runner and report schema | planned | `tools/vortex/Run-LightBenchValidation.ps1` does not yet exist; implement it and a schema-validated result report. |
| EX10-03 | Native game-facing/lifecycle cases | planned | Integrate production callers without DemoShell, scene lifecycle, sharing and numerical recovery into the runner. |
| EX10-04 | Extend the existing MultiView runner | planned | Extend `Run-VortexMultiViewValidation.ps1`, existing analyzer/assertions and result schema; retain structural checks and add exposure/image/interaction acceptance. |
| EX10-05 | Reuse current per-view proof tools | in_progress | Slice 5 has isolated/reordered/shared, source-loss, resize, lifetime, mode and diagnostic capture/assertion tools. Their integration into the final runner/report and remaining cases are unfinished. |
| EX10-06 | Complete report provenance | planned | Record resolved parameters, experiment/version/revision, build/shader identity, device/backend, actual dt, frame/view validity, tolerances, measurements and captures. |
| EX10-07 | Entire acceptance matrix | planned | Run every case in section 9, including native presented-image inspection; required failed/unsupported cases block completion. |
| EX10-08 | Final owner-document reconciliation | planned | Reconcile equations/contracts, implementation/operating docs and this tracker against final evidence. |
| EX10-GATE | Full package completion | planned | Every required feature, experiment, command and acceptance gate passes. The overall goal remains active until this is proven. |

### 3.8 Requirement coverage and update discipline

The item IDs are stable work items, not commit-history entries. Update their rows
in place. When an item is reopened, state the defect and remaining validation
there; after correction, replace the stale limitation with the current result.
Record completed code and its proof together. If work stops before validation,
leave the item `in_progress` and name the missing check. Update section 3.1 before
moving to another item so the user can see what is being worked on without
reading tool logs or reconstructing Git history.

| Plan section 8 requirements | Tracking items |
| --- | --- |
| Slice 5: early P; full 4.4 migration; S/P; formats; range/recovery; scene lifecycle; MultiView; gate | EX05-01; EX05-04–09/18; EX05-03; EX05-02/17; EX05-10–16/19–21; EX05-22–25; EX05-26–30; EX05-GATE |
| Slice 6: camera persistence; authoring surfaces; full round-trip; mask runtime; DemoShell; activation policy; gate | EX06-01; EX06-02–05; EX06-06; EX06-07; EX06-08; EX06-09; EX06-GATE |
| Slice 7: point/spot; shared consumers; independent calibration; material mapping; gate | EX07-02–03; EX07-04; EX07-01/06; EX07-05; EX07-GATE |
| Slice 8: diagnostics; known inputs; identity; disabled/enabled cost; gate | EX08-01–03; EX08-06; EX08-04; EX08-05; EX08-GATE |
| Slice 9: definitions; ownership/reset; UI; seven visual experiments; preset/loading; MultiView; both READMEs; gate | EX09-01; EX09-02; EX09-10; EX09-03–09/12; EX09-11; EX09-13; EX09-14–15; EX09-GATE |
| Slice 10: batch; LightBench runner; MultiView extension; provenance; full acceptance/docs; gate | EX10-01; EX10-02–03; EX10-04–05; EX10-06; EX10-07–08; EX10-GATE |

### Related editor package

**Implementation package:** `ED-M08 — V0.1 canonical authoring and rendering`.
**Status:** `planned`; design package ready, implementation and rendered gates open.

The [cross-engine/editor plan](../../../../design/editor/plan/ED-M08-runtime-parity-and-standalone-validation.md)
defines native implementation and native visual validation before editor work.
The engine [rendering contract](plan/editor-v01-rendering-contract.md) and
[captured-sky IBL contract](plan/editor-v01-captured-sky-ibl.md) define this
extension. The editor ledger owns its slice-by-slice progress; this ledger
records the native capability boundary without duplicating its schedule.

VTX-M08 remains validated at its static diffuse-only scope. ED-M08 requires new
proof for its formats, directional/visibility/shadow behavior, captured sky and
specular IBL, camera framing, grading, primitives and material contract.

**Latest closed plan:** `design/vortex/plan/VTX-M08-skybox-static-skylight.md`.
Validated M08 LLD references are `design/vortex/lld/cubemap-processing.md` and
`design/vortex/lld/skybox-static-skylight.md`.

## 4. Milestone Ledger

| ID | Milestone | Status | Current Evidence | Missing To Close |
| --- | --- | --- | --- | --- |
| Exposure / LightBench / MultiView | Complete global exposure and benchmark package | `in_progress` | [Current work, slice status and item-level evidence](#3-exposure-delivery-status). | All open items and gates in section 3; no independent completion claim in this summary row. |
| ED-M08 native extension | V0.1 canonical authoring and rendering | `planned` | Final rendering/IBL contracts, source-local deferred annotations and editor execution plan. | Native implementation, focused tests and visual proof outside the editor, followed by integrated editor qualification. |
| VTX-M00 | Planning and status truth surface | `validated` | `PLAN.md` was rewritten as a milestone-first plan; this milestone/status ledger exists; restricted doc scans and `git diff --check` passed on 2026-04-25. | No open planning-status gap. |
| VTX-M01 | Renderer Core and SceneRenderer baseline | `validated` | Vortex module, Renderer Core, publication, upload/resource substrate, SceneRenderer shell, SceneTextures, non-runtime facades, resolve/cleanup, and related tests are present and freshly validated. Build proof passed `cmake --build out\build-ninja --config Debug --target Oxygen.Vortex.LinkTest Oxygen.Vortex.RendererCapability.Tests Oxygen.Vortex.RenderContext.Tests Oxygen.Vortex.SceneRendererShell.Tests Oxygen.Vortex.SceneTextures.Tests Oxygen.Vortex.SceneRendererPublication.Tests Oxygen.Vortex.RenderContextMaterializer.Tests Oxygen.Vortex.RendererFacadePresets.Tests Oxygen.Vortex.RenderGraphHarnessFacade.Tests Oxygen.Vortex.SinglePassHarnessFacade.Tests Oxygen.Vortex.UploadCoordinator.Tests Oxygen.Vortex.ViewConstantsManager.Tests oxygen-examples-vortexbasic --parallel 4`. Focused CTest passed the corresponding Vortex substrate/facade suites plus `Oxygen.Vortex.LinkTest`. Runtime proof `tools\vortex\Run-VortexBasicRuntimeValidation.ps1 -Output out\build-ninja\analysis\vortex\m01-m03-closeout\vortexbasic-foundation -Frame 3 -RunFrames 6 -Fps 10 -BuildJobs 4` passed overall with runtime exit 0, final present nonzero, CDB/debug-layer `overall_verdict=pass`, no D3D12/DXGI errors, and no blocking warnings. | No open VTX-M01 closure gap. |
| VTX-M02 | Deferred core visual path | `validated` | InitViews, depth prepass, generic Screen HZB, base pass/GBuffer/velocity, Stage 10 publication, deferred lighting, shader families, debug views, and focused tests/tools are present and freshly validated. Build proof passed `Oxygen.Vortex.SceneRendererPublication.Tests`, `Oxygen.Vortex.SceneRendererDeferredCore.Tests`, `oxygen-graphics-direct3d12_shaders`, `Oxygen.Graphics.Direct3D12.ShaderBakeCatalog.Tests`, and `oxygen-examples-vortexbasic`. CTest passed `Oxygen.Vortex.SceneRendererPublication.Tests`, `Oxygen.Vortex.SceneRendererDeferredCore.Tests`, and `Oxygen.Graphics.Direct3D12.ShaderBakeCatalog.Tests`. Runtime proof `vortexbasic-foundation.validation.txt` passed with Stage 3 depth scope/draw/clear/copy counts, Stage 5 Screen HZB scope, Stage 9 base-pass scope/draw counts, Stage 9 GBuffer/base-color/velocity nonzero proof, Stage 12 scope/directional/point/spot draw counts, and phase-stage order checks. | No open VTX-M02 closure gap. |
| VTX-M03 | Migration-critical non-environment services | `validated` | LightingService, ShadowService baseline, PostProcessService, Stage 8/12/22 routing, and focused tests are present and freshly validated. Build proof passed `Oxygen.Vortex.LightingService.Tests`, `Oxygen.Vortex.ShadowService.Tests`, `Oxygen.Vortex.PostProcessService.Tests`, `Oxygen.Vortex.SceneRendererDeferredCore.Tests`, shader bake/catalog targets, and `oxygen-examples-vortexbasic`. CTest passed `Oxygen.Vortex.LightingService.Tests`, `Oxygen.Vortex.ShadowService.Tests`, `Oxygen.Vortex.PostProcessService.Tests`, `Oxygen.Vortex.SceneRendererDeferredCore.Tests`, and `Oxygen.Graphics.Direct3D12.ShaderBakeCatalog.Tests`. Runtime proof `vortexbasic-foundation.validation.txt` passed with Stage 12 deferred-lighting scope, directional/point/spot draw-count proof, nonzero Stage 12 SceneColor for directional/point/spot lighting, compositing present operation count, final present nonzero, CDB/debug-layer `overall_verdict=pass`, no D3D12/DXGI errors, and no blocking warnings. Later milestones validate expanded conventional shadows, diagnostics, translucency, environment, and composition behavior. | No open VTX-M03 closure gap. |
| VTX-M04D | Environment / fog parity closure | `validated` | VTX-M04D.1 through VTX-M04D.6 validated environment publication truth, height fog, local fog volumes, volumetric fog, consolidated environment runtime proof, and main-view aerial perspective against the UE5.7 source/shader families recorded in the M04D plans. Evidence includes focused Vortex builds/tests, ShaderBake/catalog validation where shaders changed, VortexBasic and RenderScene RenderDoc proof, CDB/D3D12 debug-layer audits where required, and user visual confirmation for projected shadows and city scene shadows. Static specified-cubemap SkyLight products are validated under VTX-M08. | Captured-scene SkyLight, real-time capture, and reflection/360 AP remain deferred to later resource paths. |
| VTX-M04D.1 | Environment publication and sky/fog contract truth | `validated` | EnvironmentLightingService sanitizes IBL probe bindings, distinguishes authored SkyLight from usable IBL resources, keeps invalid SkyLight/volumetric products explicit, exposes Stage 14 local-fog state through SceneRenderer, and focused EnvironmentLightingService/SceneRendererPublication tests passed on 2026-04-25. | Real SkyLight capture/filtering and fog parity are outside VTX-M04D.1 and tracked by later milestones. |
| VTX-M04D.2 | UE5.7 exponential height fog parity | `validated` | CPU/HLSL height-fog payload, authored layer translation, analytic `HeightFogCommon`-shaped line integral, sky-depth exclusion, DemoShell/RenderScene controls, focused build/tests, ShaderBake/catalog validation, focused VortexBasic enabled/disabled RenderDoc proof, and city-scale RenderScene capture proof are recorded. | Cubemap inscattering resource binding/sampling remains explicitly unavailable and deferred. |
| VTX-M04D.3 | UE5.7 local fog volume parity | `validated` | Analytical local-fog volume path is implemented and proven: UE5.7 source mapping, authoring sanitization, sorting/capping, HZB-backed tiled culling, single draw-indirect rendering for the Stage-15 local-fog compose path, analytical shader path, SceneColor contribution, far-depth no-op behavior, focused tests, ShaderBake/catalog validation, VortexBasic runtime/capture proof, and focused RenderDoc draw-args probe. | Local-fog participating-media injection into volumetric fog is validated under VTX-M04D.4, not VTX-M04D.3. |
| VTX-M04D.4 | UE5.7 volumetric fog parity | `validated` | Stage-14 compute volumetric fog allocates and publishes `IntegratedLightScattering`, Stage 15 composes it, and proof covers captured fog payload/SRV/grid state, volume min/max/slices, directional CSM shadowed-light sampling, local-fog participating-media injection, Oxygen distant-SkyLight volumetric ambient, temporal jitter/reset/history-miss reprojection, city-scale `CityEnvironmentValidation`, and D3D12 debug-layer audit. Directional CSM projected-shadow blocker was closed with RenderDoc proof and user visual confirmation. | Accepted Oxygen divergence: single integrated-scattering temporal product without UE conservative-depth history fixup or pre-exposure transfer. Real SkyLight cubemap capture/filtering remains outside this milestone. |
| VTX-M04D.5 | Environment runtime proof and Async preparation | `validated` | `Run-VortexBasicRuntimeValidation.ps1` builds VortexBasic, runs a CDB/D3D12 debug-layer audit, captures RenderDoc frame 5, and asserts one runtime path with atmosphere, main-view AP, height fog, local fog, volumetric fog, authored SkyLight unavailable state, and SkyLight volumetric injection. Focused EnvironmentLightingService tests passed. | Real SkyLight cubemap capture/filtering remains a later IBL/resource gap. Async proof is validated separately by VTX-M04E. |
| VTX-M04D.6 | UE5.7 aerial perspective parity | `validated` for main-view AP | Vortex AP sampling uses camera-volume lookup helpers traced to UE5.7 `SkyAtmosphere` shader behavior, preserves raw camera-volume generation with apply-time strength control, exposes effective DemoShell/VortexBasic AP controls, fixes city-scale reversed-Z far-depth AP composition, and has focused enabled/disabled plus city-scale RenderDoc proof. | Reflection/360-view AP is explicitly deferred to the future reflection-capture resource path. |
| VTX-M04E | Async migration parity gate | `validated` | Async authors a Vortex-ready scene environment, sun directional light, shadow receiver ground, lifted sphere/two-submesh geometry, and Vortex runtime view metadata. DemoShell no longer overrides the scene-authored environment. `Run-AsyncRuntimeValidation.ps1` passed with RenderDoc structural/product proof, final present proof, and overlay composition proof on 2026-04-26. | No open M04E closure gap. |
| VTX-M04F | Single-view composition and presentation closeout | `validated` | Runtime composition registration, queued single-view composition copy, Stage 22 post-process routing, and overlay blend path exist. `AnalyzeRenderDocAsyncProducts.py` proves exactly one post-Stage-22 composition copy from `Async.SceneColor`, exactly one overlay blend after scene copy, final present output, and focused `RendererCompositionQueue` tests passed. | No open M04F closure gap. |
| VTX-M05A | Diagnostics product service | `validated` | Diagnostics runtime service, authoritative shader-debug registry, frame ledger, capture manifest, GPU timeline facade, Diagnostics panel, proof script hardening, direct/masked debug corrections, unsupported IBL/light-culling mode handling, and solid/wireframe/wireframe-overlay render controls are implemented and documented in the LLD/plan. Validation evidence: focused Vortex/DemoShell builds and tests, release DiagnosticsService test, VortexBasic runtime RenderDoc/debug-layer proof, TexturedCube panel-registration smoke, RenderScene build/ShaderBake validation, and user visual confirmation for solid plus wireframe overlay. | No open M05A closure gap. Optional GPU debug primitives remain deferred until a concrete proof gap requires them. |
| VTX-M05B | Occlusion consumer closeout | `validated` | LLD/plan updated; result substrate, HZB tester, base-pass consumers, diagnostics facts, `vtx.occlusion.*` controls, and VortexBasic `--with-occlusion` proof scene/tooling exist. Validation: ShaderBake rebuilt 186 modules; focused `RendererCapability`, `OcclusionModule`, `SceneRendererPublication`, and `SceneRendererDeferredCore` tests passed 62/62; CDB/D3D12 audit report `out/build-ninja/analysis/vortex/occlusion/vortex-occlusion.debug-layer.report.txt` passed with 0 D3D12/DXGI errors; RenderDoc proof `vortex-occlusion.proof.report.txt` shows Stage 3 depth draws 3, Stage 5 occlusion dispatch 1, Stage 9 base-pass scene draws 2, and Stage 20 ground grid absent; user visual confirmation approved. | No open M05B closure gap. |
| VTX-M05C | Translucency stage | `validated` | Stage 18 `TranslucencyModule`/`TranslucencyMeshProcessor`, SceneRenderer wiring, forward unlit-material exposure contract fix, VortexBasic cyan sphere + magenta cylinder proof scene, and RenderDoc/CDB proof tooling are present. Senior-review remediation on 2026-04-26 fixed sparse-bounds sort fallback, invalid draw rejection, projection-kind detection, diagnostics skip reasons/logging, and Stage 18 PSO/root-binding descriptor caching; broader UE-class gaps for per-material sided culling, instanced draw merging, lightweight translucent shading, and material fog/AP controls are documented as deferred scope. UE5.7 re-check covered standard straight-alpha blending and read-only depth state. Validation: focused ShaderBake/catalog tests passed previously; remediation validation passed `cmake --build out\build-ninja --config Debug --target Oxygen.Vortex.SceneRendererDeferredCore --parallel 4`, `ctest --preset test-debug -R "Oxygen\.Vortex\.SceneRendererDeferredCore" --output-on-failure` with 40/40 tests, and `git diff --check`. Fresh VortexBasic translucency proof after remediation passed `cmake --build out\build-ninja --config Debug --target oxygen-vortex oxygen-graphics-direct3d12 oxygen-examples-vortexbasic --parallel 4`, a CDB/debug-layer audit, runtime log inspection, RenderDoc capture, and `Verify-VortexTranslucencyProof.ps1`. Fresh CDB report `out/build-ninja/analysis/vortex/translucency/m05c-review-remediation/vortexbasic-translucency-review-remediation.debug-layer.report.txt` passed with runtime exit 0, no debugger break, 0 D3D12/DXGI errors, and 0 blocking warnings. Fresh RenderDoc report `out/build-ninja/analysis/vortex/translucency/m05c-review-remediation/vortexbasic-translucency-review-remediation_capture.rdc_vortex_translucency_report.txt` proves Stage 18 scope count 1, Stage 18 draw count 2, Stage 9 draw count 2, ground grid absent, Stage 18 after post-opaque and before resolve, cyan pixels 2130, magenta pixels 225, max RGB delta 2684, `stage18_scene_color_changed=true`, `runtime_log_translucency_enabled=true`, and `runtime_log_draw_metadata_count=4`. User visual confirmation approved the final scene. | No open M05C closure gap. |
| VTX-M05D | Conventional shadow parity and local-light expansion | `validated` | Directional CSM UE5.7 audit/remediation is recorded in `shadow-service.md` and `VTX-M05D-conventional-shadow-parity.md`: stable/no-AA frusta, sphere bounds, texel snapping, 5000-unit directional depth extent, UE-style non-last transition overlap coverage, optional CSM constant/slope depth bias, default zero bias, and resolution-hint wiring are present. Release `RenderScene --scene VsmTwoCubes --directional-shadows conventional` smoke/capture/probes validated the local-scale CSM descriptor/settings after the Serio scene-loader fix. Slice E spot-light conventional shadows are implemented and validated with focused tests, shader validation, CDB/debug-layer audit, RenderDoc probe `spot-shadow-validation.bias0.final.spot-shadow-probe.txt`, and user visual confirmation after authored spot bias `0.0`. Slice F point-light conventional shadows are implemented with cube-array storage, six explicit face depth slices, Stage 12 point-shadow consumption, point proxy sphere winding regression coverage, and focused `PointShadowValidation` proof. Validation passed `cmake --build out\build-ninja --target Oxygen.Vortex.SceneRendererDeferredCore.Tests Oxygen.Vortex.ShadowService.Tests oxygen-graphics-direct3d12_shaders --parallel 4`; tests passed SceneRendererDeferredCore `43/43` and ShadowService `9/9`; ShaderBake repacked `186` modules. CDB report `point-shadow-validation.final.debug-layer.report.txt` passed with runtime exit `0`, no debugger break, `0` D3D12/DXGI errors, and `0` blocking warnings. RenderDoc probe `point-shadow-validation.final.point-shadow-probe.txt` proves Stage 8 point draws `168,171,187,190,206,209,225,228,244,247,263,266`, non-clear `Vortex.PointShadowCubeSurface` slices `0`, `2`, and `5`, Stage 12 point draw `343`, Stage 12 point SRV binding, and `SceneColor` contribution. User visual confirmation on 2026-04-27 approved both point-light shell and point-shadow artifact fixes. | No open M05D closure gap. Stage 18 translucent local-light shadow consumption, layered one-pass point cubemap rendering, local-light shadow caching, and VSM remain deferred future work. |
| VTX-M06A | Multi-view proof closeout | `validated` | B1-H implementation exists. B1: per-view `ViewRenderSettings`, producer-owned `ViewStateHandle`, packet-level state-handle copy, packet-owned effective shader-debug mode, handle-keyed `PreviousViewHistoryCache`, ViewStateHandle-keyed exposure state, and removal of `FramePlanBuilder` frame-global render/debug fields. B2: runtime C++ payload types for `ViewKind`, `ViewFeatureMask`, `ViewSurfaceRoute`, `OverlayPolicy`, auxiliary IO descriptors, factory classification, packet copies, and `FramePlanBuilder` view-kind validation. C: `PerViewScope`, no-eager-cursor frame entry materialization, serialized `SceneRenderer::RenderViewFamily`, per-view scene-product reset while reusing the existing `SceneTextures` family, and pre/post per-view binding publication. D: descriptor-keyed `SceneTextureLeasePool`, exclusive per-view leases routed through `RenderViewFamily`, explicit exhaustion, queue-affinity keying, and focused allocation-churn coverage. E: route-aware layer planning, structural full-surface copy selection independent of `kZOrderScene`/primary id, deterministic surface/layer debug names, and filtered surface submissions. F: typed `AuxiliaryDependencyGraph`, runtime publication/materialization of auxiliary descriptors, required producer resolution, producer-before-consumer ordering, extracted color-product publication, and dependent view GPU consumption. G: typed overlay batches, `on_overlay` to `kViewScreen` compatibility conversion, reserved world-depth-aware/world-foreground view overlay placeholders, surface overlay execution before presentable handoff, and typed `IViewExtension` hooks including `OnPostComposition`. H: `Examples/MultiView --proof-layout true`, `--aux-proof-layout true`, CDB/debug-layer audit script, RenderDoc analyzer, assertion script, allow-list, validation schema, and scene-texture lease-pool runtime churn telemetry are implemented. Latest validation passed proof-script parser checks, `AnalyzeRenderDocVortexMultiView.py` py-compile, standard runtime proof `Run-VortexMultiViewValidation.ps1 -Output out\build-ninja\analysis\vortex\m06a-multiview\multiview-proof -Frame 5 -RunFrames 65 -Fps 30 -BuildJobs 4`, auxiliary runtime proof `Run-VortexMultiViewValidation.ps1 -AuxProofLayout -Output out\build-ninja\analysis\vortex\m06a-multiview\multiview-aux-proof -Frame 5 -RunFrames 65 -Fps 30 -BuildJobs 4`, CDB reports with `overall_verdict=pass`, runtime exit `0`, `d3d12_error_count=0`, `dxgi_error_count=0`, and `blocking_warning_count=0`, RenderDoc reports with `overall_verdict=true`, `stage9_scope_count=4`, `composition_view_ids=1,2,3,4`, `aux_consume_scope_count=1`, `aux_consume_copy_count=1`, and `aux_consume_before_composition=true`, allocation reports with `steady_state_frame_count=60` and `steady_state_allocations_after_warmup=0`, focused section 9 build/ctest with 10/10 matched tests passing, and user visual confirmation on 2026-04-28 that GroundGrid stability is corrected. ShaderBake/catalog validation was not run because no shader bytecode, catalog, HLSL ABI, or root-binding files changed. | No open VTX-M06A closure gap. Offscreen-only proof remains VTX-M06B; feature-gated runtime variants remain VTX-M06C. |
| VTX-M06B | Offscreen proof closeout | `validated` | Detailed plan `design/vortex/plan/VTX-M06B-offscreen-proof-closeout.md` exists. Slices A-D landed source/test proof for docs truth, offscreen scene execution, deferred/forward routing, and output product final state. Slice E source commits `f12fcefb9`, `dfc3dab3c`, `5299d6c1c`, `62b4b525f`, and `97ca7fb65` add runtime texture composition layers, embedded offscreen execution, a visually inspectable MultiView offscreen layout, forward-wireframe regression coverage, and solid forward base-pass SceneColor output without deferred GBuffer publication; the offscreen capture proof tile now requests forward solid. Proof tooling `Run-VortexOffscreenValidation.ps1`, `AnalyzeRenderDocVortexOffscreen.py`, and `Assert-VortexOffscreenProof.ps1` runs the closure gate. Latest validation passed `powershell -NoProfile -ExecutionPolicy Bypass -File tools\vortex\Run-VortexOffscreenValidation.ps1 -Output out\build-ninja\analysis\vortex\m06b-offscreen\offscreen-proof -Frame 5 -RunFrames 65 -Fps 30 -BuildJobs 4`: CDB report `overall_verdict=pass`, runtime exit `0`, `d3d12_error_count=0`, `dxgi_error_count=0`, and `blocking_warning_count=0`; RenderDoc report `overall_verdict=true`, `deferred_draw_count=10`, `forward_draw_count=5`, preview/capture composite work counts `1/1`, and non-empty preview/capture texture RGB proof; allocation report `run_frames_at_least_60=true`, `steady_state_frame_count=190`, `steady_state_allocations_after_warmup=0`, and `overall_verdict=pass`; assertion report `overall_verdict=pass`. Focused build/CTest also passed `OffscreenSceneFacade`, `RendererCompositionQueue`, and `SceneRendererDeferredCore` with 3/3 test executables. User visual confirmation on 2026-04-28 approved the solid-forward offscreen proof visual. ShaderBake/catalog validation was not run because no shader source, shader ABI, root-binding, or catalog files changed. | No open VTX-M06B closure gap. Feature-gated runtime variants remain VTX-M06C. |
| VTX-M06C | Feature-gated runtime variants | `validated` | Detailed plan `design/vortex/plan/VTX-M06C-feature-gated-runtime-variants.md` exists. Slices B-E implemented and validated feature-profile vocabulary/carry, depth-only and shadow-only gates, no-environment/no-shadowing/no-volumetrics gates, diagnostics-only product truth, and focused source tests. Slice F added the MultiView `--feature-variant-proof-layout true` runtime proof, RenderDoc analyzer, CDB/debug-layer wrapper, assertion script, and allocation-churn report. Closure validation passed the full focused build plus `oxygen-graphics-direct3d12_shaders`, full focused CTest with 8/8 Vortex test executables, CDB/debug-layer report with runtime exit `0`, no debugger break, `d3d12_error_count=0`, `dxgi_error_count=0`, and `blocking_warning_count=0`, RenderDoc report with `overall_verdict=true`, `expected_variant_view_count=6`, `composition_view_ids=1,2,3,4,5,6`, correct reduced-variant stage omission, and Stage 22 scene-lighting-only proof, allocation report with `steady_state_frame_count=60` and `steady_state_allocations_after_warmup=0`, assertion report with 65 runtime records for every variant and `overall_verdict=pass`, absence of the prior missing-SceneColor compositing warning, and user visual confirmation on 2026-04-28 approving the proof visuals, shadow stability, compact labels, and `BLACK expected` markers. | No open VTX-M06C closure gap. |
| VTX-M07 | Production readiness and legacy retirement | `validated` | Detailed plan `design/vortex/plan/VTX-M07-production-readiness-legacy-retirement.md` exists and is updated with closure evidence. Static source seam guard `tools/vortex/Assert-VortexLegacySeams.ps1` passed with tooling at `out\build-ninja\analysis\vortex\m07-closeout\legacy-seams-tooling.txt`, scanning 581 current source/build/tooling files with no forbidden legacy renderer seams. Stale uncompiled MultiView legacy source (`Examples/MultiView/ImGuiView.*`) was removed. Active DemoShell/TexturedCube/Physics compatibility seams under `oxygen::renderer` / `renderer::` were removed. Async README/tooling now executes current Vortex proof without `Capture-AsyncLegacyReference.ps1` or `ReferenceRoot`. Required demo refresh fixed DemoShell scene-authored environment behavior, fixed exposure seeding, TexturedCube/InputSystem/Physics sun/environment setup, no-texture-sampling solid materials, and the Physics +Z-up floor; Physics direct-light RenderDoc probe `out\build-ninja\analysis\physics\physics-after-scene-cleanup-direct-lighting-probe.txt` passed with nonzero floor direct lighting proof. Full registered example build matrix passed. Short D3D12/debug-layer smokes passed for Async, InputSystem, LightBench, TexturedCube, Physics, RenderScene, VortexBasic, and MultiView; Devices, Platform, and OxCo examples are classified with build/help/smoke evidence. Closure build passed `Oxygen.Vortex.LinkTest`, `oxygen-vortex`, and all required Vortex example targets. Focused CTest passed 14/14 after explicitly rebuilding a stale `Oxygen.Vortex.OcclusionModule.Tests` binary. Current-path doc/source audit `out\build-ninja\analysis\vortex\m07-closeout\current-source-legacy-doc-audit.txt` passed with zero matches under `src/Oxygen`, `Examples`, and `tools/vortex`. Binary dependency audit `out\build-ninja\analysis\vortex\m07-closeout\binary-dependencies.txt` passed with zero legacy renderer dependency matches. VortexBasic, Async, MultiView standard/auxiliary, Offscreen, and Feature-variant runtime proof wrappers passed under `out\build-ninja\analysis\vortex\m07-closeout` with CDB/debug-layer and RenderDoc/scripted-analysis reports. ShaderBake/catalog validation was not run because no shader source, shader ABI, root-binding, or catalog data changed. `oxygen::imgui` remains allowed UI infrastructure and not a legacy renderer seam. | No open VTX-M07 closure gap. VTX-M08 is validated as the first post-baseline family. |
| VTX-M08 | Skybox and static specified-cubemap SkyLight | `validated` | Plan `design/vortex/plan/VTX-M08-skybox-static-skylight.md` and validated-reference LLDs `design/vortex/lld/cubemap-processing.md` / `design/vortex/lld/skybox-static-skylight.md` record the closed implementation and UE5.7 source-derived contract. Slice A validated SkyLight authored fields, current 92-byte `SkyLightEnvironmentRecord`, no legacy cooked-record decoding, scene descriptor/schema/PakGen/PakDump/DemoShell propagation, Vortex `SkyLightEnvironmentModel` hashing, and per-view `VisibleSkyBackground` plan state with no-environment gating. Slice B validated explicit static SkyLight product state, dedicated `diffuse_sh_srv`, CPU/HLSL `EnvironmentFrameBindings` mirror update, specified-cubemap asset validation, CPU cubemap processing with yaw/lower-hemisphere/HDR scale/mip/average-brightness/eight-`float4` SH output, renderer-owned TextureCube plus SH upload/cache/publication, `GpuSkyLightParams::diffuse_sh_slot`, and `ForwardMesh_PS.hlsl` / `ForwardDebug_PS.hlsl` / `LocalFogVolumeCommon.hlsli` migration away from visual-skybox and cubemap-irradiance fallbacks. Slice C validated scene-authored `SkySphere` publication, invalid default cubemap descriptors, Sky pass solid/cubemap branch before procedural-atmosphere LUT requirements, SkySphere pass gating without procedural-atmosphere opt-in, explicit `SkySphere::intensity` identity semantics, RenderScene startup-skybox loading through `SkyboxService`, focused builds/tests, CDB/debug-layer audit, RenderDoc skybox proof, and 65-frame allocation-churn proof. Slice D validated Stage 12 static SkyLight diffuse SH consumption in deferred lighting, `ibl-only` / `direct-plus-ibl` service-pass debug routing, stable product-cache state after upload-ticket retirement, focused Vortex tests plus ShaderBake/catalog validation, CDB/debug-layer audit, and IBL-only RenderDoc proof with one static SkyLight draw, no direct/sky scopes, valid processed cubemap and diffuse SH slots, non-black SceneColor, and passing pixel histories. Slice E validated direct-plus-IBL interaction and off/on lifecycle proof with RenderDoc, CDB/debug-layer, valid SkyLight re-publication after re-enable, and zero post-warmup allocation churn. Final focused validation passed `ctest --preset test-debug -R "Oxygen\.(Vortex\.CompositionPlanner|Vortex\.EnvironmentLightingService|Examples\.DemoShell\.EnvironmentSettingsService)" --output-on-failure`; `git diff --check` passed; user visual confirmation approved sun enable/disable/enable, visible SkyLight contribution, SkyBox disable behavior, and SkyBox suppression when procedural atmosphere is active. | No open VTX-M08 closure gap. Accepted future gaps: captured-scene SkyLight, real-time capture, cubemap blend transitions, SkyLight AO/DFAO/bent-normal/cloud occlusion, baked/static-lightmap SkyLight integration, static-cubemap specular reflections, broader reflection probes, volumetric-cloud sky capture, and procedural sun-disk overlay in static cubemap skybox imagery. |
| VTX-FUTURE | Reserved post-baseline families | `future` | Some placeholder directories or shader inventory may exist. | Geometry virtualization, material composition, broader indirect lighting/GI/reflections, VSM, clouds, heterogeneous volumes, water, hair, distortion, light shafts. |

## 5. Open Cross-Milestone Gaps

| Gap | Blocks | Required Resolution |
| --- | --- | --- |
| Captured/real-time SkyLight, broader SkyLight occlusion, baked SkyLight, and reflection-probe/specular IBL remain future features. | Future indirect-lighting/reflection parity beyond the VTX-M08 static specified-cubemap diffuse baseline. | Promote a future indirect-lighting/reflection milestone with LLDs, UE5.7 grounding, runtime proof, and explicit product ownership before claiming broader IBL/reflection parity. |
| Reflection/360-view aerial perspective lacks a runtime resource path. | Future reflection-capture parity. | Implement with the future reflection-capture resource path before claiming reflection/360 AP parity. |

## 6. Update Checklist

Before changing a milestone status:

1. Update the milestone's design and detailed plan if scope changed.
2. Update exactly one row in the milestone ledger.
3. State implementation evidence, validation evidence, and residual gap in that
   row.
4. Run `git diff --check` for docs-only edits, and the relevant focused
   build/test/runtime proof for implementation edits.
