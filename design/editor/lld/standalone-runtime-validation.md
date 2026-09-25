# Standalone Runtime Validation LLD

Status: `final implementation contract; implementation and qualification tracked in ED-M08`

The entire validation workflow is **development-only**. Normal Debug and Release
editor/RenderScene builds and their SDK/install/package graphs contain no
validation protocol, runner, UI, fixtures, metrics, or qualification-only
instrumentation. This document defines the implementation contract;
[ED-M08](../plan/ED-M08-runtime-parity-and-standalone-validation.md) records delivery
and evidence. The [V0.1 authoring scope](../review/ED-M08-v01-authoring-scope.md)
defines the required field surface; existing registrations do not expand it.

V0.1 has no backward-compatibility requirement. Useful legacy intent migrates
to the canonical model; no legacy alias, fallback reader or parallel
field behavior is retained for compatibility. Integrity backups, publication
recovery and saved-revision ownership remain required and are not compatibility
features.

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

### 5.1 Target topology

Paths in this table are relative to the repository root. These are development
targets, excluded from the default solution/CMake build and every package.

| Owner/path                                                                              | Concrete responsibility                                                                                                                                                                                                                           |
| --------------------------------------------------------------------------------------- | ------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| `projects/Oxygen.Engine/tools/validation/Schemas/`                                      | Canonical versioned JSON schemas and valid/rejection examples shared by native and managed tests.                                                                                                                                                 |
| `projects/Oxygen.Engine/tools/validation/Protocol/`                                     | Native schema-backed readers, bounded semantic validation and evidence writers.                                                                                                                                                                   |
| `projects/Oxygen.Engine/tools/validation/Driver/`                                       | `Oxygen.Tools.EditorValidation.Native` executable: exact content loading, controlled native view, check execution and exit.                                                                                                                       |
| `projects/Oxygen.Engine/tools/validation/Capture/`                                      | `Oxygen.Tools.EditorValidation.Capture` development module/bridge: session registry, native observation, completed-frame capture, GPU exposure readback and checkpoint scheduling. The driver and embedded test host use the same implementation. |
| `tests/EditorValidation/Oxygen.Editor.Validation.csproj`                                | Managed preparation, independent expected-state oracle, schema resources/DTOs, process ownership, comparison, fixtures and developer results.                                                                                                     |
| `projects/Oxygen.Editor.WorldEditor/tests/UI/Oxygen.Editor.WorldEditor.UI.Tests.csproj` | Existing WinUI test host, with an opt-in reference to the managed validation project; composes real editor services and the development delivery/capture gate.                                                                                    |
| Production ContentPipeline/WorldEditor/Runtime/Interop                                  | Actual authoring, saved snapshots, cook/publication, mutation, loading and view responsibilities. No M08 orchestration or production reference to development contracts.                                                                          |
| Production Scene/Content/Engine/Vortex/Graphics                                         | Actual scene/loading/rendering/readback behavior. Production changes require an independent runtime responsibility.                                                                                                                               |
| DemoShell/RenderScene                                                                   | Ordinary consumers. The driver calls DemoShell's existing scene-loading library and Content APIs with exact inputs; it does not duplicate the loader or launch normal RenderScene with a validation mode.                                         |

Enable the native target with CMake `OXYGEN_BUILD_EDITOR_VALIDATION=ON` (default
`OFF`) and the managed test-host reference with MSBuild
`OxygenEditorValidation=true` (default `false`). Ordinary test enablement and
Debug/Release selection do not enable either. The native entry is
`Oxygen.Tools.EditorValidation.Native.exe --request <absolute-request.json>`;
`--request` is its sole content/configuration input.

The development build writes only beneath
`artifacts/ed-m08/<Configuration>/`: `native-build/`, `managed-build/`,
`managed-obj/` and `stage/`. Every participating managed project uses its own
subdirectory for intermediate/output files. Build a private native SDK and
matching Interop into that staging tree; resolve all test-host native dependencies
from it. Neither build nor staging overwrites the normal SDK, project `bin/obj`,
editor or RenderScene outputs. Run evidence lives beneath the managed runner's
canonical `EvidenceRoot` (default `artifacts/ed-m08/runs/`), in
`<EvidenceRoot>/<operation-id>/`. A supplied evidence root must pass the same
ownership and reparse checks; it cannot overlap authored content or cooked roots.
Ordinary engine fixes continue to build through the existing `out/build-ninja`
and SDK workflow; the private tree is used only for the opt-in qualification build.

### 5.2 Instrumentation boundary

Dependencies point **development tooling → production capabilities**. The private
build compiles the same production scene projection, loader, renderer and shader
sources. Additional managed/native development sources provide instrumentation;
they do not implement alternative transforms, material conversion, lighting,
exposure, camera fitting or scene loading.

Qualification-only call sites use the private build's
`OXYGEN_EDITOR_VALIDATION` compile definition (C++) and
`OXYGEN_EDITOR_VALIDATION` conditional symbol (managed). Normal compilation removes
the calls, registration and development bridge exports entirely. Hook
implementations and protocol declarations reside only in the development paths
above; normal public SDK headers and production assemblies have no validation
types, resources, module initializers or dependency references. The conditional
test-host project reference is the only managed entry into this graph.

The native capture module registers at engine startup and unregisters only after
its engine/view/GPU work drains. Its private bridge maps an opaque session handle
to the real engine, scene activation and view generation. It accepts capture
control and returns copied observations/results, never pointers into native
state. The embedded development Interop build binds the real engine instance to
that module; the native driver binds its own instance. No process-global search
for a convenient scene/view, Qt/desktop capture or normal runtime discovery of
validation tools is permitted. Sections 8.4-8.5 define the hook behavior.

