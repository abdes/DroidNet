# PakTool Implementation Plan

This plan defines the remaining work to ship `Oxygen.Cooker.PakTool` as a
release-grade native Oxygen content-management tool. It follows the corrected
design in `paktool_design.md`, the current `Oxygen/Cooker/Pak` contracts, and
the repository guardrails for evidence-backed completion.

Status convention:

- `pending`: implementation has not started.
- `in_progress`: implementation exists in part, or required validation evidence
  is still missing.
- `completed`: implementation exists, docs are aligned, and validation evidence
  is recorded.

Current overall status: `in_progress`

Rationale:

- The native pak builder pipeline exists.
- The standalone tool target, catalog sidecar IO contract, staged publication
  flow, and tool-level validation are not complete.
- Validation evidence for the tool does not yet exist in this review
  iteration.

## Phase 1. Close Native Pak Contract Gaps

Objective: make the existing pak-domain library fully consumable by a
standalone CLI without duplicating packaging behavior in tool code.

### Task 1.1: Populate `PakBuildResult::output_catalog`

Status: `completed`

Required work:

- Ensure `PakBuilder` returns a fully populated `data::PakCatalog`.
- Derive catalog entries from authoritative build/planning data inside the pak
  domain layer.
- Ensure `catalog_digest` is computed deterministically from canonical catalog
  content.

Implementation notes:

- `PakPlanBuilder::BuildResult` now carries a planner-owned `output_catalog`.
- The planner builds catalog entries from its finalized emitted asset set, after
  patch classification/filtering.
- `PakBuilder` now forwards that authoritative catalog into
  `PakBuildResult::output_catalog`.
- The catalog digest is now computed deterministically from:
  - `content_version`
  - `source_key`
  - emitted catalog entries sorted by `asset_key`

Validation:

- Extend native pak tests to prove `output_catalog` is populated for:
  - full builds
  - patch builds
  - empty builds where the catalog is still structurally valid
- Verify deterministic rebuilds produce identical catalog content.

Validation evidence:

- Built in `out/build-vs` with `/m:6`:
  - `cmake --build 'out/build-vs' --target Oxygen.Cooker.PakDomainValidation.Tests Oxygen.Cooker.PakE2E.Tests --config Debug -- /m:6`
- Executed:
  - `Oxygen.Cooker.PakDomainValidation.Tests`
  - `Oxygen.Cooker.PakE2E.Tests`
- Added/verified coverage for:
  - deterministic planner catalog equivalence
  - full-build catalog population
  - patch-build catalog population
  - empty full-build catalog structural validity

Exit gate:

- Tool code can persist `result.output_catalog` without re-deriving catalog
  state.

### Task 1.2: Confirm Result Semantics For Tool Consumption

Status: `completed`

Required work:

- Audit `PakBuildResult`, `PakBuildSummary`, and `PakBuildTelemetry` for tool
  reporting and exit-code mapping.
- Add missing native tests for:
  - manifest emission presence/absence semantics
  - warning/error propagation
  - `fail_on_warnings`
  - zero-source full builds
  - delete-only patch builds

Implementation notes:

- Removed `planning_duration` from `PakPlanBuilder::BuildResult`.
- Planning time is now measured by `PakBuilder`, where full build telemetry
  ownership belongs.
- Builder API coverage now explicitly verifies:
  - full build without manifest leaves `patch_manifest` empty
  - full build with manifest populates `patch_manifest`
  - patch build populates `patch_manifest`
  - `fail_on_warnings` escalates a real planner warning into an error
    diagnostic without requiring tool-local exit-status inference

Validation:

- Native tests prove the tool can treat `PakBuildResult` as authoritative for:
  - success/failure
  - summary reporting
  - telemetry reporting
  - manifest presence

Validation evidence:

- Built in `out/build-vs` with `/m:6`:
  - `cmake --build 'out/build-vs' --target Oxygen.Cooker.PakDomainValidation.Tests Oxygen.Cooker.PakE2E.Tests --config Debug -- /m:6`
