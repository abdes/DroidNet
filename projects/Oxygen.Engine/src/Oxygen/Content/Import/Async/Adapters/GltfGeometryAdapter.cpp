//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <Oxygen/Content/Import/Async/Adapters/GltfGeometryAdapter.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <numeric>
#include <optional>
#include <span>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include <glm/glm.hpp>

#include <Oxygen/Base/Logging.h>
#include <Oxygen/Content/Import/gltf/cgltf.h>
#include <Oxygen/Content/Import/util/ImportNaming.h>

namespace oxygen::content::import::adapters {

namespace {

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

  [[nodiscard]] auto MakeCancelDiagnostic(std::string_view source_id)
    -> ImportDiagnostic
  {
    return ImportDiagnostic {
      .severity = ImportSeverity::kError,
      .code = "import.cancelled",
      .message = "Import cancelled",
      .source_path = std::string(source_id),
      .object_path = {},
    };
  }

  [[nodiscard]] auto ResultToMessage(const cgltf_result result) -> const char*
  {
    switch (result) {
    case cgltf_result_success:
      return "success";
    case cgltf_result_data_too_short:
      return "data too short";
    case cgltf_result_unknown_format:
      return "unknown format";
    case cgltf_result_invalid_json:
      return "invalid json";
    case cgltf_result_invalid_gltf:
      return "invalid gltf";
    case cgltf_result_out_of_memory:
      return "out of memory";
    case cgltf_result_legacy_gltf:
      return "legacy gltf";
    case cgltf_result_io_error:
      return "io error";
    default:
      return "unknown error";
    }
  }

  [[nodiscard]] auto MakeParseDiagnostic(
    std::string_view source_id, const cgltf_result result) -> ImportDiagnostic
  {
    return ImportDiagnostic {
      .severity = ImportSeverity::kError,
      .code = "gltf.parse_failed",
      .message = std::string(ResultToMessage(result)),
      .source_path = std::string(source_id),
      .object_path = {},
    };
  }

  using CgltfDataPtr = std::unique_ptr<cgltf_data, decltype(&cgltf_free)>;

  [[nodiscard]] auto LoadDataFromFile(const std::filesystem::path& path,
    const GeometryAdapterInput& input,
    std::vector<ImportDiagnostic>& diagnostics) -> CgltfDataPtr
  {
    if (input.stop_token.stop_requested()) {
      DLOG_F(
        WARNING, "glTF load cancelled: source_id='{}'", input.source_id_prefix);
      diagnostics.push_back(MakeCancelDiagnostic(input.source_id_prefix));
      return { nullptr, &cgltf_free };
    }

    cgltf_options options {};
    cgltf_data* data = nullptr;
    const auto parse_result
      = cgltf_parse_file(&options, path.string().c_str(), &data);
    if (parse_result != cgltf_result_success) {
      DLOG_F(ERROR, "glTF parse failed: path='{}' result='{}'", path.string(),
        ResultToMessage(parse_result));
      diagnostics.push_back(
        MakeParseDiagnostic(input.source_id_prefix, parse_result));
      return { nullptr, &cgltf_free };
    }

    const auto load_result
      = cgltf_load_buffers(&options, data, path.string().c_str());
    if (load_result != cgltf_result_success) {
      DLOG_F(ERROR, "glTF buffer load failed: path='{}' result='{}'",
        path.string(), ResultToMessage(load_result));
      diagnostics.push_back(
        MakeParseDiagnostic(input.source_id_prefix, load_result));
      cgltf_free(data);
      return { nullptr, &cgltf_free };
    }

    return { data, &cgltf_free };
  }

  [[nodiscard]] auto LoadDataFromMemory(const std::span<const std::byte> bytes,
    const GeometryAdapterInput& input,
    std::vector<ImportDiagnostic>& diagnostics) -> CgltfDataPtr
  {
    if (input.stop_token.stop_requested()) {
      DLOG_F(WARNING, "glTF load cancelled (memory): source_id='{}'",
        input.source_id_prefix);
      diagnostics.push_back(MakeCancelDiagnostic(input.source_id_prefix));
      return { nullptr, &cgltf_free };
    }

    cgltf_options options {};
    cgltf_data* data = nullptr;
    const auto parse_result = cgltf_parse(
      &options, bytes.data(), static_cast<cgltf_size>(bytes.size()), &data);
    if (parse_result != cgltf_result_success) {
      DLOG_F(ERROR, "glTF parse failed (memory): result='{}'",
        ResultToMessage(parse_result));
      diagnostics.push_back(
        MakeParseDiagnostic(input.source_id_prefix, parse_result));
      return { nullptr, &cgltf_free };
    }

    const auto load_result = cgltf_load_buffers(&options, data, "");
    if (load_result != cgltf_result_success) {
      DLOG_F(ERROR, "glTF buffer load failed (memory): result='{}'",
        ResultToMessage(load_result));
      diagnostics.push_back(
        MakeParseDiagnostic(input.source_id_prefix, load_result));
      cgltf_free(data);
      return { nullptr, &cgltf_free };
    }

    return { data, &cgltf_free };
  }

