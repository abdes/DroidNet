//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <exception>
#include <limits>
#include <optional>
#include <span>
#include <stop_token>
#include <string>
#include <utility>
#include <vector>

#include <Oxygen/Base/Logging.h>
#include <Oxygen/Base/Macros.h>
#include <Oxygen/Base/ObserverPtr.h>
#include <Oxygen/Cooker/Import/AsyncImportService.h>
#include <Oxygen/Cooker/Import/IAsyncFileReader.h>
#include <Oxygen/Cooker/Import/IAsyncFileWriter.h>
#include <Oxygen/Cooker/Import/ImportDiagnostics.h>
#include <Oxygen/Cooker/Import/ImportOptions.h>
#include <Oxygen/Cooker/Import/ImportRequest.h>
#include <Oxygen/Cooker/Import/Internal/AssetReferenceBuilder.h>
#include <Oxygen/Cooker/Import/Internal/Emitters/AssetEmitter.h>
#include <Oxygen/Cooker/Import/Internal/ImportPipeline.h>
#include <Oxygen/Cooker/Import/Internal/ImportSession.h>
#include <Oxygen/Cooker/Import/Internal/LooseCookedIndexRegistry.h>
#include <Oxygen/Cooker/Import/Internal/Pipelines/ScriptAssetImportPipeline.h>
#include <Oxygen/Cooker/Import/Internal/Pipelines/ScriptImportPipelineCommon.h>
#include <Oxygen/Cooker/Import/Internal/ResourceTableRegistry.h>
#include <Oxygen/Cooker/Import/Internal/Utils/VirtualPathResolution.h>
#include <Oxygen/Core/Meta/Scripting/ScriptCompileMode.h>
#include <Oxygen/Core/Types/AssetKey.h>
#include <Oxygen/Core/Types/DescriptorExtensions.h>
#include <Oxygen/Data/AssetReferences.h>
#include <Oxygen/Data/AssetType.h>
#include <Oxygen/Data/DescriptorStrings.h>
#include <Oxygen/Data/LooseCookedIndexFormat.h>
#include <Oxygen/Data/PakFormat.h>
#include <Oxygen/Data/ScriptAsset.h>
#include <Oxygen/OxCo/Co.h>
#include <Oxygen/OxCo/Nursery.h>
#include <Oxygen/Scripting/Analysis/ScriptAnalyzer.h>
#include <Oxygen/Scripting/Analysis/ScriptDiagnostic.h>

namespace oxygen::content::import {

namespace {

  namespace script_import = detail::script_import;

  using core::meta::scripting::ScriptCompileMode;
  using data::AssetType;
  using data::loose_cooked::FileKind;
  using data::pak::core::DataBlobSizeT;
  using data::pak::core::OffsetT;
  using data::pak::core::ResourceIndexT;
  using data::pak::scripting::ScriptAssetDesc;
  using data::pak::scripting::ScriptAssetFlags;
  using data::pak::scripting::ScriptCompression;
  using data::pak::scripting::ScriptEncoding;
  using data::pak::scripting::ScriptLanguage;
  using data::pak::scripting::ScriptParamRecord;
  using data::pak::scripting::ScriptResourceDesc;
  enum class EmbeddedResourceKind : uint8_t { kSource = 0, kBytecode = 1 };

  //! Descriptor under construction. Built in place and never moved: the
  //! reference builder's containers allocate when moved.
  struct ScriptDescriptorContext final {
    ScriptDescriptorContext() = default;
    ~ScriptDescriptorContext() = default;
    OXYGEN_MAKE_NON_COPYABLE(ScriptDescriptorContext)
    OXYGEN_MAKE_NON_MOVABLE(ScriptDescriptorContext)

    data::AssetKey key;
    std::string virtual_path;
    std::string descriptor_relpath;
    ScriptAssetDesc descriptor {};
    AssetReferenceBuilder references;
    //! Declared parameters, written after the descriptor.
    std::vector<ScriptParamRecord> params;
    //! The descriptor string table, written after the parameters.
    data::DescriptorStringsBuilder strings;
  };

  [[nodiscard]] auto IsStopRequested(const std::stop_token& token) noexcept
    -> bool
  {
    return token.stop_possible() && token.stop_requested();
  }