New deep observations, exposure telemetry, delivery holds, checkpoint scheduling,
metrics and fixtures remain in this graph. Reusability alone is not a reason to
ship instrumentation. Production camera fitting, light selection, shadow receiving
and content correctness fixes retain their production owners.

### 5.3 Exclusion gate

Inspect normal Debug **and** Release project/CMake graphs, binary symbols,
embedded resources, initializers, packages and SDK/install manifests: **zero
development-validation dependencies or payloads**. In particular there are no
capture bridge exports or instrumentation symbols in normal binaries. Build and
run the normal editor and RenderScene without the opt-in settings. Separately
verify that the private bundle contains matching native/Interop identities and
cannot resolve DLLs from the normal output tree.

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

ContentPipeline owns a destination-parameterized saved-snapshot core. It accepts
the caller-owned canonical private input directory and the already-acquired
source read set/document gates, and performs the shared copy, hashing and
coherency checks. The ordinary `CookInputSnapshotCapture` wrapper supplies its
existing `<project>/.build/cook/<operation>/inputs` destination. Development
preparation supplies `<EvidenceRoot>/<operation-id>/inputs`; it does not call a
wrapper that writes qualification inputs into the project's cook scratch area.
The core rejects source/published-root overlap, reparse points and non-private
destinations before writing. It preserves existing source and writer ownership;
it does not reacquire admission or nested readers. Both callers use this one
copy/hash implementation. Destination selection is an ordinary snapshot/staging
capability; validation protocols, expected fields and comparison remain outside
the production core.

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

The managed runner executes the admitted operation inside the existing
`IContentCookCoordinator.RunAsync` delegate. That shared writer scope is the
reservation; there is no second validation lock or coordinator. Already-admitted
cook/mount work finishes first. Later cooks and mount changes queue behind the
reservation and resume after it ends. Validation does not submit a cook request
or change session Pause automatic cooking.

Acquire the coordinator reservation before `CookOutputLease` and
`CookedLibraryReadSet` readers, then short saved-document reads. Recheck the
captured `ContentCookOperation` project lifetime throughout.
Release document reads after immutable capture; retain the output/native readers
through child and GPU drain. Nested verification must reuse the acquired reader,
not register another reader behind a waiting writer.

There is one admitted validation operation per project and one embedded capture
owner per runtime run. The native child begins only after embedded capture has
released its scene/view ownership; comparison starts after child output closes.
The coordinator delegate remains alive until all associated readers and native
drain owners finish, including cancellation. This ordering prevents a queued
publisher from acquiring the writer while its validation reader remains active.

The coordinator reservation prevents this editor from repeatedly attempting
publication against its own validation reader. OS-backed leases protect against
other editors. Expected contention yields a waiting/busy result, not a loop of
thrown IOException/CookOutputBusyException. Never hold a validation reservation
while waiting for a Save/Cook recovery action that needs that same admission.

Readiness, capture and process waits are cancellable. Once the ordinary project/
window-close or project-replacement workflow completes its save/conflict decisions and accepts
the transition, the opt-in host's pre-transition hook marks that project Closing,
prevents new validation admission and cancels its admitted/queued validation.
It performs this **before** waiting for coordinator admission or disposing the
runtime/project. It then awaits the independent `CleanupCompletion` task from
section 10 and continues the normal transition. A canceled close dialog does not
cancel validation or change project lifetime. Same-project cook and mount-order
changes remain queued; they do not use this close hook. Waiting for a later
project-lifetime token alone is insufficient: that token can be published only
after the transition acquires the writer held by validation. Closing only the
captured document or changing scene activation invalidates its embedded session;
it does not mark the whole project Closing. Once embedded capture has released,
such document navigation does not cancel an independent standalone child.

## 7. Version-1 Protocol And Evidence

The native driver accepts the absolute immutable request path defined in section
5.1. Canonical schema files are `request.schema.json`, `roots-manifest.schema.json`,
`profile.schema.json`, `identity-map.schema.json`,
`expected-state.schema.json`, `observed-state.schema.json`,
`capture-manifest.schema.json`, `capture-status.schema.json`, `native-result.schema.json`,
`comparison.schema.json` and `result.schema.json` under `tools/validation/Schemas/`.
Native and managed readers validate against those same files, then enforce
cross-record invariants. Examples and a shared rejection corpus cover every
schema. All schemas reject undeclared fields; additions require a schema version
change, not an unvalidated extension bag.

Every artifact carries `protocol_version: 1`, `artifact_version: 1`, a snake_case
`artifact_kind`, and `operation_id`. Enum values are snake_case strings, never
native/managed ordinal numbers. Normalize UUIDs to lowercase `D` format and
SHA-256 hashes to lowercase hexadecimal when writing; reject malformed values.

