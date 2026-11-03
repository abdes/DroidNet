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

#include <Oxygen/Content/VirtualPath.h>
#include <Oxygen/Cooker/Import/ImportDiagnostics.h>
#include <Oxygen/Cooker/Import/Internal/BufferSource.h>
#include <Oxygen/Cooker/Import/Internal/ImportManifest_schema.h>
#include <Oxygen/Cooker/Import/Internal/Utils/BufferViews.h>
#include <Oxygen/Cooker/Import/Internal/Utils/JsonSchemaValidation.h>
#include <Oxygen/Cooker/Import/Internal/Utils/VirtualPathResolution.h>
#include <Oxygen/Cooker/Import/SourceMounts.h>

namespace oxygen::content::import::internal {
namespace {
  using nlohmann::json;
  using nlohmann::json_schema::json_validator;

  auto GetBufferEntryValidator() -> json_validator&
  {
    static auto validator = [] -> json_validator {
      const auto root_schema = json::parse(kBufferContainerSchema);
      return MakeJsonSchemaValidator(json {
        { "$schema", "http://json-schema.org/draft-07/schema#" },
        { "definitions", root_schema.at("definitions") },
        { "$ref", "#/definitions/buffer_descriptor" },
      });
    }();
    return validator;
  }

  auto AddDiagnostic(std::vector<ImportDiagnostic>& diagnostics,
    const std::filesystem::path& descriptor_path, const ImportSeverity severity,
    std::string code, std::string message,
    const json::json_pointer& pointer = json::json_pointer {}) -> void
  {
    diagnostics.push_back({
      .severity = severity,
      .code = std::move(code),
      .message = std::move(message),
      .source_path = descriptor_path.string(),
      .json_pointer = pointer.to_string(),
    });
  }

  auto ValidateBufferChunkSchema(std::vector<ImportDiagnostic>& diagnostics,
    const std::filesystem::path& descriptor_path, const nlohmann::json& chunk,
    const json::json_pointer& chunk_pointer) -> bool
  {
    const auto config = internal::JsonSchemaValidationDiagnosticConfig {
      .validation_failed_code = "buffer.container.schema_validation_failed",
      .validation_failed_prefix = "Buffer chunk validation failed: ",
      .validator_failure_code = "buffer.container.schema_validator_failure",
      .validator_failure_prefix = "Buffer container schema validator failed: ",
    };

    return internal::ValidateJsonSchemaWithDiagnostics(
      GetBufferEntryValidator(), chunk, config,
      [&](const std::string_view code, const std::string& message,
        const std::string& issue_pointer) -> void {
        // The validator's pointer is relative to the chunk.
        AddDiagnostic(diagnostics, descriptor_path, ImportSeverity::kError,
          std::string(code), message,
          chunk_pointer / json::json_pointer { issue_pointer });
      });
  }

  auto GetBufferContainerValidator() -> json_validator&
  {
    static auto validator
      = MakeJsonSchemaValidator(json::parse(kBufferContainerSchema));
    return validator;
  }

  auto ValidateContainerSchema(std::vector<ImportDiagnostic>& diagnostics,
    const std::filesystem::path& descriptor_path,
    const nlohmann::json& document) -> bool
  {
    const auto config = internal::JsonSchemaValidationDiagnosticConfig {
      .validation_failed_code = "buffer.container.schema_validation_failed",
      .validation_failed_prefix = "Buffer container validation failed: ",
      .validator_failure_code = "buffer.container.schema_validator_failure",
      .validator_failure_prefix = "Buffer container schema validator failed: ",
    };

    return internal::ValidateJsonSchemaWithDiagnostics(
      GetBufferContainerValidator(), document, config,
      [&](const std::string_view code, const std::string& message,
        const std::string& issue_pointer) -> void {
        AddDiagnostic(diagnostics, descriptor_path, ImportSeverity::kError,
          std::string(code), message, json::json_pointer { issue_pointer });
      });
  }

} // namespace

auto BufferSource::FromContainer(const std::string_view bytes,
  const std::filesystem::path& descriptor_path, const std::string_view anchor,
  const MountTable& mounts, std::vector<ImportDiagnostic>& diagnostics)
  -> std::optional<std::vector<BufferSource>>
{
  json document;
  try {
    document = json::parse(bytes);
  } catch (const json::exception& failure) {
    AddDiagnostic(diagnostics, descriptor_path, ImportSeverity::kError,
      "buffer.container.request_invalid_json", failure.what());
    return std::nullopt;
  }
  if (!document.is_object()) {
    AddDiagnostic(diagnostics, descriptor_path, ImportSeverity::kError,
      "buffer.container.request_invalid_json",
      "Buffer container must be a JSON object");
    return std::nullopt;
  }
  if (!ValidateContainerSchema(diagnostics, descriptor_path, document)) {
    return std::nullopt;
  }
  return FromDeclarations(
    document.at("buffers"), descriptor_path, anchor, mounts, diagnostics);
}

auto BufferSource::FromDeclarations(const nlohmann::json& buffer_chunks,
  const std::filesystem::path& descriptor_path, const std::string_view anchor,
  const MountTable& mounts, std::vector<ImportDiagnostic>& diagnostics,
  const json::json_pointer& buffers_pointer)
  -> std::optional<std::vector<BufferSource>>
{
  const auto diagnostics_start = diagnostics.size();
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
    const auto chunk_pointer = buffers_pointer / i;
    const auto& buffer_doc = buffer_chunks.at(i);

    if (!ValidateBufferChunkSchema(
          diagnostics, descriptor_path, buffer_doc, chunk_pointer)) {
      continue;
    }

    auto entry = BufferSource {};
    entry.json_pointer = chunk_pointer.to_string();
    const auto source_field = (chunk_pointer / "source").to_string();
    const auto located = mounts.LocateReference(descriptor_path,
      CommittedPath {
        .text = buffer_doc.at("source").get_ref<const std::string&>(),
        .json_pointer = source_field,
      },
      diagnostics);
    if (!located.has_value()) {
      continue;
    }
    entry.source_path = located->file;

    const auto product_field = chunk_pointer / "virtual_path";
    const auto& product_text
      = buffer_doc.at("virtual_path").get_ref<const std::string&>();
    const auto product = oxygen::content::ResolveReference(
      anchor, product_text, VirtualPathRuleSet::kAuthored);
    if (!product.has_value()) {
      diagnostics.push_back(RefuseReference(anchor, descriptor_path.string(),
        product_text, product.error(), product_field.to_string()));
      continue;
    }
    entry.source_id = *product;
    if (!seen_source_ids.insert(entry.source_id).second) {
      AddDiagnostic(diagnostics, descriptor_path, ImportSeverity::kError,
        "buffer.container.virtual_path_duplicate",
        "Duplicate buffer virtual_path in descriptor: " + entry.source_id,
        product_field);
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

    auto view_issues = std::vector<internal::BufferDescriptorViewIssue> {};
    entry.view_specs = internal::ParseBufferViewSpecs(
      buffer_doc, chunk_pointer / "views", view_issues);
    for (const auto& issue : view_issues) {
      AddDiagnostic(diagnostics, descriptor_path, ImportSeverity::kError,
        issue.code, issue.message, json::json_pointer { issue.json_pointer });
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
