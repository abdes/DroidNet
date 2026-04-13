# Pak Builder End-to-End Execution Plan (Spec-Locked)

**Date:** 2026-02-28
**Status:** Design / Implementation Plan

## 1. Objective

Implement `design/content-pipeline/pak-builder-api-specification.md` end-to-end with:

- no backward-compatibility shims,
- no parallel/alternate production build pipelines,
- no behavior beyond what the design specifies unless explicitly approved.

This plan is implementation-oriented and mapped to current repository structure.
Public API namespace for this work is fixed to `oxygen::content::pak`.

## 2. Hard Boundaries (Must Not Be Violated)

1. Latest-schema only. No schema-version branching.
2. One architecture only: immutable `PakPlan` + one `PakWriter`; patch is planning mode, not a separate writer path.
3. No cross-domain type aliasing in schema headers (`pak::<domain>` isolation rule).
4. No post-write full-file CRC pass; streaming CRC with skip-field semantics only.
5. No first-mounted-wins behavior for patch-aware resolution paths; precedence is fixed to last-mounted-wins.
6. No additional features, data fields, compatibility knobs, or fallback modes without formal approval.
7. No directory proliferation under `src/Oxygen/Cooker/Pak/`; only directories with clear architectural semantics are allowed.
8. Pak build diagnostics/reporting must be engine-grade and first-class (comparable in quality and detail to Import diagnostics/reporting).
9. No dependency edge from `Oxygen.Content` to `Oxygen.Cooker` (compile-time, link-time, or runtime API ownership).
10. `*/Internal/*` headers are module-private. No other module may include or consume them.
11. Acceptance criteria authority is frozen to this file and `design/content-pipeline/pak-builder-api-specification.md` only; no external notes/reviews/chats are normative unless promoted into one of these files with formal approval.

## 3. Current Baseline (Repository Facts)

1. No C++ Pak builder implementation currently exists in `src/Oxygen/Cooker`.
2. Existing PAK generation logic exists in Python tooling (`src/Oxygen/Cooker/Tools/PakGen`) and tests.
3. Runtime `AssetLoader` key resolution already iterates highest-precedence-first (last-mounted-wins).
4. `VirtualPathResolver` is currently first-mounted-wins and must be changed.
5. Cooker module has no `Pak/` directory or Pak-specific C++ tests yet.

## 4. Target Code Placement

Primary implementation (as requested): `src/Oxygen/Cooker/Pak/` (mostly).

Required supporting changes outside cooker:

- `src/Oxygen/Content/` for runtime patch application contract (mount precedence + tombstones + virtual path parity).
- `src/Oxygen/Data/` for shared patch/catalog model types consumed by both cooker and content (this is mandatory to keep `Content` independent from `Cooker`).

Pak directory structure policy:

- Default to a flat `src/Oxygen/Cooker/Pak/` layout.
- Introduce at most one internal-only subdirectory (`Internal/`) if and only if it clearly separates public API from private implementation.
- Do not create thematic subdirectories (`Plan/`, `Writer/`, `Serialization/`, `Validators/`) unless file count and ownership force a split and that split is explicitly approved.

Runtime precedence/tombstones specification is fixed to the following artifacts:

1. Normative design text:
   - [`design/content-pipeline/pak-builder-api-specification.md`](../content-pipeline/pak-builder-api-specification.md) Section 11.
2. Runtime policy contract doc (new, normative for content implementation):
   - `src/Oxygen/Content/Docs/patch_resolution_contract.md`.
3. Single shared runtime implementation helper (authoritative behavior):
   - `src/Oxygen/Content/Internal/PatchResolutionPolicy.h`
   - `src/Oxygen/Content/Internal/PatchResolutionPolicy.cpp`
4. Tombstone storage/registration surface:
   - `src/Oxygen/Content/Internal/ContentSourceRegistry.h/.cpp`
5. Enforcement call sites (must route through shared helper, no duplicated logic):
   - [`src/Oxygen/Content/AssetLoader.cpp`](/F:/projects/DroidNet/projects/Oxygen.Engine/src/Oxygen/Content/AssetLoader.cpp)
   - [`src/Oxygen/Content/VirtualPathResolver.cpp`](/F:/projects/DroidNet/projects/Oxygen.Engine/src/Oxygen/Content/VirtualPathResolver.cpp)
6. Conformance tests (authoritative executable spec):
   - `src/Oxygen/Content/Test/PatchResolution_test.cpp`
   - existing resolver/loader tests updated to parity assertions.

