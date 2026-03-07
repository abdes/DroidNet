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

Status: `completed`

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

Evidence:

- Added report schema:
  `src/Oxygen/Cooker/Tools/PakTool/Schemas/oxygen.pak-build-report.schema.json`
- Added schema validation tests:
  `src/Oxygen/Cooker/Tools/PakTool/Test/PakBuildReportJsonSchema_test.cpp`
- Added test target wiring:
  `src/Oxygen/Cooker/Tools/PakTool/Test/CMakeLists.txt`
- Validation executed:
  - `cmake --build 'out/build-vs' --target Oxygen.Cooker.PakBuildReportSchema.Tests --config Debug -- /m:6`
  - `out/build-vs/bin/Debug/Oxygen.Cooker.PakBuildReportSchema.Tests.exe`

Exit gate:

- Automation can rely on a versioned report contract instead of scraping
  stdout.

### Task 3.2: Implement Staged Output Publication

Status: `completed`

Required work:

- Design and implement staged sibling paths for:
  - pak output
  - manifest output
  - catalog output
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

Evidence:

- Added tool-local publication helper:
  `src/Oxygen/Cooker/Tools/PakTool/ArtifactPublication.h`
  `src/Oxygen/Cooker/Tools/PakTool/ArtifactPublication.cpp`
- Added publication tests:
  `src/Oxygen/Cooker/Tools/PakTool/Test/PakToolArtifactPublication_test.cpp`
- Added test target wiring:
  `src/Oxygen/Cooker/Tools/PakTool/Test/CMakeLists.txt`
- Validation executed:
  - `cmake --build 'out/build-vs' --target Oxygen.Cooker.PakToolPublication.Tests --config Debug -- /m:6`
  - `out/build-vs/bin/Debug/Oxygen.Cooker.PakToolPublication.Tests.exe`

Exit gate:

- Artifact publication is operationally safe for CI/release workflows.

### Task 3.3: Implement Structured Build Report Writer

Status: `completed`

Required work:

- Implement `--diagnostics-file` report emission using deterministic JSON.
- Include artifact publication details:
  - final paths
  - staged paths for authoritative artifacts as needed for debugging
  - pak size / crc
  - catalog digest
  - manifest emission state

Validation:

- Unit/integration tests verify report creation, schema conformance, and
  deterministic content.

Evidence:

- Added tool-local report writer:
  `src/Oxygen/Cooker/Tools/PakTool/BuildReportJson.h`
  `src/Oxygen/Cooker/Tools/PakTool/BuildReportJson.cpp`
- Added report writer tests:
  `src/Oxygen/Cooker/Tools/PakTool/Test/PakToolBuildReportJson_test.cpp`
- Added test target wiring:
  `src/Oxygen/Cooker/Tools/PakTool/Test/CMakeLists.txt`
- Validation executed:
  - `cmake --build 'out/build-vs' --target Oxygen.Cooker.PakToolReport.Tests --config Debug -- /m:6`
  - `out/build-vs/bin/Debug/Oxygen.Cooker.PakToolReport.Tests.exe`

Exit gate:

- CI can consume one stable report artifact for each tool run.

## Phase 4. Implement `Oxygen.Cooker.PakTool`

Objective: add the standalone native CLI and map it directly onto corrected pak
contracts.

Corrected execution scope:

- The authoritative staged publication set is `pak`, `catalog`, and optional
  `manifest`.
- The optional diagnostics/build report is emitted after publication outcome is
  known because it reports that outcome; it is not part of the same
  transactional publish set.

### Task 4.1: Add Tool Target And CMake Wiring

Status: `completed`

Required work:

- Create `src/Oxygen/Cooker/Tools/PakTool`.
- Add subdirectory wiring under `src/Oxygen/Cooker/Tools/CMakeLists.txt`.
- Add executable target, version define, dependencies, and install rules using
  existing Oxygen tool conventions.
- Embed the tool-local build report schema into the `PakTool` target itself.

Validation:

- The tool target builds successfully in the normal build graph.

Implementation notes:

- `src/Oxygen/Cooker/Tools/PakTool/CMakeLists.txt` now follows the same
  section ordering and formatting style as the other cooker tools.
- The tool-local build report schema is now embedded into the
  `Oxygen.Cooker.PakTool` target itself via `oxygen_embed_json_schemas(...)`.
