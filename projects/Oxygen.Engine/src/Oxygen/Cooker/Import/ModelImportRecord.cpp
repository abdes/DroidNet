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
#include <memory>
#include <optional>
#include <set>
#include <span>
#include <stdexcept>
#include <stop_token>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

#include <nlohmann/json.hpp>
#include <nlohmann/json_fwd.hpp>

#include <Oxygen/Base/Filesystem.h>
#include <Oxygen/Base/Logging.h>
#include <Oxygen/Base/ObserverPtr.h>
#include <Oxygen/Base/Sha256.h>
#include <Oxygen/Base/Uuid.h>
#include <Oxygen/Content/LoaderContext.h>
#include <Oxygen/Content/Loaders/GeometryLoader.h>
#include <Oxygen/Content/VirtualPath.h>
#include <Oxygen/Cooker/Cooked/CookedContent.h>
#include <Oxygen/Cooker/Cooked/CookerStorage.h>
#include <Oxygen/Cooker/Cooked/Validate.h>
#include <Oxygen/Cooker/Import/ImportCodes.h>
#include <Oxygen/Cooker/Import/ImportDiagnostics.h>
#include <Oxygen/Cooker/Import/ImportLimits.h>
#include <Oxygen/Cooker/Import/ImportManifest.h>
#include <Oxygen/Cooker/Import/ImportReport.h>
#include <Oxygen/Cooker/Import/ImportRequest.h>
#include <Oxygen/Cooker/Import/Internal/GeometryDefaults.h>
#include <Oxygen/Cooker/Import/Internal/ImportManifest_schema.h>
#include <Oxygen/Cooker/Import/Internal/Utils/DescriptorDocument.h>
#include <Oxygen/Cooker/Import/Internal/Utils/JsonSchemaValidation.h>
#include <Oxygen/Cooker/Import/ModelImportRecord.h>
#include <Oxygen/Cooker/Import/ModelImportRecordError.h>
#include <Oxygen/Cooker/Import/ModelImportState.h>
#include <Oxygen/Cooker/Import/SceneImportSettings.h>
#include <Oxygen/Cooker/Import/SourceMounts.h>
#include <Oxygen/Cooker/Import/SurfaceProvenance.h>
#include <Oxygen/Cooker/Import/TextureImportSettings.h>
#include <Oxygen/Core/Types/DescriptorExtensions.h>
#include <Oxygen/Data/AssetType.h>
#include <Oxygen/Data/CookedSource.h>
#include <Oxygen/Data/LooseCookedIndexFormat.h>
#include <Oxygen/Data/SceneIds.h>
#include <Oxygen/Data/StableId.h>
#include <Oxygen/Serio/AtomicFile.h>
#include <Oxygen/Serio/FileLock.h>
#include <Oxygen/Serio/Integrity.h>
#include <Oxygen/Serio/MemoryStream.h>
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
    "depends_on",
    "output",
    "cooked_context_roots",
    "source_key",
    "surface_provenance",
  };

  constexpr auto kHeadFileName = "head.json";

  namespace codes = model_record_codes;

  //! Refuses with `code`, about `file` and its JSON field at the RFC 6901
  //! pointer `field`.
  [[noreturn]] auto Refuse(const std::string_view code,
    const std::filesystem::path& file, std::string field, std::string message)
    -> void
  {
    throw ModelImportRecordError(ImportDiagnostic {
      .severity = ImportSeverity::kError,
      .code = std::string(code),
      .message = std::move(message),
      .source_path = file.generic_string(),
      .json_pointer = std::move(field),
    });
  }

  //! Refuses with the first error of a lookup that found nothing, or with
  //! `fallback` as an invalid record when it reported none.
  [[noreturn]] auto Refuse(const std::vector<ImportDiagnostic>& findings,
    const std::filesystem::path& file, std::string field, std::string fallback)
    -> void
  {
    const auto error = std::ranges::find(
      findings, ImportSeverity::kError, &ImportDiagnostic::severity);
    if (error != findings.end()) {
      throw ModelImportRecordError(*error);
    }
    Refuse(codes::kInvalid, file, std::move(field), std::move(fallback));
  }

  //! The text of a record file, which its writers publish atomically: it is
  //! opened sharing deletion, so a publication while it is read replaces it.
  auto ReadText(const std::filesystem::path& path) -> std::string
  {
    const auto bytes = serio::ReadPublishedFile(path, kMaxImportMetadataBytes);
    if (!bytes) {
      Refuse(codes::kUnreadable, path, {},
        "Cannot read model import file " + path.generic_string() + ": "
          + bytes.error().message());
    }
    const auto chars = bytes->Bytes();
    auto text = std::string(chars.size(), ' ');
    std::ranges::transform(chars, text.begin(),
      [](const std::byte byte) -> char { return static_cast<char>(byte); });
    return text;
  }

  auto Digest(std::string_view text) -> base::Sha256Digest
  {
    return base::ComputeSha256(
      std::as_bytes(std::span(text.data(), text.size())));
  }

  //! The digest of a file an operation needs; `code` refuses one that cannot
  //! be read.
  auto FileDigest(const std::filesystem::path& path,
    const std::string_view code = codes::kUnreadable) -> base::Sha256Digest
  {
    const auto digest = serio::ComputeFileSha256(path);
    if (!digest) {
      Refuse(code, path, {},
        "Cannot hash " + path.generic_string() + ": "
          + digest.error().message());
    }
    return *digest;
  }

  //! The digest `text` spells, in the field `field` of `file`; `code`
  //! refuses another text.
  auto ReadDigest(const std::string& text, const std::filesystem::path& file,
    std::string field, const std::string_view code = codes::kInvalid)
    -> base::Sha256Digest
  {
    const auto digest = base::Sha256DigestFromHex(text);
    if (!digest.has_value()) {
      Refuse(code, file, std::move(field), "Not a SHA-256 digest: " + text);
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

  auto Exists(const std::filesystem::path& path) -> bool
  {
    std::error_code error;
    return std::filesystem::is_regular_file(base::ToNativePath(path), error);
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

  //! The identity of the model whose record is `record_path`; throws what
  //! refuses it.
  auto RequireIdentity(const MountTable& mounts,
    const std::filesystem::path& record_path) -> std::string
  {
    auto findings = std::vector<ImportDiagnostic> {};
    auto identity
      = ModelImportRecord::IdentityOf(mounts, record_path, findings);
    if (!identity.has_value()) {
      Refuse(findings, record_path, {},
        "Not a model import record: " + record_path.generic_string());
    }
    return std::move(*identity);
  }

  //! The record of the model `identity`; throws what refuses it.
  auto RequireRecordPath(const MountTable& mounts,
    const std::string_view identity) -> std::filesystem::path
  {
    auto findings = std::vector<ImportDiagnostic> {};
    const auto path
      = ModelImportRecord::RecordPathOf(mounts, identity, findings);
    if (!path.has_value()) {
      Refuse(findings, std::filesystem::path { mounts.declared_in }, {},
        "The model " + std::string(identity) + " has no record location");
    }
    return Absolute(*path);
  }

  //! Locks a record. The lock file beside it exists only while the lock is
  //! held; another holder is `import.model_record_busy`.
  auto RecordLock(const std::filesystem::path& path) -> serio::FileLock
  {
    auto lock_path = path;
    lock_path += ".lock";
    auto lock = serio::FileLock::TryAcquire(lock_path,
      serio::FileLockMode::kExclusive, serio::FileLockOpenMode::kTemporary);
    if (!lock) {
      if (lock.error() == std::errc::device_or_resource_busy) {
        Refuse(codes::kBusy, path, {},
          "Another import or save holds the model import record "
            + path.generic_string());
      }
      throw std::system_error(lock.error(),
        "Cannot lock the model import record " + path.generic_string());
    }
    return std::move(lock).value();
  }

  auto FormatOf(const std::filesystem::path& source) -> ImportFormat
  {
    ImportRequest request;
    request.source_path = source;
    return request.GetFormat();
  }

  //! The manifest job type of the record `record`'s source.
  auto JobType(const std::filesystem::path& record,
    const std::filesystem::path& source) -> std::string_view
  {
    const auto format = FormatOf(source);
    if (format != ImportFormat::kGltf && format != ImportFormat::kFbx) {
      Refuse(codes::kInvalid, record, "/source",
        "A model import source must be glTF, GLB or FBX: "
          + source.generic_string());
    }
    return format == ImportFormat::kFbx ? "fbx" : "gltf";
  }

  //! Refuses settings that are no object or that hold a key the record and
  //! its manifest own.
  auto RequireSettings(
    const std::filesystem::path& record, const json& settings) -> void
  {
    if (!settings.is_object()) {
      Refuse(codes::kInvalid, record, "/settings",
        "Model import settings must be an object");
    }
    for (const auto* key : kStructuralKeys) {
      if (settings.contains(key)) {
        Refuse(codes::kInvalid, record, std::string("/settings/") + key,
          std::string("Model import settings hold no '") + key
            + "'; the record and its manifest own it");
      }
    }
  }

  //! The settings text `text`, parsed.
  auto ParseSettings(
    const std::filesystem::path& record, const std::string& text) -> json
  {
    try {
      auto settings = json::parse(text);
      RequireSettings(record, settings);
      return settings;
    } catch (const json::parse_error& error) {
      Refuse(codes::kInvalid, record, "/settings",
        std::string("Model import settings are no JSON: ") + error.what());
    }
  }

  //! The record `record`'s gltf or fbx job.
  auto MakeJobJson(const std::filesystem::path& record,
    const ModelImportRecordContent& content, const std::string_view identity)
    -> json
  {
    auto job = ParseSettings(record, content.settings_json);
    job.update({
      { "id", "model" },
      { "type", JobType(record, content.source) },
      { "source", content.source_mount_path },
      { "virtual_path", identity },
    });
    return job;
  }

  //! Validates the settings as the import manifest does, by parsing the
  //! record's job.
  auto ValidateSettings(const std::filesystem::path& record,
    const ModelImportRecordContent& content, const std::string_view identity,
    const MountTable& mounts) -> void
  {
    const auto manifest = json {
      { "schema_version", kImportManifestSchemaVersion },
      { "jobs", json::array({ MakeJobJson(record, content, identity) }) },
    };
    auto errors = std::vector<ImportDiagnostic> {};
    const auto parsed
      = ImportManifest::Parse(manifest.dump(), content.source.parent_path(),
        {
          .project_root = {},
          .mounts_file = {},
          .owner = MountTableOwner::kProject,
          .mount_table = make_observer(&mounts),
        },
        errors);
    if (!parsed || parsed->jobs.size() != 1U) {
      Refuse(codes::kInvalid, record, "/settings",
        "Invalid model import settings: "
          + internal::FormatDiagnostics(errors));
    }
  }

  auto ReadFingerprint(const std::filesystem::path& record, const json& value)
    -> ModelSourceFingerprint
  {
    auto fingerprint = ModelSourceFingerprint {
      .size = value.at("size").get<uint64_t>(),
      .sha256 = ReadDigest(value.at("sha256").get<std::string>(), record,
        "source_fingerprint.sha256"),
      .files = {},
    };
    for (const auto& file : value.at("files")) {
      fingerprint.files.push_back({
        .path = file.at("path").get<std::string>(),
        .size = file.at("size").get<uint64_t>(),
        .sha256 = ReadDigest(file.at("sha256").get<std::string>(), record,
          "source_fingerprint.files"),
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

  auto ReadNodes(const std::filesystem::path& record, const json& document,
    const char* key) -> std::vector<ModelPublishedNode>
  {
    auto nodes = std::vector<ModelPublishedNode> {};
    for (const auto& node : document.value(key, json::array())) {
      const auto id
        = data::NodeId::FromString(node.at("node_id").get<std::string>());
      if (!id) {
        Refuse(codes::kInvalid, record, std::string("/") + key,
          std::string("Invalid node ID in '") + key + "'");
      }
      nodes.push_back({
        .anchor = node.at("anchor").get<std::string>(),
        .node_id = id.value(),
      });
    }
    return nodes;
  }

  //! Every record of the bound `Source` mounts, by path: the files named
  //! `*.import.json` in their folders.
  auto RecordFiles(const MountTable& mounts)
    -> std::vector<std::filesystem::path>
  {
    auto paths = std::vector<std::filesystem::path> {};
    for (const auto& mount : mounts.mounts) {
      if (mount.kind != MountKind::kSource
        || mount.state != MountState::kBound) {
        continue;
      }
      for (const auto& entry : std::filesystem::recursive_directory_iterator(
             base::ToNativePath(mount.folder))) {
        if (entry.is_regular_file()
          && entry.path().filename().string().ends_with(
            ModelImportRecord::kRecordSuffix)) {
          paths.push_back(Absolute(base::ToLogicalPath(entry.path())));
        }
      }
    }
    std::ranges::sort(paths);
    return paths;
  }

  //! A record that can be read, and the model it is the record of.
  struct ScannedRecord final {
    std::filesystem::path path;
    std::string identity;
    ModelImportRecordContent content;
  };

  //! The records of a mount table that can be read, and a warning for each
  //! file that cannot.
  struct RecordScan final {
    std::vector<ScannedRecord> records;
    std::vector<ImportDiagnostic> skipped;
  };

  //! Reads every record of the bound `Source` mounts. A file that is not a
  //! valid record, damaged or of an older format, is no model's record: it is
  //! skipped, so it never stops importing the other models.
  auto ScanRecords(const MountTable& mounts) -> RecordScan
  {
    auto scan = RecordScan {};
    for (const auto& path : RecordFiles(mounts)) {
      try {
        auto identity = RequireIdentity(mounts, path);
        auto content = ModelImportRecord::Read(path, mounts);
        scan.records.push_back({
          .path = path,
          .identity = std::move(identity),
          .content = std::move(content),
        });
      } catch (const std::exception& error) {
        scan.skipped.push_back({
          .severity = ImportSeverity::kWarning,
          .code = std::string(model_record_codes::kUnreadable),
          .message = "Skipped " + path.generic_string()
            + ", which is not a valid model import record: " + error.what(),
          .source_path = path.generic_string(),
          .object_path = {},
        });
      }
    }
    return scan;
  }

  auto ReadUnlocatedContent(const std::filesystem::path& record_path)
    -> ModelImportRecordContent;

  //! Refuses a second record of `mounts` with the source identity of
  //! `content`. A record's identity is its path, so no two share one.
  auto RequireUniqueRecord(const std::filesystem::path& record_path,
    const ModelImportRecordContent& content, const MountTable& mounts) -> void
  {
    const auto path = Absolute(record_path);
    for (const auto& other_path : RecordFiles(mounts)) {
      if (other_path == path) {
        continue;
      }
      auto other = ModelImportRecordContent {};
      try {
        other = ReadUnlocatedContent(other_path);
      } catch (const std::exception&) {
        continue;
      }
      if (other.source_identity == content.source_identity) {
        Refuse(codes::kDuplicate, path, "/source_identity",
          "Two model import records have the source identity "
            + content.source_identity.ToString() + ": " + path.generic_string()
            + " and " + other_path.generic_string());
      }
    }
  }

  //! The folder of a record's generations: its source identity, in the
  //! generations folder the caller owns.
  auto GenerationDirectory(const ModelImportGenerations& generations,
    const ModelImportRecordContent& content) -> std::filesystem::path
  {
    if (generations.root.empty()) {
      throw std::invalid_argument(
        "Model import generations need a generations folder");
    }
    return Absolute(generations.root) / content.source_identity.ToString();
  }

  struct Head final {
    Uuid id;
    std::string index_sha256;
  };

  //! The head of a record's generations; a malformed one is
  //! `import.generation_invalid`.
  auto ReadHead(const std::filesystem::path& directory) -> std::optional<Head>
  {
    const auto path = directory / kHeadFileName;
    if (!Exists(path)) {
      return std::nullopt;
    }
    try {
      const auto document = json::parse(ReadText(path));
      const auto id = Uuid::FromString(document.at("id").get<std::string>());
      if (!id || !id.value().IsValidV7()) {
        Refuse(codes::kGenerationInvalid, path, "/id",
          "The selected generation is not named by a UUIDv7");
      }
      auto index = document.at("index_sha256").get<std::string>();
      static_cast<void>(
        ReadDigest(index, path, "index_sha256", codes::kGenerationInvalid));
      return Head { .id = id.value(), .index_sha256 = std::move(index) };
    } catch (const json::exception& error) {
      Refuse(codes::kGenerationInvalid, path, {},
        std::string("The generations head is malformed: ") + error.what());
    }
  }

  //! What an attempt must find unchanged when it publishes: the record and
  //! the selected generation, either of which a competing attempt changes.
  auto Baseline(const std::filesystem::path& record_path,
    const ModelImportGenerations& generations,
    const ModelImportRecordContent& content) -> base::Sha256Digest
  {
    const auto head = GenerationDirectory(generations, content) / kHeadFileName;
    return Digest(ReadText(record_path) + "\n"
      + (Exists(head) ? ReadText(head) : std::string {}));
  }

  auto SelectedPath(const ModelImportGenerations& generations,
    const ModelImportRecordContent& content)
    -> std::optional<std::filesystem::path>
  {
    const auto directory = GenerationDirectory(generations, content);
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

} // namespace

struct ModelImportRecord::State {
  std::filesystem::path record_path {};
  std::string identity;
  ModelImportGenerations generations {};
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

namespace {

  //! A record read without locating its source: what reading the source's
  //! identity and generations needs.
  struct RecordDocument final {
    ModelImportRecordContent content;
    std::string source_text;
  };

  //! The record's JSON, valid against its schema.
  auto ParseDocument(const std::filesystem::path& path) -> json
  {
    auto document = json {};
    try {
      document = json::parse(ReadText(path));
    } catch (const json::parse_error& error) {
      Refuse(codes::kInvalid, path, {},
        std::string("A model import record is JSON: ") + error.what());
    }
    auto validator
      = internal::MakeJsonSchemaValidator(json::parse(kModelImportSchema));
    try {
      internal::ValidateJsonSchema(
        validator, document, "Model import record " + path.generic_string());
    } catch (const std::invalid_argument& error) {
      Refuse(codes::kInvalid, path, {}, error.what());
    }
    return document;
  }

  auto ReadDocument(const std::filesystem::path& record_path) -> RecordDocument
  {
    const auto path = Absolute(record_path);
    const auto document = ParseDocument(path);

    auto content = ModelImportRecordContent {};
    content.source_identity
      = Uuid::FromString(document.at("source_identity").get<std::string>())
          .value();
    auto source_text = document.at("source").get<std::string>();
    content.settings_json = document.at("settings").dump();
    try {
      content.surface_provenance = document.contains("surface_provenance")
        ? SurfaceProvenance::Parse(
            document.at("surface_provenance").dump(), content.source_identity)
        : std::make_shared<const SurfaceProvenance>(content.source_identity);
    } catch (const std::invalid_argument& error) {
      Refuse(codes::kInvalid, path, "/surface_provenance", error.what());
    }

    auto anchors = std::set<std::string> {};
    for (const auto& entry : document.value("node_continuity", json::array())) {
      auto& added = content.node_continuity.emplace_back(ModelNodeContinuity {
        .anchor = entry.at("anchor").get<std::string>(),
        .continues = entry.at("continues").get<std::string>(),
      });
      if (!anchors.insert(added.anchor).second) {
        Refuse(codes::kInvalid, path, "/node_continuity",
          "Two node continuity entries have the anchor '" + added.anchor + "'");
      }
    }
    content.published_nodes = ReadNodes(path, document, "published_nodes");
    content.new_nodes = ReadNodes(path, document, "new_nodes");
    content.vanished_nodes = ReadNodes(path, document, "vanished_nodes");
    if (document.contains("source_fingerprint")) {
      content.source_fingerprint
        = ReadFingerprint(path, document.at("source_fingerprint"));
    }
    return {
      .content = std::move(content),
      .source_text = std::move(source_text),
    };
  }

  //! Locates a record's `source`, a file reference from the record.
  auto LocateRecordSource(const std::filesystem::path& record_path,
    const std::string& text, const MountTable& mounts) -> MountedSource
  {
    auto findings = std::vector<ImportDiagnostic> {};
    const auto located = mounts.LocateReference(record_path,
      CommittedPath { .text = text, .json_pointer = "/source" }, findings);
    if (!located.has_value()) {
      Refuse(findings, record_path, "source",
        "Cannot locate the source '" + text + "'");
    }
    return *located;
  }

  //! The mount path of a source a command names, as a record stores it.
  auto MountPathOfSource(const std::filesystem::path& source,
    const MountTable& mounts) -> MountedSource
  {
    auto findings = std::vector<ImportDiagnostic> {};
    const auto located = mounts.MountPathOf(Absolute(source), findings);
    if (!located.has_value()) {
      Refuse(findings, source, {},
        "Cannot locate the source " + source.generic_string());
    }
    return *located;
  }

  auto ReadUnlocatedContent(const std::filesystem::path& record_path)
    -> ModelImportRecordContent
  {
    return ReadDocument(record_path).content;
  }

  //! Removes the unselected, unleased generations of the record at `path`,
  //! under its lock.
  auto ReclaimGenerations(const std::filesystem::path& path,
    const ModelImportGenerations& generations) -> size_t
  {
    const auto record_lock = RecordLock(path);
    const auto content = ReadDocument(path).content;
    const auto selected = SelectedPath(generations, content);
    const auto directory = GenerationDirectory(generations, content);
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

} // namespace

auto ModelImportRecord::RecordPathOf(const MountTable& mounts,
  const std::string_view identity, std::vector<ImportDiagnostic>& findings)
  -> std::optional<std::filesystem::path>
{
  if (const auto error = content::ValidateVirtualPath(
        identity, content::VirtualPathRuleSet::kAuthored)) {
    findings.push_back(Diagnostic(ImportSeverity::kError,
      std::string(content::to_string(*error)),
      "The engine refuses the model identity " + std::string(identity),
      std::filesystem::path { mounts.declared_in }));
    return std::nullopt;
  }
  const auto record = std::string(identity) + std::string(kRecordSuffix);
  return mounts
    .LocateSource(
      CommittedPath { .text = record, .json_pointer = "/virtual_path" },
      findings)
    .transform([](const MountedSource& located) -> std::filesystem::path {
      return located.file;
    });
}

auto ModelImportRecord::IdentityOf(const MountTable& mounts,
  const std::filesystem::path& record_path,
  std::vector<ImportDiagnostic>& findings) -> std::optional<std::string>
{
  const auto name = record_path.filename().string();
  if (!name.ends_with(kRecordSuffix) || name.size() == kRecordSuffix.size()) {
    findings.push_back(Diagnostic(ImportSeverity::kError,
      std::string(model_record_codes::kMisnamed),
      "A model import record is named after its model, with the suffix "
        + std::string(kRecordSuffix) + ": " + record_path.generic_string(),
      record_path));
    return std::nullopt;
  }
  const auto located = mounts.MountPathOf(record_path, findings);
  if (!located.has_value()) {
    return std::nullopt;
  }
  auto identity = located->mount_path.substr(
    0, located->mount_path.size() - kRecordSuffix.size());
  if (const auto error = content::ValidateVirtualPath(
        identity, content::VirtualPathRuleSet::kAuthored)) {
    findings.push_back(Diagnostic(ImportSeverity::kError,
      std::string(content::to_string(*error)),
      "The record " + record_path.generic_string() + " is the record of "
        + identity + ", an identity the engine refuses",
      record_path));
    return std::nullopt;
  }
  return identity;
}

auto ModelImportRecord::Read(const std::filesystem::path& record_path,
  const MountTable& mounts) -> ModelImportRecordContent
{
  const auto identity = RequireIdentity(mounts, record_path);
  auto [content, source_text] = ReadDocument(record_path);
  const auto located = LocateRecordSource(record_path, source_text, mounts);
  content.source = located.file;
  content.source_mount_path = located.mount_path;
  ValidateSettings(record_path, content, identity, mounts);
  return content;
}

auto ModelImportRecord::Serialize(const ModelImportRecordContent& content,
  const std::string_view identity) -> std::string
{
  if (content.source_mount_path.empty()) {
    throw std::invalid_argument(
      "A model import record names its source by its mount path");
  }
  const auto source_text
    = content::RelativeReference(identity, content.source_mount_path);
  if (const auto resolved = content::ResolveReference(
        identity, source_text, content::VirtualPathRuleSet::kCooked);
    !resolved.has_value()) {
    throw std::invalid_argument("The record of " + std::string(identity)
      + " cannot name its source " + content.source_mount_path + " ("
      + std::string(content::to_string(resolved.error())) + ")");
  }
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

auto ModelImportRecord::MakeJob(const ModelImportRecordContent& content,
  const std::string_view identity) -> std::string
{
  return MakeJobJson({}, content, identity).dump();
}

auto ModelImportRecord::MakeSettings(const SceneImportSettings& settings)
  -> std::string
{
  auto job = TextureSettings(settings.texture_defaults);
  job.update({
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
    { "omitted_light_range_m", settings.omitted_light_range_m },
    { "import_lights", settings.import_lights },
    { "import_cameras", settings.import_cameras },
    { "light_intensity_scale", settings.light_intensity_scale },
    { "normals_smoothing_angle_deg", settings.normals_smoothing_angle_deg },
    { "generate_lods", settings.generate_lods },
    { "lod_levels", settings.lod_levels },
    { "lod_reduction_percent", settings.lod_reduction_percent },
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

auto ModelImportRecord::LocateRecord(const ModelImportRecordQuery& query)
  -> ModelImportRecordLocation
{
  const auto& [source, identity, mounts] = query;
  const auto file = Absolute(source);
  if (!Exists(file)) {
    Refuse(codes::kSourceMissing, file, {},
      "The model source " + file.generic_string() + " does not exist");
  }
  const auto mount_path = MountPathOfSource(file, *mounts).mount_path;
  const auto target = RequireRecordPath(*mounts, identity);
  const auto same_source
    = [&mount_path](const ModelImportRecordContent& record) -> bool {
    return content::FoldCase(record.source_mount_path)
      == content::FoldCase(mount_path);
  };
  auto scan = ScanRecords(*mounts);
  const auto& records = scan.records;
  auto location = ModelImportRecordLocation {
    .record_path = target,
    .exists = false,
    .diagnostics = std::move(scan.skipped),
  };

  // 1. The record at the identity.
  for (const auto& record : records) {
    if (record.path != target) {
      continue;
    }
    location.exists = true;
    if (!same_source(record.content)) {
      location.diagnostics.push_back(Diagnostic(ImportSeverity::kInfo,
        std::string(import_codes::kSourceRebound),
        "The model " + record.identity + " now imports from "
          + file.generic_string() + " instead of "
          + record.content.source.generic_string(),
        file));
    }
    return location;
  }

  // 2. Another model from the same file.
  for (const auto& record : records) {
    if (same_source(record.content)) {
      location.diagnostics.push_back(Diagnostic(ImportSeverity::kInfo,
        std::string(import_codes::kSourceHasModel),
        "The file is already the source of the model " + record.identity + " ("
          + record.path.generic_string()
          + "); this import makes another model. To reimport that model, "
            "import with --record "
          + record.path.generic_string(),
        file));
      return location;
    }
  }

  // 3. The source of a model moved: only on proof. The model keeps its
  // identity, which is where its record lives.
  auto fingerprint = std::optional<std::pair<uint64_t, base::Sha256Digest>> {};
  const auto same_bytes = [&](const ModelImportRecordContent& record) -> bool {
    if (!record.source_fingerprint) {
      return false;
    }
    if (!fingerprint) {
      fingerprint.emplace(
        std::filesystem::file_size(base::ToNativePath(file)), FileDigest(file));
    }
    return record.source_fingerprint->size == fingerprint->first
      && record.source_fingerprint->sha256 == fingerprint->second;
  };
  auto moved = std::vector<const ScannedRecord*> {};
  for (const auto& record : records) {
    if (!Exists(record.content.source) && same_bytes(record.content)) {
      moved.push_back(&record);
    }
  }
  if (moved.size() == 1U) {
    const auto& record = *moved.front();
    location.record_path = record.path;
    location.exists = true;
    location.diagnostics.push_back(Diagnostic(ImportSeverity::kInfo,
      std::string(import_codes::kSourceRelocated),
      "The model " + record.identity + " lost its source "
        + record.content.source.generic_string()
        + ", and this file has its bytes: the import rebinds it to this "
          "file and it keeps its identity; moving its record gives it "
          "another",
      file));
    return location;
  }
  if (moved.size() > 1U) {
    auto names = std::string {};
    for (const auto* record : moved) {
      names += (names.empty() ? "" : ", ") + record->path.generic_string();
    }
    location.diagnostics.push_back(Diagnostic(ImportSeverity::kWarning,
      std::string(import_codes::kSourceRelocationAmbiguous),
      "Several records whose source is missing have this file's bytes; none "
      "is taken: "
        + names + ". Import with --record to choose one",
      file));
  }

  // 4. A new model, with what may explain it.
  for (const auto& record : records) {
    if (Exists(record.content.source) && same_bytes(record.content)) {
      location.diagnostics.push_back(Diagnostic(ImportSeverity::kInfo,
        std::string(import_codes::kSourceCopy),
        "The file is a copy of " + record.content.source.generic_string()
          + ", the source of the model " + record.identity
          + "; importing it as that model makes it that model's source",
        file));
    } else if (!Exists(record.content.source)
      && record.content.source.filename() == file.filename()) {
      location.diagnostics.push_back(Diagnostic(ImportSeverity::kWarning,
        std::string(import_codes::kSourceRelocationCandidate),
        "The model " + record.identity + " lost its source "
          + record.content.source.generic_string()
          + " and may be this file, moved and changed. To rebind it: "
            "--record "
          + record.path.generic_string() + " " + file.generic_string(),
        file));
    }
  }
  return location;
}

auto ModelImportRecord::Save(const ModelImportRecordSave& save) -> void
{
  const auto path = Absolute(save.record_path);
  const auto identity = RequireIdentity(*save.mounts, path);
  const auto exists = Exists(path);
  std::filesystem::create_directories(base::ToNativePath(path.parent_path()));
  const auto lock = RecordLock(path);
  auto content = ModelImportRecordContent {};
  if (exists) {
    content = Read(path, *save.mounts);
  } else {
    const auto source_identity = Uuid::Generate();
    content.source_identity = source_identity;
    content.surface_provenance
      = std::make_shared<const SurfaceProvenance>(source_identity);
  }
  if (!save.source.empty()) {
    const auto located = MountPathOfSource(save.source, *save.mounts);
    content.source = located.file;
    content.source_mount_path = located.mount_path;
  }
  if (save.settings_json) {
    content.settings_json = ParseSettings(path, *save.settings_json).dump();
  }
  if (content.source.empty()) {
    Refuse(codes::kInvalid, path, "/source",
      "A new model import record needs a source");
  }
  ValidateSettings(path, content, identity, *save.mounts);
  RequireUniqueRecord(path, content, *save.mounts);
  const auto text = Serialize(content, identity);
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
    if (key != "$schema" && key != "schema_version" && key != "jobs") {
      throw std::invalid_argument("A recipe holds one gltf or fbx job and no '"
        + key + "'; worker budgets are execution options");
    }
  }
  auto errors = std::vector<ImportDiagnostic> {};
  const auto manifest = ImportManifest::Parse(recipe.dump(), path.parent_path(),
    {
      .project_root = {},
      .mounts_file = {},
      .owner = MountTableOwner::kProject,
      .mount_table = save.mounts,
    },
    errors);
  if (!manifest || manifest->jobs.size() != 1U
    || !manifest->jobs.front().depends_on.empty()) {
    throw std::invalid_argument(
      "A recipe is one self-contained gltf or fbx job: "
      + internal::FormatDiagnostics(errors));
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
  auto location = ModelImportRecordLocation {};
  if (save.record_path.empty()) {
    location = LocateRecord({
      .source = scene.source_path,
      .identity = scene.virtual_path,
      .mounts = save.mounts,
    });
  } else {
    location.record_path = Absolute(save.record_path);
    location.exists = Exists(location.record_path);
    const auto identity = RequireIdentity(*save.mounts, location.record_path);
    if (identity != scene.virtual_path) {
      throw std::invalid_argument("The recipe names the model "
        + scene.virtual_path + ", but " + location.record_path.generic_string()
        + " is the record of " + identity
        + ": a record's identity is where it lives");
    }
  }
  Save({
    .record_path = location.record_path,
    .source = scene.source_path,
    .settings_json = settings.dump(),
    .mounts = save.mounts,
  });
  return location;
}

auto ModelImportRecord::Prepare(const std::filesystem::path& record_path,
  const ModelImportGenerations& generations, const MountTable& mounts,
  std::vector<ImportDiagnostic> notes) -> std::shared_ptr<ModelImportRecord>
{
  if (generations.root.empty()) {
    throw std::invalid_argument(
      "Model import generations need a generations folder");
  }
  auto state = std::make_unique<State>();
  state->record_path = Absolute(record_path);
  state->identity = RequireIdentity(mounts, state->record_path);
  state->generations.root = Absolute(generations.root);
  state->notes = std::move(notes);
  const auto lock = RecordLock(state->record_path);
  state->content = Read(state->record_path, mounts);
  RequireUniqueRecord(state->record_path, state->content, mounts);
  if (!Exists(state->content.source)) {
    Refuse(codes::kSourceMissing, state->record_path, "/source",
      "The source " + state->content.source.generic_string() + " of the model "
        + state->identity + " is missing; importing the new file as "
        + state->identity + " rebinds it");
  }
  state->baseline
    = Baseline(state->record_path, state->generations, state->content);
  state->previous = SelectedPath(state->generations, state->content);
  state->generation = Uuid::Generate();
  const auto root = GenerationDirectory(state->generations, state->content)
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

  // The same `model` job a manifest runs, naming the model by its identity.
  const auto manifest = json {
    { "schema_version", kImportManifestSchemaVersion },
    {
      "jobs",
      json::array({
        {
          { "id", "model" },
          { "type", "model" },
          { "virtual_path", state->identity },
        },
      }),
    },
  };
  auto diagnostics = std::vector<ImportDiagnostic> {};
  auto parsed
    = ImportManifest::Parse(manifest.dump(), state->record_path.parent_path(),
      {
        .project_root = {},
        .mounts_file = {},
        .owner = MountTableOwner::kProject,
        .mount_table = make_observer(&mounts),
      },
      diagnostics);
  if (!parsed || parsed->jobs.size() != 1U) {
    Refuse(codes::kInvalid, state->record_path, "/settings",
      internal::FormatDiagnostics(diagnostics));
  }
  auto& job = parsed->jobs.front();
  auto& settings = job.job_type == "fbx" ? job.fbx : job.gltf;
  settings.cooked_root = root.string();
  auto request = job.BuildRequest(diagnostics);
  if (!request) {
    Refuse(codes::kInvalid, state->record_path, "/settings",
      internal::FormatDiagnostics(diagnostics));
  }
  state->request = std::move(*request);
  state->request.source_key = data::SourceKey { state->generation };
  return std::shared_ptr<ModelImportRecord>(
    new ModelImportRecord(std::move(state)));
}

// Prepare refused a record whose source identity another record has.
auto ModelImportRecord::ReclaimUnusedGenerations() const -> size_t
{
  return ReclaimGenerations(state_->record_path, state_->generations);
}

auto ModelImportRecord::ReclaimUnusedGenerations(
  const std::filesystem::path& record_path,
  const ModelImportGenerations& generations, const MountTable& mounts) -> size_t
{
  const auto path = Absolute(record_path);
  RequireUniqueRecord(path, ReadUnlocatedContent(path), mounts);
  return ReclaimGenerations(path, generations);
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
  const std::filesystem::path& record_path,
  const ModelImportGenerations& generations)
  -> std::optional<std::filesystem::path>
{
  const auto path = Absolute(record_path);
  const auto content = ReadDocument(path).content;
  const auto directory = GenerationDirectory(generations, content);
  const auto head = ReadHead(directory);
  if (!head) {
    return std::nullopt;
  }
  auto selected = directory / head->id.ToString();
  const auto head_path = directory / kHeadFileName;
  if (!std::filesystem::is_directory(base::ToNativePath(selected))) {
    Refuse(codes::kGenerationMissing, head_path, "/id",
      "The selected generation " + selected.generic_string()
        + " is gone; import the model again");
  }
  const auto lease = serio::FileLock::TryAcquire(
    selected / data::loose_cooked::kGenerationLeaseFileName,
    serio::FileLockMode::kShared);
  if (!lease) {
    Refuse(codes::kGenerationInvalid, head_path, "/id",
      "The selected generation " + selected.generic_string()
        + " cannot be leased: " + lease.error().message());
  }
  const auto actual
    = FileDigest(selected / oxygen::data::loose_cooked::kIndexFileName,
      codes::kGenerationInvalid);
  if (base::ToHexString(actual) != head->index_sha256) {
    Refuse(codes::kGenerationInvalid, head_path, "/index_sha256",
      "The index of the selected generation " + selected.generic_string()
        + " changed after publication");
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
  const auto content = cooked::CookedContent::Open(cooked::RealCookerStorage(),
    data::CookedSource {
      .kind = data::CookedSourceKind::kLooseCooked,
      .path = report.cooked_root,
    });
  cooked::Validate(content);
  if (content.Summary().source_key != expected_source_key) {
    throw std::invalid_argument(
      "Native generation source key changed during import");
  }
  size_t geometry_count = 0U;
  for (const auto& asset : content.Assets()) {
    if (asset.type != data::AssetType::kGeometry) {
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
    const auto descriptor = content.Descriptor(asset.key);
    serio::ReadOnlyMemoryStream stream(descriptor);
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
  state_->validated_index = FileDigest(
    report.cooked_root / oxygen::data::loose_cooked::kIndexFileName);
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
      != FileDigest(
        report.cooked_root / oxygen::data::loose_cooked::kIndexFileName)) {
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
  const auto record_text = Serialize(content, state_->identity);
  const auto head_text = ordered_json {
    { "id", state_->generation.ToString() },
    { "index_sha256", base::ToHexString(index_digest) },
  }
                           .dump(2)
    + "\n";

  const auto lock = RecordLock(state_->record_path);
  if (Baseline(state_->record_path, state_->generations, state_->content)
    != state_->baseline) {
    Refuse(codes::kChanged, state_->record_path, {},
      "The model import settings or publication changed during the import");
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
  WriteText(GenerationDirectory(state_->generations, content) / kHeadFileName,
    head_text);
  state_->published = true;
  state_->generation_lock.reset();
}

} // namespace oxygen::content::import
