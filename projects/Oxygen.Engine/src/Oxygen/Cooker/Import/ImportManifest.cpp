//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <filesystem>
#include <limits>
#include <memory>
#include <optional>
#include <ostream>
#include <ranges>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

#include <fmt/base.h>
#include <fmt/format.h>
#include <nlohmann/json-schema.hpp>
#include <nlohmann/json.hpp>
#include <nlohmann/json_fwd.hpp>

#include <Oxygen/Base/Logging.h>
#include <Oxygen/Base/ObserverPtr.h>
#include <Oxygen/Base/Uuid.h>
#include <Oxygen/Content/VirtualPath.h>
#include <Oxygen/Cooker/Import/BufferContainerImportRequestBuilder.h>
#include <Oxygen/Cooker/Import/BufferContainerImportSettings.h>
#include <Oxygen/Cooker/Import/CollisionShapeDescriptorImportRequestBuilder.h>
#include <Oxygen/Cooker/Import/CollisionShapeDescriptorImportSettings.h>
#include <Oxygen/Cooker/Import/GeometryDescriptorImportRequestBuilder.h>
#include <Oxygen/Cooker/Import/ImportConcurrency.h>
#include <Oxygen/Cooker/Import/ImportDiagnostics.h>
#include <Oxygen/Cooker/Import/ImportLimits.h>
#include <Oxygen/Cooker/Import/ImportManifest.h>
#include <Oxygen/Cooker/Import/ImportOptions.h>
#include <Oxygen/Cooker/Import/ImportRequest.h>
#include <Oxygen/Cooker/Import/InputImportRequestBuilder.h>
#include <Oxygen/Cooker/Import/InputImportSettings.h>
#include <Oxygen/Cooker/Import/Internal/GeometryDefaults.h>
#include <Oxygen/Cooker/Import/Internal/ImportManifest_schema.h>
#include <Oxygen/Cooker/Import/Internal/SceneImportRequestBuilder.h>
#include <Oxygen/Cooker/Import/Internal/TextureImportRequestBuilder.h>
#include <Oxygen/Cooker/Import/Internal/Utils/DescriptorDocument.h>
#include <Oxygen/Cooker/Import/Internal/Utils/JsonSchemaValidation.h>
#include <Oxygen/Cooker/Import/MaterialDescriptorImportRequestBuilder.h>
#include <Oxygen/Cooker/Import/MaterialDescriptorImportSettings.h>
#include <Oxygen/Cooker/Import/ModelImportRecord.h>
#include <Oxygen/Cooker/Import/ModelImportRecordError.h>
#include <Oxygen/Cooker/Import/PhysicsImportRequestBuilder.h>
#include <Oxygen/Cooker/Import/PhysicsImportSettings.h>
#include <Oxygen/Cooker/Import/PhysicsMaterialDescriptorImportRequestBuilder.h>
#include <Oxygen/Cooker/Import/PhysicsMaterialDescriptorImportSettings.h>
#include <Oxygen/Cooker/Import/SceneDescriptorImportRequestBuilder.h>
#include <Oxygen/Cooker/Import/SceneImportSettings.h>
#include <Oxygen/Cooker/Import/ScriptImportRequestBuilder.h>
#include <Oxygen/Cooker/Import/ScriptImportSettings.h>
#include <Oxygen/Cooker/Import/SourceMounts.h>
#include <Oxygen/Cooker/Import/TextureDescriptorImportRequestBuilder.h>
#include <Oxygen/Cooker/Import/TextureDescriptorImportSettings.h>
#include <Oxygen/Cooker/Import/TextureImportSettings.h>
#include <Oxygen/Data/SourceKey.h>
#include <Oxygen/Data/SurfaceId.h>
#include <Oxygen/Serio/AtomicFile.h>
#include <Oxygen/Serio/ByteBuffer.h>
#include <Oxygen/Serio/Types.h>

namespace oxygen::content::import {

namespace {

  using nlohmann::json;
  using nlohmann::json_schema::json_validator;

  //! Where the diagnostics of one manifest go, and the file they are about.
  struct Findings final {
    observer_ptr<std::vector<ImportDiagnostic>> diagnostics;
    std::string source_path;

    //! Adds an error about the value at `json_pointer` of the manifest file.
    auto Error(const std::string_view code, std::string message,
      std::string json_pointer) const -> void
    {
      diagnostics->push_back(ImportDiagnostic {
        .severity = ImportSeverity::kError,
        .code = std::string { code },
        .message = std::move(message),
        .source_path = source_path,
        .json_pointer = std::move(json_pointer),
      });
    }
  };

  //! The pointer to the job at `index` of a manifest's `jobs`.
  auto JobPointer(const std::size_t index) -> json::json_pointer
  {
    return json::json_pointer {} / "jobs" / index;
  }

  //! True when a diagnostic added from position `first` on is an error.
  auto HasErrorSince(const std::vector<ImportDiagnostic>& diagnostics,
    const std::size_t first) -> bool
  {
    return std::ranges::any_of(
      diagnostics | std::views::drop(static_cast<std::ptrdiff_t>(first)),
      [](const ImportDiagnostic& diagnostic) -> bool {
        return diagnostic.severity == ImportSeverity::kError;
      });
  }

  auto ManifestSchemaValidator() -> json_validator&
  {
    static auto validator
      = internal::MakeJsonSchemaValidator(json::parse(kImportManifestSchema));
    return validator;
  }

  //! Reports every issue the manifest schema finds in `document`.
  auto ValidateAgainstSchema(const json& document, const Findings& findings)
    -> bool
  {
    return internal::ValidateJsonSchemaWithDiagnostics(
      ManifestSchemaValidator(), document,
      internal::JsonSchemaValidationDiagnosticConfig {
        .validation_failed_code = manifest_codes::kSchemaValidationFailed,
        .validation_failed_prefix = {},
        .validator_failure_code = manifest_codes::kSchemaValidatorFailure,
        .validator_failure_prefix = "the schema validator failed: ",
      },
      [&findings](const std::string_view code, std::string message,
        std::string json_pointer) -> void {
        findings.Error(code, std::move(message), std::move(json_pointer));
      });
  }

