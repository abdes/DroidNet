//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <optional>
#include <vector>

#include <Oxygen/Cooker/Import/ImportDiagnostics.h>
#include <Oxygen/Cooker/Import/ImportRequest.h>
#include <Oxygen/Cooker/Import/ImportRequestCodes.h>
#include <Oxygen/Cooker/Import/InputImportRequestBuilder.h>
#include <Oxygen/Cooker/Import/InputImportSettings.h>
#include <Oxygen/Cooker/Import/Internal/Utils/RequestPreparation.h>

namespace oxygen::content::import::internal {

auto BuildInputImportRequest(const InputImportSettings& settings,
  std::vector<ImportDiagnostic>& diagnostics) -> std::optional<ImportRequest>
{
  if (settings.source_path.empty()) {
    RefuseSetting(diagnostics, request_codes::kSourceMissing,
      "source_path is required", "/source");
    return std::nullopt;
  }

  auto request = ImportRequest {};
  request.source_path = settings.source_path;
  request.asset_virtual_path = settings.virtual_path;
  request.input = ImportRequest::InputPayload {};

  if (!ApplyCookedRoot(settings.cooked_root, request, diagnostics)
    || !ApplyCookedContextRoots(
      settings.cooked_context_roots, request, diagnostics)
    || !CheckInputSourceName(request.source_path, diagnostics)) {
    return std::nullopt;
  }
  return request;
}

} // namespace oxygen::content::import::internal