- The tool-local report schema is installed from the tool module, not from the
  pak domain module.
- `PakTool` tests now live under `src/Oxygen/Cooker/Tools/PakTool/Test` and
  are declared from the tool module instead of the pak-domain test tree.
- The emitted report `$schema` reference now points at the canonical Oxygen
  schema identifier rather than a source-tree-relative path.

Validation evidence:

- Built in `out/build-vs` with `/m:6`:
  - `cmake --build 'out/build-vs' --target Oxygen.Cooker.PakTool Oxygen.Cooker.PakBuildReportSchema.Tests Oxygen.Cooker.PakToolPublication.Tests Oxygen.Cooker.PakToolReport.Tests --config Debug -- /m:6`
- Executed:
  - `out/build-vs/bin/Debug/Oxygen.Cooker.PakTool.exe --help`
  - `out/build-vs/bin/Debug/Oxygen.Cooker.PakBuildReportSchema.Tests.exe`
  - `out/build-vs/bin/Debug/Oxygen.Cooker.PakToolPublication.Tests.exe`
  - `out/build-vs/bin/Debug/Oxygen.Cooker.PakToolReport.Tests.exe`

Exit gate:

- `Oxygen.Cooker.PakTool --help` runs successfully.

### Task 4.2: Implement CLI Parsing

Status: `completed`

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

Implementation notes:

- Added typed tool-local CLI state in
  `src/Oxygen/Cooker/Tools/PakTool/PakToolOptions.h` so later request assembly
  consumes structured parser output instead of scraping raw parser state.
- Repeatable source options now preserve the authoritative CLI source order in a
  single `std::vector<data::CookedSource>` rather than splitting loose and pak
  inputs into separate arrays.
- `src/Oxygen/Cooker/Tools/PakTool/CliBuilder.cpp` now defines the full
  approved command surface for `build` and `patch`, including:
  - shared tool options
  - shared request options
  - full-build manifest opt-in
  - patch-only base catalog and compatibility relaxation flags
- Added dedicated parser coverage in
  `src/Oxygen/Cooker/Tools/PakTool/Test/PakToolCli_test.cpp` for:
  - full-build parsing with repeatable sources and shared flags
  - patch parsing with repeatable base catalogs and relaxation flags
  - missing required option rejection
  - patch-only option rejection on `build`

Validation evidence:

- Built in `out/build-vs` with `/m:6`:
  - `cmake --build 'out/build-vs' --target Oxygen.Cooker.PakTool Oxygen.Cooker.PakToolCli.Tests --config Debug -- /m:6`
- Executed:
  - `out/build-vs/bin/Debug/Oxygen.Cooker.PakToolCli.Tests.exe`
  - `out/build-vs/bin/Debug/Oxygen.Cooker.PakTool.exe build --help`

Exit gate:

- Every required `PakBuildRequest` field can be expressed from the CLI.

### Task 4.3: Implement Request Assembly And Input Validation

Status: `completed`

Required work:

- Convert CLI options into `PakBuildRequest`.
- Parse `--source-key` through the runtime-owned UUIDv7 path and normalize to
  canonical text in reports.
- Load base catalogs using `PakCatalogIo`.
- Validate source and output filesystem preconditions before invoking the
  builder.
- Allocate staged output paths.

Implementation notes:

- Added tool-local request assembly helpers in:
  - `src/Oxygen/Cooker/Tools/PakTool/RequestPreparation.h`
  - `src/Oxygen/Cooker/Tools/PakTool/RequestPreparation.cpp`
- Added a reusable request snapshot contract in:
  - `src/Oxygen/Cooker/Tools/PakTool/RequestSnapshot.h`
- `PreparePakToolRequest(...)` now:
  - validates ordered cooked source inputs against their declared kind
  - validates final artifact file paths and creates required parent
    directories before the builder runs
  - parses `source_key` via `data::SourceKey::FromString`, which delegates to
    the shared `Uuid::FromString` UUIDv7 parser
  - loads patch base catalogs through `PakCatalogIo::Read`
  - allocates staged builder-facing output paths through
    `MakeArtifactPublicationPlan(...)`
  - maps patch compatibility relaxation flags into the authoritative
    `PakBuildRequest::patch_compat` contract
  - preserves ordered source inputs in both the builder request and the
    report-side request snapshot

