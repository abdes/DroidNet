//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <memory>
#include <span>
#include <stop_token>
#include <string>
#include <unordered_set>
#include <utility>
#include <vector>

#include <nlohmann/json.hpp>

#include <Oxygen/Base/Finally.h>
#include <Oxygen/Base/ObserverPtr.h>
#include <Oxygen/Cooker/Import/BufferImportTypes.h>
#include <Oxygen/Cooker/Import/IAsyncFileReader.h>
#include <Oxygen/Cooker/Import/ImportCodes.h>
#include <Oxygen/Cooker/Import/ImportDiagnostics.h>
#include <Oxygen/Cooker/Import/ImportRequest.h>
#include <Oxygen/Cooker/Import/Internal/BufferSource.h>
#include <Oxygen/Cooker/Import/Internal/Emitters/BufferEmitter.h>
#include <Oxygen/Cooker/Import/Internal/Emitters/ResourceDescriptorEmitter.h>
#include <Oxygen/Cooker/Import/Internal/ImportSession.h>
#include <Oxygen/Cooker/Import/Internal/Jobs/BufferImportSubmitter.h>
#include <Oxygen/Cooker/Import/Internal/Pipelines/BufferPipeline.h>
#include <Oxygen/Cooker/Import/Internal/Utils/BufferViews.h>
#include <Oxygen/Cooker/Import/Internal/Utils/SourceReadDiagnostics.h>
#include <Oxygen/Data/BufferResource.h>
#include <Oxygen/Data/CookedFormatError.h>
#include <Oxygen/Data/PakFormat_core.h>
#include <Oxygen/OxCo/Algorithms.h>
#include <Oxygen/OxCo/Co.h>

namespace oxygen::content::import::detail {

namespace {

  auto AddDiagnostic(ImportSession& session, const ImportRequest& request,
    const ImportSeverity severity, std::string code, std::string message,
    const nlohmann::json::json_pointer& pointer
    = nlohmann::json::json_pointer {}) -> void
  {
    session.AddDiagnostic({
      .severity = severity,
      .code = std::move(code),
      .message = std::move(message),
      .source_path = request.source_path.string(),
      .json_pointer = pointer.to_string(),
    });
  }

  auto AddDiagnostics(
    ImportSession& session, std::vector<ImportDiagnostic> diagnostics) -> void
  {
    for (auto& diagnostic : diagnostics) {
      session.AddDiagnostic(std::move(diagnostic));
    }
  }

  auto AddViewIssues(ImportSession& session, const ImportRequest& request,
    const std::vector<internal::BufferDescriptorViewIssue>& issues) -> void
  {
    for (const auto& issue : issues) {
      AddDiagnostic(session, request, ImportSeverity::kError, issue.code,
        issue.message, nlohmann::json::json_pointer { issue.json_pointer });
    }
  }

