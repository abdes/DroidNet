//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <algorithm>
#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <limits>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include <Oxygen/Base/Logging.h>
#include <Oxygen/Base/Sha256.h>
#include <Oxygen/Cooker/Import/IAsyncFileReader.h>
#include <Oxygen/Cooker/Import/IAsyncFileWriter.h>
#include <Oxygen/Cooker/Import/ImportDiagnostics.h>
#include <Oxygen/Cooker/Import/Internal/Emitters/AssetEmitter.h>
#include <Oxygen/Cooker/Import/Internal/ImportSession.h>
#include <Oxygen/Cooker/Import/Internal/Jobs/ScriptImportJob.h>
#include <Oxygen/Cooker/Import/Internal/LooseCookedIndexRegistry.h>
#include <Oxygen/Cooker/Import/Internal/Utils/AssetKeyUtils.h>
#include <Oxygen/Cooker/Import/Internal/Utils/ImportSettingsUtils.h>
#include <Oxygen/Data/LooseCookedIndexFormat.h>
#include <Oxygen/Data/PakFormat.h>

namespace oxygen::content::import::detail {

namespace {

  constexpr auto kScriptDescriptorExtension = std::string_view { ".oscript" };
  constexpr auto kScriptsTableFileName = std::string_view { "scripts.table" };
  constexpr auto kScriptsDataFileName = std::string_view { "scripts.data" };
  constexpr auto kScriptContentHashBytes = size_t { 8 };
  constexpr auto kByteShiftBits = size_t { 8 };

  auto JoinRelativePath(
    const std::string_view base, const std::string_view child) -> std::string
  {
    if (base.empty()) {
      return std::string { child };
    }
    if (child.empty()) {
      return std::string { base };
    }
    return std::string { base } + "/" + std::string { child };
  }

  auto EnsureLeadingSlash(const std::string_view path) -> std::string
  {
    if (path.starts_with('/')) {
      return std::string { path };
    }
    return std::string { "/" } + std::string { path };
  }

  auto JoinVirtualPath(const std::string_view root, const std::string_view leaf)
    -> std::string
  {
    auto rooted = EnsureLeadingSlash(root);
    if (rooted == "/") {
      return EnsureLeadingSlash(leaf);
    }
    if (leaf.empty()) {
      return rooted;
    }
    if (leaf.front() == '/') {
      return rooted + std::string { leaf };
    }
    return rooted + "/" + std::string { leaf };
  }

  auto DeriveScriptName(const std::filesystem::path& source_path) -> std::string
  {
    const auto stem = source_path.stem().string();
    if (!stem.empty()) {
      return stem;
    }
    return "Script";
  }

  auto BuildScriptDescriptorRelPath(
    const ImportRequest& request, std::string_view script_name) -> std::string
  {
    const auto leaf = std::string { script_name }
      + std::string { kScriptDescriptorExtension };
    const auto scripts_dir = JoinRelativePath(
      request.loose_cooked_layout.descriptors_dir, "Scripts");
    return JoinRelativePath(scripts_dir, leaf);
  }

  auto BuildScriptsTableRelPath(const ImportRequest& request) -> std::string
  {
    return JoinRelativePath(
      request.loose_cooked_layout.resources_dir, kScriptsTableFileName);
  }

  auto BuildScriptsDataRelPath(const ImportRequest& request) -> std::string
  {
    return JoinRelativePath(
      request.loose_cooked_layout.resources_dir, kScriptsDataFileName);
  }

  auto BuildExternalSourcePath(const std::filesystem::path& source_path)
    -> std::string
  {
    auto filename = source_path.filename().generic_string();
    if (!filename.empty()) {
      return filename;
    }
    return "script.luau";
  }

  template <size_t N>
  auto CopyNullTerminated(const std::string_view src, std::span<char, N> dst)
    -> bool
  {
    if (src.size() >= dst.size()) {
      return false;
    }
    std::ranges::fill(dst, '\0');
    std::ranges::copy(src, dst.begin());
    return true;
  }

  auto ComputeContentHash64(const std::span<const std::byte> bytes) -> uint64_t
  {
    const auto digest = base::ComputeSha256(bytes);
    auto hash = uint64_t { 0 };
    for (size_t i = 0; i < kScriptContentHashBytes; ++i) {
      hash |= static_cast<uint64_t>(digest.at(i)) << (i * kByteShiftBits);
    }
    return hash;
  }

