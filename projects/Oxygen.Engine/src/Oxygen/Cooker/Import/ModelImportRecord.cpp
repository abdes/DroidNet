//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <algorithm>
#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <filesystem>
#include <fstream>
#include <ios>
#include <iterator>
#include <memory>
#include <optional>
#include <set>
#include <span>
#include <sstream>
#include <stdexcept>
#include <stop_token>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

#include <nlohmann/json-schema.hpp>
#include <nlohmann/json.hpp>
#include <nlohmann/json_fwd.hpp>

#include <Oxygen/Base/Filesystem.h>
#include <Oxygen/Base/Logging.h>
#include <Oxygen/Base/Sha256.h>
#include <Oxygen/Base/Uuid.h>
#include <Oxygen/Content/LoaderContext.h>
#include <Oxygen/Content/Loaders/GeometryLoader.h>
#include <Oxygen/Cooker/Import/ImportDiagnostics.h>
#include <Oxygen/Cooker/Import/ImportManifest.h>
#include <Oxygen/Cooker/Import/ImportReport.h>
#include <Oxygen/Cooker/Import/ImportRequest.h>
#include <Oxygen/Cooker/Import/Internal/GeometryDefaults.h>
#include <Oxygen/Cooker/Import/Internal/ImportManifest_schema.h>
#include <Oxygen/Cooker/Import/Internal/Utils/JsonSchemaValidation.h>
#include <Oxygen/Cooker/Import/ModelImportRecord.h>
#include <Oxygen/Cooker/Import/ModelImportState.h>
#include <Oxygen/Cooker/Import/SceneImportSettings.h>
#include <Oxygen/Cooker/Import/SurfaceProvenance.h>
#include <Oxygen/Cooker/Import/TextureImportSettings.h>
#include <Oxygen/Cooker/Loose/Inspection.h>
#include <Oxygen/Cooker/Loose/Validation.h>
#include <Oxygen/Core/Types/DescriptorExtensions.h>
#include <Oxygen/Data/AssetType.h>
#include <Oxygen/Data/LooseCookedIndexFormat.h>
#include <Oxygen/Data/SceneIds.h>
#include <Oxygen/Data/StableId.h>
#include <Oxygen/Serio/AtomicFile.h>
#include <Oxygen/Serio/FileLock.h>
#include <Oxygen/Serio/FileStream.h>
#include <Oxygen/Serio/Reader.h>

namespace oxygen::content::import {
namespace {

  using nlohmann::json;
  using nlohmann::ordered_json;

  //! Job keys a record's settings never hold: the record and its manifest own
  //! them.
  constexpr auto kStructuralKeys = std::array {
    "id",
    "type",
    "source",
    "virtual_path",
    "record",
    "depends_on",
    "output",
    "layout",
    "cooked_context_roots",
    "source_key",
    "surface_provenance",
  };

  constexpr auto kHeadFileName = "head.json";

  auto ReadText(const std::filesystem::path& path) -> std::string
  {
    std::ifstream input(base::ToNativePath(path), std::ios::binary);
    if (!input) {
      throw std::runtime_error(
        "Cannot read model import file: " + path.string());
    }
    std::string text(std::istreambuf_iterator<char>(input), {});
    if (input.bad()) {
      throw std::runtime_error(
        "Model import file read failed: " + path.string());
    }
    return text;
  }

  auto Digest(std::string_view text) -> base::Sha256Digest
  {
    return base::ComputeSha256(
      std::as_bytes(std::span(text.data(), text.size())));
  }

  auto ReadDigest(const std::string& text) -> base::Sha256Digest
  {
    const auto digest = base::Sha256DigestFromHex(text);
    if (!digest.has_value()) {
      throw std::invalid_argument("Invalid model import digest: " + text);
    }
    return *digest;
  }

  auto WriteText(const std::filesystem::path& path, const std::string& text)
    -> void
  {
    const auto result = serio::WriteFileAtomically(
      path, std::as_bytes(std::span(text.data(), text.size())));
    if (!result) {
      throw std::system_error(
        result.error(), "Cannot write model import file " + path.string());
    }
    if (result.value().durability_error) {
      LOG_F(WARNING,
        "Model import file committed; directory durability failed: {}",
        result.value().durability_error.message());
    }
  }

  auto Absolute(const std::filesystem::path& path) -> std::filesystem::path
  {
    return base::ToLogicalPath(std::filesystem::weakly_canonical(
      base::ToNativePath(base::ToLogicalPath(path))));
  }

  auto IsWithin(const std::filesystem::path& path,
    const std::filesystem::path& root) -> bool
  {
    const auto relative = Absolute(path).lexically_relative(Absolute(root));
    return !relative.empty() && !relative.is_absolute()
      && *relative.begin() != ".." && relative != ".";
  }

  auto RequireWithin(const std::filesystem::path& path,
    const std::filesystem::path& root) -> void
  {
    if (!IsWithin(path, root)) {
      throw std::invalid_argument("Model import path must stay inside "
        + root.generic_string() + ": " + path.generic_string());
    }
  }

  auto Exists(const std::filesystem::path& path) -> bool
  {
    std::error_code error;
    return std::filesystem::is_regular_file(base::ToNativePath(path), error);
  }

  auto SameFile(const std::filesystem::path& a, const std::filesystem::path& b)
    -> bool
  {
    std::error_code error;
    const auto same = std::filesystem::equivalent(
      base::ToNativePath(a), base::ToNativePath(b), error);
    return !error && same;
  }