- Executed:
  - `out/build-vs/bin/Debug/Oxygen.Cooker.PakDomainValidation.Tests.exe`
  - `out/build-vs/bin/Debug/Oxygen.Cooker.PakE2E.Tests.exe`
- Added/verified coverage for:
  - full-build manifest absence semantics
  - full-build manifest presence semantics
  - patch-build manifest presence semantics
  - warning-to-error escalation via `fail_on_warnings`
  - zero-source full build
  - delete-only patch build

Exit gate:

- No tool-local recomputation is needed to determine build outcome.

## Phase 2. Establish Library-Owned Catalog Sidecar Contract

Objective: create the reusable persisted catalog contract required for patch
workflows.

### Task 2.1: Add Pak Catalog Schema

Status: `completed`

Required work:

- Add
  `src/Oxygen/Cooker/Pak/Schemas/oxygen.pak-catalog.schema.json`.
- Define canonical encodings for:
  - `source_key`
  - `asset_key`
  - `asset_type`
  - digest fields
- Wire the schema into CMake embedding/install rules alongside other cooker
  schemas.

Implementation notes:

- Added the canonical pak catalog schema at
  `src/Oxygen/Cooker/Pak/Schemas/oxygen.pak-catalog.schema.json`.
- The schema now fixes canonical JSON encodings for:
  - `source_key` as lowercase UUIDv7 text and `asset_key` as canonical
    lowercase asset-key text
  - `asset_type` as the authoritative Oxygen asset-type string set:
    `Material`, `Geometry`, `Scene`, `Script`, `InputAction`,
    `InputMappingContext`, `PhysicsMaterial`, `CollisionShape`,
    `PhysicsScene`
  - digest fields as 64-character lowercase hex strings
- Cooker CMake now:
  - embeds the pak catalog schema into generated
    `Pak/Internal/PakCatalog_schema.h`
  - installs the schema alongside the existing cooker schema set
- Added focused schema tests covering canonical acceptance and representative
  invalid payload rejection.

Corrected scope:

- `SourceKey` is authoritative as RFC 9562 UUIDv7 end to end.
- The schema contract is now closed over canonical lowercase UUIDv7
  `source_key` text rather than generic lowercase UUID text.

Validation:

- Schema file exists in source control.
- CMake embeds and installs the schema.
- Example valid and invalid payloads are covered by tests.

Validation evidence:

- Built in `out/build-vs` with `/m:6`:
  - `cmake --build 'out/build-vs' --target Oxygen.Cooker.PakCatalogSchema.Tests --config Debug -- /m:6`
- Verified generated embedded schema header:
  - `out/build-vs/generated/Oxygen/Cooker/Pak/Internal/PakCatalog_schema.h`
- Executed:
  - `out/build-vs/bin/Debug/Oxygen.Cooker.PakCatalogSchema.Tests.exe`
- Installed the project data component:
  - `cmake --install 'out/build-vs' --config Debug --component Oxygen_data`
- Verified published schema path:
  - `out/install/Debug/schemas/oxygen.pak-catalog.schema.json`
- Re-ran the schema test after tightening `source_key` to canonical UUIDv7:
  - `out/build-vs/bin/Debug/Oxygen.Cooker.PakCatalogSchema.Tests.exe`

Exit gate:

- There is exactly one supported catalog exchange format for patch workflows.

### Task 2.2: Implement `PakCatalogIo`

Status: `completed`

Required work:

- Add pak-domain catalog IO helpers, for example `PakCatalogIo`.
- Implement:
  - serialize `data::PakCatalog` to canonical JSON
  - parse JSON into `data::PakCatalog`
  - validate required fields and field formats
  - preserve deterministic ordering through `ordered_json`
- Expand scope to close the shared runtime contract gap discovered during pak
  implementation:
  - add shared `Uuid` validation/parsing helpers for canonical UUIDv7 text and
    raw bytes
  - update `SourceKey` comments and construction helpers to enforce UUIDv7-only
    semantics without pak-local duplication
  - enforce UUIDv7 validation on runtime binary ingress paths for pak headers
    and loose cooked indexes
  - enforce UUIDv7 validation on cooker/build request ingress paths before
    persisted artifacts are written
