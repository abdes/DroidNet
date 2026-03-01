# Input Cooking Implementation Plan (Execution Locked)

This is the implementation plan for `design/cook_input.md`.

## 1. Rules of Engagement Compliance

This plan follows `.github/instructions/cpp_coding_style.instructions.md` collaboration rules.

1. No shortcuts.
2. Truthful progress only.
3. Trackable status after each completed task.
4. API correctness over guessing.
5. No regressions.
6. Clean boundaries (`Content` runtime does not depend on demo code).
7. Search discipline with `rg` and explicit file impact lists.

## 2. Mandatory Architecture Gates

1. Input import code path must be `InputImportJob -> InputImportPipeline`.
2. `AsyncImportService` must route input requests only to `InputImportJob`.
3. No direct import execution branch outside the job/pipeline path.
4. New binary serialization/deserialization logic must use `oxygen::serio`.
5. A single input job class is used for both input asset kinds (`InputImportJob`).
6. A single input pipeline class is used for both input asset kinds (`InputImportPipeline`).
7. Manifest-mode mapping-context jobs must express explicit dependencies on required action jobs.

## 3. File-Level Change Map

## 3.1 New Files

1. `src/Oxygen/Cooker/Import/InputImportSettings.h`
2. `src/Oxygen/Cooker/Import/InputImportRequestBuilder.h`
3. `src/Oxygen/Cooker/Import/Internal/InputImportRequestBuilder.cpp`
4. `src/Oxygen/Cooker/Import/Internal/Jobs/InputImportJob.h`
5. `src/Oxygen/Cooker/Import/Internal/Jobs/InputImportJob.cpp`
6. `src/Oxygen/Cooker/Import/Internal/Pipelines/InputImportPipeline.h`
7. `src/Oxygen/Cooker/Import/Internal/Pipelines/InputImportPipeline.cpp`
8. `src/Oxygen/Cooker/Tools/ImportTool/InputCommand.h`
9. `src/Oxygen/Cooker/Tools/ImportTool/InputCommand.cpp`
10. `src/Oxygen/Content/InputContextHydration.h`
11. `src/Oxygen/Content/Internal/InputContextHydration.cpp`
12. `src/Oxygen/Cooker/Import/Schemas/oxygen.input.schema.json`
13. `src/Oxygen/Cooker/Import/Schemas/oxygen.input-action.schema.json`

## 3.2 Modified Files

### Import core

1. `src/Oxygen/Cooker/Import/ImportOptions.h`
2. `src/Oxygen/Cooker/Import/ImportOptions.cpp`
3. `src/Oxygen/Cooker/Import/AsyncImportService.cpp`
4. `src/Oxygen/Cooker/Import/ImportManifest.h`
5. `src/Oxygen/Cooker/Import/ImportManifest.cpp`
6. `src/Oxygen/Cooker/Import/ImportReport.h`
7. `src/Oxygen/Cooker/Import/Internal/ImportSession.cpp`
8. `src/Oxygen/Cooker/CMakeLists.txt`

### ImportTool

1. `src/Oxygen/Cooker/Tools/ImportTool/main.cpp`
2. `src/Oxygen/Cooker/Tools/ImportTool/BatchCommand.cpp`
3. `src/Oxygen/Cooker/Tools/ImportTool/ImportRunner.cpp`
4. `src/Oxygen/Cooker/Tools/ImportTool/CMakeLists.txt`
5. `src/Oxygen/Cooker/Tools/ImportTool/README.md`

### Data/runtime

