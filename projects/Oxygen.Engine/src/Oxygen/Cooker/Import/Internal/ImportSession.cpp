//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <filesystem>
#include <limits>
#include <memory>
#include <mutex>
#include <optional>
#include <ranges>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>
#include <type_traits>
#include <utility>
#include <vector>

#include <Oxygen/Base/Logging.h>
#include <Oxygen/Base/ObserverPtr.h>
#include <Oxygen/Base/ScopeGuard.h>
#include <Oxygen/Content/LooseCookedIndex.h>
#include <Oxygen/Content/VirtualPath.h>
#include <Oxygen/Cooker/Cooked/CookerStorage.h>
#include <Oxygen/Cooker/Import/ImportDiagnostics.h>
#include <Oxygen/Cooker/Import/ImportOptions.h>
#include <Oxygen/Cooker/Import/ImportReport.h>
#include <Oxygen/Cooker/Import/ImportRequest.h>
#include <Oxygen/Cooker/Import/Internal/Emitters/AssetEmitter.h>
#include <Oxygen/Cooker/Import/Internal/Emitters/BufferEmitter.h>
#include <Oxygen/Cooker/Import/Internal/Emitters/PhysicsResourceEmitter.h>
#include <Oxygen/Cooker/Import/Internal/Emitters/ResourceDescriptorEmitter.h>
#include <Oxygen/Cooker/Import/Internal/Emitters/TextureEmitter.h>
#include <Oxygen/Cooker/Import/Internal/ImportSession.h>
#include <Oxygen/Cooker/Import/Internal/LooseCookedIndexRegistry.h>
#include <Oxygen/Cooker/Import/Internal/LooseCookedLayout.h>
#include <Oxygen/Cooker/Import/Internal/LooseCookedWriter.h>
#include <Oxygen/Cooker/Import/Internal/ResourceTableRegistry.h>
#include <Oxygen/Cooker/Import/Internal/Utils/MountedCookedSources.h>
#include <Oxygen/Cooker/Import/Internal/Utils/VirtualPathResolution.h>
#include <Oxygen/Cooker/Import/ModelImportState.h>
#include <Oxygen/Cooker/Import/SourceMounts.h>
#include <Oxygen/Cooker/Import/SurfaceProvenance.h>
#include <Oxygen/Core/Types/AssetKey.h>
#include <Oxygen/Data/AssetType.h>
#include <Oxygen/Data/LooseCookedIndexEncoder.h>
#include <Oxygen/Data/LooseCookedIndexFormat.h>
#include <Oxygen/Data/PakFormat.h>
#include <Oxygen/Data/SceneAsset.h>
#include <Oxygen/OxCo/Awaitables.h>
#include <Oxygen/OxCo/Co.h>
#include <Oxygen/OxCo/Nursery.h>
#include <Oxygen/Serio/IoDriver.h>

namespace oxygen::content::import {

namespace {

  namespace loose_layout = internal::loose_layout;

  //! Index entries refused because the job has no recorded source.
  class LooseCookedSourceOutsideMount final : public std::runtime_error {
  public:
    using std::runtime_error::runtime_error;
  };

  //! A resource table kind, its data file kind and the data file's path.
  struct ResourceFiles final {
    data::loose_cooked::FileKind table;
    data::loose_cooked::FileKind data;
    std::string_view data_relpath;
  };

  //! The table of `kind` this session wrote, under its content name; none
  //! when the session wrote none, and the published one stays.
  auto WrittenTableOf(const ResourceTableRegistry::SessionEnd& tables,
    const data::loose_cooked::FileKind kind)
    -> const ResourceTableRegistry::WrittenTable*
  {
    const auto written = std::ranges::find(
      tables.written, kind, &ResourceTableRegistry::WrittenTable::kind);
    return written == tables.written.end() ? nullptr : &*written;
  }

  auto AppendOutputRecord(const cooked::CookerStorage& storage,
    std::vector<ImportOutputRecord>& outputs,
    std::vector<ImportDiagnostic>& diagnostics,
    const std::filesystem::path& cooked_root, std::string_view relpath,
    std::string_view source_path) -> bool
  {
    const auto size
      = storage.SizeOf(cooked_root / std::filesystem::path(relpath));
    if (!size.has_value()) {
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
      .size_bytes = size->get(),
    });
    return true;
  }

