//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <nlohmann/json.hpp>

#include <Oxygen/Base/ObserverPtr.h>
#include <Oxygen/Cooker/Import/BufferContainerImportRequestBuilder.h>
#include <Oxygen/Cooker/Import/BufferContainerImportSettings.h>
#include <Oxygen/Cooker/Import/ImportDiagnostics.h>
#include <Oxygen/Cooker/Import/ImportRequest.h>
#include <Oxygen/Cooker/Import/ImportSourceDocument.h>
#include <Oxygen/Cooker/Import/Internal/BufferSource.h>
#include <Oxygen/Cooker/Import/Internal/Utils/RequestPreparation.h>
#include <Oxygen/Cooker/Import/SourceMounts.h>

namespace oxygen::content::import::internal {

auto BuildBufferContainerRequest(const BufferContainerImportSettings& settings,
  const std::string_view identity, const MountTable& mounts,
  std::vector<ImportDiagnostic>& diagnostics,
  std::shared_ptr<const CapturedInputSet> captured_inputs)
  -> std::optional<ImportRequest>
{
  const auto descriptor_path
    = std::filesystem::path(settings.descriptor_path).lexically_normal();
  auto document = ImportSourceDocument::Load(
    descriptor_path, "buffer-container", diagnostics, captured_inputs.get());
  if (!document.has_value()) {
    return std::nullopt;
  }

  const auto sources = BufferSource::FromContainer(
    document->text, descriptor_path, identity, mounts, diagnostics);
  if (!sources.has_value()) {
    return std::nullopt;
  }

  auto request = StartDescriptorRequest(
    {
      .descriptor_path = make_observer(&descriptor_path),
      .document = make_observer(&*document),
      .cooked_root = settings.cooked_root,
      .cooked_context_roots = {},
      .captured_inputs = std::move(captured_inputs),
    },
    diagnostics);
  if (!request.has_value()) {
    return std::nullopt;
  }

  // `FromContainer` parsed and validated this document.
  const auto name
    = nlohmann::json::parse(document->text).value("name", std::string {});
  ApplyJobName(settings.job_name.empty() ? name : settings.job_name, *request);
  request->buffer_container = ImportRequest::BufferContainerPayload {
    .descriptor_json = std::move(document->text),
  };
  return request;
}

} // namespace oxygen::content::import::internal
