//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <filesystem>
#include <memory>
#include <optional>
#include <utility>
#include <vector>

#include <Oxygen/Base/ObserverPtr.h>
#include <Oxygen/Cooker/Import/ImportDiagnostics.h>
#include <Oxygen/Cooker/Import/ImportRequest.h>
#include <Oxygen/Cooker/Import/ImportSourceDocument.h>
#include <Oxygen/Cooker/Import/Internal/MaterialSource.h>
#include <Oxygen/Cooker/Import/Internal/Utils/RequestPreparation.h>
#include <Oxygen/Cooker/Import/MaterialDescriptorImportRequestBuilder.h>
#include <Oxygen/Cooker/Import/MaterialDescriptorImportSettings.h>

namespace oxygen::content::import::internal {

auto BuildMaterialDescriptorRequest(
  const MaterialDescriptorImportSettings& settings,
  std::vector<ImportDiagnostic>& diagnostics,
  std::shared_ptr<const CapturedInputSet> captured_inputs)
  -> std::optional<ImportRequest>
{
  const auto descriptor_path
    = std::filesystem::path(settings.descriptor_path).lexically_normal();
  auto document = ImportSourceDocument::Load(
    descriptor_path, "material", diagnostics, captured_inputs.get());
  if (!document.has_value()) {
    return std::nullopt;
  }

  const auto prepared = MaterialSource::FromDescriptor(
    document->text, descriptor_path, settings.job_name, diagnostics);
  if (!prepared) {
    return std::nullopt;
  }

  auto request = StartDescriptorRequest(
    {
      .descriptor_path = make_observer(&descriptor_path),
      .document = make_observer(&*document),
      .cooked_root = settings.cooked_root,
      .cooked_context_roots = settings.cooked_context_roots,
      .captured_inputs = std::move(captured_inputs),
    },
    diagnostics);
  if (!request.has_value()) {
    return std::nullopt;
  }

  request->job_name = prepared->name;
  request->material_descriptor = ImportRequest::MaterialDescriptorPayload {
    .descriptor_json = std::move(document->text),
  };
  return request;
}

} // namespace oxygen::content::import::internal
