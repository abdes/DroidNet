# Material Pipeline (v2)

**Parent:** [async_import_pipeline_v2.md](async_import_pipeline_v2.md)

Read: [emission contract](#emission-contract), [data model](#data-model),
[architecture](#alignment-with-current-architecture).

## Emission contract

Canonical descriptors author `parameters.emissive_color` (finite linear RGB in
[0,1], default white) and `parameters.emissive_intensity` (finite float32 in
[0,65504], default zero). Schema validation rejects the retired
`emissive_factor` property. The descriptor job compiles color times intensity
once into `MaterialInputs::emissive_factor`; imported model adapters provide the
same compiled RGB input. The pipeline rejects non-finite, negative or
out-of-range factors and writes float32 channels without clamping or binary16
conversion.

`MaterialAssetDesc` is 306 packed bytes. Native readers accept only the current
`kMaterialAssetVersion`. Recook maintained content and update fixture writers
together; do not add old-format branches or managed binary writers.

---

## Overview

This document specifies the **MaterialPipeline** used by async imports to cook
material descriptors (`.omat`). The pipeline is compute-only and must produce
**feature-complete** material assets, closing legacy gaps while staying fully
compatible with the PAK format and async architecture.

Core properties:

- **Compute-only**: builds `MaterialAssetDesc` and optional shader references.
- **No I/O**: `AssetEmitter` writes `.omat` files.
- **Job-scoped**: created per job and started in the job’s child nursery.
- **ThreadPool-aware**: supports future heavy material graph processing.
- **Planner‑gated**: the planner submits work only after texture indices and
  other dependencies are ready.

---

## Alignment With Current Architecture

The pipeline-agnostic concurrency and lifetime model is defined in the parent
document
[design/async_import_pipeline_v2.md](design/async_import_pipeline_v2.md) under
**Concurrency, Ownership, and Lifetime (Definitive)**.

### Pipeline vs Emitter Responsibilities

- **MaterialPipeline**: produces `CookedMaterialPayload` (descriptor bytes).
- **AssetEmitter**: writes `.omat` files and records assets.

---

## Data Model

### WorkItem

A work item carries a `MaterialSource` (`Import/Internal/MaterialSource.h`), which
owns the material's semantics: its domain, alpha mode, scalar `MaterialInputs`,
`MaterialTextureBindings`, ORM policy, ambient occlusion mode, procedural grid and
shader requests. Descriptors and model adapters produce the same type.

Notes:

- `textures.*.index` must be final texture indices from `TextureEmitter`. The
  planner must ensure these dependencies are ready before submission. For
  textures, index `0` refers to the fallback texture (`kFallbackResourceIndex`).
  Use `assigned=false` to indicate a truly missing texture slot; `assigned=true`
  means the slot should be sampled (including placeholder or fallback).
- A material has one UV transform and vertices have one UV set: the first assigned
  texture's transform is written, and the others use it. glTF and FBX adapters warn
  of what they drop, and descriptors warn `material.uv_dropped`. Issue #113 tracks
  per-slot transforms and a second set.
- `shader_requests` is optional; when empty, the pipeline synthesizes default
  stage bindings for the resolved domain and flags.
- `orm_policy` controls `kMaterialFlag_GltfOrmPacked`, the only thing that says a
  texture is packed; slots holding the same texture do not.
- Values that are not finite are refused with `material.input_not_finite`; model
  adapters replace them by their defaults first, with that code as a warning.
  Emissive is HDR and is **not** clamped.

### WorkResult

```cpp
struct CookedMaterialPayload {
  data::AssetKey material_key;
  std::string virtual_path;
  std::string descriptor_relpath;
  std::vector<std::byte> descriptor_bytes; // .omat payload
};

struct WorkResult {
  std::string source_id;
  std::optional<CookedMaterialPayload> cooked;
  std::vector<ImportDiagnostic> diagnostics;
  ImportWorkItemTelemetry telemetry;
  bool success = false;
};
```

---

## Public API (Pattern)

```cpp
class MaterialPipeline final {
public:
  struct Config {
    size_t queue_capacity = 64;
    uint32_t worker_count = 2;
    bool use_thread_pool = true;
  };

  explicit MaterialPipeline(co::ThreadPool& thread_pool, Config cfg = {});

  void Start(co::Nursery& nursery);

  [[nodiscard]] auto Submit(WorkItem item) -> co::Co<>;
  [[nodiscard]] auto TrySubmit(WorkItem item) -> bool;
  [[nodiscard]] auto Collect() -> co::Co<WorkResult>;

  void Close();

  [[nodiscard]] auto HasPending() const noexcept -> bool;
  [[nodiscard]] auto PendingCount() const noexcept -> size_t;
  [[nodiscard]] auto GetProgress() const noexcept -> PipelineProgress;

  // Queue inspection helpers exposed by the implementation.
  [[nodiscard]] auto InputQueueSize() const noexcept -> size_t;
  [[nodiscard]] auto InputQueueCapacity() const noexcept -> size_t;
  [[nodiscard]] auto OutputQueueSize() const noexcept -> size_t;
  [[nodiscard]] auto OutputQueueCapacity() const noexcept -> size_t;
};
```

---

## Worker Behavior

Materials are lightweight today, but the pipeline is structured for future
parallel work. Workers run on the import thread; expensive analysis can be
offloaded to the ThreadPool when `use_thread_pool = true`.

For each work item:

1. Check cancellation; if canceled, return `success=false`.
2. Compute material identity:
   - `virtual_path = request.ModelProductVirtualPath(storage_material_name, ".omat")`,
     flat under the model (`/Cooked/Ship.glb/Paint.omat`), or a material
     descriptor job's identity
   - `descriptor_relpath`: that virtual path under the mount (`Ship.glb/Paint.omat`)
   - `material_key = xxHash3-128(canonical virtual_path)`
3. Normalize scalar inputs:
   - Apply `roughness_as_glossiness` inversion first, so `roughness` is always a
     roughness; with a roughness texture, also set
     `kMaterialFlag_GlossinessTexture`, whose samples the shader reads as glossiness
   - Clamp base color and scalar factors to [0, 1]
   - Clamp `alpha_cutoff` (0 stays 0: it keeps every pixel), `specular_factor`,
     `clearcoat_factor`,
     `clearcoat_roughness`, `transmission_factor`, and `thickness_factor`
   - Clamp `ior` to `>= 1.0` and `attenuation_distance` to `>= 0`
   - Preserve emissive HDR (no clamp)
4. Resolve domain + alpha:
   - Start with `material_domain` from the work item
   - If `alpha_mode == kMasked`, set `material_domain = kMasked` unless the
     domain is `kDecal`, `kUserInterface`, or `kPostProcess`, and set
     `kMaterialFlag_AlphaTest`
   - If `alpha_mode == kBlended`, set `material_domain = kAlphaBlended` unless
     the domain is `kDecal`, `kUserInterface`, or `kPostProcess`
   - If `alpha_mode == kOpaque`, a `kMasked` or `kAlphaBlended` domain decides,
     and `kMasked` sets `kMaterialFlag_AlphaTest`
   - A mode and a domain that disagree warn `material.domain_alpha_conflict`
5. Resolve UV transforms:
   - If all assigned textures share the same `uv_set` and `uv_transform`, store
     that transform in the descriptor extension (see "UV Transform Extension").
   - If assigned textures have differing transforms the _first assigned_
     texture's transform is stored (see the notes on the work item).
6. Resolve flags:
   - Start with `kMaterialFlag_NoTextureSampling`.
   - Apply `kMaterialFlag_DoubleSided`, `kMaterialFlag_Unlit`.
   - Resolve ORM packing using metallic+roughness compatibility:
     - `OrmPolicy::kForcePacked` requires _metallic_ and _roughness_ to be
       `assigned=true` and to share the same `source_id` and exact `uv_set` +
       `uv_transform`; otherwise the pipeline emits a blocking diagnostic
       (`material.orm_policy`) and the item fails.
     - `OrmPolicy::kAuto` enables packing when metallic and roughness are both
       assigned and share the same `source_id` and UV transform; ambient
       occlusion is not required to enable packing (if AO is present but has a
       different source it remains a separate texture). If packing succeeds,
       `kMaterialFlag_GltfOrmPacked` is set and metallic/roughness indices are
       set to the packed index.
7. Bind textures:
   - Copy texture indices into `MaterialAssetDesc` for all slots
   - If ORM packed, set the metallic and roughness references to the packed
     texture; ambient occlusion keeps its own binding, which may be the same one
   - Clear `kMaterialFlag_NoTextureSampling` if any `assigned == true`
8. Resolve shader stages:
   - If `shader_requests` is empty, synthesize default shader refs for the
     resolved `material_domain` and flags
   - Otherwise, copy requests verbatim and build `shader_stages`
   - Serialize shader references in ascending `shader_type` bit order
9. Build `MaterialAssetDesc`:
   - `header.asset_type = AssetType::kMaterial`
   - `header.name = material_name`
   - `header.version = kMaterialAssetVersion`
   - Fill `material_domain`, `flags`, `shader_stages`, and all scalar fields
10. Serialize descriptor bytes with packed alignment = 1, followed by shader
    references.
11. Return `CookedMaterialPayload`.

---

## Pipeline Stages (Complete)

The pipeline is defined as two stages with explicit parallelization boundaries:

### Stage A: Source Normalization (Import Thread)

- Material naming and identity (virtual path, key, relpath)
- Scalar value normalization and unit conversion
- Alpha mode and material domain resolution
- ORM detection (source id equality)
- UV transform resolution
- Default shader binding decisions (when no explicit requests)

### Stage B: Material Cook (ThreadPool-capable)

- Populate `MaterialAssetDesc` (all PBR fields)
- Build `ShaderReferenceDesc` table and `shader_stages`
- Serialize descriptor bytes + shader references

---

## Cooked Output Contract

The enclosing PAK container and the material descriptor have independent
versions. `kMaterialAssetVersion` is the sole native descriptor-version
authority; increment it when changing the material layout.

### Material Descriptor (`.omat`)

Packed binary blob:

```text
MaterialAssetDesc (306 bytes)
ShaderReferenceDesc[ popcount(shader_stages) ]
string table
```

Key requirements:

- `material_domain` matches resolved domain (opaque, masked, blended, etc.)
- `flags` includes `kMaterialFlag_NoTextureSampling` only when no textures are
  assigned across **all** slots
- `shader_stages` matches the serialized shader refs (zero is invalid for
  successful materials and must emit a diagnostic)
- `header.version = kMaterialAssetVersion`
- `header.content_hash` is left to the emitter: `AssetEmitter` records it
  (`data::StampDescriptorHash`) over descriptor bytes + shader refs

### UV Transform Fields

`MaterialAssetDesc` stores the default per-material UV transform (global,
applied to all slots) in explicit fields. The pipeline must **always** write
these values; identity means “no transform.”

```text
struct MaterialAssetDesc {
  // ...
  float uv_scale[2];
  float uv_offset[2];
  float uv_rotation_radians;
  uint8_t uv_set;
  // ...
};
```

Contract:

- If all assigned textures share identical `uv_set` and `uv_transform`, store
  that transform here.
- If assigned textures have differing transforms, the _first assigned_
  texture's transform is stored here. Loaders read these fields unconditionally.

Renderer convention:

- `uv_rotation_radians` is counter-clockwise rotation in UV space.
- Rotation is around the UV origin (0,0), not the center of the texture.
- Transform order is: scale → rotation → offset.
- `uv_set` selects the source UV set (0 = TEXCOORD0). Additional sets require
  vertex payload support; the current renderer only provides uv0.

---

## Feature Completion Requirements

The MaterialPipeline must **fix** legacy omissions and provide full coverage of
the PAK format:

- Material domains and alpha modes (`kOpaque`, `kMasked`, `kAlphaBlended`,
  `kDecal`, `kUserInterface`, `kPostProcess`), including alpha cutoff.
- Shader reference table emission for all required stages.
- Full PBR Tier 1/2 fields: emissive, specular, sheen, clearcoat, transmission,
  thickness, IOR, and attenuation.
- Asset header metadata: `version` (the emitter records `content_hash` when it
  emits the descriptor).
- One UV transform and one UV set, with a warning for what asks for more.
- The procedural grid and the ambient occlusion mode, from descriptors.

---

## Source Mapping Requirements (Importer/Orchestrator)

The pipeline consumes normalized `MaterialInputs` and `MaterialTextureBindings`.
Importer/orchestrator responsibilities:

- **glTF PBR Metallic-Roughness**:
  - `baseColorFactor/Texture` → `base_color` and `base_color_texture`
  - `metallicFactor/roughnessFactor` → scalars
  - `metallicRoughnessTexture` → metallic + roughness (set ORM packed)
  - `occlusionTexture` → AO (shares ORM when packed)
  - `normalTexture.scale` → `normal_scale`
  - `emissiveFactor/Texture` → `emissive_factor` + `emissive_texture`
- **glTF alpha**:
  - `OPAQUE` → `kOpaque`
  - `MASK` → `kMasked` + `alpha_cutoff`
  - `BLEND` → `kAlphaBlended`
- **glTF extensions**:
  - `KHR_materials_unlit` → `unlit`
  - `KHR_materials_ior` → `ior`
  - `KHR_materials_specular` → `specular_factor` = 0.5 × its factor, and
    `specular_texture` cooked from its alpha channel into red
  - `KHR_materials_sheen` → `sheen_color_factor` + `sheen_color_texture`
  - `KHR_materials_clearcoat` → `clearcoat_factor`, `clearcoat_roughness`,
    `clearcoat_texture`, `clearcoat_normal_texture`
  - `KHR_materials_transmission` → `transmission_factor` +
    `transmission_texture`
  - `KHR_materials_volume` → `thickness_factor`, `attenuation_color`,
    `attenuation_distance`, `thickness_texture`
- **FBX**:
  - Diffuse/albedo → `base_color`
  - Emissive → `emissive_factor`
  - Specular by shader: Blender's specular level (`SpecularFactor`) as it is;
    a PBR shader's specular weight as 0.5 × weight; FBX Phong or Lambert
    specular shapes a highlight, so it is not imported and its texture is
    reported
  - Shininess/glossiness → `roughness` (invert and clamp to [0,1])
  - Transparency → `alpha_mode` + `alpha_cutoff` when masked

---

## Separation of Concerns

- **MaterialPipeline**: compute-only descriptor assembly.
- **TexturePipeline**: cooks textures and provides indices.
- **AssetEmitter**: `.omat` output and records.

---

## Cancellation Semantics

Pipeline must honor `WorkItem.stop_token`. Cancellation returns `success=false`
without partial outputs.

---

## Backpressure and Memory Safety

Use bounded queues. Materials are small; default capacity is generous but
bounded to avoid unbounded descriptor accumulation.

---

## Progress Reporting

Pipeline tracks submitted/completed/failed counts and exposes
`PipelineProgress`. The job maps this to `ImportProgress`.

---

## Robustness Rules (Do Not Violate)

1. Pipeline never writes output files.
2. Descriptor serialization must be packed (alignment = 1).
3. `kMaterialFlag_NoTextureSampling` must reflect `assigned` texture bindings.
4. `header.version` must use `kMaterialAssetVersion`.
5. `header.content_hash` covers descriptor bytes + shader refs. The emitter
   guarantees it: `AssetEmitter::Emit` and `EmitSync` record it in every
   descriptor, and loaders and `content::ValidateAssetDescriptor` refuse a
   descriptor without it (`kMissingDigest` / `kDigestMismatch`).
6. No exceptions across async boundaries.

---

## Validation and Limits (Industry-Style Defaults)

The pipeline must validate inputs and emit diagnostics for out-of-range or
invalid data. Conservative defaults aligned with common engine practice:

- Shader stage count must be 1..32; all `shader_type` values must be unique.
- `ShaderReferenceDesc` strings (`source_path`, `entry_point`, `defines`) are
  stored whole in the descriptor string table; they are never truncated.
- `MaterialTextureBinding.index` must fit `ResourceIndexT`; if out of range,
  emit a blocking error.
- `MaterialAlphaMode::kMasked` requires `alpha_cutoff` in [0,1]; clamp and emit
  a warning when out of range.
- `OrmPolicy::kForcePacked` requires `metallic` and `roughness` to be
  `assigned=true` and to share identical `source_id`, `uv_set`, and
  `uv_transform`; otherwise emit a blocking error (diagnostic code
  `material.orm_policy`). `OrmPolicy::kAuto` will enable packing when metallic
  and roughness are assigned and compatible; ambient occlusion may be packed
  when compatible but is not required.

---

## See Also

- [texture_work_pipeline_v2.md](texture_work_pipeline_v2.md)
- `src/Oxygen/Cooker/Import/FbxImporter.cpp` (legacy material emission)
- `src/Oxygen/Data/PakFormat.h` (MaterialAssetDesc)