Validation evidence:

- Built in `out/build-vs` with `/m:6`:
  - `cmake --build 'out/build-vs' --target Oxygen.Cooker.PakTool Oxygen.Cooker.PakToolCli.Tests Oxygen.Cooker.PakToolRequestPreparation.Tests --config Debug -- /m:6`
- Executed:
  - `out/build-vs/bin/Debug/Oxygen.Cooker.PakToolCli.Tests.exe`
  - `out/build-vs/bin/Debug/Oxygen.Cooker.PakToolRequestPreparation.Tests.exe`
  - `out/build-vs/bin/Debug/Oxygen.Cooker.PakTool.exe build --help`
- Added/verified coverage for:
  - full-build request assembly with preserved ordered source inputs
  - invalid UUIDv7 source-key rejection
  - missing source path rejection
  - invalid base catalog rejection
  - patch request assembly from multiple base catalogs
  - output parent directory creation failure

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

Status: `in_progress`

Required work:

- Seal loose-cooked external script assets into staged embedded-source script
  assets before invoking `PakBuilder`.
- Call `PakBuilder::Build` with staged builder-facing output paths.
- Persist `result.output_catalog` through `PakCatalogIo`.
- Publish manifest only when requested/emitted.
- Emit the structured build report when requested after publication outcome is
  known.
- Finalize/publish staged artifacts in deterministic order.

Implementation notes:

- Corrected scope:
  - `PakTool` cannot assume loose-cooked inputs are already PAK-sealed.
  - For loose-cooked script assets, PakTool must stage a narrow sealing pass
    that rewrites `kAllowExternalSource` descriptors into embedded-source
    staged descriptors without mutating the input cooked root.
  - The sealing pass must preserve existing embedded source/bytecode payloads,
    must never compile, and must never invent bytecode.
  - Patch planning and catalog generation must observe the sealed staged roots,
    not the raw loose-cooked descriptor form.
- Added the tool-local execution seam in:
  - `src/Oxygen/Cooker/Tools/PakTool/CommandExecution.h`
  - `src/Oxygen/Cooker/Tools/PakTool/CommandExecution.cpp`
- `main.cpp` now dispatches `build` and `patch` through
  `ExecutePakToolCommand(...)` instead of stopping at parse-only behavior.
- The execution flow now:
  - prepares a staged `PakBuildRequest`
  - must prepare staged sealed loose-cooked roots when script sealing is
    required
  - invokes `PakBuilder`
  - persists the staged canonical catalog through `PakCatalogIo::Write`
  - publishes staged `pak` / `catalog` / optional `manifest` deterministically
  - suppresses authoritative final catalog/manifest outputs on build failure
  - emits the optional diagnostics report after publication outcome is known
- Integrated end-to-end coverage now lives in:
  - `src/Oxygen/Cooker/Tools/PakTool/Test/PakToolCommandExecution_test.cpp`

Validation:

- End-to-end tests verify:
  - full build -> pak + catalog
  - full build with manifest -> pak + catalog + manifest
  - patch build -> pak + catalog + manifest
  - external-source loose-cooked script assets are sealed into embedded-source
    staged inputs before packaging
  - failed build -> no authoritative final catalog/manifest sidecars

Validation evidence:

- Built in `out/build-vs` with `/m:6`:
  - `cmake --build 'out/build-vs' --target Oxygen.Cooker.PakTool Oxygen.Cooker.PakToolExecution.Tests --config Debug -- /m:6`
- Executed:
  - `out/build-vs/bin/Debug/Oxygen.Cooker.PakToolExecution.Tests.exe`
  - `out/build-vs/bin/Debug/Oxygen.Cooker.PakTool.exe build --help`
- Added/verified coverage for:
  - full build publishing `pak` + catalog + optional report
  - full build with manifest publishing `pak` + catalog + manifest
  - patch build publishing `pak` + catalog + manifest
  - build-failure suppression of authoritative final catalog/manifest sidecars

Validation gap:

- Staged script sealing for loose-cooked external script assets is not yet
  implemented or validated in the current evidence above.

Exit gate:

- `PakTool` produces the required artifact set safely for both modes and seals
  loose-cooked external script assets into embedded-source staged inputs before
  packaging.

### Task 4.5: Exit Status And Console Reporting

