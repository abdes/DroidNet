# Standalone Runtime Validation LLD

Status: `revised development-only architecture approved; property-scope review and implementation pending`

The user's 2026-09-15 revision makes the **entire validation workflow
development-only**. This supersedes the earlier shipped Validate in Standalone
command, RenderScene validation flag, and prohibition on a separate qualification
executable. Normal Debug and Release editor/RenderScene builds and their
SDK/install/package graphs contain no validation protocol, runner, UI, fixtures,
metrics, or qualification-only instrumentation.

The original review baseline was `9df3b2b88`. The current plan is in
[ED-M08](../plan/ED-M08-runtime-parity-and-standalone-validation.md). Before
freezing field payloads, complete the substantive
[V0.1 authoring-scope review](../review/ED-M08-v01-authoring-scope.md).
Existing property registrations are evidence for that review, not automatic
approval of every exposed field.

V0.1 has no backward-compatibility requirement. Useful legacy intent migrates
to the approved canonical model; no legacy alias, fallback reader or parallel
field behavior is retained for compatibility. Integrity backups, publication
recovery and saved-revision ownership remain required and are not compatibility
features. Property decisions are made interactively one at a time, with current
context, viable options, industry practice and future implications. Approval of
one property does not approve a whole category or protocol payload.

## 1. Purpose

Prove that the exact saved/published editor scene loads through native runtime
content APIs and renders the same authored content as embedded preview.
An explicitly opted-in development test/tool runner reads authored/published
content and writes only isolated derived evidence. It exercises the real editor
authoring, Save, and Cook workflows; it does not add a validation command to the
shipped editor. Save and Cook remain separate explicit actions when preparation
needs them.

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
one or rewrite its aspect/clipping. M08's separate development driver uses the
real loading/rendering capabilities with an exact controlled profile; it adds
no mode to the normal RenderScene executable.

Current runtime observations read native node/environment/background values.
They do not enumerate a complete hierarchy or all scalar material fields and
do not observe GPU auto-exposure. Existing frame-capture settings configure
PIX/RenderDoc; Graphics has GPU readback primitives but no parity-PNG completion
contract. M08 consumes those production capabilities through development-only
drivers/adapters. New exposure telemetry, deep observations, capture holds/hooks,
and checkpoint scheduling needed only for qualification stay in opt-in targets.
A capability being reusable does not justify shipping its instrumentation.

## 4. Development Workflow

1. Explicitly invoke the development test/tool target with a project, authored
   scene, and explicit authored camera. Its own output explains unavailable
   prerequisites; there is no product menu, command, panel, or persistent setting.
2. Where preparation requires Save or Cook, use the real editor's existing
   workflows only after an explicit developer/user action. UI workflow tests
   invoke those real actions and recheck preflight. Unrelated dirty documents
   do not block and remain untouched.
3. The development adapter participates in the existing project coordination and
   reader contracts. Verify committed publication, capture the selected saved
   dependency closure and ordered mounts, and retain native/artifact ownership.
4. Build expected authored state and source-to-cooked identity mapping from
   verified saved inputs. Neither the live runtime nor cooked observations is
   the expected authored-state oracle.
5. Acquire a development-owned embedded capture session, apply the saved
   projection/camera, suppress tool overlays/navigation for that target, and
   capture completed scene frames while later authoring delivery stays pending.
6. Release the embedded session and converge to current authoring/view intent.
   Launch the matched opt-in native validation driver with the immutable request.
   It loads only the allowed roots/scene and produces observations and images.
7. Drain the child, compare each observation to expectations, then compare images.
   Finalize a structured result and expose original evidence in test/tool output.
8. Release reservations/readers only after actual native reads and cleanup end.
   Newer edits remain dirty. Results identify the captured revision and can
   become historical without changing their original verdict.

## 5. Ownership, Build Isolation And Dependency Direction

