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
#include <Oxygen/Cooker/Import/CollisionShapeDescriptorImportRequestBuilder.h>
#include <Oxygen/Cooker/Import/CollisionShapeDescriptorImportSettings.h>
#include <Oxygen/Cooker/Import/ImportDiagnostics.h>
#include <Oxygen/Cooker/Import/ImportRequest.h>
#include <Oxygen/Cooker/Import/ImportSourceDocument.h>
#include <Oxygen/Cooker/Import/Internal/CollisionShapeSource.h>
#include <Oxygen/Cooker/Import/Internal/Utils/RequestPreparation.h>

namespace oxygen::content::import::internal {

auto BuildCollisionShapeDescriptorRequest(
  const CollisionShapeDescriptorImportSettings& settings,
  std::vector<ImportDiagnostic>& diagnostics,
  std::shared_ptr<const CapturedInputSet> captured_inputs)
  -> std::optional<ImportRequest>
{
  const auto descriptor_path
    = std::filesystem::path(settings.descriptor_path).lexically_normal();
  auto document = ImportSourceDocument::Load(descriptor_path,
    "collision shape descriptor", diagnostics, captured_inputs.get());
  if (!document.has_value()) {
    return std::nullopt;
  }
  const auto shape = PrepareCollisionShapeDescriptor(
    document->text, descriptor_path, diagnostics);
  if (!shape.has_value()) {
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
    settings.job_name.empty() ? shape->name : settings.job_name, *request);
  request->collision_shape_descriptor
    = ImportRequest::CollisionShapeDescriptorPayload {
        .descriptor_json = std::move(document->text),
      };
  return request;
}

} // namespace oxygen::content::import::internal
