# Physics Data Architecture Implementation Plan

This plan is the migration contract for `physics.md`. It is ordered, blocking, and
zero-shim: no legacy identity, schema, cooker, loader, or runtime fallback paths
may remain once all phases are complete.

Status: `in_progress` until every phase exit gate has code + docs + validation evidence.
Phase status summary: **Phase 1 complete**; Phases 2-8 pending.

## Phase 1 Progress Snapshot (2026-03-04)

Implemented in this iteration (Phase 1 exit gate met):

- Added deterministic `AssetKey` derivation API in code:
  `AssetKey::FromVirtualPath(virtual_path)` using
  `xxHash3-128(UTF-8 bytes)`.
- Removed cooker free-function minting (`MakeDeterministicAssetKey`) and
  standardized call sites directly on `AssetKey::FromVirtualPath(...)`.
- Added shared canonical virtual-path validator and wired it into resolver and
  cooker/content canonical checks.
- Added canonical-path validation test matrix (valid/invalid examples from
  design) and deterministic path-key tests.
- Removed `AssetKeyPolicy` and all `asset_key_policy`/`kRandom` branches from
  import options, jobs, and pipelines.
- Removed `GenerateAssetGuid()` from `AssetKey.h` and migrated all remaining
  code call sites:
  - asset identity paths now use deterministic canonical-path key derivation.
  - non-asset UUID generation paths now use `Uuid::Generate()`.
- Updated scripting-sidecar tests to deterministic key semantics (same
  canonical path -> same key).
- Added/kept resolver precedence + tombstone tests in
  `VirtualPathResolver_test.cpp`.
- Constrained `AssetKey` creation API:
  - converted `AssetKey` from UUID-wrapper semantics to a dedicated 16-byte
    value type (no `NamedType<Uuid,...>` inheritance).
  - removed constructor-based UUID minting paths.
  - removed free-function identity minting path.
  - standardized on `AssetKey::FromVirtualPath(...)` (minting) and
    `AssetKey::FromBytes(...)` (rehydration).
  - kept `AssetKey` byte storage private; no mutable byte access API was added.

Validation evidence captured in this iteration:

- Focused build validation:
  - `cmake --build out/build-vs --config Debug --target Oxygen.Data.All.Tests`
    -> **success**
  - `cmake --build out/build-vs --config Debug --target oxygen-examples-demoshell`
    -> **success**
  - `cmake --build out/build-vs --config Debug --target oxygen-examples-async`
    -> **success**
  - `cmake --build out/build-vs --config Debug --target oxygen-examples-inputsystem`
    -> **success**
  - `cmake --build out/build-vs --config Debug --target oxygen-examples-lightbench`
    -> **success**
  - `cmake --build out/build-vs --config Debug --target oxygen-examples-multiview`
    -> **success**
  - `cmake --build out/build-vs --config Debug --target oxygen-examples-physics`
    -> **success**
  - `cmake --build out/build-vs --config Debug --target oxygen-examples-texturedcube`
    -> **success**
- Focused runtime tests:
  - `Oxygen.Data.All.Tests.exe --gtest_filter=AssetKey*` -> **2 passed**
  - `Oxygen.Content.LooseCooked.Tests.exe --gtest_filter=VirtualPathResolverTest*` -> **7 passed**
- Regression validation after Phase 1 test-fix completion:
  - `cmake --build out/build-vs --config Debug --target Oxygen.Cooker.AsyncImportScript.Tests` -> **success**
  - `Oxygen.Cooker.AsyncImportScript.Tests.exe --gtest_color=no` -> **78 passed**
  - `Oxygen.Content.AssetLoader.Tests.exe --gtest_filter=AssetLoaderSceneTest.LoadAssetSceneWithPhysicsSidecarLoadsV7` -> **1 passed**
  - `Oxygen.Scripting.Module.Tests.exe --gtest_filter=ScriptingModuleTest.SceneSlotLocalStatePersistsAcrossGameplayFrames` -> **1 passed**