  //! The content root of a record: its folder's parent.
  auto ContentRootOf(const std::filesystem::path& record_path)
    -> std::filesystem::path
  {
    return Absolute(record_path).parent_path().parent_path();
  }

  auto RecordLock(const std::filesystem::path& path) -> serio::FileLock
  {
    auto lock_path = path;
    lock_path += ".lock";
    auto lock = serio::FileLock::TryAcquire(lock_path,
      serio::FileLockMode::kExclusive, serio::FileLockOpenMode::kOpenOrCreate);
    if (!lock) {
      throw std::system_error(lock.error(), "Model import record is busy");
    }
    return std::move(lock).value();
  }

  auto FormatOf(const std::filesystem::path& source) -> ImportFormat
  {
    ImportRequest request;
    request.source_path = source;
    return request.GetFormat();
  }

  auto JobType(const std::filesystem::path& source) -> std::string_view
  {
    const auto format = FormatOf(source);
    if (format != ImportFormat::kGltf && format != ImportFormat::kFbx) {
      throw std::invalid_argument(
        "A model import source must be glTF, GLB or FBX: " + source.string());
    }
    return format == ImportFormat::kFbx ? "fbx" : "gltf";
  }

  auto RequireSettings(const json& settings) -> void
  {
    if (!settings.is_object()) {
      throw std::invalid_argument("Model import settings must be an object");
    }
    for (const auto* key : kStructuralKeys) {
      if (settings.contains(key)) {
        throw std::invalid_argument(
          std::string("Model import settings hold no '") + key
          + "'; the record and its manifest own it");
      }
    }
  }

  auto MakeJobJson(const ModelImportRecordContent& content) -> json
  {
    auto job = json::parse(content.settings_json);
    RequireSettings(job);
    job.update({
      { "id", "model" },
      { "type", JobType(content.source) },
      { "source", Absolute(content.source).generic_string() },
      { "virtual_path", content.virtual_path },
    });
    return job;
  }

  //! Validates the settings as the import manifest does, by parsing the
  //! record's job.
  auto ValidateSettings(const ModelImportRecordContent& content) -> void
  {
    const auto manifest = json {
      { "version", 1 },
      { "jobs", json::array({ MakeJobJson(content) }) },
    };
    std::ostringstream errors;
    const auto parsed = ImportManifest::Parse(
      manifest.dump(), content.source.parent_path(), std::nullopt, errors);
    if (!parsed || parsed->jobs.size() != 1U) {
      throw std::invalid_argument(
        "Invalid model import settings: " + errors.str());
    }
  }

  auto ReadFingerprint(const json& value) -> ModelSourceFingerprint
  {
    auto fingerprint = ModelSourceFingerprint {
      .size = value.at("size").get<uint64_t>(),
      .sha256 = ReadDigest(value.at("sha256").get<std::string>()),
      .files = {},
    };
    for (const auto& file : value.at("files")) {
      fingerprint.files.push_back({
        .path = file.at("path").get<std::string>(),
        .size = file.at("size").get<uint64_t>(),
        .sha256 = ReadDigest(file.at("sha256").get<std::string>()),
      });
    }
    return fingerprint;
  }

  auto WriteFingerprint(const ModelSourceFingerprint& fingerprint)
    -> ordered_json
  {
    auto files = ordered_json::array();
    for (const auto& file : fingerprint.files) {
      files.push_back({
        { "path", file.path },
        { "size", file.size },
        { "sha256", base::ToHexString(file.sha256) },
      });
    }
    return {
      { "size", fingerprint.size },
      { "sha256", base::ToHexString(fingerprint.sha256) },
      { "files", std::move(files) },
    };
  }

  auto WriteNodes(const std::vector<ModelPublishedNode>& published)
    -> ordered_json
  {
    auto nodes = ordered_json::array();
    for (const auto& node : published) {
      nodes.push_back({
        { "anchor", node.anchor },
        { "node_id", data::to_string(node.node_id) },
      });
    }
    return nodes;
  }

  auto ReadNodes(const json& document, const char* key)
    -> std::vector<ModelPublishedNode>
  {
    auto nodes = std::vector<ModelPublishedNode> {};
    for (const auto& node : document.value(key, json::array())) {
      const auto id
        = data::NodeId::FromString(node.at("node_id").get<std::string>());
      if (!id) {
        throw std::invalid_argument(
          std::string("Invalid node ID in '") + key + "'");
      }
      nodes.push_back({
        .anchor = node.at("anchor").get<std::string>(),
        .node_id = id.value(),
      });
    }
    return nodes;
  }

  auto IdentityOfName(const std::filesystem::path& record_path)
    -> std::optional<Uuid>
  {
    const auto name = record_path.filename().string();
    if (!name.ends_with(ModelImportRecord::kRecordSuffix)) {
      return std::nullopt;
    }
    const auto identity = Uuid::FromString(
      name.substr(0, name.size() - ModelImportRecord::kRecordSuffix.size()));
    if (!identity || !identity.value().IsValidV7()) {
      return std::nullopt;
    }
    return identity.value();
  }

  //! Every record of `content_root`, by path.
  auto RecordPaths(const std::filesystem::path& content_root)
    -> std::vector<std::filesystem::path>
  {
    const auto folder
      = Absolute(content_root) / ModelImportRecord::kRecordsFolder;
    auto paths = std::vector<std::filesystem::path> {};
    if (!std::filesystem::exists(base::ToNativePath(folder))) {
      return paths;
    }
    for (const auto& entry :
      std::filesystem::directory_iterator(base::ToNativePath(folder))) {
      if (entry.is_regular_file()
        && entry.path().filename().string().ends_with(
          ModelImportRecord::kRecordSuffix)) {
        paths.push_back(Absolute(base::ToLogicalPath(entry.path())));
      }
    }
    std::ranges::sort(paths);
    return paths;
  }

