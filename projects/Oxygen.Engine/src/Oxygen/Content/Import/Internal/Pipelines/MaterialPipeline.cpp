//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <algorithm>
#include <array>
#include <limits>
#include <span>
#include <string_view>
#include <utility>

#include <Oxygen/Base/Logging.h>
#include <Oxygen/Content/Import/Internal/Pipelines/MaterialPipeline.h>
#include <Oxygen/Content/Import/Internal/Utils/AssetKeyUtils.h>
#include <Oxygen/Content/Import/Internal/Utils/ContentHashUtils.h>
#include <Oxygen/Content/Import/Internal/Utils/StringUtils.h>
#include <Oxygen/Core/Types/ShaderType.h>
#include <Oxygen/Data/AssetType.h>
#include <Oxygen/Serio/MemoryStream.h>
#include <Oxygen/Serio/Writer.h>

namespace oxygen::content::import {

namespace {

  constexpr size_t kShaderSourcePathMax
    = sizeof(data::pak::ShaderReferenceDesc::source_path);
  constexpr size_t kShaderEntryPointMax
    = sizeof(data::pak::ShaderReferenceDesc::entry_point);
  constexpr size_t kShaderDefinesMax
    = sizeof(data::pak::ShaderReferenceDesc::defines);

  constexpr uint32_t kMaxShaderStages = 32;

  struct MaterialUvTransformDesc {
    float uv_scale[2] = { 1.0f, 1.0f };
    float uv_offset[2] = { 0.0f, 0.0f };
    float uv_rotation_radians = 0.0f;
    uint8_t uv_set = 0;
  };

  struct ShaderBuildResult {
    std::vector<data::pak::ShaderReferenceDesc> shader_refs;
    uint32_t shader_stages = 0;
    bool has_error = false;
  };

  struct BuildOutcome {
    std::vector<std::byte> bytes;
    std::vector<ImportDiagnostic> diagnostics;
    bool canceled = false;
    bool has_error = false;
  };

  [[nodiscard]] auto IsShaderTypeValid(const uint8_t shader_type) -> bool
  {
    if (shader_type == 0) {
      return false;
    }
    const auto max_type = static_cast<uint32_t>(ShaderType::kMaxShaderType);
    return static_cast<uint32_t>(shader_type) <= max_type;
  }

  [[nodiscard]] auto ShaderStageBit(const uint8_t shader_type) -> uint32_t
  {
    return 1u << static_cast<uint32_t>(shader_type);
  }

  [[nodiscard]] auto HasErrorDiagnostic(
    const std::vector<ImportDiagnostic>& diagnostics) -> bool
  {
    return std::any_of(
      diagnostics.begin(), diagnostics.end(), [](const ImportDiagnostic& diag) {
        return diag.severity == ImportSeverity::kError;
      });
  }

  [[nodiscard]] auto MakeWarningDiagnostic(std::string code,
    std::string message, std::string_view source_id,
    std::string_view object_path) -> ImportDiagnostic
  {
    return ImportDiagnostic {
      .severity = ImportSeverity::kWarning,
      .code = std::move(code),
      .message = std::move(message),
      .source_path = std::string(source_id),
      .object_path = std::string(object_path),
    };
  }

  [[nodiscard]] auto MakeErrorDiagnostic(std::string code, std::string message,
    std::string_view source_id, std::string_view object_path)
    -> ImportDiagnostic
  {
    return ImportDiagnostic {
      .severity = ImportSeverity::kError,
      .code = std::move(code),
      .message = std::move(message),
      .source_path = std::string(source_id),
      .object_path = std::string(object_path),
    };
  }

  [[nodiscard]] auto BuildDefinesString(const bool alpha_test_enabled)
    -> std::string
  {
    if (!alpha_test_enabled) {
      return {};
    }
    return "ALPHA_TEST=1";
  }

