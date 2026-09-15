# ED-M08 — Runtime parity and standalone qualification

Status: **ready for implementation**

## 1. Outcome

The editor and native engine implement one canonical V0.1 authoring contract.
Saved and cooked scenes reproduce geometry, material slots, visibility, cameras,
lighting and environment in native and embedded rendering. An opt-in development
harness proves semantic and image parity for the complete workload and field suite.

Production behavior ships in its owning modules. Qualification protocols,
fixtures, comparisons, instrumentation and runners belong exclusively to
development targets. Normal Debug and Release applications and SDK packages
contain no qualification workflow.

Trace: REQ-018/019/022-026/030/037/039-042; SUCCESS-001/003/004/006.

## 2. Implementation document map

Each document owns the details listed below. This plan owns execution order and
acceptance gates; field defaults and wire contracts are not independently
redefined by the schedule.

| Document | Implementation authority |
| --- | --- |
| [PRD](../PRD.md), sections 8–10 | Feature boundary, workload and release envelope |
| [V0.1 authoring contract](../review/ED-M08-v01-authoring-scope.md) | Complete scope, exclusions and rejected alternatives |
| [Visibility and light participation](../review/ED-M08-node-light-visibility-review.md) | Local/Inherit flags, workspace Hide, contribution and shadow semantics |
| [Celestial-light contract](../review/ED-M08-celestial-light-authoring.md) | None/Primary/Secondary ownership, two contributors and conflicts |
| [Scene authoring model](../lld/scene-authoring-model.md) | Canonical scene/component identities, hierarchy and workspace-state separation |
| [Property inspector](../lld/property-inspector.md) | Exact fields, units, defaults, validation and conditional UI |
| [Material editor](../lld/material-editor.md) | Scalar PBR/emission representation, editing, persistence and preview |
| [Environment authoring](../lld/environment-authoring.md) | Atmosphere, captured sky light, exposure, grading and background |
| [Property pipeline](../lld/property-pipeline.md) | Edit sessions, revisions, validation, mixed selection and history |
| [Live engine sync](../lld/live-engine-sync.md) | Full projection, asset completion, native mutation and convergence |
| [Content pipeline](../lld/content-pipeline.md) | Slot identity, migration, saved snapshots, provenance, publication, ordered mounts and native producers |
| [Runtime integration](../lld/runtime-integration.md) | Build compatibility, scene/view lifetimes and production capabilities |
| [Standalone qualification](../lld/standalone-runtime-validation.md) | Development topology, protocol, admission, observations, capture, comparison and cleanup |
| [Settings architecture](../lld/settings-architecture.md) | Authored settings, local workspace state, runtime-session and startup preferences |
| [Documents and commands](../lld/documents-and-commands.md) | Save/close lifecycle, command outcomes and document ownership |
| [Cooking workflows](../lld/content-cooking-workflows.md) | Actual Save/Cook/reimport/recovery actions used by qualification |
| [Engine deferred capabilities](../../../projects/Oxygen.Engine/design/vortex/plan/editor-v01-deferred-capabilities.md) | Post-V0.1 exclusions and source-local TODO IDs |

### Native implementation references

| Document | Native implementation authority |
| --- | --- |
| [V0.1 rendering contract](../../../projects/Oxygen.Engine/design/vortex/plan/editor-v01-rendering-contract.md) | Concrete source owners and required changes for light enumeration, visibility, shadow receiving/contact, camera framing and grading |
| [Captured-sky IBL](../../../projects/Oxygen.Engine/design/vortex/plan/editor-v01-captured-sky-ibl.md) | Scene-global capture anchor, HDR products, diffuse SH, GGX filtering/BRDF integration, atomic publication and Stage 13 activation |
| [Lighting service](../../../projects/Oxygen.Engine/design/vortex/lld/lighting-service.md), [shadow service](../../../projects/Oxygen.Engine/design/vortex/lld/shadow-service.md) | Direct-light/shadow family ownership, wire/resource contracts and established CSM filtering/bias behavior |
| [Environment service](../../../projects/Oxygen.Engine/design/vortex/lld/environment-service.md), [indirect lighting service](../../../projects/Oxygen.Engine/design/vortex/lld/indirect-lighting-service.md) | Environment product publication and canonical indirect surface evaluation; retirement of the Stage 12 ambient bridge |
| [Cubemap processing](../../../projects/Oxygen.Engine/design/vortex/lld/cubemap-processing.md), [static skylight baseline](../../../projects/Oxygen.Engine/design/vortex/lld/skybox-static-skylight.md) | Existing source orientation, SH/radiance normalization and static-cubemap behavior reused by the new IBL contract |
| [View initialization](../../../projects/Oxygen.Engine/design/vortex/lld/init-views.md), [post-process service](../../../projects/Oxygen.Engine/design/vortex/lld/post-process-service.md) | View/history ownership, exposure, output composition and per-view processing |