| Required field                                          | Type and contract                                                                                                                                                                                                                                                 |
| ------------------------------------------------------- | ----------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| `protocol_version`, `artifact_version`, `artifact_kind` | Integers 1, 1 and `request`; reject unknown versions before loading content.                                                                                                                                                                                      |
| `operation_id`, `project_id`, `publication_id`          | UUIDs; publication ID identifies the committed receipt, supplemented by its hash and per-product proofs.                                                                                                                                                          |
| `publication_receipt_hash`, `input_identity`            | SHA-256 identities of the verified publication and selected saved input set.                                                                                                                                                                                      |
| `project_root`                                          | Canonical absolute project root; it is not the evidence directory.                                                                                                                                                                                                |
| `build`                                                 | Configuration, protocol/schema versions and separate cooking-producer, embedded runtime and standalone/capture artifact inventories/fingerprints. Product schema versions carry their actual positive version numbers; they are not forced to protocol version 1. |
| `roots_manifest_path`, `roots_fingerprint`              | Immutable `roots-manifest.json` and SHA-256 of its exact bytes. Its `roots` array supplies supported kinds, canonical absolute paths, mount identities and protected file manifests/hashes in mount order.                                                        |
| `scene_virtual_path`, `scene_asset_key`                 | Exact path AND key; both resolve to the same winning cooked scene.                                                                                                                                                                                                |
| `identity_map_path`, `identity_map_hash`                | Immutable GUID/index and URI/key/winning-source map. No expected numeric property values.                                                                                                                                                                         |
| `expected_state_path`, `expected_state_hash`            | Managed comparison input from saved sources and separately labelled library baselines. Native code may verify its hash, but never apply or echo its values.                                                                                                       |
| `profile_path`, `profile_hash`                          | Immutable `profile.json` and SHA-256 of its exact bytes. It supplies explicit camera GUID/index, authored Auto/Fixed aspect policy, target pixels/encoding/content mapping, timestep, seed, checkpoints, history reset and effective render-policy requirements.  |
| `artifact_directory`                                    | Absolute operation-owned directory outside authored content.                                                                                                                                                                                                      |
| `evidence_root`                                         | Canonical absolute root selected by the managed runner; `artifact_directory` must equal this root joined with `operation_id`.                                                                                                                                     |
| `cancel_event_name`                                     | `Local\Oxygen.EdM08.<operation-id>`; parent-owned Windows manual-reset event used by the standalone child. Embedded cancellation uses the private ABI.                                                                                                            |

The managed writer creates `roots-manifest.json` (`artifact_kind: roots_manifest`)
and `profile.json` (`artifact_kind: profile`) as UTF-8 without a BOM, closes them,
hashes their **complete retained file bytes**, then writes the request containing
those paths/hashes. They are the only root/profile payloads; the request contains
no duplicate inline copies. Hashes are lowercase SHA-256 hexadecimal. Readers
verify raw bytes before parsing, reject invalid UTF-8/JSON and duplicate members,
then apply schema and semantic validation. They never normalize paths, whitespace,
escapes, object order or numeric spelling before hashing, and never reserialize
parsed records to reproduce the fingerprint. Different byte representations have
different fingerprints even when their JSON values are equivalent.

The publisher normalizes path values before writing; both consumers validate
those parsed values for ownership and Windows comparison separately. Keep both
manifest files open with write/replacement denied through native use. A partial
file, changed byte, absent manifest, wrong operation/version or hash mismatch
fails before using its contents. The same exact-file-byte rule applies to request,
identity/expected/observed state and capture artifact hashes. Existing production
receipt, saved-input and SDK identities retain their owning contract; retain the
original proofs rather than inventing a second serializer for them.

Input roots are canonical absolute Windows paths. Root-relative manifest paths
use forward slashes only. Reject `.`/`..`, empty segments, alternate data streams,
device namespaces, reserved DOS names, trailing dots/spaces and reparse points
in existing ancestors. Path comparison is Windows case-insensitive. Reject
duplicate canonical paths, root IDs, mount identities and subject/field keys.
The artifact directory must equal the request's parent directory and its final
component must equal `operation_id`; it must also be the immediate child of the
declared `evidence_root`. Only fixed manifest-listed relative output names are
writable beneath it. The managed launcher creates and verifies that owned
directory before launch; native verification repeats containment and reparse
checks. Neither side derives evidence paths from project layout. Input roots and
authored files are never writable outputs. Retain operation-directory and
input-file handles with replacement denied for the duration of use; a one-time
string/reparse check does not protect against replacement after verification.

Validate finite numbers, lengths, counts and allocation bounds before allocating
or mounting content. Version 1 caps requests/profiles/results/capture manifests
at 16 MiB, roots manifests/identity maps at 64 MiB, polling status at 64 KiB and
expected/observed state at 256 MiB before JSON parsing.
It permits at most 64 roots, 100,000 nodes/assets, 1,000,000 field/file records,
4,096 UTF-8 bytes per path/string and 16 capture checkpoints per request. Target
dimensions are positive and at most 8,192 pixels per axis. Multiplication and
aggregate byte counts use checked arithmetic. Schemas and semantic validators
enforce the same limits; the shared rejection corpus tests each limit and
limit-plus-one. These are rejection ceilings, not qualification workload targets.
Missing required data or observations fail.

Each operation directory contains:

- `request.json`, `roots-manifest.json`, `profile.json`, `identity-map.json`,
  `expected-state.json`, private saved inputs and their proofs.
- `embedded-observed.json`, `standalone-observed.json`, per-frame capture manifests.
- `embedded.png`, `standalone.png` for the base checkpoint; frame-suffixed images
  for additional checkpoints; optional derived `difference.png`.
- `standalone-result.json`, `comparison.json`, `result.json`, correlated output.

Observation/capture artifacts identify their own protocol version, operation,
scene/key, ordered-root fingerprint, native artifact identity, profile hash,
scene frame and native target generation. Report actual native hierarchy and
resolved asset values, plus effective settings/auto-exposure where required.
Observed fields carry native values, never the expected input merely copied back.