Status: `completed`

Required work:

- Map parse/preparation/build/runtime failures to the documented exit codes.
- Emit deterministic diagnostics with phase and code.
- Honor `--quiet` and `--no-color`.
- Report explicit publication outcome in addition to build outcome.

Implementation notes:

- Added the tool-local app/console seam in:
  - `src/Oxygen/Cooker/Tools/PakTool/App.h`
  - `src/Oxygen/Cooker/Tools/PakTool/App.cpp`
- `main.cpp` now delegates to `RunPakToolApp(...)`, which owns:
  - CLI parse error mapping to exit code `1`
  - preparation failure mapping to exit code `2`
  - build-diagnostic failure mapping to exit code `3`
  - publish/report/unhandled runtime failure mapping to exit code `4`
- The tool-local console writer now emits deterministic:
  - command/input summaries
  - warning/error diagnostics with phase and code
  - summary counters
  - timing summaries
  - explicit publication outcome
  - final result/exit-status lines
- Developer-facing troubleshooting logs are emitted separately through the
  engine logging system:
  - lifecycle events at `INFO`
  - warnings at `WARNING`
  - errors at `ERROR`
- `--quiet` now suppresses non-error output and `--no-color` removes ANSI
  color sequences from the tool-local message surface.
- Added app-level process-contract coverage in:
  - `src/Oxygen/Cooker/Tools/PakTool/Test/PakToolApp_test.cpp`

Validation:

- Tool tests verify exit-code behavior for:
  - parse errors
  - preparation/input failures
  - build diagnostics with errors
  - warnings with and without `--fail-on-warnings`
  - publish failures

Validation evidence:

- Reconfigured `out/build-vs` after the new app/test targets were added:
  - `cmake -S . -B 'out/build-vs'`
- Built in `out/build-vs` with `/m:6`:
  - `cmake --build 'out/build-vs' --target Oxygen.Cooker.PakTool Oxygen.Cooker.PakToolApp.Tests Oxygen.Cooker.PakToolExecution.Tests --config Debug -- /m:6`
- Executed:
  - `out/build-vs/bin/Debug/Oxygen.Cooker.PakToolApp.Tests.exe`
  - `out/build-vs/bin/Debug/Oxygen.Cooker.PakToolExecution.Tests.exe`
  - `out/build-vs/bin/Debug/Oxygen.Cooker.PakTool.exe build --help`
- Added/verified coverage for:
  - parse failure exit/status reporting
  - preparation/input failure exit/status reporting
  - warning-only success behavior
  - warning escalation to build failure via `--fail-on-warnings`
  - `--quiet` suppression of non-error console output
  - `--no-color` removal of ANSI sequences
  - publish failure exit/status reporting

Exit gate:

- CI can treat process exit status plus report JSON as authoritative.

## Phase 5. Verification And Documentation

Objective: close the release gate with recorded evidence.

### Task 5.1: Automated Validation

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

### Task 5.2: Manual End-To-End Validation

Status: `completed`

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

Evidence recorded so far:

- Scenario 1: isolated full-build validation using `Examples/Content/scenes/cubes`.
- Isolated validation workspace:
  `out/build-vs/manual/paktool-validation/cubes-full`
- Exact commands used:
  - `Examples/Content/cook_scenes.ps1 -Scene cubes -NoTUI -ToolPath 'H:/projects/DroidNet/projects/Oxygen.Engine/out/build-vs/bin/Debug/Oxygen.Cooker.ImportTool.exe'`
    This was first used only to confirm the scene imports cleanly from the repo
    tree.
  - Isolated import setup:
    - copied `Examples/Content/scenes/cubes/*` to
      `out/build-vs/manual/paktool-validation/cubes-full/scene`
    - rewrote the copied `import-manifest.json` output field to
      `H:/projects/DroidNet/projects/Oxygen.Engine/out/build-vs/manual/paktool-validation/cubes-full/cooked`
  - `out/build-vs/bin/Debug/Oxygen.Cooker.ImportTool.exe batch --manifest out/build-vs/manual/paktool-validation/cubes-full/scene/import-manifest.json --no-tui`
  - `out/build-vs/bin/Debug/Oxygen.Cooker.PakTool.exe build --no-color --diagnostics-file out/build-vs/manual/paktool-validation/cubes-full/cubes-full.diagnostics.json --loose-source out/build-vs/manual/paktool-validation/cubes-full/cooked --out out/build-vs/manual/paktool-validation/cubes-full/cubes-full.pak --catalog-out out/build-vs/manual/paktool-validation/cubes-full/cubes-full.catalog.json --manifest-out out/build-vs/manual/paktool-validation/cubes-full/cubes-full.manifest.json --content-version 1 --source-key 01234567-89ab-7def-8123-456789abcdef`
  - `out/build-vs/bin/Debug/Oxygen.Cooker.PakDump.exe out/build-vs/manual/paktool-validation/cubes-full/cubes-full.pak`
