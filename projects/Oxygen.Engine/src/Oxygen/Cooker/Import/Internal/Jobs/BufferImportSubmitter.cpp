//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <filesystem>
#include <limits>
#include <optional>
#include <span>
#include <stop_token>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include <Oxygen/Base/ObserverPtr.h>
#include <Oxygen/Cooker/Cooked/CookedContent.h>
#include <Oxygen/Cooker/Cooked/CookerStorage.h>
#include <Oxygen/Cooker/Import/BufferImportTypes.h>
#include <Oxygen/Cooker/Import/IAsyncFileReader.h>
#include <Oxygen/Cooker/Import/ImportDiagnostics.h>
#include <Oxygen/Cooker/Import/ImportLimits.h>
#include <Oxygen/Cooker/Import/ImportRequest.h>
#include <Oxygen/Cooker/Import/Internal/BufferSource.h>
#include <Oxygen/Cooker/Import/Internal/Emitters/BufferEmitter.h>
#include <Oxygen/Cooker/Import/Internal/Emitters/ResourceDescriptorEmitter.h>
#include <Oxygen/Cooker/Import/Internal/ImportSession.h>
#include <Oxygen/Cooker/Import/Internal/Jobs/BufferImportSubmitter.h>
#include <Oxygen/Cooker/Import/Internal/Pipelines/BufferPipeline.h>
#include <Oxygen/Cooker/Import/Internal/Utils/BufferViews.h>
#include <Oxygen/Cooker/Import/Internal/Utils/SourceReadDiagnostics.h>
#include <Oxygen/Cooker/Import/Internal/Utils/VirtualPathResolution.h>
#include <Oxygen/Core/Types/DescriptorExtensions.h>
#include <Oxygen/Data/CookedFormatError.h>
#include <Oxygen/Data/PakFormat_core.h>
#include <Oxygen/Data/ResourceDescriptor.h>
#include <Oxygen/OxCo/Co.h>

namespace oxygen::content::import::detail {

namespace {

  auto AddDiagnostic(ImportSession& session, const ImportRequest& request,
    const ImportSeverity severity, std::string code, std::string message,
    std::string object_path = {}) -> void
  {
    session.AddDiagnostic({
      .severity = severity,
      .code = std::move(code),
      .message = std::move(message),
      .source_path = request.source_path.string(),
      .object_path = std::move(object_path),
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
        issue.message, issue.object_path);
    }
  }

  [[nodiscard]] auto MakeDuration(
    const std::chrono::steady_clock::time_point start,
    const std::chrono::steady_clock::time_point end)
    -> std::chrono::microseconds
  {
    return std::chrono::duration_cast<std::chrono::microseconds>(end - start);
  }

  [[nodiscard]] auto NormalizeRelPath(const std::string& relpath) -> std::string
  {
    return std::filesystem::path(relpath).lexically_normal().generic_string();
  }