  //! The records of a content root that can be read, and a warning for each
  //! file that cannot.
  struct RecordScan final {
    std::vector<std::pair<std::filesystem::path, ModelImportRecordContent>>
      records;
    std::vector<ImportDiagnostic> skipped;
  };

  //! Reads every record of `content_root`. A file that is not a valid record,
  //! damaged or of an older format, is no model's record: it is skipped, so it
  //! never stops importing the other models.
  auto ScanRecords(const std::filesystem::path& content_root) -> RecordScan
  {
    auto scan = RecordScan {};
    for (const auto& path : RecordPaths(content_root)) {
      try {
        scan.records.emplace_back(path, ModelImportRecord::Read(path));
      } catch (const std::exception& error) {
        scan.skipped.push_back({
          .severity = ImportSeverity::kWarning,
          .code = "import.model_record_unreadable",
          .message = "Skipped " + path.generic_string()
            + ", which is not a valid model import record: " + error.what(),
          .source_path = path.generic_string(),
          .object_path = {},
        });
      }
    }
    return scan;
  }

  //! Refuses a second record with the source identity or the virtual path of
  //! `content`.
  auto RequireUniqueRecord(const std::filesystem::path& record_path,
    const ModelImportRecordContent& content) -> void
  {
    const auto path = Absolute(record_path);
    for (const auto& [other_path, other] :
      ScanRecords(ContentRootOf(path)).records) {
      if (other_path == path) {
        continue;
      }
      if (other.source_identity == content.source_identity) {
        throw std::invalid_argument("Two model import records have the source "
                                    "identity "
          + content.source_identity.ToString() + ": " + path.generic_string()
          + " and " + other_path.generic_string());
      }
      if (other.virtual_path == content.virtual_path) {
        throw std::invalid_argument("Two model import records have the "
                                    "virtual path "
          + content.virtual_path + ": " + path.generic_string() + " and "
          + other_path.generic_string());
      }
    }
  }

  auto GenerationDirectory(const std::filesystem::path& record_path,
    const ModelImportRecordContent& content) -> std::filesystem::path
  {
    const auto root = ContentRootOf(record_path);
    const auto path = root / ".cooked" / ModelImportRecord::kRecordsFolder
      / content.source_identity.ToString();
    RequireWithin(path, root);
    return path;
  }

  struct Head final {
    Uuid id;
    std::string index_sha256;
  };

  auto ReadHead(const std::filesystem::path& directory) -> std::optional<Head>
  {
    const auto path = directory / kHeadFileName;
    if (!Exists(path)) {
      return std::nullopt;
    }
    const auto document = json::parse(ReadText(path));
    const auto id = Uuid::FromString(document.at("id").get<std::string>());
    if (!id || !id.value().IsValidV7()) {
      throw std::invalid_argument("Invalid published import generation");
    }
    auto index = document.at("index_sha256").get<std::string>();
    static_cast<void>(ReadDigest(index));
    return Head { .id = id.value(), .index_sha256 = std::move(index) };
  }

  //! What an attempt must find unchanged when it publishes: the record and
  //! the selected generation, either of which a competing attempt changes.
  auto Baseline(const std::filesystem::path& record_path,
    const ModelImportRecordContent& content) -> base::Sha256Digest
  {
    const auto head = GenerationDirectory(record_path, content) / kHeadFileName;
    return Digest(ReadText(record_path) + "\n"
      + (Exists(head) ? ReadText(head) : std::string {}));
  }

  auto SelectedPath(const std::filesystem::path& record_path,
    const ModelImportRecordContent& content)
    -> std::optional<std::filesystem::path>
  {
    const auto directory = GenerationDirectory(record_path, content);
    const auto head = ReadHead(directory);
    if (!head) {
      return std::nullopt;
    }
    return directory / head->id.ToString();
  }

  auto TextureSettings(const TextureImportSettings& settings) -> json
  {
    json result = {
      { "flip_y", settings.flip_y },
      { "force_rgba", settings.force_rgba },
      { "flip_normal_green", settings.flip_normal_green },
      { "renormalize", settings.renormalize_normals },
      { "bake_hdr", settings.bake_hdr_to_ldr },
      { "exposure_ev", settings.exposure_ev },
      { "cubemap", settings.cubemap },
      { "equirect_to_cube", settings.equirect_to_cube },
    };
    if (settings.max_mip_levels > 0U) {
      result.update({ { "max_mips", settings.max_mip_levels } });
    }
    if (settings.max_size > 0U) {
      result.update({ { "max_size", settings.max_size } });
    }
    if (settings.cube_face_size > 0U) {
      result.update({ { "cube_face_size", settings.cube_face_size } });
    }
    const auto add = [&](const char* name, const std::string& value) -> void {
      if (!value.empty()) {
        result.update({ { name, value } });
      }
    };
    add("intent", settings.intent);
    add("color_space", settings.color_space);
    add("output_format", settings.output_format);
    add("data_format", settings.data_format);
    add("preset", settings.preset);
    add("mip_policy", settings.mip_policy);
    add("mip_filter", settings.mip_filter);
    add("bc7_quality", settings.bc7_quality);
    add("packing_policy", settings.packing_policy);
    add("hdr_handling", settings.hdr_handling);
    add("cube_layout", settings.cube_layout);
    return result;
  }