  auto ValidateScriptAssetRequest(ImportSession& session,
    const ImportRequest& request,
    const AsyncImportService::ScriptCompileCallback& script_compile_callback)
    -> bool
  {
    const auto& scripting = request.options.scripting;
    if (scripting.import_kind != ScriptingImportKind::kScriptAsset) {
      script_import::AddDiagnostic(session, request, ImportSeverity::kError,
        "script.request.invalid_import_kind",
        "Script asset pipeline requires "
        "options.scripting.import_kind=kScriptAsset");
      return false;
    }

    if (scripting.compile_scripts && !script_compile_callback) {
      const auto* const mode
        = (scripting.compile_mode == ScriptCompileMode::kDebug) ? "debug"
                                                                : "optimized";
      script_import::AddDiagnostic(session, request, ImportSeverity::kError,
        "script.asset.compiler_unavailable",
        "compile_scripts=true requested, but no script compiler callback is "
        "wired in Cooker/Import for mode='"
          + std::string { mode } + "'");
      return false;
    }

    return true;
  }

  auto InitScriptDescriptorContext(ImportSession& session,
    const ImportRequest& request, ScriptDescriptorContext& context) -> bool
  {
    auto placement
      = internal::PlaceAsset(request, data::kScriptDescriptorExtension);
    if (!placement.has_value()) {
      session.AddDiagnostic(std::move(placement.error()));
      return false;
    }
    auto& [virtual_path, descriptor_relpath] = *placement;
    const auto script_name
      = script_import::DeriveScriptName(request.source_path);

    const auto key = oxygen::data::AssetKey::FromVirtualPath(virtual_path);

    auto descriptor = ScriptAssetDesc {};
    descriptor.header.asset_type = static_cast<uint8_t>(AssetType::kScript);
    descriptor.header.version = data::pak::scripting::kScriptAssetVersion;
    descriptor.flags = ScriptAssetFlags::kNone;
    descriptor.header.name_offset = context.strings.Add(script_name);

    context.key = key;
    context.virtual_path = std::move(virtual_path);
    context.descriptor_relpath = std::move(descriptor_relpath);
    context.descriptor = descriptor;
    return true;
  }

  auto ToImportSeverity(const scripting::DiagnosticSeverity severity)
    -> ImportSeverity
  {
    switch (severity) {
    case scripting::DiagnosticSeverity::kError:
      return ImportSeverity::kError;
    case scripting::DiagnosticSeverity::kWarning:
      return ImportSeverity::kWarning;
    case scripting::DiagnosticSeverity::kNote:
      break;
    }
    return ImportSeverity::kInfo;
  }

  //! Reads the script's parameter declarations into cooked records.
  /*!
   Reports every analysis diagnostic. Fails when the script has errors, so a
   script whose declaration is invalid never cooks.
  */
  auto AnalyzeDeclarations(ImportSession& session, const ImportRequest& request,
    std::span<const std::byte> source_bytes, ScriptDescriptorContext& context)
    -> bool
  {
    const auto source_name
      = script_import::BuildExternalSourcePath(request).value_or(
        request.source_path.generic_string());
    const auto analysis = scripting::ScriptAnalyzer {}.Analyze({
      .source_name = source_name,
      .source = source_bytes,
      .level = scripting::AnalysisLevel::kDeclarations,
    });
    for (const auto& diagnostic : analysis.diagnostics) {
      script_import::AddDiagnostic(session, request,
        ToImportSeverity(diagnostic.severity),
        "script." + scripting::to_string(diagnostic.code),
        scripting::FormatSummary(diagnostic));
    }
    if (analysis.HasErrors()) {
      return false;
    }

    context.params.reserve(analysis.declarations.size());
    for (const auto& declaration : analysis.declarations) {
      auto record = data::MakeScriptParamRecord(
        declaration.name, declaration.default_value, context.strings);
      if (!record) {
        script_import::AddDiagnostic(session, request, ImportSeverity::kError,
          "script.asset.declaration_unencodable",
          "Parameter '" + declaration.name
            + "' has an empty name or holds a NUL byte");
        return false;
      }
      context.params.push_back(*record);
    }
    return true;
  }