Closed VTX-M08 evidence proves its original static diffuse-only implementation.
The V0.1 rendering/IBL contracts explicitly extend that baseline; the ED-M08
implementation must produce new evidence for captured sky and specular lighting.

## 3. Scope and ownership

### Production deliverables

- Ten canonical primitives, including Capsule, using shared native recipes and
  the metric centred defaults: horizontal Plane, upright Quad and 1 m outer Torus.
- Stable per-geometry material-slot identities and independent instance overrides
  for all existing slots; clearing restores the mesh material.
- Scalar emission colour/intensity with float32 storage, explicit linear-colour
  conversion and finite HDR bounds.
- Local/Inherit visibility and geometry shadow flags, independent light
  contribution/shadow controls, and functional GPU receiving/contact shadows.
- Exact authored perspective-camera selection with Auto/Fixed per-view framing.
- Per-light None/Primary/Secondary atmospheric assignment, two complete shadowed
  contributors and independent ordinary directional fill lights. Existing names
  remain; there is no competing scene Sun pointer or automatic promotion.
- Captured-sky diffuse/specular lighting and effective behavior for every retained
  environment/post-process/light field. Realtime is the V0.1 lighting workflow.
- Editor-only Hide as a local workspace/main-view mask, retaining illumination
  and caster eligibility without authored changes or cooking demand.
- One-time migration followed by canonical readers/writers and explicit repair
  states for unresolvable references.

### Development topology

| Target/location | Responsibility |
| --- | --- |
| `projects/Oxygen.Engine/tools/validation/Schemas` | Versioned qualification schemas and rejection corpus |
| `Oxygen.Tools.EditorValidation.Native` | Exact native request execution and standalone process |
| `Oxygen.Tools.EditorValidation.Capture` | Opt-in observations, checkpoints, exposure telemetry and capture bridge |
| `tests/EditorValidation/Oxygen.Editor.Validation.csproj` | Preparation, fixture/expectations, process ownership and comparisons |
| Existing WorldEditor UI test host with `OxygenEditorValidation=true` | Real editor workflows and saved-revision capture adapter |

Native qualification requires `OXYGEN_BUILD_EDITOR_VALIDATION=ON`; managed
qualification requires `OxygenEditorValidation=true`. Both default off,
independently of Debug/Release. Development builds/intermediates/staging use
`artifacts/ed-m08/<Configuration>/{native-build,managed-build,managed-obj,stage}`.
Evidence defaults to `artifacts/ed-m08/runs/<operation-id>` through an explicit
owned root. The standalone LLD defines target references and private native ABI.

Production projects reference no qualification assemblies, schemas or runners.
Qualification-only calls compile out of normal builds and their implementations
live only in opt-in sources. Loading, projection, rendering and readback remain
the real production algorithms; the harness does not duplicate them.

All authored changes use normal commands/history/revisions. Qualification never
implicitly saves or cooks. Its admission uses the existing project coordinator,
output readers and structured worker ownership, not another scheduler or lock.
Migration updates source/reference identities and recooks through native tools;
it never patches cooked binaries or installs legacy runtime readers.

Relevant engine edits carry `TODO(post-v0.1, <ID>)` at the actual deferred boundary,
linked to the engine scope record. Existing native functionality is distinguished
from missing editor exposure. Current M08 obligations are not future TODOs.

## 4. Execution sequence

Each slice ends with focused checks and a buildable code/test/doc commit. Native
contracts and rendered behavior pass before editor implementation relies on them.
M02's remaining supported-viewport evidence is a closeout gate; M09 tools are not
an entry dependency.

### M08.1 — Native canonical data, producers and primitives

Implement engine-owned schemas/versioned records for flag source modes, slot
identity/overrides, camera aspect policy, atmospheric slots and float32 emission.
Schema validation covers shape/range/count limits; semantic validation covers
identity, references, conflicts and representability.

Implement importer-owned slot inventories/provenance and continuity rules from
the content contract. Material parameter changes alone keep IDs. Structural
changes without proven continuity retain explicit repair-required references;
no name/index guessing rebinds an override. Update native hydration, Inspector
metadata and catalog output for every new record.