Module-boundary rule for this runtime helper:

1. `PatchResolutionPolicy` is **Content-internal only**.
2. `Oxygen.Cooker` must not include anything under `src/Oxygen/Content/Internal/`.
3. Cross-module sharing is limited to public `Oxygen.Data` model types (`PatchManifest`, `PakCatalog`, etc.).
4. If Cooker needs resolution semantics for validation/testing, it must implement its own logic or use shared data contracts, never Content internals.

Mandatory algorithm definition (implemented once in `PatchResolutionPolicy`):

1. Iterate mounts in reverse registration order (`last-mounted wins`).
2. For each mount:
   - if mount tombstones key: return `not_found` immediately (terminal, no fallthrough);
   - else if mount contains key: return `found` with source identity;
   - else continue.
3. If no mount yields terminal decision: return `not_found`.
4. Virtual-path lookup:
   - resolve virtual path to candidate key from highest-precedence mapping;
   - re-run the same key-resolution helper above before returning.
5. Collision diagnostics:
   - emit diagnostic for lower-priority masked mappings;
   - never change resolution outcome because of diagnostics.

## 5. Diagnostics Contract (Approved Requirement)

Pak build diagnostics are mandatory deliverables, not optional logs.

Minimum contract:

1. Structured diagnostic record per issue:
   - severity (`Info`/`Warning`/`Error`)
   - stable code (`pak.request.*`, `pak.plan.*`, `pak.write.*`, `pak.patch.*`, `pak.runtime.*`)
   - message
   - phase (`request_validation`, `planning`, `writing`, `manifest`, `finalize`)
   - context fields (`asset_key`, `resource_kind`, `table_name`, `offset`, `path`) as applicable.
2. Build summary in result/report:
   - diagnostics counts by severity
   - assets/resources processed counts
   - patch action counts (`create/replace/delete/unchanged`)
   - bytes written and CRC status
   - success/failure.
3. Timing telemetry in result/report:
   - planning duration
   - writing duration
   - manifest duration
   - total duration.
4. `fail_on_warnings` must be enforced via structured diagnostics count, not string matching.
5. Diagnostics must be deterministic for deterministic inputs (stable code ordering and context ordering).

## 6. Execution Phases

## Phase 0 - Spec Lock and Build Wiring

### Deliverables

1. Add `Pak/` subtree to cooker target in [`src/Oxygen/Cooker/CMakeLists.txt`](/F:/projects/DroidNet/projects/Oxygen.Engine/src/Oxygen/Cooker/CMakeLists.txt).
2. Add Pak test groups to [`src/Oxygen/Cooker/Test/CMakeLists.txt`](/F:/projects/DroidNet/projects/Oxygen.Engine/src/Oxygen/Cooker/Test/CMakeLists.txt).
3. Freeze acceptance criteria in this file and `design/content-pipeline/pak-builder-api-specification.md` only.
4. Document source-key determinism policy ownership at request-construction layer (CLI/tooling producing `PakBuildRequest::source_key`) and bind it to explicit test coverage:
   - deterministic derivation stability for identical normalized inputs,
   - non-zero source key guarantee,
   - reproducibility across repeated invocations.

### Exit Criteria

1. Empty Pak scaffolding builds.
2. Pak test binaries are registered in CTest (even if initially placeholder/failing in controlled sequence).
3. Acceptance criteria lock text exists in this file and `design/content-pipeline/pak-builder-api-specification.md`.
4. Source-key determinism ownership and coverage obligations are documented in both design and test-plan sections.

---

## Phase 1 - Public API Surface (Exact Spec Contract)

### Deliverables

1. Add public API headers in `src/Oxygen/Cooker/Pak/` implementing the exact Section 5 contract from `pak_builder.md`:
   - namespace `oxygen::content::pak`
   - `BuildMode`
   - `PakBuildOptions`
   - diagnostics API types (`PakDiagnosticSeverity`, `PakBuildPhase`, `PakDiagnostic`, `PakBuildSummary`, `PakBuildTelemetry`)
   - `PatchCompatibilityPolicy`
   - `PakBuildRequest`
   - `PakBuildResult`
   - `PakBuilder::Build(...) -> Result<PakBuildResult>`
2. Define/locate domain types referenced by the API (`CookedSource`, `PakCatalog`, `PatchManifest`) in dependency-safe modules.
3. Add strict request-level validation errors for all mode-specific required fields.
4. Add diagnostics/report model to public Pak API:
   - severity enum (`Info`, `Warning`, `Error`)
   - stable diagnostic code strings
   - human-readable messages
   - source context fields (asset key/resource key/path/phase)
   - build summary counters.