| Owner | Responsibility |
| --- | --- |
| Opt-in managed validation target in `tests/validation` or existing test-support | Preparation, expected state, admission adapter, owned process, comparison, fixture/coverage data, progress and derived results. Reuse an existing host where it can isolate this work; add a development project only if necessary. |
| Canonical development contracts under `Oxygen.Engine/tools/validation` | Shared schemas, protocol reader/DTO contract, rejection corpus and version compatibility. No canonical schema is owned by an example or embedded by a production editor assembly. |
| Opt-in native validation driver/adapters under `Oxygen.Engine/tools/validation` | Strict request execution, native enumeration/telemetry, deterministic checkpoints, capture correlation and evidence. A separate test/tool executable is permitted. |
| Development editor/UI-host adapter | Connect real editor services to the development runner; own qualification-only capture hold/hooks and result feedback outside the shipped composition graph. |
| Production ContentPipeline/WorldEditor/Runtime/Interop | Keep their real authoring, save/cook, mutation, loading, view and runtime responsibilities. They do not own M08 orchestration or depend on its test/tool target. |
| Production Scene/Content/Engine/Vortex/Graphics | Real scene/loading/rendering/readback behavior. Fix production defects under those owners; add production APIs only for an independent runtime responsibility. |
| DemoShell/RenderScene | Remain ordinary example consumers. Development drivers may reuse their loader/library code, but examples do not own the shared protocol and normal RenderScene receives no validation flag. |

Dependencies point **development tooling → production capabilities**, never the
reverse. Merely moving schemas into a shared production package does not meet
this requirement. New exposure telemetry, deep observations, capture hold/hooks,
checkpoint scheduling, comparison metrics, and fixtures used only for
qualification compile/link only in the opt-in managed/native target. If a hook
cannot be isolated, resolve that design constraint before adding it; do not
label validation instrumentation reusable and silently ship it.

Native validation configuration is explicitly opt-in and off by default,
independently of Debug/Release and ordinary test settings. Managed validation
has a dedicated opt-in test/tool target outside normal editor project references,
resources, module initializers, solution build selections, and packaging.
Development drivers/hooks and their schemas are not part of the normal SDK
installation. Use a separate development-validation output/staging tree; never
overwrite normal editor, RenderScene, SDK, or `bin` artifacts with a qualification
variant. Exact target names and commands are recorded when implemented.

An exclusion gate must inspect normal Debug **and** Release build graphs, binary
and embedded resources, module initializers, package contents, and install
manifests: **zero development-validation dependencies or payloads**. Build and
run the normal editor and RenderScene without the opt-in target to prove ordinary
behavior remains available. This is separate from proving the development runner.

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

The development adapter uses the existing project's operation admission. Wait cancellably
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

The opt-in development driver accepts an absolute immutable request path. Its
exact target and argument names are introduced with that target; **no validation
entry point is added to the normal RenderScene executable or editor**.

The version-1 envelope below remains a design contract, not implemented protocol
support. Payload/field details freeze only after the V0.1 scope review. Canonical
schemas and native readers belong to development tooling under `tools/validation`;
managed test/tool consumers share those sources without production references.

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
| `profile` | Explicit camera GUID/index and authored Auto/Fixed aspect policy, target pixels/encoding and content mapping, timestep, seed, scene-frame checkpoints, history reset and effective render-policy requirements. |
| `artifact_directory` | Absolute operation-owned directory outside authored content. |

Paths must resolve within their declared input/output ownership; reject traversal,
unexpected redirection, duplicate IDs/paths and malformed hashes. Validate
finite values and bounded counts/sizes before allocation. Protocol readers
reject missing required data and incompatible versions; optional diagnostic
extensions cannot satisfy a missing required observation.

Run artifacts under `.oxygen/validation/<OperationId>/` in a designated development
test project, or an explicitly owned isolated development evidence root:

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

### Compatibility and separate development builds

Normal Interop compilation continues to record its ordinary SDK dependencies;
product startup verifies them. No whole-editor qualification manifest or
validation target is a prerequisite for production startup, authoring, Save,
or Cook. Managed/UI edits do not invalidate a fixed whole-editor approval
manifest because no such manifest is required.

The development runner verifies the selected production runtime/Interop inputs,
its own opt-in driver/adapters/protocol, required schemas and shaders. Use the
existing SDK metadata/fingerprint approach for real dependencies, and generate
development target metadata only in its separate output bundle. Do not install
qualification-only headers, schemas, binaries or hooks into the normal SDK.
Verify and retain actual artifact files before use; child startup checks its
loaded identities against the request.

Cooking producer fingerprints remain per-product provenance. Exact runtime,
development tool, and profile hashes identify the parity run. Do not require
unrelated managed assemblies, tests, documentation or git dirtiness to match.
A source commit may be recorded diagnostically without becoming a startup gate.
A missing/incompatible development bundle makes qualification unavailable; it
does not affect normal source authoring, Save, Cook, or runtime availability.

## 8. Observation, Capture And Comparison

### 8.1 Required semantics