- Post-change audit checks:
  - `rg -n "GenerateAssetGuid" src Examples --glob "!**/*.md"` -> no hits.
  - `rg -n "AssetKeyPolicy::|AssetKeyPolicy|asset_key_policy" src Examples --glob "!**/*.md"` -> no hits.
  - `rg -n "MakeAssetKeyFromCanonicalVirtualPath|MakeDeterministicAssetKey" src Examples --glob "!**/*.md"` -> no hits.
  - `rg -n "AssetKey[^\\n]*Uuid::Generate\\(|AssetKey\\s*\\{\\s*oxygen::Uuid|AssetKey\\s*\\{\\s*Uuid" src Examples --glob "!**/*.md"` -> no hits.
  - `rg -n "as_writable_bytes\\s*\\(\\s*[^\\n]*AssetKey" src Examples --glob "!**/*.md"` -> no hits.

Design alignment note:

- No Phase 1 design deviation was identified relative to `physics.md`; design
  document update is not required for this closure.

## Non-Negotiable Completion Protocol

- [ ] A phase is only marked complete when implementation exists in code.
- [ ] Required docs/plans are updated in the same change set.
- [ ] Validation evidence is attached (tests run, or explicit unvalidated delta).
- [ ] If validation is not executed, phase status remains `in_progress`.
- [ ] No backward-compatibility shims, dual-write, or parallel legacy paths unless explicitly approved.
- [ ] If implementation scope is found insufficient, update design + plan docs before continuing code changes.

## Coverage Matrix (Design -> Migration Track)

| `physics.md` design area | Mandatory migration outcome | Phase |
| --- | --- | --- |
| Asset identity (canonical virtual path + deterministic `AssetKey`) | Canonical path contract enforced, resolver-driven keys, UUID path removed | 1 |
| Cross-artifact references + versioning contracts V-1/V-2/V-3 | Cross-unit references use `AssetKey`; positional indices only within regeneration units | 2, 4, 6 |
| Integrity (SHA-256 only for artifacts) | All cooked descriptor/resource integrity fields upgraded to 32-byte SHA-256 | 2, 4 |
| L2 artifact taxonomy and struct contract | Fixed-layout descriptors, trailing-array policy, backend scalar unions, strict ABI asserts | 3 |
| Side-table emission registry (rows 1-14) | Sidecar + physics table/data physical emission matches documented locations | 4 |
| Loose Cooking and PAK Builder contracts | Loose index authority, incremental-safe updates, PAK relocation without recook | 4, 5 |
| L3 hydration order and runtime handle policy | Identity guard, dependency-order hydration, backend-specific restore, session-scoped handles | 6 |
| Zero-tolerance cleanup | Legacy/random-key/JSON fallback/index misuse removed | 7 |

## Phase 1 - Identity Foundation and Canonical Path Enforcement

- [x] Replace non-deterministic identity generation with deterministic keying:
- [x] Implement `AssetKey = xxHash3-128(UTF-8 canonical virtual path)` (16-byte storage).
- [x] Migrate all `GenerateAssetGuid()` call sites (~36 noted in design) to deterministic path-key derivation (or explicit non-asset `Uuid::Generate()` where asset identity is not involved).
- [x] Delete `GenerateAssetGuid()` and block reintroduction (compile-time guard/lint rule).
- [x] Implement canonical virtual path validation rules from `physics.md`:
- [x] Absolute path (`/`), no empty segments/trailing slash, no `.` or `..`.
- [x] Allowed character set enforcement and dot-in-leaf-only rule.
- [x] Exact case preservation and <=512-byte length enforcement.
- [x] Implement/validate mount-root behavior in resolver:
- [x] Supported roots (`/Engine`, `/Game`, `/.cooked`, `/Pak/<name>`).
- [x] Source priority order and conflict logging.
- [x] Tombstone masking behavior for patch manifests.

Phase 1 exit gate:

- [x] Identity-related code paths no longer produce random GUIDs.
- [x] Canonical path validation has automated tests (valid/invalid matrix from spec examples).
- [x] Resolver priority/tombstone behavior has deterministic tests.
- [x] `AssetKey` creation API is constrained so canonical-path/resolver identity cannot be bypassed in production identity flows.

## Phase 2 - Reference and Integrity Contract Migration (Cross-Cutting)

