# Scripting Import and Sidecar Cooking Design

## 1. Scope

This specification defines two distinct scripting pipelines:

1. `ScriptAssetImportPipeline` (standalone script asset import).
2. `ScriptingSidecarImportPipeline` (optional, repeatable scene-node binding).

This is the authoritative behavior contract for scripting cook/import.

## 2. Core Pipeline Model

Scripting is not a scene import option. It is a separate domain with two
pipelines.

### 2.1 ScriptAssetImportPipeline (Operation A)

Imports one script asset identity and emits script-domain data:

1. `ScriptAssetDesc`
2. script resource descriptors/data (source and/or bytecode)

No scene-node association is required.

### 2.2 ScriptingSidecarImportPipeline (Operation B)

Imports one scene-targeted scripting sidecar and emits node associations:

1. `ScriptingComponentRecord`
2. `ScriptSlotRecord`
3. `ScriptParamRecord`

This pipeline associates existing script assets to scene nodes.

## 3. Non-Negotiable Constraints

1. No scripting options are added to `SceneImportSettings`.
2. Scene pipeline owns scene descriptor finalization.
3. Scripting pipeline owns script-domain finalization.
4. Cross-pipeline integration is refs plus patching plus explicit dependency
   edges only.
5. `PakFormat_scripting.h` contracts are authoritative for record layout and
   linkage semantics.
6. Design must support both:
   - concurrent scene import plus sidecar import
   - standalone sidecar import against already-cooked scene
7. Scene virtual-path resolution for sidecar import must delegate to
   `content::VirtualPathResolver` semantics (canonical path validation, mount
   precedence, tombstone masking). No custom resolver policy is allowed.

## 4. Request Contracts

### 4.1 Script Asset Import Request

Script asset import uses plain `ImportRequest` with
`ImportOptions::scripting.import_kind = kScriptAsset` and targets exactly one
script asset identity.

Required identity fields:

1. target script asset key or deterministic script identity input.

Optional cook options:

1. compile on/off
2. compile mode
3. storage mode

Batching is external: multiple requests, one script asset per request.

### 4.2 Scene Scripting Sidecar Import Request

Scripting sidecar import uses plain `ImportRequest` with
`ImportOptions::scripting.import_kind = kScriptingSidecar` and targets exactly
one scene via a scene locator.

Required target reference fields:

1. `target_scene_virtual_path`
   - it must satisfy canonical virtual path constraints
   - validation and precedence semantics are delegated to
     `content::VirtualPathResolver`

Sidecar import does not accept `target_scene_key`.
Scene key is always resolved internally from `target_scene_virtual_path`.

Payload fields:

1. node binding intents
2. script refs
3. slot metadata
4. param records
5. slot identifier (`slot_id`) per node binding row; required for deterministic
   dedupe and conflict reporting

Batching is external: multiple requests, one scene target per request.

### 4.3 Dispatch Contract (Request + Context)

Dispatch is owned by an orchestration layer above both pipelines and is
deterministic.

Inputs to dispatch:

1. zero or more script-asset `ImportRequest` items
2. zero or one sidecar `ImportRequest`
3. optional inflight scene context
4. optional cooked scene context

C++ request context shape (runtime-only, orchestration-populated):

1. `ImportRequest::cooked_context_roots: std::vector<std::filesystem::path>`
2. `ImportRequest::inflight_scene_contexts:
   std::vector<ImportRequest::InflightSceneContext>`
3. `ImportRequest::InflightSceneContext` fields:
   - `scene_key`
   - `virtual_path`
   - `descriptor_relpath`
   - `descriptor_bytes`

Dispatch execution:

1. Script-only:
   - run `ScriptAssetImportPipeline` for each script request
2. Sidecar-only:
   - run `ScriptingSidecarImportPipeline`
3. Script+sidecar:
   - execute script requests first
   - sidecar script refs resolve against:
     - pre-existing cooked script assets
     - successful script imports from this dispatch batch
   - run sidecar finalize only after required refs are resolved

Execution contract:

1. `ScriptAssetImportPipeline` never requires scene resolution.
2. Scene resolution is required only by `ScriptingSidecarImportPipeline`.
3. sidecar apply is atomic per request (all-or-nothing)
4. any unresolved dependency required by sidecar blocks sidecar apply
5. script-only requests are independent of scene context
6. dispatch commit semantics are per request:
   - each script request commits independently on success
   - sidecar request commits atomically on success
