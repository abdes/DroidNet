# PakGen Supersession Architecture Specification (Descriptor-First, Job/Pipeline Locked)

## 0. Status Tracking

This is the target architecture/spec contract for replacing PakGen with the C++ import + pak pipeline.

Live implementation status, evidence, and remaining work are tracked in:

1. `design/pakgen_supersession_impl.md`

Current baseline snapshot (2026-03-01):

1. Import domains already integrated: `texture` (image-driven), `fbx`, `gltf`, `script`, `script-sidecar`, `physics-sidecar`, `input`.
2. Import manifest + schema and C++ schema embedding infrastructure already exist.
3. C++ Pak builder/planner/writer exists (`PakBuilder`, `PakPlanBuilder`, `PakWriter`).
4. Missing supersession piece: first-class JSON descriptor import domains for `texture`, `buffer`, `material`, `geometry`, `scene`, then hard cutover of tooling/workflows away from PakGen.

## 1. Objective and Scope

Goal:

1. Supersede PakGen with one C++-native content pipeline driven by JSON descriptors (schema-validated) and import manifest orchestration, producing loose cooked output and final `.pak` via C++ APIs.

In scope:

1. First-class JSON descriptor domains (peer domains):
   - texture descriptor
   - buffer descriptor
   - material descriptor
   - geometry descriptor
   - scene descriptor
2. Manifest orchestration with explicit DAG dependencies (`id`, `depends_on`) across all descriptor and existing job types.
3. Systematic JSON schema validation for descriptor/manifest ingress.
4. C++ Pak build flow integration (manifest/import outputs -> `PakBuilder`).
5. PakGen deprecation and eventual removal gates.

Out of scope:

1. New binary PAK format versions.
2. New runtime asset categories beyond existing data contracts.
3. DCC-specific importer redesign (FBX/glTF paths remain valid and can coexist during transition).

## 2. Hard Constraints

1. Import execution remains `ImportJob -> Pipeline` only.
2. Descriptor domains are first-class peers. `scene` must not implicitly cook `geometry`/`material`/`texture`/`buffer`.
3. Schema validation is mandatory at descriptor ingress. Manual validation is allowed only for rules that cannot be represented in JSON schema (cross-file refs, mounted-content existence, type checks, DAG/scheduling constraints).
4. Schema validation diagnostics must be collected and surfaced as import diagnostics (author-helpful, bounded volume).
5. All schemas are module-owned source files and embedded via build-time generated headers; no hand-written embedded schema blobs.
6. Each owner module declares its schema install rules in its own `CMakeLists.txt`.
7. No compatibility hacks that preserve PakGen-specific behavior when it conflicts with C++ importer contracts.

## 3. Baseline Repository Facts

| Fact | Evidence |
| --- | --- |
| Manifest parser recognizes current job types (`texture`, `texture-descriptor`, `material-descriptor`, `fbx`, `gltf`, `script`, `script-sidecar`, `physics-sidecar`, `input`) | `src/Oxygen/Cooker/Import/ImportManifest.cpp` |
| ImportTool commands currently mirror those job types | `src/Oxygen/Cooker/Tools/ImportTool/main.cpp` |
| C++ Pak path exists and is callable | `src/Oxygen/Cooker/Pak/PakBuilder.h`, `src/Oxygen/Cooker/Pak/PakPlanBuilder.h`, `src/Oxygen/Cooker/Pak/PakWriter.h` |
| PakGen still exists as tool target | `src/Oxygen/Cooker/Tools/PakGen/CMakeLists.txt` |
| Schema embedding/install infra exists | `cmake/JsonSchemaHelpers.cmake`, `cmake/GenerateEmbeddedJsonSchemas.cmake`, `src/Oxygen/Cooker/CMakeLists.txt` |
| Shared schema validation utility exists | `src/Oxygen/Cooker/Import/Internal/Utils/JsonSchemaValidation.h` |

## 4. Decision

PakGen supersession is done by introducing descriptor-first import domains as peers and converging all packaging on C++ APIs:

1. Descriptor import produces loose cooked descriptors/resources with deterministic keys and explicit diagnostics.
2. `PakBuilder` consumes loose cooked outputs for final `.pak` creation.
3. PakGen transitions to non-default, then removed after parity gates pass.

## 5. Descriptor Domain Model (First-Class Peers)

## 5.1 Domain Inventory

| Domain | Manifest `type` | Schema file (module-owned source) | Primary output |
| --- | --- | --- | --- |
| Texture descriptor | `texture-descriptor` | `src/Oxygen/Cooker/Import/Schemas/oxygen.texture-descriptor.schema.json` | texture resources (`textures.table`/`textures.data`) |
| Buffer descriptor | `buffer-descriptor` | `src/Oxygen/Cooker/Import/Schemas/oxygen.buffer-descriptor.schema.json` | buffer resources (`buffers.table`/`buffers.data`) |
| Material descriptor | `material-descriptor` | `src/Oxygen/Cooker/Import/Schemas/oxygen.material-descriptor.schema.json` | `AssetType::kMaterial` descriptor (`.omat`) |
| Geometry descriptor | `geometry-descriptor` | `src/Oxygen/Cooker/Import/Schemas/oxygen.geometry-descriptor.schema.json` | `AssetType::kGeometry` descriptor (`.ogeo`) |
| Scene descriptor | `scene-descriptor` | `src/Oxygen/Cooker/Import/Schemas/oxygen.scene-descriptor.schema.json` | `AssetType::kScene` descriptor (`.oscene`) |

