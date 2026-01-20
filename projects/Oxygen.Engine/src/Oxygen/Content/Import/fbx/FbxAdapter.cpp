//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <memory>
#include <numbers>
#include <numeric>
#include <optional>
#include <span>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include <Oxygen/Base/Logging.h>
#include <Oxygen/Content/Import/TextureImportPresets.h>
#include <Oxygen/Content/Import/fbx/FbxAdapter.h>
#include <Oxygen/Content/Import/fbx/UfbxUtils.h>
#include <Oxygen/Content/Import/fbx/ufbx.h>
#include <Oxygen/Content/Import/util/CoordTransform.h>
#include <Oxygen/Content/Import/util/ImportNaming.h>
#include <Oxygen/Content/Import/util/StringUtils.h>
#include <Oxygen/Core/Transforms/Decompose.h>
#include <Oxygen/Data/PakFormat.h>

namespace oxygen::content::import::adapters {

struct FbxAdapter::Impl final {
  std::shared_ptr<const ufbx_scene> scene_owner;
};

FbxAdapter::FbxAdapter()
  : impl_(std::make_unique<Impl>())
{
}

FbxAdapter::~FbxAdapter() = default;

namespace {

  [[nodiscard]] auto ToStringView(const ufbx_string& s) -> std::string_view
  {
    return std::string_view(s.data, s.length);
  }

  [[nodiscard]] auto ResolveFileTexture(const ufbx_texture* texture)
    -> const ufbx_texture*
  {
    if (texture == nullptr) {
      return nullptr;
    }

    if (texture->file_textures.count > 0) {
      return texture->file_textures.data[0];
    }

    return texture;
  }

  [[nodiscard]] auto TextureIdString(const ufbx_texture& texture)
    -> std::string_view
  {
    if (texture.relative_filename.length > 0) {
      return ToStringView(texture.relative_filename);
    }
    if (texture.filename.length > 0) {
      return ToStringView(texture.filename);
    }
    if (texture.name.length > 0) {
      return ToStringView(texture.name);
    }
    return {};
  }

  [[nodiscard]] auto NormalizeTexturePathId(std::filesystem::path p)
    -> std::string
  {
    if (p.empty()) {
      return {};
    }

    p = p.lexically_normal();
    auto out = p.generic_string();

#if defined(_WIN32)
    std::transform(out.begin(), out.end(), out.begin(), [](const char c) {
      return static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    });
#endif

    return out;
  }

  [[nodiscard]] auto SelectBaseColorTexture(const ufbx_material& material)
    -> const ufbx_texture*
  {
    const auto& pbr = material.pbr.base_color;
    if (!pbr.feature_disabled && pbr.texture != nullptr) {
      return pbr.texture;
    }
    const auto& fbx = material.fbx.diffuse_color;
    if (!fbx.feature_disabled && fbx.texture != nullptr) {
      return fbx.texture;
    }
    return nullptr;
  }

  [[nodiscard]] auto SelectNormalTexture(const ufbx_material& material)
    -> const ufbx_texture*
  {
    const auto& pbr = material.pbr.normal_map;
    if (!pbr.feature_disabled && pbr.texture != nullptr) {
      return pbr.texture;
    }
    const auto& fbx = material.fbx.normal_map;
    if (!fbx.feature_disabled && fbx.texture != nullptr) {
      return fbx.texture;
    }
    return nullptr;
  }

  [[nodiscard]] auto SelectMetallicTexture(const ufbx_material& material)
    -> const ufbx_texture*
  {
    const auto& pbr = material.pbr.metalness;
    if (!pbr.feature_disabled && pbr.texture != nullptr) {
      return pbr.texture;
    }
    return nullptr;
  }

  [[nodiscard]] auto SelectRoughnessTexture(const ufbx_material& material)
    -> const ufbx_texture*
  {
    const auto& pbr = material.pbr.roughness;
    if (!pbr.feature_disabled && pbr.texture != nullptr) {
      return pbr.texture;
    }
    return nullptr;
  }

  [[nodiscard]] auto SelectAmbientOcclusionTexture(
    const ufbx_material& material) -> const ufbx_texture*
  {
    const auto& pbr = material.pbr.ambient_occlusion;
    if (!pbr.feature_disabled && pbr.texture != nullptr) {
      return pbr.texture;
    }
    return nullptr;
  }

  [[nodiscard]] auto SelectEmissiveTexture(const ufbx_material& material)
    -> const ufbx_texture*
  {
    const auto& pbr = material.pbr.emission_color;
    if (!pbr.feature_disabled && pbr.texture != nullptr) {
      return pbr.texture;
    }
    const auto& fbx = material.fbx.emission_color;
    if (!fbx.feature_disabled && fbx.texture != nullptr) {
      return fbx.texture;
    }
    return nullptr;
  }

  [[nodiscard]] auto TryReadWholeFileBytes(const std::filesystem::path& path)
    -> std::optional<std::vector<std::byte>>
  {
    std::ifstream file(path, std::ios::binary | std::ios::ate);
    if (!file) {
      return std::nullopt;
    }

    const auto size = file.tellg();
    if (size <= 0) {
      return std::nullopt;
    }

    auto bytes = std::vector<std::byte>(static_cast<size_t>(size));
    file.seekg(0, std::ios::beg);
    if (!file.read(reinterpret_cast<char*>(bytes.data()), size)) {
      return std::nullopt;
    }

    return bytes;
  }

  using oxygen::data::pak::DirectionalLightRecord;
  using oxygen::data::pak::NodeRecord;
  using oxygen::data::pak::OrthographicCameraRecord;
  using oxygen::data::pak::PerspectiveCameraRecord;
  using oxygen::data::pak::PointLightRecord;
  using oxygen::data::pak::RenderableRecord;
  using oxygen::data::pak::SpotLightRecord;

  struct UfbxCancelContext final {
    std::stop_token stop_token;
  };

