//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <fstream>
#include <nlohmann/json-schema.hpp>
#include <nlohmann/json.hpp>

#include <Oxygen/Content/Tools/ImportTool/ImportManifest.h>
#include <Oxygen/Content/Tools/ImportTool/ImportManifest_schema.h>

namespace oxygen::content::import::tool {

namespace {

  using nlohmann::json;
  using nlohmann::json_schema::json_validator;

  class SchemaValidator {
  public:
    SchemaValidator()
    {
      auto schema_json = json::parse(kImportManifestSchema);
      validator_.set_root_schema(schema_json);
    }

    auto Validate(const json& instance) const -> std::optional<std::string>
    {
      try {
        [[maybe_unused]] auto _ = validator_.validate(instance);
        return std::nullopt;
      } catch (const std::exception& e) {
        return std::string(e.what());
      }
    }

    static auto Instance() -> const SchemaValidator&
    {
      static SchemaValidator instance;
      return instance;
    }

  private:
    mutable json_validator validator_;
  };

  auto ReadJsonFile(const std::filesystem::path& path,
    std::ostream& error_stream) -> std::optional<json>
  {
    std::ifstream input(path);
    if (!input) {
      error_stream << "ERROR: failed to open manifest: " << path.string()
                   << "\n";
      return std::nullopt;
    }

    try {
      json parsed;
      input >> parsed;
      return parsed;
    } catch (const std::exception& e) {
      error_stream << "ERROR: invalid manifest JSON: " << e.what() << "\n";
      return std::nullopt;
    }
  }

  auto ResolveSourcePath(
    const std::filesystem::path& root, const std::string& source) -> std::string
  {
    std::filesystem::path source_path(source);
    if (source_path.is_absolute()) {
      return source_path.string();
    }
    return (root / source_path).lexically_normal().string();
  }

  auto ReadStringField(const json& obj, const char* name, std::string& target,
    std::ostream& errors) -> bool
  {
    if (!obj.contains(name)) {
      return true;
    }
    if (!obj[name].is_string()) {
      errors << "ERROR: '" << name << "' must be a string\n";
      return false;
    }
    target = obj[name].get<std::string>();
    return true;
  }

  auto ReadBoolField(const json& obj, const char* name, bool& target,
    std::ostream& errors) -> bool
  {
    if (!obj.contains(name)) {
      return true;
    }
    if (!obj[name].is_boolean()) {
      errors << "ERROR: '" << name << "' must be a boolean\n";
      return false;
    }
    target = obj[name].get<bool>();
    return true;
  }

  auto ReadUIntField(const json& obj, const char* name, uint32_t& target,
    std::ostream& errors) -> bool
  {
    if (!obj.contains(name)) {
      return true;
    }
    if (!obj[name].is_number_unsigned() && !obj[name].is_number_integer()) {
      errors << "ERROR: '" << name << "' must be an integer\n";
      return false;
    }
    const auto value = obj[name].get<int64_t>();
    if (value < 0) {
      errors << "ERROR: '" << name << "' must be >= 0\n";
      return false;
    }
    target = static_cast<uint32_t>(value);
    return true;
  }

  auto ReadFloatField(const json& obj, const char* name, float& target,
    bool& was_set, std::ostream& errors) -> bool
  {
    if (!obj.contains(name)) {
      return true;
    }
    if (!obj[name].is_number()) {
      errors << "ERROR: '" << name << "' must be a number\n";
      return false;
    }
    target = obj[name].get<float>();
    was_set = true;
    return true;
  }

  auto ApplyTextureOverrides(const json& obj, TextureImportSettings& settings,
    std::ostream& errors) -> bool
  {
    if (!ReadStringField(obj, "preset", settings.preset, errors)) {
      return false;
    }
    if (!ReadStringField(obj, "intent", settings.intent, errors)) {
      return false;
    }
    if (!ReadStringField(obj, "color_space", settings.color_space, errors)) {
      return false;
    }
    if (!ReadStringField(
          obj, "output_format", settings.output_format, errors)) {
      return false;
    }
    if (!ReadStringField(obj, "data_format", settings.data_format, errors)) {
      return false;
    }
    if (!ReadStringField(obj, "mip_policy", settings.mip_policy, errors)) {
      return false;
    }
    if (!ReadStringField(obj, "mip_filter", settings.mip_filter, errors)) {
      return false;
    }
    if (!ReadStringField(obj, "bc7_quality", settings.bc7_quality, errors)) {
      return false;
    }
    if (!ReadStringField(
          obj, "packing_policy", settings.packing_policy, errors)) {
      return false;
    }
    if (!ReadStringField(obj, "cube_layout", settings.cube_layout, errors)) {
      return false;
    }
    if (!ReadUIntField(
          obj, "max_mip_levels", settings.max_mip_levels, errors)) {
      return false;
    }
    if (!ReadUIntField(
          obj, "cube_face_size", settings.cube_face_size, errors)) {
      return false;
    }
    if (!ReadBoolField(obj, "flip_y", settings.flip_y, errors)) {
      return false;
    }
    if (!ReadBoolField(obj, "force_rgba", settings.force_rgba, errors)) {
      return false;
    }
    if (!ReadBoolField(obj, "cubemap", settings.cubemap, errors)) {
      return false;
    }
    if (!ReadBoolField(
          obj, "equirect_to_cube", settings.equirect_to_cube, errors)) {
      return false;
    }
    return true;
  }