### Exit Criteria

1. API signatures match design text exactly.
2. Link-level test compiles against public API with no TODO/stub behavior hidden.
3. `PakBuildResult` includes structured diagnostics and summary output suitable for CI and tool UIs.

---

## Phase 2 - Canonical Plan Model + Validation Engine

### Deliverables

1. Implement immutable `PakPlan` model with all required sections:
   - header plan
   - region plans
   - table plans
   - asset placement plan
   - asset directory plan
   - optional browse index plan
   - footer plan
   - CRC patch-field absolute offset
   - patch action map + transitive closure set
2. Implement `PlanBuilder` with planning mode policy (`Full`/`Patch`) selected at planning time.
3. Implement full validation matrix from Section 13 (planner validations), including:
   - schema ownership checks
   - layout/table/descriptor/resource/patch checks
   - index-0 policy checks
   - script slot/param bounds checks
4. Every planner validation failure emits structured diagnostics with stable codes and field-level context.

### Exit Criteria

1. `PakPlan` is immutable after successful validation.
2. Planner rejects all invalid states listed in Section 13 before any write.
3. Planner diagnostics clearly identify failing asset/resource/table/offset and violated rule.

---

## Phase 3 - Full Build Semantics

### Deliverables

1. Implement full-mode inclusion of all live assets/resources.
2. Implement deterministic ordering contracts:
   - stable input normalization
   - stable asset/resource/directory/browse-index ordering
3. Enforce index-0 semantics category-by-category (fallback-at-0 vs sentinel-at-0).
4. Explicitly extract and emit scripting sidecar tables in full mode:
   - populate `script_slot_table` when slots exist
   - emit/validate script param record arrays in-bounds.
5. Implement planner-side payload sizing invariants required before writing:
   - browse-index payload size planning uses the same structural encoding contract later used by writer serialization,
   - script slot/param range planning validates strict in-bounds ranges against source param arrays.

### Exit Criteria

1. Same normalized inputs + `deterministic=true` produce equivalent `PakPlan` outputs (ordering, offsets, sizes, and table/range metadata are stable).
2. Full-mode scripting sidecar extraction is complete and strictly validated (slot ranges in-bounds, invalid ranges fail planning with structured diagnostics).

---

## Phase 4 - Patch Planning Semantics

### Deliverables

1. Implement `Create/Replace/Delete/Unchanged` classification by:
   - canonical descriptor bytes
   - deterministic transitive resource closure digest
2. Enforce patch-specific rules:
   - emit descriptors only for `Create`/`Replace`
   - `Delete` is manifest-only
   - replace preserves `asset_type`
   - patch-local resource tables and indices
   - emitted descriptors reference patch-local resources only (except AssetKey references).
3. Build compatibility envelope data required by manifest.

### Exit Criteria

1. Classification is deterministic and reproducible.
2. Patch closure is complete and validated for every emitted descriptor.

---

## Phase 5 - PakWriter + CRC/Padding Contracts

### Deliverables

1. Implement `PakWriter` with exact phase ordering from Section 8.
2. Enforce cursor-vs-plan offset equality at each phase.
3. Implement mandatory zero-fill for:
   - alignment gaps
   - struct trailing padding gaps
4. Implement CRC option behavior exactly:
   - when `compute_crc32=true`: streaming CRC state machine, skip only `pak_crc32`, single patch seek, no full-file pass
   - when `compute_crc32=false`: run no CRC stream and keep footer `pak_crc32==0`.
5. Emit phase-aware writer diagnostics:
   - phase start/end
   - expected vs actual offsets
   - write/seek/flush failures
   - CRC state errors.
6. Implement domain serializer `Measure()` + `Store()` coupling for all writer-emitted payloads (single logic path, no divergence across domains/tables/descriptors).

### Exit Criteria

1. Binary layout exactly matches planned offsets/sizes.
2. CRC behavior matches runtime verification semantics in `Content/PakFile`.
3. Writer failures are reported as actionable structured diagnostics, not opaque errors.
4. CRC-disabled builds are deterministic and leave `pak_crc32==0`.
5. Same inputs + `deterministic=true` produce bit-exact identical pak binary output.
6. All writer-emitted domain serializers satisfy measure/store exact-size invariants.

---

## Phase 6 - Patch Manifest Emission

### Deliverables

1. Implement `PatchManifestWriter` for:
   - patch mode (required artifact),
   - full mode when `emit_manifest_in_full=true`.
