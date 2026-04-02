//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <bit>
#include <cstddef>
#include <fstream>
#include <limits>
#include <optional>
#include <string>
#include <string_view>
#include <type_traits>
#include <vector>

#include <Oxygen/Base/Logging.h>
#include <Oxygen/Cooker/Import/IAsyncFileWriter.h>
#include <Oxygen/Cooker/Import/Internal/Emitters/AssetEmitter.h>
#include <Oxygen/Cooker/Import/Internal/Emitters/BufferEmitter.h>
#include <Oxygen/Cooker/Import/Internal/Emitters/PhysicsResourceEmitter.h>
#include <Oxygen/Cooker/Import/Internal/Emitters/ResourceDescriptorEmitter.h>
#include <Oxygen/Cooker/Import/Internal/Emitters/TextureEmitter.h>
#include <Oxygen/Cooker/Import/Internal/ImportSession.h>
#include <Oxygen/Cooker/Import/Internal/LooseCookedIndexRegistry.h>
#include <Oxygen/Cooker/Import/Internal/ResourceTableRegistry.h>
#include <Oxygen/Data/LooseCookedIndexFormat.h>
#include <Oxygen/Data/PakFormat.h>
#include <Oxygen/Data/SceneAsset.h>

namespace oxygen::content::import {

namespace {

  auto EnsureExternalFileExists(const std::filesystem::path& path) -> void
  {
    std::error_code ec;
    if (std::filesystem::exists(path, ec)) {
      return;
    }

    std::filesystem::create_directories(path.parent_path(), ec);
    if (ec) {
      throw std::runtime_error(
        "Failed to create directory for external file: " + path.string());
    }

    std::ofstream out(path, std::ios::binary | std::ios::app);
    if (!out) {
      throw std::runtime_error(
        "Failed to create external file: " + path.string());
    }
  }

  auto RegisterExternalTable(LooseCookedIndexRegistry& registry,
    const data::loose_cooked::FileKind kind,
    const std::filesystem::path& cooked_root, std::string_view relpath) -> void
  {
    const auto path = cooked_root / std::filesystem::path(relpath);
    EnsureExternalFileExists(path);
    registry.RegisterExternalFile(cooked_root, kind, relpath);
  }

  auto AppendOutputRecord(std::vector<ImportOutputRecord>& outputs,
    std::vector<ImportDiagnostic>& diagnostics,
    const std::filesystem::path& cooked_root, std::string_view relpath,
    std::string_view source_path) -> bool
  {
    std::error_code ec;
    const auto full_path = cooked_root / std::filesystem::path(relpath);
    const auto size = std::filesystem::file_size(full_path, ec);
    if (ec) {
      diagnostics.push_back({
        .severity = ImportSeverity::kError,
        .code = "import.output_missing",
        .message = "Expected output missing: " + std::string(relpath),
        .source_path = std::string(source_path),
        .object_path = {},
      });
      return false;
    }

    outputs.push_back({
      .path = std::string(relpath),
      .size_bytes = size,
    });
    return true;
  }

  auto BuildPackagingSummary(const ImportReport& report,
    const std::optional<LooseCookedWriteResult>& write_result,
    const bool index_write_deferred) -> ImportPackagingSummary
  {
    ImportPackagingSummary summary {};
    summary.outputs_written = static_cast<uint32_t>(report.outputs.size());
    summary.index_written = write_result.has_value();
    summary.index_write_deferred = index_write_deferred;

    for (const auto& diagnostic : report.diagnostics) {
      switch (diagnostic.severity) {
      case ImportSeverity::kInfo:
        ++summary.diagnostics_info;
        break;
      case ImportSeverity::kWarning:
        ++summary.diagnostics_warning;
        break;
      case ImportSeverity::kError:
        ++summary.diagnostics_error;
        break;
      }

      if (diagnostic.code == "import.dedup_collision.texture") {
        ++summary.texture_dedup_collisions;
      } else if (diagnostic.code == "import.dedup_collision.buffer") {
        ++summary.buffer_dedup_collisions;
      }
    }

    if (write_result.has_value()) {
      summary.index_asset_collisions
        = write_result->collision_summary.asset_collisions;
      summary.index_file_collisions
        = write_result->collision_summary.file_collisions;
      summary.index_collisions_kept
        = write_result->collision_summary.kept_existing;
      summary.index_collisions_replaced
        = write_result->collision_summary.replaced_existing;
      summary.index_collisions_rejected
        = write_result->collision_summary.rejected;
    }

    return summary;
  }