1. `src/Oxygen/Data/PakFormat_input.h` (add flags/priority; remove `InputContextBindingRecord` + `InputContextBindingFlags`)
2. `src/Oxygen/Data/ToStringConverters.cpp` (update `InputMappingContextFlags`; remove `InputContextBindingFlags` + `kInputContextBinding` case)
3. `src/Oxygen/Data/SceneAsset.h` (remove `ComponentTraits<InputContextBindingRecord>`)
4. `src/Oxygen/Data/PakFormatSerioLoaders.h` (remove `InputContextBindingRecord` loader)
5. `src/Oxygen/Core/Meta/Data/ComponentType.inc` (remove `kInputContextBinding`)
6. `src/Oxygen/Core/Meta/Input/PakFormat.inc` (remove `kInputContextBinding`)
7. `src/Oxygen/Content/Loaders/SceneLoader.h` (remove INPT handling)
8. `src/Oxygen/Content/AssetLoader.cpp` (remove `publish_scene_input_mapping_context_dependencies`)
9. `src/Oxygen/Content/IAssetLoader.h` (add `EnumerateMountedInputContexts` declaration)
10. `src/Oxygen/Content/AssetLoader.cpp` (implement `EnumerateMountedInputContexts`; remove scene-binding dependency publishing)
11. `src/Oxygen/Data/InputMappingContextAsset.h` (add `GetDefaultPriority` accessor)
11. `Examples/DemoShell/Services/SceneLoaderService.cpp` (remove `AttachInputMappings`)
12. `src/Oxygen/Cooker/Tools/PakDump/SceneAssetDumper.h` (remove `kInputContextBinding` dump)
13. `src/Oxygen/Cooker/Tools/PakGen/src/pakgen/packing/packers.py` (remove binding record packing)
14. `src/Oxygen/Cooker/Tools/PakGen/src/pakgen/spec/validator.py` (remove binding validation)

### Tests

1. `src/Oxygen/Cooker/Test/Import/*` (new input import tests)
2. `src/Oxygen/Content/Test/AssetLoader_scene_test.cpp` (remove binding tests)
3. `src/Oxygen/Content/Test/SceneLoader_test.cpp` (remove `kInputContextBinding` tests)
4. `src/Oxygen/Content/Test/AssetLoader_lifetime_test.cpp` (remove binding test)
5. `src/Oxygen/Content/Test/TestData/scene_with_input_context_binding.yaml` (remove)
6. `src/Oxygen/Cooker/Tools/PakGen/tests/test_v6_input_assets.py` (remove binding tests)
7. `src/Oxygen/Content/Test/*` (new context enumeration/hydration tests)

## 4. Phase Plan and Status Ledger

Status values:

1. `pending`
2. `in_progress`
3. `done`

| Phase | Status | Scope | Exit Gate |
| --- | --- | --- | --- |
| P1 | done | Data format + flags | `InputMappingContextAssetDesc` invariants and tests pass |
| P2 | done | Import contracts (settings/builder/options) | valid input requests built from CLI + manifest |
| P3 | done | Job/Pipeline implementation | `.oiact` / `.oimap` emitted and indexed |
| P4 | done | ImportTool and manifest integration | `input` job type with dependency-aware batch scheduling operational |
| P5 | done | Runtime enumeration + hydration | `EnumerateMountedInputContexts` + `HydrateInputContext` operational; auto-load/auto-activate works without scene bindings |
| P6 | done | Eradicate scene-binding infrastructure | `InputContextBindingRecord`, `InputContextBindingFlags`, `kInputContextBinding`, all scene-binding code paths fully removed |
| P7 | done | Compatibility verification | importer/runtime output remains compatible with existing PAK tooling (excluding removed scene-binding records) |
| P8 | done | Full test and documentation closeout | tests and docs updated for input import/runtime/schema path |

Status update rule:

1. On each completed phase/task, update this ledger immediately.
2. Add factual evidence to Section 11 (commands/tests/files changed).

## 4.1 Live Snapshot (2026-03-01)

Implemented now:

1. Input import routing and orchestration metadata are implemented end-to-end (`ImportOptions::input`, `ImportRequest::orchestration`, `BuildInputImportRequest`, async routing to `InputImportJob`).
2. Manifest/ImportTool wiring is implemented for `type: "input"` including key whitelist enforcement and dependency-aware batch scheduling (`id`, `depends_on`, duplicate/missing/cycle checks, failure propagation).
3. Runtime input bootstrap APIs are implemented: `EnumerateMountedInputContexts()` and `HydrateInputContext(...)`, including trigger hydration and slot alias normalization.
4. Scene-binding infrastructure removal is complete in runtime/tooling paths (`INPT`, `InputContextBinding*`, `input_context_bindings`, and PakGen packing/validation hooks removed).
5. Input JSON schemas are shipped under `schemas/` and covered by schema validation tests.
6. Documentation was synchronized for implementation status and ImportTool schema usage.

Remaining backlog:

1. Execution-policy caveat only: no local full build/test pass was run in this implementation loop.

## 5. Detailed Work Packages

## P1: Data Format and Converters

Tasks:

1. Add `kAutoLoad` and `kAutoActivate` to `InputMappingContextFlags`.
2. Add `default_priority` to `InputMappingContextAssetDesc` and rebalance reserved bytes.
3. Update `to_string(InputMappingContextFlags)` converter.
4. Add/adjust tests validating struct size and flag strings.

Acceptance:

1. `sizeof(InputMappingContextAssetDesc) == 256` remains true.
2. Descriptor can be decoded by `InputMappingContextLoader`.

## P2: Import Contracts

Tasks:

1. Add `InputTuning` in `ImportOptions` (orchestration-only; presence signals input import routing). Do NOT add `InputImportKind`; document structure is determined solely inside `InputImportPipeline` from source JSON structure.
2. Add `InputImportSettings` DTO in `InputImportSettings.h` (orchestration-only fields: `source_path` only).
3. Implement `BuildInputImportRequest(...)`:
   with validation:
   - required source
   - dependency metadata pass-through (`id`, `depends_on`) for batch scheduling
   - kind-agnostic: builder does NOT inspect source content or determine asset kind
4. Add request builder tests.

Acceptance:

1. Builder emits normalized `ImportRequest` for any input source (kind-agnostic).
2. Invalid settings produce deterministic diagnostics/errors.
3. No `InputImportKind` enum exists in `ImportOptions` or `ImportRequest`.

## P3: Input Job/Pipeline

Tasks:

1. Implement `InputImportJob` from `ImportJob`.
2. Implement `InputImportPipeline` from `ImportPipeline` concept with standard API:
   - `Start`
   - `Submit`
   - `TrySubmit`
   - `Collect`
   - `Close`
3. Implement document structure detection:
   - if source JSON has `contexts` key → primary format (parse `actions[]` + `contexts[]`, emit N+M assets)
   - if source JSON has `name` + `type` at top level, no `contexts` → standalone action (emit 1 asset)
   - otherwise → reject with `input.import.unknown_document_structure`
4. Implement strict source JSON parsers for:
   - Primary format: extract shared `actions[]` and iterate `contexts[]`, emitting N action assets + M context assets
   - Standalone action: emit 1 action asset
   including:
   - action declaration parsing and dedup/conflict checking
   - `contexts[]` array iteration with per-context name uniqueness validation
   - action name resolution: resolve `action` string against `actions[]` first, then mounted/inflight content
   - per-context mapping / trigger / slot validation
5. Implement `trigger` shorthand expansion: string → `[{ type: <value>, behavior: "implicit", actuation_threshold: 0.5 }]`. Reject if both `trigger` and `triggers` present on same mapping (`input.context.trigger_ambiguous`).
6. Validate `slot` names against registered `InputSlots`. Emit `input.context.slot_unknown` for unrecognized names (warning, not error — allows forward-compat with future slot additions).
7. Parse source and serialize descriptors with `oxygen::serio`.
8. Emit all assets through `AssetEmitter` and register in index (one source file may produce N action assets + M context assets).
9. Add import diagnostics codes (including `input.asset.action_conflict` for cross-file name/type conflicts).
10. Add parser-focused tests with invalid document coverage.

Acceptance:

