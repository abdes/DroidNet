# Standalone Runtime Validation LLD

Status: `reviewed after ED-M07B; ED-M08 implementation and validation pending`

Review baseline: 2026-09-15, `9df3b2b88`. Section 9 records the user's choice
to run an automatic check and exit; the ownership, data and comparison contracts guide
the implementation sequence in the [M08 plan](../plan/ED-M08-runtime-parity-and-standalone-validation.md).

## 1. Purpose

Prove that the exact saved/published editor scene loads through native runtime
content APIs and renders the same authored content as embedded preview.
Validation reads authored/published content and writes only derived evidence.
Save and Cook remain separate, explicit workflows when preparation needs them.

## 2. Traceability And Related Contracts

GOAL-001/003; REQ-018/019/022-026/030/037/039-042;
SUCCESS-001/003/004/006.

- [PRD](../PRD.md), sections 8-10: field scope, build compatibility and workload.
- [runtime-integration.md](runtime-integration.md), sections 17-19: managed/native
  boundaries, target lifetimes and capture completion.
- [content-pipeline.md](content-pipeline.md), sections 16-19: snapshots,
  publication/readers, imported/library dependencies, native worker lifetime.
- [property-pipeline.md](property-pipeline.md) and
  [live-engine-sync.md](live-engine-sync.md): current-revision convergence.
- [property-inspector.md](property-inspector.md),
  [environment-authoring.md](environment-authoring.md) and
  [material-editor.md](material-editor.md): required authored fields.

## 3. Verified Baseline And Missing Capabilities

M07A/07B now provide complete scene v4 environment/background cooking, all eleven
built-in choices, typed source/cooked identities, per-product provenance, safe
publication, ordered library mounts, native Inspector caches and ordinary
build-compatibility checks. Their [closeout audit](../validation/ED-M07B-closeout-audit.md)
does not claim standalone rendered parity.

RenderScene loads native assets through DemoShell's scene loader. Its normal
startup reads persisted demo/render settings, discovers/restores content and
uses fuzzy scene selection. The loader selects the first camera and may create
one or rewrite its aspect/clipping. M08 adds a strict validation mode using the
same runtime loading/rendering capabilities.

Current runtime observations read native node/environment/background values.
They do not enumerate a complete hierarchy or all scalar material fields and
do not observe GPU auto-exposure. Existing frame-capture settings configure
PIX/RenderDoc; Graphics has GPU readback primitives but no editor parity-PNG
completion contract. These are explicit M08 implementation tasks.

## 4. Target Workflow

1. Invoke Validate in Standalone for the active authored scene. Resolve its
   explicit authored camera; explain unavailable prerequisites with actions.
2. If participating documents need Save or the selected closure needs Cook,
   use those existing workflows only after the named user action. Recheck the
   complete preflight afterward. Unrelated dirty documents do not block.
3. Admit validation through the project's existing operation coordinator.
   Verify committed publication, capture the selected saved dependency closure
   and ordered mounts, and acquire their read/native artifact ownership.
4. Build expected authored state and source-to-cooked identity mapping from
   verified saved inputs. The live runtime and cooked observations are not the
   expected authored-state oracle.
5. Acquire a bounded embedded capture session, apply that saved projection and
   camera, disable overlays/navigation for its viewport, and capture completed
   scene frames while newer authoring delivery stays pending.
6. Release the embedded session and converge to current authoring/view intent.
   Launch the matched RenderScene with the exact immutable request. It loads
   only the allowed roots/scene and produces native observations and images.
7. Drain the child and compare both native observations to expectations, then
   compare the images. Finalize a structured result and expose original evidence.
8. Release all reservations/readers after actual native reads and cleanup end.
   Newer edits remain dirty; results keep the captured revision and become
   historical without changing their original verdict.

## 5. Ownership And Dependency Direction