  [[nodiscard]] auto BuildDefaultShaderRequests(
    const data::MaterialDomain /*domain*/, const uint32_t flags)
    -> std::vector<ShaderRequest>
  {
    const bool alpha_test_enabled
      = (flags & data::pak::kMaterialFlag_AlphaTest) != 0u;
    const auto defines = BuildDefinesString(alpha_test_enabled);

    std::vector<ShaderRequest> shaders;
    shaders.reserve(2);

    shaders.push_back(ShaderRequest {
      .shader_type = static_cast<uint8_t>(ShaderType::kVertex),
      .source_path = "Passes/Forward/ForwardMesh_VS.hlsl",
      .entry_point = "VS",
      .defines = {},
      .shader_hash = 0,
    });
    shaders.push_back(ShaderRequest {
      .shader_type = static_cast<uint8_t>(ShaderType::kPixel),
      .source_path = "Passes/Forward/ForwardMesh_PS.hlsl",
      .entry_point = "PS",
      .defines = defines,
      .shader_hash = 0,
    });

    return shaders;
  }

  auto WriteShaderString(char* dst, const size_t dst_size,
    std::string_view value, const char* field_name, std::string_view source_id,
    std::string_view object_path, std::vector<ImportDiagnostic>& diagnostics)
    -> void
  {
    util::TruncateAndNullTerminate(dst, dst_size, value);
    if (value.size() >= dst_size) {
      diagnostics.push_back(
        MakeWarningDiagnostic("material.shader_ref_truncated",
          std::string(field_name) + " truncated to "
            + std::to_string(dst_size - 1) + " bytes",
          source_id, object_path));
    }
  }

  [[nodiscard]] auto BuildShaderReferences(
    std::vector<ShaderRequest> shader_requests, std::string_view source_id,
    std::string_view object_path, std::vector<ImportDiagnostic>& diagnostics)
    -> ShaderBuildResult
  {
    ShaderBuildResult result;

    if (shader_requests.empty()) {
      diagnostics.push_back(MakeErrorDiagnostic(
        "material.shader_stages_missing",
        "Material requires at least one shader stage", source_id, object_path));
      result.has_error = true;
      return result;
    }

    if (shader_requests.size() > kMaxShaderStages) {
      diagnostics.push_back(MakeErrorDiagnostic("material.shader_stage_count",
        "Shader stage count exceeds 32", source_id, object_path));
      result.has_error = true;
      return result;
    }

    std::array<bool, kMaxShaderStages> seen {};
    for (const auto& request : shader_requests) {
      if (!IsShaderTypeValid(request.shader_type)) {
        diagnostics.push_back(
          MakeErrorDiagnostic("material.shader_stage_invalid",
            "Shader type is invalid", source_id, object_path));
        result.has_error = true;
        continue;
      }

      const auto stage_bit = ShaderStageBit(request.shader_type);
      const auto stage_index = static_cast<size_t>(request.shader_type - 1u);
      if (stage_index >= seen.size()) {
        diagnostics.push_back(
          MakeErrorDiagnostic("material.shader_stage_invalid",
            "Shader type is out of range", source_id, object_path));
        result.has_error = true;
        continue;
      }

      if (seen[stage_index]) {
        diagnostics.push_back(MakeErrorDiagnostic(
          "material.shader_stage_duplicate",
          "Shader type is duplicated in request list", source_id, object_path));
        result.has_error = true;
        continue;
      }

      if (request.source_path.empty() || request.entry_point.empty()) {
        diagnostics.push_back(MakeErrorDiagnostic("material.shader_ref_invalid",
          "Shader source_path and entry_point must be set", source_id,
          object_path));
        result.has_error = true;
        continue;
      }

      seen[stage_index] = true;
      result.shader_stages |= stage_bit;
    }

    if (result.has_error) {
      return result;
    }

    std::sort(shader_requests.begin(), shader_requests.end(),
      [](const ShaderRequest& lhs, const ShaderRequest& rhs) {
        return lhs.shader_type < rhs.shader_type;
      });

    result.shader_refs.reserve(shader_requests.size());
    for (const auto& request : shader_requests) {
      data::pak::ShaderReferenceDesc ref {};
      ref.shader_type = request.shader_type;
      WriteShaderString(ref.source_path, kShaderSourcePathMax,
        request.source_path, "source_path", source_id, object_path,
        diagnostics);
      WriteShaderString(ref.entry_point, kShaderEntryPointMax,
        request.entry_point, "entry_point", source_id, object_path,
        diagnostics);
      WriteShaderString(ref.defines, kShaderDefinesMax, request.defines,
        "defines", source_id, object_path, diagnostics);
      ref.shader_hash = request.shader_hash;
      result.shader_refs.push_back(ref);
    }

    return result;
  }

