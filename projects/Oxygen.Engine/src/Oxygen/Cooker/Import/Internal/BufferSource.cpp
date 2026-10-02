//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_set>
#include <utility>
#include <vector>

#include <nlohmann/json-schema.hpp>
#include <nlohmann/json.hpp>
#include <nlohmann/json_fwd.hpp>

#include <Oxygen/Cooker/Import/ImportDiagnostics.h>
#include <Oxygen/Cooker/Import/Internal/BufferSource.h>
#include <Oxygen/Cooker/Import/Internal/ImportManifest_schema.h>
#include <Oxygen/Cooker/Import/Internal/Utils/BufferDescriptorSidecar.h>
#include <Oxygen/Cooker/Import/Internal/Utils/JsonSchemaValidation.h>

namespace oxygen::content::import::internal {
namespace {
  using nlohmann::json;
  using nlohmann::json_schema::json_validator;

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

  auto AddDiagnostic(std::vector<ImportDiagnostic>& diagnostics,
    const std::filesystem::path& descriptor_path, const ImportSeverity severity,
    std::string code, std::string message, std::string object_path = {}) -> void
  {
    diagnostics.push_back({
      .severity = severity,
      .code = std::move(code),
      .message = std::move(message),
      .source_path = descriptor_path.string(),
      .object_path = std::move(object_path),
    });
  }

  auto ValidateBufferChunkSchema(std::vector<ImportDiagnostic>& diagnostics,
    const std::filesystem::path& descriptor_path, const nlohmann::json& chunk,
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
        AddDiagnostic(diagnostics, descriptor_path, ImportSeverity::kError,
          std::string(code), message, full_object_path);
      });
  }

} // namespace

auto BufferSource::FromDeclarations(const nlohmann::json& buffer_chunks,
  const std::filesystem::path& descriptor_path,
  std::vector<ImportDiagnostic>& diagnostics,
  const std::string_view object_path_prefix)
  -> std::optional<std::vector<BufferSource>>
{
  const auto diagnostics_start = diagnostics.size();
  const auto descriptor_dir = descriptor_path.parent_path();
  auto entries = std::vector<BufferSource> {};
  auto seen_source_ids = std::unordered_set<std::string> {};

  if (!buffer_chunks.is_array()) {
    AddDiagnostic(diagnostics, descriptor_path, ImportSeverity::kError,
      "buffer.container.buffers_missing",
      "Descriptor must contain an array 'buffers'");
    return std::nullopt;
  }

  entries.reserve(buffer_chunks.size());

  for (size_t i = 0; i < buffer_chunks.size(); ++i) {
    const auto object_path
      = std::string(object_path_prefix) + "[" + std::to_string(i) + "]";
    const auto& buffer_doc = buffer_chunks.at(i);

    if (!ValidateBufferChunkSchema(
          diagnostics, descriptor_path, buffer_doc, object_path)) {
      continue;
    }

    auto entry = BufferSource {};
    entry.object_path = object_path;
    const auto source_text = buffer_doc.at("source").get<std::string>();
    auto source_path = std::filesystem::path(source_text);
    if (source_path.is_relative()) {
      source_path = descriptor_dir / source_path;
    }
    entry.source_path = source_path.lexically_normal();

    entry.source_id = buffer_doc.at("virtual_path").get<std::string>();
    if (!seen_source_ids.insert(entry.source_id).second) {
      AddDiagnostic(diagnostics, descriptor_path, ImportSeverity::kError,
        "buffer.container.virtual_path_duplicate",
        "Duplicate buffer virtual_path in descriptor",
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
    for (const auto& issue : view_issues) {
      AddDiagnostic(diagnostics, descriptor_path, ImportSeverity::kError,
        issue.code, issue.message, issue.object_path);
    }
    if (!view_issues.empty()) {
      continue;
    }

    entries.push_back(std::move(entry));
  }

  if (diagnostics.size() != diagnostics_start) {
    return std::nullopt;
  }
  return entries;
}

} // namespace oxygen::content::import::internal