| Owner | Responsibility |
| --- | --- |
| WorldEditor | Scene/camera context, explicit save/cook recovery, capture-aware scene-sync hold, view intent and result UI. |
| ContentPipeline | UI-independent request/expected-state preparation, provenance/mount admission, owned process, comparison and derived result persistence. |
| Runtime | Managed target validation, native observation/capture capabilities and completion; no authoring DTOs or cook policy. |
| Interop | Narrow adapters for engine public capabilities; no independent renderer/asset policy. |
| Engine Scene/Content/Engine/Vortex/Graphics | Actual scene/asset observations, readiness, deterministic timing, final render output and GPU readback ownership. |
| RenderScene / reusable DemoShell code | Strict request runner, native scene hydration, camera/profile selection and native artifacts. |
| Editor composition root | Registers the WorldEditor capture adapter with the ContentPipeline workflow. |

ContentPipeline must not add a dependency on WorldEditor or Runtime to orchestrate
capture. Define a managed capture-client contract in its validation namespace;
WorldEditor implements it using its scene/document services and Runtime.
Runtime capability DTOs stay runtime-owned. Use existing test projects/native
CMake registration; no validation probe executable, new authoring domain or
generic validation center is required.

## 6. Admission, Provenance And Immutable Inputs

### 6.1 Selected publication and saved state

The validation identity comprises publication receipt hash/ID, selected product
proofs, saved-input hashes/revisions, exact ordered mounts, runtime/build proof
and profile. A session-local content-revision counter or latest cook ID alone
does not identify the complete scene.

A partial cook's receipt retains full roots but only that operation's captured
input set. Resolve the scene and every dependency through per-product provenance
and current saved-input discovery. Capture saved bytes with existing document/read
gates, verify their fingerprints against the published products, and retain the
private copy for expected-state generation. Recheck dirty state during capture.

If the saved inputs no longer reproduce the published product identity, report
Needs cooking. If receipt/provenance is missing/corrupt or recovery is incomplete,
use the existing recovery/repair workflow; do not infer old authoring values from
cooked bytes or depend on a cleaned-up previous cook operation directory.
Validation does not need an archive of every historical source revision.

Expected node GUIDs, names, parent membership, flags and values come from saved
scene data. Pair them with the descriptor's deterministic node indices and
verified descriptor/output identity. Names are not keys; duplicate names and
reparenting are required cases. Return/record the map from the generator or a
shared traversal contract without adding author GUIDs to raw binary structures
in managed code. Assert that native enumeration has neither missing nor extra
authored nodes.

### 6.2 Project and library roots

Use the same saved low-to-high mount order as the preview: last mounted wins.
A new library's default priority remains below project output and above older
libraries; deliberate overrides retain their saved order. Validation must not
sort roots alphabetically, rediscover all directories or invent another priority.

Protect all root index, descriptor and shared resource files through existing
output readers/file-handle leases. A project registration marker alone does not
protect a foreign library. The request records a complete file manifest/hash for
each root, kind, mount identity, order and selected asset resolution.

The current browser supports loose cooked libraries. Describe and validate that
actual supported kind; do not add PAK-library UI scope to M08. Any future source
kind requires its supported native loader and proof before admission.

Reuse native Inspector plus the verified-hash derived cache for library dependency
metadata. Project-authored values are compared to saved source. A source-less
library has a distinct, explicitly labelled cooked baseline: verify dependency
keys, winning root/content hash and loaded semantics against native inspection.
Do not claim reconstruction of unavailable original authoring. An intentional
library override is reported as such and uses that winning baseline; it does
not silently count as preservation of the overridden project material's values.

### 6.3 Coordination and lock order

Validation shares the existing project's operation admission. Wait cancellably
for already-admitted cook/mount work, then establish a reservation and acquire
the verified read set. Later cooks/mount changes wait visibly and resume when the
reservation ends. This must not change session Pause automatic cooking.

Acquire the coordinator reservation before output registration/file readers,
then short saved-document reads. Recheck the project lifetime throughout.
Release document reads after immutable capture; retain the output/native readers
through child and GPU drain. Nested verification must reuse the acquired reader,
not register another reader behind a waiting writer.

The coordinator reservation prevents this editor from repeatedly attempting
publication against its own validation reader. OS-backed leases protect against
other editors. Expected contention yields a waiting/busy result, not a loop of
thrown IOException/CookOutputBusyException. Never hold a validation reservation
while waiting for a Save/Cook recovery action that needs that same admission.