  auto BuildPackagingSummary(const ImportReport& report,
    const std::optional<LooseCookedWriteResult>& write_result)
    -> ImportPackagingSummary
  {
    ImportPackagingSummary summary {};
    summary.outputs_written = static_cast<uint32_t>(report.outputs.size());
    summary.index_written = write_result.has_value();

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
    }

    return summary;
  }

  //! The bytes of the file `path`; empty when it cannot be read.
  auto ReadBinaryFile(const cooked::CookerStorage& storage,
    const std::filesystem::path& path) -> std::vector<std::byte>
  {
    auto bytes
      = cooked::ReadWholeFile(storage, path, cooked::kMaxMetadataBytes);
    return bytes ? std::move(bytes).value() : std::vector<std::byte> {};
  }

  struct SceneScriptingCounts {
    uint32_t components = 0;
    uint32_t slots = 0;
    uint32_t parameters = 0;
  };

  auto ReadSceneScriptingCounts(const cooked::CookerStorage& storage,
    const std::filesystem::path& descriptor_path, const data::AssetKey& key)
    -> SceneScriptingCounts
  {
    const auto bytes = ReadBinaryFile(storage, descriptor_path);
    if (bytes.empty()) {
      return {};
    }

    try {
      auto scene = data::SceneAsset(key, bytes);
      const auto components
        = scene.GetComponents<data::pak::scripting::ScriptingComponentRecord>();
      SceneScriptingCounts result;
      result.components = static_cast<uint32_t>(components.size());
      for (const auto& component : components) {
        result.slots += component.slot_count;
        for (const auto& slot : scene.ReadScriptSlots(
               component.slot_start_index, component.slot_count)) {
          result.parameters += slot.params_count;
        }
      }
      return result;
    } catch (...) {
      return {};
    }
  }

  //! The file system of `registry`, which every session of a service shares.
  auto StorageOf(const observer_ptr<LooseCookedIndexRegistry> registry)
    -> observer_ptr<cooked::ImportStorage>
  {
    CHECK_NOTNULL_F(
      registry, "ImportSession requires a LooseCookedIndexRegistry");
    return registry->Storage();
  }

  auto BuildDiagnosticKey(const ImportDiagnostic& diagnostic) -> std::string
  {
    auto key = std::string {};
    key.reserve(diagnostic.code.size() + diagnostic.message.size()
      + diagnostic.source_path.size() + diagnostic.object_path.size()
      + diagnostic.json_pointer.size() + 16U);
    key.append(std::to_string(static_cast<uint32_t>(diagnostic.severity)));
    key.push_back('\x1F');
    key.append(diagnostic.code);
    key.push_back('\x1F');
    key.append(diagnostic.message);
    key.push_back('\x1F');
    key.append(diagnostic.source_path);
    key.push_back('\x1F');
    key.append(diagnostic.object_path);
    key.push_back('\x1F');
    key.append(diagnostic.json_pointer);
    return key;
  }

} // namespace

ImportSession::ImportSession(const ImportRequest& request,
  observer_ptr<IAsyncFileReader> file_reader,
  observer_ptr<serio::IoDriver> driver, observer_ptr<co::Nursery> writes,
  observer_ptr<co::ThreadPool> thread_pool,
  observer_ptr<ResourceTableRegistry> table_registry,
  observer_ptr<LooseCookedIndexRegistry> index_registry,
  observer_ptr<IAsyncFileReader> cooked_reader)
  : request_(request)
  , file_reader_(file_reader)
  , cooked_reader_(cooked_reader ? cooked_reader : file_reader)
  , driver_(driver)
  , writes_(writes)
  , thread_pool_(thread_pool)
  , table_registry_(table_registry)
  , index_registry_(index_registry)
  , storage_(StorageOf(index_registry))
  , cooked_root_(request.ResolveCookedRoot())
{
  DLOG_F(INFO, "Session created for: {}", request_.source_path.string());

  DCHECK_F(driver_ != nullptr, "ImportSession requires an IO driver");
  DCHECK_F(index_registry_ != nullptr,
    "ImportSession requires a LooseCookedIndexRegistry");
  DCHECK_F(table_registry_ != nullptr,
    "ImportSession requires a ResourceTableRegistry");

  // The index session takes the root's writer lease and drops what no
  // commit records, so the tables load what the latest commit lists.
  index_participation_.emplace(
    index_registry_->BeginSession(cooked_root_, request_.source_key));
  try {
    table_participation_.emplace(table_registry_->BeginSession(cooked_root_));
  } catch (...) {
    index_registry_->AbortSession(*index_participation_);
    throw;
  }
}

