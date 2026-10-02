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
#include <fstream>
#include <ios>
#include <limits>
#include <optional>
#include <span>
#include <stop_token>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include <Oxygen/Base/ObserverPtr.h>
#include <Oxygen/Cooker/Import/BufferImportTypes.h>
#include <Oxygen/Cooker/Import/IAsyncFileReader.h>
#include <Oxygen/Cooker/Import/ImportDiagnostics.h>
#include <Oxygen/Cooker/Import/ImportRequest.h>
#include <Oxygen/Cooker/Import/Internal/BufferSource.h>
#include <Oxygen/Cooker/Import/Internal/Emitters/BufferEmitter.h>
#include <Oxygen/Cooker/Import/Internal/Emitters/ResourceDescriptorEmitter.h>
#include <Oxygen/Cooker/Import/Internal/ImportSession.h>
#include <Oxygen/Cooker/Import/Internal/Jobs/BufferImportSubmitter.h>
#include <Oxygen/Cooker/Import/Internal/Pipelines/BufferPipeline.h>
#include <Oxygen/Cooker/Import/Internal/Utils/BufferDescriptorSidecar.h>
#include <Oxygen/Cooker/Import/Internal/Utils/VirtualPathResolution.h>
#include <Oxygen/Data/PakFormat_core.h>
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

  [[nodiscard]] auto NormalizeRelPath(std::string relpath) -> std::string
  {
    return std::filesystem::path(std::move(relpath))
      .lexically_normal()
      .generic_string();
  }

  [[nodiscard]] auto ReadBinaryFile(const std::filesystem::path& path)
    -> std::optional<std::vector<std::byte>>
  {
    auto in = std::ifstream(path, std::ios::binary | std::ios::ate);
    if (!in.is_open()) {
      return std::nullopt;
    }
    const auto end = in.tellg();
    if (end < 0) {
      return std::nullopt;
    }
    const auto size = static_cast<size_t>(end);
    in.seekg(0, std::ios::beg);

    auto bytes = std::vector<std::byte>(size);
    if (size > 0U) {
      in.read(reinterpret_cast<char*>(bytes.data()),
        static_cast<std::streamsize>(size));
      if (!in) {
        return std::nullopt;
      }
    }
    return bytes;
  }

  [[nodiscard]] auto LoadCanonicalBufferSidecarRelpathsByIndex(
    const std::filesystem::path& cooked_root)
    -> std::unordered_map<uint32_t, std::string>
  {
    auto sidecar_paths = std::vector<std::filesystem::path> {};
    auto ec = std::error_code {};
    if (!std::filesystem::exists(cooked_root, ec)) {
      return {};
    }

    for (auto it
      = std::filesystem::recursive_directory_iterator(cooked_root, ec);
      !ec && it != std::filesystem::recursive_directory_iterator {};
      it.increment(ec)) {
      if (ec) {
        break;
      }
      if (!it->is_regular_file(ec)) {
        continue;
      }
      if (it->path().extension() != ".obuf") {
        continue;
      }
      sidecar_paths.push_back(it->path());
    }

    std::ranges::sort(sidecar_paths, [](const auto& lhs, const auto& rhs) {
      return lhs.generic_string() < rhs.generic_string();
    });

    auto by_index = std::unordered_map<uint32_t, std::string> {};
    for (const auto& path : sidecar_paths) {
      const auto bytes = ReadBinaryFile(path);
      if (!bytes.has_value()) {
        continue;
      }

      auto parsed = internal::ParsedBufferDescriptorSidecar {};
      auto parse_error = std::string {};
      if (!internal::ParseBufferDescriptorSidecar(
            *bytes, parsed, parse_error)) {
        continue;
      }

      const auto relpath = std::filesystem::relative(path, cooked_root, ec);
      if (ec) {
        ec.clear();
        continue;
      }

      by_index.try_emplace(parsed.resource_index.get(),
        NormalizeRelPath(relpath.generic_string()));
    }

    return by_index;
  }

} // namespace

BufferImportSubmitter::BufferImportSubmitter(ImportSession& session,
  const ImportRequest& request, const observer_ptr<IAsyncFileReader> reader,
  const std::stop_token stop_token)
  : session_(session)
  , request_(request)
  , reader_(reader)
  , stop_token_(stop_token)
{
}