  [[nodiscard]] auto BuildMaterialUvTransformDesc(
    const std::vector<const MaterialTextureBinding*>& bindings)
    -> MaterialUvTransformDesc
  {
    MaterialUvTransformDesc desc {};

    const MaterialTextureBinding* reference = nullptr;
    for (const auto* binding : bindings) {
      if (binding == nullptr || !binding->assigned) {
        continue;
      }
      reference = binding;
      break;
    }

    if (reference == nullptr) {
      return desc;
    }

    // TODO: Update this when multiple UV sets/transforms are supported in
    // material descriptors.
    DLOG_F(INFO,
      "MaterialPipeline: using single UV transform from '{}'; "
      "multiple UV sets not yet supported",
      reference->source_id);

    desc.uv_scale[0] = reference->uv_transform.scale[0];
    desc.uv_scale[1] = reference->uv_transform.scale[1];
    desc.uv_offset[0] = reference->uv_transform.offset[0];
    desc.uv_offset[1] = reference->uv_transform.offset[1];
    desc.uv_rotation_radians = reference->uv_transform.rotation_radians;
    desc.uv_set = reference->uv_set;
    return desc;
  }

  [[nodiscard]] auto Normalize01(const float value) -> float
  {
    return std::clamp(value, 0.0f, 1.0f);
  }

  [[nodiscard]] auto ResolveMaterialKey(const ImportRequest& request,
    std::string_view virtual_path) -> data::AssetKey
  {
    switch (request.options.asset_key_policy) {
    case AssetKeyPolicy::kRandom:
      return util::MakeRandomAssetKey();
    case AssetKeyPolicy::kDeterministicFromVirtualPath:
      return util::MakeDeterministicAssetKey(virtual_path);
    }
    return util::MakeDeterministicAssetKey(virtual_path);
  }

  [[nodiscard]] auto ResolveMaterialDomain(const data::MaterialDomain domain,
    const MaterialAlphaMode alpha_mode, uint32_t& flags) -> data::MaterialDomain
  {
    auto resolved = domain;

    if (alpha_mode == MaterialAlphaMode::kMasked) {
      flags |= data::pak::kMaterialFlag_AlphaTest;
      if (domain != data::MaterialDomain::kDecal
        && domain != data::MaterialDomain::kUserInterface
        && domain != data::MaterialDomain::kPostProcess) {
        resolved = data::MaterialDomain::kMasked;
      }
    } else if (alpha_mode == MaterialAlphaMode::kBlended) {
      if (domain != data::MaterialDomain::kDecal
        && domain != data::MaterialDomain::kUserInterface
        && domain != data::MaterialDomain::kPostProcess) {
        resolved = data::MaterialDomain::kAlphaBlended;
      }
    }

    return resolved;
  }

