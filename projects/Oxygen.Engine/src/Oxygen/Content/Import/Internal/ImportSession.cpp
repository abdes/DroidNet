//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <fstream>
#include <optional>

#include <Oxygen/Base/Logging.h>
#include <Oxygen/Content/Import/IAsyncFileWriter.h>
#include <Oxygen/Content/Import/Internal/Emitters/AssetEmitter.h>
#include <Oxygen/Content/Import/Internal/Emitters/BufferEmitter.h>
#include <Oxygen/Content/Import/Internal/Emitters/TextureEmitter.h>
#include <Oxygen/Content/Import/Internal/ImportSession.h>
#include <Oxygen/Content/Import/Internal/LooseCookedIndexRegistry.h>
#include <Oxygen/Content/Import/Internal/ResourceTableRegistry.h>
#include <Oxygen/Data/LooseCookedIndexFormat.h>
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
    const data::loose_cooked::v1::FileKind kind,
    const std::filesystem::path& cooked_root, std::string_view relpath) -> void
  {
    const auto path = cooked_root / std::filesystem::path(relpath);
    EnsureExternalFileExists(path);
    registry.RegisterExternalFile(cooked_root, kind, relpath);
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
  DLOG_F(INFO, "Session options: with_content_hashing={}",
    request_.options.with_content_hashing);

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
    config.with_content_hashing = request_.options.with_content_hashing;
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
    buffer_emitter_.emplace(std::make_unique<import::BufferEmitter>(
      *file_writer_, aggregator, request_.loose_cooked_layout, cooked_root_));
  }
  return **buffer_emitter_;
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

auto ImportSession::AddDiagnostic(ImportDiagnostic diagnostic) -> void
{
  const bool is_error = (diagnostic.severity == ImportSeverity::kError);

  {
    std::scoped_lock lock(diagnostics_mutex_);
    diagnostics_.push_back(std::move(diagnostic));
    if (is_error) {
      has_errors_ = true;
    }
  }

  // Log based on severity
  const auto& added = diagnostics_.back();
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
    });
  }

  // Build the report
  const bool had_errors = HasErrors();
  ImportReport report {
    .cooked_root = cooked_root_,
    .source_key = {},
    .diagnostics = Diagnostics(),
    .materials_written = 0,
    .geometry_written = 0,
    .scenes_written = 0,
    .success = false,
  };

  // Always attempt to write the index to keep file sizes in sync, even if
  // diagnostics reported errors. This prevents stale index metadata from
  // invalidating previously cooked content.
  try {
    using data::loose_cooked::v1::FileKind;

    const auto& layout = request_.loose_cooked_layout;

    DLOG_F(INFO,
      "Registering index entries: textures={} buffers={} assets={} "
      "cooked_root='{}'",
      texture_count, buffer_count, asset_count, cooked_root_.string());

    if (texture_count > 0) {
      index_registry_->RegisterExternalFile(
        cooked_root_, FileKind::kTexturesData, layout.TexturesDataRelPath());

      RegisterExternalTable(*index_registry_, FileKind::kTexturesTable,
        cooked_root_, layout.TexturesTableRelPath());
    }

    if (buffer_count > 0) {
      index_registry_->RegisterExternalFile(
        cooked_root_, FileKind::kBuffersData, layout.BuffersDataRelPath());

      RegisterExternalTable(*index_registry_, FileKind::kBuffersTable,
        cooked_root_, layout.BuffersTableRelPath());
    }

    if (asset_emitter_.has_value()) {
      for (const auto& rec : (*asset_emitter_)->Records()) {
        index_registry_->RegisterExternalAssetDescriptor(cooked_root_, rec.key,
          rec.asset_type, rec.virtual_path, rec.descriptor_relpath,
          rec.descriptor_size, rec.descriptor_sha256);
      }
    }

    const auto write_result = index_registry_->EndSession(cooked_root_);
    if (write_result.has_value()) {
      LOG_F(INFO, "Index write completed: assets={} files={} cooked_root='{}'",
        write_result->assets.size(), write_result->files.size(),
        cooked_root_.string());
      report.source_key = write_result->source_key;
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
        default:
          break;
        }
      }
    }

    if (had_errors) {
      report.diagnostics.push_back({
        .severity = ImportSeverity::kWarning,
        .code = "import.index_written_with_errors",
        .message = "Index written despite import errors",
        .source_path = request_.source_path.string(),
      });
    }

    report.success = !had_errors;

    DLOG_F(INFO, "Finalize complete: {} materials, {} geometry, {} scenes",
      report.materials_written, report.geometry_written, report.scenes_written);
  } catch (const std::exception& ex) {
    report.success = false;
    report.diagnostics.push_back({
      .severity = ImportSeverity::kError,
      .code = "import.index_write_failed",
      .message = ex.what(),
      .source_path = request_.source_path.string(),
    });
    LOG_F(ERROR, "Failed to write index: {}", ex.what());
  }

  co_return report;
}

} // namespace oxygen::content::import
