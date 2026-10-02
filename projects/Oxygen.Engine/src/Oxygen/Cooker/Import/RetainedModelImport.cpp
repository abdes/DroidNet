//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <ios>
#include <iterator>
#include <memory>
#include <optional>
#include <span>
#include <sstream>
#include <stdexcept>
#include <stop_token>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>

#include <fmt/format.h>
#include <fmt/ranges.h>
#include <nlohmann/json-schema.hpp>
#include <nlohmann/json.hpp>
#include <nlohmann/json_fwd.hpp>

#include <Oxygen/Base/Filesystem.h>
#include <Oxygen/Base/Logging.h>
#include <Oxygen/Base/Sha256.h>
#include <Oxygen/Base/Uuid.h>
#include <Oxygen/Content/LoaderContext.h>
#include <Oxygen/Content/Loaders/GeometryLoader.h>
#include <Oxygen/Cooker/Import/ImportManifest.h>
#include <Oxygen/Cooker/Import/ImportReport.h>
#include <Oxygen/Cooker/Import/ImportRequest.h>
#include <Oxygen/Cooker/Import/Internal/ImportManifest_schema.h>
#include <Oxygen/Cooker/Import/MaterialSlotProvenance.h>
#include <Oxygen/Cooker/Import/RetainedModelImport.h>
#include <Oxygen/Cooker/Import/SceneImportSettings.h>
#include <Oxygen/Cooker/Import/TextureImportSettings.h>
#include <Oxygen/Cooker/Loose/Inspection.h>
#include <Oxygen/Cooker/Loose/LooseCookedLayout.h>
#include <Oxygen/Cooker/Loose/Validation.h>
#include <Oxygen/Data/AssetType.h>
#include <Oxygen/Data/LooseCookedIndexFormat.h>
#include <Oxygen/Serio/AtomicFile.h>
#include <Oxygen/Serio/FileLock.h>
#include <Oxygen/Serio/FileStream.h>
#include <Oxygen/Serio/Reader.h>

namespace oxygen::content::import {
namespace {

  using nlohmann::json;

  auto ReadText(const std::filesystem::path& path) -> std::string
  {
    std::ifstream input(base::ToNativePath(path), std::ios::binary);
    if (!input) {
      throw std::runtime_error(
        "Cannot read retained import record: " + path.string());
    }
    std::string text(std::istreambuf_iterator<char>(input), {});
    if (input.bad()) {
      throw std::runtime_error(
        "Retained import record read failed: " + path.string());
    }
    return text;
  }

  auto Digest(std::string_view text) -> base::Sha256Digest
  {
    return base::ComputeSha256(
      std::as_bytes(std::span(text.data(), text.size())));
  }

  auto WriteText(const std::filesystem::path& path, const std::string& text)
    -> void
  {
    const auto result = serio::WriteFileAtomically(
      path, std::as_bytes(std::span(text.data(), text.size())));
    if (!result) {
      throw std::system_error(
        result.error(), "Cannot publish retained import record");
    }
    if (result.value().durability_error) {
      LOG_F(WARNING,
        "Retained record committed; directory durability failed: {}",
        result.value().durability_error.message());
    }
  }

  auto Absolute(const std::filesystem::path& path) -> std::filesystem::path
  {
    return base::ToLogicalPath(std::filesystem::weakly_canonical(
      base::ToNativePath(base::ToLogicalPath(path))));
  }

  auto RequireWithin(const std::filesystem::path& path,
    const std::filesystem::path& root) -> void
  {
    const auto relative = Absolute(path).lexically_relative(Absolute(root));
    if (relative.empty() || relative.is_absolute() || *relative.begin() == ".."
      || relative == ".") {
      throw std::invalid_argument(
        "Retained import path must stay inside Content");
    }
  }

