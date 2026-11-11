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

#include <Oxygen/Base/ObserverPtr.h>
#include <Oxygen/Cooker/Import/ImportDiagnostics.h>
#include <Oxygen/Cooker/Import/ImportRequest.h>
#include <Oxygen/Cooker/Import/ImportSourceDocument.h>
#include <Oxygen/Cooker/Import/Internal/PhysicsMaterialSource.h>
#include <Oxygen/Cooker/Import/Internal/Utils/RequestPreparation.h>
#include <Oxygen/Cooker/Import/PhysicsMaterialDescriptorImportRequestBuilder.h>
#include <Oxygen/Cooker/Import/PhysicsMaterialDescriptorImportSettings.h>

namespace oxygen::content::import::internal {

auto BuildPhysicsMaterialDescriptorRequest(
  const PhysicsMaterialDescriptorImportSettings& settings,
  std::vector<ImportDiagnostic>& diagnostics,
  std::shared_ptr<const CapturedInputSet> captured_inputs)
  -> std::optional<ImportRequest>
{
  const auto descriptor_path
    = std::filesystem::path(settings.descriptor_path).lexically_normal();
  auto document = ImportSourceDocument::Load(descriptor_path,
    "physics material descriptor", diagnostics, captured_inputs.get());
  if (!document.has_value()) {
    return std::nullopt;
  }
  const auto material = PreparePhysicsMaterialDescriptor(
    document->text, descriptor_path, diagnostics);
  if (!material.has_value()) {
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

  ApplyJobName(
    settings.job_name.empty() ? material->name : settings.job_name, *request);
  request->physics_material_descriptor
    = ImportRequest::PhysicsMaterialDescriptorPayload {
        .descriptor_json = std::move(document->text),
      };
  return request;
}

} // namespace oxygen::content::import::internal
