# PakTool Technical Design

This document defines the release-target design for
`src/Oxygen/Cooker/Tools/PakTool`, the native Oxygen CLI for building
`.pak` archives from cooked content and for emitting the metadata required to
manage those archives across build, patch, and publishing workflows.

The design is intentionally anchored to the existing native pak pipeline in
`Oxygen/Cooker/Pak`. `PakTool` is a productization layer over that pipeline. It
must complete the current native contracts, expose them safely through a CLI,
and emit durable metadata artifacts that fit the rest of the Oxygen content
toolset.

## 1. Purpose

`PakTool` must provide one authoritative native entry point for:

- full pak builds from cooked sources,
- patch pak builds from cooked sources plus persisted base catalogs,
- deterministic diagnostics and exit behavior for CI,
- durable sidecar artifacts for later patch, audit, and publishing stages.

The tool must not introduce a second packaging algorithm, a second patch
planner, or a second metadata model. Its job is to:

1. parse tool inputs,
2. load/validate external metadata,
3. call `oxygen::content::pak::PakBuilder`,
4. stage and publish artifacts safely,
5. report results to humans and automation.

## 2. Repository Baseline

The repository already contains the native packaging pipeline that the tool must
reuse directly:

- `PakBuilder`
- `PakPlanBuilder`
- `PakValidation`
- `PakWriter`
- `PakManifestWriter`
- `PakBuildRequest`
- `PakBuildResult`
- `data::PakCatalog`

Current request/result contracts:

- `PakBuildRequest` already models the required build inputs:
  - `BuildMode`
  - `std::vector<data::CookedSource>`
  - output pak/manifest paths
  - `content_version`
  - `source_key`
  - `base_catalogs`
  - `PatchCompatibilityPolicy`
  - `PakBuildOptions`
- `PakBuildResult` already exposes:
  - `output_catalog`
  - optional `patch_manifest`
  - `file_size`
  - `pak_crc32`
  - diagnostics
  - summary
  - telemetry

Current format baseline:

- The pak binary format remains version `7` in
  `src/Oxygen/Data/PakFormat_core.h`.

Design consequence:

- `PakTool` must wrap the current builder contracts faithfully.
- Any contract gap that prevents a complete tool workflow must be fixed in the
  native pak library first.
- Any new persisted interchange artifact must be versioned, schema-backed, and
  placed under the correct domain ownership.

## 3. Scope

### 3.1 In Scope

- A native CLI application named `Oxygen.Cooker.PakTool`.
- `build` and `patch` commands mapped directly to `BuildMode`.
- CLI parsing into `PakBuildRequest`.
- Loading persisted pak catalog sidecars for patch mode.
- Persisting output catalog sidecars for both build modes.
- Persisting manifest outputs through the existing native manifest writer path.
- Emitting a structured build report file for automation when requested.
- Deterministic console diagnostics and stable exit codes.
- Safe output staging/publish semantics for final artifacts.
- Automated coverage for CLI parsing, catalog IO, report IO, and tool
  end-to-end behavior.

### 3.2 Out of Scope For This Release

- Pak binary format changes.
- New chunking/compression/container features.
- Replacing the existing plan/writer pipeline with a different execution model.
- GUI/editor integration.
- Rich terminal UI.
- Additional command modes such as `plan-only` or `validate-only`.

Rich TUI support is explicitly deferred. This release must not expose UI flags
that have no runtime effect.

## 4. Non-Negotiable Fit Requirements

`PakTool` is only acceptable as an Oxygen-grade content-management tool if all
of the following are true:

1. There is one native packaging pipeline, owned by `Oxygen/Cooker/Pak`.
2. The persisted catalog contract is library-owned, not tool-local.
3. New JSON artifacts are schema-backed, versioned, and deterministically
   serialized.
4. Build outputs are staged before publication so failed builds do not publish
   authoritative partial metadata.
5. Tool contracts align with existing Oxygen tooling conventions:
   - `oxygen::clap` command model
   - CMake module wiring
   - installable schemas
   - deterministic diagnostics/reporting
6. No dead CLI surface is introduced for hypothetical future features.

## 5. Architecture Principles

### 5.1 Single Native Packaging Pipeline

All real build work must flow through `PakBuilder`. Tool code may:

- parse CLI options,
- load sidecars,
- validate filesystem inputs,
- stage/publish outputs,
- format diagnostics and reports.

Tool code must not duplicate:

