# Oxygen Editor PRD

Status: `active product requirements`

This document defines traceable product requirements for Oxygen Editor V0.1.
Architecture, ownership, and implementation contracts live in
[ARCHITECTURE.md](./ARCHITECTURE.md), [DESIGN.md](./DESIGN.md), and
[lld/](./lld/README.md).

LLDs, milestone plans, and validation summaries must reference `GOAL-XXX`,
`REQ-XXX`, or `SUCCESS-XXX` IDs from this document. Requirement IDs are
intentionally coarse so implementation can evolve without breaking traceability
on every small workflow step.

## 1. Product Position

Oxygen Editor V0.1 is the first production-quality vertical slice of the
editor. It is not a demo shell and not a temporary proof. Its primary user is
the engine developer who needs to validate that Oxygen scenes can be authored,
previewed through the embedded engine, cooked, and loaded by standalone runtime
code without manual file repair.

Technical artists and tools developers are secondary V0.1 users. Their needs
matter because V0.1 must establish real editor workflows, but breadth is
deliberately constrained to static scene authoring, scalar material authoring,
scoped source import, and live/cooked runtime parity.

## 2. Problem Statement

The editor already has important foundations:

- WinUI shell, routing, docking, tabs, and project/workspace concepts
- Project Browser and project-opening flow
- embedded Oxygen Engine loop and Vortex-rendered viewport
- scene documents, hierarchy editing, and managed scene/domain model
- transform, geometry, camera, and light components
- save, cook, cooked-root mount, and live-engine sync paths

The editor is not yet a dependable authoring product because:

- component and material editors are incomplete or inconsistent
- scene mutations are not uniformly command-based, undoable, or validated
- live sync is direct and imperative rather than a complete component adapter
  model
- content browsing, source import, descriptor generation, cooking, and cooked
  inspection are not one coherent asset workflow
- camera, lighting, environment, exposure, and tone mapping are not presented
  as coherent authoring surfaces
- failures are too often discovered through logs after the fact rather than as
  visible editor operation results

## 3. Goals

| ID | Goal |
| --- | --- |
| `GOAL-001` | Enable an engine-developer validation workflow: author a supported scene, preview it live, cook it, and load the cooked scene in standalone runtime. |
| `GOAL-002` | Deliver usable static scene authoring for procedural meshes, scoped source-imported geometry/material assets, scalar materials, camera, sun, atmosphere, exposure, and tone mapping. |
| `GOAL-003` | Establish embedded live preview parity for authored scene content while allowing editor-only overlays, icons, gizmos, debug visuals, and diagnostics to differ from standalone runtime. |
| `GOAL-004` | Establish a real scalar material editor baseline that can grow into texture and graph workflows later. |
| `GOAL-005` | Make procedural descriptors, scoped source import, generated descriptors/manifests, cooked output, mount state, and content browser selection understandable and actionable. |
| `GOAL-006` | Make failures honest: user actions that fail surface visible operation results and useful logs. |

## 4. Non-Goals

Out of V0.1 scope:

- replacing engine runtime systems with editor-only copies
- editing cooked binary data as source authoring data
- building compatibility bridges to hide bad engine APIs
- full material graph editing
- texture authoring, complex texture graph workflows, and advanced material
  node networks
- physics scene sidecar editing or physics simulation authoring
- animation, prefab, terrain, gameplay, and particle editors
- a full validation dashboard with filters and fix actions
- a generic project settings editor, user-selectable project renderer presets,
  a dedicated batch-recook-stale command, or dedicated descriptor/manifest
  editor/launcher actions; section 8 defines the supported replacement workflows
- autosave or crash recovery of changes after the last explicit successful Save
- treating logs as a substitute for operation results where the user initiated
  an editor action

## 5. Requirements

Requirements describe product behavior that must work as specified. The
verification method for that behavior is decided during implementation and
recorded by the owning LLD/milestone plan.