### 7.1 Identity and field records

Identity maps contain the complete node roster (`source_node_id`,
`native_node_index`, `parent_native_node_index`) and asset resolutions
(`asset_uri`, `asset_key`, `root_id`, content hash). Node indices are identity
associations, not property expectations. Engine-owned material `slot_id`, geometry
identity, layout revision and per-LOD bindings identify material assignments;
array positions are lookup coordinates only. An unresolved slot/layout identity
fails preparation rather than being omitted or guessed from names/indices.

Expected and observed state each contain a complete `nodes` list, independently
of `fields`, so missing/extra nodes fail even when no scalar field mentions them.
Every field record has `field_id`, `subject`, and `value: { kind, data }`.
The subject is `scene`, `node` or `asset`, with its corresponding stable UUID or
asset URI/key and optional material `slot_id`. The unique key is subject identity
plus `field_id`; ordering cannot mask duplicates.

Validate unique node associations and a complete acyclic parent forest before
comparison. Parent references, root references and material slot bindings must
resolve inside their declared roster/layout. Native handles are diagnostic
process-local values, never authored identities. Checkpoints are unique positive
scene-frame numbers in increasing order. Shared byte/hash vectors verify that
both readers hash the retained manifests identically, including non-ASCII paths,
JSON escapes and altered whitespace; no canonical-JSON reconstruction is used.

Value kinds are `scalar`, `vector2`, `vector3`, `vector4`, `quaternion`, `boolean`,
`enum`, `string`, `identity` and `node_reference`. Vectors use fixed-length numeric
arrays; quaternions use `[x,y,z,w]`. Enum data is the canonical named value, with
explicit conversion from differently named native enum members. Node references
are UUID or null. Identity data carries conceptual selection, resolved asset key
and winning root; `none` and `default` remain distinct selections.

Expected records carry `origin: authored | cooked_baseline`, saved source hash
and source JSON pointer. Observed records carry the actual native member/getter
path and a JSON pointer to their own serialized `value.data`. A field catalog in
`tests/EditorValidation/Fields/` binds each required field to source, canonical
units, native getter, independent conversion example and visible case. UI scalar
Euler rotation registrations map to one canonical local quaternion observation;
do not fabricate native Euler storage. Local and derived world transforms are
separate fields. The identity map contains no expected numerical property values.

### 7.2 Native execution and managed parity

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

### 7.3 Compatibility and separate development builds

Normal Interop compilation continues to record its ordinary SDK dependencies;
product startup verifies them. No whole-editor qualification manifest or
validation target is a prerequisite for production startup, authoring, Save,
or Cook. Managed/UI edits do not invalidate a fixed whole-editor qualification
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

The required field set is the final V0.1 surface in PRD section 8, the
[authoring-scope record](../review/ED-M08-v01-authoring-scope.md), and its owning
authoring LLDs. The field catalog covers that surface exactly. Include component
presence, authored names/flags, every
authored camera (not just the selected one), parent relationships/transforms,
geometry and resolved slot identities. Map required fields to source, stored
native and effective observations explicitly. Missing support for a required
field fails qualification; no test adapter silently excludes it.

The [visibility contract](../review/ED-M08-node-light-visibility-review.md)
requires these observations and cases:

- Scene Visibility source mode (`Inherit / Shown / Hidden`) and resolved flag;
  geometry Cast/Receive Shadows source mode (`Inherit / On / Off`) and resolved
  flags. New roots resolve Shown/On/On and new children inherit. A local override
  can differ from a hidden parent; geometry and light consumers use that same
  resolved visibility rather than adding a light-only ancestor gate.
- Independent light Affects Scene and light Cast Shadows. Participation Off
  stops illumination/atmospheric contribution while preserving stored intensity,
  colour, visibility and role assignment. Effective Hidden also suppresses
  contribution without rewriting the light's participation setting.
- Receiver Off skips direct-light shadow attenuation with a real GPU effect;
  it preserves visibility, direct lighting, ambient occlusion and casting.
  Geometry casting covers opaque/masked surfaces, not blended casting.
- Hidden camera nodes remain selectable and usable. Authored Hidden removes
  geometry from shadow submission; visible off-screen casters may remain.
  Authored Shadows Only/Hidden Shadow modes are excluded from V0.1.
- Effective flag and hierarchy changes invalidate directional selection and
  affected captured-lighting products. Test visibility-only changes after a
  populated resolver cache; existing role/flag unit tests do not prove this.

Editor-only Hide is separate per-user/project workspace state outside authored
content. It masks geometry/gizmo representations and descendants only in the
editing main view while retaining illumination and caster eligibility. Parent
hide/show preserves child hide choices and Show All clears the view overrides.
Qualification verifies no source/dirty/history/cook effects and renders its
controlled targets without workspace masks. Release preserves the current valid
editing-view choices. Runtime-loaded `IsActive` is derived, not authored state.

Migrate useful old visibility/caster values to canonical explicit modes; do not
replace old child intent with new-creation defaults. Historically ineffective
receiver flags migrate to their actual rendered receiving-On behavior. Remove
obsolete representations/readers rather than testing a compatibility branch.
The [celestial-light contract](../review/ED-M08-celestial-light-authoring.md)
uses canonical per-light `AtmosphereLightSlot = None / Primary / Secondary`.
Preserve the existing enum and assignment API names; no A/B rename or competing
editable scene Sun pointer. Primary/Secondary are stable slots, not brightness
priority, Moon classification or promotion rules.

