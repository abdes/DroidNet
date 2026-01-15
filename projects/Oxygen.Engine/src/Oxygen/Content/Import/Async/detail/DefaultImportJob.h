//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <Oxygen/Content/Import/Async/Detail/ImportJob.h>

namespace oxygen::content::import::detail {

//! Default placeholder job implementation.
/*!
 Provides a minimal job implementation that exercises `ImportSession` creation
 and finalization.

 This job exists as a bridge while format-specific jobs (FBX/GLB/etc.) are
 introduced.
*/
class DefaultImportJob final : public ImportJob {
public:
  using ImportJob::ImportJob;

private:
  [[nodiscard]] auto ExecuteAsync() -> co::Co<ImportReport> override;
};

} // namespace oxygen::content::import::detail