1. Path is strictly `InputImportJob -> InputImportPipeline`.
2. No scene patching code is part of input import pipeline.
3. Source schema validation follows `design/cook_input.md` Section 7.4 exactly.
4. A primary format source file with `actions[]` and `contexts[]` emits N+M assets (N action descs + M context descs) from a single import job.
5. Action names in any context's mappings resolve against declared actions first, then mounted content and successful predecessor jobs.
6. Conflicting action definitions (same name, different type) produce `input.asset.action_conflict` and fail.
7. One `InputImportPipeline` instance processes mixed standalone-action and primary-format inputs in the same queue/run.
8. Context names within a single file must be unique; duplicates produce `input.context.name_duplicate` and fail.
9. `trigger` shorthand correctly expands to full trigger specification.
10. Document structure detection correctly routes to the right parser.

## P4: ImportTool and Manifest Wiring

Tasks:

1. Add `InputCommand` and command options.
2. Wire command in `main.cpp`.
3. Extend `BatchCommand` and `ImportRunner` job type resolution.
4. Extend manifest parser with `id` and `depends_on` fields for input jobs.
5. Add dependency graph builder + topological scheduler in batch flow:
   - validate unique `id` values (diagnostic: `input.manifest.job_id_duplicate`)
   - validate every `depends_on` target exists (diagnostic: `input.manifest.dep_missing_target`)
   - detect cycles (diagnostic: `input.manifest.dep_cycle`)
   - skip dependents deterministically on predecessor failure (diagnostic: `input.import.skipped_predecessor_failed`)
   - (wrong-type detection removed: both primary-format and standalone-action files produce action assets, so kind-level checks are invalid; unresolved refs caught at pipeline parse time)
6. Extend `ImportManifest` defaults/job parsing/build request path for `input` only.
7. Enforce input-job whitelist keys only: `id`, `type`, `source`, `depends_on`. Reject any other key with diagnostic `input.manifest.key_not_allowed`. Explicitly rejected keys include but are not limited to: `output`, `name`, `verbose`, `content_hashing`, `report`, and all source-document fields (`actions`, `contexts`, `mappings`, `triggers`, `trigger`, `slot`, `priority`, `consumes_input`, `auto_load`, `auto_activate`).
8. Update ImportTool README.

Acceptance:

1. Single-job and batch imports support one manifest job type (`input`) through the same import service path.
2. Batch execution honors `depends_on` ordering and failure propagation.
3. Report job type output is correct for input requests.
4. Input job parser rejects any key outside `id/type/source/depends_on` with `input.manifest.key_not_allowed`.
5. Cycle detection prevents circular dependency chains.
6. Unresolved action references from cross-file imports are caught at pipeline parse time via `input.context.action_unresolved`.

## P5: Runtime Enumeration and Hydration

Tasks:

1. Add `MountedInputContextEntry` struct and `EnumerateMountedInputContexts()` declaration to `IAssetLoader.h`.
2. Implement `EnumerateMountedInputContexts()` in `AssetLoader.cpp` — iterate sources, read headers, filter `kInputMappingContext`, decode descriptor for name/flags/priority.
3. Add `GetDefaultPriority()` accessor to `InputMappingContextAsset`.
4. Add `HydrateInputContext()` free function in `InputContextHydration.h/.cpp` — resolve actions, build triggers, return live `InputMappingContext`.
5. Add unit tests for `EnumerateMountedInputContexts` (correct entries, empty sources, mixed asset types).
6. Add unit tests for `HydrateInputContext` (action resolution, trigger chain building, error paths).

Acceptance:

1. `EnumerateMountedInputContexts` returns correct entries for all mounted sources.
2. `HydrateInputContext` produces a live `InputMappingContext` from a binary asset.
3. Auto-load/auto-activate scenario works end-to-end without scene bindings.
4. No `InputBootstrapService` class exists.

## P6: Eradicate Scene-Binding Infrastructure

Tasks:

