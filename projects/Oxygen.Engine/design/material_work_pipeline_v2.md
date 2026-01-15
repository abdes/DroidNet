# Material Pipeline (v2)

**Status:** Complete Design (Phase 5)
**Date:** 2026-01-15
**Parent:** [async_import_pipeline_v2.md](async_import_pipeline_v2.md)

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

---

## Alignment With Current Architecture

The pipeline-agnostic concurrency and lifetime model is defined in the parent
document [design/async_import_pipeline_v2.md](design/async_import_pipeline_v2.md)
under **Concurrency, Ownership, and Lifetime (Definitive)**.

### Pipeline vs Emitter Responsibilities

- **MaterialPipeline**: produces `CookedMaterialPayload` (descriptor bytes).
- **AssetEmitter**: writes `.omat` files and records assets.

---

## Data Model

### WorkItem

```cpp
struct MaterialUvTransform {
  float scale[2] = { 1.0f, 1.0f };
  float offset[2] = { 0.0f, 0.0f };
  float rotation_radians = 0.0f;
};

enum class MaterialAlphaMode : uint8_t { kOpaque, kMasked, kBlended };

struct ShaderRequest {
  uint8_t shader_type = 0; // ShaderType enum value
  std::string source_path;
  std::string entry_point;
  std::string defines;
  uint64_t shader_hash = 0;
};

struct MaterialTextureBinding {
  uint32_t index = 0;        // Texture table index (0 = fallback / none)
  bool assigned = false;     // True if a texture was resolved (placeholder ok)
  std::string source_id;     // Optional identity for ORM detection
  uint8_t uv_set = 0;        // Source UV set index (0 = default)
  MaterialUvTransform uv_transform;
};

struct MaterialTextureBindings {
  MaterialTextureBinding base_color;
  MaterialTextureBinding normal;
  MaterialTextureBinding metallic;
  MaterialTextureBinding roughness;
  MaterialTextureBinding ambient_occlusion;
  MaterialTextureBinding emissive;
  MaterialTextureBinding specular;
  MaterialTextureBinding sheen_color;
  MaterialTextureBinding clearcoat;
  MaterialTextureBinding clearcoat_normal;
  MaterialTextureBinding transmission;
  MaterialTextureBinding thickness;
};

struct MaterialInputs {
  float base_color[4] = { 1.0f, 1.0f, 1.0f, 1.0f };
  float normal_scale = 1.0f;
  float metalness = 0.0f;
  float roughness = 1.0f;
  float ambient_occlusion = 1.0f;
  float emissive_factor[3] = { 0.0f, 0.0f, 0.0f }; // HDR allowed
  float alpha_cutoff = 0.5f;
  float ior = 1.5f;
  float specular_factor = 1.0f;
  float sheen_color_factor[3] = { 0.0f, 0.0f, 0.0f };
  float clearcoat_factor = 0.0f;
  float clearcoat_roughness = 0.0f;
  float transmission_factor = 0.0f;
  float thickness_factor = 0.0f;
  float attenuation_color[3] = { 1.0f, 1.0f, 1.0f };
  float attenuation_distance = 0.0f;
  bool double_sided = false;
  bool unlit = false;
  bool roughness_as_glossiness = false;
};

enum class OrmPolicy : uint8_t { kAuto, kForcePacked, kForceSeparate };

struct WorkItem {
  std::string source_id;
  std::string material_name;
  std::string storage_material_name;
  const void* source_key{};

  MaterialDomain material_domain = MaterialDomain::kOpaque;
  MaterialAlphaMode alpha_mode = MaterialAlphaMode::kOpaque;
  MaterialInputs inputs;
  MaterialTextureBindings textures;
  OrmPolicy orm_policy = OrmPolicy::kAuto;
  std::vector<ShaderRequest> shader_requests;

  ImportRequest request;
  std::stop_token stop_token;
};
```

Notes:

- `material_name` and `storage_material_name` should be resolved by the
  orchestrator using `BuildMaterialName` and `NamespaceImportedAssetName`.
- `storage_material_name` is used for virtual paths and descriptor relpaths.
- `material_domain` uses `oxygen::data::MaterialDomain`.
- `textures.*.index` must be final texture indices from `TextureEmitter`.
  Missing textures should be resolved to placeholders when `want_textures` is
  true so `assigned` is still true.
- `textures.*.uv_set` refers to the source UV set. If `uv_set != 0`, the
  orchestrator must remap geometry UVs to set 0 or bake textures accordingly.
- `textures.*.uv_transform` comes from source material transforms (glTF or FBX).
  When transforms differ between slots, they must be baked into the affected
  textures so the material can use a single global UV transform.