- Resulting artifact paths:
  - `out/build-vs/manual/paktool-validation/cubes-full/cooked/container.index.bin`
  - `out/build-vs/manual/paktool-validation/cubes-full/cubes-full.pak`
  - `out/build-vs/manual/paktool-validation/cubes-full/cubes-full.catalog.json`
  - `out/build-vs/manual/paktool-validation/cubes-full/cubes-full.manifest.json`
  - `out/build-vs/manual/paktool-validation/cubes-full/cubes-full.diagnostics.json`
- Observed results:
  - isolated import succeeded with exit code `0`
  - isolated cooked output contained `13` filesystem entries including
    `container.index.bin`
  - full pak build succeeded with exit code `0`
  - published artifacts were present for `pak`, `catalog`, `manifest`, and
    `diagnostics`
  - resulting pak size was `9008` bytes
  - catalog digest was
    `15e7eda4b21511b60eb3c8368f9654aa935a8f0f93eda65437eb3a4451855df6`
- `PakDump` inspection passed without format anomalies:
  - exit code `0`
  - footer magic reported `OK`
  - footer asset count `9`
  - directory asset count `9`
  - CRC32 reported as `0xd4e81145`
- Scenario 2: isolated patch-build validation using a modified `cubes` scene and
  the published full-build catalog as the base input.
- Isolated validation workspaces:
  - `out/build-vs/manual/paktool-validation/cubes-patch-1`
  - `out/build-vs/manual/paktool-validation/cubes-patch-2`
- Exact commands used:
  - copied `Examples/Content/scenes/cubes/*` to
    `out/build-vs/manual/paktool-validation/cubes-patch-1/scene`
  - rewrote the copied `import-manifest.json` output field to
    `H:/projects/DroidNet/projects/Oxygen.Engine/out/build-vs/manual/paktool-validation/cubes-patch-1/cooked`
  - modified `MatCube.material.json` to change `base_color` and `roughness`
  - `out/build-vs/bin/Debug/Oxygen.Cooker.ImportTool.exe batch --manifest out/build-vs/manual/paktool-validation/cubes-patch-1/scene/import-manifest.json --no-tui`
  - `out/build-vs/bin/Debug/Oxygen.Cooker.PakTool.exe patch --no-color --diagnostics-file out/build-vs/manual/paktool-validation/cubes-patch-1/cubes-patch-1.diagnostics.json --loose-source out/build-vs/manual/paktool-validation/cubes-patch-1/cooked --base-catalog out/build-vs/manual/paktool-validation/cubes-full/cubes-full.catalog.json --out out/build-vs/manual/paktool-validation/cubes-patch-1/cubes-patch-1.pak --catalog-out out/build-vs/manual/paktool-validation/cubes-patch-1/cubes-patch-1.catalog.json --manifest-out out/build-vs/manual/paktool-validation/cubes-patch-1/cubes-patch-1.manifest.json --content-version 1 --source-key 01234567-89ab-7def-8123-456789abcdef`
  - `out/build-vs/bin/Debug/Oxygen.Cooker.PakDump.exe out/build-vs/manual/paktool-validation/cubes-patch-1/cubes-patch-1.pak`
  - copied `out/build-vs/manual/paktool-validation/cubes-patch-1/scene/*` to
    `out/build-vs/manual/paktool-validation/cubes-patch-2/scene`
  - rewrote the copied `import-manifest.json` output field to
    `H:/projects/DroidNet/projects/Oxygen.Engine/out/build-vs/manual/paktool-validation/cubes-patch-2/cooked`
  - modified `MatCubeB.material.json` to change `base_color` and `roughness`
  - `out/build-vs/bin/Debug/Oxygen.Cooker.ImportTool.exe batch --manifest out/build-vs/manual/paktool-validation/cubes-patch-2/scene/import-manifest.json --no-tui`
  - `out/build-vs/bin/Debug/Oxygen.Cooker.PakTool.exe patch --no-color --diagnostics-file out/build-vs/manual/paktool-validation/cubes-patch-2/cubes-patch-2.diagnostics.json --loose-source out/build-vs/manual/paktool-validation/cubes-patch-2/cooked --base-catalog out/build-vs/manual/paktool-validation/cubes-patch-1/cubes-patch-1.catalog.json --out out/build-vs/manual/paktool-validation/cubes-patch-2/cubes-patch-2.pak --catalog-out out/build-vs/manual/paktool-validation/cubes-patch-2/cubes-patch-2.catalog.json --manifest-out out/build-vs/manual/paktool-validation/cubes-patch-2/cubes-patch-2.manifest.json --content-version 1 --source-key 01234567-89ab-7def-8123-456789abcdef`
