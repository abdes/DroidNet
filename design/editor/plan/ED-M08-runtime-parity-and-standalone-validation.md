# ED-M08 - Runtime Parity And Standalone Validation

Status: `development-only revision approved; property-scope review and implementation pending`

On 2026-09-15 the user made the **entire validation workflow development-only**.
This replaces the shipped scene command, normal RenderScene validation CLI, and
no-separate-executable constraint. The earlier review baseline was `9df3b2b88`
plus built-in status fix `267c23c9d`; that history is not evidence of the revised
workflow's implementation or qualification.

Complete and accept the researched
[V0.1 authoring-scope review](../review/ED-M08-v01-authoring-scope.md) before
freezing fields/protocol/fixtures. Current registrations are review evidence,
not automatic approval of every exposed field.

V0.1 does not preserve backward compatibility: migrate useful legacy intent to
the new canonical model and remove compatibility aliases/fallback paths instead
of qualifying both. Preserve content-integrity backups/recovery and ordinary
saved-revision ownership. Review properties interactively one at a time with
context, options, industry practice and future rationale; category/package
approval is not implied.

## 1. Outcome And Scope

An opt-in development test/tool target compares the exact saved/published editor
scene and camera in embedded and standalone native rendering. It runs a check,
writes actionable evidence, and exits. Development/UI tests and joint review use
real authoring, Save and Cook surfaces. Normal Debug/Release editor, RenderScene,
SDK, install and package graphs contain no validation UI/CLI/protocol, metrics,
fixtures, or qualification-only instrumentation.

The workload remains exactly 100 nodes / 1,000 logical catalog entries and at
most 250,000 visible triangles. Required fields come from the accepted scope
review and retain the existing semantic/image gates. A small scene or successful
process exit is a smoke checkpoint, not milestone completion.

Trace: REQ-018/019/022-026/030/037/039-042; SUCCESS-001/003/004/006.

Required contracts:

- [standalone-runtime-validation.md](../lld/standalone-runtime-validation.md):
  development ownership/isolation, snapshots, protocol, capture and comparisons.
- [V0.1 scope review](../review/ED-M08-v01-authoring-scope.md): researched
  professional workflow and field acceptance before freeze.
- [runtime-integration.md](../lld/runtime-integration.md), sections 17-19:
  compatibility, target lifetime and existing runtime/readback owners.
- [content-pipeline.md](../lld/content-pipeline.md), sections 16-19:
  publication, provenance, imports/libraries, workers and built-ins.
- [property-pipeline.md](../lld/property-pipeline.md),
  [live-engine-sync.md](../lld/live-engine-sync.md),
  [property-inspector.md](../lld/property-inspector.md),
  [environment-authoring.md](../lld/environment-authoring.md), and
  [material-editor.md](../lld/material-editor.md): behavior and scope-review
  inputs; reconcile their required tables with the accepted decision.

Non-scope: shipped validation entry/UI or protocol; M09 tools; multi-viewport
stability; new physics/scripting/texture authoring workflows; a new production
validation domain; whole-editor approval manifests; implicit Save/Cook;
generic dashboards; M10's full hardware/performance qualification. A separate
opt-in development executable is permitted when necessary.

## 2. Freshness Assessment After M07