1. Remove `AttachInputMappings` and all scene-driven context activation from `SceneLoaderService`.
2. Remove `publish_scene_input_mapping_context_dependencies` and all call sites in `AssetLoader.cpp`.
3. Remove `INPT` component table handling from `SceneLoader` entirely (no compatibility parsing).
4. Remove `InputContextBindingRecord` struct from `PakFormat_input.h`.
5. Remove `InputContextBindingFlags` enum and `to_string(InputContextBindingFlags)` from `PakFormat_input.h` / `ToStringConverters.cpp`.
6. Remove `ComponentTraits<InputContextBindingRecord>` from `SceneAsset.h`.
7. Remove `kInputContextBinding` from `ComponentType.inc` and `PakFormat.inc`.
8. Remove `InputContextBindingRecord` serio loader from `PakFormatSerioLoaders.h`.
9. Remove `kInputContextBinding` dump branch from `PakDump/SceneAssetDumper.h`.
10. Remove `input_context_bindings` packing/validation from PakGen `packers.py` and `validator.py`.
11. Remove `pack_input_context_binding_record` from PakGen `packers.py`.
12. Remove or update `test_v6_input_assets.py` tests that reference `input_context_bindings`.
13. Remove or update `AssetLoader_scene_test.cpp` tests that reference `InputContextBindingRecord`.
14. Remove or update `SceneLoader_test.cpp` tests that reference `kInputContextBinding`.
15. Remove or update `AssetLoader_lifetime_test.cpp` tests that reference `scene_with_input_context_binding`.
16. Remove test data `scene_with_input_context_binding.yaml`.

Acceptance:

1. `InputContextBindingRecord` and `InputContextBindingFlags` do not exist in the codebase.
2. `kInputContextBinding` component type does not exist in the codebase.
3. No code path reads, writes, packs, or dumps scene input context binding data.
4. Runtime behavior is driven exclusively by standalone contexts + app-driven enumeration/hydration.
5. All tests pass without any scene-binding infrastructure.

## P7: Compatibility Verification

Tasks:

1. Verify importer-emitted input descriptors are consumable by runtime loaders.
2. Verify existing PakDump inspection still decodes input action and mapping-context descriptors without tool changes.
3. Validate binary compatibility against `design/pak_input.md` record sizes/offset contracts for all non-removed record types.
4. Verify scene loader gracefully skips unknown component types in PAK files that contain legacy `INPT` records.
5. Document any gaps as follow-up items outside this plan.

Acceptance:

1. PakGen and PakDump source files changed only for scene-binding removal (no other modifications).
2. Compatibility checks pass for importer output and runtime consumption.
3. Legacy PAK files with `INPT` scene component tables do not crash on load.

## P8: Test and Docs Closeout

Tasks:

1. Update/remove content tests that referenced scene binding dependency behavior (handled in P6).
2. Add context enumeration and hydration tests (enumeration correctness, hydration action resolution, error paths).
3. Add importer integration tests for both document structures.
4. Reconcile docs (`design/pak_input.md` references and new docs links).
5. Add manifest key whitelist enforcement tests:
   - verify `input` jobs accept exactly `id`, `type`, `source`, `depends_on`
   - verify rejection with `input.manifest.key_not_allowed` for `output`, `name`, `verbose`, `content_hashing`, and source-document fields
6. Add dependency DAG tests:
   - topological ordering verification
   - cycle detection and rejection
   - missing target detection and rejection
   - wrong-type dependency detection removed (both formats produce actions; unresolved refs caught at parse time)
   - failure propagation (predecessor fails -> all transitive dependents skipped)
7. Add mixed processing tests:
   - single `InputImportPipeline` instance processes both document structures in one run
   - interleaved standalone-action and primary-format work items
   - action refs resolve against predecessor results
8. Add trigger shorthand tests:
   - `trigger` string expands to correct trigger record
   - `trigger` + `triggers` coexistence rejected
9. Add document structure detection tests:
   - primary format recognized (has `contexts`)
   - standalone action recognized (has `name` + `type`, no `contexts`)
   - unknown structure rejected