  auto BuildScriptBindingsTableRelPath(const ImportRequest& request)
    -> std::string
  {
    return request.loose_cooked_layout.ScriptBindingsTableRelPath();
  }

  auto BuildScriptBindingsDataRelPath(const ImportRequest& request)
    -> std::string
  {
    return request.loose_cooked_layout.ScriptBindingsDataRelPath();
  }

  template <typename RecordT>
  auto CountPackedRecords(const std::filesystem::path& file_path) -> uint32_t
  {
    static_assert(std::is_trivially_copyable_v<RecordT>);

    std::error_code ec;
    const auto size = std::filesystem::file_size(file_path, ec);
    if (ec || size == 0U || (size % sizeof(RecordT)) != 0U) {
      return 0U;
    }

    const auto count = size / sizeof(RecordT);
    if (count > (std::numeric_limits<uint32_t>::max)()) {
      return (std::numeric_limits<uint32_t>::max)();
    }
    return static_cast<uint32_t>(count);
  }

  auto ReadBinaryFile(const std::filesystem::path& path)
    -> std::vector<std::byte>
  {
    auto in = std::ifstream(path, std::ios::binary | std::ios::ate);
    if (!in) {
      return {};
    }
    const auto end = in.tellg();
    if (end <= std::streampos { 0 }) {
      return {};
    }

    const auto size = static_cast<size_t>(end);
    auto bytes = std::vector<std::byte>(size);
    in.seekg(0, std::ios::beg);
    in.read(
      std::bit_cast<char*>(bytes.data()), static_cast<std::streamsize>(size));
    if (!in.good() && !in.eof()) {
      return {};
    }
    return bytes;
  }

  auto CountScriptingComponentsInSceneDescriptor(
    const std::filesystem::path& descriptor_path, const data::AssetKey& key)
    -> uint32_t
  {
    const auto bytes = ReadBinaryFile(descriptor_path);
    if (bytes.empty()) {
      return 0U;
    }

    try {
      auto scene = data::SceneAsset(key, bytes);
      const auto components
        = scene.GetComponents<data::pak::scripting::ScriptingComponentRecord>();
      const auto count = components.size();
      if (count > (std::numeric_limits<uint32_t>::max)()) {
        return (std::numeric_limits<uint32_t>::max)();
      }
      return static_cast<uint32_t>(count);
    } catch (...) {
      return 0U;
    }
  }