| Verified source/evidence | Reuse or required change |
| --- | --- |
| [M07B closeout audit](../validation/ED-M07B-closeout-audit.md) | Authoring, built-ins, publication, imports and native preview are validated at their recorded scope. Do not repeat their implementation or call that standalone image parity. |
| `CookPublicationReceipt`, `CookProvenance`, `CookInputSnapshotCapture` | Reuse saved-byte capture, receipts and per-product freshness. A receipt records the latest operation's inputs while retaining complete roots; it is not a complete saved scene/material snapshot for every preserved product. M08 must resolve the selected scene's closure through provenance. |
| `CookOutputLease.Inspection.cs`, `CookedContentMountService`, `CookedLibraryReadSet` | Reuse asynchronous leases, verified root files and saved priority. Protect all selected libraries as well as project output. Add validation admission to the existing project coordinator; a reader alone currently makes publication report busy. |
| `EditorNativeCompatibilityService`, `NativeSdkMetadata` | Reuse ordinary Interop/runtime and producer/schema proofs. The separate development driver carries its own metadata; its absence never blocks product startup, Save or Cook. |
| `SceneDescriptorGenerator.AddNode` | Emits scene v4 and depth-first node indices, but no author GUID in native node records or returned node map. Add an explicit source-GUID/index map tied to verified descriptor bytes; never match duplicate node names. |
| `RuntimeNodeState`, `RuntimeEnvironmentState`, background observations | Reuse real native reads. Missing deep hierarchy/material/effective GPU observations needed solely for qualification belong in opt-in telemetry/adapters, not shipped instrumentation. |
| Runtime `FrameCaptureSettings`; Graphics `ReadbackManager` | Reuse real GPU readback. Qualification PNG/correlation, telemetry and scheduling belong to the opt-in development adapter, not normal Runtime/Interop builds merely because they are reusable. |
| RenderScene `main_impl.cpp` and `MainModule.cpp` | Normal demo startup restores state and substitutes preview content. Preserve it; the development driver reuses loader/render capabilities without a normal RenderScene validation flag. |
| DemoShell `SceneLoaderService` | Reuse native loading/hydration. Current selection uses the first camera, may synthesize one, normalizes clipping values, and overwrites aspect from the viewport. Strict validation must select the exact authored camera and preserve valid saved values. Scene-only loading already works without a physics sidecar. |
| `SceneEngineSync`, `WorkspacePublicationPreview` | Reuse lifetime and full-projection contracts. Qualification-only capture hold/hooks belong to an opt-in test-host adapter. Rendering advances while that adapter holds later delivery. |
| `InspectorControlTests.CatalogWorkloadFixture.cs` | Reuse its deterministic content-building approach. Explicitly set Manual exposure/ACES and a camera aimed at the fixture; its EV value alone does not select Manual mode. Count actual logical catalog rows and visible triangles. |
| Status ledger | 07A/07B are complete. M02 window/dock resize and consolidated discovery evidence remain a closure dependency, not a reason to block M08 implementation. |
| Built-in status regression reported during this review | A combined browser query treated cooked built-in companions as authored JSON inputs. Fix `267c23c9d` passes the expanded native exception regression and 449 pipeline tests. The user confirmed Vortex Main/NewScene2 switching without the exceptions after refreshing Debug. Retain this regression; earlier M07 checks did not cover the combined query. |

No native build or rendered parity test was performed for this document review.

## 3. Entry Conditions And Execution Order

### Accepted history and revised entry gate

The user initially authorized M08 after the native content, preview-sun and
tooling groundwork. Maintained content and four original RenderScene sources
were refreshed; sunlight on/off and authored-light preservation evidence remains
in the [preview-sun record](../../../projects/Oxygen.Engine/design/vortex/plan/renderscene-preview-sun.md).
The broad example/interactive-import matrix was not claimed complete. This is
useful groundwork, not M08 parity closure.

The subsequent development-only instruction paused provisional production
ContentPipeline/protocol work and supersedes that placement. Preserve review
drafts as drafts; do not restore them to production dependencies.

- [x] Read M07B's audit and inspect current integration points.
- [x] Preserve ordinary production SDK/build compatibility; no qualification
  manifest is required for startup, authoring, Save or Cook.
- [x] Preserve numeric/image/ownership gates and the check-then-exit intent.
- [x] Entire workflow is development-only; a separate opted-in driver is allowed.
- [x] Captured-sky diffuse and specular image-based lighting is required.
- [ ] Complete and accept the V0.1 scope review and reconcile PRD/authoring tables
  through explicit individual property decisions before freezing coverage or
  protocol payloads. Do not treat the captured-sky decision as package approval.
- [ ] Establish opt-in targets and prove exclusion from normal Debug/Release
  references, resources, initializers, builds, SDK/install and packages.
- [ ] Complete M02's remaining supported one-viewport evidence before M08
  closes; do not reopen other milestones or multi-viewport work.

After those entry decisions, implement the slices below with focused checks and
self-contained commits. Native and managed development contracts may land in
successive buildable slices. Reserve the full rendered suite for integration
and closeout, rather than repeating it after each edit.

### Build and code ownership