  auto UfbxProgressCallback(void* user, const ufbx_progress*)
    -> ufbx_progress_result
  {
    const auto* ctx = static_cast<const UfbxCancelContext*>(user);
    if (ctx != nullptr && ctx->stop_token.stop_requested()) {
      return UFBX_PROGRESS_CANCEL;
    }
    return UFBX_PROGRESS_CONTINUE;
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

  [[nodiscard]] auto ToVec3(const ufbx_vec3 v) -> glm::vec3
  {
    return glm::vec3 {
      static_cast<float>(v.x),
      static_cast<float>(v.y),
      static_cast<float>(v.z),
    };
  }

  [[nodiscard]] auto FindNodesForMesh(const ufbx_scene& scene,
    const ufbx_mesh* target_mesh) -> std::vector<const ufbx_node*>
  {
    std::vector<const ufbx_node*> nodes;
    for (size_t ni = 0; ni < scene.nodes.count; ++ni) {
      const auto* node = scene.nodes.data[ni];
      if (node != nullptr && node->mesh == target_mesh) {
        nodes.push_back(node);
      }
    }
    return nodes;
  }

  [[nodiscard]] auto DisambiguateMeshName(const ufbx_scene& scene,
    const ImportRequest& request, const ufbx_mesh& mesh, uint32_t ordinal,
    std::unordered_map<std::string, uint32_t>& name_usage) -> std::string
  {
    const auto authored_name = fbx::ToStringView(mesh.name);
    auto mesh_name = util::BuildMeshName(authored_name, request, ordinal);
    const auto original_mesh_name = mesh_name;

    if (const auto it = name_usage.find(mesh_name); it != name_usage.end()) {
      const auto collision_ordinal = it->second;
      std::string new_name;

      const auto nodes = FindNodesForMesh(scene, &mesh);
      if (!nodes.empty()) {
        const auto* node = nodes.front();
        const auto node_name = fbx::ToStringView(node->name);
        if (!node_name.empty()) {
          const std::string prefix = mesh_name.starts_with("G_") ? "" : "G_";
          new_name = prefix + std::string(node_name) + "_"
            + std::string(authored_name.empty()
                ? ("Mesh_" + std::to_string(ordinal))
                : std::string(authored_name));
        }
      }

      if (new_name.empty()) {
        new_name = mesh_name + "_" + std::to_string(collision_ordinal);
      }

      LOG_F(INFO, "Geometry name collision detected for '{}', renamed to '{}'",
        original_mesh_name.c_str(), new_name.c_str());
      mesh_name = std::move(new_name);
    }

    name_usage[original_mesh_name]++;
    return mesh_name;
  }

  [[nodiscard]] auto ToVec2(const ufbx_vec2 v) -> glm::vec2
  {
    return glm::vec2 {
      static_cast<float>(v.x),
      static_cast<float>(v.y),
    };
  }

  [[nodiscard]] auto ToVec4(const ufbx_vec4 v) -> glm::vec4
  {
    return glm::vec4 {
      static_cast<float>(v.x),
      static_cast<float>(v.y),
      static_cast<float>(v.z),
      static_cast<float>(v.w),
    };
  }

  [[nodiscard]] auto ToMat4(const ufbx_matrix matrix) -> glm::mat4
  {
    const auto c0 = matrix.cols[0];
    const auto c1 = matrix.cols[1];
    const auto c2 = matrix.cols[2];
    const auto c3 = matrix.cols[3];

    return glm::mat4 {
      glm::vec4 { static_cast<float>(c0.x), static_cast<float>(c0.y),
        static_cast<float>(c0.z), 0.0F },
      glm::vec4 { static_cast<float>(c1.x), static_cast<float>(c1.y),
        static_cast<float>(c1.z), 0.0F },
      glm::vec4 { static_cast<float>(c2.x), static_cast<float>(c2.y),
        static_cast<float>(c2.z), 0.0F },
      glm::vec4 { static_cast<float>(c3.x), static_cast<float>(c3.y),
        static_cast<float>(c3.z), 1.0F },
    };
  }

  struct AxisVec final {
    int x = 0;
    int y = 0;
    int z = 0;
  };

  [[nodiscard]] auto AxisToVec(const ufbx_coordinate_axis axis) -> AxisVec
  {
    switch (axis) {
    case UFBX_COORDINATE_AXIS_POSITIVE_X:
      return AxisVec { .x = 1, .y = 0, .z = 0 };
    case UFBX_COORDINATE_AXIS_NEGATIVE_X:
      return AxisVec { .x = -1, .y = 0, .z = 0 };
    case UFBX_COORDINATE_AXIS_POSITIVE_Y:
      return AxisVec { .x = 0, .y = 1, .z = 0 };
    case UFBX_COORDINATE_AXIS_NEGATIVE_Y:
      return AxisVec { .x = 0, .y = -1, .z = 0 };
    case UFBX_COORDINATE_AXIS_POSITIVE_Z:
      return AxisVec { .x = 0, .y = 0, .z = 1 };
    case UFBX_COORDINATE_AXIS_NEGATIVE_Z:
      return AxisVec { .x = 0, .y = 0, .z = -1 };
    case UFBX_COORDINATE_AXIS_UNKNOWN:
      break;
    }

    return AxisVec {};
  }

  [[nodiscard]] auto IsLeftHandedAxes(const ufbx_coordinate_axes& axes)
    -> std::optional<bool>
  {
    if (axes.right == UFBX_COORDINATE_AXIS_UNKNOWN
      || axes.up == UFBX_COORDINATE_AXIS_UNKNOWN
      || axes.front == UFBX_COORDINATE_AXIS_UNKNOWN) {
      return std::nullopt;
    }

    const auto right = AxisToVec(axes.right);
    const auto up = AxisToVec(axes.up);
    const auto forward = AxisToVec(axes.front);

    const AxisVec cross_ru {
      .x = right.y * up.z - right.z * up.y,
      .y = right.z * up.x - right.x * up.z,
      .z = right.x * up.y - right.y * up.x,
    };

    const int det = cross_ru.x * forward.x + cross_ru.y * forward.y
      + cross_ru.z * forward.z;
    return det < 0;
  }

  template <typename T> [[nodiscard]] auto HasAttribute(const T& stream) -> bool
  {
    return stream.exists && stream.values.data != nullptr
      && stream.indices.data != nullptr;
  }

  struct TriangleMeshBuffers {
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

  [[nodiscard]] auto FindSkinDeformer(const ufbx_mesh& mesh)
    -> const ufbx_skin_deformer*
  {
    auto find_in_connections
      = [](const ufbx_connection_list& list) -> const ufbx_skin_deformer* {
      for (size_t i = 0; i < list.count; ++i) {
        const auto& conn = list.data[i];
        if (conn.src != nullptr
          && conn.src->type == UFBX_ELEMENT_SKIN_DEFORMER) {
          return reinterpret_cast<const ufbx_skin_deformer*>(conn.src);
        }
        if (conn.dst != nullptr
          && conn.dst->type == UFBX_ELEMENT_SKIN_DEFORMER) {
          return reinterpret_cast<const ufbx_skin_deformer*>(conn.dst);
        }
      }
      return nullptr;
    };

    auto is_connected_to = [](const ufbx_connection_list& list,
                             const ufbx_element* element) -> bool {
      if (element == nullptr) {
        return false;
      }
      for (size_t i = 0; i < list.count; ++i) {
        const auto& conn = list.data[i];
        if (conn.src == element || conn.dst == element) {
          return true;
        }
      }
      return false;
    };

    for (size_t i = 0; i < mesh.skin_deformers.count; ++i) {
      const auto* deformer = mesh.skin_deformers.data[i];
      if (deformer != nullptr) {
        return deformer;
      }
    }

    for (size_t i = 0; i < mesh.all_deformers.count; ++i) {
      const auto* element = mesh.all_deformers.data[i];
      if (element != nullptr && element->type == UFBX_ELEMENT_SKIN_DEFORMER) {
        return reinterpret_cast<const ufbx_skin_deformer*>(element);
      }
    }

    for (size_t i = 0; i < mesh.element.connections_dst.count; ++i) {
      const auto& conn = mesh.element.connections_dst.data[i];
      if (conn.src != nullptr && conn.src->type == UFBX_ELEMENT_SKIN_DEFORMER) {
        return reinterpret_cast<const ufbx_skin_deformer*>(conn.src);
      }
    }

    for (size_t i = 0; i < mesh.element.connections_src.count; ++i) {
      const auto& conn = mesh.element.connections_src.data[i];
      if (conn.dst != nullptr && conn.dst->type == UFBX_ELEMENT_SKIN_DEFORMER) {
        return reinterpret_cast<const ufbx_skin_deformer*>(conn.dst);
      }
    }

    for (size_t i = 0; i < mesh.instances.count; ++i) {
      const auto* node = mesh.instances.data[i];
      if (node == nullptr) {
        continue;
      }

      if (const auto* deformer
        = find_in_connections(node->element.connections_dst);
        deformer != nullptr) {
        return deformer;
      }
      if (const auto* deformer
        = find_in_connections(node->element.connections_src);
        deformer != nullptr) {
        return deformer;
      }
    }

    if (mesh.element.scene != nullptr) {
      const auto& scene = *mesh.element.scene;
      for (size_t i = 0; i < scene.skin_deformers.count; ++i) {
        const auto* deformer = scene.skin_deformers.data[i];
        if (deformer != nullptr) {
          if (is_connected_to(deformer->element.connections_dst, &mesh.element)
            || is_connected_to(
              deformer->element.connections_src, &mesh.element)) {
            return deformer;
          }
        }
      }
    }

    return nullptr;
  }

  [[nodiscard]] auto NormalizeWeights(const glm::vec4 weights) -> glm::vec4
  {
    const auto sum = weights.x + weights.y + weights.z + weights.w;
    if (sum <= 0.0F) {
      return glm::vec4(0.0F);
    }
    return weights / sum;
  }

  auto CleanSkinWeights(std::vector<glm::vec4>& weights,
    std::vector<glm::uvec4>& joints, std::vector<ImportDiagnostic>& diagnostics,
    std::string_view source_id, std::string_view object_path) -> void
  {
    constexpr float kMinWeight = 1.0e-4F;

    if (weights.size() != joints.size()) {
      diagnostics.push_back(MakeErrorDiagnostic("mesh.skinning_buffers",
        "Skinning buffers must have matching sizes", source_id, object_path));
      return;
    }

    for (size_t i = 0; i < weights.size(); ++i) {
      auto w = weights[i];
      auto j = joints[i];

      std::array<std::pair<float, uint32_t>, 4> influences = {
        std::make_pair(w.x, j.x),
        std::make_pair(w.y, j.y),
        std::make_pair(w.z, j.z),
        std::make_pair(w.w, j.w),
      };

      std::sort(influences.begin(), influences.end(),
        [](const auto& a, const auto& b) { return a.first > b.first; });

      size_t kept = 0;
      for (size_t k = 0; k < influences.size(); ++k) {
        if (influences[k].first < kMinWeight) {
          influences[k].first = 0.0F;
        }
        if (influences[k].first > 0.0F) {
          ++kept;
        }
      }

      if (kept == 0) {
        w = glm::vec4(0.0F);
        j = glm::uvec4(0u);
      } else {
        w = NormalizeWeights(glm::vec4 { influences[0].first,
          influences[1].first, influences[2].first, influences[3].first });
        j = glm::uvec4 { influences[0].second, influences[1].second,
          influences[2].second, influences[3].second };
      }

      weights[i] = w;
      joints[i] = j;
    }
  }

  [[nodiscard]] auto BuildTriangleBuffers(const ufbx_mesh& mesh,
    const ufbx_node* material_node,
    const std::unordered_map<const ufbx_material*, uint32_t>&
      scene_material_index_by_ptr,
    const uint32_t material_key_count,
    std::vector<ImportDiagnostic>& diagnostics, std::string_view source_id,
    std::string_view object_path) -> std::optional<TriangleMeshBuffers>
  {
    if (mesh.num_indices == 0 || mesh.num_faces == 0) {
      diagnostics.push_back(MakeErrorDiagnostic(
        "mesh.no_faces", "FBX mesh contains no faces", source_id, object_path));
      return std::nullopt;
    }

    if (!HasAttribute(mesh.vertex_position)) {
      diagnostics.push_back(MakeErrorDiagnostic("mesh.missing_positions",
        "FBX mesh missing vertex positions", source_id, object_path));
      return std::nullopt;
    }

    TriangleMeshBuffers out;
    out.positions.reserve(mesh.num_indices);
    out.normals.reserve(mesh.num_indices);
    out.texcoords.reserve(mesh.num_indices);
    out.tangents.reserve(mesh.num_indices);
    out.bitangents.reserve(mesh.num_indices);
    out.colors.reserve(mesh.num_indices);
    const auto estimated_tris
      = mesh.num_triangles > 0 ? mesh.num_triangles : mesh.num_indices;
    out.indices.reserve(estimated_tris * 3u);

    struct MaterialRange {
      TriangleRange range {};
      std::vector<uint32_t> indices;
    };

    std::unordered_map<uint32_t, MaterialRange> range_map;
    std::vector<uint32_t> tri_indices;
    tri_indices.resize(static_cast<size_t>(mesh.max_face_triangles) * 3u);
    size_t triangulated_faces = 0;

    for (size_t idx = 0; idx < mesh.num_indices; ++idx) {
      out.positions.push_back(
        ToVec3(ufbx_get_vertex_vec3(&mesh.vertex_position, idx)));

      if (HasAttribute(mesh.vertex_normal)) {
        out.normals.push_back(
          ToVec3(ufbx_get_vertex_vec3(&mesh.vertex_normal, idx)));
      }
      if (HasAttribute(mesh.vertex_uv)) {
        out.texcoords.push_back(
          ToVec2(ufbx_get_vertex_vec2(&mesh.vertex_uv, idx)));
      }
      if (HasAttribute(mesh.vertex_tangent)) {
        out.tangents.push_back(
          ToVec3(ufbx_get_vertex_vec3(&mesh.vertex_tangent, idx)));
      }
      if (HasAttribute(mesh.vertex_bitangent)) {
        out.bitangents.push_back(
          ToVec3(ufbx_get_vertex_vec3(&mesh.vertex_bitangent, idx)));
      }
      if (HasAttribute(mesh.vertex_color)) {
        out.colors.push_back(
          ToVec4(ufbx_get_vertex_vec4(&mesh.vertex_color, idx)));
      }
    }

    const ufbx_material_list* material_list = &mesh.materials;
    if (material_node != nullptr && material_node->materials.count > 0) {
      material_list = &material_node->materials;
    }

    std::vector<uint32_t> face_material_slots(
      mesh.num_faces, material_key_count);
    if (mesh.material_parts.data != nullptr && mesh.material_parts.count > 0) {
      for (size_t part_i = 0; part_i < mesh.material_parts.count; ++part_i) {
        const auto& part = mesh.material_parts.data[part_i];
        const auto slot = part.index;
        if (part.face_indices.data == nullptr) {
          continue;
        }
        const auto face_count = part.face_indices.count;
        for (size_t fi = 0; fi < face_count; ++fi) {
          const auto face_index = part.face_indices.data[fi];
          if (face_index < mesh.num_faces) {
            face_material_slots[face_index] = slot;
          }
        }
      }
    } else if (mesh.face_material.data != nullptr
      && mesh.face_material.count >= mesh.num_faces) {
      for (size_t face_i = 0; face_i < mesh.num_faces; ++face_i) {
        face_material_slots[face_i] = mesh.face_material.data[face_i];
      }
    } else {
      diagnostics.push_back(MakeWarningDiagnostic("mesh.face_material_missing",
        "FBX face material list missing; defaulting to single material",
        source_id, object_path));
    }

    for (size_t face_i = 0; face_i < mesh.num_faces; ++face_i) {
      const auto face = mesh.faces[face_i];
      if (face.num_indices < 3) {
        diagnostics.push_back(MakeWarningDiagnostic("mesh.invalid_face",
          "FBX mesh contains face with fewer than 3 indices; skipping",
          source_id, object_path));
        continue;
      }

      uint32_t material_slot = material_key_count;
      if (!face_material_slots.empty() && face_i < face_material_slots.size()
        && material_list != nullptr && material_list->data != nullptr
        && material_list->count > 0) {
        const uint32_t slot = face_material_slots[face_i];
        if (slot != UFBX_NO_INDEX && slot < material_list->count) {
          const auto* material = material_list->data[slot];
          if (material != nullptr) {
            if (const auto it = scene_material_index_by_ptr.find(material);
              it != scene_material_index_by_ptr.end()) {
              material_slot = it->second;
            }
          }
        }
      }

      auto it = range_map.find(material_slot);
      if (it == range_map.end()) {
        it = range_map
               .emplace(material_slot,
                 MaterialRange {
                   .range = TriangleRange {
                     .material_slot = material_slot,
                     .first_index = 0,
                     .index_count = 0,
                   },
                   .indices = {},
                 })
               .first;
      }

      const auto tri_count = ufbx_triangulate_face(
        tri_indices.data(), tri_indices.size(), &mesh, face);
      if (tri_count == 0) {
        diagnostics.push_back(MakeWarningDiagnostic("mesh.triangulate_failed",
          "FBX face triangulation produced no triangles; skipping face",
          source_id, object_path));
        continue;
      }

      const auto tri_index_count = static_cast<size_t>(tri_count) * 3u;
      for (size_t i = 0; i < tri_index_count; ++i) {
        const auto idx = tri_indices[i];
        if (idx >= mesh.num_indices) {
          diagnostics.push_back(MakeErrorDiagnostic("mesh.index_oob",
            "FBX mesh contains out-of-range indices", source_id, object_path));
          return std::nullopt;
        }
      }

      it->second.indices.insert(it->second.indices.end(), tri_indices.begin(),
        tri_indices.begin() + tri_index_count);
      it->second.range.index_count += static_cast<uint32_t>(tri_index_count);
      if (face.num_indices != 3) {
        ++triangulated_faces;
      }
    }

    if (triangulated_faces > 0) {
      LOG_F(INFO, "FBX mesh '{}' triangulated {} faces", object_path,
        triangulated_faces);
    }

    if (const auto* skin_deformer = FindSkinDeformer(mesh);
      skin_deformer != nullptr) {
      out.joint_indices.reserve(mesh.num_vertices);
      out.joint_weights.reserve(mesh.num_vertices);

      for (size_t i = 0; i < mesh.num_vertices; ++i) {
        if (i >= skin_deformer->vertices.count) {
          out.joint_indices.push_back(glm::uvec4(0u));
          out.joint_weights.push_back(glm::vec4(0.0F));
          continue;
        }

        const auto vertex = skin_deformer->vertices.data[i];
        glm::uvec4 joints(0u);
        glm::vec4 weights(0.0F);

        const size_t count
          = (std::min)(static_cast<size_t>(vertex.num_weights), size_t { 4 });
        for (size_t w = 0; w < count; ++w) {
          const auto weight_index = vertex.weight_begin + w;
          if (weight_index >= skin_deformer->weights.count) {
            continue;
          }
          const auto weight = skin_deformer->weights.data[weight_index];
          const auto slot = static_cast<glm::uvec4::length_type>(w);
          joints[slot] = static_cast<uint32_t>(weight.cluster_index);
          weights[slot] = static_cast<float>(weight.weight);
        }

        out.joint_indices.push_back(joints);
        out.joint_weights.push_back(weights);
      }

      CleanSkinWeights(out.joint_weights, out.joint_indices, diagnostics,
        source_id, object_path);
    }

    std::vector<std::pair<uint32_t, MaterialRange>> sorted_ranges;
    sorted_ranges.reserve(range_map.size());
    for (auto& [slot, range] : range_map) {
      sorted_ranges.push_back({ slot, std::move(range) });
    }

    std::sort(sorted_ranges.begin(), sorted_ranges.end(),
      [](const auto& a, const auto& b) { return a.first < b.first; });

    out.ranges.reserve(sorted_ranges.size());
    for (auto& entry : sorted_ranges) {
      auto& range = entry.second;
      range.range.first_index = static_cast<uint32_t>(out.indices.size());
      out.indices.insert(
        out.indices.end(), range.indices.begin(), range.indices.end());
      out.ranges.push_back(range.range);
    }

    if (out.ranges.empty()) {
      diagnostics.push_back(MakeErrorDiagnostic("mesh.no_ranges",
        "FBX mesh emitted no triangle ranges", source_id, object_path));
      return std::nullopt;
    }

    if (!out.normals.empty() && out.normals.size() != out.positions.size()) {
      out.normals.clear();
      diagnostics.push_back(MakeWarningDiagnostic("mesh.normals.mismatch",
        "FBX normals count does not match positions", source_id, object_path));
    }
    if (!out.texcoords.empty()
      && out.texcoords.size() != out.positions.size()) {
      out.texcoords.clear();
      diagnostics.push_back(MakeWarningDiagnostic("mesh.texcoords.mismatch",
        "FBX texcoords count does not match positions", source_id,
        object_path));
    }
    if (!out.tangents.empty() && out.tangents.size() != out.positions.size()) {
      out.tangents.clear();
      out.bitangents.clear();
      diagnostics.push_back(MakeWarningDiagnostic("mesh.tangents.mismatch",
        "FBX tangents count does not match positions", source_id, object_path));
    }
    if (!out.colors.empty() && out.colors.size() != out.positions.size()) {
      out.colors.clear();
      diagnostics.push_back(MakeWarningDiagnostic("mesh.colors.mismatch",
        "FBX colors count does not match positions", source_id, object_path));
    }

    if (!out.joint_indices.empty()
      && out.joint_indices.size() != out.positions.size()) {
      out.joint_indices.clear();
      out.joint_weights.clear();
      diagnostics.push_back(MakeWarningDiagnostic("mesh.skinning.mismatch",
        "FBX skinning buffers count does not match positions", source_id,
        object_path));
    }

    if (!out.joint_indices.empty()) {
      const auto* skin_deformer = FindSkinDeformer(mesh);
      if (skin_deformer != nullptr) {
        const size_t cluster_count = skin_deformer->clusters.count;
        out.inverse_bind_matrices.reserve(cluster_count);
        out.joint_remap.reserve(cluster_count);
        for (size_t i = 0; i < cluster_count; ++i) {
          const auto* cluster = skin_deformer->clusters.data[i];
          if (cluster == nullptr) {
            continue;
          }
          out.inverse_bind_matrices.push_back(
            ToMat4(cluster->geometry_to_bone));
          out.joint_remap.push_back(static_cast<uint32_t>(i));
        }
      }
    }

    return out;
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
    id.append("::");
    id.append(std::to_string(ordinal));
    return id;
  }

  enum class TextureUsage : uint8_t {
    kBaseColor,
    kNormal,
    kMetallic,
    kRoughness,
    kMetallicRoughness,
    kOcclusion,
    kEmissive,
  };

  [[nodiscard]] auto UsageLabel(const TextureUsage usage) -> std::string_view
  {
    switch (usage) {
    case TextureUsage::kBaseColor:
      return "base_color";
    case TextureUsage::kNormal:
      return "normal";
    case TextureUsage::kMetallic:
      return "metallic";
    case TextureUsage::kRoughness:
      return "roughness";
    case TextureUsage::kMetallicRoughness:
      return "metallic_roughness";
    case TextureUsage::kOcclusion:
      return "occlusion";
    case TextureUsage::kEmissive:
      return "emissive";
    }
    return "texture";
  }

  [[nodiscard]] auto PresetForUsage(const TextureUsage usage) -> TexturePreset
  {
    switch (usage) {
    case TextureUsage::kBaseColor:
      return TexturePreset::kAlbedo;
    case TextureUsage::kNormal:
      return TexturePreset::kNormal;
    case TextureUsage::kMetallic:
      return TexturePreset::kMetallic;
    case TextureUsage::kRoughness:
      return TexturePreset::kRoughness;
    case TextureUsage::kMetallicRoughness:
      return TexturePreset::kORMPacked;
    case TextureUsage::kOcclusion:
      return TexturePreset::kAO;
    case TextureUsage::kEmissive:
      return TexturePreset::kEmissive;
    }
    return TexturePreset::kData;
  }

  [[nodiscard]] auto BuildTextureSourceId(std::string_view prefix,
    std::string_view texture_id, const TextureUsage usage) -> std::string
  {
    std::string id;
    if (!prefix.empty()) {
      id = std::string(prefix);
      id.append("::");
    }
    id.append("tex::");
    if (!texture_id.empty()) {
      id.append(texture_id.begin(), texture_id.end());
    } else {
      id.append("texture");
    }
    id.append("::");
    id.append(UsageLabel(usage));
    return id;
  }

  struct TextureIdentity final {
    std::string texture_id;
    const ufbx_texture* file_texture = nullptr;
    std::filesystem::path resolved_path;
    bool embedded = false;
  };

  [[nodiscard]] auto ResolveTextureIdentity(const ufbx_texture* texture,
    const ImportRequest& request, std::string_view source_id,
    std::vector<ImportDiagnostic>& diagnostics)
    -> std::optional<TextureIdentity>
  {
    const auto* file_tex = ResolveFileTexture(texture);
    if (file_tex == nullptr) {
      return std::nullopt;
    }

    TextureIdentity identity {};
    identity.file_texture = file_tex;
    identity.embedded
      = (file_tex->content.data != nullptr && file_tex->content.size > 0);

    if (identity.embedded) {
      const auto id = TextureIdString(*file_tex);
      if (!id.empty()) {
        identity.texture_id = "embedded:" + std::string(id);
      } else {
        identity.texture_id
          = "embedded:fbx_texture_" + std::to_string(file_tex->element_id);
      }
      return identity;
    }

    std::string_view rel = fbx::ToStringView(file_tex->relative_filename);
    std::string_view abs = fbx::ToStringView(file_tex->filename);

    if (rel.empty() && abs.empty()) {
      const auto rel_prop
        = ufbx_find_string(&file_tex->props, "RelativeFilename", {});
      const auto abs_prop = ufbx_find_string(&file_tex->props, "FileName", {});
      if (rel_prop.length > 0) {
        rel = fbx::ToStringView(rel_prop);
      }
      if (abs_prop.length > 0) {
        abs = fbx::ToStringView(abs_prop);
      }
    }

    if (!rel.empty()) {
      identity.resolved_path
        = request.source_path.parent_path() / std::filesystem::path(rel);
    } else if (!abs.empty()) {
      const auto abs_path = std::filesystem::path(abs);
      identity.resolved_path = abs_path.is_absolute()
        ? abs_path
        : (request.source_path.parent_path() / abs_path);
    }

    if (!identity.resolved_path.empty()) {
      identity.texture_id = NormalizeTexturePathId(identity.resolved_path);
    }

    if (identity.texture_id.empty()) {
      const auto id = TextureIdString(*file_tex);
      if (!id.empty()) {
        identity.texture_id = std::string(id);
      } else {
        identity.texture_id
          = "fbx_texture_" + std::to_string(file_tex->element_id);
      }
    }

    if (identity.texture_id.empty()) {
      diagnostics.push_back(MakeWarningDiagnostic("fbx.texture.id_missing",
        "FBX texture could not be assigned a stable id", source_id, ""));
      return std::nullopt;
    }

    return identity;
  }

  [[nodiscard]] auto ResolveTextureSourceBytes(const TextureIdentity& identity,
    std::string_view source_id, const std::shared_ptr<const ufbx_scene>& owner,
    std::vector<ImportDiagnostic>& diagnostics)
    -> std::optional<TexturePipeline::SourceBytes>
  {
    if (identity.file_texture == nullptr) {
      return std::nullopt;
    }

    const auto make_placeholder = []() -> TexturePipeline::SourceBytes {
      auto bytes = std::make_shared<std::vector<std::byte>>();
      return TexturePipeline::SourceBytes {
        .bytes = std::span<const std::byte>(bytes->data(), bytes->size()),
        .owner = std::static_pointer_cast<const void>(bytes),
      };
    };

    if (identity.embedded) {
      const auto bytes = std::span<const std::byte>(
        reinterpret_cast<const std::byte*>(identity.file_texture->content.data),
        identity.file_texture->content.size);
      if (bytes.empty()) {
        diagnostics.push_back(MakeWarningDiagnostic("fbx.texture.empty",
          "FBX embedded texture payload is empty", source_id, ""));
        return make_placeholder();
      }
      return TexturePipeline::SourceBytes {
        .bytes = bytes,
        .owner = std::static_pointer_cast<const void>(owner),
      };
    }

    if (identity.resolved_path.empty()) {
      diagnostics.push_back(MakeWarningDiagnostic("fbx.texture.path_missing",
        "FBX texture has no resolved file path", source_id, ""));
      return make_placeholder();
    }

    auto bytes = TryReadWholeFileBytes(identity.resolved_path);
    if (!bytes.has_value() || bytes->empty()) {
      diagnostics.push_back(MakeWarningDiagnostic("fbx.texture.load_failed",
        "Failed to read FBX texture file", source_id,
        identity.resolved_path.string()));
      return make_placeholder();
    }

    auto shared_bytes
      = std::make_shared<std::vector<std::byte>>(std::move(*bytes));

    return TexturePipeline::SourceBytes {
      .bytes
      = std::span<const std::byte>(shared_bytes->data(), shared_bytes->size()),
      .owner = std::static_pointer_cast<const void>(shared_bytes),
    };
  }

  [[nodiscard]] auto IsLambertMaterial(const ufbx_material& material) -> bool
  {
    if (material.shader_type == UFBX_SHADER_FBX_LAMBERT) {
      return true;
    }

    const auto shading_model = fbx::ToStringView(material.shading_model_name);
    if (shading_model == "Lambert" || shading_model == "lambert") {
      return true;
    }

    const auto name = fbx::ToStringView(material.name);
    if (util::StartsWithIgnoreCase(name, "lambert")) {
      return true;
    }

    return false;
  }

  [[nodiscard]] auto HasMaterialTextures(const ufbx_material* material) -> bool
  {
    if (material == nullptr) {
      return false;
    }

    return SelectBaseColorTexture(*material) != nullptr
      || SelectNormalTexture(*material) != nullptr
      || SelectMetallicTexture(*material) != nullptr
      || SelectRoughnessTexture(*material) != nullptr
      || SelectAmbientOcclusionTexture(*material) != nullptr
      || SelectEmissiveTexture(*material) != nullptr;
  }

  [[nodiscard]] auto BuildSceneSourceId(
    std::string_view prefix, const ImportRequest& request) -> std::string
  {
    if (!prefix.empty()) {
      return std::string(prefix);
    }
    return util::BuildSceneName(request);
  }

  struct NodeInput final {
    std::string authored_name;
    std::string base_name;
    uint32_t parent_index = 0;
    glm::mat4 local_matrix { 1.0F };
    glm::mat4 world_matrix { 1.0F };
    bool has_renderable = false;
    bool has_camera = false;
    bool has_light = false;
    bool visible = true;
    const void* source_node = nullptr;
  };

  [[nodiscard]] auto AppendString(std::vector<std::byte>& strings,
    const std::string_view value) -> oxygen::data::pak::StringTableOffsetT
  {
    const auto offset
      = static_cast<oxygen::data::pak::StringTableOffsetT>(strings.size());
    const auto bytes
      = std::as_bytes(std::span<const char>(value.data(), value.size()));
    strings.insert(strings.end(), bytes.begin(), bytes.end());
    strings.push_back(std::byte { 0 });
    return offset;
  }

  [[nodiscard]] auto MakeNodeKey(const std::string_view node_virtual_path)
    -> data::AssetKey
  {
    return util::MakeDeterministicAssetKey(node_virtual_path);
  }

  [[nodiscard]] auto MakeLocalTransformMatrix(const ufbx_transform& transform)
    -> glm::mat4
  {
    const auto matrix = ufbx_transform_to_matrix(&transform);
    return coord::ToGlmMat4(matrix);
  }

  [[nodiscard]] auto MakeSceneLoadError(std::string_view source_id,
    std::string_view error_message) -> ImportDiagnostic
  {
    return ImportDiagnostic {
      .severity = ImportSeverity::kError,
      .code = "fbx.parse_failed",
      .message = std::string(error_message),
      .source_path = std::string(source_id),
      .object_path = {},
    };
  }

  [[nodiscard]] auto LoadSceneFromFile(const std::filesystem::path& path,
    const AdapterInput& input, std::vector<ImportDiagnostic>& diagnostics)
    -> std::shared_ptr<const ufbx_scene>
  {
    if (input.stop_token.stop_requested()) {
      DLOG_F(
        WARNING, "FBX load cancelled: source_id='{}'", input.source_id_prefix);
      diagnostics.push_back(MakeCancelDiagnostic(input.source_id_prefix));
      return {};
    }

    const auto& coordinate_policy = input.request.options.coordinate;
    if (coordinate_policy.unit_normalization
        == UnitNormalizationPolicy::kApplyCustomFactor
      && !(coordinate_policy.custom_unit_scale > 0.0F)) {
      DLOG_F(ERROR, "FBX invalid custom unit scale: source_id='{}' scale={} ",
        input.source_id_prefix, coordinate_policy.custom_unit_scale);
      diagnostics.push_back(MakeErrorDiagnostic("fbx.invalid_custom_unit_scale",
        "custom_unit_scale must be > 0 when using custom unit normalization",
        input.source_id_prefix, input.object_path_prefix));
      return {};
    }

    ufbx_load_opts opts {};
    ufbx_error error {};

    UfbxCancelContext cancel_ctx { .stop_token = input.stop_token };
    opts.progress_cb.fn = &UfbxProgressCallback;
    opts.progress_cb.user = &cancel_ctx;

    opts.target_axes = coord::EngineWorldTargetAxes();
    opts.target_camera_axes = coord::EngineCameraTargetAxes();
    opts.geometry_transform_handling
      = UFBX_GEOMETRY_TRANSFORM_HANDLING_HELPER_NODES;
    opts.space_conversion = UFBX_SPACE_CONVERSION_MODIFY_GEOMETRY;
    // Default to mirroring along engine forward/back, then refine using
    // FBX axis metadata if available.
    opts.handedness_conversion_axis = UFBX_MIRROR_AXIS_Y;
    if (const auto target_unit_meters
      = coord::ComputeTargetUnitMeters(coordinate_policy);
      target_unit_meters.has_value()) {
      opts.target_unit_meters = *target_unit_meters;
    }
    opts.generate_missing_normals = true;
    opts.skip_skin_vertices = false;
    opts.clean_skin_weights = true;

    {
      ufbx_load_opts probe_opts = opts;
      probe_opts.target_axes = ufbx_coordinate_axes {
        .right = UFBX_COORDINATE_AXIS_UNKNOWN,
        .up = UFBX_COORDINATE_AXIS_UNKNOWN,
        .front = UFBX_COORDINATE_AXIS_UNKNOWN,
      };
      probe_opts.target_camera_axes = probe_opts.target_axes;
      probe_opts.handedness_conversion_axis = UFBX_MIRROR_AXIS_NONE;
      probe_opts.handedness_conversion_retain_winding = false;
      probe_opts.reverse_winding = false;

      ufbx_error probe_error {};
      ufbx_scene* probe_scene
        = ufbx_load_file(path.string().c_str(), &probe_opts, &probe_error);
      if (probe_scene != nullptr) {
        const auto handedness = IsLeftHandedAxes(probe_scene->settings.axes);
        ufbx_free_scene(probe_scene);

        if (!handedness.has_value()) {
          diagnostics.push_back(MakeWarningDiagnostic("fbx.axis_unknown",
            "FBX axis metadata is incomplete; using default handedness "
            "conversion",
            input.source_id_prefix, input.object_path_prefix));
        } else if (*handedness) {
          opts.handedness_conversion_axis = UFBX_MIRROR_AXIS_Y;
        } else {
          opts.handedness_conversion_axis = UFBX_MIRROR_AXIS_NONE;
        }
      }
    }

    ufbx_scene* scene = ufbx_load_file(path.string().c_str(), &opts, &error);
    if (scene == nullptr) {
      if (error.type == UFBX_ERROR_CANCELLED
        || input.stop_token.stop_requested()) {
        DLOG_F(WARNING, "FBX load cancelled: path='{}'", path.string());
        diagnostics.push_back(MakeCancelDiagnostic(input.source_id_prefix));
        return {};
      }
      const auto desc = fbx::ToStringView(error.description);
      DLOG_F(
        ERROR, "FBX load failed: path='{}' error='{}'", path.string(), desc);
      diagnostics.push_back(MakeSceneLoadError(input.source_id_prefix, desc));
      return {};
    }

    return std::shared_ptr<const ufbx_scene>(
      scene, [](const ufbx_scene* value) {
        ufbx_free_scene(const_cast<ufbx_scene*>(value));
      });
  }

  [[nodiscard]] auto LoadSceneFromMemory(const std::span<const std::byte> bytes,
    const AdapterInput& input, std::vector<ImportDiagnostic>& diagnostics)
    -> std::shared_ptr<const ufbx_scene>
  {
    if (input.stop_token.stop_requested()) {
      DLOG_F(WARNING, "FBX load cancelled (memory): source_id='{}'",
        input.source_id_prefix);
      diagnostics.push_back(MakeCancelDiagnostic(input.source_id_prefix));
      return {};
    }

    const auto& coordinate_policy = input.request.options.coordinate;
    if (coordinate_policy.unit_normalization
        == UnitNormalizationPolicy::kApplyCustomFactor
      && !(coordinate_policy.custom_unit_scale > 0.0F)) {
      DLOG_F(ERROR, "FBX invalid custom unit scale: source_id='{}' scale={} ",
        input.source_id_prefix, coordinate_policy.custom_unit_scale);
      diagnostics.push_back(MakeErrorDiagnostic("fbx.invalid_custom_unit_scale",
        "custom_unit_scale must be > 0 when using custom unit normalization",
        input.source_id_prefix, input.object_path_prefix));
      return {};
    }

    ufbx_load_opts opts {};
    ufbx_error error {};

    UfbxCancelContext cancel_ctx { .stop_token = input.stop_token };
    opts.progress_cb.fn = &UfbxProgressCallback;
    opts.progress_cb.user = &cancel_ctx;

    opts.target_axes = coord::EngineWorldTargetAxes();
    opts.target_camera_axes = coord::EngineCameraTargetAxes();
    opts.geometry_transform_handling
      = UFBX_GEOMETRY_TRANSFORM_HANDLING_HELPER_NODES;
    opts.space_conversion = UFBX_SPACE_CONVERSION_MODIFY_GEOMETRY;
    // Default to mirroring along engine forward/back, then refine using
    // FBX axis metadata if available.
    opts.handedness_conversion_axis = UFBX_MIRROR_AXIS_Y;
    if (const auto target_unit_meters
      = coord::ComputeTargetUnitMeters(coordinate_policy);
      target_unit_meters.has_value()) {
      opts.target_unit_meters = *target_unit_meters;
    }
    opts.generate_missing_normals = true;
    opts.skip_skin_vertices = false;
    opts.clean_skin_weights = true;

    {
      ufbx_load_opts probe_opts = opts;
      probe_opts.target_axes = ufbx_coordinate_axes {
        .right = UFBX_COORDINATE_AXIS_UNKNOWN,
        .up = UFBX_COORDINATE_AXIS_UNKNOWN,
        .front = UFBX_COORDINATE_AXIS_UNKNOWN,
      };
      probe_opts.target_camera_axes = probe_opts.target_axes;
      probe_opts.handedness_conversion_axis = UFBX_MIRROR_AXIS_NONE;
      probe_opts.handedness_conversion_retain_winding = false;
      probe_opts.reverse_winding = false;

      ufbx_error probe_error {};
      ufbx_scene* probe_scene = ufbx_load_memory(
        bytes.data(), bytes.size(), &probe_opts, &probe_error);
      if (probe_scene != nullptr) {
        const auto handedness = IsLeftHandedAxes(probe_scene->settings.axes);
        ufbx_free_scene(probe_scene);

        if (!handedness.has_value()) {
          diagnostics.push_back(MakeWarningDiagnostic("fbx.axis_unknown",
            "FBX axis metadata is incomplete; using default handedness "
            "conversion",
            input.source_id_prefix, input.object_path_prefix));
        } else if (*handedness) {
          opts.handedness_conversion_axis = UFBX_MIRROR_AXIS_Y;
        } else {
          opts.handedness_conversion_axis = UFBX_MIRROR_AXIS_NONE;
        }
      }
    }

    const void* data = bytes.data();
    const auto size = static_cast<size_t>(bytes.size());
    ufbx_scene* scene = ufbx_load_memory(data, size, &opts, &error);
    if (scene == nullptr) {
      if (error.type == UFBX_ERROR_CANCELLED
        || input.stop_token.stop_requested()) {
        DLOG_F(WARNING, "FBX load cancelled (memory): source_id='{}'",
          input.source_id_prefix);
        diagnostics.push_back(MakeCancelDiagnostic(input.source_id_prefix));
        return {};
      }
      const auto desc = fbx::ToStringView(error.description);
      DLOG_F(ERROR, "FBX load failed (memory): error='{}'", desc);
      diagnostics.push_back(MakeSceneLoadError(input.source_id_prefix, desc));
      return {};
    }

    return std::shared_ptr<const ufbx_scene>(
      scene, [](const ufbx_scene* value) {
        ufbx_free_scene(const_cast<ufbx_scene*>(value));
      });
  }

  [[nodiscard]] auto StreamWorkItemsFromScene(const ufbx_scene& scene,
    const AdapterInput& input, GeometryWorkItemSink& sink)
    -> WorkItemStreamResult
  {
    WorkItemStreamResult result;
    if (input.stop_token.stop_requested()) {
      result.success = false;
      result.diagnostics.push_back(
        MakeCancelDiagnostic(input.source_id_prefix));
      return result;
    }

    std::unordered_map<std::string, uint32_t> name_usage;
    std::unordered_map<const ufbx_material*, uint32_t>
      scene_material_index_by_ptr;
    scene_material_index_by_ptr.reserve(scene.materials.count);
    for (uint32_t mat_i = 0; mat_i < scene.materials.count; ++mat_i) {
      const auto* mat = scene.materials.data[mat_i];
      if (mat == nullptr) {
        continue;
      }
      scene_material_index_by_ptr.emplace(mat, mat_i);
    }
    const auto mesh_count = static_cast<uint32_t>(scene.meshes.count);
    DLOG_F(2, "FBX scene meshes={} skin_deformers={}", mesh_count,
      scene.skin_deformers.count);

    for (uint32_t mesh_i = 0; mesh_i < mesh_count; ++mesh_i) {
      if (input.stop_token.stop_requested()) {
        result.success = false;
        result.diagnostics.push_back(
          MakeCancelDiagnostic(input.source_id_prefix));
        return result;
      }

      const auto* mesh = scene.meshes.data[mesh_i];
      if (mesh == nullptr) {
        continue;
      }

      const auto authored_name = fbx::ToStringView(mesh->name);
      DLOG_F(2,
        "FBX mesh[{}] name='{}' indices={} faces={} skin_deformers={} "
        "all_deformers={} instances={} conn_src={} conn_dst={}",
        mesh_i, authored_name, mesh->num_indices, mesh->num_faces,
        mesh->skin_deformers.count, mesh->all_deformers.count,
        mesh->instances.count, mesh->element.connections_src.count,
        mesh->element.connections_dst.count);
      auto mesh_name
        = DisambiguateMeshName(scene, input.request, *mesh, mesh_i, name_usage);

      const auto storage_mesh_name
        = util::NamespaceImportedAssetName(input.request, mesh_name);

      GeometryPipeline::WorkItem item;
      item.source_id = BuildSourceId(input.source_id_prefix, mesh_name, mesh_i);
      item.mesh_name = mesh_name;
      item.storage_mesh_name = storage_mesh_name;
      item.source_key = mesh;
      item.material_keys.assign(
        input.material_keys.begin(), input.material_keys.end());
      item.default_material_key = input.default_material_key;
      item.want_textures = true;
      bool has_material_textures = false;
      const ufbx_node* material_node
        = (mesh->instances.count > 0) ? mesh->instances.data[0] : nullptr;
      const ufbx_material_list* material_list = &mesh->materials;
      if (material_node != nullptr && material_node->materials.count > 0) {
        material_list = &material_node->materials;
      }
      for (size_t mat_i = 0; mat_i < material_list->count; ++mat_i) {
        const auto* material = material_list->data[mat_i];
        if (HasMaterialTextures(material)) {
          has_material_textures = true;
          break;
        }
      }
      item.has_material_textures = has_material_textures;
      item.request = input.request;
      item.stop_token = input.stop_token;

      std::vector<ImportDiagnostic> diagnostics;
      auto buffers = BuildTriangleBuffers(*mesh, material_node,
        scene_material_index_by_ptr,
        static_cast<uint32_t>(input.material_keys.size()), diagnostics,
        item.source_id, item.mesh_name);
      if (!buffers.has_value()) {
        result.diagnostics.insert(
          result.diagnostics.end(), diagnostics.begin(), diagnostics.end());
        result.success = false;
        continue;
      }

      const auto* skin_deformer = FindSkinDeformer(*mesh);
      DLOG_F(2, "FBX mesh[{}] skin_deformer_found={} joints={} weights={}",
        mesh_i, skin_deformer != nullptr, buffers->joint_indices.size(),
        buffers->joint_weights.size());

      const bool is_skinned = !buffers->joint_indices.empty()
        && buffers->joint_weights.size() == buffers->joint_indices.size();

      auto owner = std::make_shared<TriangleMeshBuffers>(std::move(*buffers));
      TriangleMesh triangle_mesh {
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
        .bounds = std::nullopt,
      };

      item.lods = {
        MeshLod {
          .lod_name = "LOD0",
          .source = std::move(triangle_mesh),
          .source_owner = std::move(owner),
        },
      };

      if (!sink.Consume(std::move(item))) {
        return result;
      }
      ++result.emitted;
    }

    return result;
  }

} // namespace

auto FbxAdapter::Parse(const std::filesystem::path& source_path,
  const AdapterInput& input) -> ParseResult
{
  ParseResult result;
  auto scene = LoadSceneFromFile(source_path, input, result.diagnostics);
  if (!scene) {
    DLOG_F(ERROR, "FBX parse failed: path='{}' diagnostics={}",
      source_path.string(), result.diagnostics.size());
    if (result.diagnostics.empty()) {
      result.diagnostics.push_back(MakeErrorDiagnostic("fbx.parse_failed",
        "FBX parse failed without diagnostics", input.source_id_prefix, ""));
    }
    impl_->scene_owner.reset();
    result.success = false;
    return result;
  }

  impl_->scene_owner = std::move(scene);
  return result;
}

auto FbxAdapter::Parse(const std::span<const std::byte> source_bytes,
  const AdapterInput& input) -> ParseResult
{
  ParseResult result;
  auto scene = LoadSceneFromMemory(source_bytes, input, result.diagnostics);
  if (!scene) {
    DLOG_F(ERROR, "FBX parse failed (memory): diagnostics={}",
      result.diagnostics.size());
    if (result.diagnostics.empty()) {
      result.diagnostics.push_back(MakeErrorDiagnostic("fbx.parse_failed",
        "FBX parse failed without diagnostics", input.source_id_prefix, ""));
    }
    impl_->scene_owner.reset();
    result.success = false;
    return result;
  }

  impl_->scene_owner = std::move(scene);
  return result;
}

auto FbxAdapter::BuildWorkItems(GeometryWorkTag, GeometryWorkItemSink& sink,
  const AdapterInput& input) -> WorkItemStreamResult
{
  if (!impl_->scene_owner) {
    WorkItemStreamResult result;
    result.success = false;
    result.diagnostics.push_back(MakeErrorDiagnostic("fbx.scene.not_parsed",
      "FBX adapter has no parsed scene", input.source_id_prefix,
      input.object_path_prefix));
    return result;
  }

  return StreamWorkItemsFromScene(*impl_->scene_owner, input, sink);
}

auto FbxAdapter::BuildWorkItems(MaterialWorkTag, MaterialWorkItemSink& sink,
  const AdapterInput& input) -> WorkItemStreamResult
{
  if (!impl_->scene_owner) {
    WorkItemStreamResult result;
    result.success = false;
    result.diagnostics.push_back(MakeErrorDiagnostic("fbx.scene.not_parsed",
      "FBX adapter has no parsed scene", input.source_id_prefix,
      input.object_path_prefix));
    return result;
  }

  WorkItemStreamResult result;
  if (input.stop_token.stop_requested()) {
    result.success = false;
    result.diagnostics.push_back(MakeCancelDiagnostic(input.source_id_prefix));
    return result;
  }

  const auto& scene = *impl_->scene_owner;

  std::unordered_map<const ufbx_texture*, std::string> texture_ids;

  auto resolve_texture_id
    = [&](const ufbx_texture* texture,
        std::string_view material_source_id) -> std::optional<std::string> {
    const auto* file_tex = ResolveFileTexture(texture);
    if (file_tex == nullptr) {
      return std::nullopt;
    }

    if (const auto it = texture_ids.find(file_tex); it != texture_ids.end()) {
      return it->second;
    }

    auto identity = ResolveTextureIdentity(
      texture, input.request, material_source_id, result.diagnostics);
    if (!identity.has_value()) {
      return std::nullopt;
    }

    texture_ids.emplace(file_tex, identity->texture_id);
    return identity->texture_id;
  };

  auto apply_binding
    = [&](MaterialTextureBinding& binding,
        std::optional<std::string> texture_id, const TextureUsage usage) {
        if (!texture_id.has_value()) {
          return;
        }

        binding.assigned = true;
        binding.source_id
          = BuildTextureSourceId(input.source_id_prefix, *texture_id, usage);
        binding.index = 0;
        binding.uv_set = 0;
      };

  const auto material_count = static_cast<uint32_t>(scene.materials.count);
  if (material_count == 0) {
    MaterialPipeline::WorkItem item;
    item.material_name = util::BuildMaterialName("M_Default", input.request, 0);
    item.source_id
      = BuildSourceId(input.source_id_prefix, item.material_name, 0);
    item.storage_material_name
      = util::NamespaceImportedAssetName(input.request, item.material_name);
    item.material_domain = data::MaterialDomain::kOpaque;
    item.alpha_mode = MaterialAlphaMode::kOpaque;
    item.request = input.request;
    item.stop_token = input.stop_token;

    if (!sink.Consume(std::move(item))) {
      return result;
    }
    ++result.emitted;
    return result;
  }

  for (uint32_t i = 0; i < material_count; ++i) {
    if (input.stop_token.stop_requested()) {
      result.success = false;
      result.diagnostics.push_back(
        MakeCancelDiagnostic(input.source_id_prefix));
      return result;
    }

    const auto* material = scene.materials.data[i];
    const auto authored_name = material != nullptr
      ? fbx::ToStringView(material->name)
      : std::string_view {};
    const auto material_name
      = util::BuildMaterialName(authored_name, input.request, i);

    MaterialPipeline::WorkItem item;
    item.source_id = BuildSourceId(input.source_id_prefix, material_name, i);
    item.material_name = material_name;
    item.storage_material_name
      = util::NamespaceImportedAssetName(input.request, material_name);
    item.source_key = material;
    item.material_domain = data::MaterialDomain::kOpaque;
    item.alpha_mode = MaterialAlphaMode::kOpaque;

    if (material != nullptr) {
      ufbx_vec4 base = { 1.0, 1.0, 1.0, 1.0 };
      if (material->pbr.base_color.has_value
        && material->pbr.base_color.value_components >= 3) {
        base = material->pbr.base_color.value_vec4;
      } else if (material->fbx.diffuse_color.has_value
        && material->fbx.diffuse_color.value_components >= 3) {
        const auto dc = material->fbx.diffuse_color.value_vec3;
        base = { dc.x, dc.y, dc.z, 1.0 };
      }

      float base_factor = 1.0F;
      if (material->pbr.base_factor.has_value) {
        base_factor
          = util::Clamp01(util::ToFloat(material->pbr.base_factor.value_real));
      } else if (material->fbx.diffuse_factor.has_value) {
        base_factor = util::Clamp01(
          util::ToFloat(material->fbx.diffuse_factor.value_real));
      }

      item.inputs.base_color[0]
        = util::Clamp01(util::ToFloat(base.x) * base_factor);
      item.inputs.base_color[1]
        = util::Clamp01(util::ToFloat(base.y) * base_factor);
      item.inputs.base_color[2]
        = util::Clamp01(util::ToFloat(base.z) * base_factor);
      item.inputs.base_color[3]
        = util::Clamp01(util::ToFloat(base.w) * base_factor);

      if (material->pbr.metalness.has_value) {
        item.inputs.metalness
          = util::Clamp01(util::ToFloat(material->pbr.metalness.value_real));
      }

      if (material->pbr.roughness.has_value) {
        item.inputs.roughness
          = util::Clamp01(util::ToFloat(material->pbr.roughness.value_real));
      }
      item.inputs.roughness_as_glossiness
        = material->features.roughness_as_glossiness.enabled;

      if (material->pbr.ambient_occlusion.has_value) {
        item.inputs.ambient_occlusion = util::Clamp01(
          util::ToFloat(material->pbr.ambient_occlusion.value_real));
      }

      {
        ufbx_vec4 emission = { 0.0, 0.0, 0.0, 0.0 };
        if (material->pbr.emission_color.has_value
          && material->pbr.emission_color.value_components >= 3) {
          emission = material->pbr.emission_color.value_vec4;
        } else if (material->fbx.emission_color.has_value
          && material->fbx.emission_color.value_components >= 3) {
          const auto ec = material->fbx.emission_color.value_vec3;
          emission = { ec.x, ec.y, ec.z, 0.0 };
        }

        float emission_factor = 1.0F;
        if (material->pbr.emission_factor.has_value) {
          emission_factor
            = util::ToFloat(material->pbr.emission_factor.value_real);
        } else if (material->fbx.emission_factor.has_value) {
          emission_factor
            = util::ToFloat(material->fbx.emission_factor.value_real);
        }

        item.inputs.emissive_factor[0]
          = util::ToFloat(emission.x) * emission_factor;
        item.inputs.emissive_factor[1]
          = util::ToFloat(emission.y) * emission_factor;
        item.inputs.emissive_factor[2]
          = util::ToFloat(emission.z) * emission_factor;
      }

      if (material->pbr.normal_map.has_value) {
        item.inputs.normal_scale = (std::max)(0.0F,
          util::ToFloat(material->pbr.normal_map.value_real));
      } else if (material->fbx.bump_factor.has_value) {
        item.inputs.normal_scale = (std::max)(0.0F,
          util::ToFloat(material->fbx.bump_factor.value_real));
      }

      float specular_factor = 1.0F;
      if (IsLambertMaterial(*material)) {
        specular_factor = 0.5F;
      } else if (material->pbr.specular_factor.has_value) {
        specular_factor = util::Clamp01(
          util::ToFloat(material->pbr.specular_factor.value_real));
      } else if (material->fbx.specular_factor.has_value) {
        specular_factor = util::Clamp01(
          util::ToFloat(material->fbx.specular_factor.value_real));
      }

      if (material->pbr.specular_color.has_value) {
        const auto& c = material->pbr.specular_color.value_vec4;
        const float intensity = (std::max)({ util::ToFloat(c.x),
          util::ToFloat(c.y), util::ToFloat(c.z) });
        specular_factor *= intensity;
      } else if (material->fbx.specular_color.has_value) {
        const auto& c = material->fbx.specular_color.value_vec4;
        const float intensity = (std::max)({ util::ToFloat(c.x),
          util::ToFloat(c.y), util::ToFloat(c.z) });
        specular_factor *= intensity;
      }

      item.inputs.specular_factor = util::Clamp01(specular_factor);

      item.inputs.double_sided = material->features.double_sided.enabled;
      item.inputs.unlit = material->features.unlit.enabled;

      const auto* base_color_tex = SelectBaseColorTexture(*material);
      const auto* normal_tex = SelectNormalTexture(*material);
      const auto* metallic_tex = SelectMetallicTexture(*material);
      const auto* roughness_tex = SelectRoughnessTexture(*material);
      const auto* ao_tex = SelectAmbientOcclusionTexture(*material);
      const auto* emissive_tex = SelectEmissiveTexture(*material);

      const auto* metallic_file = ResolveFileTexture(metallic_tex);
      const auto* roughness_file = ResolveFileTexture(roughness_tex);
      const bool orm_packed
        = metallic_file != nullptr && metallic_file == roughness_file;

      if (orm_packed) {
        const auto texture_id
          = resolve_texture_id(metallic_tex, item.source_id);
        if (texture_id.has_value()) {
          const auto source_id = BuildTextureSourceId(input.source_id_prefix,
            *texture_id, TextureUsage::kMetallicRoughness);
          item.textures.metallic.assigned = true;
          item.textures.metallic.source_id = source_id;
          item.textures.metallic.index = 0;
          item.textures.metallic.uv_set = 0;
          item.textures.roughness.assigned = true;
          item.textures.roughness.source_id = source_id;
          item.textures.roughness.index = 0;
          item.textures.roughness.uv_set = 0;

          const auto* ao_file = ResolveFileTexture(ao_tex);
          if (ao_file != nullptr && ao_file == metallic_file) {
            item.textures.ambient_occlusion.assigned = true;
            item.textures.ambient_occlusion.source_id = source_id;
            item.textures.ambient_occlusion.index = 0;
            item.textures.ambient_occlusion.uv_set = 0;
          }
        }
      }

      if (!orm_packed) {
        apply_binding(item.textures.metallic,
          resolve_texture_id(metallic_tex, item.source_id),
          TextureUsage::kMetallic);
        apply_binding(item.textures.roughness,
          resolve_texture_id(roughness_tex, item.source_id),
          TextureUsage::kRoughness);
      }

      apply_binding(item.textures.base_color,
        resolve_texture_id(base_color_tex, item.source_id),
        TextureUsage::kBaseColor);
      apply_binding(item.textures.normal,
        resolve_texture_id(normal_tex, item.source_id), TextureUsage::kNormal);
      if (!orm_packed || item.textures.ambient_occlusion.source_id.empty()) {
        apply_binding(item.textures.ambient_occlusion,
          resolve_texture_id(ao_tex, item.source_id), TextureUsage::kOcclusion);
      }
      apply_binding(item.textures.emissive,
        resolve_texture_id(emissive_tex, item.source_id),
        TextureUsage::kEmissive);
    }

    item.request = input.request;
    item.stop_token = input.stop_token;

    if (!sink.Consume(std::move(item))) {
      return result;
    }
    ++result.emitted;
  }

  return result;
}

auto FbxAdapter::BuildWorkItems(TextureWorkTag, TextureWorkItemSink& sink,
  const AdapterInput& input) -> WorkItemStreamResult
{
  if (!impl_->scene_owner) {
    WorkItemStreamResult result;
    result.success = false;
    result.diagnostics.push_back(MakeErrorDiagnostic("fbx.scene.not_parsed",
      "FBX adapter has no parsed scene", input.source_id_prefix,
      input.object_path_prefix));
    return result;
  }

  WorkItemStreamResult result;
  if (input.stop_token.stop_requested()) {
    result.success = false;
    result.diagnostics.push_back(MakeCancelDiagnostic(input.source_id_prefix));
    return result;
  }

  const auto& scene = *impl_->scene_owner;
  std::unordered_map<std::string, TexturePipeline::WorkItem> work_items;
  std::unordered_map<const ufbx_texture*, TextureIdentity> identities;

  auto get_identity
    = [&](const ufbx_texture* texture,
        std::string_view source_id) -> std::optional<TextureIdentity> {
    const auto* file_tex = ResolveFileTexture(texture);
    if (file_tex == nullptr) {
      return std::nullopt;
    }

    if (const auto it = identities.find(file_tex); it != identities.end()) {
      return it->second;
    }

    auto identity = ResolveTextureIdentity(
      texture, input.request, source_id, result.diagnostics);
    if (!identity.has_value()) {
      return std::nullopt;
    }

    identities.emplace(file_tex, *identity);
    return identity;
  };

  auto register_texture
    = [&](const ufbx_texture* texture, const TextureUsage usage,
        std::string_view source_id) {
        if (texture == nullptr) {
          return;
        }

        auto identity = get_identity(texture, source_id);
        if (!identity.has_value()) {
          return;
        }

        const auto tex_source_id = BuildTextureSourceId(
          input.source_id_prefix, identity->texture_id, usage);
        if (work_items.find(tex_source_id) != work_items.end()) {
          return;
        }

        auto bytes = ResolveTextureSourceBytes(
          *identity, tex_source_id, impl_->scene_owner, result.diagnostics);
        if (!bytes.has_value()) {
          return;
        }

        auto desc = MakeDescFromPreset(PresetForUsage(usage));
        desc.source_id = tex_source_id;
        desc.stop_token = input.stop_token;

        TexturePipeline::WorkItem item {};
        item.source_id = tex_source_id;
        item.texture_id = tex_source_id;
        item.source_key = identity->file_texture;
        item.desc = std::move(desc);
        item.packing_policy_id = "d3d12";
        item.output_format_is_override = false;
        item.failure_policy
          = input.request.options.texture_tuning.placeholder_on_failure
          ? TexturePipeline::FailurePolicy::kPlaceholder
          : TexturePipeline::FailurePolicy::kStrict;
        item.source = *bytes;
        item.stop_token = input.stop_token;

        work_items.emplace(tex_source_id, std::move(item));
      };

  const auto material_count = static_cast<uint32_t>(scene.materials.count);
  for (uint32_t i = 0; i < material_count; ++i) {
    if (input.stop_token.stop_requested()) {
      result.success = false;
      result.diagnostics.push_back(
        MakeCancelDiagnostic(input.source_id_prefix));
      return result;
    }

    const auto* material = scene.materials.data[i];
    if (material == nullptr) {
      continue;
    }

    const auto material_name = util::BuildMaterialName(
      fbx::ToStringView(material->name), input.request, i);
    const auto material_source_id
      = BuildSourceId(input.source_id_prefix, material_name, i);

    const auto* base_color_tex = SelectBaseColorTexture(*material);
    const auto* normal_tex = SelectNormalTexture(*material);
    const auto* metallic_tex = SelectMetallicTexture(*material);
    const auto* roughness_tex = SelectRoughnessTexture(*material);
    const auto* ao_tex = SelectAmbientOcclusionTexture(*material);
    const auto* emissive_tex = SelectEmissiveTexture(*material);

    const auto* metallic_file = ResolveFileTexture(metallic_tex);
    const auto* roughness_file = ResolveFileTexture(roughness_tex);
    const bool orm_packed
      = metallic_file != nullptr && metallic_file == roughness_file;

    register_texture(
      base_color_tex, TextureUsage::kBaseColor, material_source_id);
    register_texture(normal_tex, TextureUsage::kNormal, material_source_id);
    register_texture(emissive_tex, TextureUsage::kEmissive, material_source_id);

    if (orm_packed) {
      register_texture(
        metallic_tex, TextureUsage::kMetallicRoughness, material_source_id);
      const auto* ao_file = ResolveFileTexture(ao_tex);
      if (ao_file == nullptr || ao_file != metallic_file) {
        register_texture(ao_tex, TextureUsage::kOcclusion, material_source_id);
      }
    } else {
      register_texture(
        metallic_tex, TextureUsage::kMetallic, material_source_id);
      register_texture(
        roughness_tex, TextureUsage::kRoughness, material_source_id);
      register_texture(ao_tex, TextureUsage::kOcclusion, material_source_id);
    }
  }

  for (auto& [_, item] : work_items) {
    if (!sink.Consume(std::move(item))) {
      return result;
    }
    ++result.emitted;
  }

  return result;
}

auto FbxAdapter::BuildSceneStage(const SceneStageInput& input,
  std::vector<ImportDiagnostic>& diagnostics) const -> SceneStageResult
{
  SceneStageResult result;
  if (input.stop_token.stop_requested()) {
    diagnostics.push_back(MakeCancelDiagnostic(input.source_id));
    return result;
  }

  if (!impl_->scene_owner) {
    diagnostics.push_back(MakeErrorDiagnostic("fbx.scene.not_parsed",
      "FBX adapter has no parsed scene", input.source_id, {}));
    return result;
  }

  if (input.request == nullptr) {
    diagnostics.push_back(MakeErrorDiagnostic("scene.request_missing",
      "Scene stage input is missing request data", input.source_id, {}));
    return result;
  }

  const auto& scene = *impl_->scene_owner;
  const auto& request = *input.request;

  std::unordered_map<const ufbx_mesh*, data::AssetKey> mesh_keys;
  mesh_keys.reserve(scene.meshes.count);

  if (!input.geometry_keys.empty()
    && input.geometry_keys.size() < scene.meshes.count) {
    diagnostics.push_back(MakeErrorDiagnostic("scene.geometry_key_missing",
      "Geometry key count does not match mesh count", input.source_id, {}));
  }

  for (size_t i = 0; i < scene.meshes.count; ++i) {
    const auto* mesh = scene.meshes.data[i];
    if (mesh == nullptr) {
      continue;
    }

    if (i < input.geometry_keys.size()) {
      mesh_keys.emplace(mesh, input.geometry_keys[i]);
    }
  }

  std::vector<NodeInput> nodes;
  nodes.reserve(scene.nodes.count > 0 ? scene.nodes.count : 1u);

  auto traverse = [&](auto&& self, const ufbx_node* node, uint32_t parent,
                    std::string_view parent_name, uint32_t& ordinal,
                    const glm::mat4& parent_world) -> void {
    if (node == nullptr || input.stop_token.stop_requested()) {
      return;
    }

    const auto authored = fbx::ToStringView(node->name);
    const auto base_name
      = util::BuildSceneNodeName(authored, request, ordinal, parent_name);

    const auto local_matrix = MakeLocalTransformMatrix(node->local_transform);
    const auto world_matrix = parent_world * local_matrix;

    NodeInput node_input;
    node_input.authored_name = std::string(authored);
    node_input.base_name = base_name;
    node_input.parent_index = parent;
    node_input.local_matrix = local_matrix;
    node_input.world_matrix = world_matrix;
    node_input.visible = node->visible;
    node_input.has_camera = node->camera != nullptr;
    node_input.has_light = node->light != nullptr;
    node_input.has_renderable = false;
    node_input.source_node = node;

    if (node->mesh != nullptr) {
      const auto it = mesh_keys.find(node->mesh);
      if (it != mesh_keys.end()) {
        node_input.has_renderable = true;
      }
    }

    const auto index = static_cast<uint32_t>(nodes.size());
    if (index == 0) {
      node_input.parent_index = 0;
    }

    nodes.push_back(std::move(node_input));
    const auto current_name = nodes.back().base_name;

    ++ordinal;

    for (size_t i = 0; i < node->children.count; ++i) {
      self(self, node->children.data[i], index, current_name, ordinal,
        world_matrix);
    }
  };

  uint32_t ordinal = 0;
  if (scene.root_node != nullptr) {
    traverse(traverse, scene.root_node, 0, {}, ordinal, glm::mat4 { 1.0F });
  }

  if (nodes.empty()) {
    NodeInput root;
    root.authored_name = "root";
    root.base_name = "root";
    root.parent_index = 0;
    root.local_matrix = glm::mat4 { 1.0F };
    root.world_matrix = glm::mat4 { 1.0F };
    root.visible = true;
    nodes.push_back(std::move(root));
  }

  std::vector<uint32_t> kept_indices;
  kept_indices.reserve(nodes.size());

  if (request.options.node_pruning == NodePruningPolicy::kDropEmptyNodes) {
    for (uint32_t i = 0; i < nodes.size(); ++i) {
      const auto& node = nodes[i];
      if (node.has_renderable || node.has_camera || node.has_light) {
        kept_indices.push_back(i);
      }
    }
  } else {
    for (uint32_t i = 0; i < nodes.size(); ++i) {
      kept_indices.push_back(i);
    }
  }

  if (kept_indices.empty()) {
    NodeInput root;
    root.authored_name = "root";
    root.base_name = "root";
    root.parent_index = 0;
    root.local_matrix = glm::mat4 { 1.0F };
    root.world_matrix = glm::mat4 { 1.0F };
    root.visible = true;
    nodes.clear();
    nodes.push_back(std::move(root));
    kept_indices.push_back(0);
  }

  std::vector<int32_t> old_to_new(nodes.size(), -1);
  for (uint32_t new_index = 0; new_index < kept_indices.size(); ++new_index) {
    old_to_new[kept_indices[new_index]] = static_cast<int32_t>(new_index);
  }

  std::vector<NodeInput> pruned_nodes;
  pruned_nodes.reserve(kept_indices.size());

  for (uint32_t new_index = 0; new_index < kept_indices.size(); ++new_index) {
    const auto old_index = kept_indices[new_index];
    auto node = nodes[old_index];

    uint32_t parent = node.parent_index;
    while (parent < nodes.size() && old_to_new[parent] < 0) {
      const auto next_parent = nodes[parent].parent_index;
      if (next_parent == parent) {
        break;
      }
      parent = next_parent;
    }

    uint32_t new_parent_index = new_index;
    if (parent < nodes.size() && old_to_new[parent] >= 0) {
      new_parent_index = static_cast<uint32_t>(old_to_new[parent]);
    }

    if (new_parent_index != new_index) {
      const auto parent_old_index = kept_indices[new_parent_index];
      const auto& parent_world = nodes[parent_old_index].world_matrix;

      glm::vec3 parent_translation {};
      glm::vec3 parent_scale { 1.0F, 1.0F, 1.0F };
      glm::quat parent_rotation {};
      const bool parent_decomposed = transforms::TryDecomposeTransform(
        parent_world, parent_translation, parent_rotation, parent_scale);
      const bool can_reparent = parent_decomposed
        && transforms::IsUniformScale(parent_scale)
        && transforms::IsIdentityRotation(parent_rotation);

      if (!can_reparent) {
        diagnostics.push_back(
          MakeWarningDiagnostic("scene.pruning.reparent_skipped",
            "Skipped reparenting due to non-uniform or rotated parent; "
            "preserving world transform",
            input.source_id, node.base_name));
        new_parent_index = new_index;
        node.local_matrix = node.world_matrix;
      } else {
        const auto det = glm::determinant(parent_world);
        if (std::abs(det) > 1e-6F) {
          node.local_matrix = glm::inverse(parent_world) * node.world_matrix;
        } else {
          diagnostics.push_back(MakeErrorDiagnostic("scene.pruning.singular",
            "Node pruning failed due to singular parent transform",
            input.source_id, node.base_name));
        }
      }
    }

    node.parent_index = new_parent_index;
    pruned_nodes.push_back(std::move(node));
  }

  SceneBuild build;
  build.nodes.reserve(pruned_nodes.size());
  build.strings.push_back(std::byte { 0 });

  std::unordered_map<std::string, uint32_t> name_usage;
  name_usage.reserve(pruned_nodes.size());

  const auto scene_name = util::BuildSceneName(request);
  const auto virtual_path
    = request.loose_cooked_layout.SceneVirtualPath(scene_name);

  for (uint32_t i = 0; i < pruned_nodes.size(); ++i) {
    auto& node = pruned_nodes[i];
    auto name = node.base_name;
    auto& count = name_usage[name];
    if (count > 0) {
      const auto suffix = "_" + std::to_string(count);
      name += suffix;
      diagnostics.push_back(MakeWarningDiagnostic("scene.node_name_renamed",
        "Duplicate node name renamed with suffix", input.source_id,
        node.base_name));
    }
    ++count;

    glm::vec3 translation {};
    glm::vec3 scale { 1.0F, 1.0F, 1.0F };
    glm::quat rotation {};
    const bool used_fallback = transforms::DecomposeTransformOrFallback(
      node.local_matrix, translation, rotation, scale);
    if (used_fallback) {
      diagnostics.push_back(MakeWarningDiagnostic("scene.transform_sanitized",
        "Node '" + name
          + "' transform sanitized: non-finite values reset to identity TRS; "
            "invalid rotation set to identity.",
        input.source_id, name));
    }

    NodeRecord rec {};
    rec.node_id = MakeNodeKey(std::string(virtual_path) + "/" + name);
    rec.scene_name_offset = AppendString(build.strings, name);
    rec.parent_index = node.parent_index;
    rec.node_flags = node.visible ? data::pak::kSceneNodeFlag_Visible : 0;
    rec.translation[0] = translation.x;
    rec.translation[1] = translation.y;
    rec.translation[2] = translation.z;
    rec.rotation[0] = rotation.x;
    rec.rotation[1] = rotation.y;
    rec.rotation[2] = rotation.z;
    rec.rotation[3] = rotation.w;
    rec.scale[0] = scale.x;
    rec.scale[1] = scale.y;
    rec.scale[2] = scale.z;
    build.nodes.push_back(rec);

    const auto* ufbx_node = static_cast<const ::ufbx_node*>(node.source_node);
    if (ufbx_node != nullptr && ufbx_node->mesh != nullptr) {
      const auto it = mesh_keys.find(ufbx_node->mesh);
      if (it != mesh_keys.end()) {
        build.renderables.push_back(RenderableRecord {
          .node_index = i,
          .geometry_key = it->second,
          .visible = 1,
          .reserved = {},
        });
      }
    }

    if (ufbx_node != nullptr && ufbx_node->camera != nullptr) {
      const auto& cam = *ufbx_node->camera;
      if (cam.projection_mode == UFBX_PROJECTION_MODE_PERSPECTIVE) {
        float near_plane = std::abs(util::ToFloat(cam.near_plane));
        float far_plane = std::abs(util::ToFloat(cam.far_plane));
        if (far_plane < near_plane) {
          std::swap(far_plane, near_plane);
        }

        const float fov_y_rad = util::ToFloat(cam.field_of_view_deg.y)
          * (std::numbers::pi_v<float> / 180.0F);

        build.perspective_cameras.push_back(PerspectiveCameraRecord {
          .node_index = i,
          .fov_y = fov_y_rad,
          .aspect_ratio = util::ToFloat(cam.aspect_ratio),
          .near_plane = near_plane,
          .far_plane = far_plane,
          .reserved = {},
        });
      } else if (cam.projection_mode == UFBX_PROJECTION_MODE_ORTHOGRAPHIC) {
        float near_plane = std::abs(util::ToFloat(cam.near_plane));
        float far_plane = std::abs(util::ToFloat(cam.far_plane));
        if (far_plane < near_plane) {
          std::swap(far_plane, near_plane);
        }

        const float half_w = util::ToFloat(cam.orthographic_size.x) * 0.5F;
        const float half_h = util::ToFloat(cam.orthographic_size.y) * 0.5F;

        build.orthographic_cameras.push_back(OrthographicCameraRecord {
          .node_index = i,
          .left = -half_w,
          .right = half_w,
          .bottom = -half_h,
          .top = half_h,
          .near_plane = near_plane,
          .far_plane = far_plane,
          .reserved = {},
        });
      } else {
        diagnostics.push_back(MakeWarningDiagnostic("scene.camera.unsupported",
          "Unsupported camera projection type", input.source_id, name));
      }
    }

    if (ufbx_node != nullptr && ufbx_node->light != nullptr) {
      const auto& light = *ufbx_node->light;
      switch (light.type) {
      case UFBX_LIGHT_DIRECTIONAL: {
        DirectionalLightRecord rec_light {};
        rec_light.node_index = i;
        rec_light.common.affects_world = light.cast_light ? 1U : 0U;
        rec_light.common.color_rgb[0]
          = (std::max)(0.0F, util::ToFloat(light.color.x));
        rec_light.common.color_rgb[1]
          = (std::max)(0.0F, util::ToFloat(light.color.y));
        rec_light.common.color_rgb[2]
          = (std::max)(0.0F, util::ToFloat(light.color.z));
        rec_light.common.intensity
          = (std::max)(0.0F, util::ToFloat(light.intensity));
        rec_light.common.casts_shadows = light.cast_shadows ? 1U : 0U;
        build.directional_lights.push_back(rec_light);
        break;
      }
      case UFBX_LIGHT_POINT:
      case UFBX_LIGHT_AREA:
      case UFBX_LIGHT_VOLUME: {
        PointLightRecord rec_light {};
        rec_light.node_index = i;
        rec_light.common.affects_world = light.cast_light ? 1U : 0U;
        rec_light.common.color_rgb[0]
          = (std::max)(0.0F, util::ToFloat(light.color.x));
        rec_light.common.color_rgb[1]
          = (std::max)(0.0F, util::ToFloat(light.color.y));
        rec_light.common.color_rgb[2]
          = (std::max)(0.0F, util::ToFloat(light.color.z));
        rec_light.common.intensity
          = (std::max)(0.0F, util::ToFloat(light.intensity));
        rec_light.common.casts_shadows = light.cast_shadows ? 1U : 0U;
        build.point_lights.push_back(rec_light);
        if (light.type != UFBX_LIGHT_POINT) {
          diagnostics.push_back(
            MakeWarningDiagnostic("fbx.light.unsupported_type",
              "Unsupported FBX light type converted to point light",
              input.source_id, name));
        }
        break;
      }
      case UFBX_LIGHT_SPOT: {
        SpotLightRecord rec_light {};
        rec_light.node_index = i;
        rec_light.common.affects_world = light.cast_light ? 1U : 0U;
        rec_light.common.color_rgb[0]
          = (std::max)(0.0F, util::ToFloat(light.color.x));
        rec_light.common.color_rgb[1]
          = (std::max)(0.0F, util::ToFloat(light.color.y));
        rec_light.common.color_rgb[2]
          = (std::max)(0.0F, util::ToFloat(light.color.z));
        rec_light.common.intensity
          = (std::max)(0.0F, util::ToFloat(light.intensity));
        rec_light.common.casts_shadows = light.cast_shadows ? 1U : 0U;
        const float inner = util::ToFloat(light.inner_angle);
        const float outer = util::ToFloat(light.outer_angle);
        rec_light.inner_cone_angle_radians = (std::max)(0.0F, inner);
        rec_light.outer_cone_angle_radians
          = (std::max)(rec_light.inner_cone_angle_radians, outer);
        build.spot_lights.push_back(rec_light);
        break;
      }
      default:
        diagnostics.push_back(MakeWarningDiagnostic("scene.light.unsupported",
          "Unsupported light type", input.source_id, name));
        break;
      }
    }
  }

  result.build = std::move(build);
  result.success = true;
  return result;
}

auto FbxAdapter::BuildWorkItems(SceneWorkTag, SceneWorkItemSink& sink,
  const AdapterInput& input) -> WorkItemStreamResult
{
  if (!impl_->scene_owner) {
    WorkItemStreamResult result;
    result.success = false;
    result.diagnostics.push_back(MakeErrorDiagnostic("fbx.scene.not_parsed",
      "FBX adapter has no parsed scene", input.source_id_prefix,
      input.object_path_prefix));
    return result;
  }

  WorkItemStreamResult result;
  if (input.stop_token.stop_requested()) {
    result.success = false;
    result.diagnostics.push_back(MakeCancelDiagnostic(input.source_id_prefix));
    return result;
  }

  auto item = ScenePipeline::WorkItem::MakeWorkItem(shared_from_this(),
    BuildSceneSourceId(input.source_id_prefix, input.request), {}, {},
    input.request, input.stop_token);

  if (!sink.Consume(std::move(item))) {
    return result;
  }

  ++result.emitted;
  return result;
}

} // namespace oxygen::content::import::adapters