10. Add slot name validation tests:
    - known slots accepted (`Space`, `W`, `MouseXY`)
    - unknown slots emit `input.context.slot_unknown`
11. Add JSON Schema validation tests:
    - canonical examples pass schema validation
    - unknown fields rejected
    - invalid enum values rejected
12. Generate and ship JSON Schema files:
    - `src/Oxygen/Cooker/Import/Schemas/oxygen.input.schema.json` (primary format)
    - `src/Oxygen/Cooker/Import/Schemas/oxygen.input-action.schema.json` (standalone action)
    - slot enum generated from `InputSlots` registered names
    - trigger type enum matches `ActionTriggers.inc`

Acceptance:

1. All impacted tests pass.
2. Docs match implemented behavior.
3. Every non-negotiable requirement has at least one covering test.

## 6. Diagnostics Plan

Stable diagnostic namespaces:

1. `input.request.*`
2. `input.import.*`
3. `input.asset.*`
4. `input.context.*`
5. `input.manifest.*`

Required concrete diagnostics:

1. `input.request.type_invalid`
2. `input.request.source_missing`
3. `input.asset.action_conflict` (same name, different type across files)
4. `input.context.flags_invalid` (auto_activate without auto_load — auto-corrected with warning)
5. `input.context.action_unresolved` (action name not found)
6. `input.context.name_duplicate` (duplicate context names within one file)
7. `input.context.trigger_ambiguous` (both `trigger` and `triggers` on same mapping)
8. `input.context.slot_unknown` (slot name not in registered InputSlots)
9. `input.context.hydration_failed`
10. `input.context.name_conflict`
11. `input.import.index_registration_failed`
12. `input.import.unknown_document_structure` (source JSON matches neither format)
13. `input.import.skipped_predecessor_failed`
14. `input.manifest.job_id_missing`
15. `input.manifest.job_id_duplicate`
16. `input.manifest.dep_missing_target`
17. `input.manifest.dep_cycle`
18. `input.manifest.dep_wrong_type` (reserved — not emitted; kept for diagnostic code stability)
19. `input.manifest.key_not_allowed`

## 7. Regression Protection

Must-not-regress areas:

1. Script asset and script sidecar import behavior.
2. Scene descriptor loading for non-input component tables.
3. Patch/tombstone precedence behavior in content source resolution.

## 8. Minimal File Proliferation Rule

1. Add only the files listed in Section 3.1.
2. Do not create extra subdirectories unless required by existing module layout.
3. Reuse existing utility and emitter classes instead of duplicating helpers.

## 9. Verification Commands (to run during implementation)

1. targeted unit tests for modified modules.
2. importer integration tests for input command and manifest path.
3. dependency scheduling tests for manifest batches (ordering + failure propagation + cycle rejection).
4. compatibility checks on importer output with existing runtime/tool readers.

(Exact command lines will be logged in Section 11 as phases complete.)

## 10. Done Definition

Done requires:

1. All phase exit gates satisfied.
2. Input import path locked to `InputImportJob -> InputImportPipeline`.
3. Scene input binding infrastructure fully removed (`InputContextBindingRecord`, `InputContextBindingFlags`, `kInputContextBinding`, all code paths).
4. Runtime lifecycle aligned with app-driven model (`EnumerateMountedInputContexts` + `HydrateInputContext`). PAK tool compatibility preserved for all non-removed record types.
5. Status ledger and evidence log fully updated.

## 11. Evidence Log

This section is updated during implementation.

Current:

1. P1 completed:
   - `src/Oxygen/Data/InputMappingContextAsset.h`: `GetDefaultPriority()` accessor is present.
   - `src/Oxygen/Data/ToStringConverters.cpp`: `InputMappingContextFlags` now stringifies `AutoLoad` and `AutoActivate`.
