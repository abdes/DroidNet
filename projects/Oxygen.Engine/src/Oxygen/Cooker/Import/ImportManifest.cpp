//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <algorithm>
#include <fstream>
#include <nlohmann/json-schema.hpp>
#include <nlohmann/json.hpp>
#include <sstream>
#include <vector>

#include <Oxygen/Cooker/Import/BufferContainerImportRequestBuilder.h>
#include <Oxygen/Cooker/Import/CollisionShapeDescriptorImportRequestBuilder.h>
#include <Oxygen/Cooker/Import/GeometryDescriptorImportRequestBuilder.h>
#include <Oxygen/Cooker/Import/ImportManifest.h>
#include <Oxygen/Cooker/Import/InputImportRequestBuilder.h>
#include <Oxygen/Cooker/Import/Internal/ImportManifest_schema.h>
#include <Oxygen/Cooker/Import/Internal/SceneImportRequestBuilder.h>
#include <Oxygen/Cooker/Import/Internal/TextureImportRequestBuilder.h>
#include <Oxygen/Cooker/Import/MaterialDescriptorImportRequestBuilder.h>
#include <Oxygen/Cooker/Import/PhysicsImportRequestBuilder.h>
#include <Oxygen/Cooker/Import/PhysicsMaterialDescriptorImportRequestBuilder.h>
#include <Oxygen/Cooker/Import/SceneDescriptorImportRequestBuilder.h>
#include <Oxygen/Cooker/Import/ScriptImportRequestBuilder.h>
#include <Oxygen/Cooker/Import/TextureDescriptorImportRequestBuilder.h>

namespace oxygen::content::import {

namespace {

  using nlohmann::json;
  using nlohmann::json_schema::error_handler;
  using nlohmann::json_schema::json_validator;

  class CollectingErrorHandler final : public error_handler {
  public:
    static constexpr size_t kMaxErrors = 12;

    void error(const json::json_pointer& ptr, const json& instance,
      const std::string& message) override
    {
      std::ostringstream out;
      const auto path = ptr.to_string();
      out << (path.empty() ? "<root>" : path) << ": " << message;
      if (!instance.is_discarded() && instance.is_primitive()) {
        out << " (value=" << instance.dump() << ")";
      }

      if (errors_.size() < kMaxErrors) {
        errors_.push_back(out.str());
      }
      ++total_errors_;
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
      if (total_errors_ > errors_.size()) {
        out << "- ... " << (total_errors_ - errors_.size())
            << " additional schema error(s) suppressed\n";
      }
      return out.str();
    }

  private:
    std::vector<std::string> errors_;
    size_t total_errors_ = 0;
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

  auto ReadStringArrayField(const json& obj, const char* name,
    std::vector<std::string>& target, std::ostream& errors) -> bool
  {
    if (!obj.contains(name)) {
      return true;
    }
    if (!obj[name].is_array()) {
      errors << "ERROR: '" << name << "' must be an array\n";
      return false;
    }
    target.clear();
    target.reserve(obj[name].size());
    for (size_t i = 0; i < obj[name].size(); ++i) {
      if (!obj[name][i].is_string()) {
        errors << "ERROR: '" << name << "[" << i << "]' must be a string\n";
        return false;
      }
      target.push_back(obj[name][i].get<std::string>());
    }
    return true;
  }

  auto TrimInPlace(std::string& value) -> void
  {
    const auto is_ws = [](const unsigned char ch) {
      return ch == ' ' || ch == '\t' || ch == '\r' || ch == '\n';
    };
    const auto first = std::find_if(value.begin(), value.end(),
      [&](const char ch) { return !is_ws(static_cast<unsigned char>(ch)); });
    if (first == value.end()) {
      value.clear();
      return;
    }
    const auto last = std::find_if(value.rbegin(), value.rend(),
      [&](const char ch) { return !is_ws(static_cast<unsigned char>(ch)); });
    value = std::string(first, last.base());
  }

  auto CollectJobDependencies(const json& job, const std::string_view job_type,
    const bool require_job_id, std::string& job_id,
    std::vector<std::string>& depends_on, std::ostream& errors) -> bool
  {
    if (!ReadStringField(job, "id", job_id, errors)) {
      return false;
    }
    TrimInPlace(job_id);
    if (require_job_id && job_id.empty()) {
      if (job_type == "input") {
        errors << "ERROR [input.manifest.job_id_missing]: input job.id is "
                  "required and must be a string\n";
      } else {
        errors << "ERROR: " << job_type
               << " job.id is required and must be a string\n";
      }
      return false;
    }

    if (!ReadStringArrayField(job, "depends_on", depends_on, errors)) {
      return false;
    }
    for (size_t dep_index = 0; dep_index < depends_on.size(); ++dep_index) {
      auto& dep = depends_on[dep_index];
      TrimInPlace(dep);
      if (dep.empty()) {
        errors << "ERROR: " << job_type << " job.depends_on[" << dep_index
               << "] must not be empty\n";
        return false;
      }
    }
    return true;
  }