The required field set is **pending the researched V0.1 authoring-scope review**
and its explicit acceptance. Review professional workflows, authoring value,
canonical storage/units, runtime support and testing cost; do not freeze the
current registrations as the product surface. Reconcile approved changes with
PRD section 8 and the relevant authoring LLDs before protocol/fixture completion.

For the approved scope, include component presence, authored names/flags, every
authored camera (not just the selected one), parent relationships/transforms,
geometry and resolved slot identities. Map approved fields to source, stored
native and effective observations explicitly. Missing support for an approved
required field fails qualification; excluding a field needs a recorded scope
decision rather than silent omission.

Captured-sky lighting supporting both diffuse and specular image-based lighting
is explicitly approved. This capability decision does not approve every related
property/knob; remaining property decisions follow the review's one-at-a-time
interactive process.

Compare stored native values and effective derived values separately. Examples:
camera aspect policy/fixed ratio, effective per-view aspect, vertical FOV,
near/far and parented world pose; light stored intensity and
effective compensation/sun selection; material alpha mode/cutoff and factors;
environment's stored exposure settings and frame-specific GPU exposure.
Useful legacy source intent must migrate to canonical PostProcess or the other
approved canonical owner. Validation accepts that canonical model only; old
mirror fields, enum aliases or alternate readers are not compatibility gates.

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

Use the selected authored PerspectiveCamera's transform, vertical FOV, near/far
and approved aspect policy. [Decision 6: camera aspect fitting](../review/ED-M08-v01-authoring-scope.md)
approves **Auto and Fixed, with Auto the default for newly created cameras**:

- **Auto** derives effective aspect from that view's render target while keeping
  authored vertical FOV. Resolve it per view; do not write the derived ratio
  into saved camera data or persistent demo settings.
- **Fixed** preserves the authored ratio and complete composition in a centred
  content rectangle, with letterbox or pillarbox bars as needed. Do not crop or
  stretch the scene to fill a mismatched target.

Both embedded and native paths must use the same authored policy and target
mapping. The full 100-node fixture explicitly selects **Fixed 16:9**, despite the
new-camera Auto default. Add Fixed 4:3 and Auto target-resize cases. Fixed 4:3 in
1920x1080 has content rectangle `(240, 0, 1440, 1080)`; a 16:9 frame in 1440x1080
has `(0, 135, 1440, 810)`. These exercise both bar directions without changing
authored camera identity, pose or vertical FOV. Navigation is not authored state.

Record authored policy/ratio, target pixel dimensions, the exact integer content
rectangle `(x, y, width, height)`, and actual effective projection aspect/vertical
FOV for each view and capture. Check policy, target and rectangle exactly; use
the unchanged scalar tolerances for aspect/FOV. The prohibition is against
mutating authored policy, not against Auto deriving its required per-view aspect.

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
Fixed-frame bars are composed after scene post-processing and exposure/metering;
they must not enter scene luminance or Auto-exposure histograms. Capture the
complete target including those bars. Test that changing bar area alone, while
keeping scene content/projection constant, does not contaminate exposure.
Use the same defined SDR output encoding on both paths; remove BGRA/row-pitch
differences without another exposure/gamma adjustment or image resizing.
Readback completes only after the GPU copy fence and encoding/file writes.
A native capture owns source textures and related resources until then. The
checkpoint scheduler, qualification telemetry and capture-result protocol remain
development-only; using production readback does not make their instrumentation
part of the normal renderer build.

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

Camera framing does not relax these rules. The complete target, including bars,
participates in image comparison; no extra bar/content masks are allowed.
An incorrect content rectangle or effective aspect also fails its independent
semantic check, even if large matching bar regions reduce whole-image error.

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
the aspect-policy decision does not add physical-camera authoring fields.

Subject to the accepted field-scope review, the suite includes every approved
tone mapper/exposure mode and visually meaningful
geometry/material, hierarchy/camera, light/atmosphere and background changes.
Background with atmosphere off retains picked display colour independently of
exposure/tone mapping; translucent foreground must retain its material.
Failures require a fix or an explicit revised acceptance decision, not silently
relaxed thresholds or substituted scenes.

### 8.4 Saved-revision embedded capture session

Only one development capture session owns the embedded target. The opt-in
adapter owns its hold/hook; normal editor builds contain no qualification session.
It records operation, project, run,
document, scene activation and view generation, saved revision, temporary profile
and current view intent. Before pinning, finish/cancel the active edit gesture
through its ordinary contract and drain earlier conflicting projection work.