  auto ApplyMaterialInputs(const MaterialInputs& inputs,
    const MaterialAlphaMode alpha_mode, std::string_view source_id,
    std::string_view object_path, std::vector<ImportDiagnostic>& diagnostics,
    data::pak::MaterialAssetDesc& desc) -> void
  {
    desc.base_color[0] = Normalize01(inputs.base_color[0]);
    desc.base_color[1] = Normalize01(inputs.base_color[1]);
    desc.base_color[2] = Normalize01(inputs.base_color[2]);
    desc.base_color[3] = Normalize01(inputs.base_color[3]);

    desc.normal_scale = (std::max)(0.0f, inputs.normal_scale);

    float roughness = inputs.roughness;
    if (inputs.roughness_as_glossiness) {
      roughness = 1.0f - roughness;
    }

    desc.metalness = data::Unorm16 { Normalize01(inputs.metalness) };
    desc.roughness = data::Unorm16 { Normalize01(roughness) };
    desc.ambient_occlusion
      = data::Unorm16 { Normalize01(inputs.ambient_occlusion) };

    desc.emissive_factor[0] = data::HalfFloat { inputs.emissive_factor[0] };
    desc.emissive_factor[1] = data::HalfFloat { inputs.emissive_factor[1] };
    desc.emissive_factor[2] = data::HalfFloat { inputs.emissive_factor[2] };

    float alpha_cutoff = inputs.alpha_cutoff;
    if (alpha_mode == MaterialAlphaMode::kMasked
      && (alpha_cutoff < 0.0f || alpha_cutoff > 1.0f)) {
      diagnostics.push_back(MakeWarningDiagnostic("material.alpha_cutoff_range",
        "Alpha cutoff outside [0,1] was clamped", source_id, object_path));
    }
    alpha_cutoff = Normalize01(alpha_cutoff);
    desc.alpha_cutoff = data::Unorm16 { alpha_cutoff };

    desc.ior = (std::max)(1.0f, inputs.ior);
    desc.specular_factor
      = data::Unorm16 { Normalize01(inputs.specular_factor) };

    desc.sheen_color_factor[0]
      = data::HalfFloat { Normalize01(inputs.sheen_color_factor[0]) };
    desc.sheen_color_factor[1]
      = data::HalfFloat { Normalize01(inputs.sheen_color_factor[1]) };
    desc.sheen_color_factor[2]
      = data::HalfFloat { Normalize01(inputs.sheen_color_factor[2]) };

    desc.clearcoat_factor
      = data::Unorm16 { Normalize01(inputs.clearcoat_factor) };
    desc.clearcoat_roughness
      = data::Unorm16 { Normalize01(inputs.clearcoat_roughness) };
    desc.transmission_factor
      = data::Unorm16 { Normalize01(inputs.transmission_factor) };
    desc.thickness_factor
      = data::Unorm16 { Normalize01(inputs.thickness_factor) };

    desc.attenuation_color[0]
      = data::HalfFloat { Normalize01(inputs.attenuation_color[0]) };
    desc.attenuation_color[1]
      = data::HalfFloat { Normalize01(inputs.attenuation_color[1]) };
    desc.attenuation_color[2]
      = data::HalfFloat { Normalize01(inputs.attenuation_color[2]) };
    desc.attenuation_distance = (std::max)(0.0f, inputs.attenuation_distance);
  }

  [[nodiscard]] auto HasAnyAssignedTextures(
    const MaterialTextureBindings& textures) -> bool
  {
    return textures.base_color.assigned || textures.normal.assigned
      || textures.metallic.assigned || textures.roughness.assigned
      || textures.ambient_occlusion.assigned || textures.emissive.assigned
      || textures.specular.assigned || textures.sheen_color.assigned
      || textures.clearcoat.assigned || textures.clearcoat_normal.assigned
      || textures.transmission.assigned || textures.thickness.assigned;
  }