Observe every stored assignment, including hidden/off lights, independently of
effective participation. Occupied-slot edits/imports/cooks must reject with the
occupant's identity and leave assignments unchanged. Disabling/hiding a source
retains its slot; Secondary-only operation never promotes it to Primary. None
preserves ordinary directional illumination without atmosphere participation.
Migrate useful explicit slots and unambiguous old sun intent; report conflicts
instead of guessing by name, brightness or traversal order. Remove duplicate
`SunNodeId`/`IsSunLight`/Contributes authoring state and fallback execution paths.

Qualify Primary-only, Secondary-only and both simultaneously, with independent
direction, colour, illuminance, source angle and requested shadows in
forward/deferred surface paths and applicable fog. Include role-None fill,
atmosphere-off, hidden/disabled sources, re-enable, conflict rejection and
Save/cook/load preservation. Both sources must update atmosphere and captured-sky
diffuse/specular products correctly. Explicit-disk exclusion from reflection
capture must not omit either source's scattering. Two disks with one actual
surface/shadow light cannot pass.

A Moon use case means directional moonlight and an analytic disk. It does not
claim lunar textures, phases or orbit simulation. More than two atmospheric
sources and sky-only authoring are post-V0.1 scope; Affects Scene continues to
gate all light contribution. Deferred work is tracked under the owning engine
[capability record](../../../projects/Oxygen.Engine/design/vortex/milestones/ED-M08/deferred-capabilities.md),
with source-local comments added as the corresponding engine code is modified.

Captured-sky lighting supplies both diffuse and specular image-based lighting.
Its authored fields and refresh policy follow the environment authoring contract.

Compare stored native values and effective derived values separately. Examples:
camera aspect policy/fixed ratio, effective per-view aspect, vertical FOV,
near/far and parented world pose; light stored intensity/atmosphere assignment and
effective participation in direct, shadowed and atmospheric lighting; material alpha mode/cutoff and factors;
environment's stored exposure settings and frame-specific GPU exposure.
Useful legacy source intent must migrate to canonical PostProcess or the other
canonical owner. Validation accepts that canonical model only; old
mirror fields, enum aliases or alternate readers are not compatibility gates.
Material observations use the canonical float32 emissive RGB storage. Recooked
products must satisfy the same scalar tolerance as saved source; binary16
quantization is not an exception to the `1e-4` gate.

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
and aspect policy: **Auto and Fixed, with Auto the default for newly created
cameras**:

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
static Manual success cannot substitute for Auto behavior. Physical-camera
authoring is outside V0.1; existing native camera-exposure capability remains a
separate runtime feature and does not introduce ManualCamera editor cases.

The suite includes every V0.1 tone mapper/exposure mode and visually meaningful
geometry/material, hierarchy/camera, light/atmosphere and background changes.
Background with atmosphere off retains picked display colour independently of
exposure/tone mapping; translucent foreground must retain its material.
Failures require a fix or an explicit revised acceptance decision, not silently
relaxed thresholds or substituted scenes.

### 8.4 Saved-revision embedded capture session

`EmbeddedCaptureSession` in `tests/EditorValidation/` is the sole managed owner.
It records operation, project, runtime run, document lifetime, scene activation,
view generation, saved revision and profile hash. Its states are Acquiring,
Preparing, Capturing, Releasing and Released; cancellation can enter Releasing
from any earlier state. A process-local asynchronous gate admits one session.

The private WorldEditor/Runtime build supplies narrowly scoped delivery hooks
compiled under the symbol in section 5.2. The test host registers its gate before
constructing real `SceneEngineSync` and runtime services. The hook executes at the
scene-sync scheduling boundary before projection/property/hierarchy/environment/
asset-delivery jobs reach `IRuntimeWorldCommands`. Matching later jobs remain in
their existing revision-aware pending queues; they do not return a false Applied
outcome. World commands already in native queues drain before pinning. The
development gate also intercepts the matching view's input/profile delivery;
other document authoring and unrelated project operations continue normally.

Acquisition runs on the owning UI dispatcher: finish or cancel the active gesture
through its ordinary contract, close later delivery, drain earlier jobs, then
project the immutable saved snapshot through the **same production scene
projector and native commands** used by full scene sync. A session-scoped control
token permits this projection through the closed gate. This token cannot be used
by ordinary UI jobs. Projection establishes a fresh native activation; callbacks
from the displaced activation fail the existing generation checks. The saved
projection's own asset requests finish through the normal loader before readiness.
The expected-state oracle does not supply runtime property commands.

Authoring commits, Undo/Redo and new revisions continue during capture; rendering
also advances. A saved snapshot is a private projection input, never a replacement
for the open document model. The gate covers all mutation delivery paths, including
completion-triggered refresh jobs; it does not hold arbitrary old load callbacks
and replay them into the pinned activation. This is not the publication pause
that suspends cooked-content rendering.

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
On the UI dispatcher, check all captured lifetimes. For a still-current document,
capture one coherent **current** authoring snapshot and invoke the existing full
scene projection with the control token. That fresh activation supersedes pending
jobs covered by its revision. Remove the capture profile/view mapping, preserve
the current editing-view hide mask, reopen delivery and let later valid jobs
converge through ordinary sync. Capture release completes only when the gate and
temporary ownership are removed; preview readiness separately follows the actual
current projection. Do not restore a saved/old scene or acknowledge later edits
merely because camera settings were reset.