ImportSession::~ImportSession() { DLOG_F(INFO, "Session destroyed"); }

auto ImportSession::DrainAndRetire() -> co::Co<>
{
  const auto started = std::chrono::steady_clock::now();
  const auto timing = ScopeGuard([this, started] noexcept -> void {
    finalize_duration_ += std::chrono::duration_cast<std::chrono::microseconds>(
      std::chrono::steady_clock::now() - started);
  });
  std::exception_ptr failure;
  try {
    if (index_participation_ && index_participation_->IsActive()) {
      index_registry_->AbortSession(*index_participation_);
    }
  } catch (...) {
    if (!failure) {
      failure = std::current_exception();
    }
  }
  try {
    if (table_participation_ && table_participation_->IsActive()) {
      table_registry_->AbortSession(*table_participation_);
    }
  } catch (...) {
    if (!failure) {
      failure = std::current_exception();
    }
  }
  if (failure) {
    std::rethrow_exception(failure);
  }
  co_return;
}

auto ImportSession::Request() const noexcept -> const ImportRequest&
{
  return request_;
}

auto ImportSession::CookedRoot() const noexcept -> const std::filesystem::path&
{
  return cooked_root_;
}

auto ImportSession::FileReader() const noexcept
  -> observer_ptr<IAsyncFileReader>
{
  return file_reader_;
}

auto ImportSession::Driver() const noexcept -> observer_ptr<serio::IoDriver>
{
  return driver_;
}