- [ ] Enforce reference contracts across data/model/tooling code:
- [ ] Virtual path and `AssetKey` references carry no generation/version stamp (V-1).
- [ ] Positional indices remain only within regeneration units (V-2).
- [ ] Any cross-regeneration-unit reference is `AssetKey` only (V-3).
- [ ] Eliminate container-level ordinal coupling:
- [ ] Physics resource descriptor table and container asset catalog are `AssetKey` keyed.
- [ ] Incremental loose recook updates one asset without invalidating unrelated indices.
- [ ] Upgrade artifact integrity fields to SHA-256 (32 bytes) where currently narrower:
- [ ] `AssetHeader::content_hash`.
- [ ] `PhysicsResourceDesc` integrity hash.
- [ ] Loose index `AssetEntry` descriptor integrity hash.
- [ ] Any remaining asset-level hash fields in content descriptors/tables.
- [ ] Remove prohibited asset-integrity algorithms (CRC variants, MD5/SHA-1, non-crypto hash misuse).
- [ ] Keep CRC32 limited to whole-container (PAK file-level checksum) only.

Phase 2 exit gate:

- [ ] No cross-unit positional reference path remains in code or serialized layout.
- [ ] All asset-integrity fields are 32-byte SHA-256 and covered by tests.
- [ ] Algorithm usage audit proves prohibited hashes are not used for asset integrity.

## Phase 3 - L2 Schema and ABI Update (`PakFormat_physics.h` and Related Formats)

- [ ] Implement/finalize backend scalar unions per binding record type:
- [ ] `RigidBodyBackendScalars`, `CharacterBackendScalars`, `SoftBodyBackendScalars`, `JointBackendScalars`, `VehicleWheelBackendScalars`.
- [ ] Reconcile soft-body backend scalar fields with final spec values before code freeze.
- [ ] Implement trailing-array descriptor contracts:
- [ ] `CompoundShapeChildDesc` trailing array.
- [ ] `SoftBodyBindingRecord` pinned and kinematic vertex trailing arrays (`count + self-relative byte_offset` pairs).
- [ ] Ensure vehicle wheels use shared side-table slice (`wheel_slice_offset`, `wheel_slice_count`) and not trailing array.
- [ ] Update fixed-layout descriptors/binding records to match design:
- [ ] `CollisionShapeAssetDesc`, `PhysicsMaterialAssetDesc`, `PhysicsSceneAssetDesc`, `PhysicsComponentTableDesc`, `PhysicsResourceDesc`.
- [ ] `RigidBodyBindingRecord`, `ColliderBindingRecord`, `CharacterBindingRecord`, `SoftBodyBindingRecord`, `JointBindingRecord`, `VehicleBindingRecord`, `VehicleWheelBindingRecord`, `AggregateBindingRecord`.
- [ ] Apply explicit packing discipline:
- [ ] Stable member ordering, explicit `reserved` bytes, 16-byte multiple sizing where required.
- [ ] Full `static_assert` coverage for size/offset invariants and trailing-array navigation fields.

Phase 3 exit gate:

- [ ] Binary layout asserts pass for every touched descriptor/record.
- [ ] Golden-file serialization/deserialization tests validate offsets, counts, and padding.
- [ ] Updated schema documentation reflects final struct field names and sizes.

## Phase 4 - Cooker and Loose Layout Emission (`Cooker/Import/Internal`)

- [ ] Implement/update cook pipelines for all physics artifact classes:
- [ ] Enforce schema-first validation for all L1 physics source payloads (manual checks only for non-schema constraints).
- [ ] Maintain an explicit L1->L2 field-mapping checklist for rigid body, collider, character, soft body, joint, vehicle, vehicle wheel, and aggregate records.
- [ ] Physics materials (`.opmat`) fixed descriptor emission with SHA-256 patching.
- [ ] Collision shapes (`.ocshape`) analytic vs backend-cooked split, including compound child trailing array emission.
- [ ] Physics sidecar (`.opscene`) with full component-table directory emission.
- [ ] Backend-cooked blob classes (`shape`, `constraint`, `vehicle`, `soft-body`) into `physics.table` + `physics.data`.
- [ ] Enforce side-table emission registry order/location exactly as design rows 1-14.
- [ ] Emit and patch `PhysicsSceneAssetDesc` staleness fields:
- [ ] `target_scene_key`.
- [ ] `target_scene_content_hash` (SHA-256 of paired `.oscene` artifact).
- [ ] `target_node_count`.
- [ ] Ensure loose index (`*.oxlcidx`) remains authoritative:
- [ ] Asset entries include key, descriptor-relative path, virtual path, type tag, descriptor hash.
- [ ] File records include physics table/data and other resource files.
- [ ] Incremental recook correctness:
- [ ] Re-cooking one asset updates only affected descriptor/blob/index records.
- [ ] No container-wide ordinal drift dependency.

