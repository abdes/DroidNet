//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <limits>
#include <memory>
#include <numeric>
#include <optional>
#include <span>
#include <sstream>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include <Oxygen/Base/Logging.h>
#include <Oxygen/Content/Import/Pipelines/GeometryPipeline.h>
#include <Oxygen/Content/Import/TextureImportPresets.h>
#include <Oxygen/Content/Import/gltf/GltfAdapter.h>
#include <Oxygen/Content/Import/gltf/cgltf.h>
#include <Oxygen/Content/Import/util/ImportNaming.h>
#include <Oxygen/Content/Import/util/StringUtils.h>
#include <Oxygen/Core/Transforms/Decompose.h>
#include <Oxygen/Data/PakFormat.h>

namespace oxygen::content::import::adapters {

struct GltfAdapter::Impl final {
  std::shared_ptr<const cgltf_data> data_owner;
};

GltfAdapter::GltfAdapter()
  : impl_(std::make_unique<Impl>())
{
}

GltfAdapter::~GltfAdapter() = default;

namespace {

  using oxygen::data::pak::DirectionalLightRecord;
  using oxygen::data::pak::NodeRecord;
  using oxygen::data::pak::OrthographicCameraRecord;
  using oxygen::data::pak::PerspectiveCameraRecord;
  using oxygen::data::pak::PointLightRecord;
  using oxygen::data::pak::RenderableRecord;
  using oxygen::data::pak::SpotLightRecord;

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
    const AdapterInput& input, std::vector<ImportDiagnostic>& diagnostics)
    -> CgltfDataPtr
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
    const AdapterInput& input, std::vector<ImportDiagnostic>& diagnostics)
    -> CgltfDataPtr
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
        glm::vec4 { v[0], v[1], v[2], v[3] },
        glm::vec4 { v[4], v[5], v[6], v[7] },
        glm::vec4 { v[8], v[9], v[10], v[11] },
        glm::vec4 { v[12], v[13], v[14], v[15] },
      };
    }
    return out;
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

  struct AccessorBounds final {
    glm::vec3 min = glm::vec3(0.0F);
    glm::vec3 max = glm::vec3(0.0F);
  };

  [[nodiscard]] auto ReadAccessorBounds(const cgltf_accessor* accessor)
    -> std::optional<AccessorBounds>
  {
    if (accessor == nullptr || accessor->has_min == 0
      || accessor->has_max == 0) {
      return std::nullopt;
    }

    return AccessorBounds {
      .min = glm::vec3(static_cast<float>(accessor->min[0]),
        static_cast<float>(accessor->min[1]),
        static_cast<float>(accessor->min[2])),
      .max = glm::vec3(static_cast<float>(accessor->max[0]),
        static_cast<float>(accessor->max[1]),
        static_cast<float>(accessor->max[2])),
    };
  }

  [[nodiscard]] auto ToBounds3(const AccessorBounds& bounds) -> Bounds3
  {
    Bounds3 out;
    out.min = std::array<float, 3> {
      bounds.min.x,
      bounds.min.y,
      bounds.min.z,
    };
    out.max = std::array<float, 3> {
      bounds.max.x,
      bounds.max.y,
      bounds.max.z,
    };
    return out;
  }

  [[nodiscard]] auto DetermineJointCount(const std::vector<glm::uvec4>& indices)
    -> uint32_t
  {
    uint32_t max_joint = 0;
    for (const auto& joints : indices) {
      max_joint = (std::max)(max_joint, joints.x);
      max_joint = (std::max)(max_joint, joints.y);
      max_joint = (std::max)(max_joint, joints.z);
      max_joint = (std::max)(max_joint, joints.w);
    }
    return indices.empty() ? 0 : (max_joint + 1U);
  }

  [[nodiscard]] auto FindSkinForMesh(
    const cgltf_data& data, const cgltf_mesh& mesh) -> const cgltf_skin*
  {
    if (mesh.primitives_count == 0) {
      return nullptr;
    }

    for (cgltf_size i = 0; i < data.nodes_count; ++i) {
      const auto* node = &data.nodes[i];
      if (node != nullptr && node->mesh == &mesh && node->skin != nullptr) {
        return node->skin;
      }
    }
    return nullptr;
  }

  //! Compute the world transform matrix for a glTF node (in glTF space).
  [[nodiscard]] auto ComputeNodeWorldTransform(const cgltf_node* node)
    -> glm::mat4
  {
    if (node == nullptr) {
      return glm::mat4 { 1.0F };
    }

    cgltf_float world_matrix[16] = {};
    cgltf_node_transform_world(node, world_matrix);

    glm::mat4 result(1.0F);
    for (int c = 0; c < 4; ++c) {
      for (int r = 0; r < 4; ++r) {
        result[c][r] = static_cast<float>(world_matrix[c * 4 + r]);
      }
    }
    return result;
  }

  //! Check if a mesh requires winding reversal based on glTF spec.
  /*!
   Per glTF 2.0 spec section 3.7.4: "When a mesh primitive uses any
   triangle-based topology, the determinant of the node's global transform
   defines the winding order of that primitive. If the determinant is a
   positive value, the winding order triangle faces is counterclockwise;
   in the opposite case, the winding order is clockwise."

   This function finds the first node that references the given mesh and
   checks if its world transform has a negative determinant.

   @note If a mesh is instanced by multiple nodes with different determinant
   signs, this returns the result for the first instance found. In practice,
   glTF exporters typically avoid such configurations.
  */
  [[nodiscard]] auto MeshRequiresWindingReversal(
    const cgltf_data& data, const cgltf_mesh& mesh) -> bool
  {
    for (cgltf_size i = 0; i < data.nodes_count; ++i) {
      const auto* node = &data.nodes[i];
      if (node != nullptr && node->mesh == &mesh) {
        const auto world_transform = ComputeNodeWorldTransform(node);
        const auto det = glm::determinant(glm::mat3(world_transform));
        return det < 0.0F;
      }
    }
    // No node references this mesh; assume no reversal needed
    return false;
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
    id.append("::");
    id.append(std::to_string(ordinal));
    return id;
  }

  enum class TextureUsage : uint8_t {
    kBaseColor,
    kNormal,
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
    case TextureUsage::kMetallicRoughness:
      return TexturePreset::kORMPacked;
    case TextureUsage::kOcclusion:
      return TexturePreset::kAO;
    case TextureUsage::kEmissive:
      return TexturePreset::kEmissive;
    }
    return TexturePreset::kData;
  }

  [[nodiscard]] auto BuildTextureName(
    const cgltf_data& data, const cgltf_texture& texture) -> std::string
  {
    if (texture.name != nullptr && *texture.name != '\0') {
      return texture.name;
    }
    if (texture.image != nullptr && texture.image->name != nullptr
      && *texture.image->name != '\0') {
      return texture.image->name;
    }
    if (texture.image != nullptr && texture.image->uri != nullptr
      && *texture.image->uri != '\0') {
      return texture.image->uri;
    }
    const auto index = cgltf_texture_index(&data, &texture);
    return "texture_" + std::to_string(index);
  }

  [[nodiscard]] auto BuildTextureSourceId(std::string_view prefix,
    const cgltf_data& data, const cgltf_texture& texture,
    const TextureUsage usage) -> std::string
  {
    auto name = BuildTextureName(data, texture);
    std::string id;
    if (!prefix.empty()) {
      id = std::string(prefix);
      id.append("::");
    }
    id.append("tex::");
    id.append(name);
    id.append("::");
    id.append(UsageLabel(usage));
    return id;
  }

  [[nodiscard]] auto ResolveUvSet(const cgltf_texture_view& view) -> uint8_t
  {
    if (view.has_transform && view.transform.has_texcoord) {
      return static_cast<uint8_t>(view.transform.texcoord);
    }
    return static_cast<uint8_t>(view.texcoord);
  }

  auto ApplyTextureBinding(MaterialTextureBinding& binding,
    const cgltf_texture_view& view, std::string source_id) -> void
  {
    binding.assigned = true;
    binding.source_id = std::move(source_id);
    binding.index = 0;
    binding.uv_set = ResolveUvSet(view);
    if (view.has_transform) {
      binding.uv_transform.scale[0]
        = static_cast<float>(view.transform.scale[0]);
      binding.uv_transform.scale[1]
        = static_cast<float>(view.transform.scale[1]);
      binding.uv_transform.offset[0]
        = static_cast<float>(view.transform.offset[0]);
      binding.uv_transform.offset[1]
        = static_cast<float>(view.transform.offset[1]);
      binding.uv_transform.rotation_radians
        = static_cast<float>(view.transform.rotation);
    }

    DLOG_F(INFO,
      "glTF texture bind: source_id='{}' uv_set={} uv_scale=({:.4f},{:.4f}) "
      "uv_offset=({:.4f},{:.4f}) uv_rot={:.4f}",
      binding.source_id, binding.uv_set, binding.uv_transform.scale[0],
      binding.uv_transform.scale[1], binding.uv_transform.offset[0],
      binding.uv_transform.offset[1], binding.uv_transform.rotation_radians);
  }

  [[nodiscard]] auto LoadExternalBytes(const std::filesystem::path& path,
    std::vector<ImportDiagnostic>& diagnostics, std::string_view source_id)
    -> std::shared_ptr<std::vector<std::byte>>
  {
    std::ifstream file(path, std::ios::binary | std::ios::ate);
    if (!file) {
      diagnostics.push_back(MakeWarningDiagnostic("gltf.image.load_failed",
        "Failed to open glTF image file", source_id, path.string()));
      return std::make_shared<std::vector<std::byte>>();
    }

    const auto size = file.tellg();
    if (size <= 0) {
      diagnostics.push_back(MakeWarningDiagnostic("gltf.image.empty",
        "glTF image file is empty", source_id, path.string()));
      return std::make_shared<std::vector<std::byte>>();
    }

    auto bytes
      = std::make_shared<std::vector<std::byte>>(static_cast<size_t>(size));
    file.seekg(0, std::ios::beg);
    if (!file.read(reinterpret_cast<char*>(bytes->data()), size)) {
      diagnostics.push_back(MakeWarningDiagnostic("gltf.image.read_failed",
        "Failed to read glTF image file", source_id, path.string()));
      return std::make_shared<std::vector<std::byte>>();
    }

    return bytes;
  }

  [[nodiscard]] auto DecodeBase64(std::string_view encoded)
    -> std::shared_ptr<std::vector<std::byte>>
  {
    auto bytes = std::make_shared<std::vector<std::byte>>();
    bytes->reserve((encoded.size() * 3U) / 4U);

    auto decode_char = [](const char c) -> int {
      if (c >= 'A' && c <= 'Z') {
        return c - 'A';
      }
      if (c >= 'a' && c <= 'z') {
        return c - 'a' + 26;
      }
      if (c >= '0' && c <= '9') {
        return c - '0' + 52;
      }
      if (c == '+') {
        return 62;
      }
      if (c == '/') {
        return 63;
      }
      return -1;
    };

    uint32_t accum = 0;
    int bits = 0;
    for (const char c : encoded) {
      if (c == '=') {
        break;
      }

      const int value = decode_char(c);
      if (value < 0) {
        continue;
      }

      accum = (accum << 6) | static_cast<uint32_t>(value);
      bits += 6;
      if (bits >= 8) {
        bits -= 8;
        const auto byte = static_cast<std::byte>((accum >> bits) & 0xFFu);
        bytes->push_back(byte);
      }
    }

    return bytes;
  }

  [[nodiscard]] auto DecodeDataUri(std::string_view uri,
    std::vector<ImportDiagnostic>& diagnostics, std::string_view source_id)
    -> std::shared_ptr<std::vector<std::byte>>
  {
    const auto comma = uri.find(',');
    if (comma == std::string_view::npos) {
      diagnostics.push_back(MakeWarningDiagnostic("gltf.image.data_uri",
        "glTF data URI is missing a payload", source_id, ""));
      return std::make_shared<std::vector<std::byte>>();
    }

    const auto header = uri.substr(0, comma);
    if (header.find(";base64") == std::string_view::npos) {
      diagnostics.push_back(MakeWarningDiagnostic("gltf.image.data_uri",
        "glTF data URI is not base64 encoded", source_id, ""));
      return std::make_shared<std::vector<std::byte>>();
    }

    auto bytes = DecodeBase64(uri.substr(comma + 1));
    if (bytes->empty()) {
      diagnostics.push_back(MakeWarningDiagnostic("gltf.image.data_uri",
        "glTF data URI payload is empty", source_id, ""));
      return std::make_shared<std::vector<std::byte>>();
    }

    return bytes;
  }

  [[nodiscard]] auto ResolveImageBytes(const cgltf_image& image,
    const std::filesystem::path& base_dir,
    const std::shared_ptr<const cgltf_data>& owner,
    std::vector<ImportDiagnostic>& diagnostics, std::string_view source_id)
    -> std::optional<TexturePipeline::SourceBytes>
  {
    const auto make_placeholder = []() -> TexturePipeline::SourceBytes {
      auto bytes = std::make_shared<std::vector<std::byte>>();
      return TexturePipeline::SourceBytes {
        .bytes = std::span<const std::byte>(bytes->data(), bytes->size()),
        .owner = std::static_pointer_cast<const void>(bytes),
      };
    };
    if (image.buffer_view != nullptr && image.buffer_view->buffer != nullptr
      && image.buffer_view->buffer->data != nullptr) {
      const auto* buffer = image.buffer_view->buffer;
      const auto* raw = static_cast<const std::byte*>(buffer->data)
        + image.buffer_view->offset;
      const auto size = static_cast<size_t>(image.buffer_view->size);
      return TexturePipeline::SourceBytes {
        .bytes = std::span<const std::byte>(raw, size),
        .owner = std::static_pointer_cast<const void>(owner),
      };
    }

    if (image.uri == nullptr || *image.uri == '\0') {
      diagnostics.push_back(MakeWarningDiagnostic("gltf.image.missing_uri",
        "glTF image has no buffer view or URI", source_id, ""));
      return make_placeholder();
    }

    std::string_view uri(image.uri);
    if (uri.rfind("data:", 0) == 0) {
      auto bytes = DecodeDataUri(uri, diagnostics, source_id);
      return TexturePipeline::SourceBytes {
        .bytes = std::span<const std::byte>(bytes->data(), bytes->size()),
        .owner = std::static_pointer_cast<const void>(bytes),
      };
    }

    const auto path = base_dir / std::filesystem::path(std::string(uri));
    auto bytes = LoadExternalBytes(path, diagnostics, source_id);

    return TexturePipeline::SourceBytes {
      .bytes = std::span<const std::byte>(bytes->data(), bytes->size()),
      .owner = std::static_pointer_cast<const void>(bytes),
    };
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

  [[nodiscard]] auto GltfToOxygenBasis() -> glm::mat4
  {
    // glTF: Y-up, -Z forward, X right
    // Oxygen: Z-up, -Y forward, X right
    return glm::mat4 {
      glm::vec4 { 1.0F, 0.0F, 0.0F, 0.0F },
      glm::vec4 { 0.0F, 0.0F, 1.0F, 0.0F },
      glm::vec4 { 0.0F, -1.0F, 0.0F, 0.0F },
      glm::vec4 { 0.0F, 0.0F, 0.0F, 1.0F },
    };
  }

  [[nodiscard]] auto ComputeUnitScale(const CoordinateConversionPolicy& policy)
    -> float
  {
    switch (policy.unit_normalization) {
    case UnitNormalizationPolicy::kPreserveSource:
      return 1.0F;
    case UnitNormalizationPolicy::kNormalizeToMeters:
      return 1.0F;
    case UnitNormalizationPolicy::kApplyCustomFactor:
      return policy.custom_unit_scale;
    }
    return 1.0F;
  }

  [[nodiscard]] auto ConvertGltfPosition(
    const glm::vec3 v, const CoordinateConversionPolicy& policy) -> glm::vec3
  {
    // glTF: Y-up, -Z forward, X right
    // Oxygen: Z-up, -Y forward, X right
    const auto converted = glm::vec3 { v.x, -v.z, v.y };
    return converted * ComputeUnitScale(policy);
  }

  [[nodiscard]] auto ConvertGltfDirection(const glm::vec3 v) -> glm::vec3
  {
    // glTF: Y-up, -Z forward, X right
    // Oxygen: Z-up, -Y forward, X right
    return glm::vec3 { v.x, -v.z, v.y };
  }

  [[nodiscard]] auto ConvertGltfTransform(
    const glm::mat4& m, const CoordinateConversionPolicy& policy) -> glm::mat4
  {
    const auto c = GltfToOxygenBasis();
    auto converted = c * m * glm::transpose(c);
    const auto scale = ComputeUnitScale(policy);
    if (scale != 1.0F) {
      converted[3].x *= scale;
      converted[3].y *= scale;
      converted[3].z *= scale;
    }
    return converted;
  }

  [[nodiscard]] auto StreamWorkItemsFromData(const cgltf_data& data,
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

    std::unordered_map<const cgltf_material*, uint32_t> material_index;
    for (cgltf_size i = 0; i < data.materials_count; ++i) {
      const auto* mat = &data.materials[i];
      if (mat != nullptr) {
        material_index.emplace(mat, static_cast<uint32_t>(i));
      }
    }

    std::unordered_map<std::string, uint32_t> name_usage;
    uint32_t mesh_ordinal = 0;

    struct PrimitiveInfo {
      const cgltf_primitive* prim = nullptr;
      const cgltf_accessor* positions = nullptr;
      const cgltf_accessor* normals = nullptr;
      const cgltf_accessor* texcoords = nullptr;
      const cgltf_accessor* tangents = nullptr;
      const cgltf_accessor* colors = nullptr;
      const cgltf_accessor* joints = nullptr;
      const cgltf_accessor* weights = nullptr;
      uint32_t material_slot = 0;
      uint32_t vertex_count = 0;
      uint32_t index_count = 0;
      bool has_normals = false;
      bool has_texcoords = false;
      bool has_tangents = false;
      bool has_colors = false;
      bool has_skin = false;
    };

    for (cgltf_size mesh_i = 0; mesh_i < data.meshes_count; ++mesh_i) {
      const auto* mesh = &data.meshes[mesh_i];
      if (mesh == nullptr) {
        continue;
      }

      const std::string mesh_name = util::BuildMeshName(mesh->name != nullptr
          ? std::string_view(mesh->name)
          : std::string_view {},
        input.request, static_cast<uint32_t>(mesh_i));

      auto storage_name = mesh_name;
      auto& storage_name_count = name_usage[storage_name];
      if (storage_name_count > 0) {
        storage_name += "_" + std::to_string(storage_name_count);
      }
      ++storage_name_count;

      std::vector<PrimitiveInfo> primitives;
      primitives.reserve(mesh->primitives_count);

      bool all_normals = true;
      bool all_texcoords = true;
      bool all_tangents = true;
      bool any_texcoords = false;
      bool any_colors = false;
      bool any_skin = false;
      bool has_material_textures = false;

      size_t total_vertices = 0;
      size_t total_indices = 0;

      for (cgltf_size prim_i = 0; prim_i < mesh->primitives_count; ++prim_i) {
        if (input.stop_token.stop_requested()) {
          result.success = false;
          result.diagnostics.push_back(
            MakeCancelDiagnostic(input.source_id_prefix));
          return result;
        }

        const auto& prim = mesh->primitives[prim_i];
        if (prim.type != cgltf_primitive_type_triangles) {
          result.diagnostics.push_back(
            MakeErrorDiagnostic("gltf.primitive.type",
              "glTF primitive is not a triangle list; import requires "
              "triangles only",
              input.source_id_prefix, mesh_name));
          result.success = false;
          return result;
        }

        const cgltf_accessor* positions = nullptr;
        const cgltf_accessor* normals = nullptr;
        const cgltf_accessor* texcoords = nullptr;
        const cgltf_accessor* tangents = nullptr;
        const cgltf_accessor* colors = nullptr;
        const cgltf_accessor* joints = nullptr;
        const cgltf_accessor* weights = nullptr;
        std::unordered_map<cgltf_size, const cgltf_accessor*>
          texcoords_by_index;
        std::optional<cgltf_size> color_index;
        std::unordered_map<uint32_t, const cgltf_accessor*> joints_by_index;
        std::unordered_map<uint32_t, const cgltf_accessor*> weights_by_index;

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
            texcoords_by_index[attr.index] = attr.data;
            break;
          case cgltf_attribute_type_tangent:
            tangents = attr.data;
            break;
          case cgltf_attribute_type_color:
            if (!color_index.has_value() || attr.index < color_index.value()) {
              color_index = attr.index;
              colors = attr.data;
            }
            break;
          case cgltf_attribute_type_joints:
            joints_by_index[attr.index] = attr.data;
            break;
          case cgltf_attribute_type_weights:
            weights_by_index[attr.index] = attr.data;
            break;
          default:
            break;
          }
        }

        if (!texcoords_by_index.empty()) {
          std::vector<cgltf_size> preferred_uv_sets;
          preferred_uv_sets.reserve(5U);

          if (prim.material != nullptr) {
            const auto& material = *prim.material;
            if (material.has_pbr_metallic_roughness) {
              const auto& pbr = material.pbr_metallic_roughness;
              if (pbr.base_color_texture.texture != nullptr) {
                preferred_uv_sets.push_back(
                  ResolveUvSet(pbr.base_color_texture));
              }
              if (pbr.metallic_roughness_texture.texture != nullptr) {
                preferred_uv_sets.push_back(
                  ResolveUvSet(pbr.metallic_roughness_texture));
              }
            }
            if (material.normal_texture.texture != nullptr) {
              preferred_uv_sets.push_back(
                ResolveUvSet(material.normal_texture));
            }
            if (material.occlusion_texture.texture != nullptr) {
              preferred_uv_sets.push_back(
                ResolveUvSet(material.occlusion_texture));
            }
            if (material.emissive_texture.texture != nullptr) {
              preferred_uv_sets.push_back(
                ResolveUvSet(material.emissive_texture));
            }
          }

          cgltf_size fallback_uv_set = (std::numeric_limits<cgltf_size>::max)();
          for (const auto& [uv_set, accessor] : texcoords_by_index) {
            (void)accessor;
            fallback_uv_set = (std::min)(fallback_uv_set, uv_set);
          }
          auto selected_uv_set = fallback_uv_set;
          if (texcoords_by_index.size() > 1U) {
            std::ostringstream message;
            message << "glTF primitive exposes multiple UV sets; ";
            message << "available={";
            bool first = true;
            for (const auto& [uv_set, accessor] : texcoords_by_index) {
              (void)accessor;
              if (!first) {
                message << ",";
              }
              first = false;
              message << uv_set;
            }
            message << "}";
            result.diagnostics.push_back(MakeWarningDiagnostic("mesh.uv_sets",
              message.str(), input.source_id_prefix, mesh_name));
          }
          if (!preferred_uv_sets.empty()) {
            const auto first_uv = preferred_uv_sets.front();
            bool mixed = false;
            for (const auto uv_set : preferred_uv_sets) {
              if (uv_set != first_uv) {
                mixed = true;
                break;
              }
            }
            if (mixed) {
              result.diagnostics.push_back(
                MakeWarningDiagnostic("mesh.uv_set_conflict",
                  "Material uses multiple UV sets; using the first available",
                  input.source_id_prefix, mesh_name));
            }

            const auto preferred_uv = preferred_uv_sets.front();
            if (const auto it = texcoords_by_index.find(preferred_uv);
              it != texcoords_by_index.end()) {
              selected_uv_set = preferred_uv;
            } else {
              result.diagnostics.push_back(
                MakeWarningDiagnostic("mesh.uv_set_missing",
                  "Material requests a UV set not present on the primitive; "
                  "using the first available",
                  input.source_id_prefix, mesh_name));
            }
          }

          if (texcoords_by_index.size() > 1U) {
            std::ostringstream message;
            message << "Selected UV set " << selected_uv_set;
            result.diagnostics.push_back(
              MakeWarningDiagnostic("mesh.uv_set_selected", message.str(),
                input.source_id_prefix, mesh_name));
          }

          if (const auto it = texcoords_by_index.find(selected_uv_set);
            it != texcoords_by_index.end()) {
            texcoords = it->second;
          }
        }

        if (!joints_by_index.empty() && !weights_by_index.empty()) {
          const auto& pick = joints_by_index;
          uint32_t selected_index = std::numeric_limits<uint32_t>::max();
          for (const auto& [index, accessor] : pick) {
            if (weights_by_index.contains(index) && index < selected_index) {
              selected_index = index;
              joints = accessor;
              weights = weights_by_index.at(index);
            }
          }
        }

        if (positions == nullptr) {
          result.diagnostics.push_back(
            MakeErrorDiagnostic("mesh.missing_positions",
              "glTF primitive missing POSITION attribute",
              input.source_id_prefix, mesh_name));
          result.success = false;
          continue;
        }

        const auto vertex_count = static_cast<uint32_t>(positions->count);
        if (vertex_count == 0) {
          result.diagnostics.push_back(
            MakeErrorDiagnostic("mesh.missing_positions",
              "glTF primitive contains no vertex positions",
              input.source_id_prefix, mesh_name));
          result.success = false;
          continue;
        }

        const bool has_normals
          = normals != nullptr && normals->count == positions->count;
        const bool has_texcoords
          = texcoords != nullptr && texcoords->count == positions->count;
        const bool has_tangents
          = tangents != nullptr && tangents->count == positions->count;
        const bool has_colors
          = colors != nullptr && colors->count == positions->count;
        const bool has_joints
          = joints != nullptr && joints->count == positions->count;
        const bool has_weights
          = weights != nullptr && weights->count == positions->count;
        const bool has_skin = has_joints && has_weights;

        all_normals = all_normals && has_normals;
        all_texcoords = all_texcoords && has_texcoords;
        all_tangents = all_tangents && has_tangents;
        any_texcoords = any_texcoords || has_texcoords;
        any_colors = any_colors || has_colors;
        any_skin = any_skin || has_skin;

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
          result.diagnostics.push_back(
            MakeWarningDiagnostic("mesh.material_slot_oob",
              "glTF material slot exceeds imported material key count",
              input.source_id_prefix, mesh_name));
        }

        if (prim.material != nullptr && HasMaterialTextures(prim.material)) {
          has_material_textures = true;
        }

        uint32_t index_count = 0;
        if (prim.indices != nullptr) {
          index_count = static_cast<uint32_t>(prim.indices->count);
        } else {
          index_count = vertex_count;
        }

        if ((index_count % 3U) != 0U) {
          result.diagnostics.push_back(MakeErrorDiagnostic("mesh.invalid_range",
            "glTF primitive index count must be a multiple of 3",
            input.source_id_prefix, mesh_name));
          result.success = false;
          return result;
        }

        primitives.push_back(PrimitiveInfo {
          .prim = &prim,
          .positions = positions,
          .normals = normals,
          .texcoords = texcoords,
          .tangents = tangents,
          .colors = colors,
          .joints = joints,
          .weights = weights,
          .material_slot = material_slot,
          .vertex_count = vertex_count,
          .index_count = index_count,
          .has_normals = has_normals,
          .has_texcoords = has_texcoords,
          .has_tangents = has_tangents,
          .has_colors = has_colors,
          .has_skin = has_skin,
        });

        total_vertices += vertex_count;
        total_indices += index_count;
      }

      if (primitives.empty()) {
        result.diagnostics.push_back(
          MakeWarningDiagnostic("mesh.empty_primitives",
            "glTF mesh has no supported primitives; skipping",
            input.source_id_prefix, mesh_name));
        continue;
      }

      if (any_texcoords && !all_texcoords) {
        result.diagnostics.push_back(MakeWarningDiagnostic("mesh.missing_uvs",
          "glTF mesh has mixed UV availability across primitives; some "
          "submeshes will use default UVs",
          input.source_id_prefix, mesh_name));
      }

      GeometryPipeline::WorkItem item;
      item.source_id
        = BuildSourceId(input.source_id_prefix, storage_name, mesh_ordinal++);
      item.mesh_name = storage_name;
      item.storage_mesh_name
        = util::NamespaceImportedAssetName(input.request, storage_name);
      item.source_key = mesh;
      item.material_keys.assign(
        input.material_keys.begin(), input.material_keys.end());
      item.default_material_key = input.default_material_key;
      item.want_textures = true;
      item.has_material_textures = has_material_textures;
      item.request = input.request;
      if (!all_normals
        && item.request.options.normal_policy
          == GeometryAttributePolicy::kPreserveIfPresent) {
        item.request.options.normal_policy
          = GeometryAttributePolicy::kGenerateMissing;
      }
      if (!all_tangents
        && item.request.options.tangent_policy
          == GeometryAttributePolicy::kPreserveIfPresent) {
        item.request.options.tangent_policy
          = GeometryAttributePolicy::kGenerateMissing;
      }

      const bool keep_normals = all_normals
        && item.request.options.normal_policy != GeometryAttributePolicy::kNone;
      const bool keep_texcoords = any_texcoords;
      const bool keep_tangents = all_tangents && keep_normals && keep_texcoords
        && item.request.options.tangent_policy
          != GeometryAttributePolicy::kNone;
      const bool keep_colors = any_colors;
      const bool keep_skin = any_skin;
      item.stop_token = input.stop_token;

      struct MeshBuffers final {
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

      auto owner = std::make_shared<MeshBuffers>();
      owner->positions.reserve(total_vertices);
      if (keep_normals) {
        owner->normals.reserve(total_vertices);
      }
      if (keep_texcoords) {
        owner->texcoords.reserve(total_vertices);
      }
      if (keep_tangents) {
        owner->tangents.reserve(total_vertices);
        owner->bitangents.reserve(total_vertices);
      }
      if (keep_colors) {
        owner->colors.reserve(total_vertices);
      }
      if (keep_skin) {
        owner->joint_indices.reserve(total_vertices);
        owner->joint_weights.reserve(total_vertices);
      }
      owner->indices.reserve(total_indices);
      owner->ranges.reserve(primitives.size());

      for (const auto& prim_info : primitives) {
        auto positions_vec = ReadVec3(prim_info.positions);
        if (positions_vec.empty()) {
          result.diagnostics.push_back(MakeErrorDiagnostic(
            "mesh.missing_positions", "glTF primitive contains no positions",
            input.source_id_prefix, mesh_name));
          result.success = false;
          continue;
        }

        for (auto& position : positions_vec) {
          position
            = ConvertGltfPosition(position, input.request.options.coordinate);
        }

        const auto base_vertex = static_cast<uint32_t>(owner->positions.size());
        owner->positions.insert(
          owner->positions.end(), positions_vec.begin(), positions_vec.end());

        if (keep_normals) {
          if (prim_info.has_normals) {
            auto normals_vec = ReadVec3(prim_info.normals);
            for (auto& normal : normals_vec) {
              normal = ConvertGltfDirection(normal);
            }
            owner->normals.insert(
              owner->normals.end(), normals_vec.begin(), normals_vec.end());
          } else {
            owner->normals.insert(
              owner->normals.end(), positions_vec.size(), glm::vec3(0.0F));
          }
        }

        if (keep_texcoords) {
          if (prim_info.has_texcoords) {
            auto texcoords_vec = ReadVec2(prim_info.texcoords);
            if (!texcoords_vec.empty()) {
              bool has_invalid_uv = false;
              auto min_uv = glm::vec2 { (std::numeric_limits<float>::max)() };
              auto max_uv
                = glm::vec2 { (std::numeric_limits<float>::lowest)() };
              for (const auto& uv : texcoords_vec) {
                if (!std::isfinite(uv.x) || !std::isfinite(uv.y)) {
                  has_invalid_uv = true;
                  break;
                }
                min_uv = glm::min(min_uv, uv);
                max_uv = glm::max(max_uv, uv);
              }
              if (has_invalid_uv) {
                result.diagnostics.push_back(MakeWarningDiagnostic(
                  "mesh.invalid_uvs",
                  "glTF primitive has NaN/Inf UVs; rendering may be corrupted",
                  input.source_id_prefix, mesh_name));
              } else {
                DLOG_F(INFO,
                  "glTF mesh '{}' UV range min=({:.4f},{:.4f}) "
                  "max=({:.4f},{:.4f})",
                  mesh_name, min_uv.x, min_uv.y, max_uv.x, max_uv.y);
                constexpr float kUvAbsLimit = 10000.0F;
                const auto max_abs
                  = glm::max(glm::abs(min_uv), glm::abs(max_uv));
                if (max_abs.x > kUvAbsLimit || max_abs.y > kUvAbsLimit) {
                  std::ostringstream message;
                  message.setf(std::ios::fixed);
                  message << "glTF primitive UV range is extremely large; "
                             "textures may appear noisy ("
                          << "min=" << std::setprecision(3) << min_uv.x << ","
                          << min_uv.y << " max=" << max_uv.x << "," << max_uv.y
                          << ")";
                  result.diagnostics.push_back(
                    MakeWarningDiagnostic("mesh.uv_range_suspicious",
                      message.str(), input.source_id_prefix, mesh_name));
                }
              }
            }
            owner->texcoords.insert(owner->texcoords.end(),
              texcoords_vec.begin(), texcoords_vec.end());
          } else {
            owner->texcoords.insert(
              owner->texcoords.end(), positions_vec.size(), glm::vec2(0.0F));
          }
        }

        if (keep_colors) {
          if (prim_info.has_colors) {
            auto colors_vec = ReadVec4(prim_info.colors);
            owner->colors.insert(
              owner->colors.end(), colors_vec.begin(), colors_vec.end());
          } else {
            owner->colors.insert(
              owner->colors.end(), positions_vec.size(), glm::vec4(1.0F));
          }
        }

        if (keep_skin) {
          if (prim_info.has_skin) {
            auto joint_indices_vec = ReadUVec4(prim_info.joints);
            auto joint_weights_vec = ReadVec4(prim_info.weights);
            owner->joint_indices.insert(owner->joint_indices.end(),
              joint_indices_vec.begin(), joint_indices_vec.end());
            owner->joint_weights.insert(owner->joint_weights.end(),
              joint_weights_vec.begin(), joint_weights_vec.end());
          } else {
            owner->joint_indices.insert(
              owner->joint_indices.end(), positions_vec.size(), glm::uvec4(0u));
            owner->joint_weights.insert(owner->joint_weights.end(),
              positions_vec.size(), glm::vec4(0.0F));
          }
        }

        if (keep_tangents) {
          if (prim_info.has_tangents) {
            auto tangents_vec = ReadVec4(prim_info.tangents);
            const auto normal_offset
              = owner->normals.size() - positions_vec.size();
            for (size_t i = 0; i < tangents_vec.size(); ++i) {
              const auto& t = tangents_vec[i];
              const auto& n = owner->normals[normal_offset + i];
              const auto tangent
                = ConvertGltfDirection(glm::vec3 { t.x, t.y, t.z });
              owner->tangents.push_back(tangent);
              const auto bitangent = glm::cross(n, tangent) * t.w;
              owner->bitangents.push_back(bitangent);
            }
          } else {
            owner->tangents.insert(
              owner->tangents.end(), positions_vec.size(), glm::vec3(0.0F));
            owner->bitangents.insert(
              owner->bitangents.end(), positions_vec.size(), glm::vec3(0.0F));
          }
        }

        auto indices_vec = ReadIndices(prim_info.prim->indices);
        if (indices_vec.empty()) {
          result.diagnostics.push_back(
            MakeWarningDiagnostic("gltf.missing_indices",
              "glTF primitive missing indices; generated sequential indices",
              input.source_id_prefix, mesh_name));
          indices_vec.resize(positions_vec.size());
          for (size_t i = 0; i < indices_vec.size(); ++i) {
            indices_vec[i] = static_cast<uint32_t>(i);
          }
        }

        if ((indices_vec.size() % 3U) != 0U) {
          result.diagnostics.push_back(MakeErrorDiagnostic("mesh.invalid_range",
            "glTF primitive index count must be a multiple of 3",
            input.source_id_prefix, mesh_name));
          result.success = false;
          return result;
        }

        if (indices_vec.empty()) {
          result.diagnostics.push_back(MakeErrorDiagnostic(
            "mesh.missing_indices", "glTF primitive contains no indices",
            input.source_id_prefix, mesh_name));
          result.success = false;
          continue;
        }

        const auto vertex_count_u32
          = static_cast<uint32_t>(positions_vec.size());
        uint32_t max_index = 0;
        for (const auto idx : indices_vec) {
          max_index = (std::max)(max_index, idx);
        }
        if (vertex_count_u32 == 0 || max_index >= vertex_count_u32) {
          result.diagnostics.push_back(
            MakeErrorDiagnostic("mesh.invalid_indices",
              "glTF primitive index buffer references out-of-range vertices",
              input.source_id_prefix, mesh_name));
          result.success = false;
          continue;
        }

        const auto first_index = static_cast<uint32_t>(owner->indices.size());
        for (auto& index : indices_vec) {
          index += base_vertex;
        }
        owner->indices.insert(
          owner->indices.end(), indices_vec.begin(), indices_vec.end());

        owner->ranges.push_back(TriangleRange {
          .material_slot = prim_info.material_slot,
          .first_index = first_index,
          .index_count = static_cast<uint32_t>(indices_vec.size()),
        });
      }

      if (owner->positions.empty() || owner->indices.empty()) {
        result.success = false;
        continue;
      }

      const bool is_skinned = keep_skin && !owner->joint_indices.empty()
        && !owner->joint_weights.empty();

      if (is_skinned) {
        const auto* skin = FindSkinForMesh(data, *mesh);
        if (skin == nullptr || skin->inverse_bind_matrices == nullptr) {
          result.diagnostics.push_back(
            MakeErrorDiagnostic("mesh.missing_inverse_bind",
              "glTF skinned mesh missing inverse bind matrices",
              input.source_id_prefix, mesh_name));
          result.success = false;
          continue;
        }

        owner->inverse_bind_matrices = ReadMat4(skin->inverse_bind_matrices);
        const uint32_t joint_count = DetermineJointCount(owner->joint_indices);
        if (joint_count == 0 || owner->inverse_bind_matrices.empty()) {
          result.diagnostics.push_back(
            MakeErrorDiagnostic("mesh.missing_inverse_bind",
              "glTF skinned mesh missing inverse bind matrices",
              input.source_id_prefix, mesh_name));
          result.success = false;
          continue;
        }
        if (owner->inverse_bind_matrices.size() < joint_count) {
          result.diagnostics.push_back(
            MakeErrorDiagnostic("mesh.skinning_buffers_mismatch",
              "glTF skin inverse bind count is smaller than joint count",
              input.source_id_prefix, mesh_name));
          result.success = false;
          continue;
        }

        owner->joint_remap.resize(joint_count);
        std::iota(owner->joint_remap.begin(), owner->joint_remap.end(), 0u);
      }

      std::optional<Bounds3> bounds3;

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
        .bounds = bounds3,
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

    if (!result.success && result.diagnostics.empty()) {
      DLOG_F(ERROR, "glTF import failed without diagnostics: source_id='{}'",
        input.source_id_prefix);
      result.diagnostics.push_back(MakeErrorDiagnostic("gltf.unknown_failure",
        "glTF import failed without diagnostics", input.source_id_prefix, ""));
    }
    return result;
  }

} // namespace

