# ED-M08 - Runtime Parity And Standalone Validation

Status: `ready for implementation; validation pending`

Review baseline: 2026-09-15, `9df3b2b88`, plus built-in status fix `267c23c9d`
from the regression reported during this review. This is an implementation plan,
not validation evidence. The user confirmed an automatic check that exits; screenshot comparison
is verification evidence, with no new report tab in the product UI.

## 1. Outcome And Scope

A user selects an authored scene and camera, validates its current published
content, and receives a useful comparison of the embedded preview and a separate
RenderScene process that runs the check and exits. Both paths must render the
exact saved revision and ordered content sources. The editor shows progress and
a concise result; captures and full comparison data remain derived test evidence
for diagnosis and joint review with the user.

M08 includes the PRD's 100-node / 1,000-logical-entry fixture, every required
authored field, controlled rendering, cancellation/recovery, and a usable
on-demand workflow. A single cube or successful native exit is a development
checkpoint, not milestone completion.

Trace: REQ-018/019/022-026/030/037/039-042; SUCCESS-001/003/004/006.

Required contracts:

- [standalone-runtime-validation.md](../lld/standalone-runtime-validation.md):
  request, snapshot, ownership, profile, comparison, UI and failure contracts.
- [runtime-integration.md](../lld/runtime-integration.md), especially sections
  17-19: native compatibility, target lifetime and capture capabilities.
- [content-pipeline.md](../lld/content-pipeline.md), sections 16-19:
  publication/provenance, imported and library inputs, worker ownership, built-ins.
- [property-pipeline.md](../lld/property-pipeline.md) and
  [live-engine-sync.md](../lld/live-engine-sync.md): authoring revisions and
  current-state convergence.
- [property-inspector.md](../lld/property-inspector.md),
  [environment-authoring.md](../lld/environment-authoring.md),
  [material-editor.md](../lld/material-editor.md): required field inventory.

Non-scope: M09 tools, stable multi-viewport operation, physics/scripting/texture
authoring, a new qualification executable or promotion manifest, a generic
validation dashboard, automatic saving/cooking as a side effect of validation,
an in-editor screenshot comparison tab, and M10's complete hardware/performance
qualification.

## 2. Freshness Assessment After M07

| Verified source/evidence | Reuse or required change |
| --- | --- |
| [M07B closeout audit](../validation/ED-M07B-closeout-audit.md) | Authoring, built-ins, publication, imports and native preview are validated at their recorded scope. Do not repeat their implementation or call that standalone image parity. |
| `CookPublicationReceipt`, `CookProvenance`, `CookInputSnapshotCapture` | Reuse saved-byte capture, receipts and per-product freshness. A receipt records the latest operation's inputs while retaining complete roots; it is not a complete saved scene/material snapshot for every preserved product. M08 must resolve the selected scene's closure through provenance. |
| `CookOutputLease.Inspection.cs`, `CookedContentMountService`, `CookedLibraryReadSet` | Reuse asynchronous leases, verified root files and saved priority. Protect all selected libraries as well as project output. Add validation admission to the existing project coordinator; a reader alone currently makes publication report busy. |
| `EditorNativeCompatibilityService`, `NativeSdkMetadata` | Runtime checks ordinary Interop build metadata; cooking checks producers/schemas on demand. Remove the old whole-editor fixed-manifest/probe assumption. Validation adds only its RenderScene/runtime/capture compatibility boundary. |
| `SceneDescriptorGenerator.AddNode` | Emits scene v4 and depth-first node indices, but no author GUID in native node records or returned node map. Add an explicit source-GUID/index map tied to verified descriptor bytes; never match duplicate node names. |
| `RuntimeNodeState`, `RuntimeEnvironmentState`, background observations | Reuse target correlation and actual native reads. Node observations lack complete hierarchy/flags and full scalar material values; stored environment values do not prove effective GPU exposure. Add missing public native observations. |
| Runtime `FrameCaptureSettings`; Graphics `ReadbackManager` | Existing settings configure graphics-tool captures. GPU texture/buffer readback already exists, but there is no managed scene-frame PNG parity contract. Add frame-correlated final-output capture using that engine facility. |
| RenderScene `main_impl.cpp` and `MainModule.cpp` | Normal startup reads demo settings/CVar archives, restores mounts, searches example content, creates a camera and reapplies shell post-processing. Validation needs an early isolated mode, preserving ordinary demo behavior. |
| DemoShell `SceneLoaderService` | Reuse native loading/hydration. Current selection uses the first camera, may synthesize one, normalizes clipping values, and overwrites aspect from the viewport. Strict validation must select the exact authored camera and preserve valid saved values. Scene-only loading already works without a physics sidecar. |
| `SceneEngineSync`, `WorkspacePublicationPreview` | Reuse lifetime checks and coherent full projection. Publication suspension is not the saved-revision capture session: rendering must keep advancing while later authoring delivery is held. |
| `InspectorControlTests.CatalogWorkloadFixture.cs` | Reuse its deterministic content-building approach. Explicitly set Manual exposure/ACES and a camera aimed at the fixture; its EV value alone does not select Manual mode. Count actual logical catalog rows and visible triangles. |
| Status ledger | 07A/07B are complete. M02 window/dock resize and consolidated discovery evidence remain a closure dependency, not a reason to block M08 implementation. |
| Built-in status regression reported during this review | A combined browser query treated cooked built-in companions as authored JSON inputs. Fix `267c23c9d` passes the expanded native exception regression and 449 pipeline tests. The user confirmed Vortex Main/NewScene2 switching without the exceptions after refreshing Debug. Retain this regression; earlier M07 checks did not cover the combined query. |