  auto AssignTextureIndices(const MaterialTextureBindings& textures,
    const bool orm_packed, const data::pak::ResourceIndexT orm_index,
    data::pak::MaterialAssetDesc& desc) -> void
  {
    desc.base_color_texture = textures.base_color.assigned
      ? static_cast<data::pak::ResourceIndexT>(textures.base_color.index)
      : data::pak::kNoResourceIndex;
    desc.normal_texture = textures.normal.assigned
      ? static_cast<data::pak::ResourceIndexT>(textures.normal.index)
      : data::pak::kNoResourceIndex;

    const auto metallic_index = textures.metallic.assigned
      ? static_cast<data::pak::ResourceIndexT>(textures.metallic.index)
      : data::pak::kNoResourceIndex;
    const auto roughness_index = textures.roughness.assigned
      ? static_cast<data::pak::ResourceIndexT>(textures.roughness.index)
      : data::pak::kNoResourceIndex;
    const auto ao_index = textures.ambient_occlusion.assigned
      ? static_cast<data::pak::ResourceIndexT>(textures.ambient_occlusion.index)
      : data::pak::kNoResourceIndex;

    if (orm_packed) {
      desc.metallic_texture = orm_index;
      desc.roughness_texture = orm_index;
      // If ORM is packed (flag set), the shader defaults to reading AO from the
      // Red channel of the ORM texture. We only override this if the material
      // explicitly assigns a different texture for AO.
      if (textures.ambient_occlusion.assigned
        && textures.ambient_occlusion.source_id
          != textures.metallic.source_id) {
        desc.ambient_occlusion_texture = ao_index;
      } else {
        desc.ambient_occlusion_texture = orm_index;
      }
    } else {
      desc.metallic_texture = metallic_index;
      desc.roughness_texture = roughness_index;
      desc.ambient_occlusion_texture = ao_index;
    }

    desc.emissive_texture = textures.emissive.assigned
      ? static_cast<data::pak::ResourceIndexT>(textures.emissive.index)
      : data::pak::kNoResourceIndex;
    desc.specular_texture = textures.specular.assigned
      ? static_cast<data::pak::ResourceIndexT>(textures.specular.index)
      : data::pak::kNoResourceIndex;
    desc.sheen_color_texture = textures.sheen_color.assigned
      ? static_cast<data::pak::ResourceIndexT>(textures.sheen_color.index)
      : data::pak::kNoResourceIndex;
    desc.clearcoat_texture = textures.clearcoat.assigned
      ? static_cast<data::pak::ResourceIndexT>(textures.clearcoat.index)
      : data::pak::kNoResourceIndex;
    desc.clearcoat_normal_texture = textures.clearcoat_normal.assigned
      ? static_cast<data::pak::ResourceIndexT>(textures.clearcoat_normal.index)
      : data::pak::kNoResourceIndex;
    desc.transmission_texture = textures.transmission.assigned
      ? static_cast<data::pak::ResourceIndexT>(textures.transmission.index)
      : data::pak::kNoResourceIndex;
    desc.thickness_texture = textures.thickness.assigned
      ? static_cast<data::pak::ResourceIndexT>(textures.thickness.index)
      : data::pak::kNoResourceIndex;
  }

  [[nodiscard]] auto ResolveOrmPacked(const OrmPolicy policy,
    const MaterialTextureBindings& textures, std::string_view source_id,
    std::string_view object_path, std::vector<ImportDiagnostic>& diagnostics)
    -> std::optional<data::pak::ResourceIndexT>
  {
    const auto& metallic = textures.metallic;
    const auto& roughness = textures.roughness;

    // We primarily check if Metallic and Roughness are compatible for packing,
    // as they are the core of the glTF PBR model (shared texture, usually).
    // The shader flag kMaterialFlag_GltfOrmPacked implies M is in Blue and R
    // is in Green. Even if AO is separate or missing, we must enable this flag
    // to read M/R from the correct channels.
    const bool mr_assigned = metallic.assigned && roughness.assigned;
    const bool mr_same_source = metallic.source_id == roughness.source_id
      && !metallic.source_id.empty();
    const bool mr_same_uv_set = metallic.uv_set == roughness.uv_set;
    const auto& mr_uv_a = metallic.uv_transform;
    const auto& mr_uv_b = roughness.uv_transform;
    const bool mr_same_uv_transform = mr_uv_a.scale[0] == mr_uv_b.scale[0]
      && mr_uv_a.scale[1] == mr_uv_b.scale[1]
      && mr_uv_a.offset[0] == mr_uv_b.offset[0]
      && mr_uv_a.offset[1] == mr_uv_b.offset[1]
      && mr_uv_a.rotation_radians == mr_uv_b.rotation_radians;
    const bool mr_same_uv = mr_same_uv_set && mr_same_uv_transform;

    const bool can_pack = mr_assigned && mr_same_source && mr_same_uv;

    if (policy == OrmPolicy::kForcePacked) {
      if (!can_pack) {
        diagnostics.push_back(MakeErrorDiagnostic("material.orm_policy",
          "ForcePacked requires metallic/roughness to share source and UV",
          source_id, object_path));
        return std::nullopt;
      }
      return static_cast<data::pak::ResourceIndexT>(metallic.index);
    }

    if (policy == OrmPolicy::kAuto && can_pack) {
      return static_cast<data::pak::ResourceIndexT>(metallic.index);
    }

    return std::nullopt;
  }

