//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <span>
#include <vector>

#include <Oxygen/Base/ObserverPtr.h>
#include <Oxygen/Composition/TypedObject.h>
#include <Oxygen/Cooker/Import/ImportRequest.h>
#include <Oxygen/Cooker/Import/Internal/ImportJob.h>
#include <Oxygen/Cooker/Import/Internal/ScenePhysicsFollowers.h>

namespace oxygen::content::import {
class ImportSession;
} // namespace oxygen::content::import

namespace oxygen::content::import::detail {

//! Standalone scene-descriptor import job.
class SceneDescriptorImportJob final : public ImportJob {
  OXYGEN_TYPED(SceneDescriptorImportJob)
public:
  using ImportJob::ImportJob;

private:
  [[nodiscard]] auto ExecuteAsync() -> co::Co<ImportReport> override;

  [[nodiscard]] auto FinalizeSession(ImportSession& session)
    -> co::Co<ImportReport>;

  //! Cooks `followers` again, each in a companion session, for the scene in
  //! flight `scene`.
  /*!
   The scene and its sidecars commit together or not at all. A sidecar whose
   source cannot be located or read is reported on `session`, which fails the
   scene import.

   @return The companion sessions, to finalize with `session`.
  */
  [[nodiscard]] auto CookPhysicsFollowers(ImportSession& session,
    std::span<const internal::ScenePhysicsFollower> followers,
    const ImportRequest::InflightSceneContext& scene)
    -> co::Co<std::vector<observer_ptr<ImportSession>>>;
};

} // namespace oxygen::content::import::detail
