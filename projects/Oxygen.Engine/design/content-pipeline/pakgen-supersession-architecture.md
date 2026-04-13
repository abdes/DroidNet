# PakGen Supersession Architecture Specification (Descriptor-First, Job/Pipeline Locked)

**Date:** 2026-03-02
**Status:** Design / Architecture

## 0. Status Tracking

This is the target architecture/spec contract for replacing PakGen with the C++ import + pak pipeline.

Live implementation status, evidence, and remaining work are tracked in:

1. `design/content-pipeline/pakgen-supersession-implementation-plan.md`

Current baseline snapshot (2026-03-01):

1. Import domains already integrated: `texture` (image-driven), `fbx`, `gltf`, `script`, `script-sidecar`, `physics-sidecar`, `input`, `buffer-container`.
2. Import manifest + schema and C++ schema embedding infrastructure already exist.
3. C++ Pak builder/planner/writer exists (`PakBuilder`, `PakPlanBuilder`, `PakWriter`).
4. Missing supersession piece: first-class JSON descriptor import domains for `texture`, `material`, `geometry`, `scene` (with container-owned buffer descriptors inside geometry), then hard cutover of tooling/workflows away from PakGen.

## 1. Objective and Scope

Goal:

1. Supersede PakGen with one C++-native content pipeline driven by JSON descriptors (schema-validated) and import manifest orchestration, producing loose cooked output and final `.pak` via C++ APIs.

In scope:

1. First-class JSON descriptor domains (peer domains):
   - texture descriptor
   - material descriptor
   - geometry descriptor
   - scene descriptor
2. Container-owned buffer descriptor objects inside geometry descriptors:
   - `buffers[]` entries define buffer metadata and source `.buffer.bin` location
   - `buffers[i].views[]` entries define the container-relevant slices/views
   - no standalone top-level buffer descriptor domain contract
3. Manifest orchestration with explicit DAG dependencies (`id`, `depends_on`) across all descriptor and existing job types.
4. Systematic JSON schema validation for descriptor/manifest ingress.
5. C++ Pak build flow integration (manifest/import outputs -> `PakBuilder`).
6. PakGen deprecation and eventual removal gates.

Out of scope:

1. New binary PAK format versions.
2. New runtime asset categories beyond existing data contracts.
3. DCC-specific importer redesign (FBX/glTF paths remain valid and can coexist during transition).

## 2. Hard Constraints

1. Import execution remains `ImportJob -> Pipeline` only.
2. Descriptor domains are first-class peers. `scene` must not implicitly cook `geometry`/`material`/`texture`.
3. Geometry owns buffer descriptor objects (`buffers[]`) and may emit buffer resources from those local definitions; no standalone buffer authoring contract.
4. Buffer references must use canonical buffer virtual paths (`.obuf`), not container-local identifiers.
5. Equivalent buffers shared across containers (same semantic descriptor + payload identity) must dedupe to one cooked buffer resource in a cooked root.
6. Schema validation is mandatory at descriptor ingress. Manual validation is allowed only for rules that cannot be represented in JSON schema (cross-file refs, mounted-content existence, type checks, DAG/scheduling constraints).
7. Schema validation diagnostics must be collected and surfaced as import diagnostics (author-helpful, bounded volume).
8. All schemas are module-owned source files and embedded via build-time generated headers; no hand-written embedded schema blobs.
9. Each owner module declares its schema install rules in its own `CMakeLists.txt`.
10. No compatibility hacks that preserve PakGen-specific behavior when it conflicts with C++ importer contracts.

## 3. Baseline Repository Facts

| Fact | Evidence |
| --- | --- |
| Manifest parser recognizes current job types (`texture`, `texture-descriptor`, `material-descriptor`, `buffer-container`, `fbx`, `gltf`, `script`, `script-sidecar`, `physics-sidecar`, `input`) | `src/Oxygen/Cooker/Import/ImportManifest.cpp` |
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
| Material descriptor | `material-descriptor` | `src/Oxygen/Cooker/Import/Schemas/oxygen.material-descriptor.schema.json` | `AssetType::kMaterial` descriptor (`.omat`) |
| Geometry descriptor | `geometry-descriptor` | `src/Oxygen/Cooker/Import/Schemas/oxygen.geometry-descriptor.schema.json` | `AssetType::kGeometry` descriptor (`.ogeo`), referenced buffer resources (`buffers.table`/`buffers.data`), and per-buffer metadata sidecars (`.obuf`) from container-owned `buffers[]` |
| Scene descriptor | `scene-descriptor` | `src/Oxygen/Cooker/Import/Schemas/oxygen.scene-descriptor.schema.json` | `AssetType::kScene` descriptor (`.oscene`) |

