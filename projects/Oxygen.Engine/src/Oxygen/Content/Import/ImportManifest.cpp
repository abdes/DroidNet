//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <fstream>
#include <nlohmann/json-schema.hpp>
#include <nlohmann/json.hpp>
#include <sstream>
#include <vector>

#include <Oxygen/Content/Import/ImportManifest.h>
#include <Oxygen/Content/Import/Internal/ImportManifest_schema.h>
#include <Oxygen/Content/Import/Internal/SceneImportRequestBuilder.h>
#include <Oxygen/Content/Import/Internal/TextureImportRequestBuilder.h>

namespace oxygen::content::import {

namespace {

  using nlohmann::json;
  using nlohmann::json_schema::error_handler;
  using nlohmann::json_schema::json_validator;

  class CollectingErrorHandler final : public error_handler {
  public:
    void error(const json::json_pointer& ptr, const json& instance,
      const std::string& message) override
    {
      std::ostringstream out;
      const auto path = ptr.to_string();
      out << (path.empty() ? "<root>" : path) << ": " << message;
      if (!instance.is_discarded()) {
        out << " (value=" << instance.dump() << ")";
      }
      errors_.push_back(out.str());
    }

    [[nodiscard]] auto HasErrors() const noexcept -> bool
    {
      return !errors_.empty();
    }

    [[nodiscard]] auto ToString() const -> std::string
    {
      std::ostringstream out;
      for (const auto& error : errors_) {
        out << "- " << error << "\n";
      }
      return out.str();
    }

  private:
    std::vector<std::string> errors_;
  };

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
        CollectingErrorHandler handler;
        [[maybe_unused]] auto _ = validator_.validate(instance, handler);
        if (handler.HasErrors()) {
          return handler.ToString();
        }
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

  auto ReadUInt16Field(const json& obj, const char* name, uint16_t& target,
    std::ostream& errors) -> bool
  {
    if (!obj.contains(name)) {
      return true;
    }
    uint32_t value = 0U;
    if (!ReadUIntField(obj, name, value, errors)) {
      return false;
    }
    target = static_cast<uint16_t>(value);
    return true;
  }

