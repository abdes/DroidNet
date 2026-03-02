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
#include <limits>
#include <nlohmann/json-schema.hpp>
#include <nlohmann/json.hpp>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

#include <Oxygen/Cooker/Import/BufferImportTypes.h>
#include <Oxygen/Cooker/Import/IAsyncFileReader.h>
#include <Oxygen/Cooker/Import/ImportDiagnostics.h>
#include <Oxygen/Cooker/Import/ImportRequest.h>
#include <Oxygen/Cooker/Import/Internal/Emitters/BufferEmitter.h>
#include <Oxygen/Cooker/Import/Internal/Emitters/ResourceDescriptorEmitter.h>
#include <Oxygen/Cooker/Import/Internal/ImportManifest_schema.h>
#include <Oxygen/Cooker/Import/Internal/ImportSession.h>
#include <Oxygen/Cooker/Import/Internal/Jobs/BufferImportSubmitter.h>
#include <Oxygen/Cooker/Import/Internal/Utils/JsonSchemaValidation.h>
#include <Oxygen/Cooker/Import/Internal/Utils/VirtualPathResolution.h>

namespace oxygen::content::import::detail {

namespace {

  using nlohmann::json;
  using nlohmann::json_schema::json_validator;

  struct ParsedBufferEntry final {
    std::filesystem::path source_path;
    std::string source_id;
    std::string object_path;
    std::string descriptor_relpath;
    std::vector<internal::BufferDescriptorViewSpec> view_specs;
    uint32_t usage_flags = 0;
    uint32_t element_stride = 1;
    uint8_t element_format = 0;
    uint64_t alignment = 16;
    uint64_t content_hash = 0;
  };

  auto GetBufferEntryValidator() -> json_validator&
  {
    static auto validator = []() {
      auto out = json_validator {};
      const auto root_schema = json::parse(kBufferContainerSchema);
      auto item_schema = json {
        { "$schema", "http://json-schema.org/draft-07/schema#" },
        { "definitions", root_schema.at("definitions") },
        { "$ref", "#/definitions/buffer_descriptor" },
      };
      out.set_root_schema(item_schema);
      return out;
    }();
    return validator;
  }

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

  auto ValidateBufferChunkSchema(ImportSession& session,
    const ImportRequest& request, const nlohmann::json& chunk,
    const std::string_view object_path) -> bool
  {
    const auto config = internal::JsonSchemaValidationDiagnosticConfig {
      .validation_failed_code = "buffer.container.schema_validation_failed",
      .validation_failed_prefix = "Buffer chunk validation failed: ",
      .validation_overflow_prefix = "Buffer container validation emitted ",
      .validator_failure_code = "buffer.container.schema_validator_failure",
      .validator_failure_prefix = "Buffer container schema validator failed: ",
      .max_issues = 12,
    };

    return internal::ValidateJsonSchemaWithDiagnostics(
      GetBufferEntryValidator(), chunk, config,
      [&](const std::string_view code, const std::string& message,
        const std::string& chunk_object_path) {
        auto full_object_path = std::string(object_path);
        if (!chunk_object_path.empty()) {
          full_object_path += chunk_object_path.front() == '/'
            ? chunk_object_path
            : "." + chunk_object_path;
        }
        AddDiagnostic(session, request, ImportSeverity::kError,
          std::string(code), message, full_object_path);
      });
  }

  auto ParseBufferEntries(ImportSession& session, const ImportRequest& request,
    const nlohmann::json& buffer_chunks,
    const std::filesystem::path& descriptor_dir,
    const std::string_view object_path_prefix) -> std::vector<ParsedBufferEntry>
  {
    auto entries = std::vector<ParsedBufferEntry> {};
    auto seen_source_ids = std::unordered_set<std::string> {};

    if (!buffer_chunks.is_array()) {
      AddDiagnostic(session, request, ImportSeverity::kError,
        "buffer.container.buffers_missing",
        "Descriptor must contain an array 'buffers'");
      return entries;
    }

    entries.reserve(buffer_chunks.size());

    for (size_t i = 0; i < buffer_chunks.size(); ++i) {
      const auto object_path
        = std::string(object_path_prefix) + "[" + std::to_string(i) + "]";
      const auto& buffer_doc = buffer_chunks[i];

      if (!ValidateBufferChunkSchema(
            session, request, buffer_doc, object_path)) {
        continue;
      }

      auto entry = ParsedBufferEntry {};
      entry.object_path = object_path;
      const auto source_text = buffer_doc.at("source").get<std::string>();
      auto source_path = std::filesystem::path(source_text);
      if (source_path.is_relative()) {
        source_path = descriptor_dir / source_path;
      }
      entry.source_path = source_path.lexically_normal();

      entry.source_id = buffer_doc.at("virtual_path").get<std::string>();
      if (!seen_source_ids.insert(entry.source_id).second) {
        AddDiagnostic(session, request, ImportSeverity::kError,
          "buffer.container.virtual_path_duplicate",
          "Duplicate buffer virtual_path in descriptor",
          object_path + ".virtual_path");
        continue;
      }

      if (!internal::TryVirtualPathToRelPath(
            request, entry.source_id, entry.descriptor_relpath)) {
        AddDiagnostic(session, request, ImportSeverity::kError,
          "buffer.container.virtual_path_unmounted",
          "Buffer virtual_path is outside mounted cooked roots",
          object_path + ".virtual_path");
        continue;
      }

      if (buffer_doc.contains("usage_flags")) {
        entry.usage_flags = buffer_doc.at("usage_flags").get<uint32_t>();
      }
      if (buffer_doc.contains("element_stride")) {
        entry.element_stride = buffer_doc.at("element_stride").get<uint32_t>();
      }
      if (buffer_doc.contains("element_format")) {
        const auto value = buffer_doc.at("element_format").get<uint32_t>();
        entry.element_format = static_cast<uint8_t>(value);
        if (!buffer_doc.contains("element_stride")) {
          // Format-driven descriptors use stride derived from format metadata.
          entry.element_stride = 0;
        }
      }
      if (buffer_doc.contains("alignment")) {
        entry.alignment = buffer_doc.at("alignment").get<uint64_t>();
      }
      if (buffer_doc.contains("content_hash")) {
        entry.content_hash = buffer_doc.at("content_hash").get<uint64_t>();
      }

      auto view_issues = std::vector<internal::BufferDescriptorViewIssue> {};
      entry.view_specs = internal::ParseBufferViewSpecs(
        buffer_doc, object_path + ".views", view_issues);
      AddViewIssues(session, request, view_issues);
      if (!view_issues.empty()) {
        continue;
      }

      entries.push_back(std::move(entry));
    }

    return entries;
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

auto BufferImportSubmitter::SubmitBufferChunks(
  const nlohmann::json& buffer_chunks,
  const std::filesystem::path& descriptor_dir, BufferPipeline& pipeline,
  const std::string_view object_path_prefix) -> co::Co<Submission>
{
  auto submission = Submission {};

  if (reader_ == nullptr) {
    AddDiagnostic(session_, request_, ImportSeverity::kError,
      "buffer.container.reader_unavailable",
      "Async file reader is not available");
    co_return submission;
  }

  auto entries = ParseBufferEntries(
    session_, request_, buffer_chunks, descriptor_dir, object_path_prefix);
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

    submission.descriptor_relpath_by_source_id.emplace(
      entry.source_id, entry.descriptor_relpath);
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