Scene activation, document close, view destruction and run replacement invalidate
the session and late callbacks. New activation uses normal full sync. A native
fault leaves authoring intact and preview unavailable; restart converges normally.
Capture/GPU cleanup and independent output-reader cleanup retain their respective
owners until actual work drains.

### 8.5 Native checkpoint and readback owner

`EditorValidationModule` in `tools/validation/Capture/` runs on the existing engine
frame lifecycle. The versioned private polling ABI in section 8.6 provides Begin,
Cancel, Poll and Release for an opaque session handle; it adds no product world
commands. Begin validates the immutable profile and exact native activation/view
generation. Session work is posted to the owning engine thread. A destroyed view,
changed activation or stopped run invalidates target access before teardown;
the session's copied status remains pollable until terminal drain and Release.

The private build has four instrumentation boundaries:

1. **Mutation/prepare completion:** after normal scene update and resource
   preparation, inspect the real scene roster, resolved assets, effective flags,
   selected lights, view and profile. Readiness requires no unresolved required
   resource/upload and a usable final target. A successful enqueue does not
   satisfy this check.
2. **History reset and frame origin:** use the real renderer's scene/view history
   reset operations, including exposure history; wait for their acknowledgment.
   Establish frame 0 only after readiness/reset. The module supplies the fixed
   scene-step clock through the engine's existing clock input and counts completed
   frames for this view, independently of startup/global frame sequence.
3. **Final scene output:** at the renderer's final target publication, after
   transparency, tone mapping, display conversion and camera bars but before UI,
   the hook submits the selected checkpoint's texture to Graphics' existing
   `ReadbackManager`. The same checkpoint snapshots native observations and the
   actual GPU exposure buffer produced by the exposure pass. No CPU recreation of
   auto exposure, separate shader or alternate render pass supplies the values.
4. **Fence and artifact completion:** a capture job retains texture/buffer leases,
   readback tickets, operation/activation/view identity and scene frame. It awaits
   GPU copy completion, converts row pitch/channel order to canonical sRGB RGB,
   and writes PNG/JSON on a worker. The engine/UI threads never block on encoding.

The per-session output queue is bounded to one checkpoint readback in flight.
The module pauses captured-target frame submission between scene frames if that
slot is occupied; there are no extra scene/exposure/history updates while waiting.
The host can present the retained texture. It does not skip requested checkpoints
or change the fixed timestep. Both paths use the same scheduler. Capture manifests
record target/content rectangle,
encoding, dimensions, completed scene frame, GPU exposure, profile/root/build
fingerprints and file hashes. A checkpoint is complete only after both image and
observations close successfully; completion is published after their atomic
manifest rename.

Cancel prevents new copies/checkpoints, invalidates callbacks for release, and
drains already-submitted GPU work. Readback cancellation is not permission to
free a texture before the GPU releases it. On device failure, retain the device's
actual teardown owner; report capture failure and cleanup state. Partial files
use operation-owned temporary names and cannot satisfy a manifest. The same
module is used by embedded and native-driver runs; the driver changes only the
host and loading entry point.

### 8.6 Private capture ABI version 1

The Windows x64 development DLL `Oxygen.Tools.EditorValidation.Capture` exposes
unmangled `extern "C"` functions with the C calling convention. Its private header
lives in `tools/validation/Capture/`; it is absent from normal SDK/install outputs.
Managed declarations exist only in `tests/EditorValidation/`. ABI version 1 is
independent of JSON protocol versions; both must match before capture starts.

```c
/* 8-byte packing; sizeof(OXEV_TargetV1) == 312 on the supported x64 ABI. */
typedef struct OXEV_TargetV1 {
    uint32_t abi_version;       /* 1 */
    uint32_t struct_size;       /* 312 */
    uint64_t engine_binding;
    uint64_t native_view_id;
    char project_id[36];
    char run_id[36];
    char scene_id[36];
    char document_id[36];
    char document_lifetime[36];
    char activation_id[36];
    char viewport_id[36];
    char view_generation[36];
} OXEV_TargetV1;

uint32_t OXEV_GetAbiVersion(void);
uint32_t OXEV_BeginCapture(const OXEV_TargetV1* target,
    const char* request_path_utf8, uint32_t path_bytes, uint64_t* session);
uint32_t OXEV_PollCapture(uint64_t session,
    char* destination, uint32_t capacity, uint32_t* required_bytes);
uint32_t OXEV_CancelCapture(uint64_t session);
uint32_t OXEV_ReleaseCapture(uint64_t session);
```

All integer fields are fixed-width little-endian. UUID arrays contain exactly
36 lowercase ASCII `D`-format characters without a terminator; `view_generation`
is the existing managed view-generation UUID, not a numeric native view ID.
The managed ABI mirror uses fixed byte arrays with 8-byte packing, not UTF-16
`char` arrays or the runtime records' automatic layout. Its size and every field
offset are asserted against the native header.
No C++ types, managed references, exception objects or allocator-owned strings
cross this boundary. `GetAbiVersion` returns 1. Other exports return stable ABI
control codes: 0 Ok, 1 InvalidArgument, 2 AbiMismatch, 3 InvalidTarget,
4 InvalidHandle, 5 BufferTooSmall, 6 Busy, 7 InternalError. These transport codes
do not replace the protocol's named JSON enums or native execution exit codes.