## 5.2 Domain Independence Rules

1. Material descriptor references textures/resources; it does not trigger texture cooking implicitly.
2. Geometry descriptor references buffers/materials; it does not trigger material/texture/buffer cooking implicitly.
3. Scene descriptor references geometry/material/script/input/physics sidecars; it does not trigger upstream cooking implicitly.
4. Cross-domain ordering is explicit through manifest DAG dependencies and resolvable mounted/inflight content.

## 6. Target Import Architecture

```mermaid
flowchart LR
  A[Manifest + Defaults] --> B[DAG Build + Validation]
  B --> C[Descriptor Request Builders]
  C --> D[Domain Import Jobs]
  D --> E[Domain Pipelines]
  E --> F[Asset/Resource Emitters + Index Registry]
  F --> G[Loose Cooked Output]
  G --> H[PakBuilder]
  H --> I[PakWriter]
  I --> J[output.pak]
```

Execution invariants:

1. Every descriptor domain has its own request-builder + job + pipeline entry point.
2. Shared helpers are used for schema diagnostics, reference resolution, and common emission patterns; domain logic stays domain-local.
3. No monolithic mega-pipeline that handles all domains in one file.

## 7. Validation and Diagnostics Policy

## 7.1 Validation Order

1. Parse JSON document.
2. Validate against domain schema (mandatory).
3. Run manual validation only for non-schema-enforceable rules:
   - referenced asset/resource existence
   - referenced type compatibility
   - DAG dependency integrity and scheduling constraints
   - mounted/inflight ambiguity resolution

## 7.2 Diagnostics Policy

1. Schema issues are emitted through import diagnostics using stable domain codes.
2. Use bounded issue emission (for example first N issues + overflow summary) to keep diagnostics actionable.
3. Do not duplicate the same error in multiple forms.

Diagnostic namespaces:

1. `texture.descriptor.*`
2. `buffer.descriptor.*`
3. `material.descriptor.*`
4. `geometry.descriptor.*`
5. `scene.descriptor.*`
6. `manifest.descriptor.*`

## 8. Schema Ownership, Embedding, and Distribution

1. Schema source files are owned by the module that owns the descriptor contract (current owner: `Oxygen.Cooker` import module).
2. Build-time embedding uses generated headers only (`oxygen_embed_json_schemas(...)`).
3. Runtime/compiler includes use generated headers under build output, not hand-authored header strings.
4. Installed schema artifacts use stable, versionless names by default:
   - `oxygen.texture-descriptor.schema.json`
   - `oxygen.buffer-descriptor.schema.json`
   - `oxygen.material-descriptor.schema.json`
   - `oxygen.geometry-descriptor.schema.json`
   - `oxygen.scene-descriptor.schema.json`
5. Schema semantic version lives inside each schema (`$id`/metadata), not in filename.

## 9. Manifest Orchestration Contract

For descriptor jobs:

1. `id` is required.
2. `depends_on` is optional and explicit.
3. Unknown keys are rejected per job type whitelist.
4. DAG failures are deterministic (`missing target`, `cycle`, `predecessor failed`).

The manifest remains the orchestration layer, not a data payload for descriptor internals.

## 10. PakGen Cutover Strategy

## 10.1 Cutover Stages

1. Stage A: descriptor domains integrated with tests; PakGen still available.
2. Stage B: C++ `PakBuilder` workflow exposed as first-class tool path and used by official examples/docs.
3. Stage C: parity gates pass (functional, diagnostics, determinism, install/schema).
4. Stage D: PakGen removed from default build and CI required path.
5. Stage E: PakGen code removed after one full release-cycle-equivalent confidence window (or explicit approval for immediate removal).

## 10.2 Parity Gates (Must Pass Before Stage D)

1. All descriptor domains have schema + request builder + job/pipeline + tests.
2. Cross-domain manifest DAG scenarios are covered in tests.
3. C++ pak flow covers same asset/resource composition needed by current content sets.
4. No unresolved PakGen-only dependency remains in examples or docs.

## 11. Definition of Done

Supersession is complete only when all are true:

1. `texture-descriptor`, `buffer-descriptor`, `material-descriptor`, `geometry-descriptor`, `scene-descriptor` are implemented and tested as peer domains.
2. Import manifest + schema fully describe and validate descriptor orchestration.
3. C++ `PakBuilder` is the authoritative pack path in docs/CI.
4. PakGen is no longer required for standard workflows and is removed or explicitly quarantined behind non-default tooling.
5. `design/pakgen_supersession_impl.md` reports 100% with evidence for every phase.