Implement Capsule, upright Quad, Torus defaults and finite-parameter rejection
through the shared procedural authority. Bounds, normals, tangents, winding,
recipe metadata and API prose change together. Migrate duplicate atmospheric
inputs and GeodesicSphere identities; ArrowGizmo stays an internal tool resource.
Remove the ineffective real-time-capture toggle from canonical SkyLight records,
adapters and demo controls. Captured sky has one source-change-driven production
policy; migrate its existing source/enable/parameter values and recook.

Deliver maintained native source/recipe migration and recooking scripts in this
slice. Refresh every affected maintained bundle and RenderScene source import
before M08.3 loads it. The scripts validate inputs, preserve recoverable backups,
update references and invoke current producers; no obsolete reader survives.
M08.5 extends this foundation to editor document migration and repair workflows.

Owners: `Oxygen/Data` format/catalog/procedural files; `Oxygen/Scene`;
`Oxygen/Cooker/Import` and schemas; native Inspector; shared scene hydration.

Checks: schema/round trips; slot continuity and structural changes; nonzero
slot overrides; Local/Inherit; hidden/off role conflicts; Auto/Fixed records;
finite HDR precision; primitive bounds/attributes/sidedness; obsolete format
rejection. Cook and inspect through native tools, never managed binary decoding.

### M08.2 — Native rendering and view behavior

Implement effective visibility for geometry/light eligibility and invalidation
on effective flag/hierarchy changes. Keep editing-main-view hiding separate.
Preserve independent geometry casting, light shadowing and receiver opt-out;
carry receiving into both GPU surface paths and implement retained contact-shadow
controls with their defined field semantics.

Replace single-directional surface/shadow selection with independent participating
sources. Primary and Secondary both illuminate and cast requested shadows;
None remains an ordinary directional source. Complete captured-sky diffuse
irradiance, roughness-dependent specular products, readiness and invalidation.
Activate Stage 13 indirect evaluation and retire the Stage 12 ambient bridge.
Use the scene-global capture anchor and shared producer/consumer filtering rules
in the IBL contract; camera navigation does not change authored sky lighting.

Resolve the exact camera and parented pose. Auto derives target aspect with
unchanged vertical FOV; Fixed preserves ratio/composition with a centred content
rectangle. Bars are outside metering and added after scene post-processing.
Implement retained grading/vignette and effective settings with defined ordering,
including clear-background display colour and foreground transparency.

Owners: `Scene/SceneFlags`, `Scene/Light/DirectionalLightResolver`,
`Vortex/ScenePrep`, `Vortex/Resources/DrawMetadataEmitter`, `Vortex/Lighting`,
`Vortex/Shadows`, `Vortex/Environment`, `Vortex/IndirectLighting`,
`Vortex/SceneCameraViewResolver`,
`Vortex/SceneRenderer`, and their D3D12 shaders.

Checks: independent light/caster/receiver controls; inherited Hidden/local Shown;
visibility cache invalidation; off-screen versus authored-hidden casters; opaque/
masked/Blend behavior; two shadowed directionals and Secondary alone; fill-only;
IBL readiness; grading/contact effects; Auto/Fixed projection. Preserve existing
material-sidedness and mirrored-winding correctness.

### M08.3 — Development harness and native visual gate

Implement the named opt-in targets and standalone LLD's version-1 protocol.
Build the complete fixture, field inventory and independent conversion examples.
The native driver loads only verified ordered roots and the exact scene path/key,
selects the explicit camera, and emits real observations/completed-frame images.
Expected values never populate observations or patch loaded content. Native
execution outcome is separate from the managed parity verdict.

Use production GPU readback with opt-in instrumentation at real ownership
boundaries. Validate input versions/fields/IDs/hashes/counts/paths before use.
Retain resources through GPU copy and encoding completion. PNG capture has no
RenderDoc/PIX dependency. Supply reusable documented build/run scripts.

**Native visual gate:** render every changed engine capability outside the
editor: all primitives and material slots; scalar emission/transparency;
visibility/caster/receiver controls; both atmospheric sources and fill lights;
captured diffuse/specular sky; camera framing; exposure/grading/background.
Retain images, observations, actual profile/build identities and readiness facts.
Resolve these failures before the editor-integration slice.

Checks also include row pitch/channel order/encoding, GPU-fence completion,
cancellation/device failure, in-flight resource lifetime and history reset.
Maintained RenderScene examples provide ordinary-loading regression coverage.