  //! The path of each buffer sidecar the cooked root's published index lists,
  //! by the buffer row it describes; empty for a root nothing was cooked into.
  /*!
   @throw data::CookedFormatError When the root or a sidecar cannot be read.
  */
  [[nodiscard]] auto LoadCanonicalBufferSidecarRelpathsByIndex(
    const cooked::CookerStorageReader& storage,
    const std::filesystem::path& cooked_root)
    -> std::unordered_map<uint32_t, std::string>
  {
    auto content = std::optional<cooked::CookedContent> {};
    try {
      content.emplace(cooked::CookedContent::Open(storage, cooked_root));
    } catch (const data::CookedFormatError& error) {
      if (error.Defect() == data::CookedDefect::kAbsent) {
        return {};
      }
      throw;
    }

    auto by_index = std::unordered_map<uint32_t, std::string> {};
    // In path order, so the first path of a row is its canonical one.
    for (const auto& sidecar : content->Sidecars()) {
      if (!sidecar.name.ends_with(data::kBufferDescriptorExtension)) {
        continue;
      }
      const auto bytes = cooked::ReadWholeFile(
        storage, sidecar.stored.file, kMaxImportMetadataBytes);
      if (!bytes) {
        throw data::CookedFormatError(data::CookedDefect::kTruncated,
          "the buffer sidecar '" + sidecar.name
            + "' cannot be read: " + bytes.error().message());
      }
      auto decoded = data::DecodeBufferResourceDescriptor(*bytes);
      if (!decoded) {
        throw std::move(decoded).error();
      }
      by_index.try_emplace(decoded->index.get(), sidecar.name);
    }
    return by_index;
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

auto BufferImportSubmitter::SubmitBuffers(
  const std::span<const internal::BufferSource> entries,
  BufferPipeline& pipeline) -> co::Co<Submission>
{
  auto submission = Submission {};

  if (reader_ == nullptr) {
    AddDiagnostic(*session_, *request_, ImportSeverity::kError,
      "buffer.container.reader_unavailable",
      "Async file reader is not available");
    co_return submission;
  }

  if (entries.empty()) {
    AddDiagnostic(*session_, *request_, ImportSeverity::kError,
      "buffer.container.no_buffers", "No valid buffer entries were produced");
    co_return submission;
  }

  if (session_->HasErrors()) {
    co_return submission;
  }

  submission.descriptor_relpath_by_source_id.reserve(entries.size());
  submission.descriptor_views_by_source_id.reserve(entries.size());
  // Resolve all destinations before starting source reads or pipeline work.
  for (const auto& entry : entries) {
    auto relpath = std::string {};
    // A product is named relative to its container, so it lives in the
    // container's mount; an absolute product elsewhere has no cooked root here.
    if (!internal::TryVirtualPathToRelPath(
          *request_, entry.source_id, relpath)) {
      AddDiagnostic(*session_, *request_, ImportSeverity::kError,
        "buffer.container.virtual_path_unmounted",
        "Buffer virtual_path '" + entry.source_id
          + "' is outside the mount the container cooks into",
        entry.object_path + ".virtual_path");
      continue;
    }
    submission.descriptor_relpath_by_source_id.emplace(
      entry.source_id, std::move(relpath));
  }
  if (session_->HasErrors()) {
    co_return submission;
  }
  for (const auto& entry : entries) {
    const auto read_start = std::chrono::steady_clock::now();
    auto read_result = co_await reader_->ReadFile(entry.source_path);
    session_->AddSourceLoadDuration(
      MakeDuration(read_start, std::chrono::steady_clock::now()));
    if (!read_result.has_value()) {
      if (auto too_large = SourceTooLargeDiagnostic(
            read_result.error(), entry.source_path, reader_->SourceLimit())) {
        session_->AddDiagnostic(std::move(*too_large));
      } else {
        AddDiagnostic(*session_, *request_, ImportSeverity::kError,
          "buffer.container.source_read_failed",
          "Failed reading buffer source '" + entry.source_path.string()
            + "': " + read_result.error().message(),
          entry.source_id);
      }
      continue;
    }

    const auto source_size = read_result.value().Size().get();
    if (source_size == 0U) {
      AddDiagnostic(*session_, *request_, ImportSeverity::kError,
        "buffer.container.source_empty",
        "Buffer source '" + entry.source_path.string() + "' holds no bytes",
        entry.object_path + ".source");
      continue;
    }
    if (source_size > std::numeric_limits<uint32_t>::max()) {
      AddDiagnostic(*session_, *request_, ImportSeverity::kError,
        "buffer.container.source_too_large",
        "Buffer source exceeds maximum supported size of 4 GiB",
        entry.object_path + ".source");
      continue;
    }

    auto descriptor = data::pak::core::BufferResourceDesc {};
    descriptor.size_bytes = static_cast<uint32_t>(source_size);
    descriptor.usage_flags = entry.usage_flags;
    descriptor.element_stride = entry.element_stride;
    descriptor.element_format = entry.element_format;

    auto view_issues = std::vector<internal::BufferDescriptorViewIssue> {};
    auto normalized_views = internal::NormalizeBufferViews(
      entry.view_specs, descriptor, entry.object_path + ".views", view_issues);
    AddViewIssues(*session_, *request_, view_issues);
    if (!view_issues.empty()) {
      continue;
    }

    submission.descriptor_views_by_source_id.emplace(
      entry.source_id, std::move(normalized_views));

    // The payload type is shared with cookers that build their bytes.
    const auto source_bytes = read_result.value().Bytes();
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
    ++submission.submitted_count;
  }

  co_return submission;
}

auto BufferImportSubmitter::CollectAndEmit(BufferPipeline& pipeline,
  const Submission& submission) -> co::Co<std::vector<EmittedBuffer>>
{
  auto emitted_buffers = std::vector<EmittedBuffer> {};
  emitted_buffers.reserve(submission.submitted_count);
  auto canonical_relpath_by_index
    = std::unordered_map<uint32_t, std::string> {};
  try {
    canonical_relpath_by_index = LoadCanonicalBufferSidecarRelpathsByIndex(
      *session_->Storage(), session_->CookedRoot());
  } catch (const data::CookedFormatError& error) {
    AddDiagnostic(*session_, *request_, ImportSeverity::kError,
      "buffer.container.cooked_root_unreadable",
      "The cooked root's buffer sidecars cannot be read: "
        + std::string(error.what()),
      session_->CookedRoot().string());
    co_return emitted_buffers;
  }

  for (size_t i = 0; i < submission.submitted_count; ++i) {
    auto result = co_await pipeline.Collect();

    if (result.telemetry.cook_duration.has_value()) {
      session_->AddCookDuration(*result.telemetry.cook_duration);
    }
    AddDiagnostics(*session_, std::move(result.diagnostics));
    if (!result.success) {
      continue;
    }

    const auto relpath_it
      = submission.descriptor_relpath_by_source_id.find(result.source_id);
    if (relpath_it == submission.descriptor_relpath_by_source_id.end()) {
      AddDiagnostic(*session_, *request_, ImportSeverity::kError,
        "buffer.container.internal_lookup_failed",
        "Internal buffer entry lookup failed", result.source_id);
      continue;
    }

    const auto views_it
      = submission.descriptor_views_by_source_id.find(result.source_id);
    if (views_it == submission.descriptor_views_by_source_id.end()) {
      AddDiagnostic(*session_, *request_, ImportSeverity::kError,
        "buffer.container.internal_lookup_failed",
        "Internal buffer views lookup failed", result.source_id);
      continue;
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
        "buffer.container.descriptor_missing",
        "Missing buffer descriptor for emitted resource index",
        result.source_id);
      continue;
    }

    const auto requested_relpath = NormalizeRelPath(relpath_it->second);
    const auto existing = canonical_relpath_by_index.find(emitted_index);
    if (existing != canonical_relpath_by_index.end()
      && existing->second != requested_relpath) {
      AddDiagnostic(*session_, *request_, ImportSeverity::kError,
        "buffer.container.dedup_virtual_path_conflict",
        "Equivalent buffers deduped to one resource index must share one "
        "canonical virtual_path",
        result.source_id);
      continue;
    }
    canonical_relpath_by_index.insert_or_assign(
      emitted_index, requested_relpath);

    try {
      [[maybe_unused]] const auto relpath
        = session_->ResourceDescriptorEmitter().EmitBufferAtRelPath(
          requested_relpath, data::pak::core::ResourceIndexT { emitted_index },
          *descriptor, views_it->second);
    } catch (const std::exception& ex) {
      AddDiagnostic(*session_, *request_, ImportSeverity::kError,
        "buffer.container.sidecar_emit_failed",
        "Failed to emit buffer sidecar descriptor: " + std::string(ex.what()),
        result.source_id);
    }

    emitted_buffers.push_back(EmittedBuffer {
      .source_id = result.source_id,
      .descriptor_relpath = requested_relpath,
      .resource_index = data::pak::core::ResourceIndexT { emitted_index },
      .descriptor = *descriptor,
      .views = views_it->second,
    });
  }

  co_return emitted_buffers;
}

} // namespace oxygen::content::import::detail
