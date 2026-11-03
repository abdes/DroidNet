//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <filesystem>
#include <memory>
#include <optional>
#include <ostream>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <nlohmann/json.hpp>

#include <Oxygen/Cooker/Import/BufferContainerImportRequestBuilder.h>
#include <Oxygen/Cooker/Import/BufferContainerImportSettings.h>
#include <Oxygen/Cooker/Import/ImportDiagnostics.h>
#include <Oxygen/Cooker/Import/ImportRequest.h>
#include <Oxygen/Cooker/Import/ImportSourceDocument.h>
#include <Oxygen/Cooker/Import/Internal/BufferSource.h>
#include <Oxygen/Cooker/Import/Internal/Utils/DescriptorDocument.h>
#include <Oxygen/Cooker/Import/SourceMounts.h>

namespace oxygen::content::import::internal {

auto BuildBufferContainerRequest(const BufferContainerImportSettings& settings,
  const std::string_view identity, const MountTable& mounts,
  std::ostream& error_stream,
  std::shared_ptr<const CapturedInputSet> captured_inputs)
  -> std::optional<ImportRequest>
{
  if (settings.descriptor_path.empty()) {
    error_stream << "ERROR: descriptor_path is required\n";
    return std::nullopt;
  }

  const auto descriptor_path
    = std::filesystem::path(settings.descriptor_path).lexically_normal();
  auto document = ImportSourceDocument::Load(
    descriptor_path, "buffer-container", error_stream, captured_inputs.get());
  if (!document.has_value()) {
    return std::nullopt;
  }

  auto diagnostics = std::vector<ImportDiagnostic> {};
  const auto sources = BufferSource::FromContainer(
    document->text, descriptor_path, identity, mounts, diagnostics);
  WriteDiagnostics(diagnostics, error_stream);
  if (!sources.has_value()) {
    return std::nullopt;
  }

  auto request = ImportRequest {};
  request.source_path = descriptor_path;
  request.captured_inputs = std::move(captured_inputs);
  request.preparation_inputs.push_back(document->Observation());

  if (settings.cooked_root.empty()) {
    error_stream << "ERROR: --output or --cooked-root is required\n";
    return std::nullopt;
  }

  auto cooked_root = std::filesystem::path(settings.cooked_root);
  if (!cooked_root.is_absolute()) {
    error_stream << "ERROR: cooked root must be an absolute path\n";
    return std::nullopt;
  }
  request.cooked_root = std::move(cooked_root);

  if (!settings.job_name.empty()) {
    request.job_name = settings.job_name;
  } else if (const auto name_doc = nlohmann::json::parse(document->text);
    name_doc.contains("name")) {
    // `FromContainer` parsed and validated this document.
    request.job_name = name_doc.at("name").get<std::string>();
  } else {
    const auto stem = descriptor_path.stem().string();
    if (!stem.empty()) {
      request.job_name = stem;
    }
  }

  request.buffer_container = ImportRequest::BufferContainerPayload {
    .descriptor_json = std::move(document->text),
  };

  return request;
}

} // namespace oxygen::content::import::internal