7. sidecar failure does not roll back already committed successful script asset
   imports from the same dispatch batch
8. sidecar resolution never consumes failed script-import outputs

### 4.4 ScriptingSidecarImportPipeline Scene Resolution Contract

This contract applies only to `ScriptingSidecarImportPipeline`.

Goal:

1. resolve sidecar target reference to exactly one scene key
2. produce resolved `target_scene_key` plus node binding context
3. reuse existing resolver semantics, do not reimplement precedence logic

Deterministic resolution algorithm:

1. Request must provide `target_scene_virtual_path`.
2. Apply canonical path validation using the same policy as
   `content::VirtualPathResolver`.
3. Build inflight candidates:
   - exact virtual-path matches from inflight context.
4. Inflight candidate handling:
   - `0` candidates: continue with resolver path lookup.
   - `1` candidate: use its scene key.
   - `>1` candidates: reject as inflight target scene ambiguous.
5. If lookup proceeds to resolver:
   - resolve via `content::VirtualPathResolver::ResolveAssetKey`.
   - missing result: reject as target scene missing.
   - mount order is deterministic:
     - request `cooked_root` first
     - `cooked_context_roots` in listed order (later entries have higher
       precedence / last-mounted-wins)
6. Resolve scene binding context by final target scene key:
   - prefer inflight context; otherwise use cooked-scene inspection context.
   - if no binding context resolves, reject as target scene missing.
7. The sidecar pipeline must not implement its own mount precedence,
   tombstones, or virtual path canonicalization rules.
8. Resolution must be deterministic for identical inputs and context state.

## 5. Option Contracts

### 5.1 Script Asset Options

1. `compile_scripts: bool = false`
2. `compile_mode: debug | optimized = debug`
3. `script_storage: embedded | external = embedded`

Validity:

1. `compile_scripts=true && script_storage=external` is invalid.

### 5.2 Sidecar Options

Scene sidecar import does not define script compile/storage options.
It consumes resolved script asset identities and emits bindings.

### 5.3 Compile Mode Canonicalization

Compile mode must be shared, not Engine-local:

1. `src/Oxygen/Core/Meta/Scripting/ScriptCompileMode.inc`
2. `src/Oxygen/Core/Meta/Scripting/ScriptCompileMode.h`
3. macro contract: `OXSCRP_COMPILE_MODE(name, value)`

Migration contract:

1. Existing Engine-local `ScriptCompileMode` definition is moved to
   `Core/Meta/Scripting` as the single source of truth.
2. Engine and Cooker/Import must include and consume the shared Core/Meta
   definition.
3. Engine-local duplicate enum catalogs are removed.

Consumers:

1. Engine scripting compile interfaces
2. Cooker/Import scripting code

No duplicate enum catalogs are allowed.

## 6. Emission Ownership

### 6.1 Script Asset Import Emits

1. script asset descriptor bytes
2. script resource table/data payload contributions

### 6.2 Sidecar Import Emits

1. scripting component rows referencing scene node indices
2. slot table rows
3. param table rows

Association is materialized by:

1. `ScriptingComponentRecord` -> node index plus slot range
2. `ScriptSlotRecord` -> script asset key plus param range
3. `ScriptParamRecord` -> typed param values

## 7. Dependency and Patching Model

### 7.1 Patch Contract

Patch integration is one-way:

1. Scene binding context provides:
   - resolved `target_scene_key` derived from located scene descriptor
   - `NodeRef -> SceneNodeIndex`
   - provisional scripting component rows
   - patch refs for `slot_start_index` and `slot_count`
2. Scripting finalize provides:
   - finalized slots and params
   - `SceneNodeIndex -> SlotRange`
   - resolved script refs
3. Scene finalization/patch-write applies slot ranges to provisional component
   rows.

### 7.2 Concurrent Scene Plus Sidecar (Inflight Scene Context)

Graph:

1. `SceneBuild -> ScriptingSidecarFinalize -> SceneFinalize`
2. `ScriptAssetResolveOrImport -> ScriptingSidecarFinalize`

### 7.3 Standalone Sidecar After Scene Cook (Cooked Scene Context)

Graph:

1. `CookedSceneInspect -> ScriptingSidecarFinalize -> ScenePatchWrite`
2. `ScriptAssetResolveOrImport -> ScriptingSidecarFinalize`

Forbidden cycle:

1. `SceneFinalize or ScenePatchWrite -> ScriptingSidecarFinalize`