Readiness, capture and process waits are cancellable. Project close cancels its
validation and awaits owned cleanup through the existing lifecycle; no other
process or document is closed to obtain the reservation.

## 7. Version-1 Protocol And Evidence

M08 adds this installed-tool entry point:

~~~text
Oxygen.Examples.RenderScene.exe --editor-validation-request <absolute-request.json>
~~~

No such supported option exists at the review baseline.

| Required field | Type and contract |
| --- | --- |
| `request_version` | Integer 1; reject unknown versions before loading content. |
| `operation_id`, `project_id`, `publication_id` | UUIDs; publication ID identifies the committed receipt, supplemented by its hash and per-product proofs. |
| `publication_receipt_hash`, `input_identity` | SHA-256 identities of the verified publication and selected saved input set. |
| `build` | Configuration, protocol/schema versions and separate cooking-producer, embedded runtime and standalone/capture artifact inventories/fingerprints. |
| `roots` | Ordered array of supported kinds, canonical absolute paths, mount identities and protected file manifests/hashes. |
| `scene_virtual_path`, `scene_asset_key` | Exact path AND key; both resolve to the same winning cooked scene. |
| `identity_map_path`, `identity_map_hash` | Immutable GUID/index and URI/key/winning-source map. No expected numeric property values. |
| `expected_state_path`, `expected_state_hash` | Managed comparison input from saved sources and separately labelled library baselines. Native code may verify its hash, but never apply or echo its values. |
| `profile` | Explicit camera GUID/index, target pixels/encoding, timestep, seed, scene-frame checkpoints, history reset and effective render-policy requirements. |
| `artifact_directory` | Absolute operation-owned directory outside authored content. |

Paths must resolve within their declared input/output ownership; reject traversal,
unexpected redirection, duplicate IDs/paths and malformed hashes. Validate
finite values and bounded counts/sizes before allocation. Protocol readers
reject missing required data and incompatible versions; optional diagnostic
extensions cannot satisfy a missing required observation.

Artifacts under `.oxygen/validation/<OperationId>/`:

- `request.json`, `identity-map.json`, `expected-state.json`, private saved inputs
  and their proofs.
- `embedded-observed.json`, `standalone-observed.json`, per-frame capture manifests.
- `embedded.png`, `standalone.png` for the base checkpoint; frame-suffixed images
  for additional checkpoints; optional derived `difference.png`.
- `standalone-result.json`, `comparison.json`, `result.json`, correlated output.

Observation/capture artifacts identify their own protocol version, operation,
scene/key, ordered-root fingerprint, native artifact identity, profile hash,
scene frame and native target generation. Report actual native hierarchy and
resolved asset values, plus effective settings/auto-exposure where required.
Observed fields carry native values, never the expected input merely copied back.

Native terminal result reports load/observation/capture phases and artifact hashes.
Managed terminal result adds comparisons, diagnostic codes, mismatch JSON pointers,
expected/actual values, image metrics, captured saved revision and freshness at
completion. Missing/invalid artifacts fail even if a process claims success.
Atomically finalize terminal JSON only after its referenced writes close.

Exit codes: 0 = requested native load/observation/captures completed;
2 = request/compatibility failure; 3 = load/runtime failure;
4 = observation/capture failure; 5 = cooperative cancellation.
Forced termination or crash need not return 5: the managed owner classifies the
actual outcome. Native exit 0 is never a parity verdict.

### Compatibility without a qualification build

Follow PRD section 9. Normal Interop compilation records its SDK dependencies;
runtime startup verifies those inputs. Managed/UI edits do not invalidate a fixed
whole-editor approval manifest because no such manifest is required.

Validation discovers RenderScene in the selected installed SDK and verifies its
ordinary build dependency/protocol metadata against the embedded native runtime,
required schemas and shaders. Add missing native tool metadata to the normal
CMake build/install path, not to a separate probe or qualification command.
Verify and hold the actual files before use. Child startup verifies the request
against its own loaded/runtime artifact identities.

Cooking producer fingerprints remain per-product provenance. Exact runtime/tool
and profile hashes identify this parity run. Do not require unrelated managed
assemblies, tests, documentation or git dirtiness to match for execution.
A diagnostic source commit may be recorded without becoming a startup gate.
Failure affects validation/native availability, not safe source authoring/Save.