- patch classification,
- planning,
- low-level pak writing,
- manifest generation,
- digest computation already owned by pak domain code.

### 5.2 Domain Ownership First

Artifact contracts belong where their semantics belong:

- `data::PakCatalog` sidecar IO belongs in `Oxygen/Cooker/Pak` because it is a
  reusable content-management contract used by patch planning.
- `PakTool` structured build-report emission belongs in
  `Tools/PakTool` because it is a tool-local automation/reporting contract.

This split keeps domain artifacts reusable without turning the tool into a
general-purpose library.

### 5.3 Schema-Backed Persistence

Every new persisted JSON artifact introduced here must have:

- a schema file in source control,
- an explicit schema version,
- deterministic field ordering,
- install/deployment wiring,
- tests for valid and invalid payloads.

### 5.4 Deterministic Behavior

When `PakBuildOptions::deterministic` is enabled, the tool must preserve:

- CLI source ordering into `PakBuildRequest`,
- deterministic catalog/report serialization,
- deterministic diagnostics ordering,
- deterministic publish behavior and filenames.

### 5.5 Safe Artifact Publication

The tool must stage output artifacts before publishing them to their final
paths. This applies to:

- output pak,
- output catalog,
- output manifest,
- optional diagnostics/build report.

Release-grade consequence:

- builder-facing output paths for pak/manifest should be temporary sibling paths
  in the destination directory,
- the final publish step must happen only after the build result is known,
- failed builds must not publish catalog/manifest artifacts as authoritative
  final outputs,
- best-effort cleanup of temporary files is required.

If the current native builder contracts cannot support staging cleanly, that is
a contract gap and must be addressed before the tool is called complete.

## 6. Tool Topology

The intended module split is:

- `src/Oxygen/Cooker/Pak`
  - native build contracts
  - catalog sidecar schema(s)
  - catalog read/write/validation helpers
- `src/Oxygen/Cooker/Tools/PakTool`
  - CLI
  - request assembly
  - filesystem validation
  - artifact staging/publish orchestration
  - console writer/report writer
  - process exit code mapping

### 6.1 Expected New Pak-Domain Additions

The pak domain layer is expected to gain:

- `PakCatalogIo` helper(s)
- pak catalog schema asset(s)
- any missing builder/result contract fixes needed for tool consumption

### 6.2 Expected Tool-Local Additions

The tool layer is expected to gain:

- CLI builder / command handlers
- staged output path resolver
- build report writer
- console/report formatting helpers

## 7. CLI Contract

The CLI should map directly onto the two builder modes:

```text
Oxygen.Cooker.PakTool build [options]
Oxygen.Cooker.PakTool patch [options]
```

Built-in help/version must be provided through `oxygen::clap` in the same way
other native Oxygen tools do.

CLI option design rule:

- prefer flags or inversion flags over required bool literals,
- do not require users to spell `true`/`false` for defaulted behavior unless
  the underlying parser/framework leaves no cleaner choice.

### 7.1 Common Options

- `--loose-source <dir>` repeatable
  Maps to `CookedSource { kind = kLooseCooked, path = ... }`.
- `--pak-source <path>` repeatable
  Maps to `CookedSource { kind = kPak, path = ... }`.
- `--out <pak-path>` required
  Final published pak path.
- `--catalog-out <path>` required
  Final published pak catalog sidecar path.
- `--content-version <u16>` required
  Maps to `PakBuildRequest::content_version`.
- `--source-key <uuid>` required
  Parsed through `Uuid::FromString`; must be canonical lowercase UUIDv7 text
  and is persisted in canonical lowercase UUIDv7 format.
- `--non-deterministic`
  Sets `PakBuildOptions::deterministic = false`.
- `--embed-browse-index`
  Sets `PakBuildOptions::embed_browse_index = true`.
- `--no-crc32`
- `--fail-on-warnings`
- `--quiet`
- `--no-color`
- `--diagnostics-file <path>`
  Writes the structured build report.

Intentionally not included in this release:

- `--no-tui`
- `--theme`

Those belong only in a release that actually ships a real alternate UI mode.

### 7.2 `build` Options

- `--manifest-out <path>` optional

Rules:

- In full-build mode, `--manifest-out` opts into manifest emission.
- `PakBuildOptions::emit_manifest_in_full` is set when `--manifest-out` is
  provided.

### 7.3 `patch` Options

