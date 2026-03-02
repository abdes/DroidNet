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

//! Standalone collision-shape-descriptor import job.
/*!
 Imports one schema-validated collision shape descriptor and emits one
 `.ocshape` collision shape asset descriptor.
*/
class CollisionShapeDescriptorImportJob final : public ImportJob {
  OXYGEN_TYPED(CollisionShapeDescriptorImportJob)
public:
  using ImportJob::ImportJob;

private:
  [[nodiscard]] auto ExecuteAsync() -> co::Co<ImportReport> override;

  [[nodiscard]] auto FinalizeSession(ImportSession& session)
    -> co::Co<ImportReport>;
};

} // namespace oxygen::content::import::detail