No native build or rendered parity test was performed for this document review.

## 3. Entry Conditions And Execution Order

- [x] Read M07B's complete audit and inspect the live integration points above.
- [x] Reconcile M08 with PRD section 9's ordinary build-compatibility policy.
- [x] Preserve the accepted numeric comparison thresholds and V0.1 field scope.
- [x] User confirmed Run a check, then exit. No new comparison/report tab.
- [ ] Before final M08 qualification, finish the remaining M02 supported
  single-viewport evidence under M02's existing plan. Do not reopen other
  completed milestones or multi-viewport work.

Implement in the order below. Each numbered slice ends with a self-contained
commit and its focused checks. Native and managed contracts may be split into
successive buildable commits when their dependency requires it. Keep the full
rendered suite for integration/closeout rather than rerunning it after every edit.

### M08.1 - Versioned Contracts And Qualification Fixture

Deliver:

- Version-1 request, identity map, observation, capture manifest, comparison and
  terminal result schemas/DTOs from LLD sections 7-8. Separate native execution
  result from the managed parity verdict. Strictly validate required fields,
  versions, finite numbers, unique IDs, bounded dimensions/frame counts, and
  operation-owned artifact paths.
- A fixture builder using existing test infrastructure: exactly 100 nodes
  (98 geometry, camera, sun), all eleven exposed built-in choices including the
  alias, qualified small imported geometry, shared/distinct materials, hierarchy,
  and exactly 1,000 logical catalog rows. Use explicit Manual EV 9.7, ACES,
  16:9 camera and conventional shadows; assert <=250,000 visible triangles.
- A field-coverage manifest derived from the accepted field tables and checked
  against current property registrations. For each field record its saved path,
  unit/enum conversion, expected native path, meaningful non-default case and
  whether it has a visible-effect case. Include node flags and slot clearing.
- Hand-authored expected examples for quaternion, radians, linear colour,
  identity/index mapping and material enums; they must catch a shared adapter
  error instead of computing both sides with the same implementation.

Touch points: ContentPipeline `Validation/` (new namespace, existing project),
existing WorldEditor UI fixtures, native reusable validation code in DemoShell
with its existing CMake test registration.

Checks: cross-language JSON round trips and rejection corpus; fixture counts and
saved camera/sun/exposure assertions; missing required field fails inventory
coverage. Do not add a separately built probe.

### M08.2 - Verified Saved Inputs, Mount Set And Admission

Deliver:

- A read-only validation preparation service using existing publication recovery
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
- A validation reservation in the existing coordinator: let already-admitted
  work finish; hold later cooks and confirmed mount changes in a visible waiting
  state until validation releases. Do not toggle the user's automatic-cooking
  pause preference or generate a fake cooking run. Another process is excluded
  by the existing output/file leases; ordinary contention is not an exception loop.