### M08.4 — Editor canonical authoring and live delivery

Implement the owning LLD field tables through existing document/property services:
all slots and repair affordances, emission, flags, atmospheric roles, cameras,
realtime light/shadow controls, captured sky light, exposure/grading and primitives.

WorldEditor/MaterialEditor own UI/commands; World/Managed.Assets own source data;
ContentPipeline owns native production; Runtime/Interop adapt engine operations.
Complete copy/duplicate, mixed selection, sessions, Undo/Redo, Save/reopen,
diagnostics and current-scene convergence. Workspace Hide follows the settings
contract and adds no authored state or cooking demand. Product UI contains no
qualification entry.

Checks: actual packaged controls/commands, nonzero slot assignment/clearing and
repair, published material changes, role conflicts, flag defaults/overrides,
Auto resize without dirtying, workspace Hide restoration/lifetime/accessibility.
Verify engine fixes again through normal editor workflows.

### M08.5 — Migration and verified saved-input preparation

Implement editor document migration and repair using M08.1's native producers
and maintained migration tools. Validate old inputs, write canonical source
atomically with backups, update references and invoke normal cooking.
Unrepresentable intent remains repair-required. Qualify existing project migration,
interrupted recovery and explicit slot repair through the real editor workflow.

The development preparation adapter enters the existing coordinator and resolves
the selected saved closure through source and per-product provenance. Capture
private saved bytes and verify product/receipt/publication identities. Protect
project/library files in saved mount order; native Inspector supplies opaque
library metadata. Build GUID/index and URI/key/winning-source maps tied to verified
descriptors. Release document read gates before authoring resumes; retain output
and native ownership until actual reads drain.

Recheck lifetime after waits; unwind partial acquisition in reverse order.
Save/Cook recovery occurs outside the reservation. Automatic-cooking preferences
remain unchanged. Expected contention waits without exception polling.

Checks: partial/no-op publication, unrelated dirty documents, changed dependencies,
reordered/conflicting libraries, corrupt/missing files, stale Inspector cache,
recovery journals, cancellation at each acquisition, competing cook/mount work,
project close and unchanged source/publication hashes during qualification.

### M08.6 — Embedded saved-revision capture

The opt-in host owns one session by project/document/activation/run/view generation.
Finish active gestures, drain earlier projection/publication, apply the verified
saved projection and pin camera/profile/target. Hold later properties, hierarchy,
components, asset completions and publication delivery while authoring/rendering
continue. Suppress navigation for the captured target and ignore workspace Hide
without destroying its current state.

After observations and GPU/image completion, release and converge once to the
latest valid authoring snapshot/view intent. Do not replay stale mutations or
restore a closed scene. Panel resize does not alter a pinned target; separate
Auto-resize cases use distinct controlled profiles.

Checks: edits/Undo/Redo/create/delete/reparent/slots/environment/saves during
warm-up; navigation/resize/document switch; cancel/fault/close/restart and late
callbacks. Verify saved captured state and newer preview after release. A drain
owner retains native resources if bounded teardown expires.

### M08.7 — Owned execution, comparison and results

Compose preparation → embedded capture/release → native child execution/drain →
semantic/image comparison → atomic result. Reuse structured process arguments,
owned Windows jobs and I/O drain. Terminate only owned work; leases survive until
descendant/process/native/GPU readers finish.

Compare expectations independently with embedded and standalone observations,
then compare images. Reject missing fields/checkpoints, non-finite data, wrong
operation/root/build/profile/view/frame identity and altered/truncated artifacts.
Label source-less library baselines and intentional overrides explicitly.
Native exit zero is not a parity verdict.

Retain original PNGs, differences, metrics, field mismatches and partial evidence.
Freshness is separate from verdict; later edits make the captured revision
historical without changing its outcome.

Checks: numeric boundaries, quaternion sign, enum/ID mismatch, empty-image false
positives, wrong frame/dimensions/encoding, hidden overrides, tampering, source
changes, crash/timeout/cancel, descendants retaining I/O, project replacement and
queued cooking/mount work resuming.

### M08.8 — Integrated qualification and closeout

Run the full fixture and every field case through native/embedded rendering and
real editor authoring/Save/Cook/migration/recovery. Qualify matched Release images
on the same adapter/driver; Debug covers protocol and ownership faults.

