# ED-M07B - Safe Content Publication And Compatibility

Status: `planned; no implementation or validation completion claimed`

## 1. Purpose

Close concrete descriptor, cook/publication, import and matched-build gaps before
standalone parity. Preserve ED-M07's completed delivery record and original
validation; this milestone owns the additional work and evidence.

## 2. PRD Traceability

`REQ-014` through `REQ-024`, `REQ-026`, `REQ-037`, `REQ-039` through `REQ-042`;
`SUCCESS-004`, `SUCCESS-006`, `SUCCESS-007`.

## 3. Required LLDs

[content-pipeline.md](../lld/content-pipeline.md) sections 16-17,
[runtime-integration.md](../lld/runtime-integration.md),
[environment-authoring.md](../lld/environment-authoring.md),
[asset-primitives.md](../lld/asset-primitives.md),
[project-services.md](../lld/project-services.md),
[project-layout-and-templates.md](../lld/project-layout-and-templates.md),
[diagnostics-operation-results.md](../lld/diagnostics-operation-results.md).

## 4. Identified Gaps

| Evidence in the committed source | Missing behavior | Task |
| --- | --- | --- |
| `SceneDescriptorGenerator.CreateEnvironment` produces only NativeSkyAtmosphereEnvironment; earlier code emits warnings for non-default exposure/tone/background. | Required PostProcess/Background values survive cooking and native loading, rather than being omitted. | 07B.3 |
| `ContentImportManifestBuilder` writes directly to GetCookedMountRoot; ContentCookScope/Result have no input revision/hash or publication transaction. | Coherent saved input, private staging, safe fixed-root replacement and provenance. | 07B.1/2 |
| Runtime mount contract is UnmountProjectCookedRoot followed by MountProjectCookedRoot. | Pause/drain, all-root rollback and interrupted-publication recovery. | 07B.2 |
| ProjectCookScopeProvider derives project/root/output facts; existing Content Browser exposes Cook Asset/Folder/Project. | UI completion must use the closed PRD scope, not invent settings/preset/batch schedulers. | 07B.5 |
| Native discovery locates installed tooling; no qualified artifact-set fingerprint is established by the existing design. | Detect mismatched editor/native/cooker/schema artifacts before unsafe calls. | 07B.4 |

## 5. Scope And Non-Scope

Implement the content-pipeline LLD's saved-input and journaled publication
transaction, required descriptor mapping, qualified static/scalar import,
reproduction, matched-build preflight, and existing-surface result feedback.
No generic project-settings panel, renderer-preset selector, dedicated recook-
stale scheduler, descriptor/manifest editor, autosave, multi-viewport support,
or standalone parity claim belongs here. Existing published paths stay fixed.

## 6. Implementation Sequence

### 07B.1 - Saved Dependency Snapshot And Single Cook Writer

Route every cook entry point, including material helpers, through one project
coordinator. Reject dirty participating documents, capture/hash saved inputs and
import settings under coordinated reads, and pass snapshot paths to native jobs.
Serialize overlapping requests and scope callbacks to project lifetime.

Pass: a later edit does not change captured bytes or clear dirty state; a queued
cook captures only after its gate; source mutation during discovery retries or
fails visibly; cancelled/closed-project work cannot publish.

### 07B.2 - Staging, Preview Pause, Publication And Recovery

Implement content-pipeline section 16 exactly: same-volume private output,
whole-root validation, preserved unrelated entries for partial cooks, durable
journal/backups, preview pause/read drain, all-affected-root replacement, remount,
metadata/catalog commit, and rollback/recovery. Runtime/output leases prevent
standalone readers or late requests from observing a partially replaced set.

Pass: inject failure/interruption at every journaled boundary, first publication,
partial asset recook, cancellation, native mount and rollback mount. Prior output
is intact/restored or recoverably retained with preview visibly unavailable.
No failed generation is reported Mounted/current. Authoring remains responsive.