  auto Diagnostic(const ImportSeverity severity, std::string code,
    std::string message, const std::filesystem::path& source)
    -> ImportDiagnostic
  {
    return {
      .severity = severity,
      .code = std::move(code),
      .message = std::move(message),
      .source_path = source.generic_string(),
      .object_path = {},
    };
  }

} // namespace

struct ModelImportRecord::State {
  std::filesystem::path record_path {};
  ModelImportRecordContent content;
  base::Sha256Digest baseline {};
  Uuid generation;
  ImportRequest request {};
  std::optional<std::filesystem::path> previous;
  std::optional<serio::FileLock> generation_lock;
  std::optional<base::Sha256Digest> validated_index;
  std::optional<base::Sha256Digest> validated_provenance;
  std::vector<ImportDiagnostic> notes;
  bool published = false;
  std::atomic_bool claimed { false };
};

ModelImportRecord::ModelImportRecord(std::unique_ptr<State> state)
  : state_(std::move(state))
{
}

ModelImportRecord::~ModelImportRecord() = default;

auto ModelImportRecord::Read(const std::filesystem::path& record_path)
  -> ModelImportRecordContent
{
  const auto path = Absolute(record_path);
  const auto document = json::parse(ReadText(path));
  nlohmann::json_schema::json_validator validator;
  validator.set_root_schema(json::parse(kModelImportSchema));
  internal::ValidateJsonSchema(
    validator, document, "Model import record " + path.generic_string());

  auto content = ModelImportRecordContent {};
  content.source_identity
    = Uuid::FromString(document.at("source_identity").get<std::string>())
        .value();
  if (IdentityOfName(path) != content.source_identity) {
    throw std::invalid_argument("A model import record is named by its source "
                                "identity: "
      + path.generic_string() + " should be "
      + content.source_identity.ToString() + std::string(kRecordSuffix));
  }
  const auto source
    = std::filesystem::path(document.at("source").get<std::string>());
  content.source
    = Absolute(source.is_absolute() ? source : path.parent_path() / source);
  content.virtual_path = document.at("virtual_path").get<std::string>();
  content.settings_json = document.at("settings").dump();
  content.surface_provenance = document.contains("surface_provenance")
    ? SurfaceProvenance::Parse(
        document.at("surface_provenance").dump(), content.source_identity)
    : std::make_shared<const SurfaceProvenance>(content.source_identity);

  auto anchors = std::set<std::string> {};
  for (const auto& entry : document.value("node_continuity", json::array())) {
    auto& added = content.node_continuity.emplace_back(ModelNodeContinuity {
      .anchor = entry.at("anchor").get<std::string>(),
      .continues = entry.at("continues").get<std::string>(),
    });
    if (!anchors.insert(added.anchor).second) {
      throw std::invalid_argument(
        "Two node continuity entries have the anchor '" + added.anchor + "'");
    }
  }
  content.published_nodes = ReadNodes(document, "published_nodes");
  content.new_nodes = ReadNodes(document, "new_nodes");
  content.vanished_nodes = ReadNodes(document, "vanished_nodes");
  if (document.contains("source_fingerprint")) {
    content.source_fingerprint
      = ReadFingerprint(document.at("source_fingerprint"));
  }
  ValidateSettings(content);
  return content;
}

auto ModelImportRecord::Serialize(const ModelImportRecordContent& content,
  const std::filesystem::path& record_path) -> std::string
{
  const auto path = Absolute(record_path);
  const auto source = Absolute(content.source);
  const auto source_text = IsWithin(source, ContentRootOf(path))
    ? source.lexically_relative(path.parent_path()).generic_string()
    : source.generic_string();
  auto continuity = ordered_json::array();
  for (const auto& entry : content.node_continuity) {
    continuity.push_back({
      { "anchor", entry.anchor },
      { "continues", entry.continues },
    });
  }
  auto provenance = content.surface_provenance
    ? ordered_json::parse(content.surface_provenance->Serialize())
    : ordered_json::parse(
        SurfaceProvenance(content.source_identity).Serialize());
  auto record = ordered_json {
    { "schema_version", kSchemaVersion },
    { "source", source_text },
    { "virtual_path", content.virtual_path },
    { "settings", ordered_json::parse(content.settings_json) },
    { "source_identity", content.source_identity.ToString() },
    { "surface_provenance", std::move(provenance) },
    { "node_continuity", std::move(continuity) },
    { "published_nodes", WriteNodes(content.published_nodes) },
    { "new_nodes", WriteNodes(content.new_nodes) },
    { "vanished_nodes", WriteNodes(content.vanished_nodes) },
  };
  if (content.source_fingerprint) {
    record.update({
      {
        "source_fingerprint",
        WriteFingerprint(*content.source_fingerprint),
      },
    });
  }
  return record.dump(2) + "\n";
}

auto ModelImportRecord::SerializeImportUpdate(
  const ImportRequest& request, const ImportReport& report) -> std::string
{
  if (!request.model_import || !report.success
    || report.surface_provenance_json.empty()) {
    return {};
  }
  auto update = ordered_json {
    {
      "surface_provenance",
      ordered_json::parse(report.surface_provenance_json),
    },
    { "published_nodes", WriteNodes(report.published_nodes) },
    { "new_nodes", WriteNodes(report.new_nodes) },
    { "vanished_nodes", WriteNodes(report.vanished_nodes) },
  };
  if (report.source_fingerprint) {
    update.update({
      {
        "source_fingerprint",
        WriteFingerprint(*report.source_fingerprint),
      },
    });
  }
  return update.dump();
}

auto ModelImportRecord::MakeJob(const ModelImportRecordContent& content)
  -> std::string
{
  return MakeJobJson(content).dump();
}

auto ModelImportRecord::MakeSettings(const SceneImportSettings& settings)
  -> std::string
{
  auto job = TextureSettings(settings.texture_defaults);
  job.update({
    { "content_hashing", settings.with_content_hashing },
    {
      "content_flags",
      {
        { "textures", settings.import_textures },
        { "materials", settings.import_materials },
        { "geometry", settings.import_geometry },
        { "scene", settings.import_scene },
      },
    },
    { "bake_transforms", settings.bake_transforms },
    { "gltf_omitted_light_range_m", settings.gltf_omitted_light_range_m },
    { "import_lights", settings.import_lights },
    { "import_cameras", settings.import_cameras },
    { "light_intensity_scale", settings.light_intensity_scale },
    { "normals_smoothing_angle_deg", settings.normals_smoothing_angle_deg },
  });
  const auto add = [&](const char* name, const std::string& value) -> void {
    if (!value.empty()) {
      job.update({ { name, value } });
    }
  };
  add("name", settings.job_name);
  add("unit_policy", settings.unit_policy);
  add("normals_policy", settings.normals_policy);
  add("tangents_policy", settings.tangents_policy);
  add("node_pruning", settings.node_pruning);
  add("gltf_scene", settings.gltf_scene);
  add("up_axis", settings.up_axis);
  if (!settings.material_remap.empty()) {
    job.update({ { "material_remap", settings.material_remap } });
  }
  if (!settings.geometry_defaults.empty()) {
    auto geometries = json::object();
    for (const auto& [anchor, defaults] : settings.geometry_defaults) {
      auto entry = json::object();
      if (defaults.lod_policy.has_value()) {
        entry.emplace(
          "lod_policy", internal::WriteLodPolicy(*defaults.lod_policy));
      }
      if (!defaults.surfaces.empty()) {
        auto surfaces = json::object();
        for (const auto& [id, surface] : defaults.surfaces) {
          auto values = json::object();
          if (surface.visible.has_value()) {
            values.emplace("visible", *surface.visible);
          }
          if (surface.casts_shadows.has_value()) {
            values.emplace("casts_shadows", *surface.casts_shadows);
          }
          surfaces.emplace(data::to_string(id), std::move(values));
        }
        entry.emplace("surfaces", std::move(surfaces));
      }
      geometries.emplace(anchor, std::move(entry));
    }
    job.update({ { "geometry_defaults", std::move(geometries) } });
  }
  if (settings.unit_scale_set) {
    job.update({ { "unit_scale", settings.unit_scale } });
  }
  if (!settings.texture_overrides.empty()) {
    auto overrides = json::object();
    for (const auto& [name, value] : settings.texture_overrides) {
      overrides.emplace(name, TextureSettings(value));
    }
    job.update({ { "texture_overrides", std::move(overrides) } });
  }
  return job.dump();
}

auto ModelImportRecord::PathFor(const std::filesystem::path& content_root,
  const Uuid& source_identity) -> std::filesystem::path
{
  return Absolute(content_root) / kRecordsFolder
    / (source_identity.ToString() + std::string(kRecordSuffix));
}

auto ModelImportRecord::LocateRecord(const ModelImportRecordQuery& query)
  -> ModelImportRecordLocation
{
  const auto& [content_root, source, virtual_path] = query;
  const auto file = Absolute(source);
  auto scan = ScanRecords(content_root);
  const auto& records = scan.records;
  auto location = ModelImportRecordLocation {
    .record_path = {},
    .exists = false,
    .diagnostics = std::move(scan.skipped),
  };

  // 1. The user names the model by its virtual path.
  for (const auto& [path, record] : records) {
    if (record.virtual_path != virtual_path) {
      continue;
    }
    location.record_path = path;
    location.exists = true;
    if (!SameFile(record.source, file)) {
      location.diagnostics.push_back(
        Diagnostic(ImportSeverity::kInfo, "import.source_rebound",
          "The model " + record.virtual_path + " now imports from "
            + file.generic_string() + " instead of "
            + record.source.generic_string(),
          file));
    }
    return location;
  }

  // 2. Another model from the same file.
  for (const auto& [path, record] : records) {
    if (SameFile(record.source, file)) {
      location.diagnostics.push_back(
        Diagnostic(ImportSeverity::kInfo, "import.source_has_model",
          "The file is already the source of the model " + record.virtual_path
            + " (" + path.generic_string()
            + "); this import makes another model. To rename that model's "
              "virtual path, import with --record "
            + path.generic_string(),
          file));
      location.record_path = PathFor(content_root, Uuid::Generate());
      return location;
    }
  }

  // 3. The same file moved, its virtual path with it: only on proof.
  auto fingerprint = std::optional<std::pair<uint64_t, base::Sha256Digest>> {};
  const auto same_bytes = [&](const ModelImportRecordContent& record) -> bool {
    if (!record.source_fingerprint) {
      return false;
    }
    if (!fingerprint) {
      fingerprint.emplace(std::filesystem::file_size(base::ToNativePath(file)),
        base::ComputeFileSha256(file));
    }
    return record.source_fingerprint->size == fingerprint->first
      && record.source_fingerprint->sha256 == fingerprint->second;
  };
  auto moved = std::vector<std::filesystem::path> {};
  for (const auto& [path, record] : records) {
    if (!Exists(record.source) && same_bytes(record)) {
      moved.push_back(path);
    }
  }
  if (moved.size() == 1U) {
    const auto& record = std::ranges::find(records, moved.front(),
      &std::pair<std::filesystem::path, ModelImportRecordContent>::first)
                           ->second;
    location.record_path = moved.front();
    location.exists = true;
    location.diagnostics.push_back(
      Diagnostic(ImportSeverity::kInfo, "import.source_relocated",
        "The model " + record.virtual_path + " moved from "
          + record.source.generic_string() + "; it keeps its identity as "
          + std::string(virtual_path),
        file));
    return location;
  }
  if (moved.size() > 1U) {
    auto names = std::string {};
    for (const auto& path : moved) {
      names += (names.empty() ? "" : ", ") + path.generic_string();
    }
    location.diagnostics.push_back(
      Diagnostic(ImportSeverity::kWarning, "import.source_relocation_ambiguous",
        "Several records whose source is missing have this file's bytes; none "
        "is taken: "
          + names + ". Import with --record to choose one",
        file));
  }

  // 4. A new model, with what may explain it.
  for (const auto& [path, record] : records) {
    if (Exists(record.source) && same_bytes(record)) {
      location.diagnostics.push_back(
        Diagnostic(ImportSeverity::kInfo, "import.source_copy",
          "The file is a copy of " + record.source.generic_string()
            + ", the source of the model " + record.virtual_path
            + "; importing it with that virtual path makes it that model's "
              "source",
          file));
    } else if (!Exists(record.source)
      && record.source.filename() == file.filename()) {
      location.diagnostics.push_back(Diagnostic(ImportSeverity::kWarning,
        "import.source_relocation_candidate",
        "The model " + record.virtual_path + " lost its source "
          + record.source.generic_string()
          + " and may be this file, moved and changed. To rebind it: "
            "--record "
          + path.generic_string() + " " + file.generic_string(),
        file));
    }
  }
  location.record_path = PathFor(content_root, Uuid::Generate());
  return location;
}

auto ModelImportRecord::Save(const ModelImportRecordSave& save) -> void
{
  const auto path = Absolute(save.record_path);
  const auto root = Absolute(save.content_root);
  if (path.parent_path() != root / kRecordsFolder) {
    throw std::invalid_argument("A model import record lives in "
      + (root / kRecordsFolder).generic_string() + ": "
      + path.generic_string());
  }
  std::filesystem::create_directories(base::ToNativePath(path.parent_path()));
  const auto lock = RecordLock(path);
  const auto exists = Exists(path);
  auto content = ModelImportRecordContent {};
  if (exists) {
    content = Read(path);
  } else {
    const auto identity = IdentityOfName(path);
    if (!identity) {
      throw std::invalid_argument("A new model import record is named by a "
                                  "UUIDv7 source identity: "
        + path.generic_string());
    }
    content.source_identity = *identity;
    content.surface_provenance
      = std::make_shared<const SurfaceProvenance>(*identity);
  }
  if (!save.source.empty()) {
    content.source = Absolute(save.source);
  }
  if (!save.virtual_path.empty()) {
    content.virtual_path = save.virtual_path;
  }
  if (save.settings_json) {
    const auto settings = json::parse(*save.settings_json);
    RequireSettings(settings);
    content.settings_json = settings.dump();
  }
  if (content.source.empty() || content.virtual_path.empty()) {
    throw std::invalid_argument(
      "A new model import record needs a source and a virtual path");
  }
  ValidateSettings(content);
  RequireUniqueRecord(path, content);
  const auto text = Serialize(content, path);
  if (!exists || ReadText(path) != text) {
    WriteText(path, text);
  }
}

auto ModelImportRecord::SaveRecipeFile(const ModelImportRecordSave& save,
  const std::filesystem::path& recipe_path, const ImportFormat expected_format)
  -> ModelImportRecordLocation
{
  const auto path = Absolute(recipe_path);
  const auto recipe = json::parse(ReadText(path));
  // A recipe holds content settings only: no defaults, layout or output, and
  // no worker budgets, which belong to the command's execution options.
  for (const auto& [key, value] : recipe.items()) {
    static_cast<void>(value);
    if (key != "$schema" && key != "version" && key != "jobs") {
      throw std::invalid_argument("A recipe holds one gltf or fbx job and no '"
        + key + "'; worker budgets are execution options");
    }
  }
  std::ostringstream errors;
  const auto manifest = ImportManifest::Parse(
    recipe.dump(), path.parent_path(), std::nullopt, errors);
  if (!manifest || manifest->jobs.size() != 1U
    || !manifest->jobs.front().depends_on.empty()) {
    throw std::invalid_argument(
      "A recipe is one self-contained gltf or fbx job: " + errors.str());
  }
  const auto& parsed = manifest->jobs.front();
  const auto format
    = parsed.job_type == "fbx" ? ImportFormat::kFbx : ImportFormat::kGltf;
  const auto& scene = format == ImportFormat::kFbx ? parsed.fbx : parsed.gltf;
  if ((parsed.job_type != "gltf" && parsed.job_type != "fbx")
    || format != expected_format || FormatOf(scene.source_path) != format) {
    throw std::invalid_argument(
      "Recipe format does not match the import command");
  }
  auto settings = recipe.at("jobs").at(0);
  for (const auto* key : kStructuralKeys) {
    settings.erase(key);
  }
  auto location = save.record_path.empty()
    ? LocateRecord({
        .content_root = save.content_root,
        .source = scene.source_path,
        .virtual_path = scene.virtual_path,
      })
    : ModelImportRecordLocation {
        .record_path = Absolute(save.record_path),
        .exists = Exists(save.record_path),
        .diagnostics = {},
      };
  Save({
    .record_path = location.record_path,
    .content_root = save.content_root,
    .source = scene.source_path,
    .virtual_path = scene.virtual_path,
    .settings_json = settings.dump(),
  });
  return location;
}

auto ModelImportRecord::Prepare(const std::filesystem::path& record_path,
  std::vector<ImportDiagnostic> notes) -> std::shared_ptr<ModelImportRecord>
{
  auto state = std::make_unique<State>();
  state->record_path = Absolute(record_path);
  state->notes = std::move(notes);
  const auto lock = RecordLock(state->record_path);
  state->content = Read(state->record_path);
  RequireUniqueRecord(state->record_path, state->content);
  if (!Exists(state->content.source)) {
    throw std::runtime_error("import.source_missing: the source "
      + state->content.source.generic_string() + " of the model "
      + state->content.virtual_path
      + " is missing; importing the new file with the virtual path "
      + state->content.virtual_path + " rebinds it");
  }
  state->baseline = Baseline(state->record_path, state->content);
  state->previous = SelectedPath(state->record_path, state->content);
  state->generation = Uuid::Generate();
  const auto root = GenerationDirectory(state->record_path, state->content)
    / state->generation.ToString();
  std::filesystem::create_directories(base::ToNativePath(root.parent_path()));
  if (!std::filesystem::create_directory(base::ToNativePath(root))) {
    throw std::runtime_error("Private import generation already exists");
  }
  auto generation_lock = serio::FileLock::TryAcquire(
    root / data::loose_cooked::kGenerationLeaseFileName,
    serio::FileLockMode::kShared, serio::FileLockOpenMode::kOpenOrCreate);
  if (!generation_lock) {
    throw std::system_error(
      generation_lock.error(), "Cannot reserve private generation");
  }
  state->generation_lock.emplace(std::move(generation_lock).value());

  // The same `model` job a manifest runs.
  const auto manifest = json {
    { "version", 1 },
    {
      "jobs",
      json::array({
        {
          { "id", "model" },
          { "type", "model" },
          { "record", state->record_path.generic_string() },
        },
      }),
    },
  };
  std::ostringstream errors;
  auto parsed = ImportManifest::Parse(
    manifest.dump(), state->record_path.parent_path(), std::nullopt, errors);
  if (!parsed || parsed->jobs.size() != 1U) {
    throw std::invalid_argument(errors.str());
  }
  auto& job = parsed->jobs.front();
  auto& settings = job.job_type == "fbx" ? job.fbx : job.gltf;
  settings.cooked_root = root.string();
  auto request = job.BuildRequest(errors);
  if (!request) {
    throw std::invalid_argument(errors.str());
  }
  state->request = std::move(*request);
  state->request.source_key = data::SourceKey { state->generation };
  return std::shared_ptr<ModelImportRecord>(
    new ModelImportRecord(std::move(state)));
}

auto ModelImportRecord::ReclaimUnusedGenerations() const -> size_t
{
  return ReclaimUnusedGenerations(state_->record_path);
}

auto ModelImportRecord::ReclaimUnusedGenerations(
  const std::filesystem::path& record_path) -> size_t
{
  const auto path = Absolute(record_path);
  const auto record_lock = RecordLock(path);
  const auto content = Read(path);
  RequireUniqueRecord(path, content);
  const auto selected = SelectedPath(path, content);
  const auto directory = GenerationDirectory(path, content);
  if (!std::filesystem::exists(base::ToNativePath(directory))) {
    return 0U;
  }
  size_t removed = 0U;
  for (const auto& entry :
    std::filesystem::directory_iterator(base::ToNativePath(directory))) {
    if (entry.is_symlink() || !entry.is_directory()) {
      continue;
    }
    const auto id = Uuid::FromString(entry.path().filename().string());
    if (!id || !id.value().IsValidV7()) {
      continue;
    }
    const auto candidate = directory / id.value().ToString();
    if (Absolute(entry.path()) != candidate
      || (selected && candidate == *selected)) {
      continue;
    }
    auto lease = serio::FileLock::TryAcquire(
      candidate / data::loose_cooked::kGenerationLeaseFileName,
      serio::FileLockMode::kExclusive);
    if (!lease) {
      if (lease.error() == std::errc::device_or_resource_busy
        || lease.error() == std::errc::no_such_file_or_directory) {
        continue;
      }
      throw std::system_error(
        lease.error(), "Cannot reclaim retired import generation");
    }
    std::filesystem::remove_all(base::ToNativePath(candidate));
    ++removed;
  }
  return removed;
}

auto ModelImportRecord::ClaimSubmission() -> void
{
  if (state_->claimed.exchange(true) || state_->published) {
    throw std::logic_error(
      "A prepared import generation can only be submitted once");
  }
}

auto ModelImportRecord::AllowsWriting(const std::filesystem::path& root) const
  -> bool
{
  const auto& cooked_root = state_->request.cooked_root;
  return state_->claimed.load() && !state_->published && state_->generation_lock
    && cooked_root && Absolute(root) == Absolute(*cooked_root);
}

auto ModelImportRecord::Request() const -> const ImportRequest&
{
  return state_->request;
}

auto ModelImportRecord::PreviousGeneration() const
  -> std::optional<std::filesystem::path>
{
  return state_->previous;
}

auto ModelImportRecord::Notes() const -> std::span<const ImportDiagnostic>
{
  return state_->notes;
}

auto ModelImportRecord::SelectedGeneration(
  const std::filesystem::path& record_path)
  -> std::optional<std::filesystem::path>
{
  const auto path = Absolute(record_path);
  const auto content = Read(path);
  const auto directory = GenerationDirectory(path, content);
  const auto head = ReadHead(directory);
  if (!head) {
    return std::nullopt;
  }
  auto selected = directory / head->id.ToString();
  if (!std::filesystem::exists(base::ToNativePath(selected))) {
    return selected;
  }
  const auto lease = serio::FileLock::TryAcquire(
    selected / data::loose_cooked::kGenerationLeaseFileName,
    serio::FileLockMode::kShared);
  if (!lease) {
    throw std::system_error(
      lease.error(), "Selected import generation is unavailable");
  }
  const auto actual = base::ComputeFileSha256(selected / "container.index.bin");
  if (base::ToHexString(actual) != head->index_sha256) {
    throw std::runtime_error(
      "Selected import generation index changed after publication");
  }
  return selected;
}

auto ModelImportRecord::ValidateCandidate(const ImportReport& report) -> void
{
  const auto& cooked_root = state_->request.cooked_root;
  const auto source_key = state_->request.source_key;
  if (!cooked_root || !source_key) {
    throw std::invalid_argument("Import generation identity is incomplete");
  }
  const auto expected_source_key = *source_key;
  if (state_->published || !report.success
    || report.surface_provenance_json.empty()
    || Absolute(report.cooked_root) != Absolute(*cooked_root)) {
    throw std::invalid_argument(
      "Only this attempt's successful native report can be published");
  }
  const auto provenance = SurfaceProvenance::Parse(
    report.surface_provenance_json, state_->content.source_identity);
  lc::ValidateRoot(report.cooked_root);
  lc::Inspection inspection;
  inspection.LoadFromRoot(report.cooked_root);
  if (inspection.Guid() != expected_source_key) {
    throw std::invalid_argument(
      "Native generation source key changed during import");
  }
  size_t geometry_count = 0U;
  for (const auto& asset : inspection.Assets()) {
    if (asset.asset_type != static_cast<uint8_t>(data::AssetType::kGeometry)) {
      continue;
    }
    ++geometry_count;
    const auto leaf = std::string_view(asset.virtual_path)
                        .substr(asset.virtual_path.rfind('/') + 1);
    const auto* retained = provenance->FindGeometry(
      leaf.substr(0, leaf.size() - data::kGeometryDescriptorExtension.size()));
    if (retained == nullptr) {
      throw std::invalid_argument(
        "Cooked geometry has no matching recorded provenance");
    }
    serio::FileStream<> stream(
      report.cooked_root / asset.descriptor_relpath, std::ios::in);
    serio::Reader reader(stream);
    const auto geometry = loaders::LoadGeometryAsset(LoaderContext {
      .current_asset_key = asset.key,
      .desc_reader = &reader,
      .work_offline = true,
      .parse_only = true,
    });
    if (geometry->Surfaces().layout_revision
      != retained->inventory.layout_revision) {
      throw std::invalid_argument(
        "Recorded inventory does not match its cooked geometry");
    }
  }
  if (geometry_count != provenance->Geometries().size()) {
    throw std::invalid_argument(
      "Recorded provenance contains geometry absent from its generation");
  }
  state_->validated_index
    = base::ComputeFileSha256(report.cooked_root / "container.index.bin");
  state_->validated_provenance = Digest(report.surface_provenance_json);
}

// The exported signature takes the token by value; a stop_token is a cheap
// shared handle.
auto ModelImportRecord::Publish(
  // NOLINTNEXTLINE(performance-unnecessary-value-param)
  ImportReport& report, const std::stop_token stop_token) -> void
{
  const auto& cooked_root = state_->request.cooked_root;
  const auto& validated_index = state_->validated_index;
  const auto& validated_provenance = state_->validated_provenance;
  if (!cooked_root || !validated_index || !validated_provenance) {
    throw std::invalid_argument(
      "Import candidate was not validated or changed before publication");
  }
  if (state_->published || !report.success
    || *validated_provenance != Digest(report.surface_provenance_json)
    || Absolute(report.cooked_root) != Absolute(*cooked_root)
    || *validated_index
      != base::ComputeFileSha256(report.cooked_root / "container.index.bin")) {
    throw std::invalid_argument(
      "Import candidate was not validated or changed before publication");
  }
  const auto index_digest = *validated_index;
  auto content = state_->content;
  content.surface_provenance = SurfaceProvenance::Parse(
    report.surface_provenance_json, content.source_identity);
  content.published_nodes = report.published_nodes;
  content.new_nodes = report.new_nodes;
  content.vanished_nodes = report.vanished_nodes;
  if (report.source_fingerprint) {
    content.source_fingerprint = report.source_fingerprint;
  }
  const auto record_text = Serialize(content, state_->record_path);
  const auto head_text = ordered_json {
    { "id", state_->generation.ToString() },
    { "index_sha256", base::ToHexString(index_digest) },
  }
                           .dump(2)
    + "\n";

  const auto lock = RecordLock(state_->record_path);
  if (Baseline(state_->record_path, state_->content) != state_->baseline) {
    throw std::runtime_error(
      "Model import settings or publication changed during import");
  }
  if (stop_token.stop_requested()) {
    throw std::runtime_error("Model import publication was cancelled before "
                             "commit");
  }
  report.model_record_path = state_->record_path;
  report.previous_cooked_root = state_->previous;
  // The record first: a crash before the head leaves the previous generation
  // selected, and the next import derives the same IDs from the record.
  WriteText(state_->record_path, record_text);
  WriteText(GenerationDirectory(state_->record_path, content) / kHeadFileName,
    head_text);
  state_->published = true;
  state_->generation_lock.reset();
}

} // namespace oxygen::content::import