auto GltfAdapter::Parse(const std::filesystem::path& source_path,
  const AdapterInput& input) -> ParseResult
{
  ParseResult result;
  auto data = LoadDataFromFile(source_path, input, result.diagnostics);
  if (data == nullptr) {
    DLOG_F(ERROR, "glTF parse failed: path='{}' diagnostics={} ",
      source_path.string(), result.diagnostics.size());
    if (result.diagnostics.empty()) {
      result.diagnostics.push_back(MakeErrorDiagnostic("gltf.parse_failed",
        "glTF parse failed without diagnostics", input.source_id_prefix, ""));
    }
    impl_->data_owner.reset();
    result.success = false;
    return result;
  }

  impl_->data_owner
    = std::shared_ptr<const cgltf_data>(data.release(), &cgltf_free);
  return result;
}

auto GltfAdapter::Parse(const std::span<const std::byte> source_bytes,
  const AdapterInput& input) -> ParseResult
{
  ParseResult result;
  auto data = LoadDataFromMemory(source_bytes, input, result.diagnostics);
  if (data == nullptr) {
    DLOG_F(ERROR, "glTF parse failed (memory): diagnostics={}",
      result.diagnostics.size());
    if (result.diagnostics.empty()) {
      result.diagnostics.push_back(MakeErrorDiagnostic("gltf.parse_failed",
        "glTF parse failed without diagnostics", input.source_id_prefix, ""));
    }
    impl_->data_owner.reset();
    result.success = false;
    return result;
  }

  impl_->data_owner
    = std::shared_ptr<const cgltf_data>(data.release(), &cgltf_free);
  return result;
}