auto BufferImportSubmitter::SubmitBuffers(
  const std::span<const internal::BufferSource> entries,
  BufferPipeline& pipeline) -> co::Co<Submission>
{
  auto submission = Submission {};

  if (reader_ == nullptr) {
    AddDiagnostic(session_, request_, ImportSeverity::kError,
      "buffer.container.reader_unavailable",
      "Async file reader is not available");
    co_return submission;
  }

  if (entries.empty()) {
    AddDiagnostic(session_, request_, ImportSeverity::kError,
      "buffer.container.no_buffers", "No valid buffer entries were produced");
    co_return submission;
  }

  if (session_.HasErrors()) {
    co_return submission;
  }

  submission.descriptor_relpath_by_source_id.reserve(entries.size());
  submission.descriptor_views_by_source_id.reserve(entries.size());
  // Resolve all destinations before starting source reads or pipeline work.
  for (const auto& entry : entries) {
    auto relpath = std::string {};
    if (!internal::TryVirtualPathToRelPath(
          request_, entry.source_id, relpath)) {
      AddDiagnostic(session_, request_, ImportSeverity::kError,
        "buffer.container.virtual_path_unmounted",
        "Buffer virtual_path is outside mounted cooked roots",
        entry.object_path + ".virtual_path");
      continue;
    }
    submission.descriptor_relpath_by_source_id.emplace(
      entry.source_id, std::move(relpath));
  }
  if (session_.HasErrors()) {
    co_return submission;
  }
  for (const auto& entry : entries) {
    const auto read_start = std::chrono::steady_clock::now();
    auto read_result = co_await reader_->ReadFile(entry.source_path);
    session_.AddSourceLoadDuration(
      MakeDuration(read_start, std::chrono::steady_clock::now()));
    if (!read_result.has_value()) {
      AddDiagnostic(session_, request_, ImportSeverity::kError,
        "buffer.container.source_read_failed",
        "Failed reading buffer source: " + read_result.error().ToString(),
        entry.source_id);
      continue;
    }

    const auto source_size = read_result.value().size();
    if (source_size > (std::numeric_limits<uint32_t>::max)()) {
      AddDiagnostic(session_, request_, ImportSeverity::kError,
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
    descriptor.content_hash = entry.content_hash;

    auto view_issues = std::vector<internal::BufferDescriptorViewIssue> {};
    auto normalized_views = internal::NormalizeBufferViews(
      entry.view_specs, descriptor, entry.object_path + ".views", view_issues);
    AddViewIssues(session_, request_, view_issues);
    if (!view_issues.empty()) {
      continue;
    }

    submission.descriptor_views_by_source_id.emplace(
      entry.source_id, std::move(normalized_views));

    auto item = BufferPipeline::WorkItem {};
    item.source_id = entry.source_id;
    item.cooked = CookedBufferPayload {
      .data = std::move(read_result.value()),
      .alignment = entry.alignment,
      .usage_flags = entry.usage_flags,
      .element_stride = entry.element_stride,
      .element_format = entry.element_format,
      .content_hash = entry.content_hash,
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
  const auto cooked_root = request_.cooked_root.has_value()
    ? request_.cooked_root.value()
    : request_.source_path.parent_path();
  auto canonical_relpath_by_index
    = LoadCanonicalBufferSidecarRelpathsByIndex(cooked_root);

  for (size_t i = 0; i < submission.submitted_count; ++i) {
    auto result = co_await pipeline.Collect();

    if (result.telemetry.cook_duration.has_value()) {
      session_.AddCookDuration(*result.telemetry.cook_duration);
    }
    AddDiagnostics(session_, std::move(result.diagnostics));
    if (!result.success) {
      continue;
    }

    const auto relpath_it
      = submission.descriptor_relpath_by_source_id.find(result.source_id);
    if (relpath_it == submission.descriptor_relpath_by_source_id.end()) {
      AddDiagnostic(session_, request_, ImportSeverity::kError,
        "buffer.container.internal_lookup_failed",
        "Internal buffer entry lookup failed", result.source_id);
      continue;
    }

    const auto views_it
      = submission.descriptor_views_by_source_id.find(result.source_id);
    if (views_it == submission.descriptor_views_by_source_id.end()) {
      AddDiagnostic(session_, request_, ImportSeverity::kError,
        "buffer.container.internal_lookup_failed",
        "Internal buffer views lookup failed", result.source_id);
      continue;
    }

    const auto emit_start = std::chrono::steady_clock::now();
    const auto emitted_index = session_.BufferEmitter().Emit(
      std::move(result.cooked), result.source_id);
    session_.AddEmitDuration(
      MakeDuration(emit_start, std::chrono::steady_clock::now()));

    const auto descriptor
      = session_.BufferEmitter().TryGetDescriptor(emitted_index);
    if (!descriptor.has_value()) {
      AddDiagnostic(session_, request_, ImportSeverity::kError,
        "buffer.container.descriptor_missing",
        "Missing buffer descriptor for emitted resource index",
        result.source_id);
      continue;
    }

    const auto requested_relpath = NormalizeRelPath(relpath_it->second);
    const auto existing = canonical_relpath_by_index.find(emitted_index);
    if (existing != canonical_relpath_by_index.end()
      && existing->second != requested_relpath) {
      AddDiagnostic(session_, request_, ImportSeverity::kError,
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
        = session_.ResourceDescriptorEmitter().EmitBufferAtRelPath(
          requested_relpath, data::pak::core::ResourceIndexT { emitted_index },
          *descriptor, views_it->second);
    } catch (const std::exception& ex) {
      AddDiagnostic(session_, request_, ImportSeverity::kError,
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