  [[nodiscard]] auto ReadVec2(const cgltf_accessor* accessor)
    -> std::vector<glm::vec2>
  {
    if (accessor == nullptr) {
      return {};
    }

    std::vector<glm::vec2> out(accessor->count);
    for (cgltf_size i = 0; i < accessor->count; ++i) {
      cgltf_float v[4] = {};
      cgltf_accessor_read_float(accessor, i, v, 4);
      out[i] = glm::vec2 {
        static_cast<float>(v[0]),
        static_cast<float>(v[1]),
      };
    }
    return out;
  }

  [[nodiscard]] auto ReadVec3(const cgltf_accessor* accessor)
    -> std::vector<glm::vec3>
  {
    if (accessor == nullptr) {
      return {};
    }

    std::vector<glm::vec3> out(accessor->count);
    for (cgltf_size i = 0; i < accessor->count; ++i) {
      cgltf_float v[4] = {};
      cgltf_accessor_read_float(accessor, i, v, 4);
      out[i] = glm::vec3 {
        static_cast<float>(v[0]),
        static_cast<float>(v[1]),
        static_cast<float>(v[2]),
      };
    }
    return out;
  }

  [[nodiscard]] auto ReadVec4(const cgltf_accessor* accessor)
    -> std::vector<glm::vec4>
  {
    if (accessor == nullptr) {
      return {};
    }

    std::vector<glm::vec4> out(accessor->count);
    for (cgltf_size i = 0; i < accessor->count; ++i) {
      cgltf_float v[4] = {};
      cgltf_accessor_read_float(accessor, i, v, 4);
      out[i] = glm::vec4 {
        static_cast<float>(v[0]),
        static_cast<float>(v[1]),
        static_cast<float>(v[2]),
        static_cast<float>(v[3]),
      };
    }
    return out;
  }

  [[nodiscard]] auto ReadUVec4(const cgltf_accessor* accessor)
    -> std::vector<glm::uvec4>
  {
    if (accessor == nullptr) {
      return {};
    }

    std::vector<glm::uvec4> out(accessor->count);
    for (cgltf_size i = 0; i < accessor->count; ++i) {
      cgltf_uint v[4] = {};
      cgltf_accessor_read_uint(accessor, i, v, 4);
      out[i] = glm::uvec4 { v[0], v[1], v[2], v[3] };
    }
    return out;
  }

  [[nodiscard]] auto ReadMat4(const cgltf_accessor* accessor)
    -> std::vector<glm::mat4>
  {
    if (accessor == nullptr) {
      return {};
    }

    std::vector<glm::mat4> out(accessor->count);
    for (cgltf_size i = 0; i < accessor->count; ++i) {
      cgltf_float v[16] = {};
      cgltf_accessor_read_float(accessor, i, v, 16);
      out[i] = glm::mat4 {
        glm::vec4 { static_cast<float>(v[0]), static_cast<float>(v[1]),
          static_cast<float>(v[2]), static_cast<float>(v[3]) },
        glm::vec4 { static_cast<float>(v[4]), static_cast<float>(v[5]),
          static_cast<float>(v[6]), static_cast<float>(v[7]) },
        glm::vec4 { static_cast<float>(v[8]), static_cast<float>(v[9]),
          static_cast<float>(v[10]), static_cast<float>(v[11]) },
        glm::vec4 { static_cast<float>(v[12]), static_cast<float>(v[13]),
          static_cast<float>(v[14]), static_cast<float>(v[15]) },
      };
    }
    return out;
  }