  auto ApplySceneOverrides(const json& obj, SceneImportSettings& settings,
    std::ostream& errors) -> bool
  {
    if (obj.contains("content_flags")) {
      const auto& flags = obj["content_flags"];
      if (!flags.is_object()) {
        errors << "ERROR: content_flags must be an object\n";
        return false;
      }
      ReadBoolField(flags, "textures", settings.import_textures, errors);
      ReadBoolField(flags, "materials", settings.import_materials, errors);
      ReadBoolField(flags, "geometry", settings.import_geometry, errors);
      ReadBoolField(flags, "scene", settings.import_scene, errors);
    }

    if (!ReadStringField(
          obj, "unit_normalization_policy", settings.unit_policy, errors)) {
      return false;
    }
    if (!ReadFloatField(obj, "custom_unit_scale", settings.unit_scale,
          settings.unit_scale_set, errors)) {
      return false;
    }
    if (obj.contains("bake_transforms")) {
      if (!ReadBoolField(
            obj, "bake_transforms", settings.bake_transforms, errors)) {
        return false;
      }
    }
    if (!ReadStringField(
          obj, "normals_policy", settings.normals_policy, errors)) {
      return false;
    }
    if (!ReadStringField(
          obj, "tangents_policy", settings.tangents_policy, errors)) {
      return false;
    }
    if (!ReadStringField(
          obj, "node_pruning_policy", settings.node_pruning, errors)) {
      return false;
    }
    return true;
  }

  auto ApplyCommonOverrides(const json& obj, TextureImportSettings& settings,
    std::ostream& errors) -> bool
  {
    if (!ReadStringField(obj, "cooked_root", settings.cooked_root, errors)) {
      return false;
    }
    if (!ReadStringField(obj, "job_name", settings.job_name, errors)) {
      return false;
    }
    if (!ReadBoolField(obj, "verbose", settings.verbose, errors)) {
      return false;
    }
    return true;
  }