Pin the verified saved projection while holding all later mutation delivery:
properties, hierarchy/components, asset assignments, environment and async
asset-demand completion. Authoring commits/Undo/Redo and new revisions continue;
rendering keeps advancing. This is not the publication pause that suspends
cooked-content rendering.

The development host/test output identifies capture of the saved revision and
newer pending edits without adding product validation UI. Its capture adapter
suppresses navigation/profile changes only for the bounded capture window.
A fixed capture target is independent of UI panel size; resizing the same view
must not change image dimensions or persist a temporary camera setting.
Auto uses that fixed capture target's aspect, not the current dock/window size.
Separate Auto-resize cases intentionally change the controlled target/profile
between checks; each embedded/native pair still uses identical dimensions and
policy. In-session UI resize must leave the pinned capture profile unchanged.

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

## 9. Development Workflow And Joint Review

### Entry and prerequisites

The developer explicitly runs the opt-in test/tool target. There is no shipped
Validate in Standalone command, menu, banner, panel, protocol endpoint, metrics
viewer, fixture catalog, preference, or background scheduler. A separate driver
runs the check, writes useful results, and exits. Earlier requirements for a
product scene command and no separate executable are historical and superseded.

Camera selection uses an explicitly selected authored PerspectiveCamera,
otherwise the sole authored perspective camera. Several require explicit choice;
none requires creating an authored camera. Never substitute editor navigation.
The development runner distinguishes missing runtime/camera/tooling, dirty saved
inputs, stale cooking, recovery-required publication and unresolved dependencies.

Preparation does not secretly Save or Cook. UI workflow tests and joint review
exercise the real editor's scene, material, hierarchy, Save/conflict and Cooking
surfaces. Name participating documents when a save is required and invoke the
existing workflow explicitly; recheck preflight after completion. Leave unrelated
dirty documents alone and do not hold a validation reservation during recovery.

### Feedback and lifetime

Developer/test feedback retains the concise sequence Preparing, Capturing
preview, Running standalone, Comparing, then Passed/Failed/Cancelled. Busy/phase
feedback appears within 100 ms; cancellation remains visible until cleanup
finishes. A failure identifies scene/asset/property and the next action, with
technical details and original evidence in the run directory. A test-host status
surface may reuse compact accessible controls, but it must not enter the normal
editor composition or package. No new product dashboard or comparison tab.

Do not activate another document merely to report progress. In interactive UI
qualification, navigation away during embedded capture cancels that session.
After it releases, navigation does not cancel the independent child or reactivate
an old scene. Explicit Cancel and approved project close own termination. New
edits do not clear dirty state or relabel a captured revision as current.

### Joint acceptance

Run the real authoring UI together with the user, explicitly Save/Cook as needed,
and invoke the development driver. Observe capture, standalone launch/exit,
results, a changed material/background case, rerun and cancellation. Review
original images, differences, metrics and semantic failures as retained test
evidence. Record the user's observations and allow time for their testing before
closeout. Automated checks support that review; they do not substitute a toy UI,
a hidden source edit, or a matching pair of incorrect images.

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
available, a concise user message and optional technical details. Reuse existing diagnostic vocabulary and string-kind conventions where suitable;
validation-specific codes live in the development target. Do not present a raw
stack trace as the primary developer/test-host result.

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
- [ ] The researched V0.1 scope is accepted and reconciled; every approved required
  field passes saved/embedded/standalone semantic comparison.
- [ ] Controlled static and Auto/field image cases pass unchanged tolerances,
  with observed GPU exposure and actual completed-frame identities.
- [ ] Editing/Undo/hierarchy/asset changes during capture cannot contaminate
  its saved revision; release converges to current state.
- [ ] Cancel/fault/activation/close/resize/restart and late callbacks cannot
  restore stale state or leak scene/view/GPU/reader ownership.
- [ ] Concurrent saves/cooks/mount changes wait/resume safely without exception
  polling, hidden save/cook or modifying automatic-cooking preferences.
- [ ] Development entry, real authoring/Save/Cook UI recovery, camera choice and
  readable per-run results pass development tests and joint user review.
- [ ] Normal Debug/Release editor and RenderScene project references, resources,
  initializers, binaries, packages and SDK/install outputs contain zero
  development-validation dependencies or payloads. Canonical product workflows
  remain usable without development tooling; legacy behavior is not preserved
  merely for compatibility.
- [ ] M02's outstanding single-viewport evidence and the exact M08 build/
  publication/fixture/profile artifact set are recorded before M08 closes.

M08 owns these implementation and proof obligations. M09 viewport tools and
M10's complete release/GPU performance qualification remain separate.