  [[nodiscard]] auto SerializeMaterialDescriptor(
    const data::pak::MaterialAssetDesc& desc,
    const std::vector<data::pak::ShaderReferenceDesc>& shader_refs)
    -> std::vector<std::byte>
  {
    serio::MemoryStream stream;
    serio::Writer writer(stream);
    const auto pack = writer.ScopedAlignment(1);

    (void)writer.WriteBlob(std::as_bytes(
      std::span<const data::pak::MaterialAssetDesc, 1>(&desc, 1)));
    if (!shader_refs.empty()) {
      (void)writer.WriteBlob(std::as_bytes(std::span(shader_refs)));
    }

    const auto data = stream.Data();
    return std::vector(data.begin(), data.end());
  }

  auto PatchContentHash(
    std::vector<std::byte>& bytes, const uint64_t content_hash) -> void
  {
    constexpr size_t kOffset = offsetof(data::pak::MaterialAssetDesc, header)
      + offsetof(data::pak::AssetHeader, content_hash);
    if (bytes.size() < kOffset + sizeof(content_hash)) {
      return;
    }
    std::memcpy(bytes.data() + kOffset, &content_hash, sizeof(content_hash));
  }

  [[nodiscard]] auto ComputeContentHashOnThreadPool(co::ThreadPool& thread_pool,
    std::span<const std::byte> bytes, std::stop_token stop_token)
    -> co::Co<std::optional<uint64_t>>
  {
    const auto hash = co_await thread_pool.Run(
      [bytes, stop_token](co::ThreadPool::CancelToken canceled) noexcept {
        DLOG_F(1, "MaterialPipeline: Compute content hash");
        if (stop_token.stop_requested() || canceled) {
          return uint64_t { 0 };
        }
        return util::ComputeContentHash(bytes);
      });

    if (stop_token.stop_requested() || hash == 0) {
      co_return std::nullopt;
    }

    co_return hash;
  }