**Target mapping.** The development Interop startup hook registers its actual
engine instance with the capture module's private C++ registry and receives a
nonzero `engine_binding` token. Native view/scene owners register the exact
`RuntimeSceneTarget` and `RuntimeViewTarget` associations after creation, and
invalidate them before replacement/destruction. The driver registers its own
engine/view and host-lifetime identifiers through the same registry. Registration
uses native owner references internally; no engine/scene pointer is exposed to
managed code. Begin verifies every target field against this registry, including
the request's project/scene, and acquires an owner lease. It does not infer a
target from the active window, name, raw numeric view value or request alone.
Binding/session tokens are process-local, monotonically allocated and never
reused; they are not saved identities. Exhaustion rejects allocation.

**Begin and payload.** `path_bytes` is 1..4,096, excludes a terminator and contains
valid UTF-8 with no embedded NUL. It names the immutable absolute `request.json`
from section 7, including the referenced root/profile manifests. There is no
second binary profile representation. Begin validates/copies the target and path
before returning; it never retains caller pointers. On synchronous rejection it
sets `*session=0`. On acceptance it returns a nonzero handle in Preparing state.
File verification/schema loading runs on the module's worker, with scene/view
actions posted to the engine thread; Begin does not block on content or GPU work.
Request/version/ownership failures after acceptance become failed status and
drain through the same session lifecycle. Begin acceptance is not readiness.

**Poll and buffers.** Poll is nonblocking and copies one internally consistent
UTF-8 JSON status snapshot into caller-owned memory. Status follows
`capture-status.schema.json` and includes operation/target identity, phase,
outcome, `terminal`, `cleanup_complete`, diagnostics and completed manifest paths;
deep observations remain in their files. Maximum encoded status is 64 KiB.
`required_bytes` excludes a terminator. For `destination=NULL, capacity=0`, or a
buffer too small for the current snapshot, return BufferTooSmall, report the
required size and write no payload. On Ok, copy exactly `required_bytes` bytes;
there is no NUL terminator. The host can allocate a 64 KiB buffer once and reuse
it. No native allocation is handed to the caller and no FreeBuffer API is needed.
An invalid handle reports zero required bytes. All exports contain native
exceptions and return a control code; exceptions never cross the C ABI.

**Threading and lifetime.** Calls originate on managed worker tasks, not blocking
UI waits. Poll and Cancel are safe to race: the module serializes handle state
and Poll takes a copied snapshot. It never calls a managed delegate or posts a
completion callback into managed code. Cancel is idempotent for a retained handle;
it requests cancellation on the engine/module owner and returns immediately.
It does not free resources or itself establish terminal state. The standalone
host translates its cancellation event into this same cancellation path.

Terminal status requires all session engine jobs, GPU/readback resources and
file writers to have drained; `cleanup_complete` is then true and the final
snapshot is immutable. Earlier failure/cancellation can be reported with
`terminal=false` while draining. Release returns Busy until terminal, then
removes the handle and returns Ok; subsequent calls return InvalidHandle. The
managed owner serializes Release with its own Poll/Cancel tasks and waits for
Release before freeing the DLL handle. Engine shutdown first prevents new
bindings/sessions, invalidates targets and drains their work; it unregisters
native hooks only after they can no longer execute. Copied terminal statuses
remain available for Release after target destruction. DLL unload requires all
engine bindings/hooks and session handles to have been released.

ABI tests assert layout/offsets, version/size rejection, UUID/UTF-8/path validation,
foreign/stale engine/view generations, immediate target invalidation after Begin,
poll sizing/short buffers, cancellation races, terminal-before-release, repeated
Release and DLL retention through drain. A successful Poll is status transport,
not a parity verdict.

## 9. Development Workflow And Joint Review

### Entry and prerequisites

The developer explicitly runs the opt-in test/tool target. There is no shipped
Validate in Standalone command, menu, banner, panel, protocol endpoint, metrics
viewer, fixture catalog, preference, or background scheduler. The separate driver
runs the check, writes useful results, and exits.

Camera selection uses an explicitly selected authored PerspectiveCamera,
otherwise the sole authored perspective camera. Several require explicit choice;
when none exists, preparation reports Needs camera and stops until one is authored.
Never substitute editor navigation or create a synthetic validation camera.
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
an old scene. Explicit Cancel and project close own termination. New
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

The operation has two distinct lifetimes. `executionCancellation` links the
request, active project/run and execution deadlines; it stops preparation, new
checkpoints and comparison work. `CleanupCompletion` is an independently owned
task whose lifetime is **not linked** to that canceled token, project replacement
or UI disposal. Release, current-state convergence, native cancellation, process
termination and GPU/I/O drain use this cleanup scope. Individual waits have
bounded observation deadlines; expiry transfers the unfinished task/handles to
the drain owner rather than canceling or abandoning that work. Repeated Cancel
requests join the same cleanup task and do not start parallel teardown.

For explicit Cancel with valid target lifetimes, cleanup performs section 8.4's
current-snapshot convergence using the independent scope. For accepted close,
replacement or invalidated scene/view/run, mark restoration abandoned first:
remove the development delivery/profile ownership and drain native work without
projecting another scene or waiting for new authoring assets. Every release path
removes its gate in `finally`, retains unresolved native ownership and preserves
ordinary revision-aware pending work. The application/test host awaits cleanup
before disposing services or unloading native modules. It never uses an already
canceled request token to wait for its own cleanup.

