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
#include <ios>
#include <limits>
#include <map>
#include <memory>
#include <new>
#include <numbers>
#include <numeric>
#include <optional>
#include <span>
#include <sstream>
#include <string>
#include <string_view>
#include <system_error>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

#include <glm/common.hpp>
#include <glm/ext/matrix_float4x4.hpp>
#include <glm/ext/quaternion_float.hpp>
#include <glm/ext/vector_float2.hpp>
#include <glm/ext/vector_float3.hpp>
#include <glm/ext/vector_float4.hpp>
#include <glm/ext/vector_uint4.hpp>
#include <glm/geometric.hpp>
#include <glm/gtc/matrix_access.hpp>
#include <glm/gtc/quaternion.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <glm/matrix.hpp>
#include <nlohmann/json.hpp>
#include <nlohmann/json_fwd.hpp>

#include <Oxygen/Base/Filesystem.h>
#include <Oxygen/Base/Logging.h>
#include <Oxygen/Base/Sha256.h>
#include <Oxygen/Base/Span.h>
#include <Oxygen/Cooker/Import/IAsyncFileReader.h>
#include <Oxygen/Cooker/Import/ImportDiagnostics.h>
#include <Oxygen/Cooker/Import/ImportOptions.h>
#include <Oxygen/Cooker/Import/ImportRequest.h>
#include <Oxygen/Cooker/Import/Internal/AdapterTypes.h>
#include <Oxygen/Cooker/Import/Internal/DroppedSourceFeatures.h>
#include <Oxygen/Cooker/Import/Internal/ImportSourceSnapshot.h>
#include <Oxygen/Cooker/Import/Internal/ImportedLightSemantics.h>
#include <Oxygen/Cooker/Import/Internal/ImportedPropertyCommit.h>
#include <Oxygen/Cooker/Import/Internal/MaterialSource.h>
#include <Oxygen/Cooker/Import/Internal/MeshTransformBake.h>
#include <Oxygen/Cooker/Import/Internal/ModelGeometrySource.h>
#include <Oxygen/Cooker/Import/Internal/ModelMaterialSource.h>
#include <Oxygen/Cooker/Import/Internal/ModelNodeIdentity.h>
#include <Oxygen/Cooker/Import/Internal/ModelTextureSource.h>
#include <Oxygen/Cooker/Import/Internal/Pipelines/MaterialPipeline.h>
#include <Oxygen/Cooker/Import/Internal/Pipelines/MeshBuildPipeline.h>
#include <Oxygen/Cooker/Import/Internal/Pipelines/ScenePipeline.h>
#include <Oxygen/Cooker/Import/Internal/Pipelines/TexturePipeline.h>
#include <Oxygen/Cooker/Import/Internal/SceneBuild.h>
#include <Oxygen/Cooker/Import/Internal/SceneNodeImportDefaults.h>
#include <Oxygen/Cooker/Import/Internal/SourceLayoutHash.h>
#include <Oxygen/Cooker/Import/Internal/gltf/GltfAdapter.h>
#include <Oxygen/Cooker/Import/Internal/gltf/cgltf.h>
#include <Oxygen/Cooker/Import/Naming.h>
#include <Oxygen/Cooker/Import/SceneSourceInspection.h>
#include <Oxygen/Cooker/Import/TextureImportPresets.h>
#include <Oxygen/Cooker/Import/TextureImportTypes.h>
#include <Oxygen/Core/Lighting/LightPhotometry.h>
#include <Oxygen/Core/Transforms/Decompose.h>
#include <Oxygen/Core/Types/AssetKey.h>
#include <Oxygen/Core/Types/CameraAspectMode.h>
#include <Oxygen/Core/Types/Format.h>
#include <Oxygen/Data/LightValidation.h>
#include <Oxygen/Data/MaterialDomain.h>
#include <Oxygen/Data/MeshType.h>
#include <Oxygen/Data/PakFormat.h>
#include <Oxygen/Data/SceneRecordDefaults.h>
#include <Oxygen/Data/Schema/Generated.SceneComponents.h>

namespace oxygen::content::import::adapters {

struct GltfAdapter::Impl final {
  std::shared_ptr<detail::ImportSourceSnapshot> source_snapshot;
  std::shared_ptr<const cgltf_data> data_owner;
  //! The bake-plan variant of each geometry work item, in emission order; the
  //! scene stage's geometry keys follow this order.
  std::vector<size_t> emitted_variants;
};

GltfAdapter::GltfAdapter()
  : impl_(std::make_unique<Impl>())
{
}

GltfAdapter::~GltfAdapter() = default;

namespace {

