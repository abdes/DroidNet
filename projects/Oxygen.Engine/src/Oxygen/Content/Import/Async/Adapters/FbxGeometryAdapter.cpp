//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <Oxygen/Content/Import/Async/Adapters/FbxGeometryAdapter.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include <glm/glm.hpp>

#include <Oxygen/Base/Logging.h>
#include <Oxygen/Content/Import/fbx/UfbxUtils.h>
#include <Oxygen/Content/Import/util/CoordTransform.h>
#include <Oxygen/Content/Import/util/ImportNaming.h>

namespace oxygen::content::import::adapters {

namespace {

  struct UfbxSceneView final {
    const ufbx_scene* scene = nullptr;
    std::shared_ptr<const void> scene_owner;
  };

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

  template <typename T> [[nodiscard]] auto HasAttribute(const T& stream) -> bool
  {
    return stream.exists && stream.values.data != nullptr
      && stream.indices.data != nullptr;
  }

  struct TriangulatedMeshBuffers {
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
        if (deformer == nullptr) {
          continue;
        }
        if (is_connected_to(deformer->element.connections_dst, &mesh.element)
          || is_connected_to(
            deformer->element.connections_src, &mesh.element)) {
          return deformer;
        }
        for (size_t inst = 0; inst < mesh.instances.count; ++inst) {
          const auto* node = mesh.instances.data[inst];
          if (node == nullptr) {
            continue;
          }
          if (is_connected_to(deformer->element.connections_dst, &node->element)
            || is_connected_to(
              deformer->element.connections_src, &node->element)) {
            return deformer;
          }
        }
      }
    }

