//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <Oxygen/Composition/TypedObject.h>
#include <Oxygen/Cooker/Import/Internal/ImportJob.h>

namespace oxygen::content::import {
class ImportSession;
} // namespace oxygen::content::import

namespace oxygen::content::import::detail {

//! Standalone physics-resource-descriptor import job.
/*!
 Imports one schema-validated physics resource descriptor and emits:
 - `Physics/Resources/physics.table`
 - `Physics/Resources/physics.data`
 - one `.opres` sidecar descriptor.
*/
class PhysicsResourceDescriptorImportJob final : public ImportJob {
  OXYGEN_TYPED(PhysicsResourceDescriptorImportJob)
public:
  using ImportJob::ImportJob;

private:
  [[nodiscard]] auto ExecuteAsync() -> co::Co<ImportReport> override;

  [[nodiscard]] auto FinalizeSession(ImportSession& session)
    -> co::Co<ImportReport>;
};

} // namespace oxygen::content::import::detail