### 07B.3 - Complete Native Descriptor/Load Mappings

Extend the engine-owned scene descriptor and native import/load contracts for
every editable PostProcess field and LDR BackgroundColor. Carry authored values
and enum ordinals through SceneDescriptorGenerator and native scene-system
initialization. Existing sky-atmosphere and sun/material mappings remain intact.
Do not write guessed fields or editor-owned binary records. Missing required
mapping is an error before publication, never an unsupported-field warning pass.

Pass: non-default values for every environment/post-process field survive saved
JSON -> generated descriptor -> cook -> native load observation with units and
ordinals preserved. Include all tone/exposure modes, background with atmosphere
off, and camera/light/material references. Visual equivalence is M08's gate.

### 07B.4 - Matched Artifact Preflight And Reproducible Import

Create/ship the PRD qualification manifest with exact editor/native/tool/schema
hashes. Validate it before loading interop or spawning native tools. Keep Project
Browser and safe saves available on mismatch. Implement the qualified glTF/FBX
static/scalar validation and retained import settings from pipeline section 17.

Pass: wrong/missing artifact or schema fails safely; small unit/axis/handedness
fixtures import consistently; unsupported animated/skinned/texture-bearing
qualified imports fail before publishing without destroying sources. Delete
derived data from a copied project and regenerate identical logical asset
identities and semantically equivalent descriptors/loaded values.

### 07B.5 - Existing UI Workflows And Freshness

Use Cook Selected Asset/Folder/Scene/Project to rebuild a selected stale scope.
Show busy/phase state, captured input identity, publication pause, result and
freshness. Content/result details expose copyable source/generated/cooked paths;
Inspect/Validate remain the cooked-product tools. Material swatches are labeled
approximate, with scene stale state until explicit Save/Cook publishes.

Pass: each scope cooks through the same coordinator; selecting stale content
and choosing Cook rebuilds it; no generated-file editing or new settings/preset
panel is needed. Published material changes appear on all scene uses after
resume. Failed publication has a distinct result from a successful staged cook.

## 7. Project/File Touch Points

- `ContentPipeline/src`: ContentPipelineService, ContentImportManifestBuilder,
  SceneDescriptorGenerator, tool locator/adapter, input/result contracts, new
  snapshot/journal/publication coordinator in the owning module.
- `Runtime/src/Engine`, `Interop/src/EditorModule`: runtime pause/drain/remount
  capability and typed output leases; no policy in native bridge code.
- `WorldEditor` scene commands, `MaterialEditor` cook requests, ContentBrowser
  existing command/results surfaces: consume the coordinator's outcomes.
- `Projects` project context/paths and `Managed.Assets` supported catalog/index
  adapters; neither executes editor workflow policy.
- Engine scene descriptor schemas/import builders/runtime scene systems for
  PostProcess/Background support, with engine-owned tests and validation.

## 8. Risks And Containment

Multi-root publication must have one recoverable outcome. Native read handles
must drain before fixed paths move. A saved snapshot cannot reference mutable
external inputs. Field names/units/ordinals come from engine-owned contracts.
Source/runtime changes require their owning team and validation; no engine build
is run implicitly by this documentation task.

## 9. Validation Gates

- [ ] 07B.1 input/revision/concurrency cases pass.
- [ ] 07B.2 publication, rollback, interruption, cancellation and lease cases pass.
- [ ] 07B.3 every required field survives native cook/load observation.
- [ ] 07B.4 mismatch, import conversion/rejection and clean-copy reproduction pass.
- [ ] 07B.5 all four cook scopes, stale material feedback and resumed preview pass
  through the visible editor workflow, with user validation evidence.

## 10. Status Ledger Hook

Record one ED-M07B result after these gates pass. Do not reopen ED-M07 or expand
its old validation row into proof of this transaction. ED-M08 starts only after
07A and 07B have their required evidence; no M04 audit/closure sweep is a dependency.