Canonical schemas/protocol readers and the native development driver belong
under `Oxygen.Engine/tools/validation`, outside Examples and production modules.
Managed preparation/comparison/fixture code belongs in an opt-in
`tests/validation` or existing test-support target. Reuse a test host where it
can isolate this work; add a development project only when necessary, not a new
production domain.

Native opt-in is off by default, independent of Debug/Release and ordinary unit
test settings. The managed target is absent from production references,
resources, initializers, default solution builds, and packages. Use a separate
development-validation output/staging tree. No qualification variant may replace
normal editor/RenderScene binaries or the installed SDK. Examples may supply
loader/library code as dependencies; they do not own canonical editor schemas.

'Reusable' alone does not justify shipping instrumentation. Deep observations,
exposure telemetry, capture hold/hooks, checkpoint scheduling, protocol and
metrics needed only for qualification compile/link solely in opt-in targets.
Production changes require an independent responsibility: real rendering,
authoring mutation, loading, or ordinary runtime behavior, under its real owner.

### M08.1 - Versioned Contracts And Qualification Fixture

Deliver:

- Version-1 request, identity map, observation, capture manifest, comparison and
  terminal result schemas/DTOs from LLD sections 7-8. Separate native execution
  result from the managed parity verdict. Strictly validate required fields,
  versions, finite numbers, unique IDs, bounded dimensions/frame counts, and
  operation-owned artifact paths.
- A fixture builder using existing test infrastructure: exactly 100 nodes
  (98 geometry, camera, sun), all approved canonical built-in choices,
  qualified small imported geometry, shared/distinct materials, hierarchy,
  and exactly 1,000 logical catalog rows. Use explicit Manual EV 9.7, ACES,
  16:9 camera and conventional shadows; assert <=250,000 visible triangles.
  The historical eleven-choice catalog does not create a legacy-alias
  preservation requirement; reconcile catalog scope with canonical decisions.
- A field-coverage manifest derived from the accepted scope review and
  reconciled field tables. Audit registrations against that decision rather than
  automatically including every exposed field. For each field record its saved path,
  unit/enum conversion, expected native path, meaningful non-default case and
  whether it has a visible-effect case. Include node flags and slot clearing.
- Hand-authored expected examples for quaternion, radians, linear colour,
  identity/index mapping and material enums; they must catch a shared adapter
  error instead of computing both sides with the same implementation.

Touch points: the opt-in managed test/tool target and reusable UI test support;
canonical contracts and native driver/tests under `Oxygen.Engine/tools/validation`.
Production ContentPipeline, Runtime, WorldEditor, Interop and Examples do not
reference qualification schemas or orchestration sources.

Checks: cross-language JSON round trips and rejection corpus; fixture counts and
saved camera/sun/exposure assertions; missing required field fails inventory
coverage for the approved scope; normal build/install/package exclusion. A
separately built development driver is allowed and remains explicitly opt-in.

### M08.2 - Verified Saved Inputs, Mount Set And Admission

Deliver:

- A development-owned read-only preparation adapter using existing publication recovery
  verification, provenance, saved document reads and source fingerprints.
  Select the scene closure, not every unrelated document in the project.
- Capture current saved bytes privately, check their product fingerprints against
  published provenance, and build expectations independently of cooked output.
  Use a short project admission interval while capturing identities and readers.
  Finish/release document read gates before authoring resumes.
- Node GUID-to-descriptor-index mapping plus authored URI-to-cooked key/winning
  root mapping. Reuse descriptor traversal, but validate its complete mapping
  with duplicate names, hierarchy, multiple cameras and preserved outputs.
- Record receipt hash/publication ID, per-product proofs, full ordered mount-set
  fingerprint and all protected root files. Native Inspector and its derived
  cache supply opaque library dependencies, never managed binary decoding.
- A development adapter participating in existing coordinator admission: let already-admitted
  work finish; hold later cooks and confirmed mount changes in a visible waiting
  state until validation releases. Do not toggle the user's automatic-cooking
  pause preference or generate a fake cooking run. Another process is excluded
  by the existing output/file leases; ordinary contention is not an exception loop.
- Revalidate admission after waits and handle partial acquisitions in reverse
  order. Do not acquire a nested registration reader while holding a writer.
  Never wait for a cook while retaining the validation reservation/read set.