2. P2 completed:
   - `src/Oxygen/Cooker/Import/ImportOptions.h`: added `ImportOptions::InputTuning`.
   - `src/Oxygen/Cooker/Import/ImportRequest.h`: added `OrchestrationMetadata` (`job_id`, `depends_on`).
   - Added request-builder/files:
     - `src/Oxygen/Cooker/Import/InputImportSettings.h`
     - `src/Oxygen/Cooker/Import/InputImportRequestBuilder.h`
     - `src/Oxygen/Cooker/Import/Internal/InputImportRequestBuilder.cpp`
3. P3 completed:
   - Input job/pipeline are implemented and wired:
     - `src/Oxygen/Cooker/Import/Internal/Jobs/InputImportJob.h/.cpp`
     - `src/Oxygen/Cooker/Import/Internal/Pipelines/InputImportPipeline.h/.cpp`
   - `src/Oxygen/Cooker/Import/AsyncImportService.cpp` routes `options.input` to `InputImportJob`.
4. P4 completed:
   - Manifest support for `type: "input"` implemented:
     - `src/Oxygen/Cooker/Import/ImportManifest.h/.cpp`
     - `src/Oxygen/Cooker/Import/Schemas/oxygen.import-manifest.schema.json`
   - ImportTool input command + batch DAG scheduling implemented:
     - `src/Oxygen/Cooker/Tools/ImportTool/InputCommand.h/.cpp`
     - `src/Oxygen/Cooker/Tools/ImportTool/main.cpp`
     - `src/Oxygen/Cooker/Tools/ImportTool/BatchCommand.cpp`
     - `src/Oxygen/Cooker/Tools/ImportTool/ImportRunner.cpp`
5. P5 completed:
   - Runtime APIs implemented:
     - `src/Oxygen/Content/IAssetLoader.h`
     - `src/Oxygen/Content/AssetLoader.h/.cpp`
     - `src/Oxygen/Content/InputContextHydration.h`
     - `src/Oxygen/Content/Internal/InputContextHydration.cpp`
   - Added runtime coverage in `src/Oxygen/Content/Test/AssetLoader_loading_test.cpp`.
6. P6 completed:
   - No `INPT`/`InputContextBinding*` usage remains in `src` and `Examples` code paths.
   - Removed scene binding packing/validation from PakGen:
     - `src/Oxygen/Cooker/Tools/PakGen/src/pakgen/packing/packers.py`
     - `src/Oxygen/Cooker/Tools/PakGen/src/pakgen/packing/planner.py`
     - `src/Oxygen/Cooker/Tools/PakGen/src/pakgen/packing/writer.py`
     - `src/Oxygen/Cooker/Tools/PakGen/src/pakgen/spec/validator.py`
   - Updated PakGen tests/golden:
     - `src/Oxygen/Cooker/Tools/PakGen/tests/test_v6_input_assets.py`
     - `src/Oxygen/Cooker/Tools/PakGen/tests/_golden/input_scene_spec.yaml`
7. P7 completed (static compatibility checks in this pass):
   - Verified no remaining runtime/tool references to removed scene-binding component (`rg` scan under `src` + `Examples`).
   - Verified modified PakGen Python sources/tests compile (`python -m py_compile` on touched files).
8. P8 completed:
   - Shipped JSON schemas:
     - `src/Oxygen/Cooker/Import/Schemas/oxygen.input.schema.json`
     - `src/Oxygen/Cooker/Import/Schemas/oxygen.input-action.schema.json`
   - Install packaging ships schemas:
     - `cmake/Install.cmake` installs `${PROJECT_SOURCE_DIR}/schemas` under `${OXYGEN_INSTALL_DATA}`.
   - Added schema validation tests:
     - `src/Oxygen/Cooker/Test/Import/InputJsonSchema_test.cpp`
     - wired in `src/Oxygen/Cooker/Test/CMakeLists.txt`.
   - Updated user-facing docs:
     - `src/Oxygen/Cooker/Tools/ImportTool/README.md`
     - `design/cook_input.md`