Phase 4 exit gate:

- [ ] End-to-end cooker tests cover analytic shape, non-analytic shape, joint blob, soft-body blob, vehicle blob paths.
- [ ] Complex-scene fixture validates component directory + wheel table + trailing arrays.
- [ ] Incremental recook test proves unaffected assets remain stable.

## Phase 5 - PAK Builder and Packaging Relocation

- [ ] Ensure PAK builder consumes cooked loose layout without recooking:
- [ ] Reads index as source of truth for assets/file records.
- [ ] Concatenates descriptor regions and physics resource regions.
- [ ] Rewrites offsets from loose-relative to PAK-relative where required.
- [ ] Produces self-contained mountable bundle with catalog/directory structures.
- [ ] Validate behavior across base + patch PAK layering for resolver priority expectations.

Phase 5 exit gate:

- [ ] Loose->PAK roundtrip tests confirm byte-for-byte payload preservation (except expected relocated offsets).
- [ ] Runtime can load both loose and PAK outputs with identical physics behavior.
- [ ] PAK build path contains no cooker logic.

## Phase 6 - Runtime Hydration and Simulation Contract (L3)

- [ ] Integrate physics module into engine frame orchestration (fixed simulation + transform propagation phases).
- [ ] Implement strict sidecar staleness guard before hydration:
- [ ] Reject on `target_scene_key`, `target_scene_content_hash`, or `target_node_count` mismatch.
- [ ] Hydrate in dependency order from design:
- [ ] Shapes -> Materials -> RigidBodies -> Colliders -> Characters -> SoftBodies -> Joints -> Vehicles -> Aggregates.
- [ ] Branch restore path strictly by backend format tag (`Jolt` vs `PhysX` cooked blobs).
- [ ] Maintain runtime caches/maps by `AssetKey` and scene-node-index mappings for live handles.
- [ ] Enforce runtime handle policy:
- [ ] Session-scoped handles only; never persisted.
- [ ] No raw backend pointer leakage across L3 boundary abstractions.
- [ ] Mid-session invalidation behavior follows backend/owner-responsibility contract.
- [ ] Implement simulation-to-scene sync for body and vehicle wheel transforms.

Phase 6 exit gate:

- [ ] Hydration tests cover every component table type and both backends (where available).
- [ ] Mismatch guard tests hard-fail invalid sidecars.
- [ ] Session teardown invalidates all handles; sync path verified under object removal scenarios.

## Phase 7 - Zero-Tolerance Cleanup and Legacy Removal

- [ ] Remove legacy/duplicate pipelines that bypass binary descriptors.
- [ ] Remove runtime fallback parsing of L1 JSON for physics instantiation.
- [ ] Remove any cross-collection ordinal reference remnants.
- [ ] Remove deprecated handle wrappers that contradict current runtime-handle policy.
- [ ] Remove compatibility code paths that keep old schema alive in production.

Phase 7 exit gate:

- [ ] Search-based audit confirms no deprecated APIs/symbols remain.
- [ ] Loader/cooker only accept finalized schema paths.
- [ ] Migration notes document intentional breaking changes and no-shim policy.

## Phase 8 - Final Validation and Release Gate

- [ ] Build validation:
- [ ] Full project build with required toolchain flags (including `/GR-` and C++23 expectations where applicable).
- [ ] Zero warnings for packing/alignment/serialization in touched physics content code.
- [ ] Determinism and parity validation:
- [ ] Same canonical virtual path -> same `AssetKey` across runs/machines.
- [ ] Repeat cooks produce stable descriptor/blob hashes when inputs are unchanged.
- [ ] Scene hydration parity across loose and PAK modes.
- [ ] Documentation validation:
- [ ] `physics.md` and this plan remain aligned after implementation deltas.
- [ ] Any scope correction discovered during implementation updates docs first (then code).

Final release gate:

- [ ] Every phase above has code, docs, and validation evidence.
- [ ] Remaining gaps list is empty.
- [ ] Status may be advanced from `in_progress` only after all evidence is recorded.