## 5.2 Domain Independence Rules

1. Material descriptor references textures/resources; it does not trigger texture cooking implicitly.
2. Geometry descriptor resolves its own container-owned `buffers[]` and `views[]`, and references materials; it does not trigger other container imports implicitly.
3. Scene descriptor references geometry/material/script/input/physics sidecars; it does not trigger upstream cooking implicitly.
4. Cross-domain ordering is explicit through manifest DAG dependencies and resolvable mounted/inflight content.

## 5.3 Buffer Descriptor Subdocument Contract

1. A buffer descriptor is a JSON object inside a container's `buffers[]`.
2. Each `buffers[]` entry declares a canonical `virtual_path` for its emitted metadata sidecar (`.obuf`), and that path is the unique identity handle for authored buffer metadata.
3. A buffer view descriptor is a JSON object inside `buffers[i].views[]`.
4. Buffer data is always external via `buffers[i].uri` to `.buffer.bin`.
5. Inline JSON payloads (base64/data-URI/raw arrays) are not part of the standard contract.
6. Geometry mesh-level primary buffer refs use concise fields:
   - `buffers.vb_ref`: vertex buffer virtual path (`.obuf`)
   - `buffers.ib_ref`: index buffer virtual path (`.obuf`)
7. Submesh-level material refs use concise field:
   - `material_ref`: material virtual path (`.omat`)
8. Submesh view refs use a single paired selector:
   - `view_ref`: view name resolved against both `vb_ref` and `ib_ref`
   - reserved implicit selector `__all__` always means whole-buffer view on both sides
9. Mesh/submesh references must use virtual paths (through `vb_ref`/`ib_ref`), never local buffer IDs.
10. Buffer view name `__all__` is reserved and cannot be explicitly declared in `buffers[i].views[]`.
11. Equivalent cross-container buffer definitions are allowed in authoring input, but they must collapse to one cooked buffer resource and one `.obuf` at the canonical `virtual_path`.
12. Multiple containers may reference the same external `.buffer.bin` file.
13. Deterministic resolution chain is required:
   - buffer reference -> canonical buffer virtual path
   - canonical virtual path -> resolve mounted source -> load `.obuf`
   - parse `.obuf` -> `resource_index` + `BufferResourceDesc`
   - use resolved source scope + `resource_index` to read exact `buffers.table` row and payload in `buffers.data`
14. Cross-container dedupe is mandatory: if two containers resolve to equivalent buffer identity, the cooker emits one `buffers.table` entry, one payload region, and one `.obuf`; all references resolve through that single canonical buffer virtual path.

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
2. `material.descriptor.*`
3. `geometry.descriptor.*`
4. `geometry.buffer.*`
5. `scene.descriptor.*`
6. `manifest.descriptor.*`

## 8. Schema Ownership, Embedding, and Distribution

1. Schema source files are owned by the module that owns the descriptor contract (current owner: `Oxygen.Cooker` import module).
2. Build-time embedding uses generated headers only (`oxygen_embed_json_schemas(...)`).
3. Runtime/compiler includes use generated headers under build output, not hand-authored header strings.
4. Installed schema artifacts use stable, versionless names by default:
   - `oxygen.texture-descriptor.schema.json`
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
   - descriptor domains: `texture-descriptor`, `material-descriptor`, `geometry-descriptor`, `scene-descriptor`
   - geometry coverage includes container-owned buffer descriptor subdocuments (`buffers[]`/`views[]`) with virtual-path-based references and emitted `.obuf` metadata
2. Cross-domain manifest DAG scenarios are covered in tests.
3. C++ pak flow covers same asset/resource composition needed by current content sets.
4. No unresolved PakGen-only dependency remains in examples or docs.

## 11. Definition of Done

Supersession is complete only when all are true:

1. `texture-descriptor`, `material-descriptor`, `geometry-descriptor`, `scene-descriptor` are implemented and tested as peer domains.
   - container-owned buffer descriptors are implemented and tested inside `geometry-descriptor`, including `.obuf` metadata emission and virtual-path-based resolution semantics
2. Import manifest + schema fully describe and validate descriptor orchestration.
3. C++ `PakBuilder` is the authoritative pack path in docs/CI.
4. PakGen is no longer required for standard workflows and is removed or explicitly quarantined behind non-default tooling.
5. `design/content-pipeline/pakgen-supersession-implementation-plan.md` reports 100% with evidence for every phase.
