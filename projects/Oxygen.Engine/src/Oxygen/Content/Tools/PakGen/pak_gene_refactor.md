# Oxygen PAK Generator Refactor Roadmap (Updated 2025-08-10)

> Scope: Refactor and evolve `generate_pak.py` into a modular, testable, deterministic `pakgen` tool (Python package) **without** introducing a `.ksy` schema. Each phase is *incremental*, leaves the tool in a working state, and is guarded by regression tests / golden artifacts.

---

## Guiding Principles

- Single source of truth for binary layout (constants table mirroring C++ `PakFormat.h`).
- Pure planning phase before writing: all offsets, sizes, alignment determined up front.
- Deterministic output (optionally reproducible via `--deterministic`).
- Layered design: spec parsing → model → validation → planning → packing → writing → manifest.
- Fail fast with structured error reporting; rich context for diagnostics.
- Manifest emitted as machine‑readable summary to enable CI diffs and external tooling.
- Easy extensibility: new resource or asset types register metadata, not invasive rewrites.
- Strict invariants tested (size, alignment, index bounds) with unit + property tests.

---

## High-Level Architecture (Actual)

```text
PakGen/
  pyproject.toml               # Project config (PEP 621)
  requirements.txt             # Runtime / dev deps
  pak_gene_refactor.md         # Roadmap (this file)
  src/
    pakgen/
      __init__.py              # Package init (re-exports API/version)
      _version.py              # Version metadata
      api.py                   # Public Python API (high-level helpers)
      cli.py                   # CLI entry (arg parsing, subcommands WIP)
      diff.py                  # Deep diff / parity tooling
      logging.py               # Structured logging utilities
      packing/
        __init__.py
        constants.py           # Binary layout constants (authoritative)
        errors.py              # Error types & codes
        inspector.py           # Binary read / verification / CRC
        layout.py              # Layout primitives / dataclasses (intermediate)
        packers.py             # Pure serialization helpers
        planner.py             # Plan computation (initial implementation)
        writer.py              # Writer consuming planner (tests cover basic path)
      spec/
        loader.py              # Spec loading & normalization
        models.py              # Dataclass models (resources / assets)
        validator.py           # Multi-stage validation pipeline
      utils/
        io.py                  # I/O helpers (streaming, buffered ops)
        paths.py               # Path normalization / hashing helpers
      py.typed                 # PEP 561 type marker
    oxygen_pakgen.egg-info/    # Build artifacts (setuptools metadata)
  tests/
    test_diff_deep.py          # Deep diff / snapshot parity
    test_inspector_roundtrip.py# CRC + structural readback
    test_packers_sizes.py      # Size invariants for packers
    test_planner_layout.py     # Planner alignment / offset invariants
    test_resource_limits.py    # Boundary / limits property tests
    test_validation_binary.py  # Binary parity for validation scenarios
    test_validation_errors.py  # Error code coverage tests
    test_validation_semantic.py# Semantic validation cases
    test_writer_basic.py       # Writer integration sanity
  _helpers/
    parity.py                  # Shared golden diff utilities
  _snapshots/                  # Golden artifacts (JSON, CRC, etc.)
```

Notes

- `enums.py`, `manifest.py`, registry modules are not yet present (future phases).
- `planner.py` exists with initial functionality; full determinism & manifest integration pending.
- Legacy monolithic `generate_pak.py` superseded by modular structure above.

---

## Phase Overview Table (Condensed Current View)