- `--base-catalog <path>` repeatable, required
- `--manifest-out <path>` required
- `--allow-base-set-mismatch`
- `--allow-content-version-mismatch`
- `--allow-base-source-key-mismatch`
- `--allow-catalog-digest-mismatch`

Rules:

- Each `--base-catalog` path is loaded through pak-domain catalog IO helpers.
- The compatibility flags relax the default strict `PatchCompatibilityPolicy`
  settings by flipping the corresponding `require_*` field to `false`.

### 7.4 Input Rules

- Full builds may use zero sources and still produce an empty pak.
- Patch builds may use zero sources only when base catalogs are still supplied;
  this enables delete-only patches.
- Source arguments are preserved in CLI order.
- The tool must reject:
  - missing output paths,
  - invalid `source_key` text,
  - unreadable source paths,
  - invalid catalog payloads,
  - directory/file kind mismatches for obvious path misuse.

### 7.5 Filesystem Behavior

Before the builder runs, the tool must:

- validate that parent directories for requested outputs exist or can be
  created,
- validate that source/base-catalog inputs exist and are readable,
- allocate temporary sibling paths for staged outputs.

### 7.6 Exit Codes

- `0`: build completed with no errors
- `1`: CLI usage/argument error
- `2`: external input or filesystem preparation failure before build execution
- `3`: build completed but produced error diagnostics
- `4`: unhandled runtime failure or publish failure

Warnings do not change the exit code unless `--fail-on-warnings` causes the
native builder to emit an error diagnostic.

## 8. Persisted Artifact Contracts

### 8.1 Pak Catalog Sidecar

The pak catalog sidecar is a first-class content artifact required by patch
workflows. It must not be treated as an implementation detail of `PakTool`.

Canonical contract:

- Schema file:
  `src/Oxygen/Cooker/Pak/Schemas/oxygen.pak-catalog.schema.json`
- Default extension: `.pakcatalog.json`
- Ownership: `Oxygen/Cooker/Pak`
- Serialization: deterministic JSON via `nlohmann::ordered_json`

Minimum payload:

- `schema_version`
- `source_key`
- `content_version`
- `catalog_digest`
- `entries[]`
  - `asset_key`
  - `asset_type`
  - `descriptor_digest`
  - `transitive_resource_digest`

Canonical encodings:

- `source_key`: canonical lowercase RFC 9562 UUIDv7 text
- `asset_key`: canonical text from `data::to_string(AssetKey)`
- digest fields: lowercase hex
- `asset_type`: canonical string from `data::to_string(AssetType)`

Behavioral requirements:

- catalog write/read helpers must live in pak-domain code, for example
  `PakCatalogIo`,
- patch mode accepts only this canonical catalog format,
- `source_key` round-trips must enforce the native `SourceKey` runtime
  contract and reject non-v7 values on both text and binary ingress paths,
- schema-invalid catalogs fail fast before build execution,
- catalog serialization ordering must be deterministic.

### 8.2 Structured Build Report

The diagnostics/build report is a tool-local artifact for CI and release
automation. It must be stable enough to consume without scraping stdout.

Canonical contract:

- Schema file:
  `src/Oxygen/Cooker/Tools/PakTool/Schemas/oxygen.pak-build-report.schema.json`
- Emitted only when `--diagnostics-file` is specified
- Serialization: deterministic JSON via `nlohmann::ordered_json`

Minimum payload:

- `schema_version`
- `tool_name`
- `tool_version`
- `command`
- `command_line`
- `request`
- `artifacts`
  - pak path / size / crc information
  - catalog path / catalog digest
  - manifest path when emitted
  - publication status flags
- `summary`
- `telemetry`
- `diagnostics`
- `exit_code`
- `success`

The build report is informative only. It must never become an input to patch
classification or other packaging semantics.

### 8.3 Manifest Output

Manifest generation remains owned by the existing native manifest pipeline in
this release. `PakTool` provides:

- manifest-path routing,
- staged publication,
- console/reporting around the emitted result.

`PakTool` must not fork the manifest JSON model.

## 9. Output Staging And Publish Semantics

`PakTool` must publish artifacts in two phases:

1. build/stage
2. finalize/publish

Required flow:

1. Resolve final requested artifact paths.
2. Create temporary sibling paths in the same destination directories.
3. Invoke `PakBuilder` using staged pak/manifest paths.
4. Persist staged catalog/report outputs.
5. If the build result contains errors, do not publish catalog/manifest as final
   authoritative outputs.