- Explicit development-driver selection and artifact ownership, with its separate
  build metadata and selected ordinary runtime/SDK receipts. Separate cooking producer provenance
  from runtime/capture executable hashes and optional diagnostic source revision.

Checks: partial material cook after a scene cook; unchanged/no-op publications;
older documents remaining dirty; saved dependency changes; reordered/conflicting
libraries; invalid receipt/recovery journal; missing/corrupt shared files; stale
Inspector cache; cancellation at each acquisition; concurrent cook/mount request;
no first-chance exception flood for expected contention; release after project
close. All tests preserve source/output hashes and document dirty/history state.

### M08.3 - Development Driver, Exact Loading And Native Observations

Deliver:

- An opt-in native test/tool entry accepting the immutable request. Initialize
  no restored demo content, skybox, camera rig, preview sun or scene-specific
  CVar defaults. Normal RenderScene gains no validation request flag.
- Exact ordered-root/file verification and exact scene path AND key loading via
  production AssetLoader and real hydration. No fuzzy search, extra discovery,
  repaired invalid saved values, synthetic content or post-process substitutions.
- Exact authored camera selection and preservation of every approved camera
  field, including a second/parented camera. Reuse real loader policies; any
  production fix must have an independent loading responsibility.
- Development-native observation adapters for the approved inventory: complete
  hierarchy/index/flags, stored/effective transforms, geometry/slots/materials,
  lights, environment/background and effective view settings. Qualification-only
  deep observations/telemetry stay in opt-in sources/hooks, not normal engine
  or Interop APIs.
- Actual winning mounts and dependency/readiness failures. The driver never
  reads expected numeric values to fill observations or patch the scene.
  Identity maps label actual nodes; they cannot create missing native nodes.
- Atomic native execution result with LLD exit codes and artifact hashes.
  Native success remains separate from the managed parity verdict.

Checks: exact load with example content unavailable and stale demo settings;
wrong key/path; duplicate names; missing dependency; second/parented camera;
non-default aspect/clipping; no camera; extra native node; wrong material value;
zero exit with missing/truncated evidence. Keep ordinary RenderScene loading and
production loader tests passing without validation CLI/resources.

### M08.4 - Frame-Complete Capture And Deterministic Render Profile

Deliver:

- A development-native capture adapter for final scene pixels before UI, after
  post-processing/background/transparency. Use production Graphics
  `ReadbackManager` and real renderer outputs with copy/barrier/fence ownership
  and row-pitch/format conversion. Retain resources until readback drains; the
  qualification capture protocol and encoding orchestration stay opt-in.
- A native capture result bound to run, scene, view generation, profile, scene
  frame, dimensions, output encoding and actual effective renderer settings.
  Development-host adapters bridge existing Runtime/Interop capabilities.
  Qualification-only bridge/hooks compile solely in the development target;
  no raw framebuffer-pointer contract.
- A development-only scheduler resets temporal/exposure/view histories and
  applies fixed scene timestep through supported engine configuration. Frame zero begins only when required content,
  render uploads, camera and profile are ready. Count completed scene frames,
  not process frames or requests queued before readiness.
- Preserve saved camera projection including aspect; the 1920x1080 target does
  not authorize rewriting the authored camera. Both paths use the same
  projection-to-target policy and record it.
- Opt-in CPU/GPU exposure telemetry at frames 120/240/600; reading
  the authored Auto enum or a CPU placeholder is insufficient. Capture both
  image sets at those frames.
- Controlled output encoding independent of desktop HDR/DPI/compositor state.
  Record effective CVars, adapter/driver and shader/runtime fingerprints.

Checks: readback completion vs queued acceptance; row pitch/channel order;
device/readback failure; capture cancellation with an in-flight GPU copy; view
destroy/resize and runtime replacement; identical repeated captures after reset;
manual/auto exposure frame checkpoints. No PIX/RenderDoc dependency for PNG
capture and no test that claims headless rendering proves images.

### M08.5 - Bounded Embedded Saved-Revision Session

Deliver:

- One opt-in development capture owner tied to project/run/document/scene/view generation.
  Capture view intent, finish any active edit gesture, pin the verified saved
  projection and camera/profile, then hold all later scene mutation delivery.
  Rendering and authoring continue. Development-host feedback identifies
  capture; its adapter suppresses navigation/profile changes for that target.
  No hold UI/hook/state machine ships solely for qualification.