  [[nodiscard]] auto MakeDuration(
    const std::chrono::steady_clock::time_point start,
    const std::chrono::steady_clock::time_point end)
    -> std::chrono::microseconds
  {
    return std::chrono::duration_cast<std::chrono::microseconds>(end - start);
  }

} // namespace

BufferImportSubmitter::BufferImportSubmitter(ImportSession& session,
  const ImportRequest& request, const observer_ptr<IAsyncFileReader> reader,
  std::stop_token stop_token)
  : session_(&session)
  , request_(&request)
  , reader_(reader)
  , stop_token_(std::move(stop_token))
{
}

auto BufferImportSubmitter::CookBuffers(
  const std::span<const internal::BufferSource> entries,
  const std::unordered_set<std::string>& keep_contents_of,
  BufferPipeline& pipeline) -> co::Co<CookedBuffers>
{
  auto cooked = CookedBuffers {};
  auto submitted = Submitted {};
  co_await co::AllOf(
    SubmitBuffers(entries, keep_contents_of, pipeline, submitted, cooked),
    CollectAndEmit(pipeline, submitted, cooked));
  co_return cooked;
}

auto BufferImportSubmitter::SubmitBuffers(
  const std::span<const internal::BufferSource> entries,
  const std::unordered_set<std::string>& keep_contents_of,
  BufferPipeline& pipeline, Submitted& submitted, CookedBuffers& cooked)
  -> co::Co<>
{
  // Closing it ends the collector, whatever ends the submission.
  [[maybe_unused]] auto close_pipeline
    = Finally([&pipeline] -> void { pipeline.Close(); });

  if (reader_ == nullptr) {
    AddDiagnostic(*session_, *request_, ImportSeverity::kError,
      std::string(import_codes::kBufferContainerReaderUnavailable),
      "Async file reader is not available");
    co_return;
  }

  if (entries.empty()) {
    AddDiagnostic(*session_, *request_, ImportSeverity::kError,
      std::string(import_codes::kBufferContainerNoBuffers),
      "No valid buffer entries were produced");
    co_return;
  }

  if (session_->HasErrors()) {
    co_return;
  }

  submitted.views.reserve(entries.size());
  for (const auto& entry : entries) {
    const auto read_start = std::chrono::steady_clock::now();
    auto read_result = co_await reader_->ReadFile(entry.source_path);
    session_->AddSourceLoadDuration(
      MakeDuration(read_start, std::chrono::steady_clock::now()));
    const auto source_pointer
      = nlohmann::json::json_pointer { entry.json_pointer } / "source";
    if (!read_result.has_value()) {
      if (auto too_large = SourceTooLargeDiagnostic(
            read_result.error(), entry.source_path, reader_->SourceLimit())) {
        session_->AddDiagnostic(std::move(*too_large));
      } else {
        AddDiagnostic(*session_, *request_, ImportSeverity::kError,
          std::string(import_codes::kBufferContainerSourceReadFailed),
          "Failed reading buffer source '" + entry.source_path.string()
            + "': " + read_result.error().message(),
          source_pointer);
      }
      continue;
    }

    const auto source_size = read_result.value().Size().get();
    if (source_size == 0U) {
      AddDiagnostic(*session_, *request_, ImportSeverity::kError,
        std::string(import_codes::kBufferContainerSourceEmpty),
        "Buffer source '" + entry.source_path.string() + "' holds no bytes",
        source_pointer);
      continue;
    }
    if (source_size > data::pak::core::kDataBlobMaxSize) {
      AddDiagnostic(*session_, *request_, ImportSeverity::kError,
        std::string(import_codes::kBufferContainerSourceTooLarge),
        "Buffer source exceeds maximum supported size of 4 GiB",
        source_pointer);
      continue;
    }

    auto descriptor = data::pak::core::BufferResourceDesc {};
    descriptor.size_bytes = static_cast<uint32_t>(source_size);
    descriptor.usage_flags = entry.usage_flags;
    descriptor.element_stride = entry.element_stride;
    descriptor.element_format = entry.element_format;

    // A row every reader refuses is refused here, naming its declaration.
    try {
      data::CheckBufferRow(descriptor);
    } catch (const data::CookedFormatError& error) {
      AddDiagnostic(*session_, *request_, ImportSeverity::kError,
        std::string(import_codes::kBufferContainerRowInvalid), error.what(),
        nlohmann::json::json_pointer { entry.json_pointer });
      continue;
    }

    auto view_issues = std::vector<internal::BufferDescriptorViewIssue> {};
    auto normalized_views = internal::NormalizeBufferViews(entry.view_specs,
      descriptor, nlohmann::json::json_pointer { entry.json_pointer } / "views",
      view_issues);
    AddViewIssues(*session_, *request_, view_issues);
    if (!view_issues.empty()) {
      continue;
    }

    submitted.views.emplace(entry.source_id, std::move(normalized_views));

    // The payload type is shared with cookers that build their bytes.
    const auto source_bytes = read_result.value().Bytes();
    if (keep_contents_of.contains(entry.source_id)) {
      submitted.contents.emplace(entry.source_id,
        std::make_shared<const std::vector<std::byte>>(
          source_bytes.begin(), source_bytes.end()));
    }
    auto item = BufferPipeline::WorkItem {};
    item.source_id = entry.source_id;
    item.bytes
      = std::vector<std::byte>(source_bytes.begin(), source_bytes.end());
    item.cooked = CookedBufferPayload {
      .data = {},
      .alignment = entry.alignment,
      .usage_flags = entry.usage_flags,
      .element_stride = entry.element_stride,
      .element_format = entry.element_format,
    };
    item.stop_token = stop_token_;
    co_await pipeline.Submit(std::move(item));
    ++cooked.submitted_count;
  }
}

auto BufferImportSubmitter::CollectAndEmit(BufferPipeline& pipeline,
  const Submitted& submitted, CookedBuffers& cooked) -> co::Co<>
{
  while (auto result = co_await pipeline.CollectNext()) {
    Emit(std::move(*result), submitted, cooked);
  }
}

auto BufferImportSubmitter::Emit(BufferPipeline::WorkResult result,
  const Submitted& submitted, CookedBuffers& cooked) -> void
{
  if (result.telemetry.cook_duration.has_value()) {
    session_->AddCookDuration(*result.telemetry.cook_duration);
  }
  AddDiagnostics(*session_, std::move(result.diagnostics));
  if (!result.success) {
    return;
  }

  const auto views_it = submitted.views.find(result.source_id);
  if (views_it == submitted.views.end()) {
    AddDiagnostic(*session_, *request_, ImportSeverity::kError,
      std::string(import_codes::kBufferContainerInternalLookupFailed),
      "Internal buffer views lookup failed for '" + result.source_id + "'");
    return;
  }

  const auto emit_start = std::chrono::steady_clock::now();
  const auto emitted_index
    = session_->BufferEmitter().Emit(std::move(result.cooked));
  session_->AddEmitDuration(
    MakeDuration(emit_start, std::chrono::steady_clock::now()));

  const auto descriptor
    = session_->BufferEmitter().TryGetDescriptor(emitted_index);
  if (!descriptor.has_value()) {
    AddDiagnostic(*session_, *request_, ImportSeverity::kError,
      std::string(import_codes::kBufferContainerDescriptorMissing),
      "Missing buffer descriptor for emitted resource index of '"
        + result.source_id + "'");
    return;
  }

  try {
    [[maybe_unused]] const auto relpath
      = session_->ResourceDescriptorEmitter().EmitBuffer(result.source_id,
        data::pak::core::ResourceIndexT { emitted_index }, *descriptor,
        views_it->second);
  } catch (const std::exception& ex) {
    AddDiagnostic(*session_, *request_, ImportSeverity::kError,
      std::string(import_codes::kBufferContainerSidecarEmitFailed),
      "Failed to emit the buffer sidecar descriptor of '" + result.source_id
        + "': " + std::string(ex.what()));
  }

  const auto contents_it = submitted.contents.find(result.source_id);
  cooked.emitted.push_back(EmittedBuffer {
    .source_id = result.source_id,
    .resource_index = data::pak::core::ResourceIndexT { emitted_index },
    .descriptor = *descriptor,
    .views = views_it->second,
    .contents
    = contents_it != submitted.contents.end() ? contents_it->second : nullptr,
  });
}

} // namespace oxygen::content::import::detail