  auto BuildDiagnosticKey(const ImportDiagnostic& diagnostic) -> std::string
  {
    auto key = std::string {};
    key.reserve(diagnostic.code.size() + diagnostic.message.size()
      + diagnostic.source_path.size() + diagnostic.object_path.size() + 16U);
    key.append(std::to_string(static_cast<uint32_t>(diagnostic.severity)));
    key.push_back('\x1F');
    key.append(diagnostic.code);
    key.push_back('\x1F');
    key.append(diagnostic.message);
    key.push_back('\x1F');
    key.append(diagnostic.source_path);
    key.push_back('\x1F');
    key.append(diagnostic.object_path);
    return key;
  }

} // namespace

ImportSession::ImportSession(const ImportRequest& request,
  observer_ptr<IAsyncFileReader> file_reader,
  observer_ptr<IAsyncFileWriter> file_writer,
  observer_ptr<co::ThreadPool> thread_pool,
  observer_ptr<ResourceTableRegistry> table_registry,
  observer_ptr<LooseCookedIndexRegistry> index_registry)
  : request_(request)
  , file_reader_(file_reader)
  , file_writer_(file_writer)
  , thread_pool_(thread_pool)
  , table_registry_(table_registry)
  , index_registry_(index_registry)
  , cooked_root_(
      request.cooked_root.value_or(request.source_path.parent_path()))
  , cooked_writer_(cooked_root_)
{
  DLOG_F(INFO, "Session created for: {}", request_.source_path.string());
  DLOG_F(INFO,
    "Session options: requested_hashing={} effective_hashing={} "
    "dedup_collision_policy={}",
    request_.options.with_content_hashing,
    EffectiveContentHashingEnabled(request_.options.with_content_hashing),
    to_string(request_.options.dedup_collision_policy));

  DCHECK_F(file_writer_ != nullptr,
    "ImportSession requires a valid async file writer");
  DCHECK_F(index_registry_ != nullptr,
    "ImportSession requires a LooseCookedIndexRegistry");
  DCHECK_F(table_registry_ != nullptr,
    "ImportSession requires a ResourceTableRegistry");

  // Set source key if provided in request
  if (request_.source_key.has_value()) {
    cooked_writer_.SetSourceKey(request_.source_key);
  }

  table_registry_->BeginSession(cooked_root_);
  index_registry_->BeginSession(cooked_root_, request_.source_key);
}

ImportSession::~ImportSession() { DLOG_F(INFO, "Session destroyed"); }

auto ImportSession::Request() const noexcept -> const ImportRequest&
{
  return request_;
}

auto ImportSession::CookedRoot() const noexcept -> const std::filesystem::path&
{
  return cooked_root_;
}

auto ImportSession::CookedWriter() noexcept -> LooseCookedWriter&
{
  return cooked_writer_;
}

auto ImportSession::FileReader() const noexcept
  -> observer_ptr<IAsyncFileReader>
{
  return file_reader_;
}

auto ImportSession::FileWriter() const noexcept
  -> observer_ptr<IAsyncFileWriter>
{
  return file_writer_;
}

auto ImportSession::ThreadPool() const noexcept -> observer_ptr<co::ThreadPool>
{
  return thread_pool_;
}

auto ImportSession::TableRegistry() const noexcept
  -> observer_ptr<ResourceTableRegistry>
{
  return table_registry_;
}

/*!
 Get (and lazily create) the texture emitter for this session.

 @warning This method is not thread-safe. It must be called from the importer
  thread only.
*/
auto ImportSession::TextureEmitter() -> import::TextureEmitter&
{
  if (!texture_emitter_.has_value()) {
    DCHECK_F(table_registry_ != nullptr,
      "ImportSession requires a ResourceTableRegistry for texture emission");
    auto& aggregator = table_registry_->TextureAggregator(
      cooked_root_, request_.loose_cooked_layout);
    TextureEmitter::Config config {};
    config.cooked_root = cooked_root_;
    config.layout = request_.loose_cooked_layout;
    config.with_content_hashing
      = EffectiveContentHashingEnabled(request_.options.with_content_hashing);
    config.collision_policy = request_.options.dedup_collision_policy;
    config.on_dedup_diagnostic = [this](ImportDiagnostic diagnostic) {
      AddDiagnostic(std::move(diagnostic));
    };
    texture_emitter_.emplace(std::make_unique<import::TextureEmitter>(
      *file_writer_, aggregator, std::move(config)));
  }
  return **texture_emitter_;
}

/*!
 Get (and lazily create) the buffer emitter for this session.

 @warning This method is not thread-safe. It must be called from the importer
  thread only.
*/
auto ImportSession::BufferEmitter() -> import::BufferEmitter&
{
  if (!buffer_emitter_.has_value()) {
    DCHECK_F(table_registry_ != nullptr,
      "ImportSession requires a ResourceTableRegistry for buffer emission");
    auto& aggregator = table_registry_->BufferAggregator(
      cooked_root_, request_.loose_cooked_layout);
    BufferEmitter::Config config {};
    config.collision_policy = request_.options.dedup_collision_policy;
    config.on_dedup_diagnostic = [this](ImportDiagnostic diagnostic) {
      AddDiagnostic(std::move(diagnostic));
    };
    buffer_emitter_.emplace(
      std::make_unique<import::BufferEmitter>(*file_writer_, aggregator,
        request_.loose_cooked_layout, cooked_root_, std::move(config)));
  }
  return **buffer_emitter_;
}

/*!
 Get (and lazily create) the physics resource emitter for this session.

 @warning This method is not thread-safe. It must be called from the importer
  thread only.
*/
auto ImportSession::PhysicsResourceEmitter() -> import::PhysicsResourceEmitter&
{
  if (!physics_resource_emitter_.has_value()) {
    DCHECK_F(table_registry_ != nullptr,
      "ImportSession requires a ResourceTableRegistry for physics resource "
      "emission");
    auto& aggregator = table_registry_->PhysicsAggregator(
      cooked_root_, request_.loose_cooked_layout);
    auto config = import::PhysicsResourceEmitter::Config {};
    config.collision_policy = request_.options.dedup_collision_policy;
    config.on_dedup_diagnostic = [this](ImportDiagnostic diagnostic) {
      AddDiagnostic(std::move(diagnostic));
    };
    physics_resource_emitter_.emplace(
      std::make_unique<import::PhysicsResourceEmitter>(*file_writer_,
        aggregator, request_.loose_cooked_layout, cooked_root_,
        std::move(config)));
  }
  return **physics_resource_emitter_;
}

/*!
 Get (and lazily create) the asset emitter for this session.

 @warning This method is not thread-safe. It must be called from the importer
  thread only.
*/
auto ImportSession::AssetEmitter() -> import::AssetEmitter&
{
  if (!asset_emitter_.has_value()) {
    asset_emitter_.emplace(std::make_unique<import::AssetEmitter>(
      *file_writer_, request_.loose_cooked_layout, cooked_root_));
  }
  return **asset_emitter_;
}

/*!
 Get (and lazily create) the resource descriptor emitter for this session.


 * @warning This method is not thread-safe. It must be called from the importer

 * thread only.
*/
auto ImportSession::ResourceDescriptorEmitter()
  -> import::ResourceDescriptorEmitter&
{
  if (!resource_descriptor_emitter_.has_value()) {
    resource_descriptor_emitter_.emplace(
      std::make_unique<import::ResourceDescriptorEmitter>(
        *file_writer_, request_.loose_cooked_layout, cooked_root_));
  }
  return **resource_descriptor_emitter_;
}

auto ImportSession::AddIoDuration(std::chrono::microseconds duration) noexcept
  -> void
{
  io_duration_ += duration;
}

auto ImportSession::AddSourceLoadDuration(
  std::chrono::microseconds duration) noexcept -> void
{
  source_load_duration_ += duration;
}

auto ImportSession::AddDecodeDuration(
  std::chrono::microseconds duration) noexcept -> void
{
  decode_duration_ += duration;
}

auto ImportSession::AddLoadDuration(std::chrono::microseconds duration) noexcept
  -> void
{
  load_duration_ += duration;
}

auto ImportSession::AddCookDuration(std::chrono::microseconds duration) noexcept
  -> void
{
  cook_duration_ += duration;
}

auto ImportSession::IoDuration() const noexcept -> std::chrono::microseconds
{
  return io_duration_;
}

auto ImportSession::SourceLoadDuration() const noexcept
  -> std::chrono::microseconds
{
  return source_load_duration_;
}

auto ImportSession::DecodeDuration() const noexcept -> std::chrono::microseconds
{
  return decode_duration_;
}

auto ImportSession::LoadDuration() const noexcept -> std::chrono::microseconds
{
  return load_duration_;
}

auto ImportSession::CookDuration() const noexcept -> std::chrono::microseconds
{
  return cook_duration_;
}

auto ImportSession::AddEmitDuration(std::chrono::microseconds duration) noexcept
  -> void
{
  emit_duration_ += duration;
}

auto ImportSession::EmitDuration() const noexcept -> std::chrono::microseconds
{
  return emit_duration_;
}

auto ImportSession::AddDiagnostic(ImportDiagnostic diagnostic) -> void
{
  auto added = ImportDiagnostic {};
  auto should_log = false;
  const bool is_error = diagnostic.severity == ImportSeverity::kError;
  const auto key = BuildDiagnosticKey(diagnostic);

  {
    std::scoped_lock lock(diagnostics_mutex_);
    if (!diagnostic_keys_.insert(key).second) {
      if (is_error) {
        has_errors_ = true;
      }
      return;
    }
    diagnostics_.push_back(std::move(diagnostic));
    added = diagnostics_.back();
    should_log = true;
    if (is_error) {
      has_errors_ = true;
    }
  }

  if (!should_log) {
    return;
  }

  // Log based on severity
  switch (added.severity) {
  case ImportSeverity::kError:
    LOG_F(ERROR, "[{}] {}", added.code, added.message);
    break;
  case ImportSeverity::kWarning:
    LOG_F(WARNING, "[{}] {}", added.code, added.message);
    break;
  case ImportSeverity::kInfo:
    DLOG_F(INFO, "[{}] {}", added.code, added.message);
    break;
  }
}

auto ImportSession::Diagnostics() const -> std::vector<ImportDiagnostic>
{
  std::scoped_lock lock(diagnostics_mutex_);
  return diagnostics_;
}

auto ImportSession::HasErrors() const noexcept -> bool
{
  std::scoped_lock lock(diagnostics_mutex_);
  return has_errors_;
}

auto ImportSession::Finalize() -> co::Co<ImportReport>
{
  DLOG_F(INFO, "Finalize starting");

  if (texture_emitter_.has_value()) {
    const auto ok = co_await (*texture_emitter_)->Finalize();
    if (!ok) {
      AddDiagnostic({
        .severity = ImportSeverity::kError,
        .code = "import.texture_emitter_finalize_failed",
        .message = "Texture emitter finalization failed",
        .source_path = request_.source_path.string(),
        .object_path = {},
      });
    }
  }

  if (buffer_emitter_.has_value()) {
    const auto ok = co_await (*buffer_emitter_)->Finalize();
    if (!ok) {
      AddDiagnostic({
        .severity = ImportSeverity::kError,
        .code = "import.buffer_emitter_finalize_failed",
        .message = "Buffer emitter finalization failed",
        .source_path = request_.source_path.string(),
        .object_path = {},
      });
    }
  }

  if (physics_resource_emitter_.has_value()) {
    const auto ok = co_await (*physics_resource_emitter_)->Finalize();
    if (!ok) {
      AddDiagnostic({
        .severity = ImportSeverity::kError,
        .code = "import.physics_resource_emitter_finalize_failed",
        .message = "Physics resource emitter finalization failed",
        .source_path = request_.source_path.string(),
        .object_path = {},
      });
    }
  }

  if (asset_emitter_.has_value()) {
    const auto ok = co_await (*asset_emitter_)->Finalize();
    if (!ok) {
      AddDiagnostic({
        .severity = ImportSeverity::kError,
        .code = "import.asset_emitter_finalize_failed",
        .message = "Asset emitter finalization failed",
        .source_path = request_.source_path.string(),
        .object_path = {},
      });
    }
  }

  if (resource_descriptor_emitter_.has_value()) {
    const auto ok = co_await (*resource_descriptor_emitter_)->Finalize();
    if (!ok) {
      AddDiagnostic({
        .severity = ImportSeverity::kError,
        .code = "import.resource_descriptor_emitter_finalize_failed",
        .message = "Resource descriptor emitter finalization failed",
        .source_path = request_.source_path.string(),
        .object_path = {},
      });
    }
  }

  const auto texture_count = texture_emitter_.has_value()
    ? (*texture_emitter_)->GetStats().emitted_textures
    : 0U;
  const auto buffer_count
    = buffer_emitter_.has_value() ? (*buffer_emitter_)->Count() : 0U;

#ifndef NDEBUG
  const auto asset_count
    = asset_emitter_.has_value() ? (*asset_emitter_)->Records().size() : 0U;
#endif // NDEBUG

  const auto ok = co_await table_registry_->EndSession(cooked_root_);
  if (!ok) {
    AddDiagnostic({
      .severity = ImportSeverity::kError,
      .code = "import.resource_table_finalize_failed",
      .message = "Resource table finalization failed",
      .source_path = request_.source_path.string(),
      .object_path = {},
    });
  }

  // Wait for any pending async writes
  auto flush_result = co_await file_writer_->Flush();
  if (!flush_result.has_value()) {
    AddDiagnostic({
      .severity = ImportSeverity::kError,
      .code = "import.flush_failed",
      .message = flush_result.error().message,
      .source_path = request_.source_path.string(),
      .object_path = {},
    });
  }

  // Build the report
  bool had_errors = HasErrors();
  auto report = ImportReport {};
  report.cooked_root = cooked_root_;
  report.diagnostics = Diagnostics();

  // Always attempt to write the index to keep file sizes in sync, even if
  // diagnostics reported errors. This prevents stale index metadata from
  // invalidating previously cooked content.
  try {
    using data::loose_cooked::FileKind;
    constexpr std::string_view kIndexFileName = "container.index.bin";

    const auto& layout = request_.loose_cooked_layout;

    DLOG_F(INFO,
      "Registering index entries: textures={} buffers={} assets={} "
      "cooked_root='{}'",
      texture_count, buffer_count, asset_count, cooked_root_.string());

    bool output_missing = false;

    if (texture_count > 0) {
      index_registry_->RegisterExternalFile(
        cooked_root_, FileKind::kTexturesData, layout.TexturesDataRelPath());

      RegisterExternalTable(*index_registry_, FileKind::kTexturesTable,
        cooked_root_, layout.TexturesTableRelPath());

      output_missing
        |= !AppendOutputRecord(report.outputs, report.diagnostics, cooked_root_,
          layout.TexturesDataRelPath(), request_.source_path.string());
      output_missing
        |= !AppendOutputRecord(report.outputs, report.diagnostics, cooked_root_,
          layout.TexturesTableRelPath(), request_.source_path.string());
    }

    if (buffer_count > 0) {
      index_registry_->RegisterExternalFile(
        cooked_root_, FileKind::kBuffersData, layout.BuffersDataRelPath());

      RegisterExternalTable(*index_registry_, FileKind::kBuffersTable,
        cooked_root_, layout.BuffersTableRelPath());

      output_missing
        |= !AppendOutputRecord(report.outputs, report.diagnostics, cooked_root_,
          layout.BuffersDataRelPath(), request_.source_path.string());
      output_missing
        |= !AppendOutputRecord(report.outputs, report.diagnostics, cooked_root_,
          layout.BuffersTableRelPath(), request_.source_path.string());
    }

    const auto physics_table_rel = layout.PhysicsTableRelPath();
    const auto physics_data_rel = layout.PhysicsDataRelPath();
    const auto physics_table_path
      = cooked_root_ / std::filesystem::path(physics_table_rel);
    const auto physics_data_path
      = cooked_root_ / std::filesystem::path(physics_data_rel);
    const auto physics_table_exists
      = std::filesystem::exists(physics_table_path);
    const auto physics_data_exists = std::filesystem::exists(physics_data_path);

    if (physics_table_exists || physics_data_exists) {
      RegisterExternalTable(*index_registry_, FileKind::kPhysicsData,
        cooked_root_, physics_data_rel);
      RegisterExternalTable(*index_registry_, FileKind::kPhysicsTable,
        cooked_root_, physics_table_rel);

      output_missing |= !AppendOutputRecord(report.outputs, report.diagnostics,
        cooked_root_, physics_data_rel, request_.source_path.string());
      output_missing |= !AppendOutputRecord(report.outputs, report.diagnostics,
        cooked_root_, physics_table_rel, request_.source_path.string());
    }

    if (asset_emitter_.has_value()) {
      const auto& records = (*asset_emitter_)->Records();
      report.outputs.reserve(report.outputs.size() + records.size());
      for (const auto& rec : records) {
        index_registry_->RegisterExternalAssetDescriptor(cooked_root_, rec.key,
          rec.asset_type, rec.virtual_path, rec.descriptor_relpath,
          rec.descriptor_size, rec.descriptor_sha256);
        report.outputs.push_back({
          .path = rec.descriptor_relpath,
          .size_bytes = rec.descriptor_size,
        });
      }
    }

    if (resource_descriptor_emitter_.has_value()) {
      const auto records = (*resource_descriptor_emitter_)->Records();
      report.outputs.reserve(report.outputs.size() + records.size());
      for (const auto& rec : records) {
        output_missing
          |= !AppendOutputRecord(report.outputs, report.diagnostics,
            cooked_root_, rec.relpath, request_.source_path.string());
      }
    }

    const auto write_result = index_registry_->EndSession(cooked_root_);
    const bool index_write_deferred = !write_result.has_value();
    if (write_result.has_value()) {
      LOG_F(INFO, "Index write completed: assets={} files={} cooked_root='{}'",
        write_result->assets.size(), write_result->files.size(),
        cooked_root_.string());
      report.source_key = write_result->source_key;
      output_missing |= !AppendOutputRecord(report.outputs, report.diagnostics,
        cooked_root_, kIndexFileName, request_.source_path.string());
    } else {
      DLOG_F(INFO,
        "Index write deferred (other sessions active) for cooked_root='{}'",
        cooked_root_.string());
    }

    if (asset_emitter_.has_value()) {
      for (const auto& rec : (*asset_emitter_)->Records()) {
        switch (rec.asset_type) {
        case data::AssetType::kMaterial:
          ++report.materials_written;
          break;
        case data::AssetType::kGeometry:
          ++report.geometry_written;
          break;
        case data::AssetType::kScene:
          ++report.scenes_written;
          break;
        case data::AssetType::kScript:
          ++report.scripts_written;
          break;
        default:
          break;
        }
      }
    }

    if (request_.options.scripting.import_kind
      == ScriptingImportKind::kScriptingSidecar) {
      report.script_slots_written
        = CountPackedRecords<data::pak::scripting::ScriptSlotRecord>(
          cooked_root_ / BuildScriptBindingsTableRelPath(request_));
      report.script_params_written
        = CountPackedRecords<data::pak::scripting::ScriptParamRecord>(
          cooked_root_ / BuildScriptBindingsDataRelPath(request_));

      auto component_count = uint64_t { 0 };
      if (asset_emitter_.has_value()) {
        for (const auto& rec : (*asset_emitter_)->Records()) {
          if (rec.asset_type != data::AssetType::kScene) {
            continue;
          }
          component_count += CountScriptingComponentsInSceneDescriptor(
            cooked_root_ / std::filesystem::path(rec.descriptor_relpath),
            rec.key);
        }
      }
      if (component_count > (std::numeric_limits<uint32_t>::max)()) {
        report.scripting_components_written
          = (std::numeric_limits<uint32_t>::max)();
      } else {
        report.scripting_components_written
          = static_cast<uint32_t>(component_count);
      }
    }

    if (report.outputs.empty()) {
      report.diagnostics.push_back({
        .severity = ImportSeverity::kError,
        .code = "import.outputs_missing",
        .message = "Import produced no outputs",
        .source_path = request_.source_path.string(),
        .object_path = {},
      });
      output_missing = true;
    }

    had_errors = had_errors || output_missing;

    if (had_errors) {
      report.diagnostics.push_back({
        .severity = ImportSeverity::kWarning,
        .code = "import.index_written_with_errors",
        .message = "Index written despite import errors",
        .source_path = request_.source_path.string(),
        .object_path = {},
      });
    }

    report.success = !had_errors;
    report.packaging
      = BuildPackagingSummary(report, write_result, index_write_deferred);

    LOG_F(INFO,
      "Packaging summary: success={} outputs={} index_written={} "
      "index_deferred={} diagnostics(info/warn/error)={}/{}/{} "
      "dedup(texture/buffer)={}/{} index_collisions(assets/files/keep/replace/"
      "reject)={}/{}/{}/{}/{}",
      report.success, report.packaging.outputs_written,
      report.packaging.index_written, report.packaging.index_write_deferred,
      report.packaging.diagnostics_info, report.packaging.diagnostics_warning,
      report.packaging.diagnostics_error,
      report.packaging.texture_dedup_collisions,
      report.packaging.buffer_dedup_collisions,
      report.packaging.index_asset_collisions,
      report.packaging.index_file_collisions,
      report.packaging.index_collisions_kept,
      report.packaging.index_collisions_replaced,
      report.packaging.index_collisions_rejected);

    DLOG_F(INFO,
      "Finalize complete: {} materials, {} geometry, {} scenes, {} scripts, "
      "{} scripting components, {} slots, {} params",
      report.materials_written, report.geometry_written, report.scenes_written,
      report.scripts_written, report.scripting_components_written,
      report.script_slots_written, report.script_params_written);
  } catch (const std::exception& ex) {
    report.success = false;
    report.diagnostics.push_back({
      .severity = ImportSeverity::kError,
      .code = "import.index_write_failed",
      .message = ex.what(),
      .source_path = request_.source_path.string(),
      .object_path = {},
    });
    LOG_F(ERROR, "Failed to write index: {}", ex.what());
  }

  co_return report;
}

} // namespace oxygen::content::import