- Revalidate admission after waits and handle partial acquisitions in reverse
  order. Do not acquire a nested registration reader while holding a writer.
  Never wait for a cook while retaining the validation reservation/read set.
- On-demand RenderScene discovery and compatible artifact ownership using
  ordinary installed SDK/build receipts. Separate cooking producer provenance
  from runtime/capture executable hashes and optional diagnostic source revision.

Checks: partial material cook after a scene cook; unchanged/no-op publications;
older documents remaining dirty; saved dependency changes; reordered/conflicting
libraries; invalid receipt/recovery journal; missing/corrupt shared files; stale
Inspector cache; cancellation at each acquisition; concurrent cook/mount request;
no first-chance exception flood for expected contention; release after project
close. All tests preserve source/output hashes and document dirty/history state.

### M08.3 - Strict Standalone Loading And Complete Native Observations

Deliver:

- RenderScene `--editor-validation-request` handling before demo settings,
  restored content, startup skybox, camera rig and scene-specific CVar seeds.
  Reject conflicting normal scene/content/profile switches in this mode.
- Verify exact ordered roots and files, then load the exact scene path AND key
  using the native AssetLoader and existing scene hydration. No name search,
  extra mount discovery, repair of invalid saved values, synthetic camera/sun
  or persisted post-process overrides.
- Refactor native reusable loader policies only where needed for exact authored
  camera selection and complete component hydration. Test a second camera and
  a parented camera; preserve all saved camera fields.
- Engine-owned observation DTOs/API for complete scene enumeration, parent/index
  relationships, flags, transforms, resolved geometry/slot keys and full required
  material values, lights, environment/background and effective view settings.
  Share native observation semantics between Interop and RenderScene, without
  exposing engine-private types to managed code.
- Report actual mounted/winning sources and required load completion/failures.
  The native renderer must not read expected values to populate observations or
  patch the loaded scene. An identity map labels observations; it cannot create
  missing native nodes.
- Atomic native terminal result with exit codes and run-local output. A zero exit
  does not authorize a managed success result.

Checks: exact-load smoke with example content inaccessible and stale demo
settings present; wrong key/path pair; duplicate names; missing dependency;
second authored camera; non-default aspect/clipping; no camera; extra native
node; wrong material enum/scalar; zero exit with missing/truncated artifacts.
Keep normal RenderScene scene-only and existing demo loading tests passing.

### M08.4 - Frame-Complete Capture And Deterministic Render Profile

Deliver:

- Engine/Vortex-owned asynchronous capture of the final scene output before UI
  composition, after post-processing/background/transparency composition.
  Use Graphics `ReadbackManager` with proper copy/barrier/fence ownership and
  row-pitch/format conversion. Preserve GPU resources until readback drains.
- A native capture result bound to run, scene, view generation, profile, scene
  frame, dimensions, output encoding and actual effective renderer settings.
  Runtime/Interop adapt that capability; no raw framebuffer pointer contract.
- Reset temporal/exposure/view histories and apply fixed scene timestep through
  supported engine configuration. Frame zero begins only when required content,
  render uploads, camera and profile are ready. Count completed scene frames,
  not process frames or requests queued before readiness.
- Preserve saved camera projection including aspect; the 1920x1080 target does
  not authorize rewriting the authored camera. Both paths use the same
  projection-to-target policy and record it.
- CPU/GPU observations of effective auto-exposure at frames 120/240/600; reading
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

- One capture owner tied to the active project/run/document/scene/view generation.
  Capture view intent, finish any active edit gesture, pin the verified saved
  projection and camera/profile, then hold all later scene mutation delivery.
  Rendering and authoring continue. The viewport clearly indicates capture;
  navigation/profile commands cannot move its camera.
- Integrate the hold with all `SceneEngineSync` paths, asset-demand completions
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
  semantic/image comparison -> atomic terminal result. Expected state remains
  editor-owned; native paths provide independently observed values.
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

### M08.7 - Scene Command And Concise Validation Feedback

Deliver the LLD section 9 interaction contract:

- Validate in Standalone stays discoverable for an active authored scene.
  Explain missing Save/Cook/camera/runtime prerequisites on invocation; avoid
  an unexplained disabled menu. Multiple cameras require explicit choice.