- Resulting artifact paths:
  - `out/build-vs/manual/paktool-validation/cubes-patch-1/cooked/container.index.bin`
  - `out/build-vs/manual/paktool-validation/cubes-patch-1/cubes-patch-1.pak`
  - `out/build-vs/manual/paktool-validation/cubes-patch-1/cubes-patch-1.catalog.json`
  - `out/build-vs/manual/paktool-validation/cubes-patch-1/cubes-patch-1.manifest.json`
  - `out/build-vs/manual/paktool-validation/cubes-patch-1/cubes-patch-1.diagnostics.json`
  - `out/build-vs/manual/paktool-validation/cubes-patch-2/cooked/container.index.bin`
  - `out/build-vs/manual/paktool-validation/cubes-patch-2/cubes-patch-2.pak`
  - `out/build-vs/manual/paktool-validation/cubes-patch-2/cubes-patch-2.catalog.json`
  - `out/build-vs/manual/paktool-validation/cubes-patch-2/cubes-patch-2.manifest.json`
  - `out/build-vs/manual/paktool-validation/cubes-patch-2/cubes-patch-2.diagnostics.json`
- Observed results:
  - both isolated patch import runs succeeded with exit code `0`
  - both isolated cooked outputs contained `13` filesystem entries including
    `container.index.bin`
  - first patch build succeeded with exit code `0`
  - first patch published `pak`, `catalog`, `manifest`, and `diagnostics`
    outputs
  - first patch pak size was `1776` bytes
  - first patch summary was `patch_replaced=1`, `patch_unchanged=8`
  - `PakDump` inspection of the first patch pak passed with:
    - exit code `0`
    - footer magic `OK`
    - footer asset count `1`
    - directory asset count `1`
    - CRC32 `0x78c59261`
  - second patch build succeeded with exit code `0` using
    `cubes-patch-1.catalog.json` as the sole `--base-catalog` input
  - second patch published `pak`, `catalog`, `manifest`, and `diagnostics`
    outputs
  - second patch pak size was `7744` bytes
  - second patch summary was `patch_created=8`, `patch_unchanged=1`
- Catalog and manifest reuse confirmation:
  - `cubes-patch-1.catalog.json` was accepted as a valid `--base-catalog`
    input by a subsequent `PakTool patch` run
  - `cubes-patch-1.manifest.json` recorded the full-build catalog digest as its
    required base catalog digest:
    `15e7eda4b21511b60eb3c8368f9654aa935a8f0f93eda65437eb3a4451855df6`
  - `cubes-patch-2.manifest.json` recorded the first patch catalog digest as
    its required base catalog digest:
    `793512d9fbeaec463f1b9dea858acc5253de284d7bac7862cd95c615b239aa16`
- `cubes-patch-2.catalog.json` was emitted successfully with catalog digest:
    `afdebe5424dc72a3484896a3c61708b2fb011b0cfa6640ab3b584c30e2e8540f`
- Scenario 3: failure-path publication validation after successful request
  preparation.
- Isolated validation workspace:
  `out/build-vs/manual/paktool-validation/cubes-failure`
