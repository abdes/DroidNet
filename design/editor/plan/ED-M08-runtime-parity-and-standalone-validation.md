# ED-M08 — Runtime parity and editor authoring workspace

Status: **in progress — M08.1, M08.F1, M08.2, M08.V0–V2 and M08.3–M08.8
validated; rescoped on 2026-10-08; M08.9 added on 2026-10-09 and next**

Current: **M08.9 Viewport performance and per-frame editor work**. The
[rescope](#retired-by-the-rescope) retires the development-only parity harness
(former M08.3 and M08.5–M08.8) and the former M08.4 audit, and replaces them
with what the editor still lacks as an authoring tool: viewport picking,
selection feedback, transform gizmos, scene helpers and a refined HUD, and a
Content Browser with real previews, working commands and drag-and-drop. ED-M09
is absorbed into M08.4–M08.6. See [remaining slices](#4-execution-sequence) and
[exit checklist](#7-exit-checklist). Captured-sky IBL is delivered by
[VX-IBL-01](../../../projects/Oxygen.Engine/design/vortex/milestones/VX-IBL-01/README.md);
its rendering, precision and publication contracts remain unchanged.

## 1. Outcome

The editor and native engine implement one canonical V0.1 authoring contract
(M08.1, M08.F1, M08.2). Every scene layout presents stable, independent
viewports whose state survives reopening (M08.V0–V2).

The editor becomes a workspace in which a content author assembles a scene
without touching the Inspector for placement: click to select, see the
selection outlined, frame it, move/rotate/scale it with snapping, see and pick
lights and cameras, and drag assets from a Content Browser that shows what
they look like. Every visible command works or explains why it is unavailable.

Runtime parity rests on one production path: the editor and the standalone
runtime share Vortex, the loaders and the cooked formats, and M08.1/M08.2
added native tests for every changed capability. The closeout loads an
editor-authored, editor-cooked project in the maintained RenderScene example.
No separate qualification harness is built.

The design mockup and brief (`F:/projects/oxygen-editor-design`, chapters
Viewport & input, Rotation feedback and Content Browser) set the layout,
density and interaction intent. WinUI 3 and the DroidNet controls are the
implementation; Oxygen contracts win where the mockup differs.

Trace: REQ-005/006/008/013/018–021/025–035/037/039–041;
SUCCESS-001/003/005/006/008.

## 2. Implementation document map

Each document owns the details listed below. This plan owns execution order and
acceptance gates; field defaults and wire contracts are not independently
redefined by the schedule.

| Document                                                                                                                 | Implementation authority                                                                                |
| ------------------------------------------------------------------------------------------------------------------------ | ------------------------------------------------------------------------------------------------------- |
| [PRD](../PRD.md), sections 8–10                                                                                          | Feature boundary, workload and release envelope                                                         |
| [V0.1 authoring contract](../review/ED-M08-v01-authoring-scope.md)                                                       | Complete scope, exclusions and rejected alternatives                                                    |
| [Visibility and light participation](../review/ED-M08-node-light-visibility-review.md)                                   | Local/Inherit flags, workspace Hide, contribution and shadow semantics                                  |
| [Celestial-light contract](../review/ED-M08-celestial-light-authoring.md)                                                | None/Primary/Secondary ownership, two contributors and conflicts                                        |
| [Scene authoring model](../lld/scene-authoring-model.md)                                                                 | Canonical scene/component identities, hierarchy and workspace-state separation                          |
| [Property inspector](../lld/property-inspector.md)                                                                       | Exact fields, units, defaults, validation and conditional UI                                            |
| [Material editor](../lld/material-editor.md)                                                                             | Scalar PBR/emission representation, editing, persistence and preview                                    |
| [Environment authoring](../lld/environment-authoring.md)                                                                 | Atmosphere, captured sky light, exposure, grading and background                                        |
| [Property pipeline](../lld/property-pipeline.md)                                                                         | Edit sessions, revisions, validation, mixed selection and history                                       |
| [Live engine sync](../lld/live-engine-sync.md)                                                                           | Full projection, asset completion, native mutation and convergence                                      |
| [Content pipeline](../lld/content-pipeline.md)                                                                           | Slot identity, migration, saved snapshots, provenance, publication, ordered mounts and native producers |
| [Runtime integration](../lld/runtime-integration.md)                                                                     | Build compatibility, scene/view lifetimes and production capabilities                                   |
| [Standalone qualification](../lld/standalone-runtime-validation.md)                                                      | Post-V0.1 development harness design; not executed by M08                                               |
| [Settings architecture](../lld/settings-architecture.md)                                                                 | Authored settings, local workspace state, runtime-session and startup preferences                       |
| [Viewport and tools](../lld/viewport-and-tools.md)                                                                       | HUD, view modes, picking, outline, framing, gizmos, snapping, helpers and drag-and-drop placement       |
| [Content Browser asset identity](../lld/content-browser-asset-identity.md)                                               | Browser layout, views, commands, thumbnails and drag sources                                            |
| [Documents and commands](../lld/documents-and-commands.md)                                                               | Save/close lifecycle, command outcomes and document ownership                                           |
| [Cooking workflows](../lld/content-cooking-workflows.md)                                                                 | Save/Cook/reimport/recovery actions exposed by the scene toolbar and Content Browser                    |
| [Engine deferred capabilities](../../../projects/Oxygen.Engine/design/vortex/milestones/ED-M08/deferred-capabilities.md) | Post-V0.1 exclusions and source-local TODO IDs                                                          |

### Native implementation references

| Document                                                                                                                                                                                                     | Native implementation authority                                                                                                     |
| ------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------ | ----------------------------------------------------------------------------------------------------------------------------------- |
| [V0.1 rendering contract](../../../projects/Oxygen.Engine/design/vortex/lld/editor-rendering.md)                                                                                                             | Concrete source owners and required changes for light enumeration, visibility, shadow receiving/contact, camera framing and grading |
| [Captured-sky IBL](../../../projects/Oxygen.Engine/design/vortex/lld/captured-sky-ibl.md)                                                                                                                    | Scene-global capture anchor, HDR products, diffuse SH, GGX filtering/BRDF integration, atomic publication and Stage 13 activation   |
| [Lighting service](../../../projects/Oxygen.Engine/design/vortex/lld/lighting-service.md), [shadow service](../../../projects/Oxygen.Engine/design/vortex/lld/shadow-service.md)                             | Direct-light/shadow family ownership, wire/resource contracts and established CSM filtering/bias behavior                           |
| [Environment service](../../../projects/Oxygen.Engine/design/vortex/lld/environment-service.md), [indirect lighting service](../../../projects/Oxygen.Engine/design/vortex/lld/indirect-lighting-service.md) | Environment product publication and canonical indirect surface evaluation; retirement of the Stage 12 ambient bridge                |
| [Cubemap processing](../../../projects/Oxygen.Engine/design/vortex/lld/cubemap-processing.md), [static skylight baseline](../../../projects/Oxygen.Engine/design/vortex/lld/skybox-static-skylight.md)       | Existing source orientation, SH/radiance normalization and static-cubemap behavior reused by the new IBL contract                   |
| [View initialization](../../../projects/Oxygen.Engine/design/vortex/lld/init-views.md), [post-process service](../../../projects/Oxygen.Engine/design/vortex/lld/post-process-service.md)                    | View/history ownership, exposure, output composition and per-view processing                                                        |

Closed VTX-M08 evidence proves its original static diffuse-only implementation.
The V0.1 rendering/IBL contracts explicitly extend that baseline; the ED-M08
implementation must produce new evidence for captured sky and specular lighting.

## 3. Scope and ownership

### Delivered canonical authoring (M08.1–M08.V2)

- Ten canonical primitives, including Capsule, using shared native recipes and
  the metric centred defaults: horizontal Plane, upright Quad and 1 m outer Torus.
- Stable per-geometry material-slot identities and independent instance overrides
  for all existing slots; clearing restores the mesh material.
- Scalar emission colour/intensity with float32 storage, explicit linear-colour
  conversion and finite HDR bounds.
- Local/Inherit visibility and geometry shadow flags, independent light
  contribution/shadow controls, and functional GPU receiving/contact shadows.
- Exact authored perspective/orthographic camera selection with Auto/Fixed
  per-view framing; viewports look through, pilot and align authored cameras.
- Per-light None/Primary/Secondary atmospheric assignment, two complete shadowed
  contributors and independent ordinary directional fill lights.
- Captured-sky diffuse/specular lighting and effective behavior for every
  retained environment/post-process/light field, including colour grading.
- One-time migration to canonical readers/writers (M08.1.4); no compatibility
  reader survives.
- One- to four-pane layouts with independent panes, focus routing, a camera
  preview inset and persisted per-pane state.

### Remaining deliverables (M08.3–M08.11)

| Slice  | User-visible outcome                                                                                   |
| ------ | ------------------------------------------------------------------------------------------------------ |
| M08.3  | One icon-led viewport HUD; view modes that change the render; layout picker; scene document toolbar.   |
| M08.4  | Click/marquee picking, selection outline in every pane, frame selected/all.                            |
| M08.5  | Native translate/rotate/scale gizmos, World/Local, snapping, tool rail and viewport editing shortcuts. |
| M08.6  | Light/camera icons, selected-helper visuals with editable range/cone handles, orientation triad.       |
| M08.7  | Redesigned Content Browser: sources/results/details layout, three views, sort, working commands.       |
| M08.8  | Rename, move, cut/copy/paste and delete that update every authored reference in one transaction.       |
| M08.9  | Smooth multi-pane viewports: one frame clock, panes render on demand, lean per-frame editor work.      |
| M08.10 | Rendered thumbnails for materials, geometry, textures and scenes in browser, pickers, Material Editor. |
| M08.11 | Drag-and-drop scene assembly, Browse to asset and the editor-to-runtime closeout walkthrough.          |

### Retired by the rescope

- **Development qualification harness** (former M08.3, the M08.5 preparation
  adapter, M08.6–M08.8 and the former qualification constants): opt-in
  native/managed targets, saved-revision capture, a standalone child process
  and semantic/image comparison. It would compare the production renderer with
  itself, cost several slices of development-only code and add no authoring
  capability. The [standalone LLD](../lld/standalone-runtime-validation.md)
  remains a post-V0.1 design.
- **Editor document migration and repair workflow** (former M08.5): M08.1.4
  migrated every maintained project and there are no external projects.
  Missing/broken reference detection remains as delivered by ED-M06.
- **Workspace-Hide representation mask** (former M08.4): Hide keeps removing
  geometry from the editing view; keeping hidden casters and lights
  contributing is post-V0.1.
- **Formal inspector audit** (former M08.4): the inspectors were rebuilt and
  validated by M08.2 and the inspector refactoring plan. Defects found while
  using them are fixed as bugs in the slice that finds them.

### Architecture decisions for M08.3–M08.11

1. **WinUI draws chrome; Vortex draws anything anchored in 3D.** The HUD,
   flyouts, tool rail, readout chips, marquee rectangle and Content Browser are
   WinUI/DroidNet controls. Grid, selection outline, gizmos, icons, helpers and
   the orientation triad are rendered per view by an editor overlay stage, so
   they are depth-correct and never lag native navigation.
2. **Generic engine primitives, editor policy in Interop.** Vortex gains
   reusable overlay primitives (lines, screen-sized billboards and handle
   meshes; depth-tested, always-on-top and occluded-dimmed modes) and an
   on-demand ID/depth pick pass. Interop `EditorModule` decides what to draw
   from selection, tool and settings. The standalone runtime never enables the
   editor overlay stage.
3. **Overlays and tools are view state.** They never enter the authored scene,
   history, Save or Cook; they are composed after post-processing and are
   excluded from exposure metering and grading. Pane-level toggles persist with
   the M08.V2 pane state; snapping is a per-user editor setting and the
   active tool is session state.
4. **One mutation path.** Native code performs hover and drag math without a
   managed round trip per pointer move. Managed code owns begin/commit/cancel:
   a drag previews through the existing property edit session (live
   projection, no history), release commits one undoable command for every
   target, and Escape restores the starting values exactly.
5. **Thumbnails are a derived local cache.** The production renderer draws
   them offscreen; they are keyed by asset identity and content revision,
   stored in the project's local cache, and never authored, cooked or required
   for browsing.
6. **Panes render on demand; the user owns the frame clock.** A pane renders
   when what it shows changes and for a short settle window afterwards; an
   idle pane keeps its last image and costs no rendering, composition or
   present. VSync and the frame-rate cap are engine-wide settings in the
   scene editor's Settings flyout; the editor imposes no rate of its own, and
   each frame has exactly one clock: the display with vsync on, the cap with
   it off.

Owners: WorldEditor `Viewport`, `ViewportViewModel`, `SceneEditorViewModel`
and the scene document toolbar; ContentBrowser shell, panes and asset
provider; Runtime view/input/pick contracts; Interop `EditorModule`,
`EditorView` and commands; Vortex overlay and pick passes and offscreen views.

Engine edits that leave a deferred boundary carry `TODO(post-v0.1, <ID>)`
linked to the engine scope record. Current M08 obligations are not future TODOs.

## 4. Execution sequence

Each slice ends with focused checks and a buildable code/test/doc commit. Native
contracts and rendered behavior pass before editor implementation relies on them.
From M08.3 on, every slice closes only after the user has reviewed the running
editor UI; implementation pauses at that point for feedback. Order: M08.1 →
M08.F1 → M08.2 → M08.V0 → M08.V1 → M08.V2 → M08.3 → M08.4 → M08.5 → M08.6 →
M08.7 → M08.8 → M08.9 → M08.10 → M08.11. Viewport slices come first for
immediate value; M08.7 and M08.8 are managed-only. M08.9 precedes thumbnails
because previews render alongside the viewports and need the frame budget it
recovers.

### M08.1 — Native canonical data, producers and primitives

Completed foundations: canonical primitives and axial Physics mapping;
Local/Inherit native records; atmospheric role records, import and conflict
validation; captured-sky toggle removal; native slot IDs, inventory validation
and revision hashing. Their remaining rendered/editor qualification stays in
the owning later slices.

#### M08.1 remaining increments

| Increment                        | State     | Deliverable and acceptance                                                                                                                                                                                                                                                                                                                                      |
| -------------------------------- | --------- | --------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| M08.1.1 Slots                    | validated | Preserve source declarations through mesh optimization; native provenance, cooked inventories/assignments, Inspector metadata and identity-based instance APIs. Reimport/cache deletion preserve proven IDs; replacement never transfers overrides by ordinal. Test nonzero slots, distinct equal-material declarations, all LOD bindings and clear-to-default. |
| M08.1.2 Cameras                  | validated | Auto/Fixed source, native/managed records and command transport; hydrate every camera. Resolve projection per target without rewriting authored ratio. Test imports, round trips, invalid inputs and target resize. Fixed bars/metering remain M08.2.                                                                                                           |
| M08.1.3 Emission                 | validated | Float32 cooked RGB and canonical colour/intensity sources; native version rejection, editor templates/adapters and fixture producers. Retire the affected managed binary writer route. Test finite HDR, 9.7 precision and source colour preservation at zero intensity.                                                                                         |
| M08.1.4 Cutover                  | validated | Publish standalone retained imports as immutable generations through Cooker/Content; upgrade maintained editor projects and retained settings, recook demo/source imports and PAKs, refresh SDK/Interop, and verify normal editor loading. Reject retired formats; retain no compatibility reader or dual source representation.                                |
| M08.1.5 Runtime identities       | validated | Intern full typed identities with mounted-source instance identity; reuse opaque IDs in cache, dependencies and in-flight work. Remove hash-as-identity, duplicate reverse registries and packed runtime source IDs. Prove forced-collision separation, same-SourceKey refresh isolation, lazy reload after eviction and bounded locator metadata.              |
| M08.1.6 Automatic load ownership | validated | Content-owned automatic checkout controls, explicit borrowing and residency pins; remove manual release balancing. Prove per-request ownership across coalesced loads, dependency transfers, cancellation, off-thread destruction and shutdown; preserve allocation-free IBL inspection.                                                                        |
| M08.1.7 Integrity inventory      | validated | Native loose index owns complete file sizes/digests and membership; protected readers reuse verification. Remove duplicate managed output proofs. Reject tampering, missing/extra members and stale verification; bump the index format and recook.                                                                                                             |
| M08.1.8 Project publication      | validated | Select one immutable ordered root set through an atomic project head; retain authored-source CAS and preview rollback. Remove cooked-directory swap/recovery phases. Keep existing incremental staging seed copies initially. Qualify multi-root crash recovery, conflicts and old readers.                                                                     |
| M08.1.9 Native analysis          | validated | Batch native source dependency/output analysis against captured input proofs. Managed orchestration keeps project resolution, dirty state and snapshot ownership. Remove parallel descriptor dependency parsers; prove analyzed/cooked closure equality and bounded process launches.                                                                           |

M08.1.7 also separates event-driven cooking freshness from integrity validation.
Badge refreshes reuse status snapshots; they neither launch tools nor hash cooked
payloads. Full native verification remains at cook reuse/publication and explicit
validation; normal mounts retain metadata admission. Unknown availability is
neutral, and observed missing output offers cooking.

Damaged shared roots rebuild automatically from known sources into empty staging;
unknown auxiliary ownership or missing sources fail without changing publication.
Native per-source reports retain auxiliary-file ownership, without duplicating
inventory hashes. Named texture descriptors participate in output association,
freshness, repair and exact material/scene reference resolution. Native job order
uses the captured dependency graph, including generated-shape default materials.

M08.1.8 closes in two implementation checkpoints, keeping engine and editor
commits separate:

1. **M08.1.8.1 native admission — validated:** prepare the complete source set off the engine
   thread; preserve generation leases and old source-qualified objects. Validate
   lifetime/restart epoch and mount revision before callback-free loader/resolver
   swaps. Deliver prepared retirements after both owners switch. Native imports
   accept a fresh root identity through the existing request contract.
2. **M08.1.8.2 publication cutover — validated:** one leased immutable document owns root order,
   input identities and provenance; one atomic head selects it. Migrate catalogs,
   inspection, cooking, mount changes and texture references to that snapshot.
   Replace root-directory swap recovery with head/source recovery. Reclaim
   superseded generations automatically during owned maintenance, excluding
   selected, recovering or leased generations. Keep ordinary staging copies and
   exclude generation markers until sealing. Rebuild maintained project outputs.

The gates are failed/stale multi-root preparation, reentrant eviction reloads,
old-reader retention, source/head crash boundaries, source CAS conflicts, catalog
head refresh, texture references after consecutive publications and safe automatic
reclamation. Native source analysis is delivered by M08.1.9.

M08.1.9's three delivery stages are validated:

1. **M08.1.9.1 native input observations — validated:** extend the existing source snapshot
   to retain successful reads and presence/absence/metadata probes. Preserve
   original I/O errors, reject contradictory observations and verify the same
   facts before publication. Test missing-file appearance, metadata changes,
   cancellation and ranged reads. Keep this mechanism in Cooker.
2. **M08.1.9.2 shared preparation and batch analysis — validated:** extract preparation from
   native descriptor builders/jobs and model adapters. Analysis and cooking use
   the same validation, references, naming and recipe interpretation for
   materials, textures, geometry, projected scenes and glTF/FBX. Expose declared
   outputs and attributed input observations through one batch tool contract;
   analysis writes no cooked output. Enforce captured input membership during
   cooking and test analysis/cook dependency equality.
   Commit checkpoints:
   - **M08.1.9.2.1 native preparation and analysis — validated:** shared source preparation,
     batch analysis, captured-reader ownership and model-read enforcement.
     Debug: 667 owning-suite tests; Release: 106 contract/model tests; each
     configuration passes four CLI tests. Scoped tidy, MSVC warning checks and
     extra-high review are clear.
   - **M08.1.9.2.2 captured batch execution — validated:** capture-map ingestion,
     observed descriptor/provenance ingress and all-family dependency enforcement.
     Full engine/examples Debug/Release builds and the installed-SDK C++20
     consumer pass; 602 owning tests and 11 CLI tests pass per configuration.
     All 16 Content scenes recook, and packaging reports no warnings/errors.
     RenderScene validates loose and PAK scene replacement, IBL and matching
     appearance. Scoped tidy, compiler warnings and extra-high review are clear.
3. **M08.1.9.3 editor cutover — validated:** batch each unresolved dependency frontier,
   capture and compare the reported input proofs, then cook through the native
   contract. Remove duplicate managed cook-dependency parsers. Retain project
   resolution, source editing, dirty-document policy and progress/diagnostics;
   badge refresh uses accepted dependency facts without launching native tools.
   Qualify bounded process launches and real import/cook/reimport workflows.
   Commit checkpoints:
   - **M08.1.9.3.1 native client and capture transport — validated:** typed reports,
     leased schemas, correlated batch queries and capture-map transport. Replacement
     analysis preserves logical identity without installing incoming sources. The
     exposed worker drain includes file/artifact cleanup. Debug/Release each pass
     73 client/compatibility tests and the installed-tool analysis/capture/cook
     roundtrip; native analysis/snapshot suites, 14 CLI cases per configuration,
     the C++20 SDK consumer, scoped tidy and extra-high review also pass.
   - **M08.1.9.3.2 discovery and status cutover — validated:** replace managed cook parsers,
     batch frontiers, verify captured observations and publish accepted facts;
     keep badge reads passive and close real editor workflows. Workspace acceptance
     includes visible content-refresh failures and project-scoped preview
     preferences (60 FPS / Error defaults), as specified in the settings LLD.
     Static textured glTF/FBX imports use the shared native policy and retain
     external images; unsupported animation/skinning and unmapped texture
     channels remain explicit errors. Validate import/reimport from retained
     sources and verify non-placeholder cooked texture bindings.

     Debug/Release pipeline suites pass 514 tests each, with affected-scope
     followups after the tangent-policy and cleanup changes. Eighteen rendered
     editor checks cover import, replacement, source-independent retry,
     picking/history/reopen, cooking feedback and runtime preferences. Four
     maintained projects recook and render in the normal Release editor; a
     rejected publication produces a persistent error banner and Retry restores
     rendering. Native texture/AO/tangent checks, RenderScene loose/PAK captures,
     the refreshed 16-scene library and four retained models pass. No-op cooks
     and passive badge reads launch no native workers. Extra-high correctness
     and complexity review is clear.

M08.1.7's whole-slice review is complete: unnecessary complexity, duplicate state,
owner/API integration and C++20/23 use were reviewed, with affected checks rerun
after corrections. The validation ledger records the accepted scope.

Execute M08.1.5 → .6 → .7 → .8 → .9 after the current-format M08.1.4
checkpoint. These are the approved simplification order; all are validated.
The separately planned [M08.F1 format milestone](ED-M08.F1-descriptor-local-references.md)
then precedes M08.2. Each format change owns its own migration and recook;
M08.1.4 verification is not deferred until F1.

M08.1.4 closes in small reviewed checkpoints:

| Checkpoint                   | Status    | Exit check                                                                                                                                                                            |
| ---------------------------- | --------- | ------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| Retained upload results      | validated | Debug/Release full engine builds; 103/102 upload, geometry and texture tests; delayed consumers, reentrant close/progress, cancellation, device loss and Debug allocation failure.    |
| Bounded resource maintenance | validated | Full Debug/Release/Tracy builds; 193 Debug and 184 Release resource/lifecycle tests; scoped tidy clean; native scene replacement with debug-layer and IBL checks. CPU baseline below. |
| SDK/editor workflows         | validated | Release SDK/Interop/app build; 13 native UI cases; all four normal projects open/render/Save; automatic publication and zero-node Save/reopen pass.                                   |

The [upload owner](../../../projects/Oxygen.Engine/design/vortex/lld/upload.md#result-ownership)
defines lifetime and maintenance contracts. Each checkpoint keeps its tests and
owner documentation with the code; no expiry workaround or GPU wait is introduced.

**Maintenance baseline — 2026-09-29.** Ryzen 9 9950X, RTX 3080 (610.62),
MSVC 14.51, Release; source is the bounded-maintenance checkpoint containing
this table. CPU tests use FakeGraphics, five runs with default 64-LOD reclamation.
Registry rows report the median across runs; geometry reports the observed range.

| Measurement                                                    | Result                                                           |
| -------------------------------------------------------------- | ---------------------------------------------------------------- |
| Remove one resource with 0 / 1,024 / 8,192 unrelated resources | 0.2 / 0.2 / 0.2 µs median                                        |
| Repoint one equivalent view with 64 / 1,024 / 8,192 aliases    | 0.1 / 0.2 / 0.2 µs median                                        |
| Reclaim 4,096 geometry assets, 64 LODs/frame                   | First frame 0.131–0.192 ms; p95 0.127–0.143 ms; maximum 0.251 ms |
| Native geometry completion publication                         | 444 Tracy samples; p95 0.120 µs; maximum 63.881 µs               |
| Native texture completion publication                          | 888 Tracy samples; p95 0.401 µs; maximum 77.798 µs               |

Reproduce CPU measurements from `projects/Oxygen.Engine` with Release binaries
`Oxygen.Graphics.Common.ResourceRegistry.Tests.exe` and
`Oxygen.Vortex.GeometryUploader.Tests.exe` under `out/build-ninja/bin/Release`,
using `--gtest_also_run_disabled_tests --gtest_filter=*Benchmark.* --gtest_repeat=5`.

The native baseline uses RenderScene's `ibl_persist_and_replace` UI test at
1920×1080, 60-FPS cap, Sponza → Lantern → Sponza, with isolated settings and
`OXYGEN_UI_TEST_FILTER=ibl_persist_and_replace`. Run the Tracy preset and export
`Vortex.Geometry` / `Vortex.Texture` zones with `tracy-csvexport -u -f`.
This replacement retains cached assets; the native timings measure publication,
not large-unload driver cost. Large-unload bounds and viewless cleanup are covered
by the CPU baseline and lifecycle regression. The separate debug-layer run passed
in 11.69 s with no D3D12 errors. Traces, screenshots and logs stay outside Git.

#### M08.1.5 qualification and locator baseline

Full Debug/Release engine and example builds pass without MSVC warnings.
Content plus renderer resource suites pass **310 Debug / 304 Release** cases.
The selected four tidy checks pass across modified files. Source review is clear.
Both SDK configurations, Interop and normal editor builds pass; existing managed
analyzer warnings remain in unchanged editor code. RenderScene passes
`ibl_persist_and_replace` and `ibl_reopen` with isolated settings, Sponza/Lantern,
1920×1080 at 60 FPS, and the D3D12 debug layer, with no debug-layer errors.

Regressions cover exact hash collisions, same-key refresh with changed payloads,
revocation during dependencies, independent generation reopen, lazy reload after
eviction, supplied-byte provenance, synthetic script reload, allocation failure
rollback and bounded locator retirement. Existing-ID inspection allocates nothing.

Release baseline (2026-09-29, MSVC x64; 65,536 locators; median of three runs):

| Measurement                                             | Result   |
| ------------------------------------------------------- | -------- |
| Intern a new cooked locator                             | 115.9 ns |
| Find an existing full identity                          | 15.4 ns  |
| Cooked locator allocator bytes                          | 7.50 MiB |
| Synthetic allocator bytes after retiring the cooked set | 9.50 MiB |
| Empty registry bucket capacity after the mixed-set peak | 4.00 MiB |

The memory figures measure interner allocator requests. The second phase briefly
holds both sets; buckets retain that peak capacity for reuse. Synthetic identities
remain valid for the loader's producer lifetime, independently of payload eviction.

Reproduce from the engine root:

```powershell
./out/build-ninja/bin/Release/Oxygen.Content.ContentIdentity.Tests.exe `
  --gtest_also_run_disabled_tests `
  --gtest_filter=ContentIdentityBenchmark.DISABLED_LocatorCost --gtest_repeat=3
```

#### M08.1.6 qualification and ownership baseline

Automatic request controls and immutable parent bindings replace manual release
balancing and the duplicate dependency graph. Exact cache-incarnation tickets
isolate replacements; frame-start processing handles 128 returned controls,
including frames without views. Explicit trim, pressure recovery and shutdown
drain fully. The [Content contract](../../../projects/Oxygen.Engine/src/Oxygen/Content/Docs/deps_and_cache.md)
owns lifetime and failure semantics.

Full Debug/Release engine and example builds pass without MSVC warnings;
**422 Debug / 419 Release** focused tests pass. Both SDK installs, Interop and
normal editor builds pass; existing managed analyzer warnings remain in unchanged
editor code. The selected four tidy checks are clean. Source review and regressions
cover callback-driven destruction/restart, allocation failure, off-thread returns,
coalesced delivery, retained children and generation-isolated script reload.
RenderScene passes `ibl_persist_and_replace` and `ibl_reopen` with isolated
Sponza/Lantern settings, 1920×1080 at 60 FPS and the D3D12 debug layer.

Release CPU baseline (2026-09-29, Ryzen 9 9950X, MSVC 14.51 x64):

| Measurement                                              | Result   |
| -------------------------------------------------------- | -------- |
| Warm acquisition and enqueue, median across three runs   | 85.9 ns  |
| Drain 128 records, median                                | 2.8 µs   |
| Drain 128 records, P95                                   | 2.9 µs   |
| Destroy a 64 MiB BufferResource, median across runs      | 1.589 ms |
| Destroy a 64 MiB BufferResource, largest observed sample | 3.254 ms |

Each run measures 512 batches of 128 acquisitions and 32 large-payload releases.
The batch limit bounds bookkeeping; individual CPU payload destruction remains
synchronous. Existing entry-unit budget accounting is unchanged; byte weighting
is tracked as [CNTT-BUDGET-01](../../../projects/Oxygen.Engine/src/Oxygen/Content/Docs/implementation_plan.md#cpu-budget-accounting).

Reproduce from the engine root:

```powershell
./out/build-ninja/bin/Release/Oxygen.Content.ContentOwnership.Tests.exe `
  --gtest_also_run_disabled_tests `
  --gtest_filter=ContentOwnershipBenchmark.DISABLED_AcquisitionAndReleaseCost `
  --gtest_repeat=3
```

Qualify the engine and all maintained examples before editor validation: finish
native content migration, retained reimport, loose/PAK loading and bounded runtime
checks first. Keep the existing IBL rendering and performance contracts intact.

Cutover includes shared [Base filesystem support](../../../projects/Oxygen.Engine/src/Oxygen/Base/Docs/Filesystem.md)
at native I/O boundaries. Qualify retained imports beyond Windows' legacy path
limit, logical record paths, and DemoShell library restoration by authored record.

M08.1.4 includes genuine zero-node cooked scenes. Preserve scene-level environment
and authored identity without placeholder nodes or omitted outputs. Qualify
descriptor generation, native cooking/loading and both formerly blocked
`NewScene2` project scenes; keep invalid component references rejected.

Owner contracts: [Content identities/ownership](../../../projects/Oxygen.Engine/src/Oxygen/Content/Docs/deps_and_cache.md#identities),
[native integrity inventory](../../../projects/Oxygen.Engine/src/Oxygen/Content/Docs/loose_cooked_content.md#complete-integrity-inventory),
and [project publication/analysis](../lld/content-pipeline.md#23-native-source-analysis).
Fix the wrong-type cache-checkout retain and qualify physics-sidecar hashes after
PAK relocation as focused correctness work, separately from the ownership and
format redesigns.

M08.1.1 also delivers the approved native retained-model publication contract:
immutable per-source generations under existing Content, selected atomically
with native provenance in the authored import record. DemoShell and CLI use the
same Cooker API; no writes beside external inputs or whole-root copies. Validate
interruption, concurrent publication, settings changes and old-reader lifetime.
The [Cooker owner design](../../../projects/Oxygen.Engine/src/Oxygen/Cooker/Docs/Import/async_import_pipeline_v2.md#retained-model-publication)
owns storage and lifecycle details.

Editor persistence, descriptor export and existing command transports move with
these contracts so upgraded projects remain usable. New inspector/repair UX and
the general migration/recovery workflow retain M08.4/M08.5 ownership. One-time
cutover scripts and recoverable backups stay in ignored local output.

Resolve slot identity at import, load, edit and geometry replacement, then use
the existing indexed render cache. Do not add UUID lookup, provenance hashing
or extra allocations to draw submission. Separate source declarations retain
independent bindings even when their default material matches.

The implementation review uses UE5.7.4 at `F:/Epic Games/UE_5.7`:
`StaticMesh.h`/`StaticMeshComponent.cpp` for slot/default ownership,
`CameraComponent.h`/`CameraStackTypes.cpp` for authored versus target aspect,
and `Math/Color.h` for linear float32 colour. Importer-specific continuity and
Oxygen's established vertical-FOV convention remain authoritative. Detailed
contracts and reference rationale belong in the owner documents below.

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
inputs and procedural identities; ArrowGizmo stays an internal tool resource.

IcoSphere is the sole canonical API, catalog, schema and generator identity for
the subdivided icosahedron. Native/managed callers, examples, fixtures and
documentation use that identity. Recook converted content and remove temporary
conversion code. Retain no duplicate alias, compatibility reader or migration
handler. Verify the single-identity contract across the repository, including
managed clients, maintained examples, tools and historical documents.

Remove the ineffective real-time-capture toggle from canonical SkyLight records,
adapters and demo controls. Captured sky has one source-change-driven production
policy; migrate its existing source/enable/parameter values and recook.

Perform native source/recipe migration and recooking in this slice. Generate
one-time migration scripts in the build tree; retain source-tree tooling only
when it has an ongoing use. Refresh every affected maintained bundle and
RenderScene source import before M08.3 loads it. Migration validates inputs,
preserves recoverable backups, updates references and invokes current producers;
no obsolete reader survives.
M08.5 extends this foundation to editor document migration and repair workflows.

Owners: `Oxygen/Data` format/catalog/procedural files; `Oxygen/Scene`;
`Oxygen/Cooker/Import` and schemas; native Inspector; shared scene hydration.

Canonical axial primitives use Oxygen's Z axis in both generated geometry and
analytic Physics shapes. Adapt backend primitive bases before authored local
transforms; do not change engine world axes or compensate in mesh generation.
Capsule render height includes the hemispheres; Physics cylindrical half-height
maps through `height = 2 * (half_height + radius)`. Qualify that relationship,
including the sphere limit, body/shape transforms and affected maintained source
recipes, before closing primitive parity.

Checks: schema/round trips; slot continuity and structural changes; nonzero
slot overrides; Local/Inherit; hidden/off role conflicts; Auto/Fixed records;
finite HDR precision; primitive bounds/attributes/sidedness; obsolete format
rejection. Cook and inspect through native tools, never managed binary decoding.

### M08.F1 — Descriptor-local reference format

Status: **validated — closed on 2026-10-06**. Depends on M08.1.9; prerequisite for M08.2 and subsequent
qualification. The [format plan](ED-M08.F1-descriptor-local-references.md) owns
execution, version changes, recooking and its
[closure verification](ED-M08.F1-descriptor-local-references.md#closure-verification). Data
and Cooker own the wire contract and packaging behavior; this milestone
introduces no compatibility reader.

### M08.2 — Native rendering and view behavior

M08.2 finishes the authoring surface the editor already shows and completes the
light-editing stack. The content author can already place and transform objects,
assign materials, add directional/point/spot lights and orthographic cameras, and
set environment/exposure/tone mapping. Five gaps remain: one missing control,
three controls shown but inert, and one complete light-type UI suite. Each item
below is a content-author need, not an internal engine seam.

1. **Every edit takes effect immediately.** When the author sets a light node's
   `Visible` flag to Hidden, changes its role (None/Primary/Secondary), or flips a
   Cast/Receive Shadows flag, the view must relight on that edit alone. Today the
   cached light list and captured-sky lighting are not recomputed on a flag-only
   change, so the edit looks like it did nothing until an unrelated edit refreshes
   the cache. Deliver: emit a mutation on flag/role changes and invalidate the
   directional-light resolver, the per-frame light selection, and the captured-sky
   products in the same operation that applied the change.

2. **Node "Rendering" section.** The author cannot yet choose which objects render
   or cast/receive shadows. Add a "Rendering" property section to the node
   Inspector — Scene Visibility, Geometry Cast Shadows, Geometry Receive Shadows —
   placed after the component inspectors. These are node flags the engine already
   stores and consumes; the section wires them through command/history/Undo/Save/
   cook and live projection.

3. **Color grading takes effect.** The author already sees Saturation, Contrast and
   Vignette in the environment inspector; editing them does nothing today. Add the
   shader consumers in the fixed order — exposure once → Rec.709 saturation →
   linear-light contrast about 0.18 → tone curve → content-ellipse vignette →
   display gamma → background/coverage composition — so the sliders grade the
   frame, including the clear-background colour and foreground transparency.

4. **Camera framing and orthographic editor.** The author can set Aspect Mode = Fixed on a
   camera; today the image stretches instead of showing letterbox/pillarbox bars.
   Auto fills the target with the unchanged vertical FOV (perspective) or
   OrthographicSize (orthographic); Fixed fits the authored ratio in a centred
   content rectangle with bars composed after post-processing and excluded from
   metering and grading. One framing contract serves both projection types. Add the
   orthographic camera sync command (`RuntimeAttachOrthographicCamera`) to enable
   rendering of authored orthographic cameras in the editor view, and add the editor
   property section (OrthographicSize, AspectMode/AspectRatio, near/far) following
   the perspective camera pattern. Physical exposure stays deferred.

5. **Point and Spot light editors.** The author can already create point and spot
   lights and the native rendering exists, but the Inspector has no controls for
   them. Add editor property sections reusing directional-light patterns and common
   inspector controls. Point lights expose lumens, range and source radius; spot
   lights additionally expose inner and outer cone angles. Wire both through
   command/history/Undo/Save/cook and live projection. Both lights support the
   same shared fields as directional (colour, Affects Scene, Cast Shadows, contact
   shadows, shadow bias/normal-bias/resolution, exposure compensation).

Pushed to M08.4: the editor representation mask — the native per-view delivery
that keeps workspace-Hide geometry casting and lighting. The eye toggle already
hides geometry in the editing view; preserving caster/light eligibility for hidden
geometry is a refinement of that shipped feature. This work defers to M08.4 as
it becomes essential only for large scenes, which is not the case for current
content validation.

Owners: `Scene/Scene.*` mutation dispatch and `Scene/SceneTraversal.h`;
`Scene/Light/DirectionalLightResolver.*`; `SceneRenderer::BuildFrameLightSelection`;
`Scene/Camera/Perspective`, `Scene/Camera/Orthographic`, `Vortex/SceneCameraViewResolver`,
`Core/Types/ResolvedView`, InitViews/SceneTextures and composition;
`Vortex/PostProcess/*` and `Tonemap.hlsl`/`Exposure.hlsl`. Light and camera editors:
WorldEditor inspector/commands, `World` light component types, camera property sections
and property slots, and the Interop light/camera-command transport. The node "Rendering"
section: `World` node-flag slots (`RenderingSlot` / `LightingSlot`) and flag-command
transport.

Checks (focused native tests and rendered evidence precede M08.3): flag/role edits
invalidate the light list and captured sky after cache population; the three node
"Rendering" flags round-trip edit/Undo/Save/reopen with live effect, including a
locally Shown child under a Hidden parent; the Saturation/Contrast/Vignette golden,
neutral, None, background and transparent-coverage cases; Auto resize without
authored mutation and the Fixed 4:3/16:9 bar fits. Point/spot light edits
(range, lumens, cone angles) with live effect and history/Undo/Save/reopen;
orthographic camera edits (OrthographicSize, AspectMode/AspectRatio, near/far)
with live effect and history/Undo/Save/reopen; orthographic framing with Auto resize
and Fixed fit; orthographic sync command attaches authored cameras to editor views.
Preserve existing material-sidedness and mirrored-winding correctness.

#### M08.2 status

Status: **validated — closed on 2026-10-08**. Every item passed the editor
workflow checks above.

| Item                 | Delivered                                                                                                                                                                                                                                                                                                                                                                                        |
| -------------------- | ------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------ |
| 1 Invalidation       | Flag changes notify light observers during the scene update; `SceneRenderer` and `SetPropertiesCommand` sync observers after each update. Native tests cover visibility, inherited (ancestor) visibility and role edits reaching the resolved lights and the captured-sky inputs after population.                                                                                               |
| 2 Rendering section  | Node descriptors and Interop `NodePropertyApplier` (`ComponentId::kNode`, local values); authored flags projected on every node create/sync. The section follows the component sections, appears only for nodes with geometry or a light, and shows the cast/receive flags only for geometry. A locally Shown child under a Hidden parent renders.                                               |
| 3 Grading shaders    | `Tonemap.hlsl` grades the foreground in the fixed order (saturation, linear contrast about 0.18, tone curve, content-ellipse vignette, display gamma); `ColorGrading_test` covers neutral, golden, None, background and coverage cases.                                                                                                                                                          |
| 4 Framing            | Orthographic and perspective Auto/Fixed framing per target, Fixed bars composed after post-processing (outside metering and grading), cooked `aspect_mode`, scene version 11. Editor camera inspectors and live attach. Viewports look through, pilot (with undoable pose commits) and align authored cameras from the viewport menu, Scene Explorer and Ctrl+Shift+F. Orthographic ground grid. |
| 5 Point/spot editors | Descriptors, Interop `LocalLightPropertyApplier`, inspectors (cones in degrees), history/save/cook/live.                                                                                                                                                                                                                                                                                         |

### M08.V0 — Viewport robustness prerequisites

Interop view and compositing corrections that M08.V1 builds on. Each lands
as its own buildable commit; no user-visible behaviour changes except R7.

| Item                   | Change and rationale                                                                                                                                                                                                                            |
| ---------------------- | ----------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| R1 Frame logging       | Per-frame, per-view publication and composition logs move to debug verbosity: they scale with pane count and flood the log.                                                                                                                     |
| R2 Composite targets   | Surface composite framebuffers carry no depth attachment; composition writes colour only, and a depth texture per backbuffer per surface is wasted GPU memory.                                                                                  |
| R3 Dead paths          | Remove the unused `EditorCompositor` copy path and the `EditorView` renderer registration/render-graph members; composition goes only through `Renderer::RegisterRuntimeComposition`.                                                           |
| R4 Target identity     | A view names its compositing target by surface key, not a raw `Surface*`, so a view outliving its surface can never present into a new surface at a reused address.                                                                             |
| R5 Resize flush        | One GPU queue flush per frame covers every surface resized that frame, instead of one flush per surface.                                                                                                                                        |
| R6 Editor camera scene | Editor navigation cameras live in an editor-owned camera scene, not the authored scene: their state survives scene replacement, authored traversals and observations see only authored nodes, and navigation stops mutating the authored scene. |
| R7 Clear colour        | Every pane uses one neutral clear colour; the per-pane diagnostic palette is removed.                                                                                                                                                           |

Checks: a native test renders and frames through a camera-scene camera,
including a scene replacement that keeps the editor camera pose; existing
Interop and viewport lifetime tests pass.

### M08.V1 — Multi-viewport layouts

Multi-viewport support returns to scope (decision `DB-006` supersedes `DB-002`).
The native engine already composes several views per frame, each presenting to
its own surface or to a destination rectangle with a z-order
(picture-in-picture), and the editor already offers one- to four-pane
`SceneViewLayout` arrangements that mostly work. M08.V1 qualifies them and
closes the gaps; the contract is the
[viewport LLD](../lld/viewport-and-tools.md#multi-viewport-layouts).

1. **Every layout is supported.** Each pane of every `SceneViewLayout` holds
   one surface lease and one engine view. A layout change creates and releases
   only the panes that change, keeps the surviving panes' camera state, and
   leaks no view or lease. Presentation follows each pane through splits,
   resizes, dock moves, document switches and close/reopen.
2. **Panes are independent.** Each pane has its own editor camera, view
   preset, orthographic size, camera control mode and viewed scene camera. A
   camera piloted in one pane moves live in every other pane that shows it or
   looks through it; one pane at a time pilots a given camera.
3. **Focus and routing.** Each document has one focused pane. It receives
   keyboard shortcuts, frame commands, Scene Explorer camera commands and Align
   to View. Pointer input (navigation, picking, gizmos) goes to the pane under
   the pointer and focuses it. Selection feedback, gizmos, icons and workspace
   Hide apply in every editing pane.
4. **Camera preview inset.** Selecting a camera node shows a
   picture-in-picture inset in the focused pane, rendered through that camera
   with its framing and bars. The inset hides when the camera is deselected or
   when the pane already looks through it. It is editor-only presentation,
   composed through the engine's destination viewport and z-order, and is
   excluded from the host view's metering.

Known defects closed by these items: a recreated view (dock move, document
switch, maximize) loses its editor camera and preset; a preset requested before
the native camera exists is dropped; maximize/restore discards the other panes
and leaks their view models, as does any layout shrink; Align to View
(`Ctrl+Shift+F`) reaches whichever pane WinUI finds first; two panes can pilot
one camera; the wheel does not focus its pane. Panes capture their editor
camera state before a view is released and recreate the view with it.
Maximize is presentation only: the layout and pane indexes are unchanged and
the hidden panes release their views but keep their state.

Owners: WorldEditor `SceneEditorViewModel`, `ViewportViewModel` and the
`Viewport` control; Runtime surface leases and view lifetimes; Interop
`EditorModule` views and `EditorCompositor`; Vortex `CompositionView`.

Checks: open, switch and close every layout, returning the native view and
lease counts to their baseline with no debug-layer or resource-state errors;
resize and dock moves present to the correct surfaces; per-pane camera
independence; a piloted camera observed from a second pane; shortcuts and
Scene Explorer commands reach the focused pane; inset show/hide rules.

### M08.V2 — Viewport state persistence

Depends on M08.V1. A scene reopens with its layout, focused pane and every
pane's camera as the user left them. Viewport state is user-local workspace
state, never authoring data: it does not dirty the scene, enter history or
reach cooked output. The storage contract is
[settings architecture §5.2](../lld/settings-architecture.md#52-viewport-state-storage-and-lifetime).

1. **What is kept.** Per scene: the layout and focused pane. Per pane, by
   layout index: editor camera position, rotation and orbit focus point, view
   preset, orthographic size, camera control mode, and the viewed scene
   camera's node ID.
2. **Restore.** State applies when the scene's panes are created, before their
   first presented frame. A pane that was piloting reopens looking through its
   camera, not piloting. A viewed camera that no longer exists falls back to
   the editor camera with an informational log entry. Panes with no stored
   state use the defaults; stored panes beyond the layout are ignored.
3. **Write.** Shortly after navigation stops, on any layout, preset, mode or
   camera-assignment change, and when the document closes. Writes are
   serialized per project, and a stale restore never overwrites newer state.
4. **Native.** View creation accepts the editor camera state (position,
   rotation, focus point and orthographic size), the view preset and the
   viewed scene camera, so a restored pane is correct on its first presented
   frame; a view query reports the editor camera state back. A separate set
   command would land after the default camera had presented.

Owners: WorldEditor viewport-state service with `IEditorSettingsManager`;
`ViewportViewModel`; Runtime and Interop view creation and camera query.

Checks: reopen restores each layout and pane; no scene dirty state or history
entry from navigation or restore; deleted-camera fallback; project and scene
isolation; an unreadable or unsupported payload is discarded with a warning.

### M08.3 — Viewport HUD, view modes and scene toolbar

Today each pane shows a hamburger, a camera button and a shading button whose
Wireframe/Shaded/Rendered choice has no effect; Show FPS and Show Stats toggle
nothing visible. M08.3 replaces that with one quiet, icon-led row and makes
every control in it real.

1. **HUD row.** Left to right: View camera ▾, View mode ▾, Show ▾, spacer, the
   transform group (filled by M08.5, focused pane only), Layout ▾ and
   Maximize. Controls are 32 px icon + short label on a translucent strip over
   the render, with tooltips naming shortcuts. As a pane narrows, labels drop
   first and then lower-priority controls move into a Viewport settings flyout;
   a quarter pane at 1280×720 shows no clipped control.
2. **View camera flyout.** Perspective modes as described rows (Turntable:
   orbit with a level horizon; Trackball: orbit freely, including roll; Fly:
   move through the scene), then a 3×2 grid of orthographic directions. Below:
   fly speed in m/s with a slider and the input hint; vertical FOV
   (perspective only); a Clipping disclosure summarizing the range, with
   validated near/far and a lens reset that leaves mode, speed and pose
   unchanged; and the existing scene-camera look-through, Pilot and Align to
   View entries. The button label names the mode or orthographic direction.
3. **View modes.** Lit, Unlit (base colour), Wireframe and Lit + wireframe; a
   Lighting group (direct only, indirect only); and a Buffer visualization group
   (world normals, roughness, metalness, linear depth, directional shadow mask).
   Each maps to the existing per-view Vortex `RenderMode`/`ShaderDebugMode`,
   applies only to its pane and persists with the pane state. The inert shading
   enum and its Wireframe default are removed.
4. **Show flyout.** Grid (the existing ground-grid pass, in every projection),
   Statistics (a corner readout of FPS and frame time from engine frame
   statistics, and the scene's node count) and the camera preview inset. Icons
   and Selection outline join in M08.4/M08.6. The former Show FPS, Show Stats,
   Stat1–3 and Show Toolbar toggles are removed.
5. **Layout picker.** A flyout of pictograms for the 14 layouts in their
   groups (One Pane, Four Quadrants; Two, Three and Four Panes with Main
   Left/Right/Top/Bottom), current layout highlighted. It replaces the
   hamburger menu. Maximize/restore keeps its M08.V1 semantics.
6. **Gesture hint.** The focused pane shows a quiet bottom-left chip with its
   mode and next gestures (for example "Alt+drag orbit · wheel zoom · RMB+WASD
   fly"); it hides while navigating and after first use per session.
7. **Scene document toolbar.** Save, Cook scene, Add ▾ (Empty node, the ten
   primitives, the three light types and both camera types, created in front
   of the focused pane's camera, selected and undoable), Browse to asset
   (Ctrl+B, reveals the selected nodes' geometry/material assets in the Content
   Browser) and Environment (selects the scene root's environment). Existing
   commands only; no second document lifecycle.

Native/Interop: per-view render and debug mode commands, a per-view grid flag
for perspective views, and a frame-statistics query.

Checks: each view mode changes only its pane, survives reopen and never dirties
the scene; HUD layout at one, two and four panes at 100% and 150% scaling;
flyouts are keyboard reachable and close with Escape; Add, Save and Cook from
the toolbar match their existing commands; Browse to asset selects the right
assets across mounts.

### M08.4 — Picking, selection outline and framing

Today selection happens only in the Scene Explorer. M08.4 makes the viewport
the primary place to select.

1. **Pick pass.** On request, Vortex renders node IDs and depth for a pick
   rectangle of one view (visible, non-hidden geometry; M08.6 adds icons) and
   reads them back asynchronously. A result carries the hit nodes, nearest
   depth, world position and geometry slot. Requests carry the view and its
   generation; results for a recreated view, replaced scene or closed document
   are dropped. An idle viewport pays nothing.
2. **Gestures.** Click selects the nearest hit node, Ctrl toggles, Shift adds
   and an empty click clears. Left-drag from empty space draws a marquee that
   selects every node with visible pixels inside it, with the same modifiers.
   Alt-navigation is unchanged, and gizmo hits (M08.5) take precedence.
   Workspace-hidden nodes are not pickable; locked nodes are selectable but not
   manipulable. Picking has no category filter: the Scene Explorer's Mesh,
   Light and Camera buttons filter the tree only.
3. **Selection outline.** Selected nodes get a crisp screen-space outline in
   the editor overlay stage, in every editing pane and view mode: accent
   colour, brighter for the active node, dimmed where occluded.
4. **Framing.** F frames the selection and Shift+F frames all, with a 10%
   margin and a short eased transition. Orthographic views adjust their size.
   Nodes without geometry use a default extent; an empty scene frames the
   origin. Double-clicking a Scene Explorer row frames that node in the
   focused pane.
5. **Shared selection.** Viewport, Scene Explorer and Inspector stay in sync
   through the existing selection service; a viewport pick reveals and scrolls
   to its Explorer row.

Checks: picking in every projection, view mode and pane of a split layout;
child meshes select their node; marquee with modifiers; stale results after
layout, document or scene changes; outline with multiple selection;
framing a transformed hierarchy, a light, a camera and an empty scene, with no
authored change.

### M08.5 — Transform gizmos and snapping

1. **Gizmos.** Native translate (axis arrows, XY/XZ/YZ plane handles and a
   view-plane centre), rotate (axis rings and a view ring) and scale (axis
   handles, plane handles and a uniform centre). They keep a constant screen
   size, use X red / Y green / Z blue, highlight on hover, have hit targets
   larger than their visuals, and dim rather than hide occluded parts.
   Oxygen's Z-up world makes XY the ground plane.
2. **Drag feedback.** The active axis draws a guideline through the pivot and
   other handles hide. Rotation follows the mockup's rotation-feedback
   contract: only the active ring, start and moving spokes, and a translucent
   signed, multi-turn swept sector in the ring plane (never the shortest arc).
   A chip near the pointer shows the applied value ("X 1.250 m", "Z 45.0°",
   "Y 1.200×") and "Esc cancel".
3. **Space and pivot.** World/Local toggle; the pivot is the active node's
   origin; a multi-selection receives one common world delta around that
   pivot. Scale always acts along each node's own axes, spreading positions
   from the pivot, because world-axis scale of a rotated node needs shear.
   Targets under transformed parents convert through their parents; a
   result the authored model cannot represent (shear) is rejected without a
   partial edit.
4. **Tools and keys.** A left tool rail in the focused pane (Select Q, Move W,
   Rotate E, Scale R, then Frame F); Space cycles tools and Move is the
   default. With viewport focus: Delete, Ctrl+D duplicate, Ctrl+Z/Ctrl+Y.
   Alt-drag on a gizmo previews on the originals and, on release, duplicates
   the selection at the dragged transforms as one undo entry; a cancelled
   Alt-drag leaves nothing. Locked nodes show no gizmo.
5. **Snapping.** The HUD transform group holds World/Local, the snap toggle
   and the translation, rotation and scale increments, collapsing into the
   settings flyout when narrow. Each increment offers presets plus a custom
   value. Translation snaps the pivot to the world grid in World space and
   the applied offset in Local space; rotation and scale snap the applied
   delta. Ctrl inverts snapping for the current drag. Toggle state,
   increments and space are per-user editor settings that persist across
   sessions and projects. The defaults are in [section 5](#5-interaction-defaults).
6. **Transactions.** A drag previews live through the property edit session,
   with Inspector values updating; release commits one undo entry for every
   target; Escape or right-click restores the starting transforms exactly.

Checks: every tool, space and handle on single and multiple selection,
including rotated and non-uniformly scaled parents; snapping and Ctrl
inversion; exact cancel; one undo entry per drag; Save, reopen and cook agree
with the viewport; gizmos usable in quarter panes and orthographic views.

### M08.6 — Scene helpers and orientation

1. **Icons.** Screen-sized billboards for directional, point and spot lights
   and for cameras, tinted by light colour, pickable through M08.4 and
   toggled from the Show flyout.
2. **Selected helpers.** A camera frustum including its Fixed-aspect frame; a
   directional light's direction arrow; a point light's range sphere; a spot
   light's inner and outer cones. Range and outer/inner cone handles are
   draggable and edit the light through the same transaction path as gizmos.
3. **Orientation triad.** A bottom-left X/Y/Z triad in axis colours, drawn
   as small letters without filled discs, following the view camera. Clicking
   an axis selects the matching orthographic view; hover and keyboard focus
   are visible.

Checks: icons and helpers in every projection and view mode; picking a light
or camera by its icon; handle drags with exact cancel and one undo entry;
triad clicks select the correct view; no helper reaches Save or Cook.

### M08.7 — Content Browser redesign and working commands

M08.7 rebuilds the presentation around the existing query, navigation and
mount semantics. The docked browser keeps its place; an expanded mode for
library work waits for dock improvements.

1. **Layout.** A navigation row (Back, Forward, Up, Refresh, breadcrumb,
   search, Filter ▾ with an active-count badge, Tiles/List/Details switch,
   details-pane toggle) and, when filtered, a row of removable filter chips
   with Clear all. Left: sources, with the Content subtree and each mount
   (Cooked, Imported, local folders) as expandable subtrees carrying
   read-only badges, and Mounts and Content priority at the bottom. Centre:
   results. Right (optional): a docked details pane replacing the tooltip,
   with preview, name, type, status, logical path, file, size, modified time,
   identity, Locate and Copy path; multi-selection shows type and status
   counts and the selected list. Footer: "N of M assets · K selected" and
   whether the location is an authoring or a read-only source.
2. **Views and selection.** Tiles (preview, name, type, status dot and
   label), List and Details (sortable name/type/status/location/size/modified
   columns); tile-size slider in Tiles; Sort by any of those fields, ascending
   or descending. Ctrl/Shift multi-selection in every view, kept across view
   switches and re-sorts. Enter and double-click open. Previews are the
   material base colour or the type glyph; rendered previews are M08.10.
3. **Commands.** New ▾ (Folder, Scene, Material); Import ▾ (Source model,
   Texture or image, Reimport, Show import source); Cook ▾ grouped as build
   scope (Cook selected, current folder, project), published output (Inspect,
   Validate) and the cooking service (Pause automatic cooking, Show cooking
   jobs). The asset commands are toolbar icons rather than a hand-made ⋯
   menu: Rename, Cut, Copy, Paste, Duplicate and Delete in Scene Explorer's
   order, then Copy path and Show in File Explorer. The DroidNet toolbar's
   overflow priorities decide what folds into its own ⋯ when the pane is
   narrow: path commands first, then asset commands, then Cook; New and
   Import stay longest. Open stays on Enter, double-click and the item
   context menu, which mirrors the toolbar. Unavailable entries stay visible,
   disabled, and explain why.
4. **Commands that need no references.** New Folder (named in place in the
   sources tree), Copy path and Show in File Explorer work in this slice; a
   folder can be renamed only while it holds no files. Rename, Cut, Copy,
   Paste, Duplicate and Delete are present and disabled, explaining that the
   scenes and materials that use an asset must be updated with it, until the
   M08.8 transaction lands; no button is ever inert.
5. **Empty states.** An empty folder and a no-match result each give one next
   action (Clear search and filters).

Checks: view-model tests for query, sort, multi-selection and each command's
availability; New Folder through the real workflow; mount browsing remains
read-only.

### M08.8 — Asset relocation and references

Assets are identified by their virtual path (`asset:///<Mount>/<Path>`), and
native asset keys derive from that path. This is kept: it is the deliberate
engine identity that M08.F1 just finalized, and it is the Unreal model, where
renames rewrite referrers. Moving to GUID identity would be another format
milestone across Data, Cooker, Content and every editor serializer, and it
would still need a reverse index for delete warnings and Find references.
Relocation is therefore a reference-aware project transaction; the contract
is [Content Browser LLD §9.5](../lld/content-browser-asset-identity.md#95-asset-relocation).

1. **Reference index.** Every authored outgoing reference, read structurally
   per file kind rather than by text search: scene component and material-slot
   geometry/material URIs, the exposure metering mask and `ExtraAssets`;
   material texture paths; geometry descriptor material, buffer and skeleton
   paths; texture descriptor sources and identity; import sidecar bundle and
   output paths. Identities compare without the scheme and the `.json`
   authoring suffix, so `asset:///Content/M/Red.omat.json` and
   `/Content/M/Red.omat` are one asset; each rewrite keeps its field's form.
   The index is built on demand from saved files, cached until a catalog
   change, and always rebuilt inside a relocation. The details pane and Find
   references show an asset's referrers.
2. **Relocation plan.** Rename, move by drag in the source tree, Cut/Paste
   and folder rename/move produce one plan: source moves (each asset with its
   companion files: a texture's image, a model's sidecar), a folder prefix
   mapping, and the rewritten bytes of every referrer, including a moved
   descriptor's own `virtual_path` and file-relative sources. The plan rejects
   name collisions, invalid names, read-only or derived targets, moves out of
   the authoring mounts, moves into the moved folder itself, renaming or moving
   a mount root or the importer's fixed type folders (`Materials`, `Geometry`,
   `Scenes`), moving a model file out of its bundle folder, and cooked-only
   imported outputs. Rename and move ask no confirmation: updating referrers is
   the operation, and the result reports what moved and how many files were
   updated, with Undo.
3. **Imported model outputs.** A model import owns one output group in its
   mount: `Materials/<group>`, `Geometry/<group>` and `Scenes/<group>`, named
   by the sidecar's `OutputDirectory` (which may be nested, such as
   `Vehicles/Car`). Import creates no folders there, so the group is renamed
   or moved with **Rename output group…** on the model source or any of its
   outputs. A real folder inside a type folder that is, or contains, an output
   group relocates it in all three type folders together. Either way the plan
   applies one prefix mapping under each type folder, rewrites
   `OutputDirectory` in every affected sidecar, checks collisions in all three
   (including other imports' groups), and the result reports the moved group
   folders. Moving a group to another mount is rejected. Renaming or moving
   the bundle folder rewrites the sidecar's `BundleRoot`; renaming the model
   file renames its sidecar and rewrites `PrimaryRelativePath` and `Files`.
4. **Scenes.** Editor scenes are `Content/Scenes/<Name>.oscene.json` and are
   identified by name, so a scene can be renamed (through the existing scene
   rename, which also fixes `ExtraAssets` and the open document) and
   duplicated, but not moved. Deleting the open scene is rejected.
5. **Transaction.** The plan runs under the content cook coordinator's
   project writer, so it never overlaps a cook or import. If any open
   document has unsaved changes, the operation is rejected and names the
   documents to save. Referrer bytes are prepared first; a journal under
   `.build/relocation/<operation>/` records every move and edit with its
   before and after hashes; files move and referrers are written through the
   atomic file store against their baselines; any failure restores every file,
   and project activation recovers an interrupted journal. Every step is
   logged. The operation result offers Undo, which plans and runs the reverse
   relocation; it does not enter scene history.
6. **Open documents.** No document is reloaded or closed by a rename or move.
   Before the writer is released, each open document that the relocation
   rewrote or moved updates itself in memory: a scene re-points the geometry
   and material-slot references, slot targets, environment mask and
   `ExtraAssets` of the affected nodes only; a material re-points its texture
   paths, and a moved material document re-points its own path and keeps its
   history. Neither change enters undo history or marks the document dirty;
   the owner records the version the transaction wrote as its saved state, so
   cooking sees a consistent document. After the follow-up cook publishes,
   the runtime refreshes only the affected scene nodes and the environment.
   A deleted asset's open document closes.
7. **Undo history.** History is never rewritten or cleared. The session keeps
   a combined in-memory table of every committed relocation's old-to-new
   identities (A to B then B to C resolves A to C; the relocation's Undo maps
   back). When undo, redo or paste restores an asset reference, it passes
   through the table, so older edits restore the asset's current path. An
   edit that restores a deleted asset completes as a missing reference with
   the existing missing-reference diagnostic and a visible warning; it never
   blocks older undo steps. The table resets when the project closes.
8. **Cooking.** A prior cooked product whose authored source no longer exists
   is retired for every asset kind, not only scenes: its root is rebuilt from
   the remaining owners, so the old native key leaves the index. An imported
   product whose outputs fall outside its sidecar's current group is rebuilt
   into the new group; neither case counts as a forbidden identity change or a
   namespace overlap with the retired owner.
9. **Copy, Duplicate and Delete.** Copy/Paste and Duplicate create new paths
   with a unique name (`Name (2)`), give copied descriptors their new identity
   and rewrite no referrers; model sources and cooked-only outputs cannot be
   copied (import again instead). Delete shows the referrers, requires
   confirmation when any exist, moves files and companions to the Recycle Bin
   (an image another texture still uses stays), and leaves referrers with the
   existing missing-reference diagnostics. The cooker reports a deleted
   texture as a warning and cooks the material without it (a scene without
   its metering mask), so one deleted texture never stops the project cook.

Implementation, in build order:

| Step   | Projects                    | Delivers                                                                 |
| ------ | --------------------------- | ------------------------------------------------------------------------ |
| M08.8a | ContentPipeline             | Reference readers/rewriters per kind, index, path mapping                |
| M08.8b | ContentPipeline             | Planner, rejection rules, journaled transaction, recovery, coordinator   |
| M08.8c | ContentPipeline             | Retirement of moved/deleted products and relocated import groups         |
| M08.8d | ContentBrowser              | Commands, tree rename/drag, confirmations, Undo, referrers, Recycle Bin  |
| M08.8e | WorldEditor, MaterialEditor | Dirty-document guard, document reload/reopen, scene rename and duplicate |

Checks: unit tests for each reader/rewriter (form preservation, nested slots,
targeted overrides, texture `source` and `virtual_path`, sidecar fields), the
planner's rejection rules and group expansion, and the transaction's rollback
from an injected failure at each step and recovery from an interrupted
journal; cooking tests that a renamed material and a renamed import group
retire their old keys and keep other assets; Content Browser command
availability and tooltips; user review of rename and move of a material,
texture, geometry descriptor, scene, source model with its sidecar, a
populated folder and an import group with referrers in saved and open
documents, the dirty-document rejection, Undo, and delete with and without
referrers.

### M08.9 — Viewport performance and per-frame editor work

A code review of the multi-pane editor against the native MultiView example
found the gap in how the editor drives the shared engine, not in Vortex
shading or managed code: both run one `AsyncEngine` frame loop on the engine
thread, and the managed layer does no per-frame work. In priority order:

1. **One frame clock, owned by the user.** Each composition surface presents
   with a fixed `Present(1, 0)` (`CompositionSwapChain::Present`), ignoring
   the vsync setting that the windowed swap chain honours, while the engine
   also paces to a 60 FPS deadline that only the editor imposes. The two
   clocks beat against each other, and every pane adds a vsync-interval
   present that can block the engine thread; the native example presents
   once. Composition surfaces follow the engine's vsync setting. With vsync
   on, each surface's swap chain has a frame-latency waitable object at
   maximum latency 1 and the engine waits once for all of them at frame
   start, so presents never block and the display is the clock. With vsync
   off, surfaces present at interval 0 (DWM composition cannot tear) and the
   frame-rate cap is the clock, or nothing when the cap is off. A cap below
   the refresh rate with vsync on is the user's choice, and both apply.
2. **Engine settings in the Settings flyout.** The scene editor's Settings
   flyout holds the settings that apply to the whole embedded engine,
   persisted with the project's preview preferences: VSync, on by default; a
   frame-rate cap, off by default and 1–240 FPS (the engine maximum) when on;
   idle panes, rendered on change by default or always rendered; and the
   existing native log verbosity. The editor imposes no rate of its own:
   `EngineConstants.DefaultTargetFps`, the preferences' `[1, max]` clamp and
   the slider's 60 maximum go, and preferences saved earlier keep their log
   verbosity but not their 60 FPS cap. Interop exposes vsync on
   `EngineRunner` through the engine's `gfx.vsync` setting. Renderer tuning
   variables (fog, occlusion, atmosphere) stay console diagnostics.
3. **Panes render on demand.** `EditorModule::OnPublishViews` publishes every
   visible pane every frame, so four panes render four full deferred
   pipelines (and the camera inset at 30 % scale) while one is navigated. A
   pane renders when its camera, extent, render options or view mode change;
   on any scene, selection, helper or gizmo change; for a pending pick; and
   when an asset it may show becomes resident. It keeps rendering for a
   settle window afterwards, until auto exposure converges and for at most
   one second. An idle pane is not rendered, composed or presented, and its
   surface keeps the last image; with idle panes set to always render, every
   visible pane renders each frame as today. Vortex gains a per-frame skip
   for a published runtime view that keeps its view state, exposure history
   and last output; Interop owns the policy. Withdrawing and republishing
   instead would discard exposure history.
4. **One compositing submission.** `Renderer::OnCompositing` acquires a
   command recorder per target surface; it records every surface's
   composition into one recorder per frame. The copy into each surface stays:
   it is inherent to one swap chain per pane.
5. **Lean per-frame editor work.** `ProcessResizeRequests` snapshots every
   surface each frame to find resizes; the registry flags a pending resize
   instead. The selection outline is rebuilt when selection changes, not per
   frame; a pane reuses its overlay rather than allocating one per frame; and
   a frame takes one view snapshot instead of copying the view list under the
   manager's mutex in each phase. Publication policy, overlay building and
   composition become separate parts of `EditorModule`, so the on-demand
   rules live in one place.
6. **Input path.** `RuntimeCommandDispatcher` calls the native input
   transport while holding its run gate, so pointer moves contend with scene
   and asset commands; the gate validates the target and the native call
   runs outside it. Unheld navigation input (the wheel) allocates a debounce
   cancellation source per event; one restartable timer replaces it.

Implementation, in build order:

| Step   | Projects                    | Delivers                                                                        |
| ------ | --------------------------- | ------------------------------------------------------------------------------- |
| M08.9a | Oxygen.Engine (D3D12)       | Composition presents follow vsync; latency waitables waited once per frame      |
| M08.9b | Oxygen.Engine (Vortex)      | Per-frame skip of a published runtime view; one compositing recorder            |
| M08.9c | Editor.Interop              | Vsync on `EngineRunner`; on-demand panes, settle window, per-frame cleanups     |
| M08.9d | Editor.Runtime, WorldEditor | Engine settings flyout and preferences, no 60 FPS default, input path, debounce |

Checks: native tests that a composition surface's sync interval follows the
vsync setting, that a skipped view keeps its exposure state and last output,
and that every surface composes from one recorder; Interop tests that an
unchanged pane is not republished, that each trigger republishes it, that the
settle window ends and that always-render publishes every pane; managed tests
that the preferences round-trip vsync, cap off and on, and idle panes, that
cap off reaches the engine as 0 and that earlier preferences drop their cap;
dispatcher tests that the native input call runs outside the gate; user review
of one- to four-pane layouts in the running editor with vsync on and off and
the cap off and on, including navigation, an edit, selection and gizmo drag in
one pane updating the others, picking in an idle pane, resize, and a reimport
refreshing idle panes.

### M08.10 — Asset thumbnails and previews

1. **Native preview renderer.** Interop renders through Vortex into an
   offscreen target with a private preview scene: neutral studio lighting and
   sky, a material on a sphere, a geometry asset auto-framed at a three-quarter
   view, at 256² scaled for DPI. Production readback returns BGRA8 pixels. At
   most one preview renders per frame alongside the viewports, cancellable,
   and nothing runs while idle.
2. **Other kinds.** Textures render on a quad from cooked data; unimported
   images decode from source. Scene thumbnails are a downscaled capture of the
   focused pane when the scene is saved, with no extra render.
3. **Thumbnail service.** Asynchronous and prioritized by visible items; keyed
   by asset identity and content revision; stored in the project's local cache
   folder, ignored by cooking and version control; invalidated by save,
   reimport and cook. Pending or failed items show the type glyph and never
   block browsing.
4. **Consumers.** Content Browser tiles, rows and details pane; the
   Inspector's geometry and material pickers, replacing flat colour swatches;
   and the Material Editor preview, re-rendered after edits, replacing the CPU
   swatch ball.

Checks: thumbnails for each kind appear and update after an edit, reimport or
cook; the cache survives restart and regenerates when deleted; viewports stay
interactive while previews render; device loss and project close cancel
pending work cleanly.

### M08.11 — Drag-and-drop scene assembly and closeout

1. **Geometry into the scene.** Dragging a geometry asset over the viewport
   shows a placement preview at the surface under the cursor (pick depth,
   falling back to the ground plane, with snapping) and drops a new node in
   one undo entry; Escape cancels. Dropping on a Scene Explorer row creates
   the node under, before or after that row.
2. **Materials onto objects.** Dragging a material highlights the object and
   slot under the cursor and assigns that slot on drop, in one undo entry.
   Dropping on an Inspector material slot assigns that slot.
3. **Locate.** Inspector asset slots gain Locate, which reveals the asset in
   the Content Browser.
4. **Closeout walkthrough.** With the user, in the normal Release editor:
   create a project, import a model, author materials, assemble a scene by
   drag-and-drop, arrange it with gizmos and snapping, light it, save, cook
   and reopen; then load the cooked project in RenderScene and compare it with
   the editor viewport.

Checks: drop placement on geometry, on empty space and in orthographic views;
slot-accurate material drops on multi-slot meshes; undo and cancel; the
walkthrough completes without manual file repair.

## 5. Interaction defaults

| Setting                      | Default                                                                                                |
| ---------------------------- | ------------------------------------------------------------------------------------------------------ |
| Snapping                     | Off; 0.25 m and 15° (Unity), 0.1 scale (Godot); presets 0.01–10 m, 1–90°, 0.01–1; per-user, persistent |
| Transform space              | World; per-user, persistent                                                                            |
| Axis colours                 | X red, Y green, Z blue in gizmos, guides, triad and vector fields                                      |
| Selection outline            | Accent colour; active node brighter; occluded parts dimmed                                             |
| Frame margin                 | 10% of the framed bounds                                                                               |
| Overlays on a new pane       | Grid, icons and outline on; statistics off                                                             |
| VSync                        | On; engine-wide, persisted with the project's preview preferences                                      |
| Frame-rate cap               | Off; 1–240 FPS when on; engine-wide, persisted with the project's preview preferences                  |
| Idle panes                   | Render on change; always render as an option; engine-wide                                              |
| Thumbnail render size        | 256² at 100% scaling                                                                                   |
| Content Browser default view | Tiles, sorted by name, details pane closed                                                             |

## 6. Build and verification

Native work uses the existing `projects/Oxygen.Engine/out/build-ninja` tree
with normal Ninja parallelism and CTest; install the SDK, then rebuild Interop
and the editor with 64-bit MSBuild. Batch engine and editor rebuilds per
review pass rather than per edit. Run only the tests for the code being
changed, one GPU test process at a time. Viewport behavior that depends on
real input is reviewed by the user in the running editor at each slice pause;
UI tests never drive the system mouse or need window focus.

## 7. Exit checklist

- [x] Canonical formats, migration and native producer/loader mappings
      (M08.1, M08.F1).
- [x] Authoring surface and rendering behavior (M08.2).
- [x] Every scene layout and the camera preview inset pass M08.V1, and
      viewport state survives reopening per M08.V2.
- [x] HUD, view modes and scene toolbar (M08.3).
- [x] Picking, outline and framing (M08.4).
- [x] Gizmos, snapping and viewport editing shortcuts (M08.5).
- [x] Scene helpers, editable light handles and orientation triad (M08.6).
- [x] Content Browser layout, views and working commands (M08.7).
- [x] Rename, move, cut/copy/paste and delete with reference updates (M08.8).
- [ ] Viewport frame clock, on-demand pane rendering and lean per-frame
      editor work (M08.9).
- [ ] Rendered thumbnails in browser, pickers and Material Editor (M08.10).
- [ ] Drag-and-drop assembly and the closeout walkthrough, including the
      RenderScene load of the editor-cooked project (M08.11).
- [ ] Owning LLDs and API prose match the implemented contracts; post-V0.1
      annotations mark every deferred boundary.
