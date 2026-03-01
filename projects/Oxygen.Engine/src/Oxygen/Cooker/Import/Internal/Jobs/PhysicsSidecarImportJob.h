//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <vector>

#include <Oxygen/Composition/TypedObject.h>
#include <Oxygen/Cooker/Import/Internal/ImportJob.h>

namespace oxygen::content::import {
class ImportSession;
} // namespace oxygen::content::import

namespace oxygen::content::import::detail {

//! Standalone physics-sidecar import job.
/*!
 Imports one scene physics sidecar request through the physics sidecar
 pipeline.
*/
class PhysicsSidecarImportJob final : public ImportJob {
  OXYGEN_TYPED(PhysicsSidecarImportJob)
public:
  using ImportJob::ImportJob;

private:
  struct LoadedSource final {
    bool success = false;
    std::vector<std::byte> bytes;
  };

  [[nodiscard]] auto ExecuteAsync() -> co::Co<ImportReport> override;

  [[nodiscard]] auto LoadSource(ImportSession& session) -> co::Co<LoadedSource>;

  [[nodiscard]] auto FinalizeSession(ImportSession& session)
    -> co::Co<ImportReport>;
};

} // namespace oxygen::content::import::detail