  auto IsAllowedInputJobKey(const std::string_view key) -> bool
  {
    return key == "id" || key == "type" || key == "source"
      || key == "depends_on" || key == "output";
  }

  auto IsAllowedPhysicsSidecarJobKey(const std::string_view key) -> bool
  {
    return key == "id" || key == "depends_on" || key == "type"
      || key == "source" || key == "bindings"
      || key == "target_scene_virtual_path" || key == "output" || key == "name"
      || key == "verbose" || key == "content_hashing"
      || key == "physics_backend";
  }

  auto ReportEarlyJobKeyWhitelistViolations(
    const json& manifest, std::ostream& errors) -> bool
  {
    if (!manifest.contains("jobs") || !manifest["jobs"].is_array()) {
      return false;
    }

    for (const auto& job : manifest["jobs"]) {
      if (!job.is_object()) {
        continue;
      }
      if (!job.contains("type") || !job["type"].is_string()) {
        continue;
      }

      const auto job_type = job["type"].get<std::string>();
      if (job_type == "input") {
        for (auto it = job.begin(); it != job.end(); ++it) {
          if (!IsAllowedInputJobKey(it.key())) {
            errors << "ERROR [input.manifest.key_not_allowed]: key '"
                   << it.key()
                   << "' is not allowed for input jobs; allowed keys are "
                      "'id', 'type', 'source', 'depends_on', 'output'\n";
            return true;
          }
        }
        if (!job.contains("id") || !job["id"].is_string()) {
          errors << "ERROR [input.manifest.job_id_missing]: input job.id "
                    "is required and must be a string\n";
          return true;
        }
        auto job_id = job["id"].get<std::string>();
        TrimInPlace(job_id);
        if (job_id.empty()) {
          errors << "ERROR [input.manifest.job_id_missing]: input job.id "
                    "must not be empty\n";
          return true;
        }
      } else if (job_type == "physics-sidecar") {
        for (auto it = job.begin(); it != job.end(); ++it) {
          if (!IsAllowedPhysicsSidecarJobKey(it.key())) {
            errors << "ERROR [physics.manifest.key_not_allowed]: key '"
                   << it.key() << "' is not allowed for physics-sidecar jobs\n";
            return true;
          }
        }
      }

      if (job_type == "script-sidecar" || job_type == "physics-sidecar") {
        const bool has_source = job.contains("source");
        const bool has_bindings = job.contains("bindings");
        if (has_source && has_bindings) {
          errors << "ERROR: " << job_type
                 << " job requires exactly one of 'source' or 'bindings'\n";
          return true;
        }
      }
    }

    return false;
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

  auto ApplyScriptAssetOverrides(const json& obj,
    ScriptAssetImportSettings& settings, std::ostream& errors) -> bool
  {
    if (!ReadBoolField(obj, "compile", settings.compile_scripts, errors)) {
      return false;
    }
    if (!ReadStringField(obj, "compile_mode", settings.compile_mode, errors)) {
      return false;
    }
    if (!ReadStringField(
          obj, "script_storage", settings.script_storage, errors)) {
      return false;
    }
    return true;
  }

  auto ApplyScriptingSidecarOverrides(const json& obj,
    ScriptingSidecarImportSettings& settings, std::ostream& errors) -> bool
  {
    if (!ReadStringField(obj, "target_scene_virtual_path",
          settings.target_scene_virtual_path, errors)) {
      return false;
    }
    return true;
  }

  auto ApplyPhysicsSidecarOverrides(const json& obj,
    PhysicsSidecarImportSettings& settings, std::ostream& errors) -> bool
  {
    if (!ReadStringField(obj, "target_scene_virtual_path",
          settings.target_scene_virtual_path, errors)) {
      return false;
    }
    auto backend_text = std::string {};
    if (!ReadStringField(obj, "physics_backend", backend_text, errors)) {
      return false;
    }
    if (!backend_text.empty()) {
      if (backend_text == "jolt") {
        settings.physics_backend = core::meta::physics::PhysicsBackend::kJolt;
      } else if (backend_text == "physx") {
        settings.physics_backend = core::meta::physics::PhysicsBackend::kPhysX;
      } else if (backend_text == "none") {
        settings.physics_backend = core::meta::physics::PhysicsBackend::kNone;
      } else {
        errors << "ERROR: 'physics_backend' must be one of "
                  "'jolt'|'physx'|'none'\n";
        return false;
      }
    }
    return true;
  }

  auto ApplyLayoutOverrides(
    const json& obj, LooseCookedLayout& layout, std::ostream& errors) -> bool
  {
    if (!ReadStringField(
          obj, "virtual_mount_root", layout.virtual_mount_root, errors)) {
      return false;
    }
    if (!ReadStringField(obj, "resources_dir", layout.resources_dir, errors)) {
      return false;
    }
    if (!ReadStringField(
          obj, "descriptors_dir", layout.descriptors_dir, errors)) {
      return false;
    }
    if (!ReadStringField(obj, "scenes_subdir", layout.scenes_subdir, errors)) {
      return false;
    }
    if (!ReadStringField(
          obj, "geometry_subdir", layout.geometry_subdir, errors)) {
      return false;
    }
    if (!ReadStringField(
          obj, "materials_subdir", layout.materials_subdir, errors)) {
      return false;
    }
    if (!ReadStringField(
          obj, "scripts_subdir", layout.scripts_subdir, errors)) {
      return false;
    }
    if (!ReadStringField(obj, "input_subdir", layout.input_subdir, errors)) {
      return false;
    }
    if (!ReadStringField(obj, "texture_descriptors_subdir",
          layout.texture_descriptors_subdir, errors)) {
      return false;
    }
    if (!ReadStringField(obj, "buffer_descriptors_subdir",
          layout.buffer_descriptors_subdir, errors)) {
      return false;
    }
    return true;
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

  auto ApplyCommonScriptOverrides(const json& obj,
    ScriptAssetImportSettings& settings, std::ostream& errors) -> bool
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

  auto ApplyCommonScriptOverrides(const json& obj,
    ScriptingSidecarImportSettings& settings, std::ostream& errors) -> bool
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

  auto ApplyCommonScriptOverrides(const json& obj,
    PhysicsSidecarImportSettings& settings, std::ostream& errors) -> bool
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

  auto ApplyCommonInputOverrides(const json& obj, InputImportSettings& settings,
    std::ostream& errors) -> bool
  {
    return ReadStringField(obj, "output", settings.cooked_root, errors);
  }

  auto ApplyCommonMaterialDescriptorOverrides(const json& obj,
    MaterialDescriptorImportSettings& settings, std::ostream& errors) -> bool
  {
    if (!ReadStringField(obj, "output", settings.cooked_root, errors)) {
      return false;
    }
    if (!ReadStringField(obj, "name", settings.job_name, errors)) {
      return false;
    }
    return true;
  }

  auto ApplyMaterialDescriptorOverrides(const json& obj,
    MaterialDescriptorImportSettings& settings, std::ostream& errors) -> bool
  {
    return ReadBoolField(
      obj, "content_hashing", settings.with_content_hashing, errors);
  }

  auto ApplyCommonPhysicsMaterialDescriptorOverrides(const json& obj,
    PhysicsMaterialDescriptorImportSettings& settings, std::ostream& errors)
    -> bool
  {
    if (!ReadStringField(obj, "output", settings.cooked_root, errors)) {
      return false;
    }
    if (!ReadStringField(obj, "name", settings.job_name, errors)) {
      return false;
    }
    return true;
  }

  auto ApplyPhysicsMaterialDescriptorOverrides(const json& obj,
    PhysicsMaterialDescriptorImportSettings& settings, std::ostream& errors)
    -> bool
  {
    return ReadBoolField(
      obj, "content_hashing", settings.with_content_hashing, errors);
  }

  auto ApplyCommonCollisionShapeDescriptorOverrides(const json& obj,
    CollisionShapeDescriptorImportSettings& settings, std::ostream& errors)
    -> bool
  {
    if (!ReadStringField(obj, "output", settings.cooked_root, errors)) {
      return false;
    }
    if (!ReadStringField(obj, "name", settings.job_name, errors)) {
      return false;
    }
    return true;
  }

  auto ApplyCollisionShapeDescriptorOverrides(const json& obj,
    CollisionShapeDescriptorImportSettings& settings, std::ostream& errors)
    -> bool
  {
    return ReadBoolField(
      obj, "content_hashing", settings.with_content_hashing, errors);
  }

  auto ApplyCommonBufferContainerOverrides(const json& obj,
    BufferContainerImportSettings& settings, std::ostream& errors) -> bool
  {
    if (!ReadStringField(obj, "output", settings.cooked_root, errors)) {
      return false;
    }
    if (!ReadStringField(obj, "name", settings.job_name, errors)) {
      return false;
    }
    return true;
  }

  auto ApplyBufferContainerOverrides(const json& obj,
    BufferContainerImportSettings& settings, std::ostream& errors) -> bool
  {
    return ReadBoolField(
      obj, "content_hashing", settings.with_content_hashing, errors);
  }

  auto ApplyCommonGeometryDescriptorOverrides(const json& obj,
    GeometryDescriptorImportSettings& settings, std::ostream& errors) -> bool
  {
    if (!ReadStringField(obj, "output", settings.cooked_root, errors)) {
      return false;
    }
    if (!ReadStringField(obj, "name", settings.job_name, errors)) {
      return false;
    }
    return true;
  }

  auto ApplyGeometryDescriptorOverrides(const json& obj,
    GeometryDescriptorImportSettings& settings, std::ostream& errors) -> bool
  {
    return ReadBoolField(
      obj, "content_hashing", settings.with_content_hashing, errors);
  }

  auto ApplyCommonSceneDescriptorOverrides(const json& obj,
    SceneDescriptorImportSettings& settings, std::ostream& errors) -> bool
  {
    if (!ReadStringField(obj, "output", settings.cooked_root, errors)) {
      return false;
    }
    if (!ReadStringField(obj, "name", settings.job_name, errors)) {
      return false;
    }
    return true;
  }

  auto ApplySceneDescriptorOverrides(const json& obj,
    SceneDescriptorImportSettings& settings, std::ostream& errors) -> bool
  {
    return ReadBoolField(
      obj, "content_hashing", settings.with_content_hashing, errors);
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

  if (ReportEarlyJobKeyWhitelistViolations(*json_data, error_stream)) {
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

  if (json_data->contains("layout")) {
    const auto& layout = (*json_data)["layout"];
    if (!layout.is_object()) {
      error_stream << "ERROR: layout must be an object\n";
      return std::nullopt;
    }
    if (!ApplyLayoutOverrides(
          layout, manifest.defaults.loose_cooked_layout, error_stream)) {
      return std::nullopt;
    }
  }

  const auto root
    = root_override.has_value() ? *root_override : manifest_path.parent_path();

  auto manifest_output_root = std::string {};
  if (!ReadStringField(
        *json_data, "output", manifest_output_root, error_stream)) {
    return std::nullopt;
  }
  if (!manifest_output_root.empty()) {
    manifest.defaults.texture.cooked_root = manifest_output_root;
    manifest.defaults.fbx.cooked_root = manifest_output_root;
    manifest.defaults.gltf.cooked_root = manifest_output_root;
    manifest.defaults.script.cooked_root = manifest_output_root;
    manifest.defaults.scripting_sidecar.cooked_root = manifest_output_root;
    manifest.defaults.physics_sidecar.cooked_root = manifest_output_root;
    manifest.defaults.input.cooked_root = manifest_output_root;
    manifest.defaults.buffer_container.cooked_root = manifest_output_root;
    manifest.defaults.material_descriptor.cooked_root = manifest_output_root;
    manifest.defaults.physics_material_descriptor.cooked_root
      = manifest_output_root;
    manifest.defaults.collision_shape_descriptor.cooked_root
      = manifest_output_root;
    manifest.defaults.geometry_descriptor.cooked_root = manifest_output_root;
    manifest.defaults.scene_descriptor.cooked_root = manifest_output_root;
  }

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
      if (!ApplyCommonOverrides(
            texture_defaults, manifest.defaults.texture, error_stream)) {
        return std::nullopt;
      }
      if (!ApplyImportOptions(texture_defaults,
            manifest.defaults.texture.with_content_hashing, error_stream)) {
        return std::nullopt;
      }
      if (!ApplyTextureOverrides(
            texture_defaults, manifest.defaults.texture, error_stream)) {
        return std::nullopt;
      }
    }

    if (defaults.contains("scene")) {
      const auto& scene_defaults = defaults["scene"];
      if (!scene_defaults.is_object()) {
        error_stream << "ERROR: defaults.scene must be an object\n";
        return std::nullopt;
      }
      if (!ApplyCommonSceneOverrides(
            scene_defaults, manifest.defaults.fbx, error_stream)) {
        return std::nullopt;
      }
      if (!ApplyCommonSceneOverrides(
            scene_defaults, manifest.defaults.gltf, error_stream)) {
        return std::nullopt;
      }
      if (!ApplySceneOverrides(
            scene_defaults, manifest.defaults.fbx, error_stream)) {
        return std::nullopt;
      }
      if (!ApplySceneOverrides(
            scene_defaults, manifest.defaults.gltf, error_stream)) {
        return std::nullopt;
      }
    }

    if (defaults.contains("script")) {
      const auto& script_defaults = defaults["script"];
      if (!script_defaults.is_object()) {
        error_stream << "ERROR: defaults.script must be an object\n";
        return std::nullopt;
      }
      if (!ApplyCommonScriptOverrides(
            script_defaults, manifest.defaults.script, error_stream)) {
        return std::nullopt;
      }
      if (!ApplyImportOptions(script_defaults,
            manifest.defaults.script.with_content_hashing, error_stream)) {
        return std::nullopt;
      }
      if (!ApplyScriptAssetOverrides(
            script_defaults, manifest.defaults.script, error_stream)) {
        return std::nullopt;
      }
    }

    if (defaults.contains("scripting_sidecar")) {
      const auto& sidecar_defaults = defaults["scripting_sidecar"];
      if (!sidecar_defaults.is_object()) {
        error_stream << "ERROR: defaults.scripting_sidecar must be an object\n";
        return std::nullopt;
      }
      if (!ApplyCommonScriptOverrides(sidecar_defaults,
            manifest.defaults.scripting_sidecar, error_stream)) {
        return std::nullopt;
      }
      if (!ApplyImportOptions(sidecar_defaults,
            manifest.defaults.scripting_sidecar.with_content_hashing,
            error_stream)) {
        return std::nullopt;
      }
      if (!ApplyScriptingSidecarOverrides(sidecar_defaults,
            manifest.defaults.scripting_sidecar, error_stream)) {
        return std::nullopt;
      }
    }

    if (defaults.contains("physics_sidecar")) {
      const auto& sidecar_defaults = defaults["physics_sidecar"];
      if (!sidecar_defaults.is_object()) {
        error_stream << "ERROR: defaults.physics_sidecar must be an object\n";
        return std::nullopt;
      }
      if (!ApplyCommonScriptOverrides(sidecar_defaults,
            manifest.defaults.physics_sidecar, error_stream)) {
        return std::nullopt;
      }
      if (!ApplyImportOptions(sidecar_defaults,
            manifest.defaults.physics_sidecar.with_content_hashing,
            error_stream)) {
        return std::nullopt;
      }
      if (!ApplyPhysicsSidecarOverrides(sidecar_defaults,
            manifest.defaults.physics_sidecar, error_stream)) {
        return std::nullopt;
      }
    }

    if (defaults.contains("input")) {
      const auto& input_defaults = defaults["input"];
      if (!input_defaults.is_object()) {
        error_stream << "ERROR: defaults.input must be an object\n";
        return std::nullopt;
      }
      if (!ApplyCommonInputOverrides(
            input_defaults, manifest.defaults.input, error_stream)) {
        return std::nullopt;
      }
    }

    if (defaults.contains("layout")) {
      const auto& layout_defaults = defaults["layout"];
      if (!layout_defaults.is_object()) {
        error_stream << "ERROR: defaults.layout must be an object\n";
        return std::nullopt;
      }
      if (!ApplyLayoutOverrides(layout_defaults,
            manifest.defaults.loose_cooked_layout, error_stream)) {
        return std::nullopt;
      }
    }

    if (defaults.contains("material_descriptor")) {
      const auto& material_defaults = defaults["material_descriptor"];
      if (!material_defaults.is_object()) {
        error_stream
          << "ERROR: defaults.material_descriptor must be an object\n";
        return std::nullopt;
      }
      if (!ApplyCommonMaterialDescriptorOverrides(material_defaults,
            manifest.defaults.material_descriptor, error_stream)) {
        return std::nullopt;
      }
      if (!ApplyMaterialDescriptorOverrides(material_defaults,
            manifest.defaults.material_descriptor, error_stream)) {
        return std::nullopt;
      }
    }

    if (defaults.contains("physics_material_descriptor")) {
      const auto& material_defaults = defaults["physics_material_descriptor"];
      if (!material_defaults.is_object()) {
        error_stream
          << "ERROR: defaults.physics_material_descriptor must be an object\n";
        return std::nullopt;
      }
      if (!ApplyCommonPhysicsMaterialDescriptorOverrides(material_defaults,
            manifest.defaults.physics_material_descriptor, error_stream)) {
        return std::nullopt;
      }
      if (!ApplyPhysicsMaterialDescriptorOverrides(material_defaults,
            manifest.defaults.physics_material_descriptor, error_stream)) {
        return std::nullopt;
      }
    }

    if (defaults.contains("collision_shape_descriptor")) {
      const auto& shape_defaults = defaults["collision_shape_descriptor"];
      if (!shape_defaults.is_object()) {
        error_stream
          << "ERROR: defaults.collision_shape_descriptor must be an object\n";
        return std::nullopt;
      }
      if (!ApplyCommonCollisionShapeDescriptorOverrides(shape_defaults,
            manifest.defaults.collision_shape_descriptor, error_stream)) {
        return std::nullopt;
      }
      if (!ApplyCollisionShapeDescriptorOverrides(shape_defaults,
            manifest.defaults.collision_shape_descriptor, error_stream)) {
        return std::nullopt;
      }
    }

    if (defaults.contains("geometry_descriptor")) {
      const auto& geometry_defaults = defaults["geometry_descriptor"];
      if (!geometry_defaults.is_object()) {
        error_stream
          << "ERROR: defaults.geometry_descriptor must be an object\n";
        return std::nullopt;
      }
      if (!ApplyCommonGeometryDescriptorOverrides(geometry_defaults,
            manifest.defaults.geometry_descriptor, error_stream)) {
        return std::nullopt;
      }
      if (!ApplyGeometryDescriptorOverrides(geometry_defaults,
            manifest.defaults.geometry_descriptor, error_stream)) {
        return std::nullopt;
      }
    }

    if (defaults.contains("scene_descriptor")) {
      const auto& scene_descriptor_defaults = defaults["scene_descriptor"];
      if (!scene_descriptor_defaults.is_object()) {
        error_stream << "ERROR: defaults.scene_descriptor must be an object\n";
        return std::nullopt;
      }
      if (!ApplyCommonSceneDescriptorOverrides(scene_descriptor_defaults,
            manifest.defaults.scene_descriptor, error_stream)) {
        return std::nullopt;
      }
      if (!ApplySceneDescriptorOverrides(scene_descriptor_defaults,
            manifest.defaults.scene_descriptor, error_stream)) {
        return std::nullopt;
      }
    }

    if (defaults.contains("buffer_container")) {
      const auto& buffer_container_defaults = defaults["buffer_container"];
      if (!buffer_container_defaults.is_object()) {
        error_stream << "ERROR: defaults.buffer_container must be an object\n";
        return std::nullopt;
      }
      if (!ApplyCommonBufferContainerOverrides(buffer_container_defaults,
            manifest.defaults.buffer_container, error_stream)) {
        return std::nullopt;
      }
      if (!ApplyBufferContainerOverrides(buffer_container_defaults,
            manifest.defaults.buffer_container, error_stream)) {
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
    manifest_job.loose_cooked_layout = manifest.defaults.loose_cooked_layout;
    manifest_job.texture = manifest.defaults.texture;
    manifest_job.fbx = manifest.defaults.fbx;
    manifest_job.gltf = manifest.defaults.gltf;
    manifest_job.script = manifest.defaults.script;
    manifest_job.scripting_sidecar = manifest.defaults.scripting_sidecar;
    manifest_job.physics_sidecar = manifest.defaults.physics_sidecar;
    manifest_job.input = manifest.defaults.input;
    manifest_job.buffer_container = manifest.defaults.buffer_container;
    manifest_job.material_descriptor = manifest.defaults.material_descriptor;
    manifest_job.physics_material_descriptor
      = manifest.defaults.physics_material_descriptor;
    manifest_job.collision_shape_descriptor
      = manifest.defaults.collision_shape_descriptor;
    manifest_job.geometry_descriptor = manifest.defaults.geometry_descriptor;
    manifest_job.scene_descriptor = manifest.defaults.scene_descriptor;
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

    if (manifest_job.job_type != "input") {
      if (!CollectJobDependencies(job, manifest_job.job_type, false,
            manifest_job.id, manifest_job.depends_on, error_stream)) {
        return std::nullopt;
      }
    }

    if (manifest_job.job_type == "input") {
      for (auto it = job.begin(); it != job.end(); ++it) {
        if (!IsAllowedInputJobKey(it.key())) {
          error_stream << "ERROR [input.manifest.key_not_allowed]: key '"
                       << it.key()
                       << "' is not allowed for input jobs; allowed keys are "
                          "'id', 'type', 'source', 'depends_on', 'output'\n";
          return std::nullopt;
        }
      }

      if (!job.contains("source") || !job["source"].is_string()) {
        error_stream << "ERROR: input job.source is required and must be a "
                        "string\n";
        return std::nullopt;
      }
      if (!job.contains("id") || !job["id"].is_string()) {
        error_stream << "ERROR [input.manifest.job_id_missing]: input job.id "
                        "is required and must be a string\n";
        return std::nullopt;
      }

      manifest_job.input.source_path
        = ResolveSourcePath(root, job["source"].get<std::string>());
      if (!ApplyCommonInputOverrides(job, manifest_job.input, error_stream)) {
        return std::nullopt;
      }
      if (!CollectJobDependencies(job, "input", true, manifest_job.id,
            manifest_job.depends_on, error_stream)) {
        return std::nullopt;
      }

      manifest.jobs.push_back(std::move(manifest_job));
      continue;
    }

    if (manifest_job.job_type == "physics-sidecar") {
      for (auto it = job.begin(); it != job.end(); ++it) {
        if (!IsAllowedPhysicsSidecarJobKey(it.key())) {
          error_stream << "ERROR [physics.manifest.key_not_allowed]: key '"
                       << it.key() << "' is not allowed for physics-sidecar "
                       << "jobs\n";
          return std::nullopt;
        }
      }
    }

    const auto has_source = job.contains("source");
    const auto has_bindings = job.contains("bindings");
    const auto is_script_sidecar_job
      = manifest_job.job_type == "script-sidecar";
    const auto is_physics_sidecar_job
      = manifest_job.job_type == "physics-sidecar";
    const auto is_sidecar_job = is_script_sidecar_job || is_physics_sidecar_job;

    if (has_bindings && !is_sidecar_job) {
      error_stream << "ERROR: job.bindings is only valid for type "
                      "'script-sidecar' or 'physics-sidecar'\n";
      return std::nullopt;
    }

    if (!has_source && !has_bindings) {
      if (is_script_sidecar_job) {
        error_stream << "ERROR: script-sidecar job requires 'source' or "
                        "'bindings'\n";
      } else if (is_physics_sidecar_job) {
        error_stream << "ERROR: physics-sidecar job requires 'source' or "
                        "'bindings'\n";
      } else {
        error_stream << "ERROR: job.source is required and must be a string\n";
      }
      return std::nullopt;
    }

    if (has_source && !job["source"].is_string()) {
      error_stream << "ERROR: job.source must be a string\n";
      return std::nullopt;
    }

    if (is_sidecar_job && (has_source == has_bindings)) {
      error_stream << "ERROR: "
                   << (is_script_sidecar_job ? "script-sidecar"
                                             : "physics-sidecar")
                   << " job requires exactly one of 'source' or 'bindings'\n";
      return std::nullopt;
    }

    if (has_bindings) {
      if (is_script_sidecar_job && !job["bindings"].is_array()) {
        error_stream << "ERROR: script-sidecar job.bindings must be an array\n";
        return std::nullopt;
      }
      if (is_physics_sidecar_job && !job["bindings"].is_object()) {
        error_stream
          << "ERROR: physics-sidecar job.bindings must be an object\n";
        return std::nullopt;
      }
    }

    if (has_source) {
      const auto source = job["source"].get<std::string>();
      manifest_job.texture.source_path = ResolveSourcePath(root, source);
      manifest_job.fbx.source_path = manifest_job.texture.source_path;
      manifest_job.gltf.source_path = manifest_job.texture.source_path;
      manifest_job.script.source_path = manifest_job.texture.source_path;
      manifest_job.scripting_sidecar.source_path
        = manifest_job.texture.source_path;
      manifest_job.physics_sidecar.source_path
        = manifest_job.texture.source_path;
      manifest_job.buffer_container.descriptor_path
        = manifest_job.texture.source_path;
      manifest_job.material_descriptor.descriptor_path
        = manifest_job.texture.source_path;
      manifest_job.physics_material_descriptor.descriptor_path
        = manifest_job.texture.source_path;
      manifest_job.collision_shape_descriptor.descriptor_path
        = manifest_job.texture.source_path;
      manifest_job.geometry_descriptor.descriptor_path
        = manifest_job.texture.source_path;
      manifest_job.scene_descriptor.descriptor_path
        = manifest_job.texture.source_path;
    } else {
      manifest_job.texture.source_path.clear();
      manifest_job.fbx.source_path.clear();
      manifest_job.gltf.source_path.clear();
      manifest_job.script.source_path.clear();
      manifest_job.scripting_sidecar.source_path.clear();
      manifest_job.physics_sidecar.source_path.clear();
      manifest_job.buffer_container.descriptor_path.clear();
      manifest_job.material_descriptor.descriptor_path.clear();
      manifest_job.physics_material_descriptor.descriptor_path.clear();
      manifest_job.collision_shape_descriptor.descriptor_path.clear();
      manifest_job.geometry_descriptor.descriptor_path.clear();
      manifest_job.scene_descriptor.descriptor_path.clear();
    }

    if (has_bindings && is_script_sidecar_job) {
      auto sidecar_doc = json::object();
      sidecar_doc["bindings"] = job["bindings"];
      manifest_job.scripting_sidecar.inline_bindings_json = sidecar_doc.dump();
      manifest_job.physics_sidecar.inline_bindings_json.clear();
    } else if (has_bindings && is_physics_sidecar_job) {
      auto sidecar_doc = json::object();
      sidecar_doc["bindings"] = job["bindings"];
      manifest_job.physics_sidecar.inline_bindings_json = sidecar_doc.dump();
      manifest_job.scripting_sidecar.inline_bindings_json.clear();
    } else {
      manifest_job.scripting_sidecar.inline_bindings_json.clear();
      manifest_job.physics_sidecar.inline_bindings_json.clear();
    }

    if (!ApplyCommonOverrides(job, manifest_job.texture, error_stream)) {
      return std::nullopt;
    }

    if (!ApplyCommonSceneOverrides(job, manifest_job.fbx, error_stream)) {
      return std::nullopt;
    }

    if (!ApplyCommonSceneOverrides(job, manifest_job.gltf, error_stream)) {
      return std::nullopt;
    }
    if (!ApplyCommonScriptOverrides(job, manifest_job.script, error_stream)) {
      return std::nullopt;
    }
    if (!ApplyCommonScriptOverrides(
          job, manifest_job.scripting_sidecar, error_stream)) {
      return std::nullopt;
    }
    if (!ApplyCommonScriptOverrides(
          job, manifest_job.physics_sidecar, error_stream)) {
      return std::nullopt;
    }
    if (!ApplyCommonMaterialDescriptorOverrides(
          job, manifest_job.material_descriptor, error_stream)) {
      return std::nullopt;
    }
    if (!ApplyCommonPhysicsMaterialDescriptorOverrides(
          job, manifest_job.physics_material_descriptor, error_stream)) {
      return std::nullopt;
    }
    if (!ApplyCommonCollisionShapeDescriptorOverrides(
          job, manifest_job.collision_shape_descriptor, error_stream)) {
      return std::nullopt;
    }
    if (!ApplyCommonBufferContainerOverrides(
          job, manifest_job.buffer_container, error_stream)) {
      return std::nullopt;
    }
    if (!ApplyCommonGeometryDescriptorOverrides(
          job, manifest_job.geometry_descriptor, error_stream)) {
      return std::nullopt;
    }
    if (!ApplyCommonSceneDescriptorOverrides(
          job, manifest_job.scene_descriptor, error_stream)) {
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
    if (!ApplyImportOptions(
          job, manifest_job.script.with_content_hashing, error_stream)) {
      return std::nullopt;
    }
    if (!ApplyImportOptions(job,
          manifest_job.scripting_sidecar.with_content_hashing, error_stream)) {
      return std::nullopt;
    }
    if (!ApplyImportOptions(job,
          manifest_job.physics_sidecar.with_content_hashing, error_stream)) {
      return std::nullopt;
    }
    if (!ApplyMaterialDescriptorOverrides(
          job, manifest_job.material_descriptor, error_stream)) {
      return std::nullopt;
    }
    if (!ApplyPhysicsMaterialDescriptorOverrides(
          job, manifest_job.physics_material_descriptor, error_stream)) {
      return std::nullopt;
    }
    if (!ApplyCollisionShapeDescriptorOverrides(
          job, manifest_job.collision_shape_descriptor, error_stream)) {
      return std::nullopt;
    }
    if (!ApplyBufferContainerOverrides(
          job, manifest_job.buffer_container, error_stream)) {
      return std::nullopt;
    }
    if (!ApplyGeometryDescriptorOverrides(
          job, manifest_job.geometry_descriptor, error_stream)) {
      return std::nullopt;
    }
    if (!ApplySceneDescriptorOverrides(
          job, manifest_job.scene_descriptor, error_stream)) {
      return std::nullopt;
    }
    if (!ApplyTextureOverrides(job, manifest_job.texture, error_stream)) {
      return std::nullopt;
    }
    if (!ApplyScriptAssetOverrides(job, manifest_job.script, error_stream)) {
      return std::nullopt;
    }
    if (!ApplyScriptingSidecarOverrides(
          job, manifest_job.scripting_sidecar, error_stream)) {
      return std::nullopt;
    }
    if (!ApplyPhysicsSidecarOverrides(
          job, manifest_job.physics_sidecar, error_stream)) {
      return std::nullopt;
    }

    manifest.jobs.push_back(std::move(manifest_job));
  }

  return manifest;
}

auto ImportManifestJob::BuildRequest(std::ostream& error_stream) const
  -> std::optional<ImportRequest>
{
  const auto AttachOrchestration =
    [&](std::optional<ImportRequest> request) -> std::optional<ImportRequest> {
    if (!request.has_value()) {
      return std::nullopt;
    }
    request->loose_cooked_layout = loose_cooked_layout;
    if (!id.empty()) {
      request->orchestration = ImportRequest::OrchestrationMetadata {
        .job_id = id,
        .depends_on = depends_on,
      };
    }
    return request;
  };

  if (job_type == "texture") {
    return AttachOrchestration(
      internal::BuildTextureRequest(texture, error_stream));
  }
  if (job_type == "texture-descriptor") {
    return AttachOrchestration(internal::BuildTextureDescriptorRequest(
      TextureDescriptorImportSettings {
        .descriptor_path = texture.source_path,
        .texture = texture,
      },
      error_stream));
  }
  if (job_type == "material-descriptor") {
    return AttachOrchestration(internal::BuildMaterialDescriptorRequest(
      material_descriptor, error_stream));
  }
  if (job_type == "physics-material-descriptor") {
    return AttachOrchestration(internal::BuildPhysicsMaterialDescriptorRequest(
      physics_material_descriptor, error_stream));
  }
  if (job_type == "collision-shape-descriptor") {
    return AttachOrchestration(internal::BuildCollisionShapeDescriptorRequest(
      collision_shape_descriptor, error_stream));
  }
  if (job_type == "buffer-container") {
    return AttachOrchestration(
      internal::BuildBufferContainerRequest(buffer_container, error_stream));
  }
  if (job_type == "geometry-descriptor") {
    return AttachOrchestration(internal::BuildGeometryDescriptorRequest(
      geometry_descriptor, error_stream));
  }
  if (job_type == "scene-descriptor") {
    return AttachOrchestration(
      internal::BuildSceneDescriptorRequest(scene_descriptor, error_stream));
  }
  if (job_type == "fbx") {
    return AttachOrchestration(
      internal::BuildSceneRequest(fbx, ImportFormat::kFbx, error_stream));
  }
  if (job_type == "gltf") {
    return AttachOrchestration(
      internal::BuildSceneRequest(gltf, ImportFormat::kGltf, error_stream));
  }
  if (job_type == "script") {
    return AttachOrchestration(
      internal::BuildScriptAssetRequest(script, error_stream));
  }
  if (job_type == "script-sidecar") {
    return AttachOrchestration(
      internal::BuildScriptingSidecarRequest(scripting_sidecar, error_stream));
  }
  if (job_type == "physics-sidecar") {
    return AttachOrchestration(
      internal::BuildPhysicsSidecarRequest(physics_sidecar, error_stream));
  }
  if (job_type == "input") {
    return AttachOrchestration(
      internal::BuildInputImportRequest(input, id, depends_on, error_stream));
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
