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

## 11. Implementation Work Plan

Tracking format:

1. `[ ]` not started
2. `[x]` completed
3. A phase is complete only when all coding tasks, test tasks, and exit criteria
   are satisfied.

### 11.1 Phase 0: Core Meta Compile-Mode Migration

Coding tasks:

- [x] Move Engine-local `ScriptCompileMode` source-of-truth into:
  `src/Oxygen/Core/Meta/Scripting/ScriptCompileMode.inc`
- [x] Define shared include facade:
  `src/Oxygen/Core/Meta/Scripting/ScriptCompileMode.h`
- [x] Update Engine includes to consume Core/Meta definition.
- [x] Update Cooker/Import includes to consume Core/Meta definition.
- [x] Remove duplicate Engine-local compile-mode catalogs.

Test tasks:

- [x] Compile/link tests proving Engine and Cooker/Import both consume the same
  Core/Meta compile-mode symbols.
- [x] Regression tests for compile-mode string/enum mappings used by tooling.

Exit criteria:

- [x] No Engine-local `ScriptCompileMode` source-of-truth remains.
- [x] Engine + Cooker/Import build against shared Core/Meta compile mode.

### 11.2 Phase 1: Public Request + Builder API

Coding tasks:

- [x] Add `ImportOptions::scripting` with:
  `ScriptingImportKind`, `ScriptStorageMode`, `compile_scripts`,
  `compile_mode`, and `target_scene_virtual_path`.
- [x] Keep a single request model: use only `ImportRequest`.
- [x] Sidecar requests require `ImportOptions::scripting.target_scene_virtual_path`.
- [x] Add settings types:
  `ScriptAssetImportSettings`, `ScriptingSidecarImportSettings`.
- [x] Add builder entrypoints:
  `BuildScriptAssetRequest(...)`, `BuildScriptingSidecarRequest(...)`,
  both returning `std::optional<ImportRequest>`.
- [x] Keep `SceneImportSettings` script-agnostic.
- [x] Keep `TextureImportSettings` script-agnostic.

Test tasks:

- [x] API compile/link tests for settings/builder/ImportOptions scripting
  symbols.
- [x] Builder validation tests:
  missing `target_scene_virtual_path`, invalid canonical path,
  invalid compile/storage combination.

Exit criteria:

- [x] Script/sidecar C++ API uses the existing single `ImportRequest` model.
- [x] Invalid builder inputs fail deterministically with structured diagnostics.

### 11.3 Phase 2: ScriptAssetImportPipeline Endpoint

Coding tasks:

- [x] Implement `ScriptAssetImportPipeline` endpoint in Import flow.
- [x] Implement script asset descriptor/resource emission contract.
- [x] Enforce `compile_scripts` + `script_storage` validity rules.
- [x] Implement identity overwrite semantics for script-asset reimport.

Test tasks:

- [x] Script asset standalone import success.
- [x] Script asset reimport overwrite by script identity.
- [x] Compile enabled/disabled behavior tests.
- [x] External-storage + compile rejection tests.

Exit criteria:

- [x] Script asset imports are repeatable and deterministic for same inputs.
- [x] Reimport overwrite semantics are verified.

### 11.4 Phase 3: ScriptingSidecarImportPipeline Resolution + Binding

Coding tasks:

- [x] Implement `ScriptingSidecarImportPipeline` endpoint.
- [x] Implement scene resolution using `target_scene_virtual_path` only.
- [x] Delegate path canonicalization/precedence/tombstones to
  `content::VirtualPathResolver` (no custom resolver wheel).
- [x] Implement inflight-context fast-path with duplicate-match rejection.
- [x] Resolve final `target_scene_key` and scene binding context.
- [x] Implement sidecar table emission:
  `ScriptingComponentRecord`, `ScriptSlotRecord`, `ScriptParamRecord`.

Test tasks:

- [x] Sidecar path-only success with inflight scene context.
- [x] Sidecar path-only success with cooked scene context.
- [x] Sidecar request without path rejected.
- [x] Invalid canonical path rejected.
- [x] Inflight duplicate path match rejected deterministically.
- [x] Resolver delegation precedence test (last-mounted-wins + tombstone behavior).
- [x] Missing node and missing script reference hard-failure tests.
- [x] Sidecar payload malformed JSON hard-failure test.
- [x] Sidecar duplicate `(node_index, slot_id)` hard-failure test.
- [x] Sidecar missing source file hard-failure test.
- [x] Sidecar missing target scene in valid cooked root hard-failure test.

Exit criteria:

- [x] Sidecar scene resolution is deterministic and wheel-free.
- [x] Sidecar apply remains atomic on failures.

### 11.5 Phase 4: Cross-Pipeline Patching + Finalization

Coding tasks:

- [x] Implement scene binding context providers:
  from scene build output and cooked scene inspection.
- [x] Implement patch refs (`slot_start_index`, `slot_count`) and patch apply.
- [x] Implement standalone-mode descriptor rewrite/index update wiring.
- [x] Enforce sidecar node-binding overwrite semantics (node-index identity).

Test tasks:

- [x] Concurrent flow parity test (scene + sidecar).
- [x] Standalone sidecar-after-scene flow test.
- [x] Context parity test (concurrent vs standalone serialized equivalence).
- [x] Node-binding overwrite and additive update behavior tests.
- [x] Node-binding overwrite/rebind behavior test.

Exit criteria:

- [x] Both concurrent and standalone workflows behave identically for same inputs.
- [x] Patch/ref application is deterministic and validated.

### 11.6 Phase 5: Async Service and Reporting Integration

Coding tasks:

- [x] Keep single submit path (`SubmitImport(ImportRequest, ...)`) and dispatch
  script/sidecar via `request.options.scripting.import_kind`.
- [x] Wire scripting dispatch through existing job-factory/internal job path.
- [ ] Extend `ImportReport` counters:
  `scripts_written`, `scripting_components_written`, `script_slots_written`,
  `script_params_written`.
- [ ] Preserve existing callback, shutdown, and cancellation contracts.

Test tasks:

- [ ] Submit rejection semantics (`std::nullopt`) for invalid/shutdown cases.
- [ ] Completion/progress callback behavior parity with existing imports.
- [ ] Report counter population tests for script-only and sidecar jobs.
- [ ] Mixed-batch commit semantics:
  successful script imports remain committed when sidecar fails atomically.

Exit criteria:

- [ ] Single submit API behavior remains identical to existing import patterns.
- [ ] Report and telemetry counters are consistent and deterministic.

### 11.7 Phase 6: Diagnostics, Determinism, and Conformance Closure

Coding tasks:

- [x] Ensure script diagnostics use stable code families:
  `script.request.*`, `script.asset.*`, `script.sidecar.*`.
- [x] Ensure phase context is reported via existing `ProgressEvent`.
- [x] Ensure deterministic ordering for sidecar rows, slot/param emission,
  and diagnostics.
- [x] Move scripting import tests into dedicated `AsyncImportScripting` target.
- [x] Consolidate scripting import tests into shared DRY fixtures/helpers.

Test tasks:

- [ ] Diagnostic field completeness tests (`severity`, `code`, `message`,
  optional `source_path`/`object_path`).
- [ ] Summary-counter consistency tests vs emitted diagnostics.
- [ ] Dispatch matrix tests:
  script-only, sidecar-only inflight, sidecar-only cooked, script+sidecar.
- [x] Deterministic repeated-run byte/record ordering tests.
- [ ] Dependency ordering and cycle-rejection tests.

Exit criteria:

- [ ] Diagnostics are CI-grade, stable, and complete.
- [ ] Determinism guarantees in Section 10 are fully covered by tests.

## 12. Non-Goals

1. No universal asset/resource cooking abstraction forced across domains.
2. No new sidecar transport mechanism.
3. No runtime API redesign outside required scripting-sidecar compatibility.