auto ImportSession::Storage() const noexcept
  -> observer_ptr<cooked::ImportStorage>
{
  return storage_;
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
    CHECK_NOTNULL_F(writes_, "ImportSession needs a nursery to emit");
    DCHECK_F(table_registry_ != nullptr,
      "ImportSession requires a ResourceTableRegistry for texture emission");
    auto& aggregator = table_registry_->TextureAggregator(cooked_root_);
    TextureEmitter::Config config {};
    config.cooked_root = cooked_root_;
    texture_emitter_.emplace(std::make_unique<import::TextureEmitter>(
      *writes_, aggregator, std::move(config)));
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
    CHECK_NOTNULL_F(writes_, "ImportSession needs a nursery to emit");
    DCHECK_F(table_registry_ != nullptr,
      "ImportSession requires a ResourceTableRegistry for buffer emission");
    auto& aggregator = table_registry_->BufferAggregator(cooked_root_);
    buffer_emitter_.emplace(std::make_unique<import::BufferEmitter>(
      *writes_, aggregator, cooked_root_));
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
    CHECK_NOTNULL_F(writes_, "ImportSession needs a nursery to emit");
    DCHECK_F(table_registry_ != nullptr,
      "ImportSession requires a ResourceTableRegistry for physics resource "
      "emission");
    auto& aggregator = table_registry_->PhysicsAggregator(cooked_root_);
    physics_resource_emitter_.emplace(
      std::make_unique<import::PhysicsResourceEmitter>(
        *writes_, aggregator, cooked_root_));
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
    CHECK_NOTNULL_F(writes_, "ImportSession needs a nursery to emit");
    asset_emitter_.emplace(std::make_unique<import::AssetEmitter>(
      *storage_, *driver_, *writes_, cooked_root_));
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
    CHECK_NOTNULL_F(writes_, "ImportSession needs a nursery to emit");
    resource_descriptor_emitter_.emplace(
      std::make_unique<import::ResourceDescriptorEmitter>(
        *storage_, *driver_, *writes_, cooked_root_));
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

auto ImportSession::FinalizeDuration() const noexcept
  -> std::chrono::microseconds
{
  return finalize_duration_;
}

auto ImportSession::AddSurfaceProvenance(SurfaceGeometryProvenance geometry)
  -> void
{
  const std::scoped_lock lock(surface_provenance_mutex_);
  surface_provenance_.push_back(std::move(geometry));
}

auto ImportSession::SetPublishedNodes(ModelNodePublication nodes) -> void
{
  const std::scoped_lock lock(surface_provenance_mutex_);
  published_nodes_ = std::move(nodes);
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

// A sidecar completes its target scene, so the scene's source produces what it
// emits: the cooked root's own record of the scene, or else the scene in flight
// or in a context root, last root first as the sidecar resolves it. Every other
// job's source produces its outputs.
auto ImportSession::Producer() const -> LooseCookedProducer
{
  auto target = std::string {};
  if (request_.options.scripting.import_kind
    == ScriptingImportKind::kScriptingSidecar) {
    target = request_.options.scripting.target_scene_virtual_path;
  } else if (request_.physics.has_value()) {
    target = request_.physics->target_scene_virtual_path;
  } else {
    return { .source = request_.source_mount_path, .completes = {} };
  }

  for (const auto& scene : request_.inflight_scene_contexts) {
    if (scene.virtual_path == target) {
      return { .source = scene.source_path, .completes = target };
    }
  }
  const auto context_roots = internal::ContextCookedRoots(request_);
  for (const auto& root : std::views::reverse(context_roots)) {
    const auto index_path = root / loose_layout::kIndexFileName;
    if (storage_->KindOf(index_path) != cooked::PathKind::kFile) {
      continue;
    }
    auto bytes
      = cooked::ReadWholeFile(*storage_, index_path, cooked::kMaxMetadataBytes);
    if (!bytes) {
      throw std::system_error(
        bytes.error(), "Failed to read the index of '" + root.string() + "'");
    }
    const auto index = lc::LooseCookedIndex::LoadFromBytes(std::move(*bytes));
    if (const auto key = index.FindAssetKeyByVirtualPath(target)) {
      if (const auto source = index.FindSourcePath(*key)) {
        return { .source = std::string(*source), .completes = target };
      }
    }
  }
  return { .source = {}, .completes = target };
}

auto ImportSession::Finalize() -> co::Co<ImportReport>
{
  const auto started = std::chrono::steady_clock::now();
  const auto timing = ScopeGuard([this, started] noexcept -> void {
    finalize_duration_ += std::chrono::duration_cast<std::chrono::microseconds>(
      std::chrono::steady_clock::now() - started);
  });
  if (!table_participation_.has_value() || !index_participation_.has_value()) {
    throw std::logic_error(
      "Import finalization requires both registry participations");
  }
  auto& table_participation = *table_participation_;
  auto& index_participation = *index_participation_;
  DLOG_F(INFO, "Finalize starting");

  if (texture_emitter_.has_value()) {
    const auto ok = co_await (*texture_emitter_)->Finalize();
    if (!ok) {
      AddDiagnostic({
        .severity = ImportSeverity::kError,
        .code = "import.texture_emitter_finalize_failed",
        .message = ok.error().Message(),
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
        .message = ok.error().Message(),
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
        .message = ok.error().Message(),
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
        .message = ok.error().Message(),
        .source_path = request_.source_path.string(),
        .object_path = {},
      });
    }
  }

  // The digests the emitter recorded describe the files only once they are
  // written.
  auto resource_descriptors_written = true;
  if (resource_descriptor_emitter_.has_value()) {
    const auto ok = co_await (*resource_descriptor_emitter_)->Finalize();
    resource_descriptors_written = ok.has_value();
    if (!ok) {
      AddDiagnostic({
        .severity = ImportSeverity::kError,
        .code = "import.resource_descriptor_emitter_finalize_failed",
        .message = ok.error().Message(),
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

  const auto tables = co_await co::NonCancellable(
    table_registry_->EndSession(table_participation));
  if (!tables.ok) {
    AddDiagnostic({
      .severity = ImportSeverity::kError,
      .code = "import.resource_table_finalize_failed",
      .message = "Resource table finalization failed",
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
    DLOG_F(INFO,
      "Registering index entries: textures={} buffers={} assets={} "
      "cooked_root='{}'",
      texture_count, buffer_count, asset_count, cooked_root_.string());

    bool output_missing = false;

    // A data file and the table of its rows register together; the table
    // has its content name, which the session's table finalization gave it.
    const auto register_resource = [&](const ResourceFiles& files) -> void {
      if (storage_->KindOf(cooked_root_ / files.data_relpath)
        == cooked::PathKind::kFile) {
        index_registry_->RegisterExternalDataFile(
          cooked_root_, files.data, files.data_relpath);
        output_missing
          |= !AppendOutputRecord(*storage_, report.outputs, report.diagnostics,
            cooked_root_, files.data_relpath, request_.source_path.string());
      }
      if (const auto* const table = WrittenTableOf(tables, files.table)) {
        index_registry_->RegisterExternalFile(
          cooked_root_, files.table, table->relpath, table->digest);
        output_missing
          |= !AppendOutputRecord(*storage_, report.outputs, report.diagnostics,
            cooked_root_, table->relpath, request_.source_path.string());
      }
    };
    if (texture_count > 0) {
      register_resource({
        .table = FileKind::kTexturesTable,
        .data = FileKind::kTexturesData,
        .data_relpath = loose_layout::kTexturesData,
      });
    }
    if (buffer_count > 0) {
      register_resource({
        .table = FileKind::kBuffersTable,
        .data = FileKind::kBuffersData,
        .data_relpath = loose_layout::kBuffersData,
      });
    }
    register_resource({
      .table = FileKind::kPhysicsTable,
      .data = FileKind::kPhysicsData,
      .data_relpath = loose_layout::kPhysicsData,
    });

    // Only the entries the index records a producer for need one.
    const auto producer = Producer();
    const auto recorded_producer = [&producer] -> const LooseCookedProducer& {
      if (producer.source.empty() && producer.completes.empty()) {
        throw LooseCookedSourceOutsideMount(
          "no recorded source: the job's source lies in no source mount");
      }
      return producer;
    };
    if (asset_emitter_.has_value()) {
      const auto& records = (*asset_emitter_)->Records();
      report.outputs.reserve(report.outputs.size() + records.size());
      // What each asset requires of other mounts, from the cooked roots its
      // job reads; read once, for the assets that reference other keys.
      auto mounts
        = std::optional<std::vector<internal::MountedCookedSource>> {};
      for (const auto& rec : records) {
        auto requirements
          = std::vector<data::loose_cooked::IndexedRequirement> {};
        if (!rec.references.Keys().empty()) {
          if (!mounts.has_value()) {
            mounts = internal::LoadMountedCookedSources(
              *this, request_, "import.requirements.");
          }
          const auto own_mount = content::MountRootOf(rec.virtual_path);
          requirements = internal::RequirementsOf(*mounts,
            own_mount.empty() ? own_mount : own_mount.substr(1),
            rec.references);
        }
        index_registry_->RegisterProducedAssetDescriptor(cooked_root_, rec.key,
          rec.asset_type, rec.virtual_path, rec.descriptor_relpath,
          LooseCookedFileDigest {
            .size = rec.descriptor_size,
            .sha256 = rec.descriptor_sha256,
          },
          rec.references, recorded_producer(), requirements);
        report.outputs.push_back({
          .path = rec.descriptor_relpath,
          .size_bytes = rec.descriptor_size,
        });
        report.products.push_back({
          .virtual_path = rec.virtual_path,
          .descriptor_relpath = rec.descriptor_relpath,
          .key = rec.key,
          .asset_type = rec.asset_type,
        });
      }
    }

    // Descriptors the emitter failed to write are not indexed; the failure
    // is already diagnosed.
    if (resource_descriptor_emitter_.has_value()
      && resource_descriptors_written) {
      const auto records = (*resource_descriptor_emitter_)->Records();
      report.outputs.reserve(report.outputs.size() + records.size());
      for (const auto& rec : records) {
        const auto produced = LooseCookedFileDigest {
          .size = rec.size_bytes,
          .sha256 = rec.sha256,
        };
        if (rec.kind == data::loose_cooked::FileKind::kAuxiliary) {
          index_registry_->RegisterExternalAuxiliaryFile(cooked_root_,
            rec.relpath, rec.virtual_path, recorded_producer(), produced);
        } else {
          index_registry_->RegisterExternalTextureDescriptor(cooked_root_,
            rec.relpath, rec.virtual_path, recorded_producer(), produced);
          report.products.push_back({
            .virtual_path = rec.virtual_path,
            .descriptor_relpath = rec.relpath,
          });
        }
        output_missing
          |= !AppendOutputRecord(*storage_, report.outputs, report.diagnostics,
            cooked_root_, rec.relpath, request_.source_path.string());
      }
    }

    // A job that completed without errors re-cooked its source in full under
    // its identity, so what it no longer produces there retires. A sidecar
    // completes one asset of its scene's source and re-cooks nothing of it.
    if (auto identity_relpath = std::string {}; producer.completes.empty()
      && !producer.source.empty() && !HasErrors()
      && internal::TryVirtualPathToRelPath(
        request_.asset_virtual_path, identity_relpath)) {
      index_registry_->RetireUnwrittenOutputs(cooked_root_,
        LooseCookedRecook {
          .source = producer.source,
          .identity = request_.asset_virtual_path,
          .identity_relpath = std::move(identity_relpath),
        });
    }

    const auto publication
      = co_await index_registry_->EndSession(index_participation);
    const auto& write_result = publication.write_result;
    report.source_key = publication.source_key;
    if (write_result.has_value()) {
      LOG_F(INFO, "Index write completed: assets={} files={} cooked_root='{}'",
        write_result->assets.size(), write_result->files.size(),
        cooked_root_.string());
      output_missing |= !AppendOutputRecord(*storage_, report.outputs,
        report.diagnostics, cooked_root_, loose_layout::kIndexFileName,
        request_.source_path.string());
    } else {
      DLOG_F(INFO, "Index published by another session for cooked_root='{}'",
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
      auto component_count = uint64_t { 0 };
      if (asset_emitter_.has_value()) {
        for (const auto& rec : (*asset_emitter_)->Records()) {
          if (rec.asset_type != data::AssetType::kScene) {
            continue;
          }
          const auto counts = ReadSceneScriptingCounts(*storage_,
            cooked_root_ / std::filesystem::path(rec.descriptor_relpath),
            rec.key);
          component_count += counts.components;
          report.script_slots_written += counts.slots;
          report.script_params_written += counts.parameters;
        }
      }
      if (component_count > std::numeric_limits<uint32_t>::max()) {
        report.scripting_components_written
          = std::numeric_limits<uint32_t>::max();
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
    if (report.success && request_.surface_provenance) {
      if (surface_source_processed_) {
        const SurfaceProvenance provenance(
          request_.surface_provenance->SourceIdentity(),
          std::move(surface_provenance_));
        report.surface_provenance_json = provenance.Serialize();
      } else {
        report.surface_provenance_json
          = request_.surface_provenance->Serialize();
      }
      if (request_.model_import) {
        const std::scoped_lock lock(surface_provenance_mutex_);
        report.published_nodes = std::move(published_nodes_.published);
        report.new_nodes = std::move(published_nodes_.added);
        report.vanished_nodes = std::move(published_nodes_.vanished);
      }
    }
    report.packaging = BuildPackagingSummary(report, write_result);

    LOG_F(INFO,
      "Packaging summary: success={} outputs={} index_written={} "
      "diagnostics(info/warn/error)={}/{}/{}",
      report.success, report.packaging.outputs_written,
      report.packaging.index_written, report.packaging.diagnostics_info,
      report.packaging.diagnostics_warning, report.packaging.diagnostics_error);

    DLOG_F(INFO,
      "Finalize complete: {} materials, {} geometry, {} scenes, {} scripts, "
      "{} scripting components, {} slots, {} params",
      report.materials_written, report.geometry_written, report.scenes_written,
      report.scripts_written, report.scripting_components_written,
      report.script_slots_written, report.script_params_written);
  } catch (const LooseCookedSourceOutsideMount& ex) {
    report.success = false;
    report.diagnostics.push_back({
      .severity = ImportSeverity::kError,
      .code = std::string { mount_codes::kSourceOutsideMount.id },
      .message = ex.what(),
      .source_path = request_.source_path.string(),
      .object_path = {},
    });
    LOG_F(ERROR, "Refused index entries: {}", ex.what());
  } catch (const LooseCookedOutputMountMixed& ex) {
    report.success = false;
    report.diagnostics.push_back({
      .severity = ImportSeverity::kError,
      .code = "import.output_mount_mixed",
      .message = ex.what(),
      .source_path = request_.source_path.string(),
      .object_path = {},
    });
    LOG_F(ERROR, "Refused index entry: {}", ex.what());
  } catch (const LooseCookedIdentityCollision& ex) {
    report.success = false;
    report.diagnostics.push_back({
      .severity = ImportSeverity::kError,
      .code = "import.identity_collision",
      .message = ex.what(),
      .source_path = request_.source_path.string(),
      .object_path = {},
    });
    LOG_F(ERROR, "Refused index entry: {}", ex.what());
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