- Use native parsing contracts for:
  - `SourceKey` as canonical lowercase UUIDv7 text matching the runtime
    contract
  - `AssetKey` as canonical asset-key text via shared data-domain parsing, not
    pak-local parsing

Validation:

- Unit tests cover:
  - round-trip serialization
  - schema-invalid rejection
  - duplicate entry rejection
  - invalid field format rejection
  - deterministic byte-for-byte output
- Shared tests cover:
  - UUIDv7 byte validation
  - UUIDv7 string parsing rejection for non-v7 values
  - runtime rejection of invalid source identities in pak and loose cooked
    headers

Validation evidence:

- Built in `out/build-vs` with `/m:6`:
  - `cmake --build 'out/build-vs' --target Oxygen.Base.Uuid.Tests Oxygen.Serio.SerioFullCycle.Tests Oxygen.Content.AssetLoader.Tests Oxygen.Content.PakFile.Tests Oxygen.Content.LooseCooked.Tests Oxygen.Cooker.LooseCooked.Tests Oxygen.Cooker.PakCatalogIo.Tests Oxygen.Cooker.PakDomainValidation.Tests --config Debug -- /m:6`
- Executed:
  - `out/build-vs/bin/Debug/Oxygen.Base.Uuid.Tests.exe`
  - `out/build-vs/bin/Debug/Oxygen.Serio.SerioFullCycle.Tests.exe`
  - `out/build-vs/bin/Debug/Oxygen.Content.PakFile.Tests.exe`
  - `out/build-vs/bin/Debug/Oxygen.Content.LooseCooked.Tests.exe`
  - `out/build-vs/bin/Debug/Oxygen.Content.AssetLoader.Tests.exe`
  - `out/build-vs/bin/Debug/Oxygen.Cooker.LooseCooked.Tests.exe`
  - `out/build-vs/bin/Debug/Oxygen.Cooker.PakCatalogIo.Tests.exe`
  - `out/build-vs/bin/Debug/Oxygen.Cooker.PakDomainValidation.Tests.exe`
- Additional implementation evidence:
  - Serio UUID deserialization now validates bytes through `Uuid::FromBytes`
    instead of mutating an already-constructed `Uuid`
  - content test PAK generation normalizes emitted header `source_identity` to
    deterministic UUIDv7 bytes and repairs CRC before runtime validation
  - shared loose-cooked and pak test fixtures now emit UUIDv7 source identities

Exit gate:

- Patch builds can load base catalogs from disk through pak-domain helpers.

## Phase 3. Define Tool-Local Report And Publish Contracts

Objective: close the operational gaps that separate a raw builder wrapper from a
release-grade tool.

### Task 3.1: Add Build Report Schema

Status: `pending`

Required work:

- Add
  `src/Oxygen/Cooker/Tools/PakTool/Schemas/oxygen.pak-build-report.schema.json`.
- Define report fields for:
  - tool identity/version
  - command and request snapshot
  - artifact publication results
  - summary
  - telemetry
  - diagnostics
  - exit code / success

Validation:

- Schema file exists in source control.
- Valid and invalid report payloads are covered by tests.

Exit gate:

- Automation can rely on a versioned report contract instead of scraping
  stdout.

### Task 3.2: Implement Staged Output Publication

Status: `pending`

Required work:

- Design and implement staged sibling paths for:
  - pak output
  - manifest output
  - catalog output
  - optional diagnostics report
- Ensure publish/finalize logic:
  - creates parent directories when allowed,
  - promotes staged artifacts only after build completion,
  - avoids publishing authoritative final catalog/manifest artifacts on build
    failure,
  - performs best-effort cleanup.

Validation:

- Integration tests verify:
  - successful publication path
  - builder-error path
  - publish failure path
  - no misleading final sidecars after failure

Exit gate:

- Artifact publication is operationally safe for CI/release workflows.