  auto ReadOptionalUIntField(const json& obj, const char* name,
    std::optional<uint32_t>& target, std::ostream& errors) -> bool
  {
    if (!obj.contains(name)) {
      return true;
    }
    uint32_t value = 0U;
    if (!ReadUIntField(obj, name, value, errors)) {
      return false;
    }
    target = value;
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
    if (obj.contains("sources")) {
      if (!obj["sources"].is_array()) {
        errors << "ERROR: 'sources' must be an array\n";
        return false;
      }
      for (const auto& mapping_json : obj["sources"]) {
        TextureSourceMapping mapping;
        if (!ReadStringField(mapping_json, "file", mapping.file, errors)) {
          return false;
        }
        ReadUInt16Field(mapping_json, "layer", mapping.layer, errors);
        ReadUInt16Field(mapping_json, "mip", mapping.mip, errors);
        ReadUInt16Field(mapping_json, "slice", mapping.slice, errors);
        settings.sources.push_back(std::move(mapping));
      }
    }
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
    if (!ReadStringField(
          obj, "mip_filter_space", settings.mip_filter_space, errors)) {
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
    if (!ReadStringField(obj, "hdr_handling", settings.hdr_handling, errors)) {
      return false;
    }
    bool dummy = false;
    if (!ReadFloatField(
          obj, "exposure_ev", settings.exposure_ev, dummy, errors)) {
      return false;
    }
    if (!ReadUIntField(obj, "max_mips", settings.max_mip_levels, errors)) {
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
    if (!ReadBoolField(
          obj, "flip_normal_green", settings.flip_normal_green, errors)) {
      return false;
    }
    if (!ReadBoolField(
          obj, "renormalize", settings.renormalize_normals, errors)) {
      return false;
    }
    if (!ReadBoolField(obj, "bake_hdr", settings.bake_hdr_to_ldr, errors)) {
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
    ReadBoolField(
      obj, "content_hashing", settings.with_content_hashing, errors);
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

    if (!ReadStringField(obj, "unit_policy", settings.unit_policy, errors)) {
      return false;
    }
    if (!ReadFloatField(obj, "unit_scale", settings.unit_scale,
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
    if (!ReadStringField(obj, "node_pruning", settings.node_pruning, errors)) {
      return false;
    }
    if (!ReadStringField(
          obj, "naming_policy", settings.naming_policy, errors)) {
      return false;
    }

    // Apply texture overrides from the same object (flat structure)
    if (!ApplyTextureOverrides(obj, settings.texture_defaults, errors)) {
      return false;
    }

    if (obj.contains("texture_overrides")) {
      const auto& overrides = obj["texture_overrides"];
      if (!overrides.is_object()) {
        errors << "ERROR: 'texture_overrides' must be an object\n";
        return false;
      }
      for (auto it = overrides.begin(); it != overrides.end(); ++it) {
        // Start with the defaults for this job
        TextureImportSettings tex_settings = settings.texture_defaults;
        if (!ApplyTextureOverrides(it.value(), tex_settings, errors)) {
          return false;
        }
        settings.texture_overrides[it.key()] = std::move(tex_settings);
      }
    }

    return true;
  }

  auto ApplyImportOptions(
    const json& obj, bool& with_content_hashing, std::ostream& errors) -> bool
  {
    return ReadBoolField(obj, "content_hashing", with_content_hashing, errors);
  }

  auto ApplyCommonOverrides(const json& obj, TextureImportSettings& settings,
    std::ostream& errors) -> bool
  {
    if (!ReadStringField(obj, "output", settings.cooked_root, errors)) {
      return false;
    }
    if (!ReadStringField(obj, "name", settings.job_name, errors)) {
      return false;
    }
    if (!ReadBoolField(obj, "verbose", settings.verbose, errors)) {
      return false;
    }
    return true;
  }

  auto ApplyPipelineConcurrency(const json& obj,
    ImportPipelineConcurrency& target, std::ostream& errors) -> bool
  {
    if (!ReadUIntField(obj, "workers", target.workers, errors)) {
      return false;
    }
    if (!ReadUIntField(obj, "queue_capacity", target.queue_capacity, errors)) {
      return false;
    }
    return true;
  }

  auto ApplyConcurrencyOverrides(
    const json& obj, ImportConcurrency& target, std::ostream& errors) -> bool
  {
    if (obj.contains("texture")) {
      if (!obj["texture"].is_object()) {
        errors << "ERROR: concurrency.texture must be an object\n";
        return false;
      }
      if (!ApplyPipelineConcurrency(obj["texture"], target.texture, errors)) {
        return false;
      }
    }
    if (obj.contains("buffer")) {
      if (!obj["buffer"].is_object()) {
        errors << "ERROR: concurrency.buffer must be an object\n";
        return false;
      }
      if (!ApplyPipelineConcurrency(obj["buffer"], target.buffer, errors)) {
        return false;
      }
    }
    if (obj.contains("material")) {
      if (!obj["material"].is_object()) {
        errors << "ERROR: concurrency.material must be an object\n";
        return false;
      }
      if (!ApplyPipelineConcurrency(obj["material"], target.material, errors)) {
        return false;
      }
    }
    const bool has_mesh_build = obj.contains("mesh_build");
    if (has_mesh_build) {
      if (!obj["mesh_build"].is_object()) {
        errors << "ERROR: concurrency.mesh_build must be an object\n";
        return false;
      }
      if (!ApplyPipelineConcurrency(
            obj["mesh_build"], target.mesh_build, errors)) {
        return false;
      }
    }
    if (obj.contains("geometry")) {
      if (!obj["geometry"].is_object()) {
        errors << "ERROR: concurrency.geometry must be an object\n";
        return false;
      }
      if (!ApplyPipelineConcurrency(obj["geometry"], target.geometry, errors)) {
        return false;
      }
    }
    if (obj.contains("scene")) {
      if (!obj["scene"].is_object()) {
        errors << "ERROR: concurrency.scene must be an object\n";
        return false;
      }
      if (!ApplyPipelineConcurrency(obj["scene"], target.scene, errors)) {
        return false;
      }
    }
    return true;
  }

  auto ApplyCommonSceneOverrides(const json& obj, SceneImportSettings& settings,
    std::ostream& errors) -> bool
  {
    if (!ReadStringField(obj, "output", settings.cooked_root, errors)) {
      return false;
    }
    if (!ReadStringField(obj, "name", settings.job_name, errors)) {
      return false;
    }
    if (!ReadBoolField(obj, "verbose", settings.verbose, errors)) {
      return false;
    }
    return true;
  }

} // namespace

auto ImportManifest::Load(const std::filesystem::path& manifest_path,
  const std::optional<std::filesystem::path>& root_override,
  std::ostream& error_stream) -> std::optional<ImportManifest>
{
  const auto json_data = ReadJsonFile(manifest_path, error_stream);
  if (!json_data.has_value()) {
    return std::nullopt;
  }

  if (const auto error = SchemaValidator::Instance().Validate(*json_data);
    error.has_value()) {
    error_stream << "ERROR: manifest schema validation failed:\n" << *error;
    if (!error->empty() && error->back() != '\n') {
      error_stream << "\n";
    }
    return std::nullopt;
  }

  ImportManifest manifest {};
  manifest.version = json_data->value("version", 1U);

  if (!ReadOptionalUIntField(*json_data, "thread_pool_size",
        manifest.thread_pool_size, error_stream)) {
    return std::nullopt;
  }
  if (!ReadOptionalUIntField(*json_data, "max_in_flight_jobs",
        manifest.max_in_flight_jobs, error_stream)) {
    return std::nullopt;
  }
  if (json_data->contains("concurrency")) {
    if (!(*json_data)["concurrency"].is_object()) {
      error_stream << "ERROR: concurrency must be an object\n";
      return std::nullopt;
    }
    ImportConcurrency concurrency {};
    if (!ApplyConcurrencyOverrides(
          (*json_data)["concurrency"], concurrency, error_stream)) {
      return std::nullopt;
    }
    manifest.concurrency = concurrency;
  }

  if (manifest.version != 1U) {
    error_stream << "ERROR: unsupported manifest version: " << manifest.version
                 << "\n";
    return std::nullopt;
  }

  const auto root
    = root_override.has_value() ? *root_override : manifest_path.parent_path();

  if (json_data->contains("defaults")) {
    const auto& defaults = (*json_data)["defaults"];
    if (!defaults.is_object()) {
      error_stream << "ERROR: defaults must be an object\n";
      return std::nullopt;
    }

    if (defaults.contains("texture")) {
      const auto& texture_defaults = defaults["texture"];
      if (!texture_defaults.is_object()) {
        error_stream << "ERROR: defaults.texture must be an object\n";
        return std::nullopt;
      }
      ApplyCommonOverrides(
        texture_defaults, manifest.defaults.texture, error_stream);
      ApplyImportOptions(texture_defaults,
        manifest.defaults.texture.with_content_hashing, error_stream);
      ApplyTextureOverrides(
        texture_defaults, manifest.defaults.texture, error_stream);
    }

    if (defaults.contains("scene")) {
      const auto& scene_defaults = defaults["scene"];
      if (!scene_defaults.is_object()) {
        error_stream << "ERROR: defaults.scene must be an object\n";
        return std::nullopt;
      }
      ApplyCommonSceneOverrides(
        scene_defaults, manifest.defaults.fbx, error_stream);
      ApplyCommonSceneOverrides(
        scene_defaults, manifest.defaults.gltf, error_stream);
      ApplySceneOverrides(scene_defaults, manifest.defaults.fbx, error_stream);
      ApplySceneOverrides(scene_defaults, manifest.defaults.gltf, error_stream);
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
    manifest_job.texture = manifest.defaults.texture;
    manifest_job.fbx = manifest.defaults.fbx;
    manifest_job.gltf = manifest.defaults.gltf;
    manifest_job.fbx.texture_defaults = manifest.defaults.texture;
    manifest_job.gltf.texture_defaults = manifest.defaults.texture;

    if (!job.contains("type") || !job["type"].is_string()) {
      error_stream << "ERROR: job.type is required and must be a string\n";
      return std::nullopt;
    }
    manifest_job.job_type = job["type"].get<std::string>();
    if (manifest_job.job_type.empty()) {
      error_stream << "ERROR: job.type must not be empty\n";
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

    if (!ApplyImportOptions(
          job, manifest_job.texture.with_content_hashing, error_stream)) {
      return std::nullopt;
    }
    if (!ApplySceneOverrides(job, manifest_job.fbx, error_stream)) {
      return std::nullopt;
    }
    if (!ApplySceneOverrides(job, manifest_job.gltf, error_stream)) {
      return std::nullopt;
    }
    if (!ApplyTextureOverrides(job, manifest_job.texture, error_stream)) {
      return std::nullopt;
    }

    manifest.jobs.push_back(std::move(manifest_job));
  }

  return manifest;
}

auto ImportManifestJob::BuildRequest(std::ostream& error_stream) const
  -> std::optional<ImportRequest>
{
  if (job_type == "texture") {
    return internal::BuildTextureRequest(texture, error_stream);
  }
  if (job_type == "fbx") {
    return internal::BuildSceneRequest(fbx, ImportFormat::kFbx, error_stream);
  }
  if (job_type == "gltf") {
    return internal::BuildSceneRequest(gltf, ImportFormat::kGltf, error_stream);
  }
  error_stream << "ERROR: unknown job_type: " << job_type << "\n";
  return std::nullopt;
}

auto ImportManifest::BuildRequests(std::ostream& error_stream) const
  -> std::vector<ImportRequest>
{
  std::vector<ImportRequest> requests;
  requests.reserve(jobs.size());
  for (const auto& job : jobs) {
    if (auto request = job.BuildRequest(error_stream)) {
      requests.push_back(std::move(*request));
    }
  }
  return requests;
}

} // namespace oxygen::content::import