  [[nodiscard]] auto ReadAccessorBounds(const cgltf_accessor* accessor)
    -> std::optional<Bounds3>
  {
    if (accessor == nullptr || !accessor->has_min || !accessor->has_max) {
      return std::nullopt;
    }
    if (accessor->type != cgltf_type_vec3) {
      return std::nullopt;
    }

    Bounds3 bounds {};
    bounds.min = { static_cast<float>(accessor->min[0]),
      static_cast<float>(accessor->min[1]),
      static_cast<float>(accessor->min[2]) };
    bounds.max = { static_cast<float>(accessor->max[0]),
      static_cast<float>(accessor->max[1]),
      static_cast<float>(accessor->max[2]) };
    return bounds;
  }

  [[nodiscard]] auto ReadIndices(const cgltf_accessor* accessor)
    -> std::vector<uint32_t>
  {
    if (accessor == nullptr) {
      return {};
    }

    std::vector<uint32_t> out(accessor->count);
    for (cgltf_size i = 0; i < accessor->count; ++i) {
      out[i] = static_cast<uint32_t>(cgltf_accessor_read_index(accessor, i));
    }
    return out;
  }

  [[nodiscard]] auto DetermineJointCount(
    const std::vector<glm::uvec4>& joint_indices) -> uint32_t
  {
    if (joint_indices.empty()) {
      return 0;
    }

    uint32_t max_joint = 0;
    for (const auto& joints : joint_indices) {
      max_joint = (std::max)(max_joint, joints.x);
      max_joint = (std::max)(max_joint, joints.y);
      max_joint = (std::max)(max_joint, joints.z);
      max_joint = (std::max)(max_joint, joints.w);
    }
    return max_joint + 1;
  }

  [[nodiscard]] auto FindSkinForMesh(
    const cgltf_data& data, const cgltf_mesh& mesh) -> const cgltf_skin*
  {
    for (cgltf_size i = 0; i < data.nodes_count; ++i) {
      const auto& node = data.nodes[i];
      if (node.mesh == &mesh && node.skin != nullptr) {
        return node.skin;
      }
    }
    return nullptr;
  }

  [[nodiscard]] auto HasMaterialTextures(const cgltf_material* material) -> bool
  {
    if (material == nullptr) {
      return false;
    }

    if (material->has_pbr_metallic_roughness) {
      const auto& pbr = material->pbr_metallic_roughness;
      if (pbr.base_color_texture.texture != nullptr
        || pbr.metallic_roughness_texture.texture != nullptr) {
        return true;
      }
    }

    if (material->normal_texture.texture != nullptr
      || material->occlusion_texture.texture != nullptr
      || material->emissive_texture.texture != nullptr) {
      return true;
    }

    return false;
  }

  [[nodiscard]] auto BuildSourceId(std::string_view prefix,
    std::string_view name, uint32_t ordinal) -> std::string
  {
    std::string id;
    if (!prefix.empty()) {
      id = std::string(prefix);
      id.append("::");
    }
    if (!name.empty()) {
      id.append(name.begin(), name.end());
    } else {
      id.append("mesh_");
      id.append(std::to_string(ordinal));
    }
    return id;
  }