- Exact command used:
  - pre-created stale final outputs:
    - `out/build-vs/manual/paktool-validation/cubes-failure/warning_fail.pak`
      with content `pak-old`
    - `out/build-vs/manual/paktool-validation/cubes-failure/warning_fail.catalog.json`
      with content `catalog-old`
    - `out/build-vs/manual/paktool-validation/cubes-failure/warning_fail.manifest.json`
      with content `manifest-old`
  - `out/build-vs/bin/Debug/Oxygen.Cooker.PakTool.exe build --no-color --diagnostics-file out/build-vs/manual/paktool-validation/cubes-failure/warning_fail.report.json --pak-source out/build-vs/manual/paktool-validation/cubes-full/cubes-full.pak --out out/build-vs/manual/paktool-validation/cubes-failure/warning_fail.pak --catalog-out out/build-vs/manual/paktool-validation/cubes-failure/warning_fail.catalog.json --manifest-out out/build-vs/manual/paktool-validation/cubes-failure/warning_fail.manifest.json --content-version 1 --source-key 01234567-89ab-7def-8123-456789abcdef --fail-on-warnings`
- Observed results:
  - command exited with code `3`
  - builder warning emitted:
    `pak.plan.pak_source_regions_projected`
  - build-failure error emitted:
    `pak.request.fail_on_warnings`
  - stale final pak remained in place with original content `pak-old`
  - stale final catalog was removed and not replaced
  - stale final manifest was removed and not replaced
  - diagnostics report was still written
  - console publication summary reported:
    `pak=skipped catalog=suppressed manifest=suppressed report=written`
  - diagnostics report recorded:
    - `artifacts.pak.published = false`
    - `artifacts.catalog.published = false`
    - `artifacts.manifest.requested = true`
    - `artifacts.manifest.published = false`

Manual validation conclusion:

- Full build publication path is verified.
- Patch build publication path is verified.
- Published catalog outputs are reusable as subsequent patch base inputs.
- Published manifests capture the expected base catalog compatibility envelope.
- Failure-path publish semantics behave as designed: authoritative final
  catalog/manifest outputs are not published on build failure.

Exit gate:

- Manual validation evidence is captured in task/PR notes.

### Task 5.3: Documentation Completion

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

1. Manual end-to-end tool validation evidence does not yet exist.
2. Final documentation completion work remains pending.

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
- `cmake --build 'out/build-vs' --target Oxygen.Cooker.PakTool Oxygen.Cooker.PakBuildReportSchema.Tests Oxygen.Cooker.PakToolPublication.Tests Oxygen.Cooker.PakToolReport.Tests --config Debug -- /m:6`
- `out/build-vs/bin/Debug/Oxygen.Cooker.PakTool.exe --help`
- `cmake --build 'out/build-vs' --target Oxygen.Cooker.PakTool Oxygen.Cooker.PakToolCli.Tests --config Debug -- /m:6`
- `out/build-vs/bin/Debug/Oxygen.Cooker.PakToolCli.Tests.exe`
- `out/build-vs/bin/Debug/Oxygen.Cooker.PakTool.exe build --help`
- `cmake --build 'out/build-vs' --target Oxygen.Cooker.PakTool Oxygen.Cooker.PakToolCli.Tests Oxygen.Cooker.PakToolRequestPreparation.Tests --config Debug -- /m:6`
- `out/build-vs/bin/Debug/Oxygen.Cooker.PakToolRequestPreparation.Tests.exe`
- `cmake --build 'out/build-vs' --target Oxygen.Cooker.PakTool Oxygen.Cooker.PakToolExecution.Tests --config Debug -- /m:6`
- `out/build-vs/bin/Debug/Oxygen.Cooker.PakToolExecution.Tests.exe`
- `cmake --build 'out/build-vs' --target Oxygen.Cooker.PakTool Oxygen.Cooker.PakToolApp.Tests Oxygen.Cooker.PakToolExecution.Tests --config Debug -- /m:6`
- `out/build-vs/bin/Debug/Oxygen.Cooker.PakToolApp.Tests.exe`
- `out/build-vs/bin/Debug/Oxygen.Cooker.PakBuildReportSchema.Tests.exe`
- `out/build-vs/bin/Debug/Oxygen.Cooker.PakToolPublication.Tests.exe`
- `out/build-vs/bin/Debug/Oxygen.Cooker.PakToolReport.Tests.exe`

Remaining validation delta:

- Manual end-to-end runs with `PakDump`