- The development adapter covers all `SceneEngineSync` delivery, asset-demand completions
  and publication delivery, not only scalar property updates. Prevent entry
  while an earlier projection/publication still owns conflicting work.
- Release immediately after embedded observation/image completion. Restore only
  valid current view intent and resynchronize one coherent latest authoring
  snapshot, superseding older pending work. Do not replay an obsolete snapshot,
  change history/dirty flags, save camera settings or resume a closed scene.
- Activation, scene/document close, viewport destruction, runtime fault/restart
  and project replacement invalidate capture callbacks. Same-view layout resize
  cannot alter the fixed capture target. Transfer teardown to its actual native
  owner if a bounded wait expires.

Checks: edit/Undo/Redo, create/delete/reparent, slot assignment, environment change
and repeated saves during warm-up; navigation input; resize; switching to scene,
material and inspection documents; cancellation/failure at each phase; late
callbacks after restart. Assert both the saved captured image/observations and
the newer preview after release. Test the native resource lifetimes, not just
mock return values.

### M08.6 - Coordinator, Comparisons And Result Integrity

Deliver:

- Compose preparation -> embedded capture/release -> child launch/drain ->
  semantic/image comparison -> atomic result in the development target.
  Expectations come from saved authoring; native paths observe independently.
- Run the child through the existing structured process runner/owned Windows
  job and I/O-drain model. Extend names/contracts only where necessary; never
  terminate unrelated RenderScene instances. Cancellation/timeout must retain
  artifacts, native leases and drain ownership until readers actually finish.
- Compare expected vs embedded, expected vs standalone, then embedded vs
  standalone images. Distinguish original authored expectations from opaque
  cooked-library expectations and intentional source overrides.
- Apply the unchanged LLD numeric rules and strict required-field completeness.
  Reject wrong operation/profile/root/build/frame identities, invalid image
  dimensions, NaN/Infinity, absent checkpoints and partial JSON.
- Keep original PNGs plus a derived difference image and per-field mismatches.
  The current/historical label is independent of pass/fail: a newer edit does
  not rewrite the result of the captured saved revision.
- Release the project reservation and all output/native/artifact handles on
  every terminal path, including failure before launch and a child that exits
  while a descendant still owns redirected output.

Checks: numerical boundary tests; opposite-sign quaternions; enum/ID mismatch;
all-black/missing geometry false positives; hidden render override; altered
artifacts; changed source during execution; nonzero exit with partial evidence;
timeout/crash/forced cancellation; same-project queued automatic work resumes;
old result does not become current after project/source/mount replacement.

### M08.7 - Development Entry And Real Authoring UI Coverage

Deliver revised LLD section 9:

- Explicit development test/tool invocation with scene/camera context,
  prerequisite feedback, check execution and exit. No shipped Validate in
  Standalone menu/command/CLI, validation panel, preference, metrics viewer,
  schema resource or module initializer.
- Real editor UI tests for authoring, hierarchy/material changes, Save/conflicts
  and Cook. Use existing actions/coordinator, name participating documents,
  choose among authored cameras, and revalidate after explicit recovery.
  Do not replace product workflows with hidden file edits or toy controls.
- Concise development progress/Cancel feedback within 100 ms and actionable
  results by scene/node/asset/field, captured revision and artifact paths. Any
  test-host UI uses compact accessible controls only in that opt-in host.
- Correct capture lifetimes during UI tests: feedback does not activate another
  document; switching away during capture cancels it; independent child work
  after release cannot reactivate a previous scene.

Checks: development invocation, real blocked/dirty/conflict/cooking paths,
camera selection, cancel/results/evidence, and ordinary one-pane document
lifetime. Exercise actual authoring accessibility/layout/themes where relevant.
Normal product menus/resources/builds/packages contain no validation feature.

### M08.8 - Field/Rendered Qualification And Closeout

- Run every approved field case through real authoring/Save/Cook and
  development-driven embedded/native paths. Add bounded visual cases for camera, hierarchy, every built-in and
  imported geometry, material replacement/None/Default, opacity/mask/blend,
  light/sun, atmosphere, exposure, tone mapping and background preservation.