  [[nodiscard]] auto BuildWorkItemsFromData(const cgltf_data& data,
    const GeometryAdapterInput& input) -> GeometryAdapterOutput
  {
    GeometryAdapterOutput output;
    if (input.stop_token.stop_requested()) {
      output.success = false;
      output.diagnostics.push_back(
        MakeCancelDiagnostic(input.source_id_prefix));
      return output;
    }

    std::unordered_map<const cgltf_material*, uint32_t> material_index;
    for (cgltf_size i = 0; i < data.materials_count; ++i) {
      const auto* mat = &data.materials[i];
      if (mat != nullptr) {
        material_index.emplace(mat, static_cast<uint32_t>(i));
      }
    }

    std::unordered_map<std::string, uint32_t> name_usage;
    uint32_t mesh_ordinal = 0;

    for (cgltf_size mesh_i = 0; mesh_i < data.meshes_count; ++mesh_i) {
      const auto* mesh = &data.meshes[mesh_i];
      if (mesh == nullptr) {
        continue;
      }

      const std::string mesh_name = util::BuildMeshName(mesh->name != nullptr
          ? std::string_view(mesh->name)
          : std::string_view {},
        input.request, static_cast<uint32_t>(mesh_i));

      for (cgltf_size prim_i = 0; prim_i < mesh->primitives_count; ++prim_i) {
        if (input.stop_token.stop_requested()) {
          output.success = false;
          output.diagnostics.push_back(
            MakeCancelDiagnostic(input.source_id_prefix));
          return output;
        }

        const auto& prim = mesh->primitives[prim_i];
        if (prim.type != cgltf_primitive_type_triangles) {
          output.diagnostics.push_back(MakeErrorDiagnostic(
            "gltf.primitive.type", "glTF primitive is not triangle list",
            input.source_id_prefix, mesh_name));
          output.success = false;
          continue;
        }

        const cgltf_accessor* positions = nullptr;
        const cgltf_accessor* normals = nullptr;
        const cgltf_accessor* texcoords = nullptr;
        const cgltf_accessor* tangents = nullptr;
        const cgltf_accessor* colors = nullptr;
        const cgltf_accessor* joints = nullptr;
        const cgltf_accessor* weights = nullptr;

        for (cgltf_size attr_i = 0; attr_i < prim.attributes_count; ++attr_i) {
          const auto& attr = prim.attributes[attr_i];
          switch (attr.type) {
          case cgltf_attribute_type_position:
            positions = attr.data;
            break;
          case cgltf_attribute_type_normal:
            normals = attr.data;
            break;
          case cgltf_attribute_type_texcoord:
            if (attr.index == 0) {
              texcoords = attr.data;
            }
            break;
          case cgltf_attribute_type_tangent:
            tangents = attr.data;
            break;
          case cgltf_attribute_type_color:
            if (attr.index == 0) {
              colors = attr.data;
            }
            break;
          case cgltf_attribute_type_joints:
            if (attr.index == 0) {
              joints = attr.data;
            }
            break;
          case cgltf_attribute_type_weights:
            if (attr.index == 0) {
              weights = attr.data;
            }
            break;
          default:
            break;
          }
        }

        if (positions == nullptr) {
          output.diagnostics.push_back(
            MakeErrorDiagnostic("mesh.missing_positions",
              "glTF primitive missing POSITION attribute",
              input.source_id_prefix, mesh_name));
          output.success = false;
          continue;
        }

        const auto bounds = ReadAccessorBounds(positions);
        auto positions_vec = ReadVec3(positions);
        auto normals_vec = ReadVec3(normals);
        auto texcoords_vec = ReadVec2(texcoords);
        auto tangents_vec = ReadVec4(tangents);
        auto colors_vec = ReadVec4(colors);
        auto joint_indices_vec = ReadUVec4(joints);
        auto joint_weights_vec = ReadVec4(weights);

        if (positions_vec.empty()) {
          DLOG_F(WARNING,
            "glTF primitive contains no vertex positions: source_id='{}' "
            "mesh='{}'",
            input.source_id_prefix, mesh_name);
          output.diagnostics.push_back(
            MakeErrorDiagnostic("mesh.missing_positions",
              "glTF primitive contains no vertex positions",
              input.source_id_prefix, mesh_name));
          output.success = false;
          continue;
        }

        auto indices_vec = ReadIndices(prim.indices);
        if (indices_vec.empty()) {
          output.diagnostics.push_back(
            MakeWarningDiagnostic("gltf.missing_indices",
              "glTF primitive missing indices; generated sequential indices",
              input.source_id_prefix, mesh_name));
          indices_vec.resize(positions_vec.size());
          for (size_t i = 0; i < indices_vec.size(); ++i) {
            indices_vec[i] = static_cast<uint32_t>(i);
          }
        }

        if (indices_vec.empty()) {
          DLOG_F(WARNING,
            "glTF primitive contains no indices: source_id='{}' mesh='{}'",
            input.source_id_prefix, mesh_name);
          output.diagnostics.push_back(MakeErrorDiagnostic(
            "mesh.missing_indices", "glTF primitive contains no indices",
            input.source_id_prefix, mesh_name));
          output.success = false;
          continue;
        }

        if ((indices_vec.size() % 3U) != 0U) {
          DLOG_F(WARNING,
            "glTF primitive index count not multiple of 3: source_id='{}' "
            "mesh='{}' index_count={}",
            input.source_id_prefix, mesh_name, indices_vec.size());
          output.diagnostics.push_back(
            MakeWarningDiagnostic("mesh.invalid_range",
              "glTF primitive index count must be a multiple of 3",
              input.source_id_prefix, mesh_name));
          output.success = false;
          continue;
        }

        const auto material_slot = [&]() -> uint32_t {
          if (prim.material == nullptr) {
            return 0;
          }
          if (const auto it = material_index.find(prim.material);
            it != material_index.end()) {
            return it->second;
          }
          return 0;
        }();

        if (!input.material_keys.empty()
          && material_slot >= input.material_keys.size()) {
          output.diagnostics.push_back(
            MakeWarningDiagnostic("mesh.material_slot_oob",
              "glTF material slot exceeds imported material key count",
              input.source_id_prefix, mesh_name));
        }

        TriangleRange range {
          .material_slot = material_slot,
          .first_index = 0,
          .index_count = static_cast<uint32_t>(indices_vec.size()),
        };

        auto storage_name = mesh_name;
        if (mesh->primitives_count > 1) {
          storage_name += "_prim_" + std::to_string(prim_i);
        }
        if (const auto it = name_usage.find(storage_name);
          it != name_usage.end()) {
          storage_name += "_" + std::to_string(it->second);
        }
        name_usage[storage_name]++;

        GeometryPipeline::WorkItem item;
        item.source_id
          = BuildSourceId(input.source_id_prefix, storage_name, mesh_ordinal++);
        item.mesh_name = storage_name;
        item.storage_mesh_name
          = util::NamespaceImportedAssetName(input.request, storage_name);
        item.source_key = &prim;
        item.material_keys.assign(
          input.material_keys.begin(), input.material_keys.end());
        item.default_material_key = input.default_material_key;
        item.want_textures = true;
        item.has_material_textures = HasMaterialTextures(prim.material);
        item.request = input.request;
        item.stop_token = input.stop_token;

        struct PrimitiveBuffers final {
          std::vector<glm::vec3> positions;
          std::vector<glm::vec3> normals;
          std::vector<glm::vec2> texcoords;
          std::vector<glm::vec3> tangents;
          std::vector<glm::vec3> bitangents;
          std::vector<glm::vec4> colors;
          std::vector<glm::uvec4> joint_indices;
          std::vector<glm::vec4> joint_weights;
          std::vector<glm::mat4> inverse_bind_matrices;
          std::vector<uint32_t> joint_remap;
          std::vector<uint32_t> indices;
          std::vector<TriangleRange> ranges;
        };

        auto owner = std::make_shared<PrimitiveBuffers>();
        owner->positions = std::move(positions_vec);
        if (normals_vec.size() == owner->positions.size()) {
          owner->normals = std::move(normals_vec);
        }
        if (texcoords_vec.size() == owner->positions.size()) {
          owner->texcoords = std::move(texcoords_vec);
        }
        if (colors_vec.size() == owner->positions.size()) {
          owner->colors = std::move(colors_vec);
        }
        if (joint_indices_vec.size() == owner->positions.size()
          && joint_weights_vec.size() == owner->positions.size()) {
          owner->joint_indices = std::move(joint_indices_vec);
          owner->joint_weights = std::move(joint_weights_vec);
        }
        owner->indices = std::move(indices_vec);
        owner->ranges = { range };

        if (tangents_vec.size() == owner->positions.size()
          && owner->normals.size() == owner->positions.size()) {
          owner->tangents.reserve(tangents_vec.size());
          owner->bitangents.reserve(tangents_vec.size());
          for (size_t i = 0; i < tangents_vec.size(); ++i) {
            const auto& t = tangents_vec[i];
            const auto n = owner->normals[i];
            const auto tangent = glm::vec3 { t.x, t.y, t.z };
            owner->tangents.push_back(tangent);
            const auto bitangent = glm::cross(n, tangent) * t.w;
            owner->bitangents.push_back(bitangent);
          }
        }

        const bool is_skinned
          = !owner->joint_indices.empty() && !owner->joint_weights.empty();

        if (is_skinned) {
          const auto* skin = FindSkinForMesh(data, *mesh);
          if (skin == nullptr || skin->inverse_bind_matrices == nullptr) {
            output.diagnostics.push_back(
              MakeErrorDiagnostic("mesh.missing_inverse_bind",
                "glTF skinned mesh missing inverse bind matrices",
                input.source_id_prefix, mesh_name));
            output.success = false;
            continue;
          }

          owner->inverse_bind_matrices = ReadMat4(skin->inverse_bind_matrices);
          const uint32_t joint_count
            = DetermineJointCount(owner->joint_indices);
          if (joint_count == 0 || owner->inverse_bind_matrices.empty()) {
            output.diagnostics.push_back(
              MakeErrorDiagnostic("mesh.missing_inverse_bind",
                "glTF skinned mesh missing inverse bind matrices",
                input.source_id_prefix, mesh_name));
            output.success = false;
            continue;
          }
          if (owner->inverse_bind_matrices.size() < joint_count) {
            output.diagnostics.push_back(
              MakeErrorDiagnostic("mesh.skinning_buffers_mismatch",
                "glTF skin inverse bind count is smaller than joint count",
                input.source_id_prefix, mesh_name));
            output.success = false;
            continue;
          }

          owner->joint_remap.resize(joint_count);
          std::iota(owner->joint_remap.begin(), owner->joint_remap.end(), 0u);
        }

        TriangulatedMesh tri_mesh {
          .mesh_type = is_skinned ? data::MeshType::kSkinned
                                  : data::MeshType::kStandard,
          .streams = MeshStreamView {
            .positions = std::span<const glm::vec3>(
              owner->positions.data(), owner->positions.size()),
            .normals = std::span<const glm::vec3>(
              owner->normals.data(), owner->normals.size()),
            .texcoords = std::span<const glm::vec2>(
              owner->texcoords.data(), owner->texcoords.size()),
            .tangents = std::span<const glm::vec3>(
              owner->tangents.data(), owner->tangents.size()),
            .bitangents = std::span<const glm::vec3>(
              owner->bitangents.data(), owner->bitangents.size()),
            .colors = std::span<const glm::vec4>(
              owner->colors.data(), owner->colors.size()),
            .joint_indices = std::span<const glm::uvec4>(
              owner->joint_indices.data(), owner->joint_indices.size()),
            .joint_weights = std::span<const glm::vec4>(
              owner->joint_weights.data(), owner->joint_weights.size()),
          },
          .inverse_bind_matrices = std::span<const glm::mat4>(
            owner->inverse_bind_matrices.data(),
            owner->inverse_bind_matrices.size()),
          .joint_remap = std::span<const uint32_t>(
            owner->joint_remap.data(), owner->joint_remap.size()),
          .indices = std::span<const uint32_t>(
            owner->indices.data(), owner->indices.size()),
          .ranges = std::span<const TriangleRange>(
            owner->ranges.data(), owner->ranges.size()),
          .bounds = bounds,
        };

        item.lods = {
          MeshLod {
            .lod_name = "LOD0",
            .source = std::move(tri_mesh),
            .source_owner = std::move(owner),
          },
        };

        output.work_items.push_back(std::move(item));
      }
    }

    if (!output.success && output.diagnostics.empty()) {
      DLOG_F(ERROR, "glTF import failed without diagnostics: source_id='{}'",
        input.source_id_prefix);
      output.diagnostics.push_back(MakeErrorDiagnostic("gltf.unknown_failure",
        "glTF import failed without diagnostics", input.source_id_prefix, ""));
    }
    return output;
  }

} // namespace

auto GltfGeometryAdapter::BuildWorkItems(
  const std::filesystem::path& source_path,
  const GeometryAdapterInput& input) const -> GeometryAdapterOutput
{
  GeometryAdapterOutput output;
  auto data = LoadDataFromFile(source_path, input, output.diagnostics);
  if (data == nullptr) {
    DLOG_F(ERROR, "glTF load failed: path='{}' diagnostics={} ",
      source_path.string(), output.diagnostics.size());
    if (output.diagnostics.empty()) {
      output.diagnostics.push_back(MakeErrorDiagnostic("gltf.load_failed",
        "glTF load failed without diagnostics", input.source_id_prefix, ""));
    }
    output.success = false;
    return output;
  }

  output = BuildWorkItemsFromData(*data, input);
  return output;
}

auto GltfGeometryAdapter::BuildWorkItems(
  const std::span<const std::byte> source_bytes,
  const GeometryAdapterInput& input) const -> GeometryAdapterOutput
{
  GeometryAdapterOutput output;
  auto data = LoadDataFromMemory(source_bytes, input, output.diagnostics);
  if (data == nullptr) {
    DLOG_F(ERROR, "glTF load failed (memory): diagnostics={}",
      output.diagnostics.size());
    if (output.diagnostics.empty()) {
      output.diagnostics.push_back(MakeErrorDiagnostic("gltf.load_failed",
        "glTF load failed without diagnostics", input.source_id_prefix, ""));
    }
    output.success = false;
    return output;
  }

  output = BuildWorkItemsFromData(*data, input);
  return output;
}

} // namespace oxygen::content::import::adapters