  auto RecordLock(const std::filesystem::path& path) -> serio::FileLock
  {
    auto lock_path = path;
    lock_path += ".lock";
    auto lock = serio::FileLock::TryAcquire(lock_path,
      serio::FileLockMode::kExclusive, serio::FileLockOpenMode::kOpenOrCreate);
    if (!lock) {
      throw std::system_error(lock.error(), "Retained import record is busy");
    }
    return std::move(lock).value();
  }

  auto ValidateRecipe(const json& recipe, const std::filesystem::path& base)
    -> ImportManifestJob
  {
    std::ostringstream errors;
    auto manifest
      = ImportManifest::Parse(recipe.dump(), base, std::nullopt, errors);
    if (!manifest || manifest->jobs.size() != 1U
      || (manifest->jobs.front().job_type != "gltf"
        && manifest->jobs.front().job_type != "fbx")
      || !manifest->jobs.front().depends_on.empty()) {
      throw std::invalid_argument(
        "Retained imports require one self-contained glTF/FBX job: "
        + errors.str());
    }
    const auto reject_identity = [](const json& object) -> void {
      if (object.contains("material_slot_source_identity")
        || object.contains("material_slot_provenance")
        || object.contains("output")) {
        throw std::invalid_argument(
          "Retained recipe identity belongs to its provenance record");
      }
    };
    reject_identity(recipe);
    for (const auto* field :
      { "thread_pool_size", "max_in_flight_jobs", "concurrency" }) {
      if (recipe.contains(field)) {
        throw std::invalid_argument(
          "Retained recipes contain content settings; use execution options "
          "for worker budgets");
      }
    }
    if (recipe.contains("defaults")) {
      reject_identity(recipe.at("defaults"));
      for (const auto& value : recipe.at("defaults")) {
        if (value.is_object()) {
          reject_identity(value);
        }
      }
    }
    for (const auto& job : recipe.at("jobs")) {
      reject_identity(job);
    }
    return std::move(manifest->jobs.front());
  }

  auto ReadRecord(const std::filesystem::path& path, const std::string& text)
    -> json
  {
    auto document = json::parse(text);
    nlohmann::json_schema::json_validator validator;
    validator.set_root_schema(json::parse(kRetainedModelImportSchema));
    validator.validate(document);
    const auto content_root = Absolute(
      path.parent_path() / document.at("content_root").get<std::string>());
    RequireWithin(path, content_root);
    static_cast<void>(MaterialSlotProvenance::Parse(
      document.at("material_slot_provenance").dump()));
    static_cast<void>(
      ValidateRecipe(document.at("recipe"), path.parent_path()));
    return document;
  }

  auto RequireUniqueRecord(
    const std::filesystem::path& path, const json& record) -> void
  {
    const auto content_root = Absolute(
      path.parent_path() / record.at("content_root").get<std::string>());
    const auto records = content_root / "imports";
    const auto& source_identity
      = record.at("material_slot_provenance").at("source_identity");
    for (const auto& entry : std::filesystem::recursive_directory_iterator(
           base::ToNativePath(records))) {
      if (!entry.is_regular_file() || Absolute(entry.path()) == path
        || !entry.path().filename().string().ends_with(".import.json")) {
        continue;
      }
      const auto other_path = Absolute(entry.path());
      const auto other = ReadRecord(other_path, ReadText(other_path));
      if (other.at("material_slot_provenance").at("source_identity")
        == source_identity) {
        throw std::invalid_argument(
          "A retained source identity must have one authored import record");
      }
    }
  }

  auto GenerationDirectory(const std::filesystem::path& record_path,
    const json& record) -> std::filesystem::path
  {
    const auto root = Absolute(
      record_path.parent_path() / record.at("content_root").get<std::string>());
    const auto provenance = MaterialSlotProvenance::Parse(
      record.at("material_slot_provenance").dump());
    const auto path
      = root / ".cooked" / "imports" / provenance->SourceIdentity().ToString();
    RequireWithin(path, root);
    return path;
  }