  auto AddDiagnostic(ImportSession& session, const ImportRequest& request,
    const ImportSeverity severity, std::string code, std::string message)
    -> void
  {
    session.AddDiagnostic({
      .severity = severity,
      .code = std::move(code),
      .message = std::move(message),
      .source_path = request.source_path.string(),
    });
  }

} // namespace

auto ScriptImportJob::ExecuteAsync() -> co::Co<ImportReport>
{
  DLOG_F(INFO, "Starting script job: job_id={} path={}", JobId(),
    Request().source_path.string());

  const auto job_start = std::chrono::steady_clock::now();
  ImportTelemetry telemetry {};
  const auto MakeDuration
    = [](const std::chrono::steady_clock::time_point start,
        const std::chrono::steady_clock::time_point end)
    -> std::chrono::microseconds {
    return std::chrono::duration_cast<std::chrono::microseconds>(end - start);
  };
  const auto FinalizeWithTelemetry
    = [&](ImportSession& session) -> co::Co<ImportReport> {
    const auto finalize_start = std::chrono::steady_clock::now();
    auto report = co_await FinalizeSession(session);
    const auto finalize_end = std::chrono::steady_clock::now();
    telemetry.finalize_duration = MakeDuration(finalize_start, finalize_end);
    telemetry.total_duration = MakeDuration(job_start, finalize_end);
    telemetry.io_duration = session.IoDuration();
    telemetry.source_load_duration = session.SourceLoadDuration();
    telemetry.decode_duration = session.DecodeDuration();
    telemetry.load_duration
      = session.SourceLoadDuration() + session.LoadDuration();
    telemetry.cook_duration = session.CookDuration();
    telemetry.emit_duration = session.EmitDuration();
    report.telemetry = telemetry;
    co_return report;
  };

  EnsureCookedRoot();

  ImportSession session(Request(), FileReader(), FileWriter(), ThreadPool(),
    TableRegistry(), IndexRegistry());

  ReportPhaseProgress(ImportPhase::kLoading, 0.0F, "Loading script source...");
  const auto load_start = std::chrono::steady_clock::now();
  const auto source = co_await LoadSource(session);
  const auto load_end = std::chrono::steady_clock::now();
  session.AddSourceLoadDuration(MakeDuration(load_start, load_end));
  if (!source.success) {
    ReportPhaseProgress(ImportPhase::kFailed, 1.0F, "Script load failed");
    co_return co_await FinalizeWithTelemetry(session);
  }

  ReportPhaseProgress(ImportPhase::kWorking, 0.5F, "Emitting script asset...");
  const auto emit_start = std::chrono::steady_clock::now();
  const auto emitted = co_await EmitScriptAsset(source, session);
  const auto emit_end = std::chrono::steady_clock::now();
  session.AddEmitDuration(MakeDuration(emit_start, emit_end));
  if (!emitted) {
    ReportPhaseProgress(ImportPhase::kFailed, 1.0F, "Script emit failed");
    co_return co_await FinalizeWithTelemetry(session);
  }

  auto report = co_await FinalizeWithTelemetry(session);
  ReportPhaseProgress(
    report.success ? ImportPhase::kComplete : ImportPhase::kFailed, 1.0F,
    report.success ? "Import complete" : "Import failed");
  co_return report;
}

auto ScriptImportJob::LoadSource(ImportSession& session) -> co::Co<LoadedSource>
{
  auto& req = Request();
  auto* const reader = FileReader().get();
  if (reader == nullptr) {
    AddDiagnostic(session, req, ImportSeverity::kError,
      "script.asset.reader_unavailable", "Async file reader is not available");
    co_return LoadedSource {
      .success = false,
    };
  }

  const auto read = co_await reader->ReadFile(req.source_path);
  if (!read.has_value()) {
    AddDiagnostic(session, req, ImportSeverity::kError,
      "script.asset.source_read_failed",
      "Failed to read script source: " + read.error().ToString());
    co_return LoadedSource {
      .success = false,
    };
  }

  co_return LoadedSource {
    .success = true,
    .bytes = read.value(),
  };
}

auto ScriptImportJob::EmitScriptAsset(
  const LoadedSource& source, ImportSession& session) -> co::Co<bool>
{
  using core::meta::scripting::ScriptCompileMode;
  using data::AssetType;
  using data::loose_cooked::FileKind;
  using data::pak::core::DataBlobSizeT;
  using data::pak::core::kNoResourceIndex;
  using data::pak::core::OffsetT;
  using data::pak::core::ResourceIndexT;
  using data::pak::scripting::ScriptAssetDesc;
  using data::pak::scripting::ScriptAssetFlags;
  using data::pak::scripting::ScriptCompression;
  using data::pak::scripting::ScriptEncoding;
  using data::pak::scripting::ScriptLanguage;
  using data::pak::scripting::ScriptResourceDesc;

  auto& req = Request();
  const auto& scripting = req.options.scripting;

  if (scripting.import_kind != ScriptingImportKind::kScriptAsset) {
    AddDiagnostic(session, req, ImportSeverity::kError,
      "script.request.invalid_import_kind",
      "Script import job requires options.scripting.import_kind=kScriptAsset");
    co_return false;
  }

  if (scripting.compile_scripts
    && scripting.script_storage == ScriptStorageMode::kExternal) {
    AddDiagnostic(session, req, ImportSeverity::kError,
      "script.request.invalid_option_combo",
      "compile_scripts=true is invalid with script_storage=external");
    co_return false;
  }

  if (scripting.compile_scripts) {
    const auto* const mode
      = (scripting.compile_mode == ScriptCompileMode::kDebug) ? "debug"
                                                              : "optimized";
    AddDiagnostic(session, req, ImportSeverity::kError,
      "script.asset.compiler_unavailable",
      "compile_scripts=true requested, but no script compiler is wired in "
      "Cooker/Import for mode='"
        + std::string { mode } + "'");
    co_return false;
  }

  const auto script_name = DeriveScriptName(req.source_path);
  const auto descriptor_relpath
    = BuildScriptDescriptorRelPath(req, script_name);
  const auto virtual_path = JoinVirtualPath(
    req.loose_cooked_layout.virtual_mount_root, descriptor_relpath);

  const auto key = [&]() -> data::AssetKey {
    switch (req.options.asset_key_policy) {
    case AssetKeyPolicy::kRandom:
      return util::MakeRandomAssetKey();
    case AssetKeyPolicy::kDeterministicFromVirtualPath:
      return util::MakeDeterministicAssetKey(virtual_path);
    }
    return util::MakeDeterministicAssetKey(virtual_path);
  }();

  ScriptAssetDesc desc {};
  desc.header.asset_type = static_cast<uint8_t>(AssetType::kScript);
  const auto name_span
    = std::span<char, sizeof(desc.header.name)>(desc.header.name);
  if (!CopyNullTerminated(script_name, name_span)) {
    AddDiagnostic(session, req, ImportSeverity::kError,
      "script.asset.name_too_long",
      "Derived script name exceeds ScriptAssetDesc::header.name capacity");
    co_return false;
  }

  desc.bytecode_resource_index = kNoResourceIndex;
  desc.source_resource_index = kNoResourceIndex;
  desc.flags = ScriptAssetFlags::kNone;

  if (scripting.script_storage == ScriptStorageMode::kEmbedded) {
    auto* const reader = FileReader().get();
    auto* const writer = FileWriter().get();
    auto* const index_registry = IndexRegistry().get();
    if (reader == nullptr || writer == nullptr || index_registry == nullptr) {
      AddDiagnostic(session, req, ImportSeverity::kError,
        "script.asset.io_unavailable",
        "Script embedded emission requires file reader/writer/index registry");
      co_return false;
    }

    const auto table_relpath = BuildScriptsTableRelPath(req);
    const auto data_relpath = BuildScriptsDataRelPath(req);
    const auto table_path = session.CookedRoot() / table_relpath;
    const auto data_path = session.CookedRoot() / data_relpath;

    const auto table_exists_result = co_await reader->Exists(table_path);
    if (!table_exists_result.has_value()) {
      AddDiagnostic(session, req, ImportSeverity::kError,
        "script.asset.scripts_table_exists_check_failed",
        "Failed checking scripts.table existence: "
          + table_exists_result.error().ToString());
      co_return false;
    }
    const auto data_exists_result = co_await reader->Exists(data_path);
    if (!data_exists_result.has_value()) {
      AddDiagnostic(session, req, ImportSeverity::kError,
        "script.asset.scripts_data_exists_check_failed",
        "Failed checking scripts.data existence: "
          + data_exists_result.error().ToString());
      co_return false;
    }

    const auto table_exists = table_exists_result.value();
    const auto data_exists = data_exists_result.value();
    if (table_exists != data_exists) {
      AddDiagnostic(session, req, ImportSeverity::kError,
        "script.asset.scripts_pair_mismatch",
        "scripts.table and scripts.data must either both exist or both be "
        "absent");
      co_return false;
    }

    auto table_entries = std::vector<ScriptResourceDesc> {};
    auto data_blob = std::vector<std::byte> {};

    if (table_exists) {
      const auto table_read = co_await reader->ReadFile(table_path);
      if (!table_read.has_value()) {
        AddDiagnostic(session, req, ImportSeverity::kError,
          "script.asset.scripts_table_read_failed",
          "Failed to read existing scripts.table: "
            + table_read.error().ToString());
        co_return false;
      }
      const auto data_read = co_await reader->ReadFile(data_path);
      if (!data_read.has_value()) {
        AddDiagnostic(session, req, ImportSeverity::kError,
          "script.asset.scripts_data_read_failed",
          "Failed to read existing scripts.data: "
            + data_read.error().ToString());
        co_return false;
      }

      const auto& table_bytes = table_read.value();
      if ((table_bytes.size() % sizeof(ScriptResourceDesc)) != 0U) {
        AddDiagnostic(session, req, ImportSeverity::kError,
          "script.asset.scripts_table_size_invalid",
          "Existing scripts.table size is not divisible by ScriptResourceDesc "
          "size");
        co_return false;
      }

      const auto entry_count = table_bytes.size() / sizeof(ScriptResourceDesc);
      table_entries.resize(entry_count);
      if (!table_bytes.empty()) {
        std::memcpy(
          table_entries.data(), table_bytes.data(), table_bytes.size());
      }
      data_blob = data_read.value();
    } else {
      table_entries.push_back(ScriptResourceDesc {});
    }

    if (table_entries.empty()) {
      table_entries.push_back(ScriptResourceDesc {});
    }

    if (table_entries.size() >= (std::numeric_limits<uint32_t>::max)()) {
      AddDiagnostic(session, req, ImportSeverity::kError,
        "script.asset.resource_index_overflow",
        "scripts.table reached maximum ScriptResourceDesc entry count");
      co_return false;
    }

    const auto data_offset = data_blob.size();
    if (data_offset > (std::numeric_limits<OffsetT>::max)()) {
      AddDiagnostic(session, req, ImportSeverity::kError,
        "script.asset.data_offset_overflow",
        "scripts.data offset overflow while appending script payload");
      co_return false;
    }
    if (source.bytes.size() > (std::numeric_limits<DataBlobSizeT>::max)()) {
      AddDiagnostic(session, req, ImportSeverity::kError,
        "script.asset.payload_too_large",
        "Script payload exceeds DataBlobSizeT limits");
      co_return false;
    }

    const auto resource_index
      = ResourceIndexT { static_cast<uint32_t>(table_entries.size()) };

    ScriptResourceDesc resource_desc {};
    resource_desc.data_offset = static_cast<OffsetT>(data_offset);
    resource_desc.size_bytes = static_cast<DataBlobSizeT>(source.bytes.size());
    resource_desc.language = ScriptLanguage::kLuau;
    resource_desc.encoding = ScriptEncoding::kSource;
    resource_desc.compression = ScriptCompression::kNone;
    resource_desc.content_hash
      = EffectiveContentHashingEnabled(req.options.with_content_hashing)
      ? ComputeContentHash64(source.bytes)
      : 0;
    table_entries.push_back(resource_desc);

    data_blob.insert(data_blob.end(), source.bytes.begin(), source.bytes.end());

    const auto table_write = co_await writer->Write(table_path,
      std::as_bytes(std::span(table_entries)),
      WriteOptions { .create_directories = true, .overwrite = true });
    if (!table_write.has_value()) {
      AddDiagnostic(session, req, ImportSeverity::kError,
        "script.asset.scripts_table_write_failed",
        "Failed to write scripts.table: " + table_write.error().ToString());
      co_return false;
    }

    const auto data_write
      = co_await writer->Write(data_path, std::span<const std::byte>(data_blob),
        WriteOptions { .create_directories = true, .overwrite = true });
    if (!data_write.has_value()) {
      AddDiagnostic(session, req, ImportSeverity::kError,
        "script.asset.scripts_data_write_failed",
        "Failed to write scripts.data: " + data_write.error().ToString());
      co_return false;
    }

    try {
      index_registry->RegisterExternalFile(
        session.CookedRoot(), FileKind::kScriptsTable, table_relpath);
      index_registry->RegisterExternalFile(
        session.CookedRoot(), FileKind::kScriptsData, data_relpath);
    } catch (const std::exception& ex) {
      AddDiagnostic(session, req, ImportSeverity::kError,
        "script.asset.index_registration_failed", ex.what());
      co_return false;
    }

    desc.source_resource_index = resource_index;
  } else {
    desc.flags = ScriptAssetFlags::kAllowExternalSource;
    const auto external_source_path = BuildExternalSourcePath(req.source_path);
    const auto external_path_span
      = std::span<char, sizeof(desc.external_source_path)>(
        desc.external_source_path);
    if (!CopyNullTerminated(external_source_path, external_path_span)) {
      AddDiagnostic(session, req, ImportSeverity::kError,
        "script.asset.external_path_too_long",
        "External script source path exceeds ScriptAssetDesc capacity");
      co_return false;
    }
  }

  try {
    session.AssetEmitter().Emit(key, AssetType::kScript, virtual_path,
      descriptor_relpath, std::as_bytes(std::span { &desc, 1 }));
  } catch (const std::exception& ex) {
    AddDiagnostic(session, req, ImportSeverity::kError,
      "script.asset.descriptor_emit_failed", ex.what());
    co_return false;
  }

  co_return true;
}

auto ScriptImportJob::FinalizeSession(ImportSession& session)
  -> co::Co<ImportReport>
{
  co_return co_await session.Finalize();
}

} // namespace oxygen::content::import::detail