2. Manifest must include all fields in Section 10 and full-mode semantics from Section 10:
   - disjoint `created/replaced/deleted`
   - compatibility envelope
   - effective compatibility policy snapshot
   - diff basis identifier
   - patch source key and digest/CRC metadata.
3. Manifest validation/emission failures must produce diagnostics with manifest field names and compatibility-envelope context.

### Exit Criteria

1. Patch mode fails if manifest cannot be emitted.
2. Full mode with `emit_manifest_in_full=true` fails if manifest cannot be emitted.
3. Manifest field completeness and disjointness are validated pre-emit.
4. Same inputs + `deterministic=true` produce bit-exact identical manifest output when manifest emission is enabled.

---

## Phase 7 - Runtime Patch Application Contract (Content Module)

### Deliverables

1. Implement compatibility validation against mounted base set.
2. Add tombstone registry support in `ContentSourceRegistry` keyed by mounted source identity.
3. Implement `PatchResolutionPolicy` shared helper with the exact algorithm defined in Section 4 above.
4. Route both `AssetLoader` key resolution and `VirtualPathResolver` virtual-path resolution through `PatchResolutionPolicy`.
5. Update `VirtualPathResolver` contract text and behavior from first-match-wins to last-mounted-wins.
6. Ensure patch application entry points in Content consume manifest/catalog types from `Oxygen.Data`, not `Oxygen.Cooker`.

### Exit Criteria

1. AssetKey and virtual-path resolutions are parity-tested through the same helper.
2. Tombstone behavior is deterministic and blocks fallthrough.
3. `Oxygen.Content` target/module has no dependency on `Oxygen.Cooker`.

---

## Phase 8 - Test Matrix Implementation (No Skips)

Implement full mandatory suite from Section 16 using C++ tests (plus existing test infra where useful).

### Test Programs to Add

1. `Cooker.Pak.BinaryConformance.Tests`
2. `Cooker.Pak.DomainValidation.Tests`
3. `Cooker.Pak.PatchPlanner.Tests`
4. `Cooker.Pak.Writer.Tests`
5. `Cooker.Pak.E2E.Tests`
6. `Content.PatchResolution.Tests` (or extension of existing Content test suites)

### Mandatory Coverage

1. Binary conformance (header/footer/table/directory/CRC skip-field).
2. Domain correctness (all descriptors/tables, script slot/param bounds, world/physics component overlap rejection).
3. Patch correctness (classification, closure, replace type mismatch, tombstones, full-build `emit_manifest_in_full` behavior).
4. Mount/resolution semantics (last-mounted-wins parity, collision masking diagnostics).
5. Diagnostics/report quality:
   - stable diagnostic codes
   - expected severities
   - required context fields populated
   - summary counters consistent with emitted diagnostics.
6. Source-key determinism policy at request-construction layer:
   - stable derivation for identical normalized inputs,
   - non-zero guarantee,
   - reproducibility across repeated runs.

### Exit Criteria

1. Every item in Section 16 has at least one explicit test.
2. No `DISABLED_` or TODO placeholders in mandatory tests.
3. Diagnostics contract tests pass for planner, writer, and manifest phases.
4. Source-key determinism policy is documented and tested at the request-construction layer (CLI/tooling that produces `PakBuildRequest::source_key`), with stable-output tests.

---

## Phase 9 - Cutover and Cleanup

### Deliverables

1. Make C++ PakBuilder the canonical production path.
2. Remove production dependence on any alternate PAK writer pipeline.
3. Update docs to reflect final runtime precedence and patch contract.
4. Ensure no docs still claim first-match-wins where design mandates last-mounted-wins.

### Exit Criteria

1. Production cook/build flow uses only the new C++ path.
2. Documentation and tests reflect one architecture only.

## 7. Concrete File-Level Work Map

## New (Cooker, lean layout)