    return nullptr;
  }

  [[nodiscard]] auto BuildSkinningBuffers(const ufbx_mesh& mesh,
    const ufbx_skin_deformer& deformer, TriangulatedMeshBuffers& out,
    std::vector<ImportDiagnostic>& diagnostics, std::string_view source_id,
    std::string_view object_path) -> bool
  {
    if (deformer.vertices.count < mesh.num_vertices) {
      DLOG_F(ERROR,
        "FBX skin data invalid: deformer vertices < mesh vertices ({} < {})",
        deformer.vertices.count, mesh.num_vertices);
      diagnostics.push_back(MakeErrorDiagnostic("mesh.skin_data_invalid",
        "Skin deformer vertex count is smaller than mesh vertex count",
        source_id, object_path));
      return false;
    }
    if (deformer.weights.count == 0) {
      DLOG_F(ERROR, "FBX skin data invalid: deformer weights empty");
      diagnostics.push_back(MakeErrorDiagnostic("mesh.skin_data_invalid",
        "Skin deformer weights array is empty", source_id, object_path));
      return false;
    }

    if (deformer.clusters.count == 0) {
      DLOG_F(ERROR, "FBX skin data invalid: no skin clusters present");
      diagnostics.push_back(MakeErrorDiagnostic("mesh.skin_data_invalid",
        "Skin deformer has no clusters", source_id, object_path));
      return false;
    }

    out.inverse_bind_matrices.reserve(deformer.clusters.count);
    out.joint_remap.reserve(deformer.clusters.count);
    for (size_t i = 0; i < deformer.clusters.count; ++i) {
      const auto* cluster = deformer.clusters.data[i];
      if (cluster == nullptr) {
        DLOG_F(ERROR, "FBX skin data invalid: null cluster at index {}", i);
        diagnostics.push_back(MakeErrorDiagnostic("mesh.skin_data_invalid",
          "Skin deformer cluster is null", source_id, object_path));
        return false;
      }
      out.inverse_bind_matrices.push_back(ToMat4(cluster->geometry_to_bone));
      out.joint_remap.push_back(static_cast<uint32_t>(i));
    }

    out.joint_indices.reserve(mesh.num_indices);
    out.joint_weights.reserve(mesh.num_indices);

    const uint32_t max_influences = 4;
    const auto max_weights_hint = deformer.max_weights_per_vertex;
    bool trimmed_influences = max_weights_hint > max_influences;
    bool missing_weights = false;
    bool invalid_cluster_index = false;
    bool invalid_weight_values = false;

    for (size_t idx = 0; idx < mesh.num_indices; ++idx) {
      const auto vertex_index = mesh.vertex_indices.data[idx];
      if (vertex_index >= deformer.vertices.count) {
        DLOG_F(ERROR,
          "FBX skin data invalid: vertex index {} >= deformer vertices {}",
          vertex_index, deformer.vertices.count);
        diagnostics.push_back(MakeErrorDiagnostic("mesh.skin_data_invalid",
          "Skin deformer vertex index out of bounds", source_id, object_path));
        return false;
      }

      const auto& skin_vertex = deformer.vertices.data[vertex_index];
      const auto weight_begin = skin_vertex.weight_begin;
      const auto weight_count = skin_vertex.num_weights;
      if (weight_begin + weight_count > deformer.weights.count) {
        DLOG_F(ERROR, "FBX skin data invalid: weight range {}..{} exceeds {}",
          weight_begin, weight_begin + weight_count, deformer.weights.count);
        diagnostics.push_back(MakeErrorDiagnostic("mesh.skin_data_invalid",
          "Skin deformer weight range out of bounds", source_id, object_path));
        return false;
      }

      const uint32_t influence_count = (std::min)(weight_count, max_influences);
      if (weight_count > max_influences) {
        trimmed_influences = true;
      }

      std::array<uint32_t, 4> indices { 0, 0, 0, 0 };
      std::array<float, 4> weights { 0.0F, 0.0F, 0.0F, 0.0F };

      float weight_sum = 0.0F;
      for (uint32_t i = 0; i < influence_count; ++i) {
        const auto& weight = deformer.weights.data[weight_begin + i];
        auto cluster_index = weight.cluster_index;
        if (cluster_index >= deformer.clusters.count) {
          invalid_cluster_index = true;
          cluster_index = 0;
        }
        float value = static_cast<float>(weight.weight);
        if (!std::isfinite(value) || value < 0.0F) {
          invalid_weight_values = true;
          value = 0.0F;
        }
        indices[i] = cluster_index;
        weights[i] = value;
        weight_sum += weights[i];
      }

      if (weight_sum > 0.0F) {
        const float inv = 1.0F / weight_sum;
        for (uint32_t i = 0; i < influence_count; ++i) {
          weights[i] *= inv;
        }
      } else {
        missing_weights = true;
      }

      out.joint_indices.push_back(
        glm::uvec4 { indices[0], indices[1], indices[2], indices[3] });
      out.joint_weights.push_back(
        glm::vec4 { weights[0], weights[1], weights[2], weights[3] });
    }

    if (trimmed_influences) {
      diagnostics.push_back(MakeWarningDiagnostic(
        "mesh.skin_influences_trimmed",
        "Skinning influences trimmed to 4 per vertex", source_id, object_path));
    }
    if (missing_weights) {
      diagnostics.push_back(MakeWarningDiagnostic("mesh.skin_weights_missing",
        "Skinning weights missing or zero for some vertices", source_id,
        object_path));
    }
    if (invalid_cluster_index) {
      DLOG_F(WARNING,
        "FBX skin data has out-of-range cluster indices; clamped to 0");
      diagnostics.push_back(MakeWarningDiagnostic("mesh.skin_cluster_oob",
        "Skinning cluster indices out of range; clamped to zero", source_id,
        object_path));
    }
    if (invalid_weight_values) {
      DLOG_F(WARNING, "FBX skin data has invalid weight values; clamped to 0");
      diagnostics.push_back(MakeWarningDiagnostic("mesh.skin_weights_invalid",
        "Skinning weights contained invalid values; clamped to zero", source_id,
        object_path));
    }

    return true;
  }

  [[nodiscard]] auto BuildTriangulatedBuffers(const ufbx_mesh& mesh,
    const std::vector<data::AssetKey>& material_keys,
    const data::AssetKey default_material_key,
    std::vector<ImportDiagnostic>& diagnostics, std::string_view source_id,
    std::string_view object_path) -> std::optional<TriangulatedMeshBuffers>
  {
    if (mesh.num_indices == 0 || mesh.num_faces == 0) {
      diagnostics.push_back(MakeErrorDiagnostic("mesh.missing_buffers",
        "FBX mesh has no indices/faces", source_id, object_path));
      return std::nullopt;
    }

    if (!HasAttribute(mesh.vertex_position)) {
      diagnostics.push_back(MakeErrorDiagnostic("mesh.missing_positions",
        "FBX mesh has no vertex positions", source_id, object_path));
      return std::nullopt;
    }

    TriangulatedMeshBuffers out;
    out.positions.reserve(mesh.num_indices);
    out.normals.reserve(mesh.num_indices);
    out.texcoords.reserve(mesh.num_indices);
    out.tangents.reserve(mesh.num_indices);
    out.bitangents.reserve(mesh.num_indices);
    out.colors.reserve(mesh.num_indices);

    const bool has_normals = HasAttribute(mesh.vertex_normal);
    const bool has_uvs = HasAttribute(mesh.vertex_uv);
    const bool has_tangents = HasAttribute(mesh.vertex_tangent);
    const bool has_bitangents = HasAttribute(mesh.vertex_bitangent);
    const bool has_colors = HasAttribute(mesh.vertex_color);

    if (material_keys.empty() && default_material_key == data::AssetKey {}) {
      diagnostics.push_back(MakeWarningDiagnostic("mesh.missing_materials",
        "Mesh material list is empty; default material key is unset", source_id,
        object_path));
    }

    for (size_t idx = 0; idx < mesh.num_indices; ++idx) {
      out.positions.push_back(ToVec3(mesh.vertex_position[idx]));
      if (has_normals) {
        out.normals.push_back(ToVec3(mesh.vertex_normal[idx]));
      }
      if (has_uvs) {
        out.texcoords.push_back(ToVec2(mesh.vertex_uv[idx]));
      }
      if (has_tangents) {
        out.tangents.push_back(ToVec3(mesh.vertex_tangent[idx]));
      }
      if (has_bitangents) {
        out.bitangents.push_back(ToVec3(mesh.vertex_bitangent[idx]));
      }
      if (has_colors) {
        out.colors.push_back(ToVec4(mesh.vertex_color[idx]));
      }
    }

    std::unordered_map<uint32_t, size_t> range_index_by_material;
    std::vector<std::vector<uint32_t>> bucket_indices;

    std::vector<uint32_t> tri_indices;
    tri_indices.resize(static_cast<size_t>(mesh.max_face_triangles) * 3);

    for (size_t face_i = 0; face_i < mesh.faces.count; ++face_i) {
      const auto face = mesh.faces.data[face_i];
      if (face.num_indices < 3) {
        continue;
      }

      uint32_t material_slot = 0;
      if (mesh.face_material.data != nullptr
        && face_i < mesh.face_material.count && mesh.materials.data != nullptr
        && mesh.materials.count > 0) {
        const uint32_t slot = mesh.face_material.data[face_i];
        if (slot != UFBX_NO_INDEX && slot < mesh.materials.count) {
          material_slot = slot;
        }
      }

      if (!material_keys.empty() && material_slot >= material_keys.size()) {
        diagnostics.push_back(MakeWarningDiagnostic("mesh.material_slot_oob",
          "Mesh material slot exceeds imported material key count", source_id,
          object_path));
      }

      const auto found = range_index_by_material.find(material_slot);
      size_t bucket_index = 0;
      if (found != range_index_by_material.end()) {
        bucket_index = found->second;
      } else {
        bucket_index = bucket_indices.size();
        range_index_by_material.emplace(material_slot, bucket_index);
        bucket_indices.emplace_back();
      }

      const auto tri_count = ufbx_triangulate_face(
        tri_indices.data(), tri_indices.size(), &mesh, face);

      auto& bucket = bucket_indices[bucket_index];
      bucket.insert(bucket.end(), tri_indices.begin(),
        tri_indices.begin() + static_cast<ptrdiff_t>(tri_count) * 3);
    }

    std::vector<std::pair<uint32_t, size_t>> material_pairs;
    material_pairs.reserve(range_index_by_material.size());
    for (const auto& [slot, index] : range_index_by_material) {
      material_pairs.emplace_back(slot, index);
    }
    std::sort(material_pairs.begin(), material_pairs.end(),
      [](const auto& a, const auto& b) { return a.first < b.first; });

    out.indices.clear();
    for (const auto& [slot, bucket_index] : material_pairs) {
      const auto& bucket = bucket_indices[bucket_index];
      if (bucket.empty()) {
        continue;
      }

      const uint32_t first_index = static_cast<uint32_t>(out.indices.size());
      const uint32_t index_count = static_cast<uint32_t>(bucket.size());
      out.indices.insert(out.indices.end(), bucket.begin(), bucket.end());

      out.ranges.push_back(TriangleRange {
        .material_slot = slot,
        .first_index = first_index,
        .index_count = index_count,
      });
    }

    if (out.indices.empty()) {
      diagnostics.push_back(MakeErrorDiagnostic("mesh.missing_buffers",
        "FBX mesh produced no triangle indices", source_id, object_path));
      return std::nullopt;
    }

    if (const auto* deformer = FindSkinDeformer(mesh); deformer != nullptr) {
      if (mesh.skin_deformers.count > 1) {
        diagnostics.push_back(
          MakeWarningDiagnostic("mesh.multiple_skin_deformers",
            "Mesh has multiple skin deformers; using the first one", source_id,
            object_path));
      }
      if (!BuildSkinningBuffers(
            mesh, *deformer, out, diagnostics, source_id, object_path)) {
        return std::nullopt;
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
    return id;
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
    const GeometryAdapterInput& input,
    std::vector<ImportDiagnostic>& diagnostics) -> std::optional<UfbxSceneView>
  {
    if (input.stop_token.stop_requested()) {
      DLOG_F(
        WARNING, "FBX load cancelled: source_id='{}'", input.source_id_prefix);
      diagnostics.push_back(MakeCancelDiagnostic(input.source_id_prefix));
      return std::nullopt;
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
      return std::nullopt;
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
    opts.handedness_conversion_axis = UFBX_MIRROR_AXIS_Y;
    if (const auto target_unit_meters
      = coord::ComputeTargetUnitMeters(coordinate_policy);
      target_unit_meters.has_value()) {
      opts.target_unit_meters = *target_unit_meters;
    }
    opts.generate_missing_normals = true;
    opts.skip_skin_vertices = false;
    opts.clean_skin_weights = true;

    ufbx_scene* scene = ufbx_load_file(path.string().c_str(), &opts, &error);
    if (scene == nullptr) {
      if (error.type == UFBX_ERROR_CANCELLED
        || input.stop_token.stop_requested()) {
        DLOG_F(WARNING, "FBX load cancelled: path='{}'", path.string());
        diagnostics.push_back(MakeCancelDiagnostic(input.source_id_prefix));
        return std::nullopt;
      }
      const auto desc = fbx::ToStringView(error.description);
      DLOG_F(
        ERROR, "FBX load failed: path='{}' error='{}'", path.string(), desc);
      diagnostics.push_back(MakeSceneLoadError(input.source_id_prefix, desc));
      return std::nullopt;
    }

    auto owner
      = std::shared_ptr<const ufbx_scene>(scene, [](const ufbx_scene* value) {
          ufbx_free_scene(const_cast<ufbx_scene*>(value));
        });

    return UfbxSceneView {
      .scene = owner.get(),
      .scene_owner = std::static_pointer_cast<const void>(owner),
    };
  }

  [[nodiscard]] auto LoadSceneFromMemory(const std::span<const std::byte> bytes,
    const GeometryAdapterInput& input,
    std::vector<ImportDiagnostic>& diagnostics) -> std::optional<UfbxSceneView>
  {
    if (input.stop_token.stop_requested()) {
      DLOG_F(WARNING, "FBX load cancelled (memory): source_id='{}'",
        input.source_id_prefix);
      diagnostics.push_back(MakeCancelDiagnostic(input.source_id_prefix));
      return std::nullopt;
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
      return std::nullopt;
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
    opts.handedness_conversion_axis = UFBX_MIRROR_AXIS_Y;
    if (const auto target_unit_meters
      = coord::ComputeTargetUnitMeters(coordinate_policy);
      target_unit_meters.has_value()) {
      opts.target_unit_meters = *target_unit_meters;
    }
    opts.generate_missing_normals = true;
    opts.skip_skin_vertices = false;
    opts.clean_skin_weights = true;

    const void* data = bytes.data();
    const auto size = static_cast<size_t>(bytes.size());
    ufbx_scene* scene = ufbx_load_memory(data, size, &opts, &error);
    if (scene == nullptr) {
      if (error.type == UFBX_ERROR_CANCELLED
        || input.stop_token.stop_requested()) {
        DLOG_F(WARNING, "FBX load cancelled (memory): source_id='{}'",
          input.source_id_prefix);
        diagnostics.push_back(MakeCancelDiagnostic(input.source_id_prefix));
        return std::nullopt;
      }
      const auto desc = fbx::ToStringView(error.description);
      DLOG_F(ERROR, "FBX load failed (memory): error='{}'", desc);
      diagnostics.push_back(MakeSceneLoadError(input.source_id_prefix, desc));
      return std::nullopt;
    }

    auto owner
      = std::shared_ptr<const ufbx_scene>(scene, [](const ufbx_scene* value) {
          ufbx_free_scene(const_cast<ufbx_scene*>(value));
        });

    return UfbxSceneView {
      .scene = owner.get(),
      .scene_owner = std::static_pointer_cast<const void>(owner),
    };
  }

} // namespace

[[nodiscard]] auto BuildWorkItemsFromScene(const UfbxSceneView& scene,
  const GeometryAdapterInput& input) -> GeometryAdapterOutput
{
  GeometryAdapterOutput output;
  if (scene.scene == nullptr) {
    DLOG_F(ERROR, "FBX scene is null: source_id='{}'", input.source_id_prefix);
    output.success = false;
    output.diagnostics.push_back(MakeErrorDiagnostic("fbx.scene.null",
      "FBX scene is null", input.source_id_prefix, input.object_path_prefix));
    return output;
  }

  std::unordered_map<std::string, uint32_t> name_usage;
  const auto mesh_count = static_cast<uint32_t>(scene.scene->meshes.count);
  DLOG_F(2, "FBX scene meshes={} skin_deformers={}", mesh_count,
    scene.scene->skin_deformers.count);
  output.work_items.reserve(mesh_count);

  for (uint32_t mesh_i = 0; mesh_i < mesh_count; ++mesh_i) {
    if (input.stop_token.stop_requested()) {
      output.success = false;
      output.diagnostics.push_back(
        MakeCancelDiagnostic(input.source_id_prefix));
      return output;
    }

    const auto* mesh = scene.scene->meshes.data[mesh_i];
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
    auto mesh_name = util::BuildMeshName(authored_name, input.request, mesh_i);

    if (const auto it = name_usage.find(mesh_name); it != name_usage.end()) {
      mesh_name += "_" + std::to_string(it->second);
    }
    name_usage[mesh_name]++;

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
    item.has_material_textures = false;
    item.request = input.request;
    item.stop_token = input.stop_token;

    std::vector<ImportDiagnostic> diagnostics;
    auto buffers = BuildTriangulatedBuffers(*mesh, item.material_keys,
      item.default_material_key, diagnostics, item.source_id, item.mesh_name);
    if (!buffers.has_value()) {
      output.diagnostics.insert(
        output.diagnostics.end(), diagnostics.begin(), diagnostics.end());
      output.success = false;
      continue;
    }

    const auto* skin_deformer = FindSkinDeformer(*mesh);
    DLOG_F(2, "FBX mesh[{}] skin_deformer_found={} joints={} weights={}",
      mesh_i, skin_deformer != nullptr, buffers->joint_indices.size(),
      buffers->joint_weights.size());

    const bool is_skinned = !buffers->joint_indices.empty()
      && buffers->joint_weights.size() == buffers->joint_indices.size();

    auto owner = std::make_shared<TriangulatedMeshBuffers>(std::move(*buffers));
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
      .bounds = std::nullopt,
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

  return output;
}

auto FbxGeometryAdapter::BuildWorkItems(
  const std::filesystem::path& source_path,
  const GeometryAdapterInput& input) const -> GeometryAdapterOutput
{
  GeometryAdapterOutput output;
  auto scene = LoadSceneFromFile(source_path, input, output.diagnostics);
  if (!scene.has_value()) {
    DLOG_F(ERROR, "FBX load failed: path='{}' diagnostics={}",
      source_path.string(), output.diagnostics.size());
    if (output.diagnostics.empty()) {
      output.diagnostics.push_back(MakeErrorDiagnostic("fbx.load_failed",
        "FBX load failed without diagnostics", input.source_id_prefix, ""));
    }
    output.success = false;
    return output;
  }

  return BuildWorkItemsFromScene(*scene, input);
}

auto FbxGeometryAdapter::BuildWorkItems(
  const std::span<const std::byte> source_bytes,
  const GeometryAdapterInput& input) const -> GeometryAdapterOutput
{
  GeometryAdapterOutput output;
  auto scene = LoadSceneFromMemory(source_bytes, input, output.diagnostics);
  if (!scene.has_value()) {
    DLOG_F(ERROR, "FBX load failed (memory): diagnostics={}",
      output.diagnostics.size());
    if (output.diagnostics.empty()) {
      output.diagnostics.push_back(MakeErrorDiagnostic("fbx.load_failed",
        "FBX load failed without diagnostics", input.source_id_prefix, ""));
    }
    output.success = false;
    return output;
  }

  return BuildWorkItemsFromScene(*scene, input);
}

} // namespace oxygen::content::import::adapters