  auto GenerationPath(const std::filesystem::path& record_path,
    const json& record, const Uuid id) -> std::filesystem::path
  {
    return GenerationDirectory(record_path, record) / id.ToString();
  }

  auto SelectedPath(const std::filesystem::path& path, const json& record)
    -> std::optional<std::filesystem::path>
  {
    if (record.at("published_generation").is_null()) {
      return std::nullopt;
    }
    const auto id = Uuid::FromString(
      record.at("published_generation").at("id").get<std::string>());
    if (!id || !id.value().IsValidV7()) {
      throw std::invalid_argument("Invalid retained generation identity");
    }
    return GenerationPath(path, record, id.value());
  }

  auto TextureRecipe(const TextureImportSettings& settings) -> json
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

struct RetainedModelImport::State {
  std::filesystem::path record_path {};
  json record;
  base::Sha256Digest baseline {};
  Uuid generation;
  ImportRequest request {};
  std::optional<std::filesystem::path> previous;
  std::optional<serio::FileLock> generation_lock;
  std::optional<base::Sha256Digest> validated_index;
  std::optional<base::Sha256Digest> validated_provenance;
  std::string serialized_candidate;
  bool published = false;
  std::atomic_bool claimed { false };
};

RetainedModelImport::RetainedModelImport(std::unique_ptr<State> state)
  : state_(std::move(state))
{
}

RetainedModelImport::~RetainedModelImport() = default;

auto RetainedModelImport::MakeRecipe(const SceneImportSettings& settings,
  const LooseCookedLayout& layout) -> std::string
{
  if (!settings.material_slot_source_identity.empty()
    || !settings.material_slot_provenance_json.empty()
    || !settings.material_slot_provenance_path.empty()) {
    throw std::invalid_argument(
      "Retained identity is owned by the import record");
  }
  ImportRequest source;
  source.source_path = settings.source_path;
  const auto format = source.GetFormat();
  if (format != ImportFormat::kGltf && format != ImportFormat::kFbx) {
    throw std::invalid_argument(
      "Retained model source must be glTF, GLB or FBX");
  }
  auto job = TextureRecipe(settings.texture_defaults);
  job.update({ { "id", "model" },
    { "type", format == ImportFormat::kFbx ? "fbx" : "gltf" },
    { "source", Absolute(settings.source_path).generic_string() },
    { "content_hashing", settings.with_content_hashing },
    { "content_flags",
      {
        { "textures", settings.import_textures },
        { "materials", settings.import_materials },
        { "geometry", settings.import_geometry },
        { "scene", settings.import_scene },
      } },
    { "bake_transforms", settings.bake_transforms },
    { "gltf_omitted_light_range_m", settings.gltf_omitted_light_range_m } });
  const auto add = [&](const char* name, const std::string& value) -> void {
    if (!value.empty()) {
      job.update({ { name, value } });
    }
  };
  add("name", settings.job_name);
  add("content_policy", settings.content_policy);
  add("unit_policy", settings.unit_policy);
  add("normals_policy", settings.normals_policy);
  add("tangents_policy", settings.tangents_policy);
  add("node_pruning", settings.node_pruning);
  add("naming_policy", settings.naming_policy);
  if (settings.unit_scale_set) {
    job.update({ { "unit_scale", settings.unit_scale } });
  }
  if (!settings.texture_overrides.empty()) {
    auto overrides = json::object();
    for (const auto& [name, value] : settings.texture_overrides) {
      overrides.emplace(name, TextureRecipe(value));
    }
    job.update({ { "texture_overrides", std::move(overrides) } });
  }
  return json {
    { "version", 1 },
    {
      "layout",
      {
        { "virtual_mount_root", layout.virtual_mount_root },
        { "resources_dir", layout.resources_dir },
        { "descriptors_dir", layout.descriptors_dir },
        { "scenes_subdir", layout.scenes_subdir },
        { "geometry_subdir", layout.geometry_subdir },
        { "materials_subdir", layout.materials_subdir },
        { "scripts_subdir", layout.scripts_subdir },
        { "input_subdir", layout.input_subdir },
        { "texture_descriptors_subdir", layout.texture_descriptors_subdir },
        { "buffer_descriptors_subdir", layout.buffer_descriptors_subdir },
      },
    },
    { "jobs", json::array({ std::move(job) }) },
  }
    .dump();
}

auto RetainedModelImport::RecordPath(const std::filesystem::path& content_root,
  const std::filesystem::path& source_path) -> std::filesystem::path
{
  const auto root = Absolute(content_root);
  const auto records = root / "imports";
  std::optional<std::filesystem::path> match;
  if (std::filesystem::exists(base::ToNativePath(records))) {
    for (const auto& entry :
      std::filesystem::directory_iterator(base::ToNativePath(records))) {
      if (!entry.is_regular_file()
        || !entry.path().filename().string().ends_with(".import.json")) {
        continue;
      }
      const auto record_path = Absolute(entry.path());
      const auto record = ReadRecord(record_path, ReadText(record_path));
      const auto job
        = ValidateRecipe(record.at("recipe"), record_path.parent_path());
      const auto& settings = job.job_type == "fbx" ? job.fbx : job.gltf;
      std::error_code error;
      if (!std::filesystem::equivalent(base::ToNativePath(settings.source_path),
            base::ToNativePath(source_path), error)
        || error) {
        continue;
      }
      if (match) {
        throw std::invalid_argument(
          "Source has multiple retained recipes; select a record explicitly");
      }
      match = record_path;
    }
  }
  if (match) {
    return *match;
  }
  const auto source = Absolute(source_path).generic_string();
  const auto key = fmt::format("{:02x}", fmt::join(Digest(source), ""));
  return records / (key + ".import.json");
}

auto RetainedModelImport::SaveRecipe(const std::filesystem::path& record_path,
  const std::filesystem::path& content_root, const std::string_view recipe_json)
  -> void
{
  const auto path = Absolute(record_path);
  const auto root = Absolute(content_root);
  RequireWithin(path, root / "imports");
  auto recipe = json::parse(recipe_json);
  const auto parsed_job = ValidateRecipe(recipe, path.parent_path());
  const auto& source_settings
    = parsed_job.job_type == "fbx" ? parsed_job.fbx : parsed_job.gltf;
  const auto source = Absolute(source_settings.source_path);
  const auto within_content = source.lexically_relative(root);
  if (!within_content.empty() && !within_content.is_absolute()
    && *within_content.begin() != "..") {
    recipe.at("jobs").at(0).update({ { "source",
      source.lexically_relative(path.parent_path()).generic_string() } });
  }
  std::filesystem::create_directories(base::ToNativePath(path.parent_path()));
  const auto lock = RecordLock(path);
  json record;
  if (std::filesystem::exists(base::ToNativePath(path))) {
    record = ReadRecord(path, ReadText(path));
    if (Absolute(
          path.parent_path() / record.at("content_root").get<std::string>())
      != root) {
      throw std::invalid_argument(
        "Retained import belongs to another Content root");
    }
  } else {
    const MaterialSlotProvenance provenance(Uuid::Generate());
    record = {
      { "schema_version", 1 },
      {
        "content_root",
        root.lexically_relative(path.parent_path()).generic_string(),
      },
      { "material_slot_provenance", json::parse(provenance.Serialize()) },
      { "published_generation", nullptr },
    };
  }
  record.update({ { "recipe", std::move(recipe) } });
  const auto text = record.dump(2);
  if (!std::filesystem::exists(base::ToNativePath(path))
    || ReadText(path) != text) {
    WriteText(path, text);
  }
}

auto RetainedModelImport::SaveRecipeFile(
  const std::filesystem::path& record_path,
  const std::filesystem::path& content_root,
  const std::filesystem::path& recipe_path, const ImportFormat expected_format)
  -> void
{
  const auto path = Absolute(recipe_path);
  auto recipe = json::parse(ReadText(path));
  const auto job = ValidateRecipe(recipe, path.parent_path());
  const auto job_format
    = job.job_type == "fbx" ? ImportFormat::kFbx : ImportFormat::kGltf;
  const auto& settings = job_format == ImportFormat::kFbx ? job.fbx : job.gltf;
  ImportRequest source;
  source.source_path = settings.source_path;
  if (job_format != expected_format || source.GetFormat() != expected_format) {
    throw std::invalid_argument(
      "Recipe format does not match the import command");
  }
  recipe.at("jobs").at(0).update(
    { { "source", Absolute(settings.source_path).generic_string() } });
  SaveRecipe(record_path, content_root, recipe.dump());
}

auto RetainedModelImport::Prepare(const std::filesystem::path& record_path)
  -> std::shared_ptr<RetainedModelImport>
{
  auto state = std::make_unique<State>();
  state->record_path = Absolute(record_path);
  const auto lock = RecordLock(state->record_path);
  const auto text = ReadText(state->record_path);
  state->record = ReadRecord(state->record_path, text);
  RequireUniqueRecord(state->record_path, state->record);
  state->baseline = Digest(text);
  state->previous = SelectedPath(state->record_path, state->record);
  state->generation = Uuid::Generate();
  const auto root
    = GenerationPath(state->record_path, state->record, state->generation);
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
  auto job = ValidateRecipe(
    state->record.at("recipe"), state->record_path.parent_path());
  auto& settings = job.job_type == "fbx" ? job.fbx : job.gltf;
  settings.cooked_root = root.string();
  settings.material_slot_provenance_json
    = state->record.at("material_slot_provenance").dump();
  std::ostringstream errors;
  auto request = job.BuildRequest(errors);
  if (!request) {
    throw std::invalid_argument(errors.str());
  }
  state->request = std::move(*request);
  state->request.source_key = data::SourceKey { state->generation };
  return std::shared_ptr<RetainedModelImport>(
    new RetainedModelImport(std::move(state)));
}

auto RetainedModelImport::ReclaimUnusedGenerations() const -> size_t
{
  return ReclaimUnusedGenerations(state_->record_path);
}

auto RetainedModelImport::ReclaimUnusedGenerations(
  const std::filesystem::path& record_path) -> size_t
{
  const auto path = Absolute(record_path);
  const auto record_lock = RecordLock(path);
  const auto record = ReadRecord(path, ReadText(path));
  RequireUniqueRecord(path, record);
  const auto selected = SelectedPath(path, record);
  const auto directory = GenerationDirectory(path, record);
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
    const auto candidate = GenerationPath(path, record, id.value());
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

auto RetainedModelImport::ClaimSubmission() -> void
{
  if (state_->claimed.exchange(true) || state_->published) {
    throw std::logic_error(
      "A prepared import generation can only be submitted once");
  }
}

auto RetainedModelImport::AllowsWriting(const std::filesystem::path& root) const
  -> bool
{
  const auto& cooked_root = state_->request.cooked_root;
  return state_->claimed.load() && !state_->published && state_->generation_lock
    && cooked_root && Absolute(root) == Absolute(*cooked_root);
}

auto RetainedModelImport::Request() const -> const ImportRequest&
{
  return state_->request;
}

auto RetainedModelImport::PreviousGeneration() const
  -> std::optional<std::filesystem::path>
{
  return state_->previous;
}

auto RetainedModelImport::SelectedGeneration(
  const std::filesystem::path& record_path)
  -> std::optional<std::filesystem::path>
{
  const auto path = Absolute(record_path);
  const auto record = ReadRecord(path, ReadText(path));
  const auto selected = SelectedPath(path, record);
  if (!selected || !std::filesystem::exists(base::ToNativePath(*selected))) {
    return selected;
  }
  const auto lease = serio::FileLock::TryAcquire(
    *selected / data::loose_cooked::kGenerationLeaseFileName,
    serio::FileLockMode::kShared);
  if (!lease) {
    throw std::system_error(
      lease.error(), "Selected import generation is unavailable");
  }
  const auto actual
    = base::ComputeFileSha256(*selected / "container.index.bin");
  if (fmt::format("{:02x}", fmt::join(actual, ""))
    != record.at("published_generation")
      .at("index_sha256")
      .get<std::string>()) {
    throw std::runtime_error(
      "Selected import generation index changed after publication");
  }
  return selected;
}

auto RetainedModelImport::ValidateCandidate(const ImportReport& report) -> void
{
  const auto& cooked_root = state_->request.cooked_root;
  const auto source_key = state_->request.source_key;
  if (!cooked_root || !source_key) {
    throw std::invalid_argument("Retained generation identity is incomplete");
  }
  const auto expected_source_key = *source_key;
  if (state_->published || !report.success
    || report.material_slot_provenance_json.empty()
    || Absolute(report.cooked_root) != Absolute(*cooked_root)) {
    throw std::invalid_argument(
      "Only this attempt's successful native report can be published");
  }
  const auto provenance
    = MaterialSlotProvenance::Parse(report.material_slot_provenance_json);
  if (provenance->SourceIdentity()
    != state_->request.material_slot_provenance->SourceIdentity()) {
    throw std::invalid_argument(
      "Native report changed its retained source identity");
  }
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
    const auto* retained = provenance->FindGeometry(asset.key);
    if (retained == nullptr) {
      throw std::invalid_argument(
        "Cooked geometry has no matching retained provenance");
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
    if (geometry->MaterialSlots().layout_revision
      != retained->inventory.layout_revision) {
      throw std::invalid_argument(
        "Retained inventory does not match its cooked geometry");
    }
  }
  if (geometry_count != provenance->Geometries().size()) {
    throw std::invalid_argument(
      "Retained provenance contains geometry absent from its generation");
  }
  state_->validated_index
    = base::ComputeFileSha256(report.cooked_root / "container.index.bin");
  state_->validated_provenance = Digest(report.material_slot_provenance_json);
  auto record = state_->record;
  record.update(
    { { "material_slot_provenance", json::parse(provenance->Serialize()) } });
  record.update({ { "published_generation",
    {
      { "id", state_->generation.ToString() },
      {
        "index_sha256",
        fmt::format("{:02x}", fmt::join(*state_->validated_index, "")),
      },
    } } });
  state_->serialized_candidate = record.dump(2);
}

auto RetainedModelImport::Publish(
  ImportReport& report, const std::stop_token stop_token) -> void
{
  const auto& cooked_root = state_->request.cooked_root;
  if (!cooked_root || state_->published || !report.success
    || !state_->validated_index || !state_->validated_provenance
    || *state_->validated_provenance
      != Digest(report.material_slot_provenance_json)
    || Absolute(report.cooked_root) != Absolute(*cooked_root)
    || *state_->validated_index
      != base::ComputeFileSha256(report.cooked_root / "container.index.bin")) {
    throw std::invalid_argument(
      "Retained candidate was not validated or changed before publication");
  }
  const auto lock = RecordLock(state_->record_path);
  if (Digest(ReadText(state_->record_path)) != state_->baseline) {
    throw std::runtime_error(
      "Retained source settings or publication changed during import");
  }
  if (stop_token.stop_requested()) {
    throw std::runtime_error(
      "Retained publication was cancelled before commit");
  }
  report.retained_record_path = state_->record_path;
  report.previous_cooked_root = state_->previous;
  WriteText(state_->record_path, state_->serialized_candidate);
  state_->published = true;
  state_->generation_lock.reset();
}

} // namespace oxygen::content::import