1. `src/Oxygen/Cooker/Pak/PakBuilder.h`
2. `src/Oxygen/Cooker/Pak/PakBuilder.cpp`
3. `src/Oxygen/Cooker/Pak/PakBuildTypes.h`
4. `src/Oxygen/Cooker/Pak/PakDiagnostics.h`
5. `src/Oxygen/Cooker/Pak/PakReport.h`
6. `src/Oxygen/Cooker/Pak/PakCatalogLoader.h/.cpp`
7. `src/Oxygen/Cooker/Pak/PakPlan.h`
8. `src/Oxygen/Cooker/Pak/PakPlanBuilder.h/.cpp`
9. `src/Oxygen/Cooker/Pak/PakPlanPolicy.h/.cpp`
10. `src/Oxygen/Cooker/Pak/PakFingerprinter.h/.cpp`
11. `src/Oxygen/Cooker/Pak/PakClosureResolver.h/.cpp`
12. `src/Oxygen/Cooker/Pak/PakValidation.h/.cpp`
13. `src/Oxygen/Cooker/Pak/PakWriter.h/.cpp`
14. `src/Oxygen/Cooker/Pak/PakStreamingCrc32.h/.cpp`
15. `src/Oxygen/Cooker/Pak/PakManifestWriter.h/.cpp`
16. `src/Oxygen/Cooker/Pak/PakMeasureStore.h/.cpp`

Optional only if proven necessary and approved:

1. `src/Oxygen/Cooker/Pak/Internal/*` for private helper decomposition.

## New/Changed (Content)

1. `src/Oxygen/Content/Internal/PatchResolutionPolicy.h`
2. `src/Oxygen/Content/Internal/PatchResolutionPolicy.cpp`
3. [`src/Oxygen/Content/AssetLoader.cpp`](/F:/projects/DroidNet/projects/Oxygen.Engine/src/Oxygen/Content/AssetLoader.cpp) (route through shared helper + tombstones).
4. [`src/Oxygen/Content/VirtualPathResolver.h`](/F:/projects/DroidNet/projects/Oxygen.Engine/src/Oxygen/Content/VirtualPathResolver.h)
5. [`src/Oxygen/Content/VirtualPathResolver.cpp`](/F:/projects/DroidNet/projects/Oxygen.Engine/src/Oxygen/Content/VirtualPathResolver.cpp)
6. `src/Oxygen/Content/Internal/ContentSourceRegistry.h/.cpp` (tombstone registration/storage)
7. `src/Oxygen/Content/Docs/patch_resolution_contract.md`

## Build/Test Wiring

1. [`src/Oxygen/Cooker/CMakeLists.txt`](/F:/projects/DroidNet/projects/Oxygen.Engine/src/Oxygen/Cooker/CMakeLists.txt)
2. [`src/Oxygen/Cooker/Test/CMakeLists.txt`](/F:/projects/DroidNet/projects/Oxygen.Engine/src/Oxygen/Cooker/Test/CMakeLists.txt)
3. [`src/Oxygen/Content/Test/CMakeLists.txt`](/F:/projects/DroidNet/projects/Oxygen.Engine/src/Oxygen/Content/Test/CMakeLists.txt)
4. [`src/Oxygen/Data/CMakeLists.txt`](/F:/projects/DroidNet/projects/Oxygen.Engine/src/Oxygen/Data/CMakeLists.txt)

## New/Changed (Data, shared models only)

1. `src/Oxygen/Data/PatchManifest.h`
2. `src/Oxygen/Data/PatchManifest.cpp`
3. `src/Oxygen/Data/PakCatalog.h`
4. `src/Oxygen/Data/PakCatalog.cpp`
5. optional codec headers/sources in `src/Oxygen/Data/` if serialization format requires dedicated encode/decode helpers.

## 8. Approval Gates (Required Before Coding the Related Phase)

Only items not concretely specified by `pak_builder.md` require approval:

1. **Manifest serialization format**: on-disk representation if multiple valid encodings are possible in current codebase conventions.
2. **CLI integration point**: whether to extend an existing cooker tool command surface or add a dedicated Pak build command.
3. **Diagnostics output channel**: whether to also emit standalone diagnostics/report files in addition to in-memory `PakBuildResult` fields.

No implementation should proceed on these sub-items until explicitly approved.

## 9. Dependency Gate (Mandatory Check)

Before merge, verify:

1. `Oxygen.Cooker` module build graph does not include `Oxygen/Content/Internal/*`.
2. `Oxygen.Content` module build graph does not include `Oxygen.Cooker` target/module.
3. Shared types used by both modules come only from `oxygen::data` public headers.

## 10. Definition of Done

Implementation is complete only when all are true:

1. `pak_builder.md` Section 5-18 requirements are implemented with no omitted mandatory behavior.
2. Mandatory tests in Section 16 all exist and pass.
3. Runtime lookup semantics match patch contract exactly (including tombstones and precedence parity).
4. C++ PakBuilder path is canonical production path; no active alternate production writer path remains.
5. No unapproved additions beyond design scope were introduced.
6. Pak diagnostics/report outputs are detailed, structured, and CI/tooling-grade.