## 8. Observation, Capture And Comparison

### 8.1 Required semantics

The field inventory covers every editable authored field in PRD section 8's
Transform/hierarchy, Geometry/slot 0, PerspectiveCamera, DirectionalLight/sun,
Environment/PostProcess/Background and scalar Material rows. Include component
presence, names/flags where authored, all camera components (not just selected),
parent relationships and transforms, geometry and resolved slot identities.

Compare stored native values and effective derived values separately. Examples:
camera FOV/aspect/near/far and parented world pose; light stored intensity and
effective compensation/sun selection; material alpha mode/cutoff and factors;
environment's stored exposure settings and frame-specific GPU exposure.
Legacy source mirror fields normalize to canonical PostProcess and are not
independent editable fields.

For engine-generated/None/Default references, record conceptual selection plus
the engine catalog's expected resolved keys/default material. Do not require
unrelated live and cooked procedural keys to be identical when the accepted
catalog defines their mapping. Required geometry counts/bounds and loaded
identities provide semantic evidence; representative images prove visible use.

Each field case declares a non-default saved value and expected native effect
before execution. Golden conversion examples must be independent of the
production descriptor/sync adapter. Corrupting/removing a native value must fail
the corresponding comparison, even when both render paths look similar.

### 8.2 Controlled render profile

The full fixture is the PRD's 100-node / 1,000-logical-entry project with at most
250,000 visible triangles. A small scene is only a smoke test. Qualification
uses matched Release artifacts on the same recorded D3D12 adapter/driver,
1920x1080 output, conventional directional shadows, fixed 1/60-second scene
steps, seed 0 where randomness is used, and no editor/debug/tool overlays.

Use the selected authored PerspectiveCamera's transform, FOV, aspect, near/far.
Do not reinterpret the navigation camera as authored state or overwrite aspect
from window dimensions. Both capture paths use the same recorded projection
and target mapping. The full fixture has an explicitly authored 16:9 camera;
non-16:9 field cases must preserve and observe their authored aspect.

Reset temporal, exposure and scene/view histories through supported engine
capabilities. Readiness means the requested scene, required geometry/material/
texture resources, uploads, camera and profile are usable by rendering.
Only then establish scene frame 0. Advance frames 1 through 120 and capture the
completed output of frame 120; process startup/asset-loading frames do not count.
No wall-clock sleep or accepted command is evidence of a rendered frame.

The base fixture explicitly selects Manual exposure EV 9.7 and ACES fitted tone
mapping. Arbitrary user scenes preserve their saved environment settings.
Do not force Manual or another tone mapper merely to obtain passing images.

Capture final scene pixels after tone mapping, authored display conversion,
background and foreground transparency composition, before UI composition.
Use the same defined SDR output encoding on both paths; remove BGRA/row-pitch
differences without another exposure/gamma adjustment or image resizing.
Readback completes only after the GPU copy fence and encoding/file writes.
A native capture owns source textures and related resources until then.

Record effective settings/CVars and native loaded artifacts, not just requested
values. Disable restored demo state, automatic camera rigs, hidden sky/material
substitutions and external capture overlays. Do not write temporary profile
values into the user's project, document or persistent demo settings.

### 8.3 Acceptance metrics

Preserve these established thresholds:

- IDs, resolved references, hierarchy, enums, booleans and array membership:
  exact against their declared identity mapping.
- Finite scalar/vector components: absolute error <=1e-4 OR relative error
  <=1e-4. Use `abs(a-b) <= max(1e-4, 1e-4 * max(abs(a), abs(b)))`.
- Quaternion orientation: angular difference <=0.01 degree, treating opposite
  signs as equivalent. Reject invalid/non-finite quaternions.
- Image channels: normalized final display-encoded sRGB RGB, whole-image
  RMSE <=0.01 and nearest-rank 99th-percentile absolute channel error <=0.03.
  Exclude only the outermost one-pixel border. Reject size/encoding mismatch;
  do not resize, align images, mask content or auto-rebaseline.

Compare expected vs embedded and expected vs standalone before reporting semantic
success. Compare the original controlled image pair and retain metrics/difference
images. Missing observations and unsupported required fields fail. Two empty or
incorrect images cannot pass the fixture's expected geometry/visible-effect gates
merely by matching each other.