  auto AppendEmbeddedScriptResource(ImportSession& session,
    const ImportRequest& request, std::span<const std::byte> source_bytes,
    observer_ptr<LooseCookedIndexRegistry> index_registry,
    ScriptDescriptorContext& context, const EmbeddedResourceKind resource_kind)
    -> co::Co<bool>
  {
    auto* const reader = session.CookedReader().get();
    auto* const writer = session.FileWriter().get();
    auto* const registry = index_registry.get();
    if (reader == nullptr || writer == nullptr || registry == nullptr) {
      script_import::AddDiagnostic(session, request, ImportSeverity::kError,
        "script.asset.io_unavailable",
        "Script embedded emission requires file reader/writer/index registry");
      co_return false;
    }
    auto* const table_registry = session.TableRegistry().get();
    if (table_registry == nullptr) {
      script_import::AddDiagnostic(session, request, ImportSeverity::kError,
        "script.asset.table_registry_unavailable",
        "Script embedded emission requires ResourceTableRegistry");
      co_return false;
    }

    const auto table_relpath = script_import::BuildScriptsTableRelPath(request);
    const auto data_relpath = script_import::BuildScriptsDataRelPath(request);
    const auto table_path = session.CookedRoot() / table_relpath;
    const auto data_path = session.CookedRoot() / data_relpath;

    auto scripts_table_lock
      = co_await table_registry->LockScriptsTable(session.CookedRoot());

    const auto table_exists_result = co_await reader->Exists(table_path);
    if (!table_exists_result.has_value()) {
      script_import::AddDiagnostic(session, request, ImportSeverity::kError,
        "script.asset.scripts_table_exists_check_failed",
        "Failed checking scripts.table existence: "
          + table_exists_result.error().ToString());
      co_return false;
    }
    const auto data_exists_result = co_await reader->Exists(data_path);
    if (!data_exists_result.has_value()) {
      script_import::AddDiagnostic(session, request, ImportSeverity::kError,
        "script.asset.scripts_data_exists_check_failed",
        "Failed checking scripts.data existence: "
          + data_exists_result.error().ToString());
      co_return false;
    }

    const auto table_exists = table_exists_result.value();
    const auto data_exists = data_exists_result.value();
    if (table_exists != data_exists) {
      script_import::AddDiagnostic(session, request, ImportSeverity::kError,
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
        script_import::AddDiagnostic(session, request, ImportSeverity::kError,
          "script.asset.scripts_table_read_failed",
          "Failed to read existing scripts.table: "
            + table_read.error().ToString());
        co_return false;
      }
      const auto data_read = co_await reader->ReadFile(data_path);
      if (!data_read.has_value()) {
        script_import::AddDiagnostic(session, request, ImportSeverity::kError,
          "script.asset.scripts_data_read_failed",
          "Failed to read existing scripts.data: "
            + data_read.error().ToString());
        co_return false;
      }

      const auto& table_bytes = table_read.value();
      if ((table_bytes.size() % sizeof(ScriptResourceDesc)) != 0U) {
        script_import::AddDiagnostic(session, request, ImportSeverity::kError,
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
    if (table_entries.size() >= std::numeric_limits<uint32_t>::max()) {
      script_import::AddDiagnostic(session, request, ImportSeverity::kError,
        "script.asset.resource_index_overflow",
        "scripts.table reached maximum ScriptResourceDesc entry count");
      co_return false;
    }
    if (data_blob.size() > std::numeric_limits<OffsetT>::max()) {
      script_import::AddDiagnostic(session, request, ImportSeverity::kError,
        "script.asset.data_offset_overflow",
        "scripts.data offset overflow while appending script payload");
      co_return false;
    }
    if (source_bytes.size() > std::numeric_limits<DataBlobSizeT>::max()) {
      script_import::AddDiagnostic(session, request, ImportSeverity::kError,
        "script.asset.payload_too_large",
        "Script payload exceeds DataBlobSizeT limits");
      co_return false;
    }

    const auto resource_index
      = ResourceIndexT { static_cast<uint32_t>(table_entries.size()) };
    auto resource_desc = ScriptResourceDesc {};
    resource_desc.data_offset = static_cast<OffsetT>(data_blob.size());
    resource_desc.size_bytes = static_cast<DataBlobSizeT>(source_bytes.size());
    resource_desc.language = ScriptLanguage::kLuau;
    resource_desc.encoding = resource_kind == EmbeddedResourceKind::kBytecode
      ? ScriptEncoding::kBytecode
      : ScriptEncoding::kSource;
    resource_desc.compression = ScriptCompression::kNone;
    resource_desc.content_hash
      = EffectiveContentHashingEnabled(request.options.with_content_hashing)
      ? script_import::ComputeContentHash64(source_bytes)
      : 0;
    table_entries.push_back(resource_desc);

    data_blob.insert(data_blob.end(), source_bytes.begin(), source_bytes.end());

    const auto table_write = co_await writer->Write(table_path,
      std::as_bytes(std::span(table_entries)),
      WriteOptions { .create_directories = true, .overwrite = true });
    if (!table_write.has_value()) {
      script_import::AddDiagnostic(session, request, ImportSeverity::kError,
        "script.asset.scripts_table_write_failed",
        "Failed to write scripts.table: " + table_write.error().ToString());
      co_return false;
    }

    const auto data_write
      = co_await writer->Write(data_path, std::span<const std::byte>(data_blob),
        WriteOptions { .create_directories = true, .overwrite = true });
    if (!data_write.has_value()) {
      script_import::AddDiagnostic(session, request, ImportSeverity::kError,
        "script.asset.scripts_data_write_failed",
        "Failed to write scripts.data: " + data_write.error().ToString());
      co_return false;
    }

    try {
      registry->RegisterExternalFile(
        session.CookedRoot(), FileKind::kScriptsTable, table_relpath);
      registry->RegisterExternalFile(
        session.CookedRoot(), FileKind::kScriptsData, data_relpath);
    } catch (const std::exception& ex) {
      script_import::AddDiagnostic(session, request, ImportSeverity::kError,
        "script.asset.index_registration_failed", ex.what());
      co_return false;
    }

    if (resource_kind == EmbeddedResourceKind::kBytecode) {
      context.descriptor.bytecode_resource_index
        = context.references.AddResource(
          data::ResourceKind::kScript, resource_index);
    } else {
      context.descriptor.source_resource_index = context.references.AddResource(
        data::ResourceKind::kScript, resource_index);
    }
    co_return true;
  }

  auto CompileSourceToBytecode(ImportSession& session, const ImportRequest& req,
    std::span<const std::byte> source_bytes,
    const AsyncImportService::ScriptCompileCallback& script_compile_callback)
    -> std::optional<std::vector<std::byte>>
  {
    const auto source_name
      = script_import::BuildExternalSourcePath(req).value_or(
        req.source_path.generic_string());
    auto compile_result
      = script_compile_callback(AsyncImportService::ScriptCompileRequest {
        .source_name = source_name,
        .source_bytes = source_bytes,
        .compile_mode = req.options.scripting.compile_mode,
      });
    if (!compile_result.success) {
      if (compile_result.diagnostics.empty()) {
        script_import::AddDiagnostic(session, req, ImportSeverity::kError,
          "script.asset.compile_failed",
          "Script compile failed with no diagnostic");
      }
      for (const auto& diagnostic : compile_result.diagnostics) {
        script_import::AddDiagnostic(session, req,
          ToImportSeverity(diagnostic.severity),
          "script." + scripting::to_string(diagnostic.code),
          scripting::FormatSummary(diagnostic));
      }
      return std::nullopt;
    }
    if (compile_result.bytecode.empty()) {
      script_import::AddDiagnostic(session, req, ImportSeverity::kError,
        "script.asset.compile_failed",
        "Script compiler produced empty bytecode output");
      return std::nullopt;
    }
    return std::move(compile_result.bytecode);
  }

  auto ConfigureExternalSource(ImportSession& session,
    const ImportRequest& request, ScriptDescriptorContext& context) -> bool
  {
    auto& desc = context.descriptor;
    desc.flags = ScriptAssetFlags::kAllowExternalSource;
    const auto external_source_path
      = script_import::BuildExternalSourcePath(request);
    if (!external_source_path) {
      script_import::AddDiagnostic(session, request, ImportSeverity::kError,
        "script.asset.source_root_invalid",
        "External scripts require a source_root containing the source file");
      return false;
    }
    desc.external_source_path_offset
      = context.strings.Add(*external_source_path);
    return true;
  }

  auto EmitScriptDescriptor(
    ImportSession& session, ScriptDescriptorContext& context) -> bool
  {
    if (!context.params.empty()) {
      context.descriptor.params = {
        .offset = sizeof(ScriptAssetDesc),
        .count = static_cast<uint32_t>(context.params.size()),
        .entry_size = sizeof(ScriptParamRecord),
      };
    }
    auto bytes = std::vector<std::byte>(sizeof(ScriptAssetDesc)
      + (context.params.size() * sizeof(ScriptParamRecord)));
    std::memcpy(bytes.data(), &context.descriptor, sizeof(ScriptAssetDesc));
    if (!context.params.empty()) {
      std::memcpy(std::span(bytes).subspan(sizeof(ScriptAssetDesc)).data(),
        context.params.data(),
        context.params.size() * sizeof(ScriptParamRecord));
    }
    data::AppendDescriptorStrings(bytes, context.strings);

    try {
      session.AssetEmitter().Emit(context.key, AssetType::kScript,
        context.virtual_path, context.descriptor_relpath, bytes,
        std::move(context.references).Build());
    } catch (const std::exception& ex) {
      script_import::AddDiagnostic(session, session.Request(),
        ImportSeverity::kError, "script.asset.descriptor_emit_failed",
        ex.what());
      return false;
    }
    return true;
  }

} // namespace

ScriptAssetImportPipeline::ScriptAssetImportPipeline()
  : ScriptAssetImportPipeline(Config {})
{
}

ScriptAssetImportPipeline::ScriptAssetImportPipeline(Config config)
  : config_(config)
  , input_channel_(config.queue_capacity)
  , output_channel_(config.queue_capacity)
{
}

ScriptAssetImportPipeline::~ScriptAssetImportPipeline()
{
  if (started_) {
    DLOG_IF_F(WARNING, HasPending(),
      "ScriptAssetImportPipeline destroyed with {} pending items",
      PendingCount());
  }
  input_channel_.Close();
  output_channel_.Close();
}

auto ScriptAssetImportPipeline::Start(co::Nursery& nursery) -> void
{
  DCHECK_F(
    !started_, "ScriptAssetImportPipeline::Start() called more than once");
  started_ = true;

  const auto worker_count = std::max(1U, config_.worker_count);
  for (uint32_t i = 0; i < worker_count; ++i) {
    nursery.Start(&ScriptAssetImportPipeline::Worker, this);
  }
}

auto ScriptAssetImportPipeline::Submit(WorkItem item) -> co::Co<>
{
  pending_.fetch_add(1, std::memory_order_acq_rel);
  submitted_.fetch_add(1, std::memory_order_acq_rel);
  co_await input_channel_.Send(std::move(item));
}

auto ScriptAssetImportPipeline::TrySubmit(WorkItem item) -> bool
{
  if (input_channel_.Closed() || input_channel_.Full()) {
    return false;
  }
  const auto ok = input_channel_.TrySend(std::move(item));
  if (ok) {
    pending_.fetch_add(1, std::memory_order_acq_rel);
    submitted_.fetch_add(1, std::memory_order_acq_rel);
  }
  return ok;
}

auto ScriptAssetImportPipeline::Collect() -> co::Co<WorkResult>
{
  auto maybe_result = co_await output_channel_.Receive();
  if (!maybe_result.has_value()) {
    co_return WorkResult {
      .source_id = {},
      .diagnostics = {},
      .telemetry = {},
      .success = false,
    };
  }

  pending_.fetch_sub(1, std::memory_order_acq_rel);
  if (maybe_result->success) {
    completed_.fetch_add(1, std::memory_order_acq_rel);
  } else {
    failed_.fetch_add(1, std::memory_order_acq_rel);
  }
  co_return std::move(*maybe_result);
}

auto ScriptAssetImportPipeline::Close() -> void { input_channel_.Close(); }

auto ScriptAssetImportPipeline::HasPending() const noexcept -> bool
{
  return pending_.load(std::memory_order_acquire) > 0;
}

auto ScriptAssetImportPipeline::PendingCount() const noexcept -> size_t
{
  return pending_.load(std::memory_order_acquire);
}

auto ScriptAssetImportPipeline::GetProgress() const noexcept -> PipelineProgress
{
  const auto submitted = submitted_.load(std::memory_order_acquire);
  const auto completed = completed_.load(std::memory_order_acquire);
  const auto failed = failed_.load(std::memory_order_acquire);
  return PipelineProgress {
    .submitted = submitted,
    .completed = completed,
    .failed = failed,
    .in_flight = submitted - completed - failed,
    .throughput = 0.0F,
  };
}

auto ScriptAssetImportPipeline::Worker() -> co::Co<>
{
  const auto MakeDuration
    = [](const std::chrono::steady_clock::time_point start,
        const std::chrono::steady_clock::time_point end)
    -> std::chrono::microseconds {
    return std::chrono::duration_cast<std::chrono::microseconds>(end - start);
  };

  while (true) {
    auto maybe_item = co_await input_channel_.Receive();
    if (!maybe_item.has_value()) {
      break;
    }

    auto item = std::move(*maybe_item);
    if (IsStopRequested(item.stop_token)) {
      co_await ReportCancelled(std::move(item));
      continue;
    }

    if (item.on_started) {
      item.on_started();
    }

    const auto process_start = std::chrono::steady_clock::now();
    auto result = WorkResult {
      .source_id = item.source_id,
      .diagnostics = {},
      .telemetry = {},
      .success = false,
    };

    try {
      result.success = co_await Process(item);
    } catch (const std::exception& ex) {
      if (item.session != nullptr) {
        const auto& request = item.session->Request();
        detail::script_import::AddDiagnostic(*item.session, request,
          ImportSeverity::kError, "script.asset.pipeline_exception",
          std::string { "Unhandled script asset pipeline exception: " }
            + ex.what());
      }
      result.success = false;
    }

    result.telemetry.cook_duration
      = MakeDuration(process_start, std::chrono::steady_clock::now());

    if (item.on_finished) {
      item.on_finished();
    }

    co_await output_channel_.Send(std::move(result));
  }

  co_return;
}

auto ScriptAssetImportPipeline::Process(WorkItem& item) -> co::Co<bool>
{
  auto* const session = item.session.get();
  if (session == nullptr) {
    co_return false;
  }

  const auto& req = session->Request();
  if (!ValidateScriptAssetRequest(
        *session, req, item.script_compile_callback)) {
    co_return false;
  }

  auto descriptor_context = ScriptDescriptorContext {};
  if (!InitScriptDescriptorContext(*session, req, descriptor_context)
    || !AnalyzeDeclarations(
      *session, req, item.source_bytes, descriptor_context)) {
    co_return false;
  }

  auto bytecode_bytes = std::optional<std::vector<std::byte>> {};
  if (req.options.scripting.compile_scripts) {
    bytecode_bytes = CompileSourceToBytecode(
      *session, req, item.source_bytes, item.script_compile_callback);
    if (!bytecode_bytes.has_value()) {
      co_return false;
    }
  }

  // Embedded storage carries the source; external storage records its path.
  // Compiled bytecode is embedded either way, so external scripts run from
  // the source when a source root has it and from the bytecode otherwise.
  if (req.options.scripting.script_storage == ScriptStorageMode::kEmbedded) {
    const auto appended
      = co_await AppendEmbeddedScriptResource(*session, req, item.source_bytes,
        item.index_registry, descriptor_context, EmbeddedResourceKind::kSource);
    if (!appended) {
      co_return false;
    }
  } else if (!ConfigureExternalSource(*session, req, descriptor_context)) {
    co_return false;
  }

  if (bytecode_bytes.has_value()) {
    const auto appended_bytecode = co_await AppendEmbeddedScriptResource(
      *session, req,
      std::span<const std::byte>(
        bytecode_bytes->data(), bytecode_bytes->size()),
      item.index_registry, descriptor_context, EmbeddedResourceKind::kBytecode);
    if (!appended_bytecode) {
      co_return false;
    }
  }

  co_return EmitScriptDescriptor(*session, descriptor_context);
}

auto ScriptAssetImportPipeline::ReportCancelled(WorkItem item) -> co::Co<>
{
  if (item.on_finished) {
    item.on_finished();
  }
  co_await output_channel_.Send(WorkResult {
    .source_id = std::move(item.source_id),
    .diagnostics = {},
    .telemetry = {},
    .success = false,
  });
}

} // namespace oxygen::content::import