| ID | Requirement |
| --- | --- |
| `REQ-001` | The editor starts at the Project Browser, even when recent project state exists. |
| `REQ-002` | The Project Browser supports recent projects, create/open project, invalid project failure state, and transition into the editor workspace. |
| `REQ-003` | After a project is opened, the editor restores project workspace and recent document/layout state where possible, and makes partial restoration failure visible. |
| `REQ-004` | Scene authoring supports node create, delete, rename, and reparent operations. |
| `REQ-005` | Scene authoring supports add, remove, and edit operations for V0.1 scene components. |
| `REQ-006` | New V0.1 scene-authoring work uses command-based mutation paths that update dirty state. |
| `REQ-007` | Scene data save/reopen round trips supported V0.1 component and environment values. |
| `REQ-008` | Supported scene mutations request live sync when the embedded engine is available. |
| `REQ-009` | V0.1 scene authoring comprises Transform, Geometry, PerspectiveCamera, DirectionalLight, Environment, and Material assignment/override as bounded by section 8. Orthographic cameras, point lights, and spot lights are outside the supported V0.1 authoring/import qualification set; their existing domain data may be preserved but cannot be advertised as supported workflows. |
| `REQ-010` | Users can create and open scalar material assets through a real material editor. |
| `REQ-011` | Users can inspect and edit scalar material properties through material editor/property UI. |
| `REQ-012` | Users can assign material assets to geometry. |
| `REQ-013` | Users can select material assets from the content browser, with thumbnails or clear visual identity. |
| `REQ-014` | Editable V0.1 material values save, reopen, cook, and appear in embedded preview after successful publication. Explicit Save schedules incremental cooking under the content workflow policy; source-save success is independent of cook success. A CPU swatch is an approximation, not runtime parity evidence. |
| `REQ-015` | Content workflow supports procedural geometry descriptors. |
| `REQ-016` | Content workflow supports scoped source import for geometry and scalar material assets. |
| `REQ-017` | The editor generates descriptors/manifests for supported V0.1 scenes and referenced assets. |
| `REQ-018` | The editor cooks the current scene and referenced V0.1 assets into the project cooked output. |
| `REQ-019` | The editor refreshes and mounts cooked output after cooking. |
| `REQ-020` | The content browser shows source, descriptor/generated, and cooked states. |
| `REQ-021` | Asset picking uses asset identity, not raw cooked path text. |
| `REQ-022` | User-triggered import, cook, mount, save, sync, and launch failures produce visible operation results. |
| `REQ-023` | Engine/runtime and pipeline failures produce useful logs. |
| `REQ-024` | Diagnostics identify whether failure is caused by authoring data, missing content, cook output, mount state, sync, or engine runtime state. |
| `REQ-025` | Embedded preview renders visible scene content through Vortex. |
| `REQ-026` | Embedded preview applies every required editable V0.1 scene/environment field. Material source changes appear after successful publication under the accepted Save/import/demand cooking policy. Unavailable runtime retains authoring state and visibly pending sync; reconnect converges to the current document revision. Unsupported required fields block release. |
| `REQ-027` | The supported V0.1 live viewport layout remains stable and does not abort; multi-viewport layouts are deferred engine/editor work. |
| `REQ-028` | Each supported visible viewport presents to the correct surface/view; V0.1 support is single live viewport unless multi-viewport is explicitly re-scoped. |
| `REQ-029` | Users can navigate the editor camera and frame all/selected. |
| `REQ-030` | Preview parity means the same authored scene content is rendered. Editor overlays, gizmos, selection outlines, node icons, and diagnostics may differ from standalone runtime. |
| `REQ-031` | Late V0.1 viewport UX includes selection highlight. |
| `REQ-032` | Late V0.1 viewport UX includes transform gizmo UX. |
| `REQ-033` | Late V0.1 viewport UX includes icons for non-geometry nodes such as cameras/lights. |
| `REQ-034` | Late V0.1 viewport UX includes useful overlays and debug visual affordances. |
| `REQ-035` | Late V0.1 viewport UX must not block earlier authoring, sync, cook, and runtime parity work. |
| `REQ-036` | The PRD requires round-trip behavior, not one universal persistence schema. Each subsystem LLD decides whether to use engine descriptors directly, augment engine schemas, generate engine descriptors from editor data, or use a hybrid model. |
| `REQ-037` | Supported V0.1 data saves, reopens, cooks, and loads without manual repair. |
| `REQ-038` | Scene/material saves preserve the last valid saved file, acknowledge only their captured revision, retain newer edits, serialize writes, and reject external-write conflicts. Crash recovery is limited to the last explicit successful Save. |
| `REQ-039` | Cook consumes a coherent saved dependency snapshot, validates staged output, and publishes it with a brief preview suspension and rollback protection. Failed/cancelled work cannot corrupt the previously published cook or falsely report it current. |
| `REQ-040` | The qualified static/scalar import subset is reproducible from retained sources and configuration on a clean project copy. Unsupported authored/imported content fails visibly before publication; source data is never silently discarded or overwritten. |
| `REQ-041` | V0.1 qualifies 100 scene nodes and 1,000 logical catalog entries on the matched Windows x64 editor/runtime/cooker/schema build and performance conditions in section 9. Larger projects are unqualified, not subject to an artificial hard cap. |
| `REQ-042` | Development-only standalone qualification loads the selected published project output through an exact request, verifies content and controlled visual parity, and emits a machine-readable result tied to the source/cook/build identity. Its workflow and tooling are excluded from normal editor Debug/Release builds. |