Auto exposure uses identical initial history, luminance input and profile;
observe actual GPU exposure and images at scene frames 120, 240 and 600.
Effective exposure difference must be <=0.05 EV at each checkpoint. Apply the
same image thresholds at each paired checkpoint. Use bright/dark cases and
non-default adaptation settings to exercise both speeds and metering modes;
static Manual success cannot substitute for Auto behavior. ManualCamera cases
record the engine's effective camera-exposure inputs as well as the saved mode;
M08 does not add new camera authoring fields.

The field suite includes every tone mapper/exposure mode and visually meaningful
geometry/material, hierarchy/camera, light/atmosphere and background changes.
Background with atmosphere off retains picked display colour independently of
exposure/tone mapping; translucent foreground must retain its material.
Failures require a fix or an explicit revised acceptance decision, not silently
relaxed thresholds or substituted scenes.

### 8.4 Saved-revision embedded capture session

Only one session owns the embedded runtime. It records operation, project, run,
document, scene activation and view generation, saved revision, temporary profile
and current view intent. Before pinning, finish/cancel the active edit gesture
through its ordinary contract and drain earlier conflicting projection work.

Pin the verified saved projection while holding all later mutation delivery:
properties, hierarchy/components, asset assignments, environment and async
asset-demand completion. Authoring commits/Undo/Redo and new revisions continue;
rendering keeps advancing. This is not the publication pause that suspends
cooked-content rendering.

The viewport labels capture of the saved revision and any newer pending edits.
Disable navigation and profile changes only for the bounded capture window.
A fixed capture target is independent of UI panel size; resizing the same view
must not change image dimensions or persist a temporary camera setting.

After embedded artifacts finalize, release without waiting for standalone.
Remove the temporary profile only if lifetimes still match, capture one coherent
current authoring snapshot, supersede pending work it covers and apply later
valid work. Never restore an old scene snapshot or claim current preview merely
because camera settings were reset.

Scene activation, document close, view destruction and run replacement invalidate
the session and late callbacks. New activation uses normal full sync. A native
fault leaves authoring intact and preview unavailable; restart converges normally.
Capture/GPU cleanup and independent output-reader cleanup retain their respective
owners until actual work drains.

## 9. User Experience

The command is discoverable for an active authored scene. Preflight shows a clear
reason and next action instead of silently disabling the command for dirty or
stale content. No scene/runtime/camera, missing tooling, recovery-required output
and unresolved dependencies are distinct conditions.

Camera selection uses the selected authored PerspectiveCamera, otherwise the
sole authored perspective camera. Several require explicit choice; none requires
creating an authored camera. No fallback to an editor-navigation camera.

Save recovery lists participating documents with document-name links and explicit
Save listed. It uses existing save-conflict handling and preserves the validation
intent. Cook recovery delegates to existing Cooking and reruns preflight after
successful publication. No automatic save or implicit cook on command invocation.
Unrelated dirty documents remain untouched.

Running feedback is concise: Preparing, Capturing preview, Running standalone,
Comparing, then Passed/Failed/Cancelled. Busy/phase feedback appears within
100 ms. Show Cancel while work owns resources and Cancelling until cleanup
finishes. Captured revision/freshness is secondary information, not several
repetitive success labels.

### Product feedback and verification evidence

User decision, 2026-09-15: **Run a check, then exit**. The standalone window is
controlled by the check, then closes automatically. This is not a new interactive
play mode. Screenshot comparisons remain verification evidence; no comparison
document tab, docked validation panel or generic test dashboard is added.

Show compact progress/Cancel at the originating scene command surface, then a
concise result through the existing operation-result infrastructure. A failure
names the scene/asset/property and next action. Its details action exposes the
captured messages and artifact directory without making the user search global
logs. Do not put a raw stack trace or a permanent large banner in the scene view.
Original images, differences and full comparison JSON are retained as files for
diagnosis and joint acceptance testing, not another everyday asset-editor view.

Do not activate another document merely to show progress during capture. Manual
navigation away during embedded capture cancels it. Navigation afterward does
not cancel the independent standalone child or reactivate an old scene on its
completion. Explicit Cancel and project close own termination.