  using data::pak::world::DirectionalLightRecord;
  using data::pak::world::MakeDefaultRecord;
  using data::pak::world::NodeRecord;
  using data::pak::world::OrthographicCameraRecord;
  using data::pak::world::PerspectiveCameraRecord;
  using data::pak::world::PointLightRecord;
  using data::pak::world::RenderableRecord;
  using data::pak::world::SpotLightRecord;

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
      .code = "import.canceled",
      .message = "Import canceled",
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
    case cgltf_result_file_not_found:
      return "file not found";
    default:
      return "unknown error";
    }
  }

  //! The luminous flux of `intensity_cd` candela over `solid_angle_sr`;
  //! infinite when a float cannot hold it.
  [[nodiscard]] auto CandelaToLumens(
    const float intensity_cd, const double solid_angle_sr) noexcept -> float
  {
    const auto flux = static_cast<double>(intensity_cd) * solid_angle_sr;
    return std::abs(flux) <= std::numeric_limits<float>::max()
      ? static_cast<float>(flux)
      : std::numeric_limits<float>::infinity();
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

  [[nodiscard]] auto TryReadJsonBool(const nlohmann::json& doc,
    const std::string_view key) -> std::optional<bool>
  {
    const auto it = doc.find(std::string(key));
    if (it == doc.end() || !it->is_boolean()) {
      return std::nullopt;
    }
    return it->get<bool>();
  }

  [[nodiscard]] auto ReadImportedLightSemantics(const cgltf_extras& extras)
    -> detail::ImportedLightSemantics
  {
    if (extras.data == nullptr) {
      return {};
    }

    const auto extras_text = std::string_view(extras.data);
    if (extras_text.empty()) {
      return {};
    }

    const auto doc = nlohmann::json::parse(extras_text, nullptr, false);
    if (doc.is_discarded() || !doc.is_object()) {
      return {};
    }

    const auto oxygen_it
      = doc.find(std::string(detail::kImportedLightExtrasNamespace));
    if (oxygen_it == doc.end() || !oxygen_it->is_object()) {
      return {};
    }

    const auto& oxygen_doc = *oxygen_it;
    return detail::ImportedLightSemantics {
      .affects_world
      = TryReadJsonBool(oxygen_doc, detail::kImportedLightAffectsWorldKey),
      .casts_shadows
      = TryReadJsonBool(oxygen_doc, detail::kImportedLightCastsShadowsKey),
    };
  }

  using CgltfDataPtr = std::unique_ptr<cgltf_data, decltype(&cgltf_free)>;

  auto Utf8Path(const std::filesystem::path& path) -> std::string
  {
    const auto text = path.u8string();
    return { text.begin(), text.end() };
  }

  auto PathFromUtf8(const std::string_view text) -> std::filesystem::path
  {
    return { std::u8string(text.begin(), text.end()) };
  }

  // cgltf owns successful reads until file.release; failures retain local RAII.
  auto ReadCgltfFile(const cgltf_memory_options* /*unused*/,
    const cgltf_file_options* file_options, const char* path, cgltf_size* size,
    void** data) noexcept -> cgltf_result
  {
    if (path == nullptr || size == nullptr || data == nullptr) {
      return cgltf_result_invalid_options;
    }
    *data = nullptr;
    try {
      const auto logical = PathFromUtf8(path);
      auto observed
        = std::optional<detail::ImportSourceSnapshot::ParserRead> {};
      if (file_options != nullptr && file_options->user_data != nullptr) {
        auto* snapshot
          = static_cast<detail::ImportSourceSnapshot*>(file_options->user_data);
        observed.emplace(snapshot->BeginParserRead(logical));
      }
      const auto requested_maximum = *size;
      // NOLINTNEXTLINE(*-signed-bitwise) - std::ios openmode bitmask
      constexpr auto kOpenMode = std::ios::binary | std::ios::ate;
      std::ifstream file(
        base::ToNativePath(observed ? observed->PhysicalPath() : logical),
        kOpenMode);
      if (!file) {
        return cgltf_result_file_not_found;
      }
      const auto length = static_cast<std::streamoff>(file.tellg());
      if (length < 0 || !std::in_range<cgltf_size>(length)) {
        return cgltf_result_io_error;
      }
      const auto requested
        = *size == 0U ? static_cast<cgltf_size>(length) : *size;
      if (std::cmp_greater(requested, length)
        || !std::in_range<std::streamsize>(requested)) {
        return cgltf_result_io_error;
      }
      // cgltf frees this raw buffer through ReleaseCgltfFile.
      // NOLINTNEXTLINE(*-avoid-c-arrays)
      auto bytes = std::make_unique_for_overwrite<char[]>(requested);
      file.seekg(0);
      if (!file.read(bytes.get(), static_cast<std::streamsize>(requested))) {
        return cgltf_result_io_error;
      }
      if (observed) {
        // A read of the whole file is recorded as one, so the import's source
        // fingerprint lists the file.
        observed->Record(std::as_bytes(std::span(bytes.get(), requested)),
          ReadOptions {
            .offset = 0U,
            .max_bytes
            = std::cmp_equal(requested, length) ? 0U : requested_maximum,
            .size_hint = 0U,
            .alignment = 0U,
          });
      }
      *size = requested;
      *data = bytes.release();
      return cgltf_result_success;
    } catch (const std::bad_alloc&) {
      return cgltf_result_out_of_memory;
    } catch (const std::filesystem::filesystem_error& error) {
      return error.code() == std::errc::no_such_file_or_directory
        ? cgltf_result_file_not_found
        : cgltf_result_io_error;
    } catch (...) {
      return cgltf_result_io_error;
    }
  }

  auto ReleaseCgltfFile(const cgltf_memory_options* /*unused*/,
    const cgltf_file_options* /*unused*/, void* data,
    cgltf_size /*unused*/) noexcept -> void
  {
    // Frees the buffer ReadCgltfFile handed to cgltf.
    // NOLINTNEXTLINE(*-avoid-c-arrays)
    const std::unique_ptr<char[]> bytes(static_cast<char*>(data));
  }

  auto MakeCgltfOptions(const AdapterInput& input) -> cgltf_options
  {
    cgltf_options options {};
    options.file.user_data = input.source_snapshot.get();
    options.file.read = &ReadCgltfFile;
    options.file.release = &ReleaseCgltfFile;
    return options;
  }

  [[nodiscard]] auto LoadDataFromFile(const std::filesystem::path& path,
    const AdapterInput& input, std::vector<ImportDiagnostic>& diagnostics,
    const bool load_external_buffers = true) -> CgltfDataPtr
  {
    if (input.stop_token.stop_requested()) {
      DLOG_F(
        WARNING, "glTF load canceled: source_id='{}'", input.source_id_prefix);
      diagnostics.push_back(MakeCancelDiagnostic(input.source_id_prefix));
      return { nullptr, &cgltf_free };
    }

    const auto options = MakeCgltfOptions(input);
    cgltf_data* data = nullptr;
    const auto parse_result
      = cgltf_parse_file(&options, Utf8Path(path).c_str(), &data);
    if (parse_result != cgltf_result_success) {
      LOG_F(ERROR, "glTF parse failed: path='{}' result='{}'", path.string(),
        ResultToMessage(parse_result));
      diagnostics.push_back(
        MakeParseDiagnostic(input.source_id_prefix, parse_result));
      return { nullptr, &cgltf_free };
    }

    if (!load_external_buffers) {
      return { data, &cgltf_free };
    }

    const auto load_result
      = cgltf_load_buffers(&options, data, Utf8Path(path).c_str());
    if (load_result != cgltf_result_success) {
      LOG_F(ERROR, "glTF buffer load failed: path='{}' result='{}'",
        path.string(), ResultToMessage(load_result));
      diagnostics.push_back(
        MakeParseDiagnostic(input.source_id_prefix, load_result));
      cgltf_free(data);
      return { nullptr, &cgltf_free };
    }

    return { data, &cgltf_free };
  }

  [[nodiscard]] auto LoadDataFromMemory(const std::span<const std::byte> bytes,
    const AdapterInput& input, std::vector<ImportDiagnostic>& diagnostics,
    const std::filesystem::path& source_path, const bool load_external_buffers)
    -> CgltfDataPtr
  {
    if (input.stop_token.stop_requested()) {
      DLOG_F(WARNING, "glTF load canceled (memory): source_id='{}'",
        input.source_id_prefix);
      diagnostics.push_back(MakeCancelDiagnostic(input.source_id_prefix));
      return { nullptr, &cgltf_free };
    }

    const auto options = MakeCgltfOptions(input);
    cgltf_data* data = nullptr;
    const auto parse_result
      = cgltf_parse(&options, bytes.data(), bytes.size(), &data);
    if (parse_result != cgltf_result_success) {
      LOG_F(ERROR, "glTF parse failed (memory): result='{}'",
        ResultToMessage(parse_result));
      diagnostics.push_back(
        MakeParseDiagnostic(input.source_id_prefix, parse_result));
      return { nullptr, &cgltf_free };
    }

    if (!load_external_buffers) {
      return { data, &cgltf_free };
    }

    const auto load_result
      = cgltf_load_buffers(&options, data, Utf8Path(source_path).c_str());
    if (load_result != cgltf_result_success) {
      LOG_F(ERROR, "glTF buffer load failed (memory): result='{}'",
        ResultToMessage(load_result));
      diagnostics.push_back(
        MakeParseDiagnostic(input.source_id_prefix, load_result));
      cgltf_free(data);
      return { nullptr, &cgltf_free };
    }

    return { data, &cgltf_free };
  }

  auto ExternalSourcePath(const char* source_uri,
    const std::filesystem::path& source_path) -> std::filesystem::path
  {
    auto uri = std::string(source_uri);
    uri.resize(cgltf_decode_uri(uri.data()));
    auto path_text = Utf8Path(source_path);
    const auto separator = path_text.find_last_of("/\\");
    path_text.resize(separator == std::string::npos ? 0U : separator + 1U);
    path_text += uri;
    // A URI that names a folder names no image: the texture is missing.
    auto path = PathFromUtf8(path_text);
    return path.has_filename() ? path : std::filesystem::path {};
  }

  [[nodiscard]] auto ReadVec2(const cgltf_accessor* accessor)
    -> std::vector<glm::vec2>
  {
    if (accessor == nullptr) {
      return {};
    }

    std::vector<glm::vec2> out(accessor->count);
    for (cgltf_size i = 0; i < accessor->count; ++i) {
      auto v = std::array<cgltf_float, 4> {};
      cgltf_accessor_read_float(accessor, i, v.data(), v.size());
      out.at(i) = glm::make_vec2(v.data());
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
      auto v = std::array<cgltf_float, 4> {};
      cgltf_accessor_read_float(accessor, i, v.data(), v.size());
      out.at(i) = glm::make_vec3(v.data());
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
      auto v = std::array<cgltf_float, 4> {};
      cgltf_accessor_read_float(accessor, i, v.data(), v.size());
      out.at(i) = glm::make_vec4(v.data());
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
      auto v = std::array<cgltf_uint, 4> {};
      cgltf_accessor_read_uint(accessor, i, v.data(), v.size());
      out.at(i) = glm::make_vec4(v.data());
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
      auto v = std::array<cgltf_float, 16> {};
      cgltf_accessor_read_float(accessor, i, v.data(), v.size());
      out.at(i) = glm::make_mat4(v.data());
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
      out.at(i) = static_cast<uint32_t>(cgltf_accessor_read_index(accessor, i));
    }
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

    for (const auto& node : std::span(data.nodes, data.nodes_count)) {
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

    if (material->has_pbr_metallic_roughness != 0) {
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
    kSpecular,
    kSheenColor,
    kClearcoat,
    kClearcoatNormal,
    kTransmission,
    kThickness,
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
    case TextureUsage::kSpecular:
      return "specular";
    case TextureUsage::kSheenColor:
      return "sheen_color";
    case TextureUsage::kClearcoat:
      return "clearcoat";
    case TextureUsage::kClearcoatNormal:
      return "clearcoat_normal";
    case TextureUsage::kTransmission:
      return "transmission";
    case TextureUsage::kThickness:
      return "thickness";
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
    case TextureUsage::kSpecular:
      return TexturePreset::kRoughness; // Best approximation (single channel)
    case TextureUsage::kSheenColor:
      return TexturePreset::kAlbedo;
    case TextureUsage::kClearcoat:
      return TexturePreset::kRoughness; // Best approximation (single channel)
    case TextureUsage::kClearcoatNormal:
      return TexturePreset::kNormal;
    case TextureUsage::kTransmission:
    case TextureUsage::kThickness:
      return TexturePreset::kRoughness; // Best approximation (single channel)
    }
    return TexturePreset::kData;
  }

  //! The source a texture cooks from: its image, or the texture itself when it
  //! has no core image. Two textures of one image share their source.
  [[nodiscard]] auto BuildTextureSourceKey(
    const cgltf_data& data, const cgltf_texture& texture) -> std::string
  {
    if (texture.image != nullptr) {
      return "image_" + std::to_string(cgltf_image_index(&data, texture.image));
    }
    return "texture_" + std::to_string(cgltf_texture_index(&data, &texture));
  }

  [[nodiscard]] auto BuildTextureSourceId(std::string_view prefix,
    const cgltf_data& data, const cgltf_texture& texture,
    const TextureUsage usage) -> std::string
  {
    const auto key = BuildTextureSourceKey(data, texture);
    std::string id;
    if (!prefix.empty()) {
      id = std::string(prefix);
      id.append("::");
    }
    id.append("tex::");
    id.append(key);
    id.append("::");
    id.append(UsageLabel(usage));
    return id;
  }

  [[nodiscard]] constexpr auto IsBc7Format(const Format format) noexcept -> bool
  {
    return format == Format::kBC7UNorm || format == Format::kBC7UNormSRGB;
  }

  [[nodiscard]] auto ResolveUvSet(const cgltf_texture_view& view) -> uint8_t
  {
    if ((view.has_transform != 0) && (view.transform.has_texcoord != 0)) {
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
    if (view.has_transform != 0) {
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

  [[nodiscard]] auto DecodeBase64(std::string_view encoded)
    -> std::shared_ptr<std::vector<std::byte>>
  {
    auto bytes = std::make_shared<std::vector<std::byte>>();
    bytes->reserve((encoded.size() * 3U) / 4U);

    // RFC 4648 alphabet: A-Z, a-z, 0-9, '+', '/'.
    constexpr int kLowerStart = 26;
    constexpr int kDigitStart = 52;
    constexpr int kPlusValue = 62;
    constexpr int kSlashValue = 63;
    constexpr uint32_t kBitsPerChar = 6U;
    constexpr uint32_t kBitsPerByte = 8U;
    auto decode_char = [](const char c) -> int {
      if (c >= 'A' && c <= 'Z') {
        return c - 'A';
      }
      if (c >= 'a' && c <= 'z') {
        return c - 'a' + kLowerStart;
      }
      if (c >= '0' && c <= '9') {
        return c - '0' + kDigitStart;
      }
      if (c == '+') {
        return kPlusValue;
      }
      if (c == '/') {
        return kSlashValue;
      }
      return -1;
    };

    uint32_t accum = 0;
    uint32_t bits = 0;
    for (const char c : encoded) {
      if (c == '=') {
        break;
      }

      const int value = decode_char(c);
      if (value < 0) {
        continue;
      }

      accum = (accum << kBitsPerChar) | static_cast<uint32_t>(value);
      bits += kBitsPerChar;
      if (bits >= kBitsPerByte) {
        bits -= kBitsPerByte;
        const auto byte = static_cast<std::byte>((accum >> bits) & 0xFFU);
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
    if (!header.contains(";base64")) {
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

  struct ResolvedTextureSource final {
    TexturePipeline::SourceBytes bytes;
    std::filesystem::path source_path {};
  };

  [[nodiscard]] auto ResolveImageBytes(const cgltf_image& image,
    const std::filesystem::path& model_path,
    const std::shared_ptr<const cgltf_data>& owner,
    std::vector<ImportDiagnostic>& diagnostics, std::string_view source_id,
    std::span<const AdapterInput::ExternalTextureBytes> external_texture_bytes)
    -> std::optional<ResolvedTextureSource>
  {
    const auto make_placeholder = [] -> ResolvedTextureSource {
      auto bytes = std::make_shared<std::vector<std::byte>>();
      return ResolvedTextureSource {
        .bytes = TexturePipeline::SourceBytes {
          .bytes = std::span<const std::byte>(bytes->data(), bytes->size()),
          .owner = std::static_pointer_cast<const void>(bytes),
        },
      };
    };

    auto find_external_bytes = [&](std::string_view texture_id)
      -> std::shared_ptr<std::vector<std::byte>> {
      for (const auto& entry : external_texture_bytes) {
        if (entry.texture_id == texture_id) {
          return entry.bytes;
        }
      }
      return {};
    };
    if (image.buffer_view != nullptr && image.buffer_view->buffer != nullptr
      && image.buffer_view->buffer->data != nullptr) {
      const auto* buffer = image.buffer_view->buffer;
      const auto buffer_bytes
        = std::span(static_cast<const std::byte*>(buffer->data), buffer->size);
      return ResolvedTextureSource {
        .bytes = TexturePipeline::SourceBytes {
          .bytes = buffer_bytes.subspan(
            image.buffer_view->offset, image.buffer_view->size),
          .owner = std::static_pointer_cast<const void>(owner),
        },
      };
    }

    if (image.uri == nullptr || *image.uri == '\0') {
      diagnostics.push_back(MakeWarningDiagnostic("gltf.image.missing_uri",
        "glTF image has no buffer view or URI", source_id, ""));
      return make_placeholder();
    }

    std::string_view uri(image.uri);
    if (uri.starts_with("data:")) {
      auto bytes = DecodeDataUri(uri, diagnostics, source_id);
      return ResolvedTextureSource {
        .bytes = TexturePipeline::SourceBytes {
          .bytes = std::span<const std::byte>(bytes->data(), bytes->size()),
          .owner = std::static_pointer_cast<const void>(bytes),
        },
      };
    }

    const auto path = ExternalSourcePath(image.uri, model_path);
    if (!external_texture_bytes.empty()) {
      const auto external_bytes = find_external_bytes(source_id);
      if (external_bytes) {
        return ResolvedTextureSource {
          .bytes = TexturePipeline::SourceBytes {
            .bytes = std::span<const std::byte>(
              external_bytes->data(), external_bytes->size()),
            .owner = std::static_pointer_cast<const void>(external_bytes),
          },
        };
      }
    }

    auto placeholder = make_placeholder();
    placeholder.source_path = path;
    return placeholder;
  }

  [[nodiscard]] auto BuildSceneSourceId(
    std::string_view prefix, const ImportRequest& request) -> std::string
  {
    if (!prefix.empty()) {
      return std::string(prefix);
    }
    return request.GetSceneName();
  }

  struct NodeInput final {
    std::string authored_name;
    std::string base_name;
    //! The node's anchor, its model node identity (ModelNodeIdentity).
    std::string anchor;
    uint32_t parent_index = 0;
    glm::mat4 local_matrix { 1.0F };
    glm::mat4 world_matrix { 1.0F };
    bool has_renderable = false;
    bool has_camera = false;
    bool has_light = false;
    bool visible = true;
    const void* source_node = nullptr;
  };

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
    case UnitNormalizationPolicy::kNormalizeToMeters:
      return 1.0F;
    case UnitNormalizationPolicy::kApplyCustomFactor:
      return policy.unit_scale;
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
      converted = glm::column(converted, 3,
        glm::column(converted, 3) * glm::vec4(scale, scale, scale, 1.0F));
    }
    return converted;
  }

  [[nodiscard]] auto BuildGltfBakePlan(const cgltf_data& data,
    const CoordinateConversionPolicy& policy) -> internal::MeshBakePlan
  {
    std::vector<internal::MeshBakeNode> nodes(data.nodes_count);
    std::vector<uint8_t> emit_mesh(data.meshes_count);
    for (size_t index = 0;
      const auto& mesh : std::span(data.meshes, data.meshes_count)) {
      emit_mesh.at(index++) = mesh.primitives_count != 0 ? 1U : 0U;
    }
    std::unordered_set<const cgltf_node*> animated_or_joint;
    for (const auto& animation :
      std::span(data.animations, data.animations_count)) {
      for (const auto& channel :
        std::span(animation.channels, animation.channels_count)) {
        animated_or_joint.insert(channel.target_node);
      }
    }
    for (const auto& skin : std::span(data.skins, data.skins_count)) {
      for (const auto* joint : std::span(skin.joints, skin.joints_count)) {
        animated_or_joint.insert(joint);
      }
    }
    for (size_t index = 0;
      const auto& node : std::span(data.nodes, data.nodes_count)) {
      auto& input = nodes.at(index++);
      if (node.mesh == nullptr) {
        continue;
      }
      input.mesh_index = static_cast<size_t>(node.mesh - data.meshes);
      auto matrix = std::array<cgltf_float, 16> {};
      cgltf_node_transform_local(&node, matrix.data());
      input.local_transform = glm::make_mat4(matrix.data());
      input.local_transform
        = ConvertGltfTransform(input.local_transform, policy);
      if (node.children_count != 0 || node.camera != nullptr
        || node.light != nullptr) {
        input.retain_reason = "attachment or camera/light transform";
      } else if (animated_or_joint.contains(&node)
        || FindSkinForMesh(data, *node.mesh) != nullptr) {
        input.retain_reason = "animation or skinning transform";
      } else if (std::ranges::any_of(std::span(node.mesh->primitives,
                                       node.mesh->primitives_count),
                   [](const cgltf_primitive& primitive) -> bool {
                     return primitive.targets_count != 0;
                   })) {
        input.retain_reason = "morph target transform";
      }
    }
    return internal::BuildMeshBakePlan(
      nodes, emit_mesh, policy.bake_transforms_into_meshes);
  }

  auto GltfSourceLayoutWitness(const cgltf_mesh& mesh,
    const std::unordered_map<const cgltf_material*, uint32_t>& material_index)
    -> std::optional<base::Sha256Digest>
  {
    SourceLayoutHash hash("oxygen.gltf-source-layout/v1");
    hash.AddCount(mesh.primitives_count);
    for (const auto& primitive :
      std::span(mesh.primitives, mesh.primitives_count)) {
      hash.Add(static_cast<uint32_t>(primitive.type));
      const auto material = material_index.find(primitive.material);
      hash.Add(material == material_index.end()
          ? std::numeric_limits<uint32_t>::max()
          : material->second);
      const cgltf_accessor* positions = nullptr;
      for (const auto& attribute :
        std::span(primitive.attributes, primitive.attributes_count)) {
        if (attribute.type == cgltf_attribute_type_position
          && attribute.index == 0) {
          positions = attribute.data;
          break;
        }
      }
      hash.AddCount(positions != nullptr ? positions->count : 0);
      if (positions != nullptr) {
        for (cgltf_size index = 0; index < positions->count; ++index) {
          std::array<cgltf_float, 3> position {};
          if (cgltf_accessor_read_float(
                positions, index, position.data(), position.size())
            == 0) {
            return std::nullopt;
          }
          for (const auto component : position) {
            hash.AddFloat(component);
          }
        }
      }
      hash.Add(primitive.indices != nullptr ? 1U : 0U);
      hash.AddCount(
        primitive.indices != nullptr ? primitive.indices->count : 0);
      if (primitive.indices != nullptr) {
        for (cgltf_size index = 0; index < primitive.indices->count; ++index) {
          hash.Add(static_cast<uint64_t>(
            cgltf_accessor_read_index(primitive.indices, index)));
        }
      }
    }
    return hash.Finish();
  }

  auto PrepareGeometryNames(const cgltf_data& data, const AdapterInput& input)
    -> ModelGeometryPreparation
  {
    auto result = ModelGeometryPreparation {};
    if (input.stop_token.stop_requested()) {
      result.success = false;
      result.diagnostics.push_back(
        MakeCancelDiagnostic(input.source_id_prefix));
      return result;
    }
    CHECK_F(input.naming_service != nullptr, "NamingService must not be null");
    const auto bake_plan
      = BuildGltfBakePlan(data, input.request.options.coordinate);
    for (size_t index = 0; index < bake_plan.retained_reasons.size(); ++index) {
      if (!bake_plan.retained_reasons.at(index).empty()) {
        result.diagnostics.push_back(
          MakeWarningDiagnostic("mesh.transform_bake_retained",
            "Retained authored node transform: "
              + bake_plan.retained_reasons.at(index),
            input.source_id_prefix, "/nodes/" + std::to_string(index)));
      }
    }
    result.sources.reserve(bake_plan.variants.size());
    for (size_t variant_index = 0; variant_index < bake_plan.variants.size();
      ++variant_index) {
      if (input.stop_token.stop_requested()) {
        result.success = false;
        result.diagnostics.push_back(
          MakeCancelDiagnostic(input.source_id_prefix));
        return result;
      }
      const auto& variant = bake_plan.variants.at(variant_index);
      const auto* mesh = &base::CheckedAt(
        std::span(data.meshes, data.meshes_count), variant.mesh_index);
      if (mesh == nullptr) {
        continue;
      }
      const auto authored_name
        = std::string_view(mesh->name != nullptr ? mesh->name : "");
      const auto context = NamingContext {
        .kind = ImportNameKind::kMesh,
        .ordinal = static_cast<uint32_t>(variant.mesh_index),
        .variant = variant.name_suffix,
      };
      auto name = input.naming_service->MakeUniqueName(authored_name, context);
      auto source_id = BuildSourceId(
        input.source_id_prefix, name, static_cast<uint32_t>(variant_index));
      result.sources.push_back({
        .variant = variant,
        .variant_index = variant_index,
        .name = std::move(name),
        .source_id = std::move(source_id),
      });
    }
    return result;
  }

  //! Streams one work item per mesh variant. A primitive or mesh whose source
  //! data cannot be built is dropped with a warning; `emitted_variants`
  //! receives the variant index of each item, in emission order.
  [[nodiscard]] auto StreamWorkItemsFromData(const cgltf_data& data,
    const AdapterInput& input, GeometryWorkItemSink& sink,
    std::vector<size_t>& emitted_variants) -> WorkItemStreamResult
  {
    WorkItemStreamResult result;
    emitted_variants.clear();
    if (input.stop_token.stop_requested()) {
      result.success = false;
      result.diagnostics.push_back(
        MakeCancelDiagnostic(input.source_id_prefix));
      return result;
    }

    std::unordered_map<const cgltf_material*, uint32_t> material_index;
    for (uint32_t i = 0;
      const auto& mat : std::span(data.materials, data.materials_count)) {
      material_index.emplace(&mat, i++);
    }

    CHECK_F(input.naming_service != nullptr, "NamingService must not be null");

    std::vector<std::optional<base::Sha256Digest>> source_witnesses(
      data.meshes_count);

    struct PrimitiveInfo {
      const cgltf_primitive* prim = nullptr;
      const cgltf_accessor* positions = nullptr;
      const cgltf_accessor* normals = nullptr;
      const cgltf_accessor* texcoords = nullptr;
      const cgltf_accessor* tangents = nullptr;
      const cgltf_accessor* colors = nullptr;
      const cgltf_accessor* joints = nullptr;
      const cgltf_accessor* weights = nullptr;
      uint32_t surface = 0;
      uint32_t vertex_count = 0;
      uint32_t index_count = 0;
      bool has_normals = false;
      bool has_texcoords = false;
      bool has_tangents = false;
      bool has_colors = false;
      bool has_skin = false;
    };

    auto prepared = PrepareGeometryNames(data, input);
    result.success = prepared.success;
    result.diagnostics = std::move(prepared.diagnostics);
    if (!result.success) {
      return result;
    }
    for (const auto& source : prepared.sources) {
      const auto& variant = source.variant;
      const auto mesh_i = variant.mesh_index;
      const auto* mesh = &base::CheckedAt(
        std::span(data.meshes, data.meshes_count), variant.mesh_index);
      const auto& mesh_name = source.name;

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

        const auto& prim = base::CheckedAt(
          std::span(mesh->primitives, mesh->primitives_count), prim_i);
        if (prim.type != cgltf_primitive_type_triangles) {
          result.diagnostics.push_back(
            MakeWarningDiagnostic("gltf.primitive.type",
              "glTF primitive is not a triangle list; the primitive is dropped",
              input.source_id_prefix, mesh_name));
          continue;
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

        for (const auto& attr :
          std::span(prim.attributes, prim.attributes_count)) {
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
            if (!color_index.has_value()
              || std::cmp_less(attr.index, color_index.value())) {
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

          if (prim.material != nullptr) {
            const auto& material = *prim.material;
            if (material.has_pbr_metallic_roughness != 0) {
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

          cgltf_size fallback_uv_set = std::numeric_limits<cgltf_size>::max();
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
            MakeWarningDiagnostic("mesh.missing_positions",
              "glTF primitive missing POSITION attribute; the primitive is "
              "dropped",
              input.source_id_prefix, mesh_name));
          continue;
        }

        const auto vertex_count = static_cast<uint32_t>(positions->count);
        if (vertex_count == 0) {
          result.diagnostics.push_back(
            MakeWarningDiagnostic("mesh.missing_positions",
              "glTF primitive contains no vertex positions; the primitive is "
              "dropped",
              input.source_id_prefix, mesh_name));
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

        const auto surface = [&] -> uint32_t {
          if (prim.material == nullptr) {
            return static_cast<uint32_t>(data.materials_count);
          }
          if (const auto it = material_index.find(prim.material);
            it != material_index.end()) {
            return it->second;
          }
          return 0;
        }();

        if (prim.material != nullptr && !input.material_keys.empty()
          && surface >= input.material_keys.size()) {
          result.diagnostics.push_back(MakeWarningDiagnostic("mesh.surface_oob",
            "glTF material index exceeds imported material key count",
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
          result.diagnostics.push_back(
            MakeWarningDiagnostic("mesh.invalid_range",
              "glTF primitive index count is not a multiple of 3; the "
              "incomplete last triangle is dropped",
              input.source_id_prefix, mesh_name));
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
          .surface = surface,
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

      MeshBuildPipeline::WorkItem item;
      auto& witness = source_witnesses.at(mesh_i);
      if (!witness.has_value()) {
        witness = GltfSourceLayoutWitness(*mesh, material_index);
      }
      if (!witness.has_value()) {
        result.diagnostics.push_back(
          MakeWarningDiagnostic("mesh.source_layout_invalid",
            "Could not read the source-native glTF positions; the mesh is "
            "dropped",
            input.source_id_prefix, mesh_name));
        continue;
      }
      item.source_layout_witness = *witness;
      item.source_id = source.source_id;
      item.mesh_name = mesh_name;
      item.storage_mesh_name = mesh_name;
      item.source_key = variant.representative_node != internal::kNoBakeIndex
        ? static_cast<const void*>(
            &base::CheckedAt(std::span(data.nodes, data.nodes_count),
              variant.representative_node))
        : static_cast<const void*>(mesh);
      item.bake_transform = variant.transform;
      item.material_keys.assign(
        input.material_keys.begin(), input.material_keys.end());
      item.default_material_key = input.default_material_key;
      item.want_textures = true;
      item.has_material_textures = has_material_textures;
      item.request = input.request;
      item.naming_service = input.naming_service;
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
          result.diagnostics.push_back(
            MakeWarningDiagnostic("mesh.missing_positions",
              "glTF primitive positions cannot be read; the primitive is "
              "dropped",
              input.source_id_prefix, mesh_name));
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
              auto min_uv = glm::vec2 { std::numeric_limits<float>::max() };
              auto max_uv = glm::vec2 { std::numeric_limits<float>::lowest() };
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
              owner->joint_indices.end(), positions_vec.size(), glm::uvec4(0U));
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
              const auto& t = tangents_vec.at(i);
              const auto& n = owner->normals.at(normal_offset + i);
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
            indices_vec.at(i) = static_cast<uint32_t>(i);
          }
        }

        // An incomplete last triangle and the triangles that name a vertex
        // the primitive does not have are dropped; the rest are kept.
        indices_vec.resize(indices_vec.size() - (indices_vec.size() % 3U));
        const auto vertex_count_u32
          = static_cast<uint32_t>(positions_vec.size());
        const auto authored_triangles = indices_vec.size() / 3U;
        auto kept = size_t { 0 };
        for (size_t triangle = 0; triangle < authored_triangles; ++triangle) {
          const auto first = triangle * 3U;
          if (indices_vec.at(first) < vertex_count_u32
            && indices_vec.at(first + 1U) < vertex_count_u32
            && indices_vec.at(first + 2U) < vertex_count_u32) {
            indices_vec.at(kept * 3U) = indices_vec.at(first);
            indices_vec.at((kept * 3U) + 1U) = indices_vec.at(first + 1U);
            indices_vec.at((kept * 3U) + 2U) = indices_vec.at(first + 2U);
            ++kept;
          }
        }
        indices_vec.resize(kept * 3U);
        if (kept != authored_triangles) {
          result.diagnostics.push_back(
            MakeWarningDiagnostic("mesh.invalid_indices",
              "glTF primitive has " + std::to_string(authored_triangles - kept)
                + " triangles that reference out-of-range vertices; they are "
                  "dropped",
              input.source_id_prefix, mesh_name));
        }
        if (indices_vec.empty()) {
          result.diagnostics.push_back(MakeWarningDiagnostic(
            "mesh.missing_indices",
            "glTF primitive has no usable triangles; the primitive is dropped",
            input.source_id_prefix, mesh_name));
          continue;
        }

        const auto first_index = static_cast<uint32_t>(owner->indices.size());
        for (auto& index : indices_vec) {
          index += base_vertex;
        }
        owner->indices.insert(
          owner->indices.end(), indices_vec.begin(), indices_vec.end());

        owner->ranges.push_back(TriangleRange {
          .surface = prim_info.surface,
          .source_slot = prim_info.prim->material != nullptr
            ? prim_info.surface
            : std::numeric_limits<uint32_t>::max(),
          .first_index = first_index,
          .index_count = static_cast<uint32_t>(indices_vec.size()),
        });
      }

      if (owner->ranges.empty()) {
        result.diagnostics.push_back(MakeWarningDiagnostic("mesh.dropped",
          "glTF mesh has no usable primitives; the mesh is dropped",
          input.source_id_prefix, mesh_name));
        continue;
      }

      auto is_skinned = keep_skin && !owner->joint_indices.empty()
        && !owner->joint_weights.empty();

      if (is_skinned) {
        // Skin data that cannot bind the mesh is dropped: the mesh imports as
        // static geometry in its bind pose.
        const auto drop_skin
          = [&](const char* code, const char* reason) -> void {
          result.diagnostics.push_back(MakeWarningDiagnostic(code,
            std::string(reason)
              + "; the mesh imports as static geometry in its bind pose",
            input.source_id_prefix, mesh_name));
          is_skinned = false;
          owner->joint_indices.clear();
          owner->joint_weights.clear();
          owner->inverse_bind_matrices.clear();
        };
        const auto* skin = FindSkinForMesh(data, *mesh);
        if (skin == nullptr || skin->inverse_bind_matrices == nullptr) {
          drop_skin("mesh.missing_inverse_bind",
            "glTF skinned mesh missing inverse bind matrices");
        } else {
          owner->inverse_bind_matrices = ReadMat4(skin->inverse_bind_matrices);
          const auto joint_count = DetermineJointCount(owner->joint_indices);
          if (joint_count == 0 || owner->inverse_bind_matrices.empty()) {
            drop_skin("mesh.missing_inverse_bind",
              "glTF skinned mesh missing inverse bind matrices");
          } else if (owner->inverse_bind_matrices.size() < joint_count) {
            drop_skin("mesh.skinning_buffers_mismatch",
              "glTF skin inverse bind count is smaller than joint count");
          } else {
            owner->joint_remap.resize(joint_count);
            std::iota(owner->joint_remap.begin(), owner->joint_remap.end(), 0U);
          }
        }
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

      if (!owner->ranges.empty()) {
        uint32_t max_surface = 0;
        for (const auto& range : owner->ranges) {
          max_surface = (std::max)(max_surface, range.surface);
        }

        std::vector<uint8_t> used(max_surface + 1, 0);
        for (const auto& range : owner->ranges) {
          used.at(range.surface) = static_cast<uint8_t>(1);
        }

        item.surfaces_used.clear();
        for (uint32_t i = 0; i < used.size(); ++i) {
          if (used.at(i) != 0U) {
            item.surfaces_used.push_back(i);
            const auto authored = i < data.materials_count;
            const auto* label = authored
              ? base::CheckedAt(
                  std::span(data.materials, data.materials_count), i)
                  .name
              : nullptr;
            const auto* fallback = authored ? "Material" : "Default";
            item.surface_names.emplace(
              authored ? i : std::numeric_limits<uint32_t>::max(),
              label != nullptr ? label : fallback);
          }
        }
      }

      item.lods = {
        MeshLod {
          .lod_name = "LOD0",
          .source = triangle_mesh,
          .source_owner = std::move(owner),
        },
      };

      if (!sink.Consume(std::move(item))) {
        return result;
      }
      emitted_variants.push_back(source.variant_index);
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

auto GltfAdapter::InspectSource(const std::filesystem::path& source_path,
  const AdapterInput& input) -> SceneSourceInspection
{
  SceneSourceInspection result;
  auto adapter = GltfAdapter {};
  result.format = "gltf";
  adapter.impl_->data_owner.reset();
  adapter.impl_->source_snapshot = input.source_snapshot;
  auto data = LoadDataFromFile(source_path, input, result.diagnostics, false);
  if (!data) {
    return result;
  }
  if (input.stop_token.stop_requested()) {
    result.diagnostics.push_back(MakeCancelDiagnostic(input.source_id_prefix));
    return result;
  }
  result.parsed = true;
  result.supported = true;
  internal::ReportDroppedSourceFeatures(
    *data, input.source_id_prefix, result.diagnostics);
  result.source_unit_meters = 1.0;
  result.source_right = "+X";
  result.source_up = "+Y";
  result.source_front = "+Z";
  result.source_left_handed = false;
  result.mesh_count = data->meshes_count;
  result.material_count = data->materials_count;
  result.node_count = data->nodes_count;
  for (const auto& buffer : std::span(data->buffers, data->buffers_count)) {
    if (buffer.uri == nullptr
      || std::string_view(buffer.uri).starts_with("data:")) {
      continue;
    }
    std::string uri(buffer.uri);
    uri.resize(cgltf_decode_uri(uri.data()));
    if (uri.empty() || uri.contains(':') || uri.contains('\0')
      || uri.starts_with('/') || uri.starts_with('\\')) {
      result.supported = false;
      result.diagnostics.push_back(
        MakeErrorDiagnostic("import.source.nonlocal_dependency",
          "A glTF buffer must use a relative file URI or embedded data.",
          input.source_id_prefix, "/buffers"));
      continue;
    }
    std::ranges::replace(uri, '\\', '/');
    result.external_files.push_back(std::move(uri));
  }
  adapter.impl_->data_owner
    = std::shared_ptr<const cgltf_data>(data.release(), &cgltf_free);
  if (result.supported) {
    for (const auto& texture :
      adapter.CollectExternalTextureSources(input, result.diagnostics)) {
      result.external_files.push_back(
        texture.resolved_path.lexically_relative(source_path.parent_path())
          .generic_string());
    }
    result.supported = std::ranges::none_of(
      result.diagnostics, [](const auto& diagnostic) -> auto {
        return diagnostic.severity == ImportSeverity::kError;
      });
  }
  std::ranges::sort(result.external_files);
  const auto duplicates = std::ranges::unique(result.external_files);
  result.external_files.erase(duplicates.begin(), duplicates.end());
  return result;
}

auto GltfAdapter::Parse(const std::filesystem::path& source_path,
  const AdapterInput& input, const ModelParseMode mode) -> ParseResult
{
  impl_->data_owner.reset();
  impl_->source_snapshot = input.source_snapshot;
  ParseResult result;
  auto data = LoadDataFromFile(
    source_path, input, result.diagnostics, mode == ModelParseMode::kGeometry);
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

  internal::ReportDroppedSourceFeatures(
    *data, input.source_id_prefix, result.diagnostics);

  impl_->data_owner
    = std::shared_ptr<const cgltf_data>(data.release(), &cgltf_free);
  return result;
}

auto GltfAdapter::Parse(const std::span<const std::byte> source_bytes,
  const AdapterInput& input, const ModelParseMode mode) -> ParseResult
{
  impl_->data_owner.reset();
  impl_->source_snapshot = input.source_snapshot;
  ParseResult result;
  auto data = LoadDataFromMemory(source_bytes, input, result.diagnostics,
    input.request.source_path, mode == ModelParseMode::kGeometry);
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

  internal::ReportDroppedSourceFeatures(
    *data, input.source_id_prefix, result.diagnostics);

  impl_->data_owner
    = std::shared_ptr<const cgltf_data>(data.release(), &cgltf_free);
  return result;
}

auto GltfAdapter::PrepareGeometry(const AdapterInput& input) const
  -> ModelGeometryPreparation
{
  if (!impl_->data_owner) {
    auto result = ModelGeometryPreparation {};
    result.success = false;
    result.diagnostics.push_back(MakeErrorDiagnostic("gltf.scene.not_parsed",
      "gltf adapter has no parsed scene", input.source_id_prefix,
      input.object_path_prefix));
    return result;
  }
  return PrepareGeometryNames(*impl_->data_owner, input);
}

auto GltfAdapter::BuildWorkItems(GeometryWorkTag /*unused*/,
  GeometryWorkItemSink& sink, const AdapterInput& input) -> WorkItemStreamResult
{
  if (!impl_->data_owner) {
    WorkItemStreamResult result;
    result.success = false;
    result.diagnostics.push_back(MakeErrorDiagnostic("gltf.scene.not_parsed",
      "glTF adapter has no parsed scene", input.source_id_prefix,
      input.object_path_prefix));
    return result;
  }

  return StreamWorkItemsFromData(
    *impl_->data_owner, input, sink, impl_->emitted_variants);
}

auto GltfAdapter::PrepareMaterials(const AdapterInput& input)
  -> ModelMaterialPreparation
{
  if (!impl_->data_owner) {
    ModelMaterialPreparation result;
    result.success = false;
    result.diagnostics.push_back(MakeErrorDiagnostic("gltf.scene.not_parsed",
      "glTF adapter has no parsed scene", input.source_id_prefix,
      input.object_path_prefix));
    return result;
  }

  CHECK_F(input.naming_service != nullptr, "NamingService must not be null");

  ModelMaterialPreparation result;
  if (input.stop_token.stop_requested()) {
    result.success = false;
    result.diagnostics.push_back(MakeCancelDiagnostic(input.source_id_prefix));
    return result;
  }

  const auto& data = *impl_->data_owner;
  const auto material_count = static_cast<uint32_t>(data.materials_count);
  result.sources.reserve(material_count);
  for (uint32_t i = 0; i < material_count; ++i) {
    if (input.stop_token.stop_requested()) {
      result.success = false;
      result.diagnostics.push_back(
        MakeCancelDiagnostic(input.source_id_prefix));
      return result;
    }
    const auto& material
      = base::CheckedAt(std::span(data.materials, data.materials_count), i);
    const std::string_view authored = material.name ? material.name : "";
    const NamingContext material_context {
      .kind = ImportNameKind::kMaterial,
      .ordinal = i,
    };
    const auto material_name
      = input.naming_service->MakeUniqueName(authored, material_context);

    ModelMaterialSource item;
    item.source_id = BuildSourceId(input.source_id_prefix, material_name, i);
    item.material.name = material_name;
    item.material.storage_name = material_name;
    item.source_key = &material;
    item.material.domain = data::MaterialDomain::kOpaque;
    item.material.alpha_mode = MaterialAlphaMode::kOpaque;

    if (material.alpha_mode == cgltf_alpha_mode_mask) {
      item.material.alpha_mode = MaterialAlphaMode::kMasked;
      item.material.domain = data::MaterialDomain::kMasked;
    } else if (material.alpha_mode == cgltf_alpha_mode_blend) {
      item.material.alpha_mode = MaterialAlphaMode::kBlended;
      item.material.domain = data::MaterialDomain::kAlphaBlended;
    }

    item.material.inputs.alpha_cutoff
      = static_cast<float>(material.alpha_cutoff);
    item.material.inputs.double_sided = material.double_sided != 0;
    item.material.inputs.unlit = material.unlit != 0;

    if (material.has_pbr_metallic_roughness != 0) {
      const auto& pbr = material.pbr_metallic_roughness;
      item.material.inputs.base_color[0]
        = static_cast<float>(pbr.base_color_factor[0]);
      item.material.inputs.base_color[1]
        = static_cast<float>(pbr.base_color_factor[1]);
      item.material.inputs.base_color[2]
        = static_cast<float>(pbr.base_color_factor[2]);
      item.material.inputs.base_color[3]
        = static_cast<float>(pbr.base_color_factor[3]);
      item.material.inputs.metalness = static_cast<float>(pbr.metallic_factor);
      item.material.inputs.roughness = static_cast<float>(pbr.roughness_factor);
    }

    item.material.inputs.emissive_factor[0]
      = static_cast<float>(material.emissive_factor[0]);
    item.material.inputs.emissive_factor[1]
      = static_cast<float>(material.emissive_factor[1]);
    item.material.inputs.emissive_factor[2]
      = static_cast<float>(material.emissive_factor[2]);

    if (material.normal_texture.texture != nullptr) {
      item.material.inputs.normal_scale
        = static_cast<float>(material.normal_texture.scale);
      ApplyTextureBinding(item.material.textures.normal,
        material.normal_texture,
        BuildTextureSourceId(input.source_id_prefix, data,
          *material.normal_texture.texture, TextureUsage::kNormal));
    }

    if (material.occlusion_texture.texture != nullptr) {
      item.material.occlusion_mode = AmbientOcclusionMode::kStrength;
      item.material.inputs.ambient_occlusion
        = static_cast<float>(material.occlusion_texture.scale);
      ApplyTextureBinding(item.material.textures.ambient_occlusion,
        material.occlusion_texture,
        BuildTextureSourceId(input.source_id_prefix, data,
          *material.occlusion_texture.texture, TextureUsage::kOcclusion));
    }

    if (material.emissive_texture.texture != nullptr) {
      ApplyTextureBinding(item.material.textures.emissive,
        material.emissive_texture,
        BuildTextureSourceId(input.source_id_prefix, data,
          *material.emissive_texture.texture, TextureUsage::kEmissive));
    }

    if (material.has_ior != 0) {
      item.material.inputs.ior = material.ior.ior;
    }

    if (material.has_specular != 0) {
      // KHR_materials_specular's 1.0 is the default reflectance, the level's
      // 0.5; its texture holds the factor in alpha, cooked into red.
      item.material.inputs.specular_factor
        = 0.5F * material.specular.specular_factor;
      if (material.specular.specular_texture.texture != nullptr) {
        ApplyTextureBinding(item.material.textures.specular,
          material.specular.specular_texture,
          BuildTextureSourceId(input.source_id_prefix, data,
            *material.specular.specular_texture.texture,
            TextureUsage::kSpecular));
      }
    }

    if (material.has_sheen != 0) {
      item.material.inputs.sheen_color_factor[0]
        = static_cast<float>(material.sheen.sheen_color_factor[0]);
      item.material.inputs.sheen_color_factor[1]
        = static_cast<float>(material.sheen.sheen_color_factor[1]);
      item.material.inputs.sheen_color_factor[2]
        = static_cast<float>(material.sheen.sheen_color_factor[2]);
      if (material.sheen.sheen_color_texture.texture != nullptr) {
        ApplyTextureBinding(item.material.textures.sheen_color,
          material.sheen.sheen_color_texture,
          BuildTextureSourceId(input.source_id_prefix, data,
            *material.sheen.sheen_color_texture.texture,
            TextureUsage::kSheenColor));
      }
    }

    if (material.has_clearcoat != 0) {
      item.material.inputs.clearcoat_factor
        = material.clearcoat.clearcoat_factor;
      item.material.inputs.clearcoat_roughness
        = material.clearcoat.clearcoat_roughness_factor;
      if (material.clearcoat.clearcoat_texture.texture != nullptr) {
        ApplyTextureBinding(item.material.textures.clearcoat,
          material.clearcoat.clearcoat_texture,
          BuildTextureSourceId(input.source_id_prefix, data,
            *material.clearcoat.clearcoat_texture.texture,
            TextureUsage::kClearcoat));
      }
      if (material.clearcoat.clearcoat_normal_texture.texture != nullptr) {
        ApplyTextureBinding(item.material.textures.clearcoat_normal,
          material.clearcoat.clearcoat_normal_texture,
          BuildTextureSourceId(input.source_id_prefix, data,
            *material.clearcoat.clearcoat_normal_texture.texture,
            TextureUsage::kClearcoatNormal));
      }
    }

    if (material.has_transmission != 0) {
      item.material.inputs.transmission_factor
        = material.transmission.transmission_factor;
      if (material.transmission.transmission_texture.texture != nullptr) {
        ApplyTextureBinding(item.material.textures.transmission,
          material.transmission.transmission_texture,
          BuildTextureSourceId(input.source_id_prefix, data,
            *material.transmission.transmission_texture.texture,
            TextureUsage::kTransmission));
      }
    }

    if (material.has_volume != 0) {
      item.material.inputs.thickness_factor = material.volume.thickness_factor;
      item.material.inputs.attenuation_distance
        = material.volume.attenuation_distance;
      item.material.inputs.attenuation_color[0]
        = static_cast<float>(material.volume.attenuation_color[0]);
      item.material.inputs.attenuation_color[1]
        = static_cast<float>(material.volume.attenuation_color[1]);
      item.material.inputs.attenuation_color[2]
        = static_cast<float>(material.volume.attenuation_color[2]);
      if (material.volume.thickness_texture.texture != nullptr) {
        ApplyTextureBinding(item.material.textures.thickness,
          material.volume.thickness_texture,
          BuildTextureSourceId(input.source_id_prefix, data,
            *material.volume.thickness_texture.texture,
            TextureUsage::kThickness));
      }
    }

    if (material.has_pbr_metallic_roughness != 0) {
      const auto& pbr = material.pbr_metallic_roughness;
      if (pbr.base_color_texture.texture != nullptr) {
        ApplyTextureBinding(item.material.textures.base_color,
          pbr.base_color_texture,
          BuildTextureSourceId(input.source_id_prefix, data,
            *pbr.base_color_texture.texture, TextureUsage::kBaseColor));
      }

      if (pbr.metallic_roughness_texture.texture != nullptr) {
        const auto source_id = BuildTextureSourceId(input.source_id_prefix,
          data, *pbr.metallic_roughness_texture.texture,
          TextureUsage::kMetallicRoughness);
        ApplyTextureBinding(item.material.textures.metallic,
          pbr.metallic_roughness_texture, source_id);
        ApplyTextureBinding(item.material.textures.roughness,
          pbr.metallic_roughness_texture, source_id);
        item.material.orm_policy = OrmPolicy::kAuto;
      }
    }

    item.material.RepairModelInputs(item.source_id, result.diagnostics);
    if (!item.material.Validate(item.source_id, result.diagnostics)) {
      result.success = false;
      return result;
    }
    result.sources.push_back(std::move(item));
  }

  return result;
}

auto GltfAdapter::BuildWorkItems(MaterialWorkTag /*unused*/,
  MaterialWorkItemSink& sink, const AdapterInput& input) -> WorkItemStreamResult
{
  auto prepared = PrepareMaterials(input);
  auto result = WorkItemStreamResult {};
  result.success = prepared.success;
  result.diagnostics = std::move(prepared.diagnostics);
  if (!result.success) {
    return result;
  }
  for (auto& source : prepared.sources) {
    if (input.stop_token.stop_requested()) {
      result.success = false;
      result.diagnostics.push_back(
        MakeCancelDiagnostic(input.source_id_prefix));
      return result;
    }
    auto item = MaterialPipeline::WorkItem {};
    item.source_id = std::move(source.source_id);
    item.source_key = source.source_key;
    item.material = std::move(source.material);
    item.request = input.request;
    item.naming_service = input.naming_service;
    item.stop_token = input.stop_token;
    if (!sink.Consume(std::move(item))) {
      return result;
    }
    ++result.emitted;
  }
  return result;
}

auto GltfAdapter::PrepareTextures(const AdapterInput& input) const
  -> ModelTexturePreparation
{
  if (!impl_->data_owner) {
    ModelTexturePreparation result;
    result.success = false;
    result.diagnostics.push_back(MakeErrorDiagnostic("gltf.scene.not_parsed",
      "glTF adapter has no parsed scene", input.source_id_prefix,
      input.object_path_prefix));
    return result;
  }

  ModelTexturePreparation result;
  if (input.stop_token.stop_requested()) {
    result.success = false;
    result.diagnostics.push_back(MakeCancelDiagnostic(input.source_id_prefix));
    return result;
  }

  const auto& data = *impl_->data_owner;

  // Sorted by source key, so textures are produced in a fixed order.
  std::map<std::string, ModelTextureSource> work_items;

  auto register_texture
    = [&](const cgltf_texture_view& view, const TextureUsage usage) -> void {
    if (view.texture == nullptr) {
      return;
    }

    const auto source_id = BuildTextureSourceId(
      input.source_id_prefix, data, *view.texture, usage);
    if (work_items.contains(source_id)) {
      return;
    }

    const auto* image = view.texture->image;
    auto desc = MakeDescFromPreset(PresetForUsage(usage));
    desc.source_id = source_id;
    desc.stop_token = input.stop_token;
    if (usage == TextureUsage::kSpecular) {
      desc.value_channel = TextureChannel::kAlpha;
    }
    const auto& tuning = input.request.options.texture_tuning;
    if (tuning.enabled) {
      desc.flip_y_on_decode = tuning.flip_y_on_decode;
      desc.force_rgba_on_decode = tuning.force_rgba_on_decode;
      desc.mip_policy = tuning.mip_policy;
      desc.max_mip_levels = tuning.max_mip_levels;
      desc.mip_filter = tuning.mip_filter;
      desc.output_format = (usage == TextureUsage::kBaseColor
                             || usage == TextureUsage::kEmissive)
        ? tuning.color_output_format
        : tuning.data_output_format;
      desc.bc7_quality = IsBc7Format(desc.output_format) ? tuning.bc7_quality
                                                         : Bc7Quality::kNone;
    }

    ModelTextureSource item {};
    item.source_id = source_id;
    item.external_texture_id = source_id;
    item.source_key = view.texture;
    item.desc = std::move(desc);
    item.packing_policy_id
      = tuning.enabled ? tuning.packing_policy_id : "d3d12";
    item.output_format_policy = tuning.enabled
      ? TexturePipeline::OutputFormatPolicy::kExplicit
      : TexturePipeline::OutputFormatPolicy::kMaterialPreset;
    item.failure_policy
      = input.request.options.texture_tuning.placeholder_on_failure
      ? TexturePipeline::FailurePolicy::kPlaceholder
      : TexturePipeline::FailurePolicy::kStrict;
    if (image != nullptr) {
      const auto uri = image->uri != nullptr ? std::string_view(image->uri)
                                             : std::string_view {};
      item.embedded = image->buffer_view != nullptr || uri.starts_with("data:");
      if (!item.embedded && !uri.empty()) {
        item.source_path
          = ExternalSourcePath(image->uri, input.request.source_path);
      }
    }
    item.image_key = BuildTextureSourceKey(data, *view.texture);
    item.ordinal = static_cast<uint32_t>(image != nullptr
        ? cgltf_image_index(&data, image)
        : cgltf_texture_index(&data, view.texture));
    item.usage = UsageLabel(usage);
    if (view.texture->name != nullptr) {
      item.authored_name = view.texture->name;
    } else if (image != nullptr && image->name != nullptr) {
      item.authored_name = image->name;
    } else {
      item.authored_name = item.source_path.stem().string();
    }

    work_items.emplace(source_id, std::move(item));
  };

  for (cgltf_size i = 0; i < data.materials_count; ++i) {
    if (input.stop_token.stop_requested()) {
      result.success = false;
      result.diagnostics.push_back(
        MakeCancelDiagnostic(input.source_id_prefix));
      return result;
    }
    const auto& material
      = base::CheckedAt(std::span(data.materials, data.materials_count), i);
    if (material.has_pbr_metallic_roughness != 0) {
      const auto& pbr = material.pbr_metallic_roughness;
      register_texture(pbr.base_color_texture, TextureUsage::kBaseColor);
      register_texture(
        pbr.metallic_roughness_texture, TextureUsage::kMetallicRoughness);
    }
    register_texture(material.normal_texture, TextureUsage::kNormal);
    register_texture(material.occlusion_texture, TextureUsage::kOcclusion);
    register_texture(material.emissive_texture, TextureUsage::kEmissive);
  }

  result.sources.reserve(work_items.size());
  for (auto& [_, item] : work_items) {
    result.sources.push_back(std::move(item));
  }
  NameModelTextures(result.sources);
  return result;
}

auto GltfAdapter::BuildWorkItems(TextureWorkTag /*unused*/,
  TextureWorkItemSink& sink, const AdapterInput& input) -> WorkItemStreamResult
{
  auto prepared = PrepareTextures(input);
  auto result = WorkItemStreamResult {};
  result.success = prepared.success;
  result.diagnostics = std::move(prepared.diagnostics);
  if (!result.success) {
    return result;
  }
  for (auto& source : prepared.sources) {
    if (input.stop_token.stop_requested()) {
      result.success = false;
      result.diagnostics.push_back(
        MakeCancelDiagnostic(input.source_id_prefix));
      return result;
    }
    const auto* texture = static_cast<const cgltf_texture*>(source.source_key);
    auto resolved = std::optional<ResolvedTextureSource> {};
    if (texture->image != nullptr) {
      resolved = ResolveImageBytes(*texture->image, input.request.source_path,
        impl_->data_owner, result.diagnostics, source.source_id,
        input.external_texture_bytes);
    } else {
      result.diagnostics.push_back(MakeWarningDiagnostic("gltf.image.missing",
        "glTF texture has no image", source.source_id, ""));
      auto bytes = std::make_shared<std::vector<std::byte>>();
      resolved = ResolvedTextureSource {
        .bytes = TexturePipeline::SourceBytes {
          .bytes = std::span<const std::byte>(*bytes),
          .owner = std::static_pointer_cast<const void>(bytes),
        },
        .source_path = {},
      };
    }
    if (!resolved.has_value()) {
      continue;
    }
    auto item = TexturePipeline::WorkItem {};
    item.source_id = std::move(source.source_id);
    item.texture_id = item.source_id;
    item.name = std::move(source.name);
    item.source_key = source.source_key;
    item.desc = std::move(source.desc);
    item.packing_policy_id = std::move(source.packing_policy_id);
    item.output_format_policy = source.output_format_policy;
    item.failure_policy = source.failure_policy;
    item.source = resolved->bytes;
    item.source_path = std::move(resolved->source_path);
    item.fallback_source_paths
      = TextureFallbackPaths(input.request.source_path, item.source_path);
    item.stop_token = input.stop_token;
    if (!sink.Consume(std::move(item))) {
      return result;
    }
    ++result.emitted;
  }
  return result;
}

auto GltfAdapter::CollectExternalBufferSources(const AdapterInput& input) const
  -> std::vector<std::filesystem::path>
{
  auto paths = std::vector<std::filesystem::path> {};
  if (!impl_->data_owner) {
    return paths;
  }
  const auto& data = *impl_->data_owner;
  for (const auto& buffer : std::span(data.buffers, data.buffers_count)) {
    if (buffer.uri != nullptr
      && !std::string_view(buffer.uri).starts_with("data:")) {
      paths.push_back(
        ExternalSourcePath(buffer.uri, input.request.source_path));
    }
  }
  return paths;
}

auto GltfAdapter::CollectExternalTextureSources(
  const AdapterInput& input, std::vector<ImportDiagnostic>& diagnostics) const
  -> std::vector<ExternalTextureSource>
{
  auto prepared = PrepareTextures(input);
  for (auto& diagnostic : prepared.diagnostics) {
    diagnostics.push_back(std::move(diagnostic));
  }
  auto sources = std::vector<ExternalTextureSource> {};
  auto seen_ids = std::unordered_set<std::string> {};
  for (auto& source : prepared.sources) {
    if (!source.embedded && !source.source_path.empty()
      && seen_ids.insert(source.external_texture_id).second) {
      sources.push_back({
        .texture_id = std::move(source.external_texture_id),
        .resolved_path = std::move(source.source_path),
      });
    }
  }
  return sources;
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

  const auto bake_plan = BuildGltfBakePlan(data, request.options.coordinate);

  if (input.geometry_keys.size() > impl_->emitted_variants.size()) {
    diagnostics.push_back(MakeErrorDiagnostic("scene.geometry_key_missing",
      "Geometry key count does not match the emitted geometry", input.source_id,
      {}));
  }
  // A node whose mesh was dropped keeps its place but shows no geometry.
  const auto geometry_keys = GeometryKeysByVariant(
    bake_plan.variants.size(), impl_->emitted_variants, input.geometry_keys);
  const auto geometry_key_of
    = [&](const cgltf_node& node) -> std::optional<oxygen::data::AssetKey> {
    const auto variant
      = bake_plan.node_variant.at(static_cast<size_t>(&node - data.nodes));
    return variant < geometry_keys.size() ? geometry_keys.at(variant)
                                          : std::nullopt;
  };

  CHECK_F(input.naming_service != nullptr, "NamingService must not be null");

  std::vector<NodeInput> nodes;
  nodes.reserve(data.nodes_count > 0 ? data.nodes_count : 1U);
  auto identity = internal::ModelNodeIdentity::ForRequest(
    request, input.source_id, diagnostics);

  const auto kInvalidParent = std::numeric_limits<uint32_t>::max();
  const auto apply_node
    = [&](const cgltf_node* node, uint32_t parent_index, uint32_t& ordinal,
        const glm::mat4& parent_world, auto&& self) -> void {
    if (node == nullptr || input.stop_token.stop_requested()) {
      return;
    }

    const auto authored = node->name != nullptr ? std::string_view(node->name)
                                                : std::string_view {};
    const NamingContext node_context {
      .kind = ImportNameKind::kSceneNode,
      .ordinal = ordinal,
    };
    const auto base_name
      = input.naming_service->MakeUniqueName(authored, node_context);

    auto local_matrix_data = std::array<cgltf_float, 16> {};
    cgltf_node_transform_local(node, local_matrix_data.data());
    auto local_matrix = glm::make_mat4(local_matrix_data.data());

    local_matrix
      = ConvertGltfTransform(local_matrix, request.options.coordinate);
    const auto source_node_index = static_cast<size_t>(node - data.nodes);
    const auto variant_index = bake_plan.node_variant.at(source_node_index);
    if (variant_index < bake_plan.variants.size()
      && bake_plan.variants.at(variant_index).transform.has_value()) {
      local_matrix = glm::mat4(1.0F);
    }
    const auto world_matrix = parent_world * local_matrix;

    NodeInput node_input;
    node_input.authored_name = std::string(authored);
    node_input.base_name = base_name;
    node_input.anchor = identity.SourceAnchor({
      .parent_anchor = parent_index == kInvalidParent
        ? std::string_view {}
        : std::string_view { nodes.at(parent_index).anchor },
      .name = authored,
    });
    node_input.local_matrix = local_matrix;
    node_input.world_matrix = world_matrix;
    node_input.visible = true;
    node_input.has_camera = node->camera != nullptr;
    node_input.has_light = node->light != nullptr;
    node_input.has_renderable
      = node->mesh != nullptr && geometry_key_of(*node).has_value();
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

    ++ordinal;

    for (const auto* child : std::span(node->children, node->children_count)) {
      self(child, index, ordinal, world_matrix, self);
    }
  };

  uint32_t ordinal = 0;
  if (data.scenes_count > 0) {
    const auto* scene = data.scene != nullptr ? data.scene : data.scenes;
    for (const auto* root : std::span(scene->nodes, scene->nodes_count)) {
      apply_node(root, kInvalidParent, ordinal, glm::mat4 { 1.0F }, apply_node);
    }
  }

  if (nodes.empty()) {
    NodeInput root;
    root.authored_name = "root";
    root.base_name = "root";
    root.anchor = internal::ModelNodeIdentity::kSynthesizedRoot;
    root.parent_index = 0;
    root.local_matrix = glm::mat4 { 1.0F };
    root.world_matrix = glm::mat4 { 1.0F };
    root.visible = true;
    nodes.push_back(std::move(root));
  }

  {
    auto anchors = std::vector<std::string> {};
    anchors.reserve(nodes.size());
    for (const auto& node : nodes) {
      anchors.push_back(node.anchor);
    }
    if (!identity.ResolveContinuity(anchors, input.source_id, diagnostics)) {
      return result;
    }
  }

  std::vector<uint32_t> kept_indices;
  kept_indices.reserve(nodes.size());

  if (request.options.node_pruning == NodePruningPolicy::kDropEmptyNodes) {
    for (uint32_t i = 0; i < nodes.size(); ++i) {
      const auto& node = nodes.at(i);
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
    root.anchor = internal::ModelNodeIdentity::kSynthesizedRoot;
    root.parent_index = 0;
    root.local_matrix = glm::mat4 { 1.0F };
    root.world_matrix = glm::mat4 { 1.0F };
    root.visible = true;
    nodes.clear();
    nodes.push_back(std::move(root));
    kept_indices.push_back(0);
  }

  std::vector old_to_new(nodes.size(), -1);
  for (uint32_t new_index = 0; new_index < kept_indices.size(); ++new_index) {
    old_to_new.at(kept_indices.at(new_index)) = static_cast<int32_t>(new_index);
  }

  std::vector<NodeInput> pruned_nodes;
  pruned_nodes.reserve(kept_indices.size());

  for (uint32_t new_index = 0; new_index < kept_indices.size(); ++new_index) {
    const auto old_index = kept_indices.at(new_index);
    auto node = nodes.at(old_index);

    uint32_t parent = node.parent_index;
    while (parent < nodes.size() && old_to_new.at(parent) < 0) {
      const auto next_parent = nodes.at(parent).parent_index;
      if (next_parent == parent) {
        break;
      }
      parent = next_parent;
    }

    uint32_t new_parent_index = new_index;
    if (parent < nodes.size() && old_to_new.at(parent) >= 0) {
      new_parent_index = static_cast<uint32_t>(old_to_new.at(parent));
    }

    // A retained parent needs only index remapping, never transform
    // reparenting.
    if (new_parent_index != new_index && parent != node.parent_index) {
      const auto parent_old_index = kept_indices.at(new_parent_index);
      const auto& parent_world = nodes.at(parent_old_index).world_matrix;

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
        constexpr auto kSingularDeterminant = 1e-6F;
        if (std::abs(det) > kSingularDeterminant) {
          node.local_matrix = glm::inverse(parent_world) * node.world_matrix;
        } else {
          diagnostics.push_back(MakeWarningDiagnostic("scene.pruning.singular",
            "Node keeps its local transform: its parent transform is singular",
            input.source_id, node.base_name));
        }
      }
    }

    node.parent_index = new_parent_index;
    pruned_nodes.push_back(std::move(node));
  }

  SceneBuild build;
  build.flags = data::pak::world::SceneAssetFlags::kModelScene;
  build.nodes.reserve(pruned_nodes.size());
  std::vector<NodeRecord> component_attachments;

  // Register generated names only after every source name, and append these
  // nodes after all source nodes to preserve authored identities and indices.
  const auto append_attachment
    = [&](const NodeInput& owner, const uint32_t owner_index,
        const std::string_view role, const glm::quat& rotation) -> uint32_t {
    const auto index = static_cast<uint32_t>(
      pruned_nodes.size() + component_attachments.size());
    const NamingContext context {
      .kind = ImportNameKind::kSceneNode,
      .ordinal = index,
    };
    const auto name = input.naming_service->MakeUniqueName(
      owner.base_name + "_" + std::string(role), context);
    NodeRecord attachment {};
    attachment.node_id
      = identity.Emit(internal::ModelNodeIdentity::AddedAnchor({
        .owner_anchor = owner.anchor,
        .role = role,
      }));
    attachment.scene_name_offset = build.strings.Add(name);
    attachment.parent_index = owner_index;
    // Later visibility/shadow edits on the source owner must still govern its
    // attached component, including when that source itself inherits a flag.
    attachment.inherited_flags = data::pak::world::kSceneNodeFlags_Inheritable;
    attachment.rotation[0] = rotation.x;
    attachment.rotation[1] = rotation.y;
    attachment.rotation[2] = rotation.z;
    attachment.rotation[3] = rotation.w;
    attachment.scale[0] = 1.0F;
    attachment.scale[1] = 1.0F;
    attachment.scale[2] = 1.0F;
    component_attachments.push_back(attachment);
    return index;
  };

  for (uint32_t i = 0; i < pruned_nodes.size(); ++i) {
    auto& node = pruned_nodes.at(i);
    const auto& name = node.base_name;

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

    const auto matrix_translation
      = glm::vec3(glm::column(node.local_matrix, 3));
    const auto translation_delta
      = glm::length(translation - matrix_translation);
    constexpr auto kTranslationTolerance = 1e-3F;
    if (translation_delta > kTranslationTolerance) {
      LOG_F(WARNING,
        "SceneImport: node '{}' translation mismatch (decompose vs matrix) "
        "decomposed=({:.6f},{:.6f},{:.6f}) matrix=({:.6f},{:.6f},{:.6f})",
        name, translation.x, translation.y, translation.z, matrix_translation.x,
        matrix_translation.y, matrix_translation.z);
    }

    NodeRecord rec {};
    rec.node_id = identity.Emit(node.anchor);
    rec.scene_name_offset = build.strings.Add(name);
    rec.parent_index = node.parent_index;
    rec.node_flags = detail::BuildImportedNodeFlags(
      node.visible, node.has_renderable, node.has_light);
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
      if (const auto key = geometry_key_of(*gltf_node); key.has_value()) {
        build.renderables.push_back(RenderableRecord {
          .node_index = i,
          .geometry_key = *key,
        });
      }
    }

    if (gltf_node != nullptr && gltf_node->camera != nullptr) {
      const auto& cam = *gltf_node->camera;
      uint32_t camera_node_index = i;
      if (cam.type == cgltf_camera_type_perspective
        || cam.type == cgltf_camera_type_orthographic) {
        // General nodes use C * M * inverse(C), but cameras retain their
        // local -Z forward/+Y up basis. A camera-only child with local C
        // produces C * M without rotating the owner's mesh, light or children.
        camera_node_index = append_attachment(
          node, i, "Camera", glm::quat_cast(GltfToOxygenBasis()));
      }
      const auto unit_scale = ComputeUnitScale(request.options.coordinate);
      auto commit = detail::ImportedPropertyCommit {};
      if (cam.type == cgltf_camera_type_perspective) {
        namespace camera_schema = data::schema::perspective_camera;
        const auto& perspective = cam.data.perspective;
        auto camera = MakeDefaultRecord<PerspectiveCameraRecord>();
        camera.node_index = camera_node_index;
        camera.fov_y = commit("fov_y", camera_schema::kFieldOfViewYRadiansRange,
          perspective.yfov, camera.fov_y);
        // Without an aspect ratio, glTF frames the render target's.
        camera.aspect_mode = CameraAspectMode::kAuto;
        if (perspective.has_aspect_ratio != 0) {
          camera.aspect_mode = CameraAspectMode::kFixed;
          camera.aspect_ratio
            = commit("aspect_ratio", camera_schema::kAspectRatioRange,
              perspective.aspect_ratio, camera.aspect_ratio);
        }
        camera.near_plane = commit("near_plane", camera_schema::kNearPlaneRange,
          perspective.znear * unit_scale, camera.near_plane);
        // An infinite glTF projection takes the schema's far plane.
        if (perspective.has_zfar != 0) {
          camera.far_plane = commit("far_plane", camera_schema::kFarPlaneRange,
            perspective.zfar * unit_scale, camera.far_plane);
        }
        build.perspective_cameras.push_back(camera);
      } else if (cam.type == cgltf_camera_type_orthographic) {
        namespace camera_schema = data::schema::orthographic_camera;
        const auto& ortho = cam.data.orthographic;
        // glTF's xmag and ymag are half extents; the volume is the schema's
        // half height and aspect ratio.
        const auto half_height
          = commit("orthographic_size", camera_schema::kOrthographicSizeRange,
            ortho.ymag * unit_scale, camera_schema::kOrthographicSizeDefault);
        const auto half_width = half_height
          * commit("aspect_ratio", camera_schema::kAspectRatioRange,
            ortho.xmag / ortho.ymag, camera_schema::kAspectRatioDefault);

        auto camera = MakeDefaultRecord<OrthographicCameraRecord>();
        camera.node_index = camera_node_index;
        camera.left = -half_width;
        camera.right = half_width;
        camera.bottom = -half_height;
        camera.top = half_height;
        camera.near_plane = commit("near_plane", camera_schema::kNearPlaneRange,
          ortho.znear * unit_scale, camera.near_plane);
        camera.far_plane = commit("far_plane", camera_schema::kFarPlaneRange,
          ortho.zfar * unit_scale, camera.far_plane);
        camera.aspect_mode = CameraAspectMode::kFixed;
        build.orthographic_cameras.push_back(camera);
      } else {
        diagnostics.push_back(MakeWarningDiagnostic("scene.camera.unsupported",
          "Unsupported camera projection type", input.source_id, name));
      }
      commit.Report(input.source_id, name, diagnostics);
    }

    if (gltf_node != nullptr && gltf_node->light != nullptr) {
      const auto node_light_semantics
        = ReadImportedLightSemantics(gltf_node->extras);
      const auto& light = *gltf_node->light;
      const auto light_semantics = ReadImportedLightSemantics(light.extras);
      const auto resolve_common
        = [&](const data::pak::world::LightCommonRecord& defaults)
        -> data::pak::world::LightCommonRecord {
        return detail::ResolveImportedLightCommon(defaults,
          node_light_semantics, light_semantics,
          /*default_affects_world=*/true,
          /*default_casts_shadows=*/true);
      };
      auto light_node_index = i;
      if (light.type == cgltf_light_type_directional
        || light.type == cgltf_light_type_spot) {
        // glTF emits along local -Z, which C maps to +Y. Native lights emit
        // along -Y. Rotate the light attachment 180 degrees about Oxygen +Z
        // to align emission and preserve local up. Directional and circular
        // spot lights have no roll-dependent shape; point lights need no basis.
        light_node_index
          = append_attachment(node, i, "Light", glm::quat(0, 0, 0, 1));
      }
      auto commit = detail::ImportedPropertyCommit {};
      const auto color
        = std::array { light.color[0], light.color[1], light.color[2] };
      // An omitted glTF range takes the importer's policy, not the schema's.
      const auto local_range = light.has_range != 0
        ? light.range * ComputeUnitScale(request.options.coordinate)
        : request.options.gltf_omitted_light_range_m;
      switch (light.type) {
      case cgltf_light_type_directional: {
        namespace light_schema = data::schema::directional_light;
        auto rec_light = MakeDefaultRecord<DirectionalLightRecord>();
        rec_light.node_index = light_node_index;
        rec_light.common = resolve_common(rec_light.common);
        detail::CommitImportedLightColor(
          commit, data::kDirectionalLightCommonRanges, color, rec_light.common);
        rec_light.intensity_lux
          = commit("intensity_lux", light_schema::kIntensityLuxRange,
            light.intensity, rec_light.intensity_lux);
        build.directional_lights.push_back(rec_light);
        break;
      }
      case cgltf_light_type_point: {
        namespace light_schema = data::schema::point_light;
        auto rec_light = MakeDefaultRecord<PointLightRecord>();
        rec_light.node_index = i;
        rec_light.common = resolve_common(rec_light.common);
        detail::CommitImportedLightColor(
          commit, data::kPointLightCommonRanges, color, rec_light.common);
        rec_light.range = commit(
          "range", light_schema::kRangeRange, local_range, rec_light.range);
        rec_light.luminous_flux_lm
          = commit("luminous_flux_lm", light_schema::kLuminousFluxLmRange,
            CandelaToLumens(light.intensity, 4.0 * std::numbers::pi),
            rec_light.luminous_flux_lm);
        build.point_lights.push_back(rec_light);
        break;
      }
      case cgltf_light_type_spot: {
        namespace light_schema = data::schema::spot_light;
        auto rec_light = MakeDefaultRecord<SpotLightRecord>();
        rec_light.node_index = light_node_index;
        rec_light.common = resolve_common(rec_light.common);
        detail::CommitImportedLightColor(
          commit, data::kSpotLightCommonRanges, color, rec_light.common);
        rec_light.range = commit(
          "range", light_schema::kRangeRange, local_range, rec_light.range);
        rec_light.inner_cone_angle_radians = commit("inner_cone_angle_radians",
          light_schema::kInnerConeAngleRadiansRange,
          light.spot_inner_cone_angle, rec_light.inner_cone_angle_radians);
        rec_light.outer_cone_angle_radians = commit("outer_cone_angle_radians",
          light_schema::kOuterConeAngleRadiansRange,
          light.spot_outer_cone_angle, rec_light.outer_cone_angle_radians);
        // The cone pair is a rule across both angles, not a single property.
        const auto cone
          = lighting::ResolveSpotConeProfile(rec_light.inner_cone_angle_radians,
            rec_light.outer_cone_angle_radians);
        if (!cone) {
          diagnostics.push_back(
            MakeWarningDiagnostic("scene.light.photometry_invalid",
              "Spot cone angles do not form a representable cone pair; the "
              "light is dropped",
              input.source_id, name));
          break;
        }
        rec_light.luminous_flux_lm
          = commit("luminous_flux_lm", light_schema::kLuminousFluxLmRange,
            CandelaToLumens(light.intensity, cone->solid_angle_sr),
            rec_light.luminous_flux_lm);
        build.spot_lights.push_back(rec_light);
        break;
      }
      default:
        diagnostics.push_back(MakeWarningDiagnostic("scene.light.unsupported",
          "Unsupported light type", input.source_id, name));
        break;
      }
      commit.Report(input.source_id, name, diagnostics);
    }
  }

  build.nodes.insert(build.nodes.end(), component_attachments.begin(),
    component_attachments.end());
  build.model_nodes = identity.TakePublishedNodes(input.source_id, diagnostics);
  result.build = std::move(build);
  result.success = true;
  return result;
}

auto GltfAdapter::BuildWorkItems(SceneWorkTag /*unused*/,
  SceneWorkItemSink& sink, const AdapterInput& input) -> WorkItemStreamResult
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
    input.request, input.naming_service, input.stop_token);

  if (!sink.Consume(std::move(item))) {
    return result;
  }

  ++result.emitted;
  return result;
}

} // namespace oxygen::content::import::adapters