- `shader_requests` is optional; when empty, the pipeline synthesizes default
  stage bindings for the requested `material_domain` and flags.
- `orm_policy` controls `kMaterialFlag_GltfOrmPacked`.
- Scalars must be pre-normalized to 0..1 where appropriate; emissive is HDR and
  is **not** clamped. `alpha_cutoff` must be in [0, 1].

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
  void Cancel();

  [[nodiscard]] auto HasPending() const noexcept -> bool;
  [[nodiscard]] auto PendingCount() const noexcept -> size_t;
  [[nodiscard]] auto GetProgress() const noexcept -> PipelineProgress;
};
```

---

## Worker Behavior

Materials are lightweight today, but the pipeline is structured for future
parallel work. Workers run on the import thread; expensive analysis can be
offloaded to the ThreadPool when `use_thread_pool = true`.

For each work item:

1) Check cancellation; if cancelled, return `success=false`.
2) Compute material identity:
   - `virtual_path = request.loose_cooked_layout.MaterialVirtualPath(
      storage_material_name)`
   - `descriptor_relpath = request.loose_cooked_layout.MaterialDescriptorRelPath(
      storage_material_name)`
   - `material_key` from `AssetKeyPolicy`
3) Normalize scalar inputs:
   - Clamp base color and scalar factors to [0, 1]
   - Clamp `alpha_cutoff`, `specular_factor`, `clearcoat_factor`,
     `clearcoat_roughness`, `transmission_factor`, and `thickness_factor`
   - Clamp `ior` to `>= 1.0` and `attenuation_distance` to `>= 0`
   - Preserve emissive HDR (no clamp)
   - Apply `roughness_as_glossiness` inversion when set
4) Resolve domain + alpha:
   - Start with `material_domain` from the work item
   - If `alpha_mode == kMasked`, set `material_domain = kMasked` unless the
     domain is `kDecal`, `kUserInterface`, or `kPostProcess`, and set
     `kMaterialFlag_AlphaTest`
   - If `alpha_mode == kBlended`, set `material_domain = kAlphaBlended` unless
     the domain is `kDecal`, `kUserInterface`, or `kPostProcess`
5) Resolve UV transforms:
   - If all assigned textures share the same `uv_set` and `uv_transform`, store
     the transform in the descriptor extension (see "UV Transform Extension")
   - Otherwise, require texture baking for non-identity transforms (owned by the
     orchestrator) and use identity transform in the descriptor
6) Resolve flags:
   - Start with `kMaterialFlag_NoTextureSampling`
   - Apply `kMaterialFlag_DoubleSided`, `kMaterialFlag_Unlit`
   - Apply `kMaterialFlag_GltfOrmPacked` when
     `orm_policy == kForcePacked`, or
     `orm_policy == kAuto` and metallic/roughness/AO share the same `source_id`
7) Bind textures:
   - Copy texture indices into `MaterialAssetDesc` for all slots
   - If ORM packed, set metallic/roughness/AO indices to the packed texture
   - Clear `kMaterialFlag_NoTextureSampling` if any `assigned == true`
8) Resolve shader stages:
   - If `shader_requests` is empty, synthesize default shader refs for the
     resolved `material_domain` and flags
   - Otherwise, copy requests verbatim and build `shader_stages`
   - Serialize shader references in ascending `shader_type` bit order
9) Build `MaterialAssetDesc`:
   - `header.asset_type = AssetType::kMaterial`
   - `header.name = material_name`
   - `header.version = 2` when UV transform extension is used, otherwise
     `kMaterialAssetVersion`
   - `header.streaming_priority` from `ImportRequest` (0 if unspecified)
   - `header.variant_flags` from import configuration
   - Fill `material_domain`, `flags`, `shader_stages`, and all scalar fields
10) Compute `header.content_hash` over the descriptor bytes + shader refs using
    `util::ComputeContentHash`.
11) Serialize descriptor bytes with packed alignment = 1, followed by shader
    references.
12) Return `CookedMaterialPayload`.

---

## Pipeline Stages (Complete)

The pipeline is defined as two stages with explicit parallelization boundaries:

### Stage A: Source Normalization (Import Thread)

- Material naming and identity (virtual path, key, relpath)
- Scalar value normalization and unit conversion
- Alpha mode and material domain resolution
- ORM detection (source id equality)
- UV transform resolution and baking decisions
- Default shader binding decisions (when no explicit requests)

### Stage B: Material Cook (ThreadPool-capable)

- Populate `MaterialAssetDesc` (all PBR fields)
- Build `ShaderReferenceDesc` table and `shader_stages`
- Serialize descriptor bytes + shader references
- Compute `header.content_hash`

---

## Cooked Output Contract (PAK v2)

This pipeline targets `oxygen::data::pak::v2`. `MaterialAssetVersion = 2`
extends the descriptor in-place without changing the container version. Any
future descriptor layout changes that exceed reserved bytes must introduce a new
PAK namespace (v5) and a new material asset version.

### Material Descriptor (`.omat`)

Packed binary blob:

```
MaterialAssetDesc (256 bytes)
ShaderReferenceDesc[ popcount(shader_stages) ]
```

Key requirements:

- `material_domain` matches resolved domain (opaque, masked, blended, etc.)
- `flags` includes `kMaterialFlag_NoTextureSampling` only when no textures are
  assigned across **all** slots
- `shader_stages` matches the serialized shader refs (zero is valid but should
  only occur for diagnostic-only materials)
- `header.version = kMaterialAssetVersion` unless the UV transform extension is
  used, in which case it must be `2`
- `header.content_hash` is non-zero when payload bytes are non-empty

### UV Transform Extension (MaterialAssetVersion 2)

The reserved bytes in `MaterialAssetDesc` are used to encode the default
per-material UV transform (global, applied to all slots). The pipeline must set
`header.version = 2` when emitting this extension.

```
struct MaterialUvTransformDesc {
  float uv_scale[2];
  float uv_offset[2];
  float uv_rotation_radians;
  uint8_t uv_set;
  uint8_t reserved[19];
};
```

Contract:

- If all assigned textures share identical `uv_set` and `uv_transform`, store
  that transform here.
- If transforms differ, textures must be baked to remove their transforms and
  this extension must be set to identity (scale 1, offset 0, rotation 0,
  uv_set 0).
- Loaders must accept version 1 (implicit identity transform) and version 2
  (explicit transform).

---

## Feature Completion Requirements

The MaterialPipeline must **fix** legacy omissions and provide full coverage of
the PAK format:

- Material domains and alpha modes (`kOpaque`, `kMasked`, `kAlphaBlended`,
  `kDecal`, `kUserInterface`, `kPostProcess`), including alpha cutoff.
- Shader reference table emission for all required stages.
- Full PBR Tier 1/2 fields: emissive, specular, sheen, clearcoat, transmission,
  thickness, IOR, and attenuation.
- Asset header metadata: `version`, `streaming_priority`, `content_hash`,
  `variant_flags`.
- UV transform and UV set handling via the extension above or baking.

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
  - `KHR_materials_specular` → `specular_factor` + `specular_texture`
  - `KHR_materials_sheen` → `sheen_color_factor` + `sheen_color_texture`
  - `KHR_materials_clearcoat` → `clearcoat_factor`, `clearcoat_roughness`,
    `clearcoat_texture`, `clearcoat_normal_texture`
  - `KHR_materials_transmission` → `transmission_factor` + `transmission_texture`
  - `KHR_materials_volume` → `thickness_factor`, `attenuation_color`,
    `attenuation_distance`, `thickness_texture`
- **FBX**:
  - Diffuse/albedo → `base_color`
  - Emissive → `emissive_factor`
  - Specular color/texture → `specular_factor` + `specular_texture`
  - Shininess/glossiness → `roughness` (invert and clamp to [0,1])
  - Transparency → `alpha_mode` + `alpha_cutoff` when masked

---

## Separation of Concerns

- **MaterialPipeline**: compute-only descriptor assembly.
- **TexturePipeline**: cooks textures and provides indices.
- **AssetEmitter**: `.omat` output and records.

---

## Cancellation Semantics

Pipeline must honor `WorkItem.stop_token`. Cancellation returns
`success=false` without partial outputs.

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

1) Pipeline never writes output files.
2) Descriptor serialization must be packed (alignment = 1).
3) `kMaterialFlag_NoTextureSampling` must reflect `assigned` texture bindings.
4) `header.version` must reflect UV transform extension usage.
5) `header.content_hash` must cover descriptor bytes + shader refs.
6) No exceptions across async boundaries.

---

## Integration Checklist (Phase 5)

- [ ] Implement `Async/Pipelines/MaterialPipeline.h/.cpp`
- [ ] Integrate with import jobs (`FbxImportJob::EmitMaterials`, glTF)
- [ ] Emit `.omat` via `session.AssetEmitter().Emit(...)`
- [ ] Extend loader/runtime to read UV transform extension (version 2)
- [ ] Add unit tests for flags, ORM detection, shader refs, and UV transform

---

## See Also

- [texture_work_pipeline_v2.md](texture_work_pipeline_v2.md)
- `src/Oxygen/Content/Import/FbxImporter.cpp` (legacy material emission)
- `src/Oxygen/Data/PakFormat.h` (MaterialAssetDesc)