### Task 3.3: Implement Structured Build Report Writer

Status: `pending`

Required work:

- Implement `--diagnostics-file` report emission using deterministic JSON.
- Include artifact publication details:
  - final paths
  - staged paths as needed for debugging
  - pak size / crc
  - catalog digest
  - manifest emission state

Validation:

- Unit/integration tests verify report creation, schema conformance, and
  deterministic content.

Exit gate:

- CI can consume one stable report artifact for each tool run.

## Phase 4. Implement `Oxygen.Cooker.PakTool`

Objective: add the standalone native CLI and map it directly onto corrected pak
contracts.

### Task 4.1: Add Tool Target And CMake Wiring

Status: `pending`

Required work:

- Create `src/Oxygen/Cooker/Tools/PakTool`.
- Add subdirectory wiring under `src/Oxygen/Cooker/Tools/CMakeLists.txt`.
- Add executable target, version define, dependencies, and install rules using
  existing Oxygen tool conventions.

Validation:

- The tool target builds successfully in the normal build graph.

Exit gate:

- `Oxygen.Cooker.PakTool --help` runs successfully.

### Task 4.2: Implement CLI Parsing

Status: `pending`

Required work:

- Implement `build` and `patch` commands in `oxygen::clap`.
- Support the approved option surface from the design doc:
  - common source/output/version flags
  - optional full-build manifest output
  - base-catalog options
  - patch compatibility relaxation flags
  - reporting/console flags
- Prefer meaningful flags over awkward bool-valued toggles for defaulted
  behavior.
- Do not add deferred UI flags with no runtime effect.

Validation:

- CLI parsing tests cover valid and invalid argument combinations.

Exit gate:

- Every required `PakBuildRequest` field can be expressed from the CLI.

### Task 4.3: Implement Request Assembly And Input Validation

Status: `pending`

Required work:

- Convert CLI options into `PakBuildRequest`.
- Parse `--source-key` using `Uuid::FromString` and normalize to canonical text
  in reports.
- Load base catalogs using `PakCatalogIo`.
- Validate source and output filesystem preconditions before invoking the
  builder.
- Allocate staged output paths.

Validation:

- Integration tests cover:
  - invalid source-key string
  - unreadable source path
  - invalid base catalog
  - patch command with multiple base catalogs
  - output directory creation/preparation failures

Exit gate:

- The tool fails early and clearly on invalid external inputs.

### Task 4.4: Invoke Builder And Persist Artifacts

Status: `pending`

Required work:

- Call `PakBuilder::Build` with staged builder-facing output paths.
- Persist `result.output_catalog` through `PakCatalogIo`.
- Publish manifest only when requested/emitted.
- Emit the structured build report when requested.
- Finalize/publish staged artifacts in deterministic order.

Validation:

- End-to-end tests verify:
  - full build -> pak + catalog
  - full build with manifest -> pak + catalog + manifest
  - patch build -> pak + catalog + manifest
  - failed build -> no authoritative final catalog/manifest sidecars

Exit gate:

- `PakTool` produces the required artifact set safely for both modes.

### Task 4.5: Exit Status And Console Reporting

Status: `pending`

Required work:

- Map parse/preparation/build/runtime failures to the documented exit codes.
- Emit deterministic diagnostics with phase and code.
- Honor `--quiet` and `--no-color`.
- Report explicit publication outcome in addition to build outcome.

Validation:

- Tool tests verify exit-code behavior for:
  - parse errors
  - preparation/input failures
  - build diagnostics with errors
  - warnings with and without `--fail-on-warnings`
  - publish failures

Exit gate:

- CI can treat process exit status plus report JSON as authoritative.

## Phase 5. Optional Shared Console Abstractions

Objective: reuse shared tooling only when it improves clarity without dragging
in unrelated runtime/UI dependencies.

### Task 5.1: Decide Message Writer Reuse Strategy

Status: `pending`

Required work:

- Evaluate whether the current message-writer abstraction should be:
  - extracted into `Cooker/Tools/Common`, or
  - reimplemented minimally in `PakTool`