- Verify the full static fixture, every approved tone mapper/exposure mode and auto-exposure
  checkpoints. Include bright background with atmosphere disabled and foreground
  transparency; this must not regress M07A's display-colour decision.
- Re-run matched Release embedded/standalone images on the same recorded adapter
  and driver. Record CPU/RAM, GPU/VRAM, driver, OS/runtime versions and fixture/
  artifact hashes from PRD section 9. Debug covers diagnostics, protocol and
  ownership integration.
  M10 retains its full release performance/hardware gate; M08 measures its own
  <=100 ms feedback and responsive/cancellable capture workflow.
- Complete M02's remaining one-pane resize/discovery evidence in its own ledger
  before claiming M08 complete. Retain M07 results at their original scope.
- Jointly test real authoring and the opt-in runner with the user: make changes,
  explicitly Save/Cook, invoke qualification, observe capture/native driver
  launch/exit and results, then repeat a material/background change and Cancel.
  No product validation action is required or introduced.
  Review original images, differences, metrics and semantic mismatches as test
  evidence outside the everyday editor UI; preserve hashes and exact commands
  in one M08 validation record. Give the user time to test before closeout.
- Prepare the affected-code analyzer/IDE pass once for commit preparation, fix
  relevant diagnostics, and commit stable code/test/doc slices. Update one
  M08 validation ledger row only after every gate passes.

## 4. Verification And Build Placement

Qualification schemas/readers, fixtures/catalogs, comparisons, orchestration,
deep observations/telemetry and capture adapters are tested through explicit
development targets. Managed UI tests reuse actual editor parts/test hosts.
Production defect tests stay with their real Scene/Content/Engine/Vortex/Graphics
or editor owner; qualification-only work is not a production dependency.

Use MSBuild/VSTest and native CMake/CTest with opt-in target configurations.
Serialize native builds and tests sharing GPU/fixture/output resources. Build
and stage development artifacts in an isolated tree, retaining ordinary SDK
fingerprints as inputs. Do not overwrite normal editor/RenderScene binaries,
SDK installation or shipping schemas. Record exact target/filter names only
when they exist; this plan does not advertise an implemented runner.

Explicit negative checks for normal **Debug and Release** build/install/package:

- Project references, embedded/content resources and module initializers contain
  zero development-validation dependencies or payloads.
- Default CMake/MSBuild/solution graphs do not compile/link qualification drivers,
  hooks, telemetry, fixture generation, protocol readers/schemas, metrics or UI.
- Packages and normal SDK/install inventories contain none of those artifacts.
- Normal editor authoring/Save/Cook/preview and ordinary RenderScene loading run
  successfully without development targets or their output present.

A separate opt-in driver is permitted, not a product startup prerequisite or a
whole-editor promotion manifest. Reusability alone does not justify shipping
instrumentation. Doc-only review needs link/consistency/diff checks, not builds
or UI launches.

## 5. Exit Gates

- [ ] M02 supported single-viewport evidence is recorded; 07A/07B remain validated.
- [ ] Exact project/scene/camera loading works with example content unavailable.
- [ ] Current saved expectations and complete mount/provenance proofs survive
  partial/no-op cooking and library priority; invalid inputs cannot launch.
- [ ] Researched V0.1 scope is accepted/reconciled before field freeze; complete
  semantics pass for every approved field, hierarchy/camera and effective setting.
- [ ] Static and auto-exposure image cases pass unchanged tolerances.
- [ ] Saved-revision capture is isolated from later edits and releases into the
  coherent current preview; no late callback restores obsolete state.
- [ ] Cancel/timeout/crash/close/restart/resize and concurrent cook/mount work
  preserve files, dirty/history state, ownership and responsiveness.
- [ ] Explicit development entry, real authoring/Save/Cook UI recovery, camera
  selection and useful development results pass tests and joint review.
- [ ] Normal Debug/Release references, resources, initializers, compile/link
  artifacts, SDK/install and packages contain zero development-validation payloads.
- [ ] Development output is isolated; normal editor and RenderScene run without it.
- [ ] Relevant analyzer/IDE checks are clean; original evidence and user review
  are recorded for the exact tested build/publication/profile.
- [ ] IMPLEMENTATION_STATUS contains one M08 validation row; M09/M10 are not
  claimed complete by this work.