## 6. Success Metrics

| ID | Success Metric |
| --- | --- |
| `SUCCESS-001` | The V0.1 workflow completes without manual file edits. |
| `SUCCESS-002` | Supported scene and material edits survive save/reopen. |
| `SUCCESS-003` | Live editor preview shows authored scene content. |
| `SUCCESS-004` | Cooked output loads in standalone runtime with expected geometry, materials, camera, directional light, atmosphere, exposure, and tone mapping. |
| `SUCCESS-005` | The supported V0.1 live viewport does not abort and presents to the correct surface; multi-viewport is deferred. |
| `SUCCESS-006` | Source import, descriptor generation, cook, mount, and standalone load have visible success/failure states. |
| `SUCCESS-007` | Material assets can be created, edited, assigned, cooked, and previewed through real editor UI. |
| `SUCCESS-008` | Viewport selection, node icons, and transform gizmo UX are delivered late in V0.1 rather than blocking early engine/editor plumbing. |
| `SUCCESS-009` | [IMPLEMENTATION_STATUS.md](./IMPLEMENTATION_STATUS.md) records one concise validation summary for the milestone. |

## 7. Validation Policy

Validation proves that the requirements work; it is not itself the product
requirement. Each LLD/milestone plan decides the right verification method for its
scope.

On 2026-09-15 the user explicitly required the entire standalone validation
workflow to remain development-only. Normal editor Debug and Release builds,
packages and the normal SDK must contain no qualification command, request
protocol, fixture, comparison harness or qualification runner. Opt-in test/tool
targets own that work and write to isolated development output directories.
Production renderer, loading and authoring fixes remain in their proper modules;
calling test instrumentation reusable does not justify shipping it.
This supersedes the earlier M08 proposal for a product Validate in Standalone
command. Ordinary field/input validation and cook/runtime compatibility checks
remain production responsibilities.

Pre-V0.1 backward compatibility is not a requirement. The user explicitly
rejected legacy fields, schema/alias compatibility and runtime fallbacks on
2026-09-15. Useful existing content migrates to the selected canonical model
using development tooling and normal recooking; the shipping product has one
current contract. Migration recovery protects source work without keeping a
legacy execution path. The authoring scope is being decided interactively in
the [M08 scope review](review/ED-M08-v01-authoring-scope.md); captured-sky diffuse
and specular lighting is approved, while the other proposed changes require
individual decisions.

Cheap, meaningful automated tests should be added when they give useful signal,
especially for domain logic, serialization round trips, descriptor generation,
asset identity, component defaults, settings behavior, and testable view-models.
Manual workflow validation is acceptable when automation is disproportionately
expensive, especially for WinUI and embedded-engine integration flows. Milestone
validation must reference the relevant `REQ-XXX` and `SUCCESS-XXX` IDs and be
summarized once in [IMPLEMENTATION_STATUS.md](./IMPLEMENTATION_STATUS.md).

## 8. Closed V0.1 Capability And UI Scope