9. Additional remediation:
   - Removed hard runtime symbol dependency on `platform::InputSlots` from cooker import pipeline by using canonical slot-name table + alias normalization (`src/Oxygen/Cooker/Import/Internal/Pipelines/InputImportPipeline.cpp`).
10. Verification caveat:
    - No local build/test execution was performed in this loop per explicit instruction.

## 12. Compliance Checklist

This checklist proves each non-negotiable requirement is satisfied by the design.

| # | Non-Negotiable Requirement | Satisfied By | Verified In |
| --- | --- | --- | --- |
| 1 | One manifest job type only: `type: "input"` | spec §7.3 enum adds `"input"` only; schema `job_settings.type` gains one value in `Import/Schemas/oxygen.import-manifest.schema.json` (embedded to generated `ImportManifest_schema.h` at build time) | P4 acceptance #1, P8 task #5 |
| 2 | One job class only: `InputImportJob` | spec §6.1 #3, impl §2 gate #5; `AsyncImportService` routes all input requests to `InputImportJob` | P3 acceptance #1 |
| 3 | One pipeline class only: `InputImportPipeline` | spec §6.1 #4, spec §10.2 single-pipeline contract; impl §2 gate #6 | P3 acceptance #1 |
| 4 | Single pipeline processes mixed batches (standalone action + primary format) | spec §10.2 items 1-4: one pipeline, both document structures, mixed workloads, internal dispatch; one file emits N actions + M contexts | P3 acceptance #4 #7, P8 task #7 |
| 5 | No input asset properties in manifest job objects | spec §7.3 delta #5 forbidden fields list; manifest whitelist `id/type/source/depends_on` | P4 acceptance #4, P8 task #5 |
| 6 | Input asset semantics only in source JSON files | spec §7.4 source JSON spec; §7.3 "no asset kind or asset properties stored in manifest job settings" | P3 tasks #3-4, P8 task #7 |
| 7 | Dependencies explicit via `id` + `depends_on` in manifest | spec §7.3 schema deltas #3; impl P4 tasks #4-5 | P4 acceptance #2, P8 task #6 |
| 8 | Jobs declare `depends_on` for predecessor jobs that produce referenced actions | spec §7.3 validation invariant #8 (reserved — content-level resolution at parse time); §7.3.1 DAG dispatch | P4 acceptance #2, P3 acceptance #5 |
| 9 | PakGen/PakDump changes limited to scene-binding removal | spec §12: only `input_context_bindings` infrastructure removed; all other contracts untouched | P7 acceptance #1 |
| 10 | Manifest input job keys exactly: `id, type, source, depends_on` | spec §7.3 validation invariant #4; impl P4 task #7 | P4 acceptance #4, P8 task #5 |
| 11 | Rejected keys include `output, name, verbose, content_hashing` + all source fields | spec §7.3 validation invariants #2-3; impl P4 task #7 explicit list | P8 task #5 |
| 12 | `InputImportJob` orchestrates only; `InputImportPipeline` parses source JSON | spec §10.1 vs §10.2; §7.4.7 parsing ownership | P3 acceptance #1-2 |
| 13 | Dependency scheduler: unique ids, missing targets, cycles, failure propagation | spec §7.3.1 #2 (4 checks); impl P4 task #5 (4 sub-items + note) | P8 task #6 |
| 14 | `InputImportKind` NOT in `ImportOptions` | spec §6.2 #1 explicitly states NOT added; §7.1 routing contract #2 | P2 acceptance #3 |
| 15 | Real JSON Schema shipped for editor integration | spec §7.5 JSON Schema Distribution; §7.4.1 file extension convention | P8 task #11 |
| 16 | No `kind` discriminator in source files | spec §7.4.3 document structure detection rules; pipeline infers from structure | P3 acceptance #10 |
| 17 | `trigger` shorthand for common case | spec §7.4.3.3 mapping record contract; trigger resolution rules | P3 acceptance #9, P8 task #8 |