6. If publication succeeds, replace or publish final targets in a deterministic
   order.
7. Best-effort delete staged files after success or failure.

Release blocker:

- The implementation is not production-ready if a failed run can silently leave
  final catalog/manifest outputs that appear authoritative.

## 10. Console UX Contract

Console behavior must be deterministic and CI-safe.

### 10.1 Messaging Surface

The tool needs these message categories:

- `Info`
- `Warning`
- `Error`
- `Report`
- `Progress`

Shared console abstractions may be extracted if the reuse is clean. The tool
must not inherit unrelated runtime dependencies from other tools just to obtain
message formatting.

### 10.2 Human Output

Minimum console output:

- command and mode
- source counts by kind
- base-catalog count for patch mode
- final requested artifact paths
- warning/error diagnostics with code and phase
- summary counters
- timing summary
- explicit publication result

### 10.3 Progress Model

Progress should be phase-oriented and map onto existing pak phases:

- request validation
- planning
- writing
- manifest
- finalize

The `finalize` phase is tool-owned and covers sidecar persistence and staged
artifact publication.

## 11. Native Contract Alignment

### 11.1 Request Mapping

`PakTool` must construct `PakBuildRequest` directly from parsed arguments:

- command -> `BuildMode`
- source options -> `std::vector<data::CookedSource>`
- staged pak/manifest paths -> `output_pak_path`, `output_manifest_path`
- `content-version` -> `content_version`
- `source-key` -> `source_key`
- patch compatibility relaxation flags -> inverted `patch_compat`
- build flags -> `PakBuildOptions`
- loaded base catalogs -> `base_catalogs`

### 11.2 Result Handling

`PakBuildResult` is the authoritative post-build state for:

- catalog content
- manifest presence
- pak size / crc
- diagnostics
- summary
- telemetry

The tool must report and persist from `PakBuildResult`. It must not recompute a
second summary.

### 11.3 Contract Gaps That Must Be Closed

The current implementation plan must explicitly close these gaps:

1. `PakBuildResult::output_catalog` must be reliably populated by the native
   builder.
2. Catalog sidecar IO must exist in pak-domain code.
3. Output staging/publish orchestration must be implemented so final artifact
   publication is safe.

All three are release blockers.

## 12. Validation Requirements

The tool is not complete until the following are validated.

### 12.1 Native Pak Validation

Existing native tests remain the baseline:

- API contract tests
- domain validation tests
- planner/patch planner tests
- writer tests
- binary conformance tests
- manifest tests

Any builder/result change required by the tool must extend the native test
surface first.

### 12.2 Tool Validation

Add automated coverage for:

- CLI parsing for `build` and `patch`
- invalid/missing required options
- `source_key` parsing failures
- catalog schema validation failures
- report schema validation / emission
- deterministic catalog serialization
- deterministic report serialization
- staged publish behavior
- full build end-to-end: pak + catalog
- full build with manifest: pak + catalog + manifest
- patch build end-to-end: pak + catalog + manifest
- exit-code behavior, including `--fail-on-warnings`

### 12.3 Manual Validation

Before the task can be called complete:

1. Run `PakTool build` on representative cooked input.
2. Inspect the emitted pak with `Oxygen.Cooker.PakDump`.
3. Run `PakTool patch` using persisted base catalog inputs.
4. Verify the emitted catalog and manifest are reusable by subsequent patch
   runs.
5. Verify failed runs do not publish misleading final sidecars.

## 13. Release Gates

The release is ready only when all of the following are true:

1. `Oxygen.Cooker.PakTool` builds and is wired into CMake/install.
2. Pak-domain catalog schema(s) are embedded/installed and exercised in tests.
3. The builder populates `PakBuildResult::output_catalog`.
4. Full mode produces published `.pak` + catalog sidecar.
5. Patch mode produces published `.pak` + catalog sidecar + manifest.
6. Structured build report emission works when requested.
7. Staged publication behavior is verified for success and failure paths.
8. Automated coverage exists for CLI parsing, catalog IO, report IO, and
   end-to-end behavior.
9. Manual validation with `PakDump` has been executed and recorded.

## 14. Deferred Follow-Ups

The following are reasonable future extensions but are out of scope here unless
separately approved:

- `plan-only` / `validate-only` commands
- richer terminal UI
- extra machine-readable outputs beyond the build report
- batch orchestration across multiple pak jobs
- future pak format evolution
