//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <exception>
#include <string>
#include <utility>

#include <Oxygen/Cooker/Import/ImportDiagnostics.h>
#include <Oxygen/Cooker/Import/ImportRequest.h>
#include <Oxygen/Cooker/Import/Internal/ImportSession.h>
#include <Oxygen/Cooker/Import/Internal/Utils/MountedCookedSources.h>
#include <Oxygen/Cooker/Import/Internal/Utils/VirtualPathResolution.h>

namespace oxygen::content::import::internal {

auto LoadMountedCookedSources(ImportSession& session,
  const ImportRequest& request, const std::string_view diagnostic_prefix)
  -> std::vector<MountedCookedSource>
{
  auto mounts = std::vector<MountedCookedSource> {};
  for (const auto& root : BuildUniqueMountedCookedRoots(request)) {
    auto mount = MountedCookedSource {
      .root = root,
      .inspection = std::nullopt,
    };
    if (lc::Inspection::IsCookedSource(mount.root)) {
      try {
        auto inspection = lc::Inspection {};
        inspection.Load(mount.root);
        mount.inspection = std::move(inspection);
      } catch (const std::exception& ex) {
        session.AddDiagnostic({
          .severity = ImportSeverity::kError,
          .code = std::string(diagnostic_prefix) + "index_load_failed",
          .message = "Failed loading cooked index: " + std::string(ex.what()),
          .source_path = request.source_path.string(),
          .object_path = mount.root.string(),
        });
      }
    }
    mounts.push_back(std::move(mount));
  }
  return mounts;
}

} // namespace oxygen::content::import::internal