- Keep any shared extraction narrow and dependency-clean.

Validation:

- The chosen abstraction compiles cleanly for the consuming tool(s) without
  forcing unrelated TUI/runtime dependencies into `PakTool`.

Exit gate:

- Console/report writing code is maintainable and not duplicated gratuitously.

Note:

- This is not a blocker if a minimal tool-local writer is the cleaner solution
  for this release.

## Phase 6. Verification And Documentation

Objective: close the release gate with recorded evidence.

### Task 6.1: Automated Validation

Status: `pending`

Required work:

- Run affected native pak tests.
- Run new catalog IO tests.
- Run new tool-level CLI/integration/report tests.
- Record the exact commands and results.

Validation evidence required:

- Passing outputs for native pak tests affected by the change.
- Passing outputs for new `PakTool` tests.

Exit gate:

- No completion claim without executed automated validation.

### Task 6.2: Manual End-To-End Validation

Status: `pending`

Required work:

1. Build a representative full pak with `PakTool build`.
2. Inspect it with `Oxygen.Cooker.PakDump`.
3. Build a representative patch pak with `PakTool patch`.
4. Confirm the generated catalog and manifest are reusable inputs.
5. Exercise at least one failure path and confirm final sidecars are not
   misleadingly published.

Validation evidence required:

- Exact command lines used.
- Resulting artifact paths.
- Confirmation that `PakDump` inspection passed without format anomalies.
- Confirmation that failure-path publish semantics behaved as designed.

Exit gate:

- Manual validation evidence is captured in task/PR notes.

### Task 6.3: Documentation Completion

Status: `pending`

Required work:

- Keep `paktool_design.md` and `paktool_impl_plan.md` aligned with the final
  implementation.
- Add `PakTool` README/help examples after implementation.
- Document any approved deferrals explicitly.

Validation:

- No mismatch remains between docs and shipped command/options/artifacts.

Exit gate:

- Documentation matches actual behavior and release scope.

## Phase Summary

Release blockers:

1. The tool target does not yet exist.
2. Staged publication behavior does not yet exist.
3. Tool-level validation evidence does not yet exist.

Non-blocking follow-ups after release:

- richer terminal UI
- extra machine-readable outputs beyond the build report
- additional command modes such as `plan-only`

## Validation Status

Implementation status: `in_progress`

Validation executed in this review iteration:

- `cmake --build 'out/build-vs' --target Oxygen.Base.Uuid.Tests Oxygen.Serio.SerioFullCycle.Tests Oxygen.Content.AssetLoader.Tests Oxygen.Content.PakFile.Tests Oxygen.Content.LooseCooked.Tests Oxygen.Cooker.LooseCooked.Tests Oxygen.Cooker.PakCatalogSchema.Tests Oxygen.Cooker.PakCatalogIo.Tests Oxygen.Cooker.PakDomainValidation.Tests --config Debug -- /m:6`
- `out/build-vs/bin/Debug/Oxygen.Base.Uuid.Tests.exe`
- `out/build-vs/bin/Debug/Oxygen.Serio.SerioFullCycle.Tests.exe`
- `out/build-vs/bin/Debug/Oxygen.Content.PakFile.Tests.exe`
- `out/build-vs/bin/Debug/Oxygen.Content.LooseCooked.Tests.exe`
- `out/build-vs/bin/Debug/Oxygen.Content.AssetLoader.Tests.exe`
- `out/build-vs/bin/Debug/Oxygen.Cooker.LooseCooked.Tests.exe`
- `out/build-vs/bin/Debug/Oxygen.Cooker.PakCatalogSchema.Tests.exe`
- `out/build-vs/bin/Debug/Oxygen.Cooker.PakCatalogIo.Tests.exe`
- `out/build-vs/bin/Debug/Oxygen.Cooker.PakDomainValidation.Tests.exe`

Remaining validation delta:

- Tool-level CLI/integration/report tests
- Manual end-to-end runs with `PakDump`
