# ED-M08 — Runtime parity and standalone qualification

Status: **in progress — M08.1 and M08.F1 validated; M08.2 native rendered behavior is next**

Current: **M08.2 native rendering and view behavior.** M08.1 and [M08.F1 descriptor-local references](ED-M08.F1-descriptor-local-references.md) are validated. F1 closes on 2026-10-06 with user reference-authoring/live mount-tree checks, the user's Main/Lantern scene packaged and rendered from its PAK alone, and five existing focused native origin/binding/cache cases passing. [Closure evidence and approved scope](ED-M08.F1-descriptor-local-references.md#closure-verification) retain failing/unrun checks without claiming passes; final managed/UI qualification and load-cost measurement are non-blocking by user decision. M08.2's native data foundation (Local/Inherit flags, cast/receive extraction, receiver and contact-shadow GPU paths, the independent directional array with Primary/Secondary slots and per-light CSM shadows, and captured-sky IBL) is already landed; M08.2 now finishes the remaining authoring gaps — live invalidation on flag/role edits, the node "Rendering" section (Scene Visibility / Cast / Receive Shadows), color-grading shader consumers, camera framing bars with orthographic camera editor UI and sync command, and point/spot light editors — while the editor representation mask is deferred to M08.4. M08.4 is re-scoped to the remaining authoring surface: the representation mask and polish. M08.3 and M08.5–M08.8 remain unchanged.
See [owners](#2-implementation-document-map), [remaining increments](#m081-remaining-increments)
and [exit checklist](#7-exit-checklist). Captured-sky IBL is delivered by
[VX-IBL-01](../../../projects/Oxygen.Engine/design/vortex/milestones/VX-IBL-01/README.md);
its rendering, precision and publication contracts remain unchanged.

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
| [Standalone qualification](../lld/standalone-runtime-validation.md)                                                      | Development topology, protocol, admission, observations, capture, comparison and cleanup                |
| [Settings architecture](../lld/settings-architecture.md)                                                                 | Authored settings, local workspace state, runtime-session and startup preferences                       |
| [Documents and commands](../lld/documents-and-commands.md)                                                               | Save/close lifecycle, command outcomes and document ownership                                           |
| [Cooking workflows](../lld/content-cooking-workflows.md)                                                                 | Actual Save/Cook/reimport/recovery actions used by qualification                                        |
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

| Target/location                                                      | Responsibility                                                          |
| -------------------------------------------------------------------- | ----------------------------------------------------------------------- |
| `projects/Oxygen.Engine/tools/validation/Schemas`                    | Versioned qualification schemas and rejection corpus                    |
| `Oxygen.Tools.EditorValidation.Native`                               | Exact native request execution and standalone process                   |
| `Oxygen.Tools.EditorValidation.Capture`                              | Opt-in observations, checkpoints, exposure telemetry and capture bridge |
| `tests/EditorValidation/Oxygen.Editor.Validation.csproj`             | Preparation, fixture/expectations, process ownership and comparisons    |
| Existing WorldEditor UI test host with `OxygenEditorValidation=true` | Real editor workflows and saved-revision capture adapter                |

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

#### M08.2 status (2026-10-07)

| Item                 | State                   | Delivered / remaining                                                                                                                                                                                                                                                                                                                                                                                                           |
| -------------------- | ----------------------- | ------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| 1 Invalidation       | landed_needs_validation | `SceneRenderer` syncs observers after `Update(false)`; `SetPropertiesCommand` updates and syncs after every applier run. Remaining: native flag/role-edit cache test.                                                                                                                                                                                                                                                           |
| 2 Rendering section  | landed_needs_validation | Node descriptors, Interop `NodePropertyApplier` (`ComponentId::kNode`, local values), projection of authored flags on every node create/sync, inspector section after component sections. Dedicated Set{Visibility,CastShadows,ReceiveShadows} commands removed. Remaining: editor workflow check, Shown child under Hidden parent rendered.                                                                                    |
| 3 Grading shaders    | planned                 | `PostProcessConfig` carries saturation/contrast/vignette; no shader consumer yet (`Tonemap.hlsl`, `Exposure.hlsl`).                                                                                                                                                                                                                                                                                                             |
| 4 Framing            | partial                 | Orthographic Auto/Fixed per target in `OrthographicCamera::ProjectionMatrix(target)` and the view resolver; cooked `OrthographicCameraRecord.aspect_mode`; size/ratio descriptor schema; scene version 11 with all Examples descriptors migrated and recooked. Editor ortho inspector, perspective Aspect Mode selector, live attach, cooking. Remaining: Fixed content rectangle, bars after post-process, metering exclusion. |
| 5 Point/spot editors | landed_needs_validation | Descriptors, Interop `LocalLightPropertyApplier`, inspectors (cones in degrees), history/save/cook/live. Remaining: editor workflow check.                                                                                                                                                                                                                                                                                      |

Evidence: full native Debug build and Examples recook; 11 native test executables
pass (Scene cameras, SceneNode, DirectionalLightResolver, Data.All, Cooker
SceneDescriptor/Scene/Gltf/Fbx, Content.AssetLoader, SceneCameraViewResolver);
Interop builds; World 79/79, ContentPipeline descriptor 41/41, WorldEditor
Unit, Unit.UI and Integration.UI lanes pass in full on the owner's runs, with
integration tests on one shared native engine and node observation checking the
rendering flags through history and Save/reopen. Not run: Interop native tests,
editor workflow checks.

Resume order: grading shader consumers (item 3), then the Fixed framing content
rectangle, bars and metering exclusion (item 4), each with native tests; then
the native flag/role-edit cache test (item 1), status docs and editor workflow
checks for items 1, 2 and 5. Editor projects cooked at scene version 10 must be
recooked.

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
visibility/caster/receiver controls; both atmospheric sources, ordinary fill and
point/spot lights; captured diffuse/specular sky; perspective/orthographic camera
framing; exposure/grading/background.
Retain images, observations, actual profile/build identities and readiness facts.
Resolve these failures before the editor-integration slice.

Checks also include row pitch/channel order/encoding, GPU-fence completion,
cancellation/device failure, in-flight resource lifetime and history reset.
Maintained RenderScene examples provide ordinary-loading regression coverage.

### M08.4 — Editor canonical authoring and live delivery

The transform, geometry/slot, directional/point/spot-light, camera and environment
field tables are now complete by M08.2; M08.4 audits the remaining surface and
adds refinements end-to-end:

- **Workspace Hide representation mask** (pushed from M08.2 for large-scene
  optimization): the native per-view delivery so hidden geometry keeps casting
  and lighting without authored changes. Apply the workspace-Hide mask to the
  editing-main-view depth/colour submissions while retaining caster/light
  eligibility. Workspace owns persistence and Show All semantics; no authored
  dirty/history/cook mutation is introduced.
- **Remaining polish:** copy/duplicate, mixed selection, sessions, diagnostics and
  current-scene convergence across every retained field. Inspector completeness
  and usability audit: verify actual packaged controls, field coverage, label/unit
  clarity and keyboard/focus behavior.

WorldEditor/MaterialEditor own UI/commands; World/Managed.Assets own source data;
ContentPipeline owns native production; Runtime/Interop adapt engine operations.
Product UI contains no qualification entry.

Checks: actual packaged controls/commands, nonzero slot assignment/clearing and
repair, published material changes, role conflicts, flag defaults/overrides.
Re-verify M08.2 deliverables through normal editor workflows: point/spot light edits
(range, lumens, cone angles) and orthographic camera edits (size, near/far) with
live effect, Auto resize without dirtying, workspace Hide restoration/lifetime/accessibility
and hidden-caster retention, and all node Rendering flags with history/Undo/Save/reopen.

**Inspector completeness and usability gate:** audit the real scene/environment
and node/component property editors before implementation, then repeat the review
on the completed packaged UI.

- Map every required field in the owning LLD tables to its actual control,
  applicable selection/mode and non-default workflow. Identify missing, hidden,
  ineffective or incorrectly bound controls; verify edit, Undo/Redo, Save/reopen
  and live effects. Include empty/scene selection and mixed selection.
- Review grouping, discoverability, progressive disclosure, labels/units, control
  choices, alignment, keyboard/focus behavior and loading/error/repair feedback.
  Exercise narrow docks and 100%/150%/200% scaling. Remove redundant information
  and unnecessary interaction steps while preserving the V0.1 field contract.
- Fix the findings and walk through the resulting UI with the user before closing
  M08.4. Record one compact coverage/findings table and acceptance outcome in the
  existing M08 validation summary; keep temporary screenshots out of Git.

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
and requested shadows; None-role fill; point/spot lights; capture invalidation;
visibility/Hide and receiver cases; every primitive/slot/emission case; Auto/Fixed
perspective and orthographic cameras; Manual/Auto exposure; every retained tone
mapper/grade/background interaction.

M02's one-viewport resize and consolidated discovery evidence is recorded under
[its plan](ED-M02-live-viewport-stabilization.md); the joint M08 review remains.
Run normal editor and RenderScene
without development tools installed. Inspect normal Debug/Release references,
resources, initializers, exports, packages and SDK inventories for zero
qualification payloads.

Jointly review real changes, reruns, cancellation, original images and results.
Finish affected-code analyzer/IDE checks at commit preparation. Record one M08
result in IMPLEMENTATION_STATUS with exact build/fixture/publication/profile
and evidence identities.

## 5. Qualification constants

The standalone LLD owns measurement algorithms. These constants are fixed:

| Gate                                     | Required value                                                                                                                                                        |
| ---------------------------------------- | --------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| Full workload                            | Exactly 100 nodes: 98 geometry, one camera, one Primary light; exactly 1,000 logical catalog entries; ≤250,000 visible triangles                                      |
| Field cases                              | Separate bounded scenes cover multiple lights and non-default fields without changing full-workload counts                                                            |
| Base image profile                       | 1920×1080; Fixed 16:9; Manual EV9.7; ACES fitted; conventional shadows; no overlays                                                                                   |
| Time/history                             | Fixed 1/60-second scene step; seed 0; reset histories; frame 0 starts after content/uploads/camera/profile readiness                                                  |
| Checkpoints                              | Completed frame 120; Auto also at 240 and 600                                                                                                                         |
| IDs/enums/booleans/membership/rectangles | Exact through the declared identity map                                                                                                                               |
| Finite scalars/vectors                   | `abs(a-b) <= max(1e-4, 1e-4 * max(abs(a), abs(b)))`                                                                                                                   |
| Quaternion orientation                   | ≤0.01 degree; opposite signs equivalent; invalid quaternions rejected                                                                                                 |
| Images                                   | Display-encoded sRGB RGB; RMSE ≤0.01; nearest-rank P99 absolute channel error ≤0.03; only outer one-pixel border excluded                                             |
| Auto exposure                            | GPU-observed difference ≤0.05 EV at each checkpoint, with the same image thresholds                                                                                   |
| Framing                                  | Fixed 4:3 in 1920×1080: `(240,0,1440,1080)`; Fixed 16:9 in 1440×1080: `(0,135,1440,810)`; bars stay in images and outside metering                                    |
| Deadlines                                | Cancellable 120-second active preparation/capture bound and 120-second child deadline per run; cancellable admission wait; separately bounded cleanup/drain ownership |
| Feedback                                 | Development progress/cancel state within 100 ms; responsive production authoring                                                                                      |

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
      M08.1 and M08.F1 are validated; remaining M08 producer/loader mappings
      retain their later slice owners.
- [ ] Engine fixes have native tests and rendered evidence outside the editor.
- [ ] Editor authoring/history/Save/cook/live delivery and workspace Hide pass.
- [ ] M08.4 inspector field coverage and usability audit pass, with user walkthrough acceptance.
- [ ] Saved-input proof/ownership survive partial/no-op publication and contention.
- [ ] Saved-revision capture isolates edits and converges correctly after release.
- [ ] Full semantic/image/GPU-exposure comparisons pass the fixed thresholds.
- [ ] Cancel/fault/close/restart/resize preserve source, publication and ownership.
- [ ] Normal Debug/Release build/install/package contain no qualification payloads.
- [ ] M02 single-viewport evidence and joint M08 review are recorded.
      The M02 evidence is recorded (ED-M02 is validated); the joint M08 review
      remains.
- [ ] Source/API prose and post-V0.1 annotations match implemented contracts.
- [ ] Affected-code diagnostics are clean and the exact evidence set is recorded.