| Phase | Theme | Status | Key Artifacts | Exit Criteria (Summary) |
|-------|-------|--------|---------------|--------------------------|
| 0 | Baseline Snapshot | DONE | binary_diff.py, inspector.py | Golden CRC + byte parity harness established |
| 1 | Extraction & Constants | DONE | constants.py, errors.py, logging_util.py | Central constants; legacy script functional |
| 2 | Pack Functions & Size Tests | DONE | packers.py, selftest | Pure packers; size & smoke tests pass |
| 2.5 | Binary Inspector & CRC | DONE | inspector.py, tests | Read/verify parity & CRC roundtrip |
| 3 | Models & Spec Normalization | DONE | models.py, spec_loader.py | Dataclass model authoritative; parity retained |
| 4 | Validation Pipeline | DONE | validator/* | Multi-stage pipeline + CLI flags |
| 5 | Planning Engine | DONE | planner.py | Deterministic PakPlan; dry-run offsets stable; plan JSON snapshot tests (basic + multi) |
| 6 | Writer Refactor | DONE | writer.py | Writer consumes PakPlan; layout math removed; bitwise parity maintained |
| 7 | Manifest Generation | DONE | manifest.py, test_manifest_generation.py | Optional JSON manifest (opt-in flag) with layout & counts; tests ensure presence/absence |
| 8 | Testing & Golden Files | DONE | tests/* (snapshots, binary diff) | Snapshot + binary diff regression layers established |
| 9 | Determinism & Repro Mode | DONE | cli.py, test_deterministic_build.py | Deterministic flag yields identical bytes + manifest hashes |
| 10 | Extended Metrics | PENDING | validator + manifest | Padding & size statistics emitted |
| 11 | Extensibility Hooks | PENDING | registry modules | Add new type via metadata only |
| 12 | Hardening & Fuzz | PENDING | fuzz_hypothesis.py | N seeds no crashes |

Phases renumbered slightly (0 & 2.5) to reflect already completed enabling work.

---

## Current Status Summary (As Of 2025-08-10)

All foundation layers (constants, packers, models, validation, inspection, regression guards) are complete. Phases 5–8 delivered deterministic planning, plan‑driven writing, optional manifest emission (opt‑in), and layered regression (snapshots + binary diff). Upcoming focus: determinism hashing (Phase 9), extended metrics enrichment (Phase 10), extensibility (Phase 11). Tech debt: manifest placeholders (`pak_crc32`, `spec_hash`) not yet populated; per‑asset manifest detail deferred; determinism SHA256 flag pending.

---

## Detailed Phases

### Phase 1: Extraction & Constants

- Move size constants (e.g., 256, 105, 108, 16, 40, 32) into `constants.py`.
- Introduce `errors.py` with base `PakError(code, message, context)`; derive existing error types.
- Implement logging_util with consistent interfaces: `log.info/verbose/warn/error_json`.
- Replace magic numbers in `generate_pak.py` imports from constants.
- Add `selftest.py` asserting constants mirror C++ struct sizes (manually defined now; future: parse header or runtime assert).

Exit Criteria:

- Script runs unchanged logically; constants referenced from new module.
- Selftest passes (invoked manually or via CI target).

### Phase 2: Pack Functions & Size Tests

- Implement pure functions in `packers.py` (e.g., `pack_texture_desc(data_offset, spec)`).
- Each returns `bytes` and raises `PackError` on invariant breach.
- Add unit tests verifying byte length & field placement (golden hex snippets for minimal valid cases).
- Remove partial / stub logic from original file; call packers.

Exit Criteria:

- All descriptor serialization solely via packers.
- Test: `pytest tests/test_packers.py` green.

### Phase 3: Models & Spec Normalization

- Dataclasses: `TextureSpec`, `BufferSpec`, `AudioSpec`, `MaterialSpec`, `GeometrySpec`, `MeshLODSpec`, `SubMeshSpec`, `MeshViewSpec`.
- `spec_loader.py`: load YAML/JSON → normalized model (fill defaults, convert hex GUIDs, clamp values, compute derived fields).
- Remove direct dict accesses in generator; consume model objects.

Exit Criteria:

- CLI generates identical PAK using model pipeline.
- Unit tests: object creation & normalization.

### Phase 4: Validation Pipeline

- Multi-stage validator: `SpecValidator` (structure), `ModelValidator` (references & bounds), `PrePlanValidator`.
- Standardized error codes for each failure type.
- CLI flag `--validate-only` performs all validations, returns non-zero on failure, prints summary & JSON error list.

Exit Criteria:

- Known invalid fixture specs produce deterministic error outputs.

### Phase 5: Planning Engine

- `planner.py` introduces `PakPlan`: offsets for regions, tables, descriptors, directory, footer.
- Compute alignments (resource-level & table). Track padding inserted.
- Provide planning summary (human + machine) with counts & offsets.

Exit Criteria:

- `--dry-run` prints plan (no file write) with identical future offsets to actual write.
- Plan invariants validated (no overlaps, proper alignment).
- Deterministic mode emits stable `PakPlan` JSON matching committed snapshot (excluding explicitly whitelisted volatile fields), providing regression guard for layout changes.

### Phase 6: Writer Refactor

- `writer.py` writes in this order: header → resource regions → resource tables → asset descriptors → directory → footer (CRC placeholder) → patch CRC.
- Single pass file I/O; no partial rewrites except CRC patch.
- Legacy write code removed.

Progress (2025-08-10): Writer now fully consumes immutable `PakPlan`; all
section offsets padded via helper `_pad_to` and validated against plan sizes.
Previous inline layout math removed. All 27 existing tests pass unchanged,
including plan parity & snapshots (ensuring no drift). Remaining tasks for
final closure: add explicit binary diff regression (old vs new writer output)
fixture and remove any residual helper paths no longer used.

Update: Legacy `align_file` helper removed (redundant post plan-driven write).
Test suite expanded to 28 tests including binary diff regression; all passing.

Exit Criteria:

- Binary diff of before/after for sample fixtures is identical.

### Phase 7: Manifest Generation (DONE)

### Phase 9: Determinism & Repro Mode (DONE)

Objective: Guarantee reproducible byte‑identical builds given identical specs
and deterministic ordering policy, and surface cryptographic + lightweight
hashes in the manifest.

Implementation:

- Added `deterministic` flag to `BuildOptions` and `--deterministic` CLI flag.
- Planner consumes flag to produce stable ordering / offsets (existing Phase 5
  logic already deterministic; flag threads intent through API/CLI).
- Canonical spec SHA256 computed via sorted‑key JSON serialization.
- After writing, file CRC32 (excluding footer CRC field) and full file SHA256
  are computed; manifest now includes `pak_crc32`, `spec_hash`, `sha256`.
- Added `test_deterministic_build.py` verifying two independent builds match
  byte‑for‑byte and that all three hashes are identical.
- Updated manifest tests to assert new hash fields populated.

Result:

- Deterministic builds produce identical binaries and stable manifest hash
  triplet (spec_hash, pak_crc32, sha256).
- Hash data establishes foundation for future reproducibility audits and cache
  keying.

Objective: Provide an optional machine‑readable JSON manifest summarising a build
without imposing extra I/O when not requested.

Implementation:

- Added `manifest.py` exposing `manifest_dict()` and `build_manifest()`.
- Extended `BuildOptions` with `manifest_path`; `build_pak()` emits manifest only
  when set.
- CLI `build` subcommand gained `--emit-manifest <path>` flag.
- Manifest contents (v1 minimal): file_size, deterministic flag, region
  summaries (name/offset/size for non‑empty), aggregate counts (regions, tables,
  assets, materials, geometries), placeholder fields `pak_crc32`, `spec_hash`.
- Tests (`test_manifest_generation.py`): ensure default build omits manifest;
  ensure opt‑in path creates file with consistent counts & non‑zero size.

Result:

- 30 test suite green including new manifest tests.
- No change to default outputs; manifest generation is fully opt‑in.
- Structure intentionally lean; future phases will enrich with hashes, per‑asset
  descriptors, padding & metric details.

### Phase 8: Testing & Golden Files (Completed)

Implemented multi-layer regression guard:

- Plan JSON snapshots (basic + multi-resource) with env-controlled update helper.
- Binary diff regression test (`test_binary_diff_regression.py`) using committed
  golden `minimal_ref.pak` (bitwise parity check + SHA256 hashes + diff context).
- Structural inspector roundtrip tests (header/footer parsing, CRC validation).
- Deep diff tests for materials/geometries, validation error coverage, planner
  alignment/padding invariants, resource limit boundaries.

Artifacts:

- `tests/_snapshots/plan_*.json` (layout snapshots)
- `tests/_golden/minimal_ref.pak` (binary golden)
- 28 passing tests spanning packers, planner, writer, diff, validation.

Exit Criteria (Met):

- Snapshot and binary diff layers detect layout or serialization drift.
- Full test suite executes cleanly (all 28 green) establishing baseline for
  upcoming manifest & determinism hashing work.

### Phase 9: Determinism & Repro Mode

- `--deterministic`: zero timestamps, fixed ordering policy, stable GUID normalization (no random defaults), manifest excludes ephemeral fields unless `--full-manifest`.

Exit Criteria:

- Repeated builds produce identical SHA256 of entire file for same inputs.

### Phase 10: Extended Validation & Metrics

- Collect & report: padding per section, largest resource, descriptor size distribution, shader stage popcount anomalies.
- Add warnings for unusually large single descriptors (> threshold).

Exit Criteria:

- Metrics appear in manifest under `statistics`.

### Phase 11: Extensibility Hooks

- Introduce `ResourceTypeRegistry` & `AssetHandlerRegistry` with declarative descriptors.
- Adding a new resource type requires implementing packer function + registry entry.

Exit Criteria:

- Example stub resource type added & recognized (behind `--experimental-new-type`).

### Phase 12: Optional Hardening & Fuzz

- Property tests with Hypothesis: random sequences of resources within limits still produce valid plan.
- Simple mutational fuzz (flip bytes in output & ensure validator catches).

Exit Criteria:

- Fuzz harness runs for N seeds w/out crashes.

---

## Invariants & Assertions (Central List)

- `sizeof(material_desc_base)==256` (MaterialAssetDesc w/out trailing shaders).
- `popcount(shader_stages) * 216 + 256 == material_descriptor_size`.
- Geometry descriptor total size = base (256) + Σ(mesh sizes + procedural blobs + submesh + meshview tables).
- Mesh: `mesh_view_count == Σ(submesh.mesh_view_count)`.
- Resource indices: `index == 0 || index < resource_table_count`.
- Region: `start % alignment == 0` and `end <= region.offset + region.size`.
- Directory size = `asset_count * 64`.
- Footer position = `file_size - 256`.
- CRC32 recomputation (excluding stored field) equals stored (unless zero skipped).
- No 64-bit overflow: every computed offset < 2^63 (safety margin) & section size fits within file.
- Descriptor `desc_size < 2^32`.

---

## Manifest Schema (v1 Draft)

```json
{
  "manifest_version": 1,
  "pak_format_version": 1,
  "content_version": 0,
  "deterministic": true,
  "file": {"size": 123456, "crc32": "0xDEADBEEF", "spec_hash": "sha256:..."},
  "resources": [
    {"type": "texture", "region": {"offset": 64, "size": 4096}, "table": {"offset": 4160, "count": 3, "entry_size": 40},
     "entries": [ {"index":1, "data_offset":512, "size":1024, "width":256, "height":256, "format":23}, "... more ..." ]},
    {"type": "buffer", "...": "..."}
  ],
  "assets": [
    {"key": "01234567-89ab-cdef-0123-456789abcdef", "type":"material", "name":"MatA",
     "descriptor_offset": 8192, "descriptor_size": 688, "shader_stage_mask":"0x15",
     "textures": {"base_color":1,"normal":0}},
    {"key": "...", "type":"geometry", "lod_count":2, "descriptor_offset":9000, "descriptor_size": 1200}
  ],
  "statistics": {
    "counts": {"assets": 5, "materials": 2, "geometries":1, "resources": {"texture":3, "buffer":2, "audio":0}},
    "padding": {"total": 512, "by_section": {"texture_region":256,"descriptors":256}},
    "max_resource": {"type":"texture", "size": 2048}
  },
  "warnings": ["material 0x.. uses no textures but shader expects 3"]
}
```

---

## Error Code Catalogue (Initial)

| Code | Description | Stage |
|------|-------------|-------|
| E_SPEC_MISSING_FIELD | Required field absent | Spec |
| E_SPEC_TYPE_MISMATCH | Field has wrong type | Spec |
| E_SPEC_VALUE_RANGE | Value out of allowed range | Spec |
| E_DUP_RESOURCE_NAME | Duplicate resource name | Spec |
| E_DUP_ASSET_KEY | Duplicate asset key | Model |
| E_INVALID_REFERENCE | Asset references unknown resource/material | Model |
| E_INDEX_OUT_OF_RANGE | Resource index >= table count | Plan/Pack |
| E_ALIGNMENT | Misalignment for region or data blob | Plan |
| E_OVERLAP | Section/data overlap detected | Plan |
| E_SIZE_MISMATCH | Calculated vs expected size differs | Pack |
| E_DESC_TOO_LARGE | Descriptor size exceeds uint32 max | Pack |
| E_WRITE_IO | I/O failure during write | Write |
| E_CRC_MISMATCH | Post-write CRC mismatch | Post-Write |
| E_INTERNAL | Unexpected invariant violation | Any |

---

## CLI Enhancements

New flags:

- `--validate-only`
- `--dry-run` (plan + summary, no file writes)
- `--emit-manifest path`
- `--deterministic`
- `--json-errors`
- `--summary` (print concise metrics)
- Subcommand style (optional future): `pakgen build`, `pakgen validate`, `pakgen diff`.

---

## Incremental Git Strategy

- Each phase in its own PR; include manifest + tests.
- Keep legacy script until Phase 6 complete; then remove deprecated paths.
- Add a `DEPRECATED:` banner in legacy areas during transition.

---

## Risk Mitigation

| Risk | Mitigation |
|------|------------|
| Divergence from C++ layout | Central constants + selftests + periodic manual compare |
| Regression in existing fixtures | Golden file diff in CI per phase |
| Scope creep | Strict adherence to phase goals; backlog for extras |
| Non-determinism introduced | Determinism mode with stable ordering & fixed timestamps |
| Hard to add new resource type | Registry abstraction in Phase 11 |

---

## Backlog (Post-Refactor Ideas)

- Compression layer per resource region
- Parallel resource data staging
- Incremental build: reuse unchanged blobs via manifest diff
- Signed PAK footer (public key / signature)
- Streaming-friendly chunk index

---

## Quick Start After Phase 6

```bash
# Validate spec only
pakgen --validate-only spec.yaml

# Dry run (plan only)
pakgen --dry-run spec.yaml

# Build pak + manifest deterministically
pakgen --deterministic --emit-manifest out/level01.manifest.json spec.yaml out/level01.pak
```

---

## Phase 1 Status (Completed)

All planned Phase 1 tasks have been implemented and validated:

- [x] Extract constants & create selftest (constants.py, selftest.py present; size assertions implemented)
- [x] Implement errors.py & update imports (central PakError + subclasses used; generate_pak.py uses shim subclasses for simple ctor)
- [x] Implement logging_util verbose/critical context support (Logger + SectionContext in logging_util.py; generate_pak.py uses them)
- [x] Replace magic numbers in existing generator (all struct sizes / limits now via imported constants aliases at top of script)
- [x] Add make target / script for selftest (NOTE: build integration target not yet added; manual invocation works; add CMake/CI hook in Phase 2)

### Phase 1 Verification

Artifacts:

- src/Oxygen/Content/Tools/PackGen/constants.py (authoritative sizes, limits)
- src/Oxygen/Content/Tools/PackGen/errors.py (error taxonomy)
- src/Oxygen/Content/Tools/PackGen/logging_util.py (structured logging)
- src/Oxygen/Content/Tools/PackGen/selftest.py (asserts key constant invariants)
- src/Oxygen/Content/Tools/PackGen/generate_pak.py (refactored to import constants/errors/logging only; no external Python code outside PackGen directory)

Behavioral Check:

- Dry-run (Empty.yaml) executes successfully using refactored script (0 resources, 0 assets) with verbose logging.
- No remaining magic numeric literals for core layout (header/footer sizes, descriptor sizes, alignment) outside constants module.
- Error raising sites now use simplified shim subclasses consistent with pre-refactor call signatures.

Deferred (intentionally to later phases):

- Test harness integration (will arrive with packers + validation phases)
- Model/packer/planner separation (Phases 2–6)
- CLI enhancements (validate-only, manifest emission, determinism)

Proceed to Phase 2 (Pack Functions & Size Tests) next.

---

## Phase 2 Task Breakdown (Pack Functions & Size Tests)

Goal: Isolate all binary descriptor serialization into pure, side‑effect free packer functions under a new `packers.py`, introduce unit/self tests asserting exact byte sizes and selected field encodings, while keeping CLI behavior unchanged.

Scope Boundaries:

- In scope: moving raw struct construction/`struct.pack` calls for header, footer, resource tables, directory entries, material/geometry/mesh/submesh/mesh view descriptors into `packers.py` functions; adding size assertions; adding a minimal test harness (can be invoked manually) that validates sizes and deterministic byte sequences for small canonical inputs.
- Out of scope: introducing dataclasses for specs (Phase 3), planning engine (Phase 5), manifest emission, or altering file write order.

Deliverables:

1. `packers.py` module containing:

    - `pack_header(version: int, content_version: int) -> bytes`
    - `pack_resource_table_entry(resource_type: str, fields: Dict[str, Any]) -> bytes`
    - `pack_material_descriptor(spec: Dict[str, Any], texture_indices: List[int]) -> bytes`
    - `pack_geometry_descriptor(geom_spec: Dict[str, Any], meshes_meta: Dict[str, Any]) -> bytes`
    - `pack_mesh_descriptor(mesh_spec: Dict[str, Any]) -> bytes`
    - `pack_submesh_descriptor(submesh_spec: Dict[str, Any]) -> bytes`
    - `pack_mesh_view_descriptor(view_spec: Dict[str, Any]) -> bytes`
    - `pack_asset_directory_entry(key: bytes, asset_type: int, desc_offset: int, desc_size: int, entry_offset: int) -> bytes`
    - `pack_footer(directory_offset: int, directory_size: int, asset_count: int, regions: Dict[str, Tuple[int,int]], tables: Dict[str, Tuple[int,int,int]]) -> bytes`
    - Constants for per-descriptor expected sizes (imported from `constants.py`).

2. Replacement in `generate_pak.py` write sections to call packers instead of inlined `struct.pack` / bytearray composition (minimal diff; original logic preserved).

3. `selftest.py` augmentation: call representative packers with dummy values to assert returned length matches expected size constants (header, footer, directory entry, material base, geometry base, mesh, submesh, mesh view).

4. New lightweight test file (Phase 2 placeholder): `packers_smoketest.py` (optional if unit test infra not yet integrated) that can be run manually and exits non‑zero on failure.

5. Documentation update in this roadmap marking Phase 2 tasks completion once done.

Granular Task List (Implemented):

- [x] Create `packers.py` skeleton with function signatures & docstrings referencing constants.
- [x] Extract header packing logic into `pack_header`.
- [x] Extract directory entry packing into `pack_directory_entry` (name adjusted from plan).
- [x] Extract footer packing into `pack_footer` (CRC still patched externally).
- [x] Extract material descriptor construction into `pack_material_asset_descriptor` (variable shader references supported via injected builder).
- [x] Extract geometry & related descriptors (`pack_geometry_asset_descriptor`, `pack_mesh_descriptor`, `pack_submesh_descriptor`, `pack_mesh_view_descriptor`).
- [x] Extract resource descriptors (`pack_buffer_resource_descriptor`, `pack_texture_resource_descriptor`, `pack_audio_resource_descriptor`).
- [x] Replace in `generate_pak.py` all descriptor `struct.pack` usages with packer calls (legacy create_* functions now thin wrappers slated for removal in Phase 3).
- [x] Add size assertions in `selftest.py` invoking each packer with minimal inputs.
- [x] Add `smoketest_packers.py` performing deterministic empty PAK construction & CRC patch logic.
- [x] Update roadmap (this document) to reflect completion.

Acceptance / Exit Criteria (Met):

- No direct descriptor-construction `struct.pack` calls remain in `generate_pak.py` (only wrappers delegating to packers & CRC patch) ✔️
- All packer functions return `bytes` and perform internal size validation before returning ✔️
- Selftest augmented with packer size checks; smoketest script added for deterministic empty PAK ✔️
- Roadmap updated (this section) ✔️

---

## Phase 4 Status (Completed)

The multi-stage validation pipeline (SpecStructure → ModelSemantic → CrossReference → PrePlan) is fully integrated. DescriptorSanity remains a planned additive stage (post-planner) to validate size / field invariants using plan context.

Key Outcomes:

- Deterministic ordering of errors (stable multi-key sort)
- JSON emission with `--json-errors`
- Property tests protect count limit & range logic
- Structural-first mode preserved for fast failure; full pipeline triggered when model already built

Deferred to later phases:

- DescriptorSanity stage (requires planner context for precise size/offset checks)
- Multi-error aggregation for structural-only fast path (currently first-error raise retained for ergonomics; JSON path already multi-error capable)

Rationale Recap (Spec-first prior to full model build): Avoids partial model states and duplication of coercion logic; now ready to expand once planner provides richer context.

---

## Phase 2.5: Binary Inspector & CRC Verification (Completed)

Purpose: Introduce a lightweight read/verify tool before Model & Planner phases to
ensure ongoing refactors can be validated against on-disk artifacts without manual
hex inspection.

Deliverables:

- `inspector.py` capable of:
  - Parsing header & footer (including regions, tables, directory metadata)
  - Locating actual CRC field (patched post-write at file_size-12)
  - Recomputing CRC (generator parity) and validating match
  - Parsing directory entries (16-byte key, type, descriptor offsets/sizes)
  - Emitting JSON (`--json`) and quiet/human modes
- Integration selftest (`test_crc_roundtrip` in `selftest.py`) building a minimal
  PAK and proving CRC roundtrip.
- Basic pytest-style test (`test_inspector_basic.py`) generating Empty & Simple
  specs and asserting zero validation issues plus structural expectations.

Key Adjustments / Discoveries:

- Confirmed directory entry key size = 16 bytes (padding 27) — corrected earlier
  mistaken assumption (32 bytes) and reverted padding.
- Identified dual CRC presence: placeholder inside footer body (always zero) and
  real CRC patched 12 bytes from file end; inspector treats placeholder as legacy.
- Harmonized CRC recomputation algorithm to match generator (exclude only final
  CRC field, include magic sentinel bytes).

Exit Criteria:

- Inspector reports `crc_match: true` and zero issues for current Empty & Simple
  fixture outputs.
- Selftest passes including CRC roundtrip and original size assertions.
- Roadmap updated with this section.

Next Phase (3) Prerequisites Now Enabled:

With reliable read-side validation, upcoming model & planner refactors can be
regression-checked by comparing inspector JSON outputs (offsets, counts, CRC)
pre/post changes.

---

Pending manual binary equivalence diff for Empty.yaml & a non-empty sample (scheduled early Phase 3).

## Phase 3: Model Layer & Regression Guard (Completed)

Goals:

- Introduce typed dataclass models for resources and assets (buffers, textures, audios, materials, geometries).
- Provide loader converting YAML/JSON into stable PakSpec while retaining legacy dict packing path for binary stability.
- Establish golden regression mechanisms (inspector JSON snapshots + byte-level hash diff) before swapping generator over to models fully.

Deliverables (status):

- models.py (DONE)
- spec_loader.py (DONE)
- Optional model bootstrap in generate_pak.py (DONE – passive)
- tests_snapshot.py (DONE – legacy manual harness; superseded by pytest test)
- test_snapshots_pytest.py (DONE – CI-ready)
- _snapshots/ (UPDATED with real CRC, sizes, counts, entries)
- binary_diff.py (DONE)
- Pytest snapshot integration (DONE – requires pytest install in CI)
- requirements-test.txt (DONE)
- Replace packing pipeline to consume models (DEFERRED – guarded by regression suite)
- Remove legacy wrapper helpers (DEFERRED – after model path authoritative)

Remaining / Follow-up (original plan):

1. Flip generator to construct descriptors from model objects directly (ensure snapshots + binary diffs unchanged).
2. Remove legacy descriptor creation wrappers (create_* helpers) post flip.
3. Migrate validation logic from generate_pak into dedicated model validators (stretch).
4. Optional: extend snapshots to include resource counts per type.

Status Update (Completed Post-Plan):

- [x] Generator flipped to build descriptors from model objects (binary parity held – snapshots & byte diffs unchanged).
- [x] Legacy create_* wrapper helpers removed (packers now consumed directly by model-driven path).
- [x] Validation logic extracted to dedicated validator module (structural checks; extensible for deeper field/range validation later).
- [x] Snapshots enriched with per‑type resource_counts (texture / buffer / audio) to aid drift diagnostics.

Notes:

- Extraction of validation achieved via dependency injection (constants & error classes passed in) preventing import cycles.
- Snapshot schema change was additive; existing assertions remained stable after updating golden files.
- Future enhancement: expand validator to full semantic checks (value ranges, cross-reference integrity) and add negative test fixtures.
- Roadmap Phase 3 core objectives now fully closed; subsequent phases (Planner, Writer, Determinism) proceed atop stabilized model + packer layers.

Risk Mitigation:

- Dict path kept authoritative until model flip executed under regression guard (snapshots + binary diff already green).
- Inspector JSON subset keeps schema stable; adding fields will not break existing tests.

---

### Phase 3 Completion Summary

The model layer now serves as the authoritative source for descriptor construction. All legacy dict/wrapper pathways have been retired, reducing duplication and risk of divergence. Validation is modularized, and enriched snapshots combined with the existing inspector & binary diff harness provide a multi-layer regression guard (structure, semantic counts, CRC, byte-level parity). This marks the formal completion of Phase 3 (including cleanup tasks initially deferred) and establishes a solid foundation for Phase 4/5 planning features and manifest/determinism work.

---

## Phase 5: Planning Engine (Upcoming NEXT)

Objective: Introduce an immutable `PakPlan` capturing exact offsets, sizes, alignment padding, and ordering decisions prior to any file I/O; eliminate duplicated offset math inside writer.

Key Responsibilities:

- Assign contiguous regions for each resource category (texture, buffer, audio, etc.)
- Compute per-descriptor offsets (assets, resource table entries, directory entries)
- Track padding inserted (per-section + total) and supply statistics to manifest
- Provide invariants: no overlap, alignment satisfied, monotonic offsets, size bounds
- Serve as single source of truth for writer & manifest

Data Structures (Planned):

- PakPlan(header: HeaderInfo, regions: list[RegionPlan], resource_tables: list[TablePlan], assets: list[AssetPlan], directory: DirectoryPlan, footer: FooterPlan, padding_stats: PaddingStats, hash_inputs: SpecHashInputs)
- RegionPlan(name, offset, size, alignment, padding_before, padding_after)
- AssetPlan(key, type, descriptor_offset, descriptor_size, extra_indices, warnings)

Algorithm Outline:

1. Collect normalized resources grouped by type; stable sort (name or spec order) under determinism flag.
2. Assign region base offsets sequentially honoring alignment (global + per-type).
3. For each region: accumulate data size; compute trailing padding if needed.
4. Build resource tables & compute entry offsets (implicit or separate region depending on format).
5. Allocate asset descriptor block (concatenate descriptors) with alignment; record each asset's descriptor_offset.
6. Allocate directory (fixed entry size * asset_count).
7. Footer reserved at fixed size from end; compute file_size = footer_offset + footer_size.
8. Validate invariants; emit diagnostics (overlaps, alignment slack > threshold).

CLI Impact:

- `--dry-run` prints human + JSON representation of PakPlan; no file writes.
- Writer upgraded to accept PakPlan and stream sections exactly once.

Testing Strategy:

- Unit tests for planner: alignment scenarios, empty inputs, large counts boundary.
- Property tests (Hypothesis) ensuring invariants hold under randomized counts/alignment seeds.
- Snapshot test comparing plan JSON (excluding volatile ordering when non-deterministic mode used).

Risks & Mitigations:

- Misalignment producing subtle CRC drift → mitigate with invariant asserts + binary diff harness.
- Overly coupled planner/writer → keep writer pure consumer (no mutation of plan) and enforce immutability.

Success Criteria:

- Bitwise identity for legacy vs plan-driven builds across existing golden fixtures.
- Dry-run offsets exactly match subsequent built file.
- Snapshot helper (`snapshot_helper.assert_matches_snapshot`) enables opt-in automatic updates via `PAKGEN_UPDATE_SNAPSHOTS`; basic and multi-resource deterministic plan snapshots maintained.

---

## Forward Roadmap Adjustments

Planned sequence after Phase 5:

1. Phase 6 Writer Refactor (consumes PakPlan, deletes legacy offset code)
2. Phase 7 Manifest (reuses PakPlan + CRC + statistics)
3. Phase 8 Determinism (finalize ordering + hash inputs; expose spec_hash field)
4. Phase 9 Extended Metrics (padding, largest resource, distribution histograms)
5. Phase 10 Extensibility (registry for new resource/asset types)
6. Phase 11 Hardening & Fuzz (randomized spec gen, mutation fuzz)

---

## Backlog (Updated)

Additions / Reprioritized:

- DescriptorSanity validation stage (post-planner) for size/popcount consistency
- Auto-generation of `error_codes.md` from errors.py (ensure coverage parity)
- Optional TOML manifest output
- Incremental build prototype (manifest diff to skip unchanged blobs)
- Compression (per-region, likely post-manifest for repeatable hashing)
- Signed footer (public key signature)
- Streaming / partial load index (chunk table)

Deferred / Lower Priority:

- enums.py (only if value set inflation becomes unwieldy)
- Parallel staging (await performance evidence of need)

---

## Risk Mitigation (Refreshed)

| Risk | Current Mitigation | Additional Action (Planned) |
|------|--------------------|------------------------------|
| Divergence from C++ layout | Central constants + selftest | Add CI header size introspection script (future) |
| Hidden offset math duplication | Planner centralization (Phase 5) | Writer refactor removes residual math |
| Non-determinism creeping in | Determinism flag + stable sorts | Hash spec + manifest diff in CI |
| Hard to add new types | Registry design placeholder | Formal registry API + example plugin |
| Validation blind spots | Multi-stage pipeline | DescriptorSanity + plan-aware checks |
| Golden drift false negatives | CRC + byte diff snapshots | Add plan JSON snapshot layer |

---

## Quick Start (Current State Pre-Planning)

```bash
# Validate spec only (multi-stage structural/semantic)
python generate_pak.py --validate-only path/to/spec.yaml

# Generate pak (legacy writer path still active prior to planner)
python generate_pak.py path/to/spec.yaml out/file.pak

# Inspector parity / CRC check
python inspector.py out/file.pak --json
```

## Quick Start (Post Phase 6 Target)

```bash
# Validate only (full pipeline)
pakgen validate spec.yaml

# Dry run planning (no write)
pakgen plan --dry-run --json spec.yaml

# Build pak + manifest deterministically
pakgen build --deterministic --emit-manifest out/level01.manifest.json spec.yaml out/level01.pak
```

---

*End of updated roadmap.*
