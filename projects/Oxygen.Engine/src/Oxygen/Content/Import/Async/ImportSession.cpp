//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <fstream>

#include <Oxygen/Base/Logging.h>
#include <Oxygen/Content/Import/Async/Emitters/AssetEmitter.h>
#include <Oxygen/Content/Import/Async/Emitters/BufferEmitter.h>
#include <Oxygen/Content/Import/Async/Emitters/TextureEmitter.h>
#include <Oxygen/Content/Import/Async/IAsyncFileWriter.h>
#include <Oxygen/Content/Import/Async/ImportSession.h>
#include <Oxygen/Content/Import/Async/ResourceTableRegistry.h>
#include <Oxygen/Data/LooseCookedIndexFormat.h>
#include <Oxygen/OxCo/Semaphore.h>

namespace oxygen::content::import {

namespace {

  auto FinalizeGate() -> oxygen::co::Semaphore&
  {
    static oxygen::co::Semaphore gate(1);
    return gate;
  }

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

  auto RegisterExternalTable(LooseCookedWriter& writer,
    const data::loose_cooked::v1::FileKind kind,
    const std::filesystem::path& cooked_root, std::string_view relpath) -> void
  {
    const auto path = cooked_root / std::filesystem::path(relpath);
    EnsureExternalFileExists(path);
    writer.RegisterExternalFile(kind, relpath);
  }

} // namespace

ImportSession::ImportSession(const ImportRequest& request,
  oxygen::observer_ptr<IAsyncFileReader> file_reader,
  oxygen::observer_ptr<IAsyncFileWriter> file_writer,
  oxygen::observer_ptr<co::ThreadPool> thread_pool,
  oxygen::observer_ptr<ResourceTableRegistry> table_registry)
  : request_(request)
  , file_reader_(file_reader)
  , file_writer_(file_writer)
  , thread_pool_(thread_pool)
  , table_registry_(table_registry)
  , cooked_root_(
      request.cooked_root.value_or(request.source_path.parent_path()))
  , cooked_writer_(cooked_root_)
{
  DLOG_F(INFO, "ImportSession created for: {}", request_.source_path.string());

  DCHECK_F(file_writer_ != nullptr,
    "ImportSession requires a valid async file writer");

  // Set source key if provided in request
  if (request_.source_key.has_value()) {
    cooked_writer_.SetSourceKey(request_.source_key);
  }
}

ImportSession::~ImportSession() { DLOG_F(INFO, "ImportSession destroyed"); }

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
  -> oxygen::observer_ptr<IAsyncFileReader>
{
  return file_reader_;
}

auto ImportSession::FileWriter() const noexcept
  -> oxygen::observer_ptr<IAsyncFileWriter>
{
  return file_writer_;
}

auto ImportSession::ThreadPool() const noexcept
  -> oxygen::observer_ptr<co::ThreadPool>
{
  return thread_pool_;
}

/*!
 Get (and lazily create) the texture emitter for this session.

 @warning This method is not thread-safe. It must be called from the importer
  thread only.
*/
auto ImportSession::TextureEmitter() -> oxygen::content::import::TextureEmitter&
{
  if (!texture_emitter_.has_value()) {
    DCHECK_F(table_registry_ != nullptr,
      "ImportSession requires a ResourceTableRegistry for texture emission");
    auto& aggregator = table_registry_->TextureAggregator(
      cooked_root_, request_.loose_cooked_layout);
    texture_emitter_.emplace(
      std::make_unique<oxygen::content::import::TextureEmitter>(
        *file_writer_, aggregator, request_.loose_cooked_layout, cooked_root_));
  }
  return **texture_emitter_;
}

/*!
 Get (and lazily create) the buffer emitter for this session.

 @warning This method is not thread-safe. It must be called from the importer
  thread only.
*/
auto ImportSession::BufferEmitter() -> oxygen::content::import::BufferEmitter&
{
  if (!buffer_emitter_.has_value()) {
    DCHECK_F(table_registry_ != nullptr,
      "ImportSession requires a ResourceTableRegistry for buffer emission");
    auto& aggregator = table_registry_->BufferAggregator(
      cooked_root_, request_.loose_cooked_layout);
    buffer_emitter_.emplace(
      std::make_unique<oxygen::content::import::BufferEmitter>(
        *file_writer_, aggregator, request_.loose_cooked_layout, cooked_root_));
  }
  return **buffer_emitter_;
}

/*!
 Get (and lazily create) the asset emitter for this session.

 @warning This method is not thread-safe. It must be called from the importer
  thread only.
*/
auto ImportSession::AssetEmitter() -> oxygen::content::import::AssetEmitter&
{
  if (!asset_emitter_.has_value()) {
    asset_emitter_.emplace(
      std::make_unique<oxygen::content::import::AssetEmitter>(
        *file_writer_, request_.loose_cooked_layout, cooked_root_));
  }
  return **asset_emitter_;
}

auto ImportSession::AddDiagnostic(ImportDiagnostic diagnostic) -> void
{
  const bool is_error = (diagnostic.severity == ImportSeverity::kError);

  {
    std::lock_guard lock(diagnostics_mutex_);
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
  std::lock_guard lock(diagnostics_mutex_);
  return diagnostics_;
}

auto ImportSession::HasErrors() const noexcept -> bool
{
  std::lock_guard lock(diagnostics_mutex_);
  return has_errors_;
}

auto ImportSession::Finalize() -> co::Co<ImportReport>
{
  DLOG_F(INFO, "ImportSession::Finalize() starting");

  auto& gate = FinalizeGate();
  auto guard = co_await gate.Lock();

  if (texture_emitter_.has_value()) {
    const auto ok = co_await (**texture_emitter_).Finalize();
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
    const auto ok = co_await (**buffer_emitter_).Finalize();
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
    const auto ok = co_await (**asset_emitter_).Finalize();
    if (!ok) {
      AddDiagnostic({
        .severity = ImportSeverity::kError,
        .code = "import.asset_emitter_finalize_failed",
        .message = "Asset emitter finalization failed",
        .source_path = request_.source_path.string(),
      });
    }
  }

  if (table_registry_) {
    const auto ok = co_await table_registry_->FinalizeAll();
    if (!ok) {
      AddDiagnostic({
        .severity = ImportSeverity::kError,
        .code = "import.resource_table_finalize_failed",
        .message = "Resource table finalization failed",
        .source_path = request_.source_path.string(),
      });
    }
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
  ImportReport report {
    .cooked_root = cooked_root_,
    .source_key = {},
    .diagnostics = Diagnostics(),
    .materials_written = 0,
    .geometry_written = 0,
    .scenes_written = 0,
    .success = !HasErrors(),
  };

  // Only write index if no errors occurred
  if (report.success) {
    try {
      using oxygen::data::loose_cooked::v1::FileKind;

      const auto& layout = request_.loose_cooked_layout;
      if (texture_emitter_.has_value() && (**texture_emitter_).Count() > 0) {
        cooked_writer_.RegisterExternalFile(
          FileKind::kTexturesData, layout.TexturesDataRelPath());

        RegisterExternalTable(cooked_writer_, FileKind::kTexturesTable,
          cooked_root_, layout.TexturesTableRelPath());
      }

      if (buffer_emitter_.has_value() && (**buffer_emitter_).Count() > 0) {
        cooked_writer_.RegisterExternalFile(
          FileKind::kBuffersData, layout.BuffersDataRelPath());

        RegisterExternalTable(cooked_writer_, FileKind::kBuffersTable,
          cooked_root_, layout.BuffersTableRelPath());
      }

      if (asset_emitter_.has_value()) {
        for (const auto& rec : (**asset_emitter_).Records()) {
          cooked_writer_.RegisterExternalAssetDescriptor(rec.key,
            rec.asset_type, rec.virtual_path, rec.descriptor_relpath,
            rec.descriptor_size, rec.descriptor_sha256);
        }
      }

      auto write_result = cooked_writer_.Finish();
      report.source_key = write_result.source_key;

      // Count assets by type
      for (const auto& asset : write_result.assets) {
        switch (asset.asset_type) {
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

      DLOG_F(INFO,
        "ImportSession::Finalize() complete: {} materials, {} "
        "geometry, {} scenes",
        report.materials_written, report.geometry_written,
        report.scenes_written);
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
  } else {
    DLOG_F(
      WARNING, "ImportSession::Finalize() skipping index write due to errors");
  }

  co_return report;
}

} // namespace oxygen::content::import