This matrix is the release boundary. The linked field tables define the exact
editable fields within each row; engine/schema additions do not silently expand
V0.1. Read-only identity, diagnostics, and editor metadata do not require runtime
projection. All editable authored fields below require commands, validation,
undo/redo, save/reopen, cook/load preservation, and the stated preview behavior.

| Capability | Authoritative field/interaction contract | Preview and release condition |
| --- | --- | --- |
| Scene hierarchy and Transform | `scene-authoring-model.md`; `property-inspector.md` Transform table; `property-pipeline.md` | Create/delete/rename/reparent and transforms synchronize; hierarchy/IDs survive cook/load. |
| Geometry and material slot 0 | `property-inspector.md` Geometry table; `asset-primitives.md`; `content-pipeline.md` section 19 | Cube, SubdividedCube, Sphere, IcoSphere (including its GeodesicSphere alias), Plane, Cylinder, Cone, Quad, Torus, ArrowGizmo, and the qualified imported static geometry subset resolve after validated cook; identity changes synchronize. Every supported picker choice must cook. Other override-slot metadata may remain read-only. |
| Perspective camera | `property-inspector.md` PerspectiveCamera table | All editable fields synchronize and cook; validation uses an explicitly chosen authored camera, separately from editor navigation. |
| Directional light and sun | `property-inspector.md` DirectionalLight table | All editable fields synchronize and cook, including coherent exclusive sun binding. |
| Scene environment and post-processing | `environment-authoring.md` editable SkyAtmosphere, Sun Binding, Exposure, Tone Mapping, Bloom, Color Grading, and Background tables | All editable fields, including background and post-processing, must have live and cooked runtime mappings. A missing native API/schema is implementation work, not a release exception. |
| Scalar material | `material-editor.md` editable V0.1 field table | Swatch responds while editing; the scene shows the last published material until saved content is successfully cooked/published. Save schedules incremental cooking, with visible stale/pending state and session pause. Texture references may be preserved read-only; texture authoring is excluded. |
| Viewport authoring | `viewport-and-tools.md` V0.1 interaction contract | One live viewport, navigation, frame selected/all, picking, selection feedback, transform gestures, icons, and bounded overlays. Multi-viewport stability is explicitly deferred. |
| Content import and browsing | `content-pipeline.md` qualified import policy; `content-browser-asset-identity.md`; `content-cooking-workflows.md` | Identity-based browsing/picking, explicit scoped import/reimport and Cook actions, plus incremental cooking after Save/import and on active-scene/asset demand. Browsing and transient edits do not cook. File rename/move/reference-repair UI is outside V0.1; unsupported actions are hidden or disabled with a reason. |

Unsupported required capabilities may produce safe diagnostics during
development; they cannot satisfy release completion. The only deferred feature
inside the supported matrix is multi-viewport stability. PRD non-goals and the
explicit unsupported component/import set are exclusions, not incomplete
implementations that can be advertised as supported.

ED-M07 UI decisions:

ED-M07B's [content workflow refinement](lld/content-cooking-workflows.md) and
[UI review](validation/ED-M07B-ux-review.md) add concrete browsing, picking,
incremental execution and recovery requirements. The user accepted its hybrid
trigger policy D1 on 2026-09-11 when directing implementation of revised M07B.

1. No generic project-settings panel or default renderer-preset selector in
   V0.1. Project manifests/mounts supply cook scope; `Projects` owns those facts.
   Supported creation uses existing templates and target selectors. Scene
   render intent uses the scene inspector; FPS/logging remain runtime-session
   controls; startup preferences stay editor-local. Cook and validation use the
   matched runtime profile, not an undeclared project renderer policy.
2. Import and successful Save schedule incremental cooking of their saved scope;
   assignment and active-scene preview request required missing/stale assets.
   Existing Cook Selected Asset, Cook Folder, Cook Current Scene, and Cook Project
   remain explicit incremental actions. The existing Cook menu provides session
   Pause automatic cooking / Resume. Browsing and unsaved edits never cook;
   external-source reimport remains explicit. One project coordinator coalesces
   requests; no separate stale-only batch scheduler is introduced.