Include Primary-only, Secondary-only and both with distinct directions/colours
and requested shadows; None-role fill; capture invalidation; visibility/Hide
and receiver cases; every primitive/slot/emission case; Auto/Fixed cameras;
Manual/Auto exposure; every retained tone mapper/grade/background interaction.

Complete M02's one-viewport resize and consolidated discovery evidence under
[its plan](ED-M02-live-viewport-stabilization.md). Run normal editor and RenderScene
without development tools installed. Inspect normal Debug/Release references,
resources, initializers, exports, packages and SDK inventories for zero
qualification payloads.

Jointly review real changes, reruns, cancellation, original images and results.
Finish affected-code analyzer/IDE checks at commit preparation. Record one M08
result in IMPLEMENTATION_STATUS with exact build/fixture/publication/profile
and evidence identities.

## 5. Qualification constants

The standalone LLD owns measurement algorithms. These constants are fixed:

| Gate | Required value |
| --- | --- |
| Full workload | Exactly 100 nodes: 98 geometry, one camera, one Primary light; exactly 1,000 logical catalog entries; ≤250,000 visible triangles |
| Field cases | Separate bounded scenes cover multiple lights and non-default fields without changing full-workload counts |
| Base image profile | 1920×1080; Fixed 16:9; Manual EV9.7; ACES fitted; conventional shadows; no overlays |
| Time/history | Fixed 1/60-second scene step; seed 0; reset histories; frame 0 starts after content/uploads/camera/profile readiness |
| Checkpoints | Completed frame 120; Auto also at 240 and 600 |
| IDs/enums/booleans/membership/rectangles | Exact through the declared identity map |
| Finite scalars/vectors | `abs(a-b) <= max(1e-4, 1e-4 * max(abs(a), abs(b)))` |
| Quaternion orientation | ≤0.01 degree; opposite signs equivalent; invalid quaternions rejected |
| Images | Display-encoded sRGB RGB; RMSE ≤0.01; nearest-rank P99 absolute channel error ≤0.03; only outer one-pixel border excluded |
| Auto exposure | GPU-observed difference ≤0.05 EV at each checkpoint, with the same image thresholds |
| Framing | Fixed 4:3 in 1920×1080: `(240,0,1440,1080)`; Fixed 16:9 in 1440×1080: `(0,135,1440,810)`; bars stay in images and outside metering |
| Deadlines | Cancellable 120-second active preparation/capture bound and 120-second child deadline per run; cancellable admission wait; separately bounded cleanup/drain ownership |
| Feedback | Development progress/cancel state within 100 ms; responsive production authoring |

No image resizing/alignment/content masking, automatic rebaselining or tolerance
adjustment is part of comparison. Required geometry and visible-effect checks
prevent two empty or identically incorrect images from satisfying the gate.

## 6. Build and evidence

Production native work uses the existing `projects/Oxygen.Engine/out/build-ninja`
tree with CMake and normal Ninja build parallelism (`cmake --build <tree>
--parallel`). Coordinate build invocations that share an output tree; this does
not limit compilation within a build to one job. Use CTest for native tests and
MSBuild.exe/VSTest for editor work; rebuild Interop after changed SDK inputs are
installed. Serialize only tests that share exclusive GPU, fixture or publication
resources; independent tests can run in parallel.

M08.3 supplies reusable build/run scripts for the named development targets.
Options select configuration, request/evidence paths and filters; development
output never replaces normal SDK/editor artifacts. Record commands and hashes
of tools/runtime/schemas/shaders, hardware/driver/OS, profiles and results beside
each run. Native example use/content refresh follows the maintained
[RenderScene guide](../../../projects/Oxygen.Engine/Examples/RenderScene/README.md).

## 7. Exit checklist

- [ ] Canonical formats/migration and every required producer/loader mapping pass.
- [ ] Engine fixes have native tests and rendered evidence outside the editor.
- [ ] Editor authoring/history/Save/cook/live delivery and workspace Hide pass.
- [ ] Saved-input proof/ownership survive partial/no-op publication and contention.
- [ ] Saved-revision capture isolates edits and converges correctly after release.
- [ ] Full semantic/image/GPU-exposure comparisons pass the fixed thresholds.
- [ ] Cancel/fault/close/restart/resize preserve source, publication and ownership.
- [ ] Normal Debug/Release build/install/package contain no qualification payloads.
- [ ] M02 single-viewport evidence and joint M08 review are recorded.
- [ ] Source/API prose and post-V0.1 annotations match implemented contracts.
- [ ] Affected-code diagnostics are clean and the exact evidence set is recorded.