  //! Reads the `source_key` that `value`, the object at `owner`, declares.
  auto ReadSourceKey(const json& value, const json::json_pointer& owner,
    std::optional<data::SourceKey>& key, const Findings& findings) -> bool
  {
    if (!value.contains("source_key")) {
      return true;
    }
    const auto parsed
      = Uuid::FromString(value.at("source_key").get<std::string>());
    if (!parsed) {
      findings.Error(manifest_codes::kSourceKeyInvalid,
        "source_key must be a canonical UUIDv7",
        (owner / "source_key").to_string());
      return false;
    }
    key = data::SourceKey { parsed.value() };
    return true;
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

  //! Reports a one-fact mount code about a committed field.
  auto ReportCode(const MountCode<std::string_view>& code,
    const std::string_view text, const json::json_pointer& field,
    const Findings& findings) -> void
  {
    findings.Error(code.id,
      fmt::vformat(code.message.get(), fmt::make_format_args(text)),
      field.to_string());
  }

  //! Reports an absolute path in a committed field.
  auto ReportAbsoluteCommitted(const std::string_view text,
    const json::json_pointer& field, const Findings& findings) -> void
  {
    ReportCode(mount_codes::kAbsoluteCommitted, text, field, findings);
  }

  //! Checks that a folder field, or each folder of an array field, of the
  //! object at `owner` is relative; the schema has checked their types.
  auto RequireRelativeFolders(const json& obj, const std::string_view key,
    const json::json_pointer& owner, const Findings& findings) -> bool
  {
    if (!obj.is_object() || !obj.contains(key)) {
      return true;
    }
    const auto check = [&findings](const json& value,
                         const json::json_pointer& field) -> bool {
      const auto& text = value.get_ref<const std::string&>();
      const auto path = std::filesystem::path(text);
      if (!path.has_root_name() && !path.has_root_directory()) {
        return true;
      }
      ReportAbsoluteCommitted(text, field, findings);
      return false;
    };
    const auto field = owner / std::string { key };
    const auto& value = obj.at(key);
    if (!value.is_array()) {
      return check(value, field);
    }
    auto relative = true;
    for (std::size_t index = 0; index < value.size(); ++index) {
      relative = check(value.at(index), field / index) && relative;
    }
    return relative;
  }

  //! Checks that the folders of a committed manifest (`project_root`, every
  //! `output` and `cooked_context_roots`) are relative.
  auto RequireRelativeFolders(const json& doc, const Findings& findings) -> bool
  {
    const auto root = json::json_pointer {};
    auto relative = RequireRelativeFolders(doc, "project_root", root, findings);
    relative
      = RequireRelativeFolders(doc, "output", root, findings) && relative;
    const auto check_settings = [&findings, &relative](const json& settings,
                                  const json::json_pointer& field) -> void {
      relative = RequireRelativeFolders(settings, "output", field, findings)
        && relative;
      relative = RequireRelativeFolders(
                   settings, "cooked_context_roots", field, findings)
        && relative;
    };
    if (doc.contains("defaults")) {
      for (const auto& [name, settings] : doc.at("defaults").items()) {
        check_settings(settings, root / "defaults" / name);
      }
    }
    const auto& jobs = doc.at("jobs");
    for (std::size_t index = 0; index < jobs.size(); ++index) {
      check_settings(jobs.at(index), JobPointer(index));
    }
    return relative;
  }

  //! The committed mount declaration of a manifest, its shape checked by the
  //! schema; a kind other than `Source` or `Cooked` is `mount.kind_unknown`.
  auto ReadMountDeclarations(const json& doc, const std::string& declared_in,
    const Findings& findings) -> std::optional<std::vector<MountDeclaration>>
  {
    auto declarations = std::vector<MountDeclaration> {};
    auto valid = true;
    const auto mounts = doc.value("mounts", json::array());
    for (std::size_t index = 0; index < mounts.size(); ++index) {
      const auto& entry = mounts.at(index);
      const auto name = entry.at("name").get<std::string>();
      const auto text
        = entry.value("kind", std::string { to_string(MountKind::kSource) });
      const auto kind = ParseMountKind(text);
      if (!kind.has_value()) {
        const auto& code = mount_codes::kKindUnknown;
        findings.diagnostics->push_back(ImportDiagnostic {
          .severity = code.severity,
          .code = std::string { code.id },
          .message
          = fmt::vformat(code.message.get(), fmt::make_format_args(name, text)),
          .source_path = declared_in,
          .json_pointer
          = (json::json_pointer {} / "mounts" / index / "kind").to_string(),
          .object_name = name,
        });
        valid = false;
        continue;
      }
      auto& declaration = declarations.emplace_back(MountDeclaration {
        .name = name,
        .kind = *kind,
        .path = std::nullopt,
      });
      if (entry.contains("path")) {
        declaration.path = entry.at("path").get<std::string>();
      }
    }
    if (!valid) {
      return std::nullopt;
    }
    return declarations;
  }

  //! Locates a source a committed field names through the manifest's mounts.
  auto LocateSource(const MountTable& mounts, const std::string& text,
    const json::json_pointer& field, const Findings& findings)
    -> std::optional<MountedSource>
  {
    const auto pointer = field.to_string();
    return mounts.LocateSource(
      CommittedPath { .text = text, .json_pointer = pointer },
      *findings.diagnostics);
  }

  //! A file a committed field names that need not lie in a mount: a mount
  //! path, or a path relative to the project root.
  auto LocateFile(const MountTable& mounts, const std::string& text,
    const json::json_pointer& field, const Findings& findings)
    -> std::optional<std::filesystem::path>
  {
    if (text.starts_with('/')) {
      return LocateSource(mounts, text, field, findings)
        .transform([](const MountedSource& source) -> std::filesystem::path {
          return source.file;
        });
    }
    const auto path = std::filesystem::path(text);
    if (path.has_root_name() || path.has_root_directory()) {
      ReportAbsoluteCommitted(text, field, findings);
      return std::nullopt;
    }
    return (mounts.project_root / path).lexically_normal();
  }

  //! A job's source and its recorded source.
  /*!
   A job reads its `source`. Its recorded source is that file's mount path,
   unless the file is derived: it lies in an output folder and the job names
   the authored source it derives from in `derived_from`, a mount path in a
   `Source` mount, as a source map names its original.
  */
  auto LocateJobSource(const MountTable& mounts, const json& job,
    const json::json_pointer& job_ptr, const Findings& findings)
    -> std::optional<MountedSource>
  {
    const auto& source = job.at("source").get_ref<const std::string&>();
    if (!job.contains("derived_from")) {
      return LocateSource(mounts, source, job_ptr / "source", findings);
    }
    const auto file = LocateFile(mounts, source, job_ptr / "source", findings);
    if (!file.has_value()) {
      return std::nullopt;
    }
    const auto field = job_ptr / "derived_from";
    if (!mounts.IsInOutputFolder(*file)) {
      ReportCode(mount_codes::kDerivedFromNotDerived, source, field, findings);
      return std::nullopt;
    }
    const auto& derived = job.at("derived_from").get_ref<const std::string&>();
    const auto path = std::filesystem::path(derived);
    if (!derived.starts_with('/')) {
      if (path.has_root_name() || path.has_root_directory()) {
        ReportAbsoluteCommitted(derived, field, findings);
      } else {
        ReportCode(mount_codes::kDerivedFromInvalid, derived, field, findings);
      }
      return std::nullopt;
    }
    const auto mount = mounts.Find(derived.substr(1, derived.find('/', 1) - 1));
    if (!mount || mount->kind != MountKind::kSource) {
      ReportCode(mount_codes::kDerivedFromInvalid, derived, field, findings);
      return std::nullopt;
    }
    const auto authored = LocateSource(mounts, derived, field, findings);
    if (!authored.has_value()) {
      return std::nullopt;
    }
    return MountedSource { .file = *file, .mount_path = authored->mount_path };
  }

  //! The model import record of the model a `model` job names by its
  //! identity, found at that identity through the manifest's mounts.
  auto ReadModelRecord(const json& job, const MountTable& mounts,
    const json::json_pointer& job_ptr, const Findings& findings)
    -> std::shared_ptr<const ModelImportRecordContent>
  {
    const auto path = ModelImportRecord::RecordPathOf(
      mounts, job.at("virtual_path").get<std::string>(), *findings.diagnostics);
    if (!path.has_value()) {
      return {};
    }
    try {
      return std::make_shared<const ModelImportRecordContent>(
        ModelImportRecord::Read(*path, mounts));
    } catch (const ModelImportRecordError& error) {
      findings.diagnostics->push_back(error.Diagnostic());
      return {};
    } catch (const std::filesystem::filesystem_error& error) {
      findings.Error(model_record_codes::kUnreadable,
        fmt::format(
          "model import record '{}': {}", path->generic_string(), error.what()),
        (job_ptr / "virtual_path").to_string());
      return {};
    }
  }

  //! The gltf or fbx job of a `model` job: its record's, with the manifest's
  //! scheduling and output keys.
  auto MakeModelJob(const json& job, const ModelImportRecordContent& record)
    -> json
  {
    auto result = json::parse(ModelImportRecord::MakeJob(
      record, job.at("virtual_path").get<std::string>()));
    for (const auto* key : {
           "id",
           "depends_on",
           "output",
           "cooked_context_roots",
           "source_key",
         }) {
      if (job.contains(key)) {
        result[key] = job.at(key);
      }
    }
    if (!job.contains("id")) {
      result.erase("id");
    }
    return result;
  }

  template <typename T>
  auto ResolveCookedRootRelativeToManifest(
    const std::filesystem::path& manifest_dir, T& settings) -> void
  {
    if (settings.cooked_root.empty()) {
      return;
    }

    auto cooked_root = std::filesystem::path(settings.cooked_root);
    if (!cooked_root.is_absolute()) {
      cooked_root = manifest_dir / cooked_root;
    }
    settings.cooked_root = cooked_root.lexically_normal().string();
  }

  auto ResolveContextRootsRelativeToManifest(
    const std::filesystem::path& manifest_dir, std::vector<std::string>& roots)
    -> void
  {
    for (auto& root : roots) {
      root = ResolveSourcePath(manifest_dir, root);
    }
  }

  auto ResolveCookedRootsRelativeToManifest(
    const std::filesystem::path& manifest_dir, ImportManifestDefaults& defaults)
    -> void
  {
    ResolveCookedRootRelativeToManifest(manifest_dir, defaults.texture);
    ResolveCookedRootRelativeToManifest(manifest_dir, defaults.fbx);
    ResolveCookedRootRelativeToManifest(manifest_dir, defaults.gltf);
    ResolveCookedRootRelativeToManifest(manifest_dir, defaults.script);
    ResolveCookedRootRelativeToManifest(
      manifest_dir, defaults.scripting_sidecar);
    ResolveCookedRootRelativeToManifest(manifest_dir, defaults.physics_sidecar);
    ResolveCookedRootRelativeToManifest(manifest_dir, defaults.input);
    ResolveCookedRootRelativeToManifest(
      manifest_dir, defaults.buffer_container);
    ResolveCookedRootRelativeToManifest(
      manifest_dir, defaults.material_descriptor);
    ResolveCookedRootRelativeToManifest(
      manifest_dir, defaults.physics_material_descriptor);
    ResolveCookedRootRelativeToManifest(
      manifest_dir, defaults.collision_shape_descriptor);
    ResolveCookedRootRelativeToManifest(
      manifest_dir, defaults.geometry_descriptor);
    ResolveCookedRootRelativeToManifest(
      manifest_dir, defaults.scene_descriptor);
    ResolveContextRootsRelativeToManifest(
      manifest_dir, defaults.material_descriptor.cooked_context_roots);
    ResolveContextRootsRelativeToManifest(
      manifest_dir, defaults.scene_descriptor.cooked_context_roots);
    ResolveContextRootsRelativeToManifest(
      manifest_dir, defaults.geometry_descriptor.cooked_context_roots);
    ResolveContextRootsRelativeToManifest(
      manifest_dir, defaults.scripting_sidecar.cooked_context_roots);
    ResolveContextRootsRelativeToManifest(
      manifest_dir, defaults.input.cooked_context_roots);
  }

  auto ResolveCookedRootsRelativeToManifest(
    const std::filesystem::path& manifest_dir, ImportManifestJob& job) -> void
  {
    ResolveCookedRootRelativeToManifest(manifest_dir, job.texture);
    ResolveCookedRootRelativeToManifest(manifest_dir, job.fbx);
    ResolveCookedRootRelativeToManifest(manifest_dir, job.gltf);
    ResolveCookedRootRelativeToManifest(manifest_dir, job.script);
    ResolveCookedRootRelativeToManifest(manifest_dir, job.scripting_sidecar);
    ResolveCookedRootRelativeToManifest(manifest_dir, job.physics_sidecar);
    ResolveCookedRootRelativeToManifest(manifest_dir, job.input);
    ResolveCookedRootRelativeToManifest(manifest_dir, job.buffer_container);
    ResolveCookedRootRelativeToManifest(manifest_dir, job.material_descriptor);
    ResolveCookedRootRelativeToManifest(
      manifest_dir, job.physics_material_descriptor);
    ResolveCookedRootRelativeToManifest(
      manifest_dir, job.collision_shape_descriptor);
    ResolveCookedRootRelativeToManifest(manifest_dir, job.geometry_descriptor);
    ResolveCookedRootRelativeToManifest(manifest_dir, job.scene_descriptor);
    ResolveContextRootsRelativeToManifest(
      manifest_dir, job.material_descriptor.cooked_context_roots);
    ResolveContextRootsRelativeToManifest(
      manifest_dir, job.scene_descriptor.cooked_context_roots);
    ResolveContextRootsRelativeToManifest(
      manifest_dir, job.geometry_descriptor.cooked_context_roots);
    ResolveContextRootsRelativeToManifest(
      manifest_dir, job.scripting_sidecar.cooked_context_roots);
    ResolveContextRootsRelativeToManifest(
      manifest_dir, job.input.cooked_context_roots);
  }

  auto TrimInPlace(std::string& value) -> void
  {
    const auto is_ws = [](const unsigned char ch) -> bool {
      return ch == ' ' || ch == '\t' || ch == '\r' || ch == '\n';
    };
    const auto first = std::ranges::find_if(value, [&](const char ch) -> bool {
      return !is_ws(static_cast<unsigned char>(ch));
    });
    if (first == value.end()) {
      value.clear();
      return;
    }
    const auto last = std::ranges::find_if(
      std::views::reverse(value), [&](const char ch) -> bool {
        return !is_ws(static_cast<unsigned char>(ch));
      });
    value = std::string(first, last.base());
  }

  //! Reads `name` of `obj` into `target` when `obj` has it; the schema has
  //! checked its type and range.
  template <typename T>
  auto ReadValue(const json& obj, const char* name, T& target) -> void
  {
    if (obj.contains(name)) {
      target = obj.at(name).get<T>();
    }
  }

  //! Reads an optional `name` of `obj` into `target`.
  template <typename T>
  auto ReadValue(const json& obj, const char* name, std::optional<T>& target)
    -> void
  {
    if (obj.contains(name)) {
      target = obj.at(name).get<T>();
    }
  }

  //! Reads the job's trimmed `id` and `depends_on`.
  /*!
   The schema requires an input job's `id` and forbids empty ones; text that is
   only whitespace is empty once trimmed.
  */
  auto ReadJobDependencies(const json& job, const json::json_pointer& job_ptr,
    const bool require_job_id, ImportManifestJob& manifest_job,
    const Findings& findings) -> bool
  {
    auto valid = true;
    ReadValue(job, "id", manifest_job.id);
    TrimInPlace(manifest_job.id);
    if (require_job_id && manifest_job.id.empty()) {
      findings.Error(manifest_codes::kInputJobIdMissing,
        "input job.id must not be empty", (job_ptr / "id").to_string());
      valid = false;
    }

    ReadValue(job, "depends_on", manifest_job.depends_on);
    for (std::size_t index = 0; index < manifest_job.depends_on.size();
      ++index) {
      auto& dependency = manifest_job.depends_on.at(index);
      TrimInPlace(dependency);
      if (dependency.empty()) {
        findings.Error(manifest_codes::kDependencyEmpty,
          "depends_on entry must not be empty",
          (job_ptr / "depends_on" / index).to_string());
        valid = false;
      }
    }
    return valid;
  }

  //! True for a job named by its virtual path: one that cooks exactly one
  //! asset, or a container (an input source or a model) whose products are
  //! named under it.
  auto IsIdentifiedJob(const std::string_view job_type) -> bool
  {
    static constexpr auto kIdentifiedJobs = std::array<std::string_view, 11> {
      "material-descriptor",
      "geometry-descriptor",
      "scene-descriptor",
      "script",
      "texture",
      "texture-descriptor",
      "physics-material-descriptor",
      "collision-shape-descriptor",
      "input",
      "gltf",
      "fbx",
    };
    return std::ranges::contains(kIdentifiedJobs, job_type);
  }

  //! Reads the identity a job cooks.
  /*!
   The schema checks the value of `virtual_path`; which jobs name one is a rule
   of the job type.
  */
  auto ReadJobVirtualPath(const json& job, const json::json_pointer& job_ptr,
    ImportManifestJob& manifest_job, const Findings& findings) -> bool
  {
    const auto identified = IsIdentifiedJob(manifest_job.job_type);
    if (!job.contains("virtual_path")) {
      if (identified) {
        findings.Error(manifest_codes::kVirtualPathMissing,
          fmt::format("{} job requires 'virtual_path', the identity of what it "
                      "cooks",
            manifest_job.job_type),
          job_ptr.to_string());
        return false;
      }
      return true;
    }
    if (!identified) {
      findings.Error(manifest_codes::kVirtualPathNotAllowed,
        fmt::format(
          "{} job produces several assets and takes no 'virtual_path'",
          manifest_job.job_type),
        (job_ptr / "virtual_path").to_string());
      return false;
    }
    manifest_job.virtual_path = job.at("virtual_path").get<std::string>();
    manifest_job.texture.virtual_path = manifest_job.virtual_path;
    manifest_job.input.virtual_path = manifest_job.virtual_path;
    manifest_job.gltf.virtual_path = manifest_job.virtual_path;
    manifest_job.fbx.virtual_path = manifest_job.virtual_path;
    return true;
  }

  auto ApplyTextureOverrides(const json& obj, TextureImportSettings& settings)
    -> void
  {
    if (obj.contains("sources")) {
      for (const auto& mapping_json : obj.at("sources")) {
        TextureSourceMapping mapping;
        ReadValue(mapping_json, "file", mapping.file);
        ReadValue(mapping_json, "layer", mapping.layer);
        ReadValue(mapping_json, "mip", mapping.mip);
        ReadValue(mapping_json, "slice", mapping.slice);
        settings.sources.push_back(std::move(mapping));
      }
    }
    ReadValue(obj, "preset", settings.preset);
    ReadValue(obj, "intent", settings.intent);
    ReadValue(obj, "color_space", settings.color_space);
    ReadValue(obj, "output_format", settings.output_format);
    ReadValue(obj, "data_format", settings.data_format);
    ReadValue(obj, "mip_policy", settings.mip_policy);
    ReadValue(obj, "mip_filter", settings.mip_filter);
    ReadValue(obj, "bc7_quality", settings.bc7_quality);
    ReadValue(obj, "packing_policy", settings.packing_policy);
    ReadValue(obj, "cube_layout", settings.cube_layout);
    ReadValue(obj, "hdr_handling", settings.hdr_handling);
    ReadValue(obj, "exposure_ev", settings.exposure_ev);
    ReadValue(obj, "max_mips", settings.max_mip_levels);
    ReadValue(obj, "max_size", settings.max_size);
    ReadValue(obj, "cube_face_size", settings.cube_face_size);
    ReadValue(obj, "flip_y", settings.flip_y);
    ReadValue(obj, "force_rgba", settings.force_rgba);
    ReadValue(obj, "flip_normal_green", settings.flip_normal_green);
    ReadValue(obj, "renormalize", settings.renormalize_normals);
    ReadValue(obj, "bake_hdr", settings.bake_hdr_to_ldr);
    ReadValue(obj, "cubemap", settings.cubemap);
    ReadValue(obj, "equirect_to_cube", settings.equirect_to_cube);
  }

  //! Reads the `geometry_defaults` of the object at `owner`.
  /*!
   The schema has checked their shape and the form of a surface id. What it
   cannot state is checked here: the LOD policy's thresholds and error lists,
   and a surface id that is not nil.
  */
  auto ReadGeometryDefaults(const json& obj, const json::json_pointer& owner,
    SceneImportSettings& settings, const Findings& findings) -> bool
  {
    if (!obj.contains("geometry_defaults")) {
      return true;
    }
    for (const auto& [anchor, entry] : obj.at("geometry_defaults").items()) {
      const auto anchor_ptr = owner / "geometry_defaults" / anchor;
      auto defaults = GeometryImportDefaults {};
      if (entry.contains("lod_policy")) {
        defaults.lod_policy = internal::ReadLodPolicy(entry.at("lod_policy"));
        if (!defaults.lod_policy.has_value()) {
          findings.Error(manifest_codes::kLodPolicyInvalid,
            fmt::format("geometry_defaults '{}': lod_policy thresholds "
                        "decrease or its screen-space error lists differ in "
                        "length",
              anchor),
            (anchor_ptr / "lod_policy").to_string());
          return false;
        }
      }
      if (entry.contains("surfaces")) {
        for (const auto& [id, values] : entry.at("surfaces").items()) {
          const auto surface_id = data::SurfaceId::FromString(id);
          if (!surface_id.has_value() || surface_id.value().IsNil()) {
            findings.Error(manifest_codes::kSurfaceIdInvalid,
              fmt::format(
                "geometry_defaults '{}': invalid surface ID '{}'", anchor, id),
              (anchor_ptr / "surfaces" / id).to_string());
            return false;
          }
          auto& surface = defaults.surfaces[surface_id.value()];
          if (values.contains("visible")) {
            surface.visible = values.at("visible").get<bool>();
          }
          if (values.contains("casts_shadows")) {
            surface.casts_shadows = values.at("casts_shadows").get<bool>();
          }
        }
      }
      settings.geometry_defaults.insert_or_assign(anchor, std::move(defaults));
    }
    return true;
  }

  auto ApplySceneOverrides(const json& obj, const json::json_pointer& owner,
    SceneImportSettings& settings, const Findings& findings) -> bool
  {
    if (obj.contains("content_flags")) {
      const auto& flags = obj.at("content_flags");
      ReadValue(flags, "textures", settings.import_textures);
      ReadValue(flags, "materials", settings.import_materials);
      ReadValue(flags, "geometry", settings.import_geometry);
      ReadValue(flags, "scene", settings.import_scene);
    }

    ReadValue(obj, "unit_policy", settings.unit_policy);
    ReadValue(obj, "unit_scale", settings.unit_scale);
    settings.unit_scale_set
      = settings.unit_scale_set || obj.contains("unit_scale");
    ReadValue(
      obj, "gltf_omitted_light_range_m", settings.gltf_omitted_light_range_m);
    ReadValue(obj, "gltf_scene", settings.gltf_scene);
    ReadValue(obj, "up_axis", settings.up_axis);
    ReadValue(obj, "import_lights", settings.import_lights);
    ReadValue(obj, "import_cameras", settings.import_cameras);
    ReadValue(obj, "light_intensity_scale", settings.light_intensity_scale);
    ReadValue(
      obj, "normals_smoothing_angle_deg", settings.normals_smoothing_angle_deg);
    ReadValue(obj, "generate_lods", settings.generate_lods);
    ReadValue(obj, "lod_levels", settings.lod_levels);
    ReadValue(obj, "lod_reduction_percent", settings.lod_reduction_percent);
    if (obj.contains("material_remap")) {
      for (const auto& [name, target] : obj.at("material_remap").items()) {
        settings.material_remap.insert_or_assign(
          name, target.get<std::string>());
      }
    }
    if (!ReadGeometryDefaults(obj, owner, settings, findings)) {
      return false;
    }
    ReadValue(obj, "bake_transforms", settings.bake_transforms);
    ReadValue(obj, "normals_policy", settings.normals_policy);
    ReadValue(obj, "tangents_policy", settings.tangents_policy);
    ReadValue(obj, "node_pruning", settings.node_pruning);

    // Apply texture overrides from the same object (flat structure)
    ApplyTextureOverrides(obj, settings.texture_defaults);

    if (obj.contains("texture_overrides")) {
      for (const auto& [name, overrides] :
        obj.at("texture_overrides").items()) {
        // Start with the defaults for this job
        auto texture = settings.texture_defaults;
        ApplyTextureOverrides(overrides, texture);
        settings.texture_overrides[name] = std::move(texture);
      }
    }
    return true;
  }

  auto ApplyScriptAssetOverrides(
    const json& obj, ScriptAssetImportSettings& settings) -> void
  {
    ReadValue(obj, "compile", settings.compile_scripts);
    ReadValue(obj, "compile_mode", settings.compile_mode);
    ReadValue(obj, "script_storage", settings.script_storage);
  }

  //! The settings every job type with a cooked root and a name takes.
  template <typename Settings>
  auto ApplyOutputAndName(const json& obj, Settings& settings) -> void
  {
    ReadValue(obj, "output", settings.cooked_root);
    ReadValue(obj, "name", settings.job_name);
  }

  //! The settings of a job type that can also be verbose.
  template <typename Settings>
  auto ApplyCommonOverrides(const json& obj, Settings& settings) -> void
  {
    ApplyOutputAndName(obj, settings);
    ReadValue(obj, "verbose", settings.verbose);
  }

  template <typename Settings>
  auto ApplyCookedContextRoots(const json& obj, Settings& settings) -> void
  {
    ReadValue(obj, "cooked_context_roots", settings.cooked_context_roots);
  }

  auto ApplyInputOverrides(const json& obj, InputImportSettings& settings)
    -> void
  {
    ReadValue(obj, "output", settings.cooked_root);
    ApplyCookedContextRoots(obj, settings);
  }

  auto ApplyScriptingSidecarOverrides(
    const json& obj, ScriptingSidecarImportSettings& settings) -> void
  {
    ReadValue(
      obj, "target_scene_virtual_path", settings.target_scene_virtual_path);
    ApplyCookedContextRoots(obj, settings);
  }

  auto ApplyPhysicsSidecarOverrides(
    const json& obj, PhysicsSidecarImportSettings& settings) -> void
  {
    ReadValue(
      obj, "target_scene_virtual_path", settings.target_scene_virtual_path);
    if (obj.contains("physics_backend")) {
      // The schema lists the backends.
      const auto backend = ParsePhysicsBackend(
        obj.at("physics_backend").get_ref<const std::string&>());
      CHECK_F(backend.has_value(), "The schema refuses an unknown backend");
      settings.physics_backend = *backend;
    }
  }

  auto ApplyPipelineConcurrency(
    const json& obj, ImportPipelineConcurrency& target) -> void
  {
    ReadValue(obj, "workers", target.workers);
    ReadValue(obj, "queue_capacity", target.queue_capacity);
  }

  auto ApplyConcurrencyOverrides(const json& obj, ImportConcurrency& target)
    -> void
  {
    const auto apply
      = [&obj](const char* name, ImportPipelineConcurrency& pipeline) -> void {
      if (obj.contains(name)) {
        ApplyPipelineConcurrency(obj.at(name), pipeline);
      }
    };
    apply("texture", target.texture);
    apply("buffer", target.buffer);
    apply("material", target.material);
    apply("mesh_build", target.mesh_build);
    apply("geometry", target.geometry);
    apply("scene", target.scene);
  }

  //! The section `name` of the manifest's `defaults`, when it has one.
  auto FindSection(const json& defaults, const char* name) -> const json*
  {
    const auto section = defaults.find(name);
    return section == defaults.end() ? nullptr : &*section;
  }

  //! Reads the manifest's `defaults`, whose shape the schema has checked.
  auto ReadDefaults(const json& defaults, ImportManifestDefaults& result,
    const Findings& findings) -> bool
  {
    const auto owner = json::json_pointer {} / "defaults";
    if (!ReadSourceKey(defaults, owner, result.source_key, findings)) {
      return false;
    }

    if (const auto* texture = FindSection(defaults, "texture")) {
      ApplyCommonOverrides(*texture, result.texture);
      ApplyTextureOverrides(*texture, result.texture);
    }

    if (const auto* scene = FindSection(defaults, "scene")) {
      ApplyCommonOverrides(*scene, result.fbx);
      ApplyCommonOverrides(*scene, result.gltf);
      const auto scene_ptr = owner / "scene";
      if (!ApplySceneOverrides(*scene, scene_ptr, result.fbx, findings)
        || !ApplySceneOverrides(*scene, scene_ptr, result.gltf, findings)) {
        return false;
      }
    }

    if (const auto* script = FindSection(defaults, "script")) {
      ApplyCommonOverrides(*script, result.script);
      ApplyScriptAssetOverrides(*script, result.script);
    }

    if (const auto* sidecar = FindSection(defaults, "scripting_sidecar")) {
      ApplyCommonOverrides(*sidecar, result.scripting_sidecar);
      ApplyScriptingSidecarOverrides(*sidecar, result.scripting_sidecar);
    }

    if (const auto* sidecar = FindSection(defaults, "physics_sidecar")) {
      ApplyCommonOverrides(*sidecar, result.physics_sidecar);
      ApplyPhysicsSidecarOverrides(*sidecar, result.physics_sidecar);
    }

    if (const auto* input = FindSection(defaults, "input")) {
      ApplyInputOverrides(*input, result.input);
    }

    if (const auto* material = FindSection(defaults, "material_descriptor")) {
      ApplyOutputAndName(*material, result.material_descriptor);
      ApplyCookedContextRoots(*material, result.material_descriptor);
    }

    if (const auto* material
      = FindSection(defaults, "physics_material_descriptor")) {
      ApplyOutputAndName(*material, result.physics_material_descriptor);
    }

    if (const auto* shape
      = FindSection(defaults, "collision_shape_descriptor")) {
      ApplyOutputAndName(*shape, result.collision_shape_descriptor);
    }

    if (const auto* geometry = FindSection(defaults, "geometry_descriptor")) {
      ApplyOutputAndName(*geometry, result.geometry_descriptor);
      ApplyCookedContextRoots(*geometry, result.geometry_descriptor);
    }

    if (const auto* scene = FindSection(defaults, "scene_descriptor")) {
      ApplyOutputAndName(*scene, result.scene_descriptor);
      ApplyCookedContextRoots(*scene, result.scene_descriptor);
    }

    if (const auto* buffers = FindSection(defaults, "buffer_container")) {
      ApplyOutputAndName(*buffers, result.buffer_container);
    }
    return true;
  }

  //! Gives every job type's settings the cooked root the manifest names.
  auto ApplyManifestOutput(
    const std::string& output, ImportManifestDefaults& defaults) -> void
  {
    defaults.texture.cooked_root = output;
    defaults.fbx.cooked_root = output;
    defaults.gltf.cooked_root = output;
    defaults.script.cooked_root = output;
    defaults.scripting_sidecar.cooked_root = output;
    defaults.physics_sidecar.cooked_root = output;
    defaults.input.cooked_root = output;
    defaults.buffer_container.cooked_root = output;
    defaults.material_descriptor.cooked_root = output;
    defaults.physics_material_descriptor.cooked_root = output;
    defaults.collision_shape_descriptor.cooked_root = output;
    defaults.geometry_descriptor.cooked_root = output;
    defaults.scene_descriptor.cooked_root = output;
  }

  //! Gives a job the defaults of the manifest.
  auto MakeJobFromDefaults(const ImportManifest& manifest) -> ImportManifestJob
  {
    ImportManifestJob job {};
    job.source_key = manifest.defaults.source_key;
    job.mounts = manifest.mounts;
    job.texture = manifest.defaults.texture;
    job.fbx = manifest.defaults.fbx;
    job.gltf = manifest.defaults.gltf;
    job.script = manifest.defaults.script;
    job.scripting_sidecar = manifest.defaults.scripting_sidecar;
    job.physics_sidecar = manifest.defaults.physics_sidecar;
    job.input = manifest.defaults.input;
    job.buffer_container = manifest.defaults.buffer_container;
    job.material_descriptor = manifest.defaults.material_descriptor;
    job.physics_material_descriptor
      = manifest.defaults.physics_material_descriptor;
    job.collision_shape_descriptor
      = manifest.defaults.collision_shape_descriptor;
    job.geometry_descriptor = manifest.defaults.geometry_descriptor;
    job.scene_descriptor = manifest.defaults.scene_descriptor;
    job.fbx.texture_defaults = manifest.defaults.texture;
    job.gltf.texture_defaults = manifest.defaults.texture;
    return job;
  }

  //! Sets or clears the file every job type reads its source or descriptor
  //! from.
  auto SetJobSourcePath(ImportManifestJob& job, const std::string& path) -> void
  {
    job.texture.source_path = path;
    job.fbx.source_path = path;
    job.gltf.source_path = path;
    job.script.source_path = path;
    job.scripting_sidecar.source_path = path;
    job.physics_sidecar.source_path = path;
    job.buffer_container.descriptor_path = path;
    job.material_descriptor.descriptor_path = path;
    job.physics_material_descriptor.descriptor_path = path;
    job.collision_shape_descriptor.descriptor_path = path;
    job.geometry_descriptor.descriptor_path = path;
    job.scene_descriptor.descriptor_path = path;
  }

  //! Reads an input job: its identity, its source and its context roots.
  auto ReadInputJob(const json& job, const json::json_pointer& job_ptr,
    const ImportManifest& manifest, ImportManifestJob& manifest_job,
    const Findings& findings) -> bool
  {
    if (!ReadJobVirtualPath(job, job_ptr, manifest_job, findings)) {
      return false;
    }
    const auto located
      = LocateJobSource(*manifest.mounts, job, job_ptr, findings);
    if (!located.has_value()) {
      return false;
    }
    manifest_job.source_mount_path = located->mount_path;
    manifest_job.input.source_path = located->file.string();
    ApplyInputOverrides(job, manifest_job.input);
    return true;
  }

  //! Reads the bindings and sources of a job that is not an input job.
  /*!
   The schema requires a source or bindings and the right type of bindings
   for each sidecar. What it does not state is checked here: which job types
   take `bindings` and `instance_bindings`.
  */
  auto ReadSourcesAndBindings(const json& job,
    const json::json_pointer& job_ptr, const ImportManifest& manifest,
    ImportManifestJob& manifest_job, const Findings& findings) -> bool
  {
    const auto has_source = job.contains("source");
    const auto has_bindings = job.contains("bindings");
    const auto is_script_sidecar_job
      = manifest_job.job_type == "script-sidecar";
    const auto is_physics_sidecar_job
      = manifest_job.job_type == "physics-sidecar";

    if (has_bindings && !is_script_sidecar_job && !is_physics_sidecar_job) {
      findings.Error(manifest_codes::kBindingsNotAllowed,
        "job.bindings is only valid for type 'script-sidecar' or "
        "'physics-sidecar'",
        (job_ptr / "bindings").to_string());
      return false;
    }

    if (!ReadJobVirtualPath(job, job_ptr, manifest_job, findings)) {
      return false;
    }

    if (job.contains("instance_bindings")
      && (!is_script_sidecar_job || !has_bindings)) {
      findings.Error(manifest_codes::kInstanceBindingsNotAllowed,
        "job.instance_bindings is only valid for a 'script-sidecar' job with "
        "inline bindings",
        (job_ptr / "instance_bindings").to_string());
      return false;
    }

    if (has_source) {
      const auto located
        = LocateJobSource(*manifest.mounts, job, job_ptr, findings);
      if (!located.has_value()) {
        return false;
      }
      manifest_job.source_mount_path = located->mount_path;
      SetJobSourcePath(manifest_job, located->file.string());
    } else {
      SetJobSourcePath(manifest_job, {});
    }

    manifest_job.scripting_sidecar.inline_bindings_json.clear();
    manifest_job.physics_sidecar.inline_bindings_json.clear();
    if (has_bindings && is_script_sidecar_job) {
      auto sidecar_doc = json::object();
      sidecar_doc.update({ { "bindings", job.at("bindings") } });
      if (job.contains("instance_bindings")) {
        sidecar_doc.update(
          { { "instance_bindings", job.at("instance_bindings") } });
      }
      manifest_job.scripting_sidecar.inline_bindings_json = sidecar_doc.dump();
    } else if (has_bindings && is_physics_sidecar_job) {
      auto sidecar_doc = json::object();
      sidecar_doc.update({ { "bindings", job.at("bindings") } });
      manifest_job.physics_sidecar.inline_bindings_json = sidecar_doc.dump();
    }
    return true;
  }

  //! Applies what the job states over the defaults it started from.
  auto ApplyJobOverrides(const json& job, const json::json_pointer& job_ptr,
    const ImportManifest& manifest, ImportManifestJob& manifest_job,
    const Findings& findings) -> bool
  {
    ApplyCommonOverrides(job, manifest_job.texture);
    ApplyCommonOverrides(job, manifest_job.fbx);
    ApplyCommonOverrides(job, manifest_job.gltf);
    ApplyCommonOverrides(job, manifest_job.script);
    ApplyCommonOverrides(job, manifest_job.scripting_sidecar);
    ApplyCommonOverrides(job, manifest_job.physics_sidecar);
    ApplyOutputAndName(job, manifest_job.material_descriptor);
    ApplyOutputAndName(job, manifest_job.physics_material_descriptor);
    ApplyOutputAndName(job, manifest_job.collision_shape_descriptor);
    ApplyOutputAndName(job, manifest_job.buffer_container);
    ApplyOutputAndName(job, manifest_job.geometry_descriptor);
    ApplyOutputAndName(job, manifest_job.scene_descriptor);

    if (!ApplySceneOverrides(job, job_ptr, manifest_job.fbx, findings)
      || !ApplySceneOverrides(job, job_ptr, manifest_job.gltf, findings)) {
      return false;
    }
    ApplyCookedContextRoots(job, manifest_job.material_descriptor);
    ApplyCookedContextRoots(job, manifest_job.geometry_descriptor);
    ApplyCookedContextRoots(job, manifest_job.scene_descriptor);
    ApplyTextureOverrides(job, manifest_job.texture);
    ApplyScriptAssetOverrides(job, manifest_job.script);

    // The defaults' texture sources come first, then the job's own.
    const auto inherited = manifest.defaults.texture.sources.size();
    for (std::size_t index = 0; index < manifest_job.texture.sources.size();
      ++index) {
      auto& mapping = manifest_job.texture.sources.at(index);
      const auto field = index < inherited
        ? json::json_pointer {} / "defaults" / "texture" / "sources" / index
          / "file"
        : job_ptr / "sources" / (index - inherited) / "file";
      const auto located
        = LocateSource(*manifest.mounts, mapping.file, field, findings);
      if (!located.has_value()) {
        return false;
      }
      mapping.file = located->file.string();
      if (manifest_job.source_mount_path.empty()) {
        manifest_job.source_mount_path = located->mount_path;
      }
    }
    ApplyScriptingSidecarOverrides(job, manifest_job.scripting_sidecar);
    ApplyPhysicsSidecarOverrides(job, manifest_job.physics_sidecar);
    return true;
  }

  //! Reads the job at `index` of the manifest's document.
  auto ReadJob(const json& entry, const std::size_t index,
    const ImportManifest& manifest, const std::filesystem::path& manifest_dir,
    const Findings& findings) -> std::optional<ImportManifestJob>
  {
    const auto job_ptr = JobPointer(index);
    auto model_record = std::shared_ptr<const ModelImportRecordContent> {};
    auto model_job = json {};
    if (entry.at("type").get_ref<const std::string&>() == "model") {
      model_record
        = ReadModelRecord(entry, *manifest.mounts, job_ptr, findings);
      if (!model_record) {
        return std::nullopt;
      }
      model_job = MakeModelJob(entry, *model_record);
    }
    const auto& job = model_record ? model_job : entry;

    auto manifest_job = MakeJobFromDefaults(manifest);
    if (!ReadSourceKey(job, job_ptr, manifest_job.source_key, findings)) {
      return std::nullopt;
    }
    manifest_job.job_type = job.at("type").get<std::string>();
    const auto is_input = manifest_job.job_type == "input";
    if (!ReadJobDependencies(job, job_ptr, is_input, manifest_job, findings)) {
      return std::nullopt;
    }

    const auto read = is_input
      ? ReadInputJob(job, job_ptr, manifest, manifest_job, findings)
      : ReadSourcesAndBindings(job, job_ptr, manifest, manifest_job, findings)
        && ApplyJobOverrides(job, job_ptr, manifest, manifest_job, findings);
    if (!read) {
      return std::nullopt;
    }
    manifest_job.fbx.model_record = model_record;
    manifest_job.gltf.model_record = std::move(model_record);
    ResolveCookedRootsRelativeToManifest(manifest_dir, manifest_job);
    return manifest_job;
  }

  //! Where a job of a loaded manifest or batch was declared.
  struct JobOrigin final {
    std::string source_path;
    std::size_t index = 0;
  };

  auto MakeOrigins(const std::string& source_path, const std::size_t count)
    -> std::vector<JobOrigin>
  {
    auto origins = std::vector<JobOrigin> {};
    origins.reserve(count);
    for (std::size_t index = 0; index < count; ++index) {
      origins.push_back(
        JobOrigin { .source_path = source_path, .index = index });
    }
    return origins;
  }

  //! The jobs of each cycle in `edges`, a job's dependencies by job index: one
  //! list per cycle, ascending, ordered by their first job.
  auto FindDependencyCycles(const std::vector<std::vector<std::size_t>>& edges)
    -> std::vector<std::vector<std::size_t>>
  {
    struct Frame final {
      std::size_t node;
      std::size_t next;
    };
    constexpr auto kUnvisited = std::numeric_limits<std::size_t>::max();
    const auto count = edges.size();
    auto order = std::vector<std::size_t>(count, kUnvisited);
    auto low = std::vector<std::size_t>(count, 0U);
    auto on_stack = std::vector<bool>(count, false);
    auto stack = std::vector<std::size_t> {};
    auto frames = std::vector<Frame> {};
    auto next_order = std::size_t { 0 };
    auto cycles = std::vector<std::vector<std::size_t>> {};

    const auto visit = [&](const std::size_t node) -> void {
      order[node] = next_order;
      low[node] = next_order;
      ++next_order;
      stack.push_back(node);
      on_stack[node] = true;
      frames.push_back(Frame { .node = node, .next = 0U });
    };

    // Tarjan's strongly connected components, without recursion.
    for (std::size_t root = 0; root < count; ++root) {
      if (order[root] != kUnvisited) {
        continue;
      }
      visit(root);
      while (!frames.empty()) {
        const auto node = frames.back().node;
        if (frames.back().next < edges[node].size()) {
          const auto target = edges[node][frames.back().next];
          ++frames.back().next;
          if (order[target] == kUnvisited) {
            visit(target);
          } else if (on_stack[target]) {
            low[node] = std::min(low[node], order[target]);
          }
          continue;
        }
        frames.pop_back();
        if (!frames.empty()) {
          const auto parent = frames.back().node;
          low[parent] = std::min(low[parent], low[node]);
        }
        if (low[node] != order[node]) {
          continue;
        }
        auto component = std::vector<std::size_t> {};
        auto member = kUnvisited;
        while (member != node) {
          member = stack.back();
          stack.pop_back();
          on_stack[member] = false;
          component.push_back(member);
        }
        if (component.size() > 1U) {
          std::ranges::sort(component);
          cycles.push_back(std::move(component));
        }
      }
    }
    std::ranges::sort(cycles);
    return cycles;
  }

  //! Checks the job graph of a manifest or of a merged batch: the ids are
  //! unique, every dependency names another job, and no jobs depend on each
  //! other in a cycle.
  /*!
   @param jobs The jobs, in order.
   @param origins Where each job was declared, as `jobs` is ordered: the
     manifest file and the job's index in its `jobs`.
   @param diagnostics Receives the errors, each at its job's manifest file.
  */
  auto CheckJobGraph(const std::span<const ImportManifestJob> jobs,
    const std::span<const JobOrigin> origins,
    std::vector<ImportDiagnostic>& diagnostics) -> void
  {
    const auto report
      = [&](const std::size_t job, const std::string_view code,
          std::string message, const json::json_pointer& pointer) -> void {
      diagnostics.push_back(ImportDiagnostic {
        .severity = ImportSeverity::kError,
        .code = std::string { code },
        .message = std::move(message),
        .source_path = origins[job].source_path,
        .json_pointer = pointer.to_string(),
      });
    };

    auto by_id = std::unordered_map<std::string, std::size_t> {};
    for (std::size_t job = 0; job < jobs.size(); ++job) {
      if (jobs[job].id.empty()) {
        continue;
      }
      const auto [first, inserted] = by_id.emplace(jobs[job].id, job);
      if (inserted) {
        continue;
      }
      const auto& other = origins[first->second];
      auto where = JobPointer(other.index).to_string();
      if (other.source_path != origins[job].source_path) {
        where += " of '" + other.source_path + "'";
      }
      report(job, manifest_codes::kJobIdDuplicate,
        fmt::format(
          "job id '{}' is also the id of the job at {}", jobs[job].id, where),
        JobPointer(origins[job].index) / "id");
    }

    auto edges = std::vector<std::vector<std::size_t>>(jobs.size());
    for (std::size_t job = 0; job < jobs.size(); ++job) {
      const auto& depends_on = jobs[job].depends_on;
      for (std::size_t index = 0; index < depends_on.size(); ++index) {
        const auto& dependency = depends_on[index];
        const auto pointer
          = JobPointer(origins[job].index) / "depends_on" / index;
        if (dependency == jobs[job].id) {
          report(job, manifest_codes::kDependencySelf,
            fmt::format("job '{}' depends on itself", dependency), pointer);
          continue;
        }
        const auto target = by_id.find(dependency);
        if (target == by_id.end()) {
          report(job, manifest_codes::kDependencyMissing,
            fmt::format("job dependency '{}' names no job", dependency),
            pointer);
          continue;
        }
        edges[job].push_back(target->second);
      }
    }

    for (const auto& cycle : FindDependencyCycles(edges)) {
      auto ids = std::string {};
      for (const auto job : cycle) {
        ids += ids.empty() ? "'" : ", '";
        ids += jobs[job].id + "'";
      }
      report(cycle.front(), manifest_codes::kDependencyCycle,
        fmt::format("jobs form a dependency cycle: {}", ids),
        JobPointer(origins[cycle.front()].index));
    }
  }

  //! Parses a manifest document into its manifest, without checking the job
  //! graph, which a batch checks once its manifests are merged.
  auto ParseManifestDocument(const std::string_view text,
    const std::filesystem::path& manifest_directory,
    const std::string& declared_in, const ImportManifestLoadOptions& options,
    const Findings& findings) -> std::optional<ImportManifest>
  {
    const auto first_finding = findings.diagnostics->size();
    auto document = json {};
    try {
      document = json::parse(text);
    } catch (const std::exception& error) {
      findings.Error(manifest_codes::kInvalidJson,
        fmt::format("the manifest is not valid JSON: {}", error.what()), {});
      return std::nullopt;
    }

    if (!ValidateAgainstSchema(document, findings)
      || !RequireRelativeFolders(document, findings)) {
      return std::nullopt;
    }

    ImportManifest manifest {};
    manifest.version = document.value("version", 1U);
    if (!ReadSourceKey(document, json::json_pointer {},
          manifest.defaults.source_key, findings)) {
      return std::nullopt;
    }

    ReadValue(document, "thread_pool_size", manifest.thread_pool_size);
    ReadValue(document, "max_in_flight_jobs", manifest.max_in_flight_jobs);
    ReadValue(
      document, "job_admission_capacity", manifest.job_admission_capacity);
    if (document.contains("max_source_bytes")) {
      manifest.max_source_bytes = serio::ByteCount {
        document.at("max_source_bytes").get<uint64_t>(),
      };
    }
    if (document.contains("concurrency")) {
      auto concurrency = ImportConcurrency {};
      ApplyConcurrencyOverrides(document.at("concurrency"), concurrency);
      manifest.concurrency = concurrency;
    }

    const auto manifest_dir = std::filesystem::absolute(manifest_directory);

    if (options.mount_table) {
      manifest.mounts
        = std::make_shared<const MountTable>(*options.mount_table);
    } else {
      const auto project_root = document.value("project_root", std::string {});
      auto declarations
        = ReadMountDeclarations(document, declared_in, findings);
      if (!declarations.has_value()) {
        return std::nullopt;
      }
      manifest.mounts = std::make_shared<const MountTable>(
        ResolveMountTable(MountTableRequest {
          .project_root = options.project_root.value_or(
            (manifest_dir / project_root).lexically_normal()),
          .declarations = std::move(*declarations),
          .form = MountDeclarationForm::kCommittedFile,
          .declared_in = declared_in,
          .working_directory = std::filesystem::current_path(),
          .mounts_option = options.mounts_file,
          .owner = options.owner,
        }));
      findings.diagnostics->insert(findings.diagnostics->end(),
        manifest.mounts->diagnostics.begin(),
        manifest.mounts->diagnostics.end());
      if (!manifest.mounts->IsValid()) {
        return std::nullopt;
      }
    }
    manifest.project_root = manifest.mounts->project_root;

    auto manifest_output_root = std::string {};
    ReadValue(document, "output", manifest_output_root);
    if (!manifest_output_root.empty()) {
      ApplyManifestOutput(manifest_output_root, manifest.defaults);
    }

    if (document.contains("defaults")
      && !ReadDefaults(document.at("defaults"), manifest.defaults, findings)) {
      return std::nullopt;
    }
    ResolveCookedRootsRelativeToManifest(manifest_dir, manifest.defaults);

    const auto& entries = document.at("jobs");
    manifest.jobs.reserve(entries.size());
    for (std::size_t index = 0; index < entries.size(); ++index) {
      auto job
        = ReadJob(entries.at(index), index, manifest, manifest_dir, findings);
      if (job.has_value()) {
        manifest.jobs.push_back(std::move(*job));
      }
    }
    if (manifest.jobs.size() != entries.size()
      || HasErrorSince(*findings.diagnostics, first_finding)) {
      return std::nullopt;
    }
    return manifest;
  }

  //! Reads the manifest file at `path` and parses it, without checking the job
  //! graph.
  auto LoadManifestFile(const std::filesystem::path& path,
    const ImportManifestLoadOptions& options, const Findings& findings)
    -> std::optional<ImportManifest>
  {
    const auto bytes = serio::ReadPublishedFile(path, kMaxImportMetadataBytes);
    if (!bytes) {
      findings.Error(manifest_codes::kReadFailed,
        fmt::format("failed to read manifest '{}': {}", path.string(),
          bytes.error().message()),
        {});
      return std::nullopt;
    }
    const auto content = bytes->Unsigned();
    const auto text = std::string(content.begin(), content.end());
    return ParseManifestDocument(
      text, path.parent_path(), path.string(), options, findings);
  }

} // namespace

auto ImportManifest::Load(const std::filesystem::path& manifest_path,
  const ImportManifestLoadOptions& options,
  std::vector<ImportDiagnostic>& diagnostics) -> std::optional<ImportManifest>
{
  const auto first_finding = diagnostics.size();
  const auto source_path = manifest_path.string();
  auto manifest = LoadManifestFile(manifest_path, options,
    Findings {
      .diagnostics = make_observer(&diagnostics),
      .source_path = source_path,
    });
  if (!manifest.has_value()) {
    return std::nullopt;
  }
  CheckJobGraph(manifest->jobs, MakeOrigins(source_path, manifest->jobs.size()),
    diagnostics);
  if (HasErrorSince(diagnostics, first_finding)) {
    return std::nullopt;
  }
  return manifest;
}

auto ImportManifest::Load(const std::filesystem::path& manifest_path,
  const ImportManifestLoadOptions& options, std::ostream& error_stream)
  -> std::optional<ImportManifest>
{
  auto diagnostics = std::vector<ImportDiagnostic> {};
  auto manifest = Load(manifest_path, options, diagnostics);
  internal::WriteDiagnostics(diagnostics, error_stream);
  return manifest;
}

auto ImportManifest::LoadBatch(
  const std::span<const std::filesystem::path> manifest_paths,
  const ImportManifestLoadOptions& options,
  std::vector<ImportDiagnostic>& diagnostics) -> std::optional<ImportManifest>
{
  const auto first_finding = diagnostics.size();
  if (manifest_paths.empty()) {
    const auto source_path = std::string {};
    Findings {
      .diagnostics = make_observer(&diagnostics),
      .source_path = source_path,
    }
      .Error(
        manifest_codes::kBatchEmpty, "a batch needs at least one manifest", {});
    return std::nullopt;
  }

  auto manifests = std::vector<ImportManifest> {};
  for (const auto& path : manifest_paths) {
    const auto source_path = path.string();
    auto manifest = LoadManifestFile(path, options,
      Findings {
        .diagnostics = make_observer(&diagnostics),
        .source_path = source_path,
      });
    if (manifest.has_value()) {
      manifests.push_back(std::move(*manifest));
    }
  }
  if (manifests.size() != manifest_paths.size()) {
    return std::nullopt;
  }

  auto tables = std::vector<MountTable> {};
  tables.reserve(manifests.size());
  for (const auto& manifest : manifests) {
    tables.push_back(*manifest.mounts);
  }
  const auto conflicts = CheckMountNameConflicts(tables);
  diagnostics.insert(diagnostics.end(), conflicts.begin(), conflicts.end());
  if (!conflicts.empty()) {
    return std::nullopt;
  }

  auto batch = std::move(manifests.front());
  auto origins
    = MakeOrigins(manifest_paths.front().string(), batch.jobs.size());
  const auto same_job
    = [](const ImportManifestJob& a, const ImportManifestJob& b) -> bool {
    return a.job_type == b.job_type && !a.source_mount_path.empty()
      && content::FoldCase(a.source_mount_path)
      == content::FoldCase(b.source_mount_path)
      && a.virtual_path == b.virtual_path;
  };
  for (std::size_t position = 1; position < manifests.size(); ++position) {
    auto& manifest = manifests[position];
    const auto source_path = manifest_paths[position].string();
    auto aliases = std::unordered_map<std::string, std::string> {};
    auto added = std::vector<std::size_t> {};
    for (std::size_t index = 0; index < manifest.jobs.size(); ++index) {
      const auto& job = manifest.jobs[index];
      const auto kept = std::ranges::find_if(batch.jobs,
        [&](const ImportManifestJob& other) { return same_job(other, job); });
      if (kept != batch.jobs.end()) {
        aliases.emplace(job.id, kept->id);
        continue;
      }
      added.push_back(index);
    }
    for (const auto index : added) {
      auto& job = manifest.jobs[index];
      for (auto& dependency : job.depends_on) {
        if (const auto alias = aliases.find(dependency);
          alias != aliases.end()) {
          dependency = alias->second;
        }
      }
      batch.jobs.push_back(std::move(job));
      origins.push_back(
        JobOrigin { .source_path = source_path, .index = index });
    }
  }

  CheckJobGraph(batch.jobs, origins, diagnostics);
  if (HasErrorSince(diagnostics, first_finding)) {
    return std::nullopt;
  }
  return batch;
}

auto ImportManifest::LoadBatch(
  const std::span<const std::filesystem::path> manifest_paths,
  const ImportManifestLoadOptions& options, std::ostream& error_stream)
  -> std::optional<ImportManifest>
{
  auto diagnostics = std::vector<ImportDiagnostic> {};
  auto batch = LoadBatch(manifest_paths, options, diagnostics);
  internal::WriteDiagnostics(diagnostics, error_stream);
  return batch;
}

auto ImportManifest::Parse(const std::string_view text,
  const std::filesystem::path& manifest_directory,
  const ImportManifestLoadOptions& options,
  std::vector<ImportDiagnostic>& diagnostics) -> std::optional<ImportManifest>
{
  const auto first_finding = diagnostics.size();
  const auto source_path = std::string {};
  auto manifest = ParseManifestDocument(text, manifest_directory,
    manifest_directory.string(), options,
    Findings {
      .diagnostics = make_observer(&diagnostics),
      .source_path = source_path,
    });
  if (!manifest.has_value()) {
    return std::nullopt;
  }
  CheckJobGraph(manifest->jobs, MakeOrigins(source_path, manifest->jobs.size()),
    diagnostics);
  if (HasErrorSince(diagnostics, first_finding)) {
    return std::nullopt;
  }
  return manifest;
}

auto ImportManifest::Parse(const std::string_view text,
  const std::filesystem::path& manifest_directory,
  const ImportManifestLoadOptions& options, std::ostream& error_stream)
  -> std::optional<ImportManifest>
{
  auto diagnostics = std::vector<ImportDiagnostic> {};
  auto manifest = Parse(text, manifest_directory, options, diagnostics);
  internal::WriteDiagnostics(diagnostics, error_stream);
  return manifest;
}

auto ImportManifestJob::SetCookedRoot(const std::string& cooked_root) -> void
{
  texture.cooked_root = cooked_root;
  fbx.cooked_root = cooked_root;
  gltf.cooked_root = cooked_root;
  script.cooked_root = cooked_root;
  scripting_sidecar.cooked_root = cooked_root;
  physics_sidecar.cooked_root = cooked_root;
  input.cooked_root = cooked_root;
  buffer_container.cooked_root = cooked_root;
  material_descriptor.cooked_root = cooked_root;
  physics_material_descriptor.cooked_root = cooked_root;
  collision_shape_descriptor.cooked_root = cooked_root;
  geometry_descriptor.cooked_root = cooked_root;
  scene_descriptor.cooked_root = cooked_root;
}

auto ImportManifestJob::SourcePath() const -> std::filesystem::path
{
  if (job_type == "texture") {
    return texture.source_path;
  }
  if (job_type == "texture-descriptor") {
    return texture.source_path;
  }
  if (job_type == "fbx") {
    return fbx.source_path;
  }
  if (job_type == "gltf") {
    return gltf.source_path;
  }
  if (job_type == "script") {
    return script.source_path;
  }
  if (job_type == "script-sidecar") {
    return scripting_sidecar.source_path;
  }
  if (job_type == "physics-sidecar") {
    return physics_sidecar.source_path;
  }
  if (job_type == "input") {
    return input.source_path;
  }
  if (job_type == "buffer-container") {
    return buffer_container.descriptor_path;
  }
  if (job_type == "material-descriptor") {
    return material_descriptor.descriptor_path;
  }
  if (job_type == "physics-material-descriptor") {
    return physics_material_descriptor.descriptor_path;
  }
  if (job_type == "collision-shape-descriptor") {
    return collision_shape_descriptor.descriptor_path;
  }
  if (job_type == "geometry-descriptor") {
    return geometry_descriptor.descriptor_path;
  }
  if (job_type == "scene-descriptor") {
    return scene_descriptor.descriptor_path;
  }
  return {};
}

auto ImportManifestJob::BuildRequest(std::ostream& error_stream,
  std::shared_ptr<const CapturedInputSet> captured_inputs) const
  -> std::optional<ImportRequest>
{
  const auto AttachOrchestration =
    [&](std::optional<ImportRequest> request) -> std::optional<ImportRequest> {
    if (!request.has_value()) {
      return std::nullopt;
    }
    request->asset_virtual_path = virtual_path;
    request->captured_inputs = captured_inputs;
    request->source_key = source_key;
    request->source_mount_path = source_mount_path;
    request->source_mounts = mounts;
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
        .mounts = mounts,
      },
      error_stream, captured_inputs));
  }
  if (job_type == "material-descriptor") {
    return AttachOrchestration(internal::BuildMaterialDescriptorRequest(
      material_descriptor, error_stream, captured_inputs));
  }
  if (job_type == "physics-material-descriptor") {
    return AttachOrchestration(internal::BuildPhysicsMaterialDescriptorRequest(
      physics_material_descriptor, error_stream, captured_inputs));
  }
  if (job_type == "collision-shape-descriptor") {
    return AttachOrchestration(internal::BuildCollisionShapeDescriptorRequest(
      collision_shape_descriptor, error_stream, captured_inputs));
  }
  if (job_type == "buffer-container") {
    CHECK_NOTNULL_F(mounts.get(), "A manifest job carries its mount table");
    return AttachOrchestration(
      internal::BuildBufferContainerRequest(buffer_container,
        BufferContainerIdentity(virtual_path, source_mount_path), *mounts,
        error_stream, captured_inputs));
  }
  if (job_type == "geometry-descriptor") {
    CHECK_NOTNULL_F(mounts.get(), "A manifest job carries its mount table");
    return AttachOrchestration(
      internal::BuildGeometryDescriptorRequest(geometry_descriptor,
        virtual_path, *mounts, error_stream, captured_inputs));
  }
  if (job_type == "scene-descriptor") {
    return AttachOrchestration(internal::BuildSceneDescriptorRequest(
      scene_descriptor, error_stream, captured_inputs));
  }
  if (job_type == "fbx") {
    return AttachOrchestration(internal::BuildSceneRequest(
      fbx, ImportFormat::kFbx, error_stream, captured_inputs));
  }
  if (job_type == "gltf") {
    return AttachOrchestration(internal::BuildSceneRequest(
      gltf, ImportFormat::kGltf, error_stream, captured_inputs));
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

auto ImportManifest::AddBaseCookedContextRoots(
  const std::span<const std::filesystem::path> roots) -> void
{
  auto base = std::vector<std::string> {};
  base.reserve(roots.size());
  for (const auto& root : roots) {
    base.push_back(std::filesystem::absolute(root).lexically_normal().string());
  }
  const auto prepend = [&base](std::vector<std::string>& job_roots) -> void {
    job_roots.insert(job_roots.begin(), base.begin(), base.end());
  };
  for (auto& job : jobs) {
    prepend(job.material_descriptor.cooked_context_roots);
    prepend(job.scene_descriptor.cooked_context_roots);
    prepend(job.geometry_descriptor.cooked_context_roots);
    prepend(job.scripting_sidecar.cooked_context_roots);
    prepend(job.input.cooked_context_roots);
  }
}

auto ImportManifest::BuildRequests(std::ostream& error_stream,
  const std::shared_ptr<const CapturedInputSet>& captured_inputs) const
  -> std::vector<ImportRequest>
{
  std::vector<ImportRequest> requests;
  requests.reserve(jobs.size());
  for (const auto& job : jobs) {
    if (auto request = job.BuildRequest(error_stream, captured_inputs)) {
      requests.push_back(std::move(*request));
    }
  }
  return requests;
}

} // namespace oxygen::content::import