auto GltfAdapter::BuildWorkItems(GeometryWorkTag, GeometryWorkItemSink& sink,
  const AdapterInput& input) -> WorkItemStreamResult
{
  if (!impl_->data_owner) {
    WorkItemStreamResult result;
    result.success = false;
    result.diagnostics.push_back(MakeErrorDiagnostic("gltf.scene.not_parsed",
      "glTF adapter has no parsed scene", input.source_id_prefix,
      input.object_path_prefix));
    return result;
  }

  return StreamWorkItemsFromData(*impl_->data_owner, input, sink);
}

auto GltfAdapter::BuildWorkItems(MaterialWorkTag, MaterialWorkItemSink& sink,
  const AdapterInput& input) -> WorkItemStreamResult
{
  if (!impl_->data_owner) {
    WorkItemStreamResult result;
    result.success = false;
    result.diagnostics.push_back(MakeErrorDiagnostic("gltf.scene.not_parsed",
      "glTF adapter has no parsed scene", input.source_id_prefix,
      input.object_path_prefix));
    return result;
  }

  WorkItemStreamResult result;
  if (input.stop_token.stop_requested()) {
    result.success = false;
    result.diagnostics.push_back(MakeCancelDiagnostic(input.source_id_prefix));
    return result;
  }

  const auto& data = *impl_->data_owner;
  const uint32_t material_count = static_cast<uint32_t>(data.materials_count);
  for (uint32_t i = 0; i < material_count; ++i) {
    const auto& material = data.materials[i];
    const std::string authored = material.name ? material.name : "";
    const auto material_name
      = util::BuildMaterialName(authored, input.request, i);

    MaterialPipeline::WorkItem item;
    item.source_id = BuildSourceId(input.source_id_prefix, material_name, i);
    item.material_name = material_name;
    item.storage_material_name
      = util::NamespaceImportedAssetName(input.request, material_name);
    item.source_key = &material;
    item.material_domain = data::MaterialDomain::kOpaque;
    item.alpha_mode = MaterialAlphaMode::kOpaque;

    if (material.alpha_mode == cgltf_alpha_mode_mask) {
      item.alpha_mode = MaterialAlphaMode::kMasked;
      item.material_domain = data::MaterialDomain::kMasked;
    } else if (material.alpha_mode == cgltf_alpha_mode_blend) {
      item.alpha_mode = MaterialAlphaMode::kBlended;
      item.material_domain = data::MaterialDomain::kAlphaBlended;
    }

    item.inputs.alpha_cutoff = static_cast<float>(material.alpha_cutoff);
    item.inputs.double_sided = true;
    item.inputs.unlit = material.unlit != 0;

    if (material.has_pbr_metallic_roughness) {
      const auto& pbr = material.pbr_metallic_roughness;
      item.inputs.base_color[0] = static_cast<float>(pbr.base_color_factor[0]);
      item.inputs.base_color[1] = static_cast<float>(pbr.base_color_factor[1]);
      item.inputs.base_color[2] = static_cast<float>(pbr.base_color_factor[2]);
      item.inputs.base_color[3] = static_cast<float>(pbr.base_color_factor[3]);
      item.inputs.metalness = static_cast<float>(pbr.metallic_factor);
      item.inputs.roughness = static_cast<float>(pbr.roughness_factor);
    }

    item.inputs.emissive_factor[0]
      = static_cast<float>(material.emissive_factor[0]);
    item.inputs.emissive_factor[1]
      = static_cast<float>(material.emissive_factor[1]);
    item.inputs.emissive_factor[2]
      = static_cast<float>(material.emissive_factor[2]);

    if (material.normal_texture.texture != nullptr) {
      item.inputs.normal_scale
        = static_cast<float>(material.normal_texture.scale);
      ApplyTextureBinding(item.textures.normal, material.normal_texture,
        BuildTextureSourceId(input.source_id_prefix, data,
          *material.normal_texture.texture, TextureUsage::kNormal));
    }

    if (material.occlusion_texture.texture != nullptr) {
      item.inputs.ambient_occlusion
        = static_cast<float>(material.occlusion_texture.scale);
      ApplyTextureBinding(item.textures.ambient_occlusion,
        material.occlusion_texture,
        BuildTextureSourceId(input.source_id_prefix, data,
          *material.occlusion_texture.texture, TextureUsage::kOcclusion));
    }

    if (material.emissive_texture.texture != nullptr) {
      ApplyTextureBinding(item.textures.emissive, material.emissive_texture,
        BuildTextureSourceId(input.source_id_prefix, data,
          *material.emissive_texture.texture, TextureUsage::kEmissive));
    }

    if (material.has_pbr_metallic_roughness) {
      const auto& pbr = material.pbr_metallic_roughness;
      if (pbr.base_color_texture.texture != nullptr) {
        ApplyTextureBinding(item.textures.base_color, pbr.base_color_texture,
          BuildTextureSourceId(input.source_id_prefix, data,
            *pbr.base_color_texture.texture, TextureUsage::kBaseColor));
      }

      if (pbr.metallic_roughness_texture.texture != nullptr) {
        const auto source_id = BuildTextureSourceId(input.source_id_prefix,
          data, *pbr.metallic_roughness_texture.texture,
          TextureUsage::kMetallicRoughness);
        ApplyTextureBinding(
          item.textures.metallic, pbr.metallic_roughness_texture, source_id);
        ApplyTextureBinding(
          item.textures.roughness, pbr.metallic_roughness_texture, source_id);
        item.orm_policy = OrmPolicy::kAuto;
      }
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

auto GltfAdapter::BuildWorkItems(TextureWorkTag, TextureWorkItemSink& sink,
  const AdapterInput& input) -> WorkItemStreamResult
{
  if (!impl_->data_owner) {
    WorkItemStreamResult result;
    result.success = false;
    result.diagnostics.push_back(MakeErrorDiagnostic("gltf.scene.not_parsed",
      "glTF adapter has no parsed scene", input.source_id_prefix,
      input.object_path_prefix));
    return result;
  }

  WorkItemStreamResult result;
  if (input.stop_token.stop_requested()) {
    result.success = false;
    result.diagnostics.push_back(MakeCancelDiagnostic(input.source_id_prefix));
    return result;
  }

  const auto& data = *impl_->data_owner;
  const auto base_dir = input.request.source_path.parent_path();

  std::unordered_map<std::string, TexturePipeline::WorkItem> work_items;

  auto register_texture = [&](const cgltf_texture_view& view,
                            const TextureUsage usage) {
    if (view.texture == nullptr) {
      return;
    }

    const auto source_id = BuildTextureSourceId(
      input.source_id_prefix, data, *view.texture, usage);
    if (work_items.find(source_id) != work_items.end()) {
      return;
    }

    const auto* image = view.texture->image;
    if (image == nullptr) {
      result.diagnostics.push_back(MakeWarningDiagnostic(
        "gltf.image.missing", "glTF texture has no image", source_id, ""));
      DLOG_F(
        INFO, "glTF texture register: source_id='{}' missing image", source_id);
      auto bytes = std::make_shared<std::vector<std::byte>>();
      TexturePipeline::WorkItem item {};
      item.source_id = source_id;
      item.texture_id = source_id;
      item.source_key = view.texture;
      item.desc = MakeDescFromPreset(PresetForUsage(usage));
      item.desc.source_id = source_id;
      item.desc.stop_token = input.stop_token;
      item.packing_policy_id = "d3d12";
      item.output_format_is_override = false;
      item.failure_policy
        = input.request.options.texture_tuning.placeholder_on_failure
        ? TexturePipeline::FailurePolicy::kPlaceholder
        : TexturePipeline::FailurePolicy::kStrict;
      item.source = TexturePipeline::SourceBytes {
        .bytes = std::span<const std::byte>(bytes->data(), bytes->size()),
        .owner = std::static_pointer_cast<const void>(bytes),
      };
      item.stop_token = input.stop_token;
      work_items.emplace(source_id, std::move(item));
      return;
    }

    auto source_bytes = ResolveImageBytes(
      *image, base_dir, impl_->data_owner, result.diagnostics, source_id);
    if (!source_bytes.has_value()) {
      DLOG_F(INFO, "glTF texture register: source_id='{}' no bytes", source_id);
      return;
    }

    DLOG_F(INFO, "glTF texture register: source_id='{}' bytes={} usage={}",
      source_id, source_bytes->bytes.size(), UsageLabel(usage));

    auto desc = MakeDescFromPreset(PresetForUsage(usage));
    desc.source_id = source_id;
    desc.stop_token = input.stop_token;

    TexturePipeline::WorkItem item {};
    item.source_id = source_id;
    item.texture_id = source_id;
    item.source_key = view.texture;
    item.desc = std::move(desc);
    item.packing_policy_id = "d3d12";
    item.output_format_is_override = false;
    item.failure_policy
      = input.request.options.texture_tuning.placeholder_on_failure
      ? TexturePipeline::FailurePolicy::kPlaceholder
      : TexturePipeline::FailurePolicy::kStrict;
    item.source = *source_bytes;
    item.stop_token = input.stop_token;

    work_items.emplace(source_id, std::move(item));
  };

  for (cgltf_size i = 0; i < data.materials_count; ++i) {
    const auto& material = data.materials[i];
    if (material.has_pbr_metallic_roughness) {
      const auto& pbr = material.pbr_metallic_roughness;
      register_texture(pbr.base_color_texture, TextureUsage::kBaseColor);
      register_texture(
        pbr.metallic_roughness_texture, TextureUsage::kMetallicRoughness);
    }
    register_texture(material.normal_texture, TextureUsage::kNormal);
    register_texture(material.occlusion_texture, TextureUsage::kOcclusion);
    register_texture(material.emissive_texture, TextureUsage::kEmissive);
  }

  for (auto& [_, item] : work_items) {
    if (!sink.Consume(std::move(item))) {
      return result;
    }
    ++result.emitted;
  }

  return result;
}

auto GltfAdapter::BuildSceneStage(const SceneStageInput& input,
  std::vector<ImportDiagnostic>& diagnostics) const -> SceneStageResult
{
  SceneStageResult result;
  if (input.stop_token.stop_requested()) {
    diagnostics.push_back(MakeCancelDiagnostic(input.source_id));
    return result;
  }

  if (!impl_->data_owner) {
    diagnostics.push_back(MakeErrorDiagnostic("gltf.scene.not_parsed",
      "glTF adapter has no parsed scene", input.source_id, {}));
    return result;
  }

  if (input.request == nullptr) {
    diagnostics.push_back(MakeErrorDiagnostic("scene.request_missing",
      "Scene stage input is missing request data", input.source_id, {}));
    return result;
  }

  const auto& data = *impl_->data_owner;
  const auto& request = *input.request;

  std::unordered_map<const cgltf_mesh*, size_t> mesh_base_index;
  mesh_base_index.reserve(data.meshes_count);
  size_t geometry_cursor = 0;

  for (cgltf_size mesh_i = 0; mesh_i < data.meshes_count; ++mesh_i) {
    const auto* mesh = &data.meshes[mesh_i];
    if (mesh == nullptr) {
      continue;
    }

    if (mesh->primitives_count == 0) {
      continue;
    }
    mesh_base_index.emplace(mesh, geometry_cursor);
    ++geometry_cursor;
  }

  if (!input.geometry_keys.empty()
    && input.geometry_keys.size() < geometry_cursor) {
    diagnostics.push_back(MakeErrorDiagnostic("scene.geometry_key_missing",
      "Geometry key count does not match mesh count", input.source_id, {}));
  }

  std::vector<NodeInput> nodes;
  nodes.reserve(data.nodes_count > 0 ? data.nodes_count : 1u);

  const auto kInvalidParent = std::numeric_limits<uint32_t>::max();
  const auto apply_node
    = [&](const cgltf_node* node, uint32_t parent_index,
        std::string_view parent_name, uint32_t& ordinal,
        const glm::mat4& parent_world, auto&& self) -> void {
    if (node == nullptr || input.stop_token.stop_requested()) {
      return;
    }

    const auto authored = node->name != nullptr ? std::string_view(node->name)
                                                : std::string_view {};
    const auto base_name
      = util::BuildSceneNodeName(authored, request, ordinal, parent_name);

    cgltf_float local_matrix_data[16] = {};
    cgltf_node_transform_local(node, local_matrix_data);
    glm::mat4 local_matrix(1.0F);
    for (int c = 0; c < 4; ++c) {
      for (int r = 0; r < 4; ++r) {
        local_matrix[c][r] = static_cast<float>(local_matrix_data[c * 4 + r]);
      }
    }

    local_matrix
      = ConvertGltfTransform(local_matrix, request.options.coordinate);
    const auto world_matrix = parent_world * local_matrix;

    NodeInput node_input;
    node_input.authored_name = std::string(authored);
    node_input.base_name = base_name;
    node_input.local_matrix = local_matrix;
    node_input.world_matrix = world_matrix;
    node_input.visible = true;
    node_input.has_camera = node->camera != nullptr;
    node_input.has_light = node->light != nullptr;
    node_input.has_renderable = node->mesh != nullptr;
    node_input.source_node = node;

    const auto index = static_cast<uint32_t>(nodes.size());
    if (parent_index == kInvalidParent) {
      node_input.parent_index = index;
    } else if (index == 0) {
      node_input.parent_index = 0;
    } else {
      node_input.parent_index = parent_index;
    }

    nodes.push_back(std::move(node_input));
    const auto current_name = nodes.back().base_name;

    ++ordinal;

    for (cgltf_size i = 0; i < node->children_count; ++i) {
      self(node->children[i], index, current_name, ordinal, world_matrix, self);
    }
  };

  uint32_t ordinal = 0;
  if (data.scenes_count > 0) {
    const auto* scene = data.scene != nullptr ? data.scene : &data.scenes[0];
    for (cgltf_size i = 0; i < scene->nodes_count; ++i) {
      apply_node(scene->nodes[i], kInvalidParent, {}, ordinal,
        glm::mat4 { 1.0F }, apply_node);
    }
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

    const auto matrix_translation = glm::vec3(node.local_matrix[3]);
    const auto translation_delta
      = glm::length(translation - matrix_translation);
    if (translation_delta > 1e-3F) {
      LOG_F(WARNING,
        "SceneImport: node '{}' translation mismatch (decompose vs matrix) "
        "decomposed=({:.6f},{:.6f},{:.6f}) matrix=({:.6f},{:.6f},{:.6f})",
        name, translation.x, translation.y, translation.z, matrix_translation.x,
        matrix_translation.y, matrix_translation.z);
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

    const auto* gltf_node = static_cast<const cgltf_node*>(node.source_node);
    if (gltf_node != nullptr && gltf_node->mesh != nullptr) {
      const auto it = mesh_base_index.find(gltf_node->mesh);
      if (it != mesh_base_index.end()) {
        const auto key_index = it->second;
        if (key_index < input.geometry_keys.size()) {
          build.renderables.push_back(RenderableRecord {
            .node_index = i,
            .geometry_key = input.geometry_keys[key_index],
            .visible = 1,
            .reserved = {},
          });
        }
      }
    }

    if (gltf_node != nullptr && gltf_node->camera != nullptr) {
      const auto& cam = *gltf_node->camera;
      if (cam.type == cgltf_camera_type_perspective) {
        const auto& perspective = cam.data.perspective;
        const float fov_y = static_cast<float>(perspective.yfov);
        const float aspect_ratio = perspective.has_aspect_ratio
          ? static_cast<float>(perspective.aspect_ratio)
          : 1.0F;
        const float near_plane = static_cast<float>(perspective.znear);
        const float far_plane = perspective.has_zfar
          ? static_cast<float>(perspective.zfar)
          : near_plane + 1000.0F;

        build.perspective_cameras.push_back(PerspectiveCameraRecord {
          .node_index = i,
          .fov_y = fov_y,
          .aspect_ratio = aspect_ratio,
          .near_plane = near_plane,
          .far_plane = far_plane,
          .reserved = {},
        });
      } else if (cam.type == cgltf_camera_type_orthographic) {
        const auto& ortho = cam.data.orthographic;
        const float half_w = static_cast<float>(ortho.xmag) * 0.5F;
        const float half_h = static_cast<float>(ortho.ymag) * 0.5F;
        const float near_plane = static_cast<float>(ortho.znear);
        const float far_plane = static_cast<float>(ortho.zfar);

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

    if (gltf_node != nullptr && gltf_node->light != nullptr) {
      const auto& light = *gltf_node->light;
      switch (light.type) {
      case cgltf_light_type_directional: {
        DirectionalLightRecord rec_light {};
        rec_light.node_index = i;
        rec_light.common.affects_world = 1U;
        rec_light.common.color_rgb[0]
          = (std::max)(0.0F, util::ToFloat(light.color[0]));
        rec_light.common.color_rgb[1]
          = (std::max)(0.0F, util::ToFloat(light.color[1]));
        rec_light.common.color_rgb[2]
          = (std::max)(0.0F, util::ToFloat(light.color[2]));
        rec_light.common.intensity
          = (std::max)(0.0F, util::ToFloat(light.intensity));
        rec_light.common.casts_shadows = 1U;
        build.directional_lights.push_back(rec_light);
        break;
      }
      case cgltf_light_type_point: {
        PointLightRecord rec_light {};
        rec_light.node_index = i;
        rec_light.common.affects_world = 1U;
        rec_light.common.color_rgb[0]
          = (std::max)(0.0F, util::ToFloat(light.color[0]));
        rec_light.common.color_rgb[1]
          = (std::max)(0.0F, util::ToFloat(light.color[1]));
        rec_light.common.color_rgb[2]
          = (std::max)(0.0F, util::ToFloat(light.color[2]));
        rec_light.common.intensity
          = (std::max)(0.0F, util::ToFloat(light.intensity));
        rec_light.common.casts_shadows = 1U;
        build.point_lights.push_back(rec_light);
        break;
      }
      case cgltf_light_type_spot: {
        SpotLightRecord rec_light {};
        rec_light.node_index = i;
        rec_light.common.affects_world = 1U;
        rec_light.common.color_rgb[0]
          = (std::max)(0.0F, util::ToFloat(light.color[0]));
        rec_light.common.color_rgb[1]
          = (std::max)(0.0F, util::ToFloat(light.color[1]));
        rec_light.common.color_rgb[2]
          = (std::max)(0.0F, util::ToFloat(light.color[2]));
        rec_light.common.intensity
          = (std::max)(0.0F, util::ToFloat(light.intensity));
        rec_light.common.casts_shadows = 1U;
        rec_light.inner_cone_angle_radians
          = (std::max)(0.0F, util::ToFloat(light.spot_inner_cone_angle));
        rec_light.outer_cone_angle_radians
          = (std::max)(rec_light.inner_cone_angle_radians,
            util::ToFloat(light.spot_outer_cone_angle));
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

auto GltfAdapter::BuildWorkItems(SceneWorkTag, SceneWorkItemSink& sink,
  const AdapterInput& input) -> WorkItemStreamResult
{
  if (!impl_->data_owner) {
    WorkItemStreamResult result;
    result.success = false;
    result.diagnostics.push_back(MakeErrorDiagnostic("gltf.scene.not_parsed",
      "glTF adapter has no parsed scene", input.source_id_prefix,
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