3. Descriptor/manifest inspection uses the existing content details/path-copy
   and cook result diagnostics, plus Inspect Cooked Output for runtime products.
   Dedicated Open Descriptor/Open Manifest commands and an embedded raw editor
   are excluded. Generated file paths needed for diagnostics remain discoverable
   and copyable; users never have to edit them to complete supported workflows.
4. Cooking does not silently save documents. Dirty participating scene/material
   documents must be saved explicitly before snapshot capture; a later edit
   remains dirty and makes the resulting cook stale, without invalidating a
   successful cook of the captured saved revision.
5. Cook runs against staging while authoring continues. Preview briefly pauses
   for validated publication and resumes from the current authoring scene.
   The pause and any failure are visible; fixed `.cooked/<Mount>` output paths
   remain the published project layout.

## 9. Compatibility And Validation Envelope

V0.1 supports Windows 11 x64 with the repository's .NET/WinUI runtime and an
Oxygen-supported D3D12 adapter. The ordinary Interop build records its installed
native SDK inputs in assembly metadata. Startup reads that metadata without
loading Interop and checks the installed native binaries. An SDK mismatch reports
which dependency changed and requests an Interop rebuild before native calls.
Managed/UI edits require no approval, qualification manifest, or separate probe
build. Debug and Release use the same automatic build-compatibility workflow.

Cooking checks its tools and matching editor/native schemas when requested and
captures producer hashes for incremental invalidation. These checks do not block
viewport startup. Regression tests run in existing test projects; no qualification
promotion command or dedicated probe project is required. Project Browser and
safe authoring/save remain available when native compatibility fails. Project
manifest schema version 1 is supported; unsupported versions are rejected
without rewriting.

Release validation records CPU, RAM, GPU/VRAM, driver, OS/runtime versions,
build configuration, and exact fixture hashes. This establishes support on the
recorded configuration, not a claim about all hardware satisfying a GPU name.

User-selected scale: 100 scene nodes and 1,000 logical authored catalog entries.
Derived companions must not inflate logical row counts. The fixture includes
hierarchy, 98 geometry nodes, one perspective camera, one directional sun,
environment settings, every exposed built-in shape and small imported meshes, shared and
distinct scalar materials. Pad the catalog with valid scalar descriptors to
exactly 1,000 entries. Keep visible geometry at or below 250,000 triangles.

Performance validation uses Release, a 1920x1080 live viewport, conventional directional
shadows, and the controlled settings in the standalone-validation LLD:

| Measurement | Required result on the recorded validation machine |
| --- | --- |
| Command/selection/field feedback | p95 at most 100 ms across 100 interactions after warm-up. |
| Warm catalog folder/filter update | p95 at most 250 ms across 100 queries. |
| Cold 1,000-entry catalog and 100-node scene opening | Each at most 5 seconds from request to usable UI, excluding explicit user decisions and first native initialization. |
| Live rendering after 120 warm-up frames | p95 frame time at most 33.3 ms over the next 600 frames, with no debug capture or validation layers. |
| Save/cook/validation operation start | Busy/progress state visible within 100 ms; UI remains responsive. |
| Repeated open/close and scene activation | 30 cycles without crash, orphaned document/runtime ownership, or monotonic growth of outstanding scene/view/surface leases. |

Measure CPU interaction timings separately from GPU frame time. Report all
failures rather than weakening the workload. Larger projects remain unqualified;
they may open if resources permit and must fail visibly without corrupting saved
data. No autosave/recovery of unsaved edits is promised; the last successful
explicit Save must remain valid after a crash or interrupted later write.

## 10. Release Acceptance

ED-M10 closes only after all non-deferred capability gates have fresh evidence
for the qualified build. ED-M08 proves the saved/published authoring slice;
ED-M09 adds viewport-tool gates; neither depends on completion of a later
milestone. The detailed ED-M08/09/10 plans own execution and evidence collection.
No prototype warning, prior milestone row, issue fix, or newly written document
substitutes for proof of a changed contract. Earlier evidence remains historical
evidence at its original scope; newly required guarantees are owned by explicit gap-closing milestones.
Previously closed milestone statuses and their original evidence remain intact.