- Named Save actions use existing document save/conflict workflows. Cook actions
  use the existing coordinator and Cooking panel. Revalidate after completion;
  no hidden Save or stale launch.
- Show concise phase/Cancel and captured-revision status within 100 ms.
  The terminal result names the scene and outcome, with actionable failures by
  node/asset/field. Keep technical output and original images in the operation's
  artifact directory, reachable from its existing result/details action.
- Reuse DroidNet compact controls, status icons/semantic colours and existing
  document/result infrastructure. No project-wide validation dashboard, second
  cooking scheduler, screenshot comparison tab or global-log scavenging.
- UI navigation must respect M08.5: reporting progress must not activate another
  document and tear down the viewport being captured. Switching away during
  capture cancels it; after capture releases, standalone work can finish without
  reactivating an old scene.

Checks: real scene menu action, blocked/dirty/conflict paths, camera choice,
Cancel and terminal feedback, artifact/details access, one-pane scene
lifetime, keyboard/automation names, narrow layout and themes/scaling. Use
existing packaged UI projects and automation peers; do not restart the external
computer-use helper identified in M07B's isolation test.

### M08.8 - Field/Rendered Qualification And Closeout

- Run every field-manifest case through real command/save/cook/embedded/native
  paths. Add bounded visual cases for camera, hierarchy, every built-in and
  imported geometry, material replacement/None/Default, opacity/mask/blend,
  light/sun, atmosphere, exposure, tone mapping and background preservation.
- Verify full static fixture, all tone mapper/exposure modes and auto-exposure
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
- Jointly test the real editor action with the user: start the check on the
  fixture, observe capture feedback and standalone launch/exit, inspect the
  concise result, then repeat a changed material/background and Cancel case.
  Review original images, differences, metrics and semantic mismatches as test
  evidence outside the everyday editor UI; preserve hashes and exact commands
  in one M08 validation record. Give the user time to test before closeout.
- Prepare the affected-code analyzer/IDE pass once for commit preparation, fix
  relevant diagnostics, and commit stable code/test/doc slices. Update one
  M08 validation ledger row only after every gate passes.

## 4. Verification And Build Placement

Managed unit tests extend ContentPipeline, Runtime, World and SceneExplorer
tests according to ownership. Rendered interactions extend the existing
WorldEditor packaged UI tests; follow their launch/test-loader setup.
Native protocol/loader tests use DemoShell's CMake/GTest pattern; native
observation/capture/readback tests live with Scene/Engine/Vortex/Graphics owners.

Use MSBuild.exe and VSTest for editor work; CMake build/install and CTest in
`projects/Oxygen.Engine/out/build-ninja` for native work. Engine build/install is
authorized in this workflow. Use the existing tree, native `--parallel 1`, and
serialize Debug/Release test runs that share fixture paths. Rebuild Interop after
changing/installing its SDK inputs. No probe or whole-editor qualification build
is an extra precondition to running validation.

Doc-only planning review requires link/consistency/diff checks, not an engine or
editor build. Exact newly added target/filter names are recorded when they exist,
rather than presenting proposed tests as already runnable commands.

## 5. Exit Gates

- [ ] M02 supported single-viewport evidence is recorded; 07A/07B remain validated.
- [ ] Exact project/scene/camera loading works with example content unavailable.
- [ ] Current saved expectations and complete mount/provenance proofs survive
  partial/no-op cooking and library priority; invalid inputs cannot launch.
- [ ] Complete native semantics pass for every required field, including full
  material values, hierarchy, all cameras and effective render settings.
- [ ] Static and auto-exposure image cases pass unchanged tolerances.
- [ ] Saved-revision capture is isolated from later edits and releases into the
  coherent current preview; no late callback restores obsolete state.
- [ ] Cancel/timeout/crash/close/restart/resize and concurrent cook/mount work
  preserve files, dirty/history state, ownership and responsiveness.
- [ ] Real user entry, prerequisite recovery, camera selection and useful
  per-run comparison/output surfaces pass UI checks.
- [ ] Relevant analyzer/IDE checks are clean; original evidence and user review
  are recorded for the exact tested build/publication/profile.
- [ ] IMPLEMENTATION_STATUS contains one M08 validation row; M09/M10 are not
  claimed complete by this work.