## 8. Reimport and Overwrite Semantics

### 8.1 Script Asset Reimport

Script asset import is repeatable.

For same script asset identity:

1. latest successful import overwrites descriptor/resource payload state for
   that identity.

### 8.2 Sidecar Reimport

Sidecar import is repeatable for the same scene.

For same scene target:

1. new bindings can be added.
2. existing scripted node bindings can be overwritten.
3. overwrite identity is node index.
4. nodes absent in new payload remain unchanged unless explicit delete semantics
   are added to sidecar schema.

### 8.3 Dedupe, Overwrite, and Rebinding Rules

#### 8.3.1 Script Request Dedupe

Deduplication key:

1. script asset identity key

Behavior:

1. multiple equivalent requests for same key are coalesced to one execution.
2. conflicting requests for same key in one dispatch batch are rejected.
3. conflict means same key with different effective source or cook options.

#### 8.3.2 Sidecar Payload Dedupe

Deduplication key:

1. `(scene_node_index, slot_id)` logical binding identity

Behavior:

1. duplicate binding rows for the same key in one sidecar payload are rejected.
2. no implicit "last row wins" is allowed within one payload.

#### 8.3.3 Rebinding Semantics (Across Imports)

Rebinding key:

1. scene node index

Behavior:

1. when a node appears in a new sidecar import, that node's scripting binding is
   overwritten with the new binding payload.
2. when a node does not appear in a new sidecar import, existing binding for that
   node is preserved.
3. this allows repeatable additive updates and targeted overwrites.

## 9. Validation and Diagnostics

### 9.1 Hard Validation Rules

1. request targets exactly one identity:
   - one script asset for Operation A
   - one scene for Operation B
2. sidecar target scene reference must resolve to exactly one scene key.
3. sidecar node refs must resolve to valid target-scene node indices.
4. script refs in sidecar must resolve to script assets.
5. slot and param ranges must satisfy `PakFormat_scripting.h` invariants.
6. dependency graph must remain acyclic and match Section 7.

### 9.2 Required Diagnostic Families

Script asset import:

1. invalid option combo
2. invalid compile mode
3. compiler unavailable
4. compile failed
5. source missing

Sidecar import:

1. target scene missing
2. target scene virtual path invalid
3. inflight target scene ambiguous
4. node ref unresolved
5. duplicate slot conflict
6. script ref unresolved
7. param invalid
8. patch-map invariant failure

Required diagnostic record fields:

1. severity (`info|warning|error`)
2. stable code (`script.request.*`, `script.asset.*`, `script.sidecar.*`)
3. message
4. request identity context (script key or scene key)
5. optional node/slot/script reference context when applicable
6. pipeline phase is carried by `ProgressEvent.header.phase` (existing Import
   progress channel), not by adding a new field to `ImportDiagnostic`

Summary/report requirements:

1. include counts by severity
2. include deterministic ordered diagnostics vector
3. summary counts must equal diagnostics vector counts

All diagnostics must be structured and deterministic.

### 9.3 Precise Failure Behavior

1. missing node ref:
   - hard error
   - sidecar request fails atomically with no binding writes
2. missing script asset ref:
   - hard error if unresolved after considering batch script imports plus cooked
     catalog state
   - sidecar request fails atomically
3. script request dedupe conflict:
   - hard error
   - conflicting key is reported
4. sidecar duplicate binding row:
   - hard error
   - duplicate binding identity is reported
5. rebinding overwrite:
   - success path, not diagnostic
   - prior node binding replaced for addressed node only
6. target scene missing:
   - hard error
   - sidecar request fails atomically
7. target scene virtual path invalid:
   - hard error
   - sidecar request fails atomically
8. inflight target scene ambiguous:
   - hard error
   - sidecar request fails atomically

## 10. Determinism

For deterministic inputs:

1. script asset outputs are deterministic.
2. sidecar binding outputs are deterministic.
3. diagnostics order is deterministic.
4. inflight-context and cooked-context sidecar execution produce equivalent
   serialized scripting state for the same baseline scene plus sidecar input.
5. sidecar row ordering is stable by `(scene_node_index, slot_id)`.
6. slot and param emission ordering is stable and derived from sorted identities.
7. diagnostics ordering key is stable and documented.

## 11. C++ API Shapes (Import-Module Consistent)

Scripting import uses existing Import-module request patterns; no separate
"special API model" is introduced.

Primary request carrier:

1. `ImportRequest`
2. `ImportRequest::options.scripting`
3. `ImportRequest::options.scripting.import_kind`:
   - `kScriptAsset`
   - `kScriptingSidecar`

Tooling-facing DTOs (CLI + manifest mapping only):

1. `ScriptAssetImportSettings`
2. `ScriptingSidecarImportSettings`

Normalization/validation boundary:

1. `BuildScriptAssetRequest(const ScriptAssetImportSettings&, std::ostream&)`
2. `BuildScriptingSidecarRequest(const ScriptingSidecarImportSettings&,
   std::ostream&)`

Canonical shared compile-mode ownership:

1. `src/Oxygen/Core/Meta/Scripting/ScriptCompileMode.inc`
2. `src/Oxygen/Core/Meta/Scripting/ScriptCompileMode.h`
3. Engine + Cooker/Import consume the same shared enum catalog.

## 12. ImportTool and Manifest Contracts

### 12.1 CLI Surface

Commands:

1. `texture`
2. `fbx`
3. `gltf`
4. `script`
5. `script-sidecar`
6. `batch`

Script command maps to `ScriptAssetImportSettings` and supports:

1. `--compile`
2. `--compile-mode`
3. `--script-storage`
4. `-i/--output`, `--name`, `--report`, `--content-hashing`

Scripting sidecar command maps to `ScriptingSidecarImportSettings` and
supports:

1. positional `source` OR `--bindings-inline` (exactly one)
2. required `--target-scene-virtual-path`
3. `-i/--output`, `--name`, `--report`, `--content-hashing`

`--bindings-inline` accepted forms:

1. JSON array of rows: `[ ... ]`
2. JSON object with `bindings` array: `{ "bindings": [ ... ] }`

### 12.2 Manifest Surface

Manifest supports scripting jobs under existing `jobs[]` model:

1. `type: "script"` with `source` required
2. `type: "script-sidecar"` with exactly one of:
   - `source`
   - `bindings` (inline array)
   and always `target_scene_virtual_path`

Top-level output fallback is supported:

1. top-level `output` seeds typed defaults
2. precedence is:
   - job `output`
   - `defaults.<type>.output`
   - top-level manifest `output`
   - global `--cooked-root`

## 13. Loose-Cooked Artifact Contract

### 13.1 Script Asset Artifacts

`ScriptAssetImportPipeline` owns:

1. `*.oscript` descriptor emission
2. `scripts.table`
3. `scripts.data`

### 13.2 Scripting Sidecar Artifacts

`ScriptingSidecarImportPipeline` owns:

1. `script-bindings.table`
2. `script-bindings.data`
3. scene scripting-component patching for exactly one target scene

### 13.3 Record Layout and Invariants

All serialized record layout/offset/range invariants are governed by:

1. `PakFormat_core.h` (region/table contracts)
2. `PakFormat_scripting.h` (scripting records and indexing semantics)

No alternate table naming or parallel scripting record layouts are allowed.

## 14. Phased Implementation Plan and Status

Status snapshot:

1. `[x]` Phase 1: Shared compile-mode ownership moved to Core/Meta/Scripting.
2. `[x]` Phase 2: Request builders and validation contracts in place.
3. `[x]` Phase 3: `ScriptAssetImportPipeline` + job path + deterministic output
   behavior.
4. `[x]` Phase 4: `ScriptingSidecarImportPipeline` scene resolution, merge,
   overwrite/rebind semantics, diagnostics.
5. `[x]` Phase 5: ImportTool integration for script + sidecar commands and
   manifest support.
6. `[x]` Phase 6: Hardening pass (context-aware resolution, sidecar atomicity
   ordering, expanded diagnostics/tests).

Implementation guardrails for all phases:

1. Scene pipeline and scripting pipelines stay separated by ownership.
2. Jobs are narrow in scope; pipelines are the concurrent execution engines.
3. No deprecated fallback code paths retained after replacement.

Test obligations (tracked and maintained):

1. request-builder contract tests
2. script asset pipeline/job tests
3. sidecar pipeline/job tests including:
   - missing target scene
   - missing script ref
   - duplicate slot conflict
   - unresolved node
   - deterministic ordering
   - mixed-batch script success plus sidecar failure behavior
4. manifest + CLI contract tests for script/sidecar options and inline bindings
5. dedicated scripting import test program:
   - `Oxygen.Cooker.AsyncImportScripting.Tests`