  auto ApplyCommonSceneOverrides(const json& obj, SceneImportSettings& settings,
    std::ostream& errors) -> bool
  {
    if (!ReadStringField(obj, "cooked_root", settings.cooked_root, errors)) {
      return false;
    }
    if (!ReadStringField(obj, "job_name", settings.job_name, errors)) {
      return false;
    }
    if (!ReadBoolField(obj, "verbose", settings.verbose, errors)) {
      return false;
    }
    return true;
  }

} // namespace

auto ImportManifestLoader::Load(const std::filesystem::path& manifest_path,
  const std::optional<std::filesystem::path>& root_override,
  std::ostream& error_stream) -> std::optional<ImportManifest>
{
  const auto json_data = ReadJsonFile(manifest_path, error_stream);
  if (!json_data.has_value()) {
    return std::nullopt;
  }

  if (const auto error = SchemaValidator::Instance().Validate(*json_data);
    error.has_value()) {
    error_stream << "ERROR: manifest schema validation failed: " << *error
                 << "\n";
    return std::nullopt;
  }

  ImportManifest manifest {};
  manifest.version = json_data->value("version", 1U);
  manifest.defaults.job_type = "texture";

  if (manifest.version != 1U) {
    error_stream << "ERROR: unsupported manifest version: " << manifest.version
                 << "\n";
    return std::nullopt;
  }

  const auto root
    = root_override.has_value() ? *root_override : manifest_path.parent_path();

  if (json_data->contains("defaults")) {
    const auto& defaults = (*json_data)["defaults"];
    ReadStringField(
      defaults, "job_type", manifest.defaults.job_type, error_stream);
    ApplyCommonOverrides(defaults, manifest.defaults.texture, error_stream);
    ApplyCommonSceneOverrides(defaults, manifest.defaults.fbx, error_stream);
    ApplyCommonSceneOverrides(defaults, manifest.defaults.gltf, error_stream);
    if (defaults.contains("import_options")) {
      if (!defaults["import_options"].is_object()) {
        error_stream << "ERROR: defaults.import_options must be an object\n";
        return std::nullopt;
      }
      if (!ApplySceneOverrides(
            defaults["import_options"], manifest.defaults.fbx, error_stream)) {
        return std::nullopt;
      }
      if (!ApplySceneOverrides(
            defaults["import_options"], manifest.defaults.gltf, error_stream)) {
        return std::nullopt;
      }
    }
    if (defaults.contains("texture")) {
      if (!defaults["texture"].is_object()) {
        error_stream << "ERROR: defaults.texture must be an object\n";
        return std::nullopt;
      }
      if (!ApplyTextureOverrides(
            defaults["texture"], manifest.defaults.texture, error_stream)) {
        return std::nullopt;
      }
    }
    if (defaults.contains("fbx")) {
      if (!defaults["fbx"].is_object()) {
        error_stream << "ERROR: defaults.fbx must be an object\n";
        return std::nullopt;
      }
      if (!ApplySceneOverrides(
            defaults["fbx"], manifest.defaults.fbx, error_stream)) {
        return std::nullopt;
      }
    }
    if (defaults.contains("gltf")) {
      if (!defaults["gltf"].is_object()) {
        error_stream << "ERROR: defaults.gltf must be an object\n";
        return std::nullopt;
      }
      if (!ApplySceneOverrides(
            defaults["gltf"], manifest.defaults.gltf, error_stream)) {
        return std::nullopt;
      }
    }
  }

  if (!json_data->contains("jobs") || !(*json_data)["jobs"].is_array()) {
    error_stream << "ERROR: manifest.jobs must be an array\n";
    return std::nullopt;
  }

  for (const auto& job : (*json_data)["jobs"]) {
    if (!job.is_object()) {
      error_stream << "ERROR: job entries must be objects\n";
      return std::nullopt;
    }

    ImportManifestJob manifest_job {};
    manifest_job.job_type = manifest.defaults.job_type;
    manifest_job.texture = manifest.defaults.texture;
    manifest_job.fbx = manifest.defaults.fbx;
    manifest_job.gltf = manifest.defaults.gltf;

    if (!ReadStringField(
          job, "job_type", manifest_job.job_type, error_stream)) {
      return std::nullopt;
    }

    if (manifest_job.job_type.empty()) {
      error_stream << "ERROR: job_type must not be empty\n";
      return std::nullopt;
    }

    if (!job.contains("source") || !job["source"].is_string()) {
      error_stream << "ERROR: job.source is required and must be a string\n";
      return std::nullopt;
    }

    const auto source = job["source"].get<std::string>();
    manifest_job.texture.source_path = ResolveSourcePath(root, source);
    manifest_job.fbx.source_path = manifest_job.texture.source_path;
    manifest_job.gltf.source_path = manifest_job.texture.source_path;

    if (!ApplyCommonOverrides(job, manifest_job.texture, error_stream)) {
      return std::nullopt;
    }

    if (!ApplyCommonSceneOverrides(job, manifest_job.fbx, error_stream)) {
      return std::nullopt;
    }

    if (!ApplyCommonSceneOverrides(job, manifest_job.gltf, error_stream)) {
      return std::nullopt;
    }

    if (job.contains("import_options")) {
      if (!job["import_options"].is_object()) {
        error_stream << "ERROR: job.import_options must be an object\n";
        return std::nullopt;
      }
      if (!ApplySceneOverrides(
            job["import_options"], manifest_job.fbx, error_stream)) {
        return std::nullopt;
      }
      if (!ApplySceneOverrides(
            job["import_options"], manifest_job.gltf, error_stream)) {
        return std::nullopt;
      }
    }

    if (job.contains("texture")) {
      if (!job["texture"].is_object()) {
        error_stream << "ERROR: job.texture must be an object\n";
        return std::nullopt;
      }
      if (!ApplyTextureOverrides(
            job["texture"], manifest_job.texture, error_stream)) {
        return std::nullopt;
      }
    }

    if (job.contains("fbx")) {
      if (!job["fbx"].is_object()) {
        error_stream << "ERROR: job.fbx must be an object\n";
        return std::nullopt;
      }
      if (!ApplySceneOverrides(job["fbx"], manifest_job.fbx, error_stream)) {
        return std::nullopt;
      }
    }

    if (job.contains("gltf")) {
      if (!job["gltf"].is_object()) {
        error_stream << "ERROR: job.gltf must be an object\n";
        return std::nullopt;
      }
      if (!ApplySceneOverrides(job["gltf"], manifest_job.gltf, error_stream)) {
        return std::nullopt;
      }
    }

    manifest.jobs.push_back(std::move(manifest_job));
  }

  return manifest;
}

} // namespace oxygen::content::import::tool