  [[nodiscard]] auto BuildMaterialPayload(
    const MaterialPipeline::WorkItem& item,
    std::vector<ImportDiagnostic> diagnostics) -> BuildOutcome
  {
    DLOG_F(1, "Building material payload: {}", item.material_name);
    BuildOutcome outcome {
      .diagnostics = std::move(diagnostics),
    };

    if (item.stop_token.stop_requested()) {
      outcome.canceled = true;
      return outcome;
    }

    const auto object_path = std::string_view(item.material_name);

    data::pak::MaterialAssetDesc desc {};
    desc.header.asset_type = static_cast<uint8_t>(data::AssetType::kMaterial);
    desc.header.version = data::pak::kMaterialAssetVersion;
    util::TruncateAndNullTerminate(
      desc.header.name, std::size(desc.header.name), item.material_name);

    desc.flags = data::pak::kMaterialFlag_NoTextureSampling;
    if (item.inputs.double_sided) {
      desc.flags |= data::pak::kMaterialFlag_DoubleSided;
    }
    if (item.inputs.unlit) {
      desc.flags |= data::pak::kMaterialFlag_Unlit;
    }

    const auto resolved_domain = ResolveMaterialDomain(
      item.material_domain, item.alpha_mode, desc.flags);
    desc.material_domain = static_cast<uint8_t>(resolved_domain);

    ApplyMaterialInputs(item.inputs, item.alpha_mode, item.source_id,
      object_path, outcome.diagnostics, desc);

    const auto orm_index = ResolveOrmPacked(item.orm_policy, item.textures,
      item.source_id, object_path, outcome.diagnostics);
    const bool orm_packed = orm_index.has_value();

    if (HasErrorDiagnostic(outcome.diagnostics)) {
      outcome.has_error = true;
      return outcome;
    }

    if (orm_packed) {
      desc.flags |= data::pak::kMaterialFlag_GltfOrmPacked;
    }

    const auto any_textures = HasAnyAssignedTextures(item.textures);
    if (any_textures) {
      desc.flags &= ~data::pak::kMaterialFlag_NoTextureSampling;
    } else {
      DLOG_F(INFO,
        "Material '{}' has no assigned textures; using scalar fallbacks",
        item.source_id);
    }

    AssignTextureIndices(
      item.textures, orm_packed, orm_packed ? *orm_index : 0, desc);

    const std::vector bindings {
      &item.textures.base_color,
      &item.textures.normal,
      &item.textures.metallic,
      &item.textures.roughness,
      &item.textures.ambient_occlusion,
      &item.textures.emissive,
      &item.textures.specular,
      &item.textures.sheen_color,
      &item.textures.clearcoat,
      &item.textures.clearcoat_normal,
      &item.textures.transmission,
      &item.textures.thickness,
    };

    const auto uv_desc = BuildMaterialUvTransformDesc(bindings);
    desc.uv_scale[0] = uv_desc.uv_scale[0];
    desc.uv_scale[1] = uv_desc.uv_scale[1];
    desc.uv_offset[0] = uv_desc.uv_offset[0];
    desc.uv_offset[1] = uv_desc.uv_offset[1];
    desc.uv_rotation_radians = uv_desc.uv_rotation_radians;
    desc.uv_set = uv_desc.uv_set;

    auto shader_requests = item.shader_requests;
    if (shader_requests.empty()) {
      shader_requests = BuildDefaultShaderRequests(resolved_domain, desc.flags);
    }

    auto shader_build = BuildShaderReferences(std::move(shader_requests),
      item.source_id, object_path, outcome.diagnostics);
    if (shader_build.has_error) {
      outcome.has_error = true;
      return outcome;
    }

    desc.shader_stages = shader_build.shader_stages;

    outcome.bytes = SerializeMaterialDescriptor(desc, shader_build.shader_refs);
    outcome.has_error = HasErrorDiagnostic(outcome.diagnostics);

    return outcome;
  }

} // namespace

MaterialPipeline::MaterialPipeline(co::ThreadPool& thread_pool, Config config)
  : thread_pool_(thread_pool)
  , config_(config)
  , input_channel_(config.queue_capacity)
  , output_channel_(config.queue_capacity)
{
}

MaterialPipeline::~MaterialPipeline()
{
  if (started_) {
    DLOG_IF_F(WARNING, HasPending(),
      "MaterialPipeline destroyed with {} pending items", PendingCount());
  }

  input_channel_.Close();
  output_channel_.Close();
}

auto MaterialPipeline::Start(co::Nursery& nursery) -> void
{
  DCHECK_F(!started_, "MaterialPipeline::Start() called more than once");
  started_ = true;

  const auto worker_count = std::max(1U, config_.worker_count);
  for (uint32_t i = 0; i < worker_count; ++i) {
    nursery.Start([this]() -> co::Co<> { co_await Worker(); });
  }
}

auto MaterialPipeline::Submit(WorkItem item) -> co::Co<>
{
  ++pending_;
  submitted_.fetch_add(1, std::memory_order_acq_rel);
  co_await input_channel_.Send(std::move(item));
}

auto MaterialPipeline::TrySubmit(WorkItem item) -> bool
{
  if (input_channel_.Closed()) {
    return false;
  }

  if (input_channel_.Full()) {
    return false;
  }

  const auto ok = input_channel_.TrySend(std::move(item));
  if (ok) {
    ++pending_;
    submitted_.fetch_add(1, std::memory_order_acq_rel);
  }
  return ok;
}

auto MaterialPipeline::Collect() -> co::Co<WorkResult>
{
  auto maybe_result = co_await output_channel_.Receive();
  if (!maybe_result.has_value()) {
    co_return WorkResult {
      .source_id = {},
      .cooked = std::nullopt,
      .diagnostics = {},
      .success = false,
    };
  }

  pending_.fetch_sub(1, std::memory_order_acq_rel);
  if (maybe_result->success) {
    completed_.fetch_add(1, std::memory_order_acq_rel);
  } else {
    failed_.fetch_add(1, std::memory_order_acq_rel);
  }
  co_return std::move(*maybe_result);
}

auto MaterialPipeline::Close() -> void { input_channel_.Close(); }

auto MaterialPipeline::HasPending() const noexcept -> bool
{
  return pending_.load(std::memory_order_acquire) > 0;
}

auto MaterialPipeline::PendingCount() const noexcept -> size_t
{
  return pending_.load(std::memory_order_acquire);
}

auto MaterialPipeline::GetProgress() const noexcept -> PipelineProgress
{
  const auto submitted = submitted_.load(std::memory_order_acquire);
  const auto completed = completed_.load(std::memory_order_acquire);
  const auto failed = failed_.load(std::memory_order_acquire);
  return PipelineProgress {
    .submitted = submitted,
    .completed = completed,
    .failed = failed,
    .in_flight = submitted - completed - failed,
    .throughput = 0.0F,
  };
}

auto MaterialPipeline::Worker() -> co::Co<>
{
  while (true) {
    auto maybe_item = co_await input_channel_.Receive();
    if (!maybe_item.has_value()) {
      break;
    }

    auto item = std::move(*maybe_item);
    if (item.stop_token.stop_requested()) {
      co_await ReportCancelled(std::move(item));
      continue;
    }

    const auto virtual_path
      = item.request.loose_cooked_layout.MaterialVirtualPath(
        item.storage_material_name);
    const auto descriptor_relpath
      = item.request.loose_cooked_layout.MaterialDescriptorRelPath(
        item.storage_material_name);
    const auto material_key = ResolveMaterialKey(item.request, virtual_path);

    BuildOutcome build_outcome;
    if (config_.use_thread_pool) {
      auto item_copy = item;
      build_outcome = co_await thread_pool_.Run(
        [item = std::move(item_copy)](
          co::ThreadPool::CancelToken canceled) noexcept {
          DLOG_F(1, "MaterialPipeline: Build material task begin");
          if (item.stop_token.stop_requested() || canceled) {
            return BuildOutcome { .canceled = true };
          }
          return BuildMaterialPayload(item, {});
        });
    } else {
      DLOG_F(2,
        "MaterialPipeline: BuildMaterialPayload on import thread material={}",
        item.material_name);
      build_outcome = BuildMaterialPayload(item, {});
    }
    if (build_outcome.canceled) {
      co_await ReportCancelled(std::move(item));
      continue;
    }

    WorkResult output {
      .source_id = item.source_id,
      .cooked = std::nullopt,
      .diagnostics = std::move(build_outcome.diagnostics),
      .success = false,
    };

    if (build_outcome.has_error) {
      co_await output_channel_.Send(std::move(output));
      continue;
    }

    if (build_outcome.bytes.empty()) {
      output.diagnostics.push_back(MakeErrorDiagnostic(
        "material.serialize_failed", "Material descriptor serialization failed",
        item.source_id, item.material_name));
      co_await output_channel_.Send(std::move(output));
      continue;
    }

    auto bytes = build_outcome.bytes;

    if (config_.with_content_hashing) {
      auto hash = co_await ComputeContentHashOnThreadPool(thread_pool_,
        std::span<const std::byte>(bytes.data(), bytes.size()),
        item.stop_token);
      if (!hash.has_value()) {
        co_await ReportCancelled(std::move(item));
        continue;
      }

      PatchContentHash(bytes, *hash);
    }

    output.cooked = CookedMaterialPayload {
      .material_key = material_key,
      .virtual_path = virtual_path,
      .descriptor_relpath = descriptor_relpath,
      .descriptor_bytes = std::move(bytes),
    };
    output.success = true;

    co_await output_channel_.Send(std::move(output));
  }

  co_return;
}

auto MaterialPipeline::ReportCancelled(WorkItem item) -> co::Co<>
{
  WorkResult canceled {
    .source_id = std::move(item.source_id),
    .cooked = std::nullopt,
    .diagnostics = {},
    .success = false,
  };
  co_await output_channel_.Send(std::move(canceled));
}

} // namespace oxygen::content::import