Reuse DroidNet compact controls, semantic WinUI status colours/icons, tooltips
and named accessible controls. Hide inapplicable actions. A run applies to its
captured revision; editing afterward does not relabel it as current or clear
document dirty state. Repeating the check captures fresh saved inputs and creates
distinct evidence.

Milestone acceptance includes joint testing with the user: execute the real
command, observe launch/progress/exit and result, change representative content,
rerun and cancel. Review retained captures/metrics together and record the user's
observations. Automated checks support that review; an extra product UI is not
required to prove the implementation.

## 10. Process Lifetime, Deadlines And Recovery

Launch structured arguments with the existing owned Windows job/process runner,
an operation-owned working directory and no shell concatenation. Capture stdout/
stderr for the operation; structured progress may update phase but log phrases
cannot establish successful load, capture or comparison.

Use a cancellable 120-second bound for active preparation/capture work and a
120-second child deadline. User Save/camera decisions are not timed as execution;
queued admission shows Waiting and remains cancellable. Each field-suite run has
its own deadline, not one deadline for the entire suite. Native cleanup has
separate bounded waits and retains a drain owner if completion arrives later.

Cooperative cancellation requests an orderly child stop; forced termination
targets only the owned child/job when needed. Await child/descendant termination,
GPU/native cleanup and redirected I/O drain before releasing associated leases.
Reuse the existing DrainCompletion ownership pattern on incomplete cleanup.
A token cancellation or timeout alone does not establish safe output replacement.

Child crash, invalid JSON, missing output, failed image encoding and disk-full
errors preserve partial evidence and source/published content. A managed terminal
result may record cleanup pending, but must not report ownership released until
the drain completes. No automatic retries that repeatedly cook, spawn processes
or throw exceptions against held locks.

Project/window shutdown follows the existing asynchronous close path; canceled
document close does not dispose a still-live project. An approved project close cancels validation
and awaits native/output ownership before project teardown completes.

## 11. Diagnostics And Persistence

Distinguish Needs save, Needs cooking, camera selection, recovery required,
request/version/build mismatch, changed mount/input set, invalid index/file proof,
unresolved asset, load failure, missing field, capture/readback failure, semantic
mismatch, image mismatch, timeout, cancellation and incomplete cleanup.

Every issue has operation/phase plus scene/node/asset/property identity when
available, a concise user message and optional technical details. Retain existing
operation-result vocabulary and string-kind conventions; add validation-specific
codes in the owning module. Do not expose a raw exception stack as primary UI.

Artifacts are local derived evidence under `.oxygen`, not saved authoring or
publication state. Missing evidence does not corrupt the project. Earlier results
remain historical and cannot close a current-build gate. No persistent validation
preference or background validation scheduler is added.

## 12. Validation Gates

- [ ] Exact project/root/path/key/camera request succeeds without example content,
  restored state, name matching or synthetic content.
- [ ] Request/build/schema/root/file mismatches fail before affected native use.
- [ ] Per-product expected state stays correct after partial/no-op cook and
  deliberate library priority changes; unavailable source is labelled honestly.
- [ ] Every required field passes saved/embedded/standalone semantic comparison.
- [ ] Controlled static and Auto/field image cases pass unchanged tolerances,
  with observed GPU exposure and actual completed-frame identities.
- [ ] Editing/Undo/hierarchy/asset changes during capture cannot contaminate
  its saved revision; release converges to current state.
- [ ] Cancel/fault/activation/close/resize/restart and late callbacks cannot
  restore stale state or leak scene/view/GPU/reader ownership.
- [ ] Concurrent saves/cooks/mount changes wait/resume safely without exception
  polling, hidden save/cook or modifying automatic-cooking preferences.
- [ ] Scene entry, prerequisite recovery, camera choice and complete readable
  per-run results pass actual UI tests and user review.
- [ ] M02's outstanding single-viewport evidence and the exact M08 build/
  publication/fixture/profile artifact set are recorded before M08 closes.

M08 owns these implementation and proof obligations. M09 viewport tools and
M10's complete release/GPU performance qualification remain separate.