The parent creates the request's cancellation event before launching the child,
with access restricted to the current Windows user. The child opens that exact
event and checks it during loading, frame scheduling and asynchronous capture
waits. The parent signals it on Cancel/timeout, allows five seconds for orderly
stop, then terminates only its owned Windows job if needed. The native driver
never opens an event name derived from scene content. The parent retains the
event until child termination. Following forced termination, allow at most five
seconds for the immediate process/job wait and redirected-I/O drain. If either
remains incomplete, transfer its handles/tasks to the existing `DrainCompletion`
owner; do not block the UI or dispose handles under active readers. Await actual
child/descendant termination, GPU/native cleanup and I/O drain before releasing
associated leases. Cancellation never treats killing a process as successful
execution or capture.
A token cancellation or timeout alone does not establish safe output replacement.

Child crash, invalid JSON, missing output, failed image encoding and disk-full
errors preserve partial evidence and source/published content. Operation progress
records incomplete cleanup while its drain owner remains active; terminal
`result.json` is finalized only after owned cleanup ends. No automatic retries
repeatedly cook, spawn processes or throw exceptions against held locks.

Project/window shutdown follows section 6.3's pre-transition ordering. After the
ordinary close decisions accept the transition, cancel validation before waiting
for its coordinator writer and await CleanupCompletion before project/runtime
teardown. A canceled close keeps the project and validation live. The transition
does not depend on a project-lifetime event that can occur only after admission.

## 11. Diagnostics And Persistence

Distinguish Needs save, Needs cooking, camera selection, recovery required,
request/version/build mismatch, changed mount/input set, invalid index/file proof,
unresolved asset, load failure, missing field, capture/readback failure, semantic
mismatch, image mismatch, timeout, cancellation and incomplete cleanup.

Every issue has operation/phase plus scene/node/asset/property identity when
available, a concise message and optional technical details. Reuse existing
diagnostic vocabulary and string-kind conventions where suitable;
validation-specific codes live in the development target. Do not present a raw
stack trace as the primary developer/test-host result.

Artifacts are local derived evidence under the operation's `EvidenceRoot`, not
saved authoring or publication state. Missing evidence does not corrupt the project. Earlier results
remain historical and cannot close a current-build gate. No persistent validation
preference or background validation scheduler is added.

## 12. Validation Gates

- [ ] Per-light None/Primary/Secondary assignments, uniqueness across hidden/off
      occupants, no promotion, conflict-aware migration and exact source round trips
      pass. Existing native enum/API names remain canonical.
- [ ] Both atmospheric sources simultaneously illuminate and cast requested
      shadows in surface paths/applicable fog, contribute to atmosphere/captured-sky
      diffuse/specular lighting, and invalidate products independently. Secondary-only
      and ordinary role-None directional operation pass without substitutions.
- [ ] Approved visibility modes/defaults, local overrides, hierarchy propagation
      and useful-content migration pass Save/cook/native/editor qualification.
- [ ] Light participation, geometry casting, light shadowing and GPU receiver
      opt-out have independent rendered effects; hidden camera nodes remain usable.
- [ ] Editor-only Hide preserves illumination/caster eligibility and child
      choices without source, dirty/history or cook changes. Controlled capture
      ignores workspace masks and safely restores the current editing view.
- [ ] Visibility/hierarchy changes invalidate cached directional membership and
      affected lighting products; authored Hidden and off-screen shadow casters are
      correctly distinguished. Excluded blended casting/hidden-shadow modes are
      not exposed as authoring features.
- [ ] Exact project/root/path/key/camera request succeeds without example content,
      restored state, name matching or synthetic content.
- [ ] Request/build/schema/root/file mismatches fail before affected native use.
- [ ] Per-product expected state stays correct after partial/no-op cook and
      deliberate library priority changes; unavailable source is labelled honestly.
- [ ] The final V0.1 scope is covered; every required
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

## 13. Design Rationale And Eliminated Alternatives

| Alternative                                                                            | Reason for elimination                                                                                                                                                                            |
| -------------------------------------------------------------------------------------- | ------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| Ship a Validate command, protocol or shared production schema package                  | Qualification is a development workflow; this would add a production dependency without an independent authoring/runtime responsibility.                                                          |
| Add a validation flag to normal RenderScene                                            | Demo restoration, scene selection and camera conveniences are different from an exact controlled check. A private driver calls the same loader without changing the ordinary example's contract.  |
| Copy loading, projection, exposure or rendering algorithms into the harness            | A matching duplicate can conceal a defect in the actual product path. Both hosts execute the real production algorithms and share only development observation/capture code.                      |
| Treat a cook receipt, cooked values or live runtime values as the full authored oracle | Partial publications and source-less libraries have different provenance. Saved source supplies authored expectations; verified library inspection supplies explicitly labelled cooked baselines. |
| Accept matching images alone                                                           | Two wrong or empty renders can match. Independent required semantics, non-default visible cases and source identity must also pass.                                                               |
| Use command acceptance, startup frame counts or elapsed sleeps as capture completion   | None proves that the requested resources/profile produced the measured frame. Readiness, completed scene-frame identity, GPU fence and closed artifacts are required.                             |
| Pause authoring or hold the publication/rendering pause for embedded capture           | Authoring must continue and the captured view must render. A bounded development delivery gate preserves pending revisions while the real renderer advances.                                      |
| Resume by replaying every queued mutation or restoring the old snapshot                | Either can apply stale state after activation changes. A fresh coherent current projection covers pending revisions, followed by later valid work.                                                |
| Relax tolerances, resize/align images or mask failed content                           | These change the measured contract. Fix the rendering/data path and retain the original images and exact numerical gates.                                                                         |
