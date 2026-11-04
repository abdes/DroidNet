//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <optional>
#include <string>
#include <utility>
#include <vector>

#include <nlohmann/json.hpp>

#include <Oxygen/Cooker/Import/ImportDiagnostics.h>
#include <Oxygen/Cooker/Import/ImportOptions.h>
#include <Oxygen/Cooker/Import/ImportRequest.h>
#include <Oxygen/Cooker/Import/Internal/PhysicsSidecarSource.h>
#include <Oxygen/Cooker/Import/Internal/Utils/RequestPreparation.h>
#include <Oxygen/Cooker/Import/PhysicsImportRequestBuilder.h>
#include <Oxygen/Cooker/Import/PhysicsImportSettings.h>

namespace oxygen::content::import::internal {

auto BuildPhysicsSidecarRequest(const PhysicsSidecarImportSettings& settings,
  std::vector<ImportDiagnostic>& diagnostics) -> std::optional<ImportRequest>
{
  const auto has_source = !settings.source_path.empty();
  const auto has_inline = !settings.inline_bindings_json.empty();
  if (!CheckSidecarSource(has_source, has_inline, diagnostics)) {
    return std::nullopt;
  }

  auto request = ImportRequest {};
  request.source_path
    = has_source ? settings.source_path : "inline://physics-sidecar";

  auto normalized_inline_bindings = std::string {};
  if (has_inline) {
    if (!PreparePhysicsSidecarDocument(
          settings.inline_bindings_json, request.source_path, diagnostics)
          .has_value()) {
      return std::nullopt;
    }
    normalized_inline_bindings
      = nlohmann::json::parse(settings.inline_bindings_json).dump();
  }

  if (!ApplyCookedRoot(settings.cooked_root, request, diagnostics)
    || !CheckTargetScene(settings.target_scene_virtual_path, diagnostics)) {
    return std::nullopt;
  }
  ApplyJobName(settings.job_name, request);

  request.physics = PhysicsImportSettings {
    .target_scene_virtual_path = settings.target_scene_virtual_path,
    .inline_bindings_json = std::move(normalized_inline_bindings),
  };
  request.options.physics.backend = settings.physics_backend;
  return request;
}

} // namespace oxygen::content::import::internal
